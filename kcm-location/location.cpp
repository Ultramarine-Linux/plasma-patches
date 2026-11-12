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
    if (state == QLatin1String("bad")) {
        return LocationState::ReadFailed;
    }
    return LocationState::Enabled;
}

LocationState parseUnitFileStateError(const QString &errorName) {
    // systemd 259 says FileNotFound here, older systemd says NoSuchUnit
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
    if (exitStatus == QProcess::NormalExit && Pkexec::isAuthenticationFailure(exitCode)) {
        return i18n("Authentication was canceled or failed. No changes were made.");
    }
    const QString details = output.trimmed();
    if (details.isEmpty()) {
        return i18n("Could not change location services. The change may have been partly applied.");
    }
    return i18n("Could not change location services. The change may have been partly applied: %1", details);
}

Location::Location(QObject *parent, const KPluginMetaData &metaData) : KQuickConfigModule(parent, metaData) {
    setButtons(NoAdditionalButton);
}

Location::~Location() { detachRunningPkexec(); }

// ~QProcess would block System Settings for up to 30 seconds trying to kill a root process it can't kill
void Location::detachRunningPkexec() {
    if (m_runningPkexec && m_runningPkexec->state() != QProcess::NotRunning) {
        m_runningPkexec->disconnect(this);
        m_runningPkexec->setParent(nullptr);
        connect(m_runningPkexec, &QProcess::finished, m_runningPkexec, &QObject::deleteLater);
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
        m_reloadAfterCurrentRead = true;
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
        if (std::exchange(m_reloadAfterCurrentRead, false)) {
            load();
            return;
        }
        const QDBusPendingReply<QString> reply = *call;
        const LocationState state =
            reply.isError() ? parseUnitFileStateError(reply.error().name()) : parseUnitFileState(reply.value());
        if (state == LocationState::ReadFailed) {
            m_lastReadError = reply.isError()
                ? i18n("Could not read the state of location services: %1", reply.error().message())
                : i18n("Could not read the state of location services: systemd reports geoclue.service as “%1”.",
                       reply.value());
            setErrorMessage(m_lastReadError);
        } else if (const QString readError = std::exchange(m_lastReadError, {});
                   !readError.isEmpty() && m_errorMessage == readError) {
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
    m_runningPkexec = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
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
    if (m_runningPkexec) {
        output = QString::fromLocal8Bit(m_runningPkexec->readAll());
        m_runningPkexec->deleteLater();
    }
    setErrorMessage(helperErrorMessage(exitCode, exitStatus, output));
    setBusy(false);
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
