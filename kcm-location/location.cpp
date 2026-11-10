// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFileInfo>
#include <QStandardPaths>

#include <utility>

#include "detachedprocess.h"
#include "location.h"
#include "pkexec.h"

#ifndef ULTRAMARINE_LOCATION_HELPER
#define ULTRAMARINE_LOCATION_HELPER "/usr/libexec/ultramarine-location-helper"
#endif

K_PLUGIN_CLASS_WITH_JSON(Location, "kcm_ultramarine_location.json")

#include "location.moc"

namespace {
    const auto SystemdService = QStringLiteral("org.freedesktop.systemd1");
    const auto SystemdPath = QStringLiteral("/org/freedesktop/systemd1");
    const auto ManagerInterface = QStringLiteral("org.freedesktop.systemd1.Manager");
    const auto GeoClueUnit = QStringLiteral("geoclue.service");
} // namespace

LocationState parseUnitFileState(const QString &state) {
    if (state == QLatin1String("masked")) {
        return LocationState::Disabled;
    }
    if (state == QLatin1String("masked-runtime")) {
        return LocationState::DisabledRuntime;
    }
    if (state.isEmpty() || state == QLatin1String("not-found")) {
        return LocationState::Unavailable;
    }
    // the unit file exists but systemd couldn't parse it, so GeoClue is installed but broken
    if (state == QLatin1String("bad")) {
        return LocationState::ReadFailed;
    }
    // static (the usual, as GeoClue is D-Bus activated), enabled, disabled, …
    return LocationState::Enabled;
}

LocationState parseUnitFileStateError(const QString &errorName) {
    // GetUnitFileState looks the unit file up on disk, and systemd 259 says FileNotFound when
    // there's none. NoSuchUnit is what other manager calls (and older systemd) say.
    if (errorName == QLatin1String("org.freedesktop.DBus.Error.FileNotFound") ||
        errorName == QLatin1String("org.freedesktop.systemd1.NoSuchUnit")) {
        return LocationState::Unavailable;
    }
    return LocationState::ReadFailed;
}

QString helperErrorMessage(const int exitCode, const QProcess::ExitStatus exitStatus, const QString &output) {
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        return {};
    }
    // the helper never exits with these itself, so they're pkexec's, from before it ran
    if (exitStatus == QProcess::NormalExit && Pkexec::isAuthenticationFailure(exitCode)) {
        return i18n("Authentication was canceled or failed. No changes were made.");
    }
    // systemctl may have gotten partway, for example masking GeoClue but failing to stop it
    const QString details = output.trimmed();
    if (details.isEmpty()) {
        return i18n("Could not change location services. The change may have been partly applied.");
    }
    return i18n("Could not change location services. The change may have been partly applied: %1", details);
}

Location::Location(QObject *parent, const KPluginMetaData &metaData) : KQuickConfigModule(parent, metaData) {
    // the switch applies right away, like the Tweaks KCM
    setButtons(NoAdditionalButton);
}

Location::~Location() {
    // ~QProcess would kill pkexec and wait up to 30 seconds for it, freezing System Settings.
    // Once pkexec runs the helper as root it can't be killed from here anyway, and the
    // helper finishes by itself, so let it. SIGPIPE is ignored in it, so writing to the
    // pipe after System Settings quits doesn't kill it.
    if (m_helper && m_helper->state() != QProcess::NotRunning) {
        m_helper->disconnect(this);
        m_helper->setParent(nullptr);
        connect(m_helper, &QProcess::finished, m_helper, &QObject::deleteLater);
    }
}

bool Location::available() const {
    return m_state == LocationState::Enabled || m_state == LocationState::Disabled ||
        m_state == LocationState::DisabledRuntime;
}

bool Location::notInstalled() const { return m_state == LocationState::Unavailable; }

bool Location::readFailed() const { return m_state == LocationState::ReadFailed; }

bool Location::locationEnabled() const { return m_state == LocationState::Enabled; }

bool Location::loading() const { return m_loading; }

bool Location::busy() const { return m_busy; }

const QString &Location::errorMessage() const { return m_errorMessage; }

void Location::load() {
    if (m_loading) {
        // the state being read may predate a change
        m_reloadPending = true;
        return;
    }
    setLoading(true);
    auto message = QDBusMessage::createMethodCall(SystemdService, SystemdPath, ManagerInterface,
                                                  QStringLiteral("GetUnitFileState"));
    message << GeoClueUnit;
    const auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        setLoading(false);
        if (std::exchange(m_reloadPending, false)) {
            load();
            return;
        }
        const QDBusPendingReply<QString> reply = *call;
        const LocationState state =
            reply.isError() ? parseUnitFileStateError(reply.error().name()) : parseUnitFileState(reply.value());
        if (state == LocationState::ReadFailed) {
            m_readError = reply.isError()
                ? i18n("Could not read the state of location services: %1", reply.error().message())
                : i18n("Could not read the state of location services: systemd reports geoclue.service as “%1”.",
                       reply.value());
            setErrorMessage(m_readError);
        } else if (const QString readError = std::exchange(m_readError, {});
                   !readError.isEmpty() && m_errorMessage == readError) {
            // a helper error shown since then stays
            setErrorMessage({});
        }
        setState(state);
    });
}

void Location::setLocationEnabled(const bool enabled) {
    if (m_busy || !available() || enabled == locationEnabled()) {
        return;
    }
    setErrorMessage({});

    const QString helper = QStringLiteral(ULTRAMARINE_LOCATION_HELPER);
    if (!QFileInfo(helper).isExecutable()) {
        setErrorMessage(i18n("Could not change location services: %1 is not installed.", helper));
        return;
    }
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty()) {
        setErrorMessage(i18n("Could not change location services: pkexec is not installed."));
        return;
    }

    setBusy(true);
    auto *process = new QProcess(this);
    m_helper = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    // keep pkexec off System Settings' terminal, if it has one, so it asks through the
    // polkit agent instead of falling back to a text prompt. SIGPIPE is ignored too, so if
    // System Settings quits mid-change, systemctl printing "Created symlink" to the closed
    // pipe can't kill it before it stops GeoClue.
    DetachedProcess::detachFromTerminal(process);
    connect(process, &QProcess::finished, this, &Location::helperFinished);
    connect(process, &QProcess::errorOccurred, this, [this, process](const QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            setErrorMessage(i18n("Could not start pkexec."));
            setBusy(false);
            load();
        }
    });
    process->start(pkexec, {helper, enabled ? QStringLiteral("enable") : QStringLiteral("disable")});
}

void Location::helperFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    QString output;
    if (m_helper) {
        output = QString::fromLocal8Bit(m_helper->readAll());
        m_helper->deleteLater();
    }
    setErrorMessage(helperErrorMessage(exitCode, exitStatus, output));
    setBusy(false);
    // show what's actually set, even after a failure
    load();
}

void Location::setState(const LocationState state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged();
}

void Location::setLoading(const bool value) {
    if (m_loading == value) {
        return;
    }
    m_loading = value;
    Q_EMIT loadingChanged();
}

void Location::setBusy(const bool value) {
    if (m_busy == value) {
        return;
    }
    m_busy = value;
    Q_EMIT busyChanged();
}

void Location::clearError() { setErrorMessage({}); }

void Location::setErrorMessage(const QString &message) {
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    Q_EMIT errorMessageChanged();
}
