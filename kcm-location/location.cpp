// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <utility>

#include "location.h"

K_PLUGIN_CLASS_WITH_JSON(Location, "kcm_ultramarine_location.json")

#include "location.moc"

namespace {
    const auto SystemdService = QStringLiteral("org.freedesktop.systemd1");
    const auto SystemdPath = QStringLiteral("/org/freedesktop/systemd1");
    const auto ManagerInterface = QStringLiteral("org.freedesktop.systemd1.Manager");
    const auto GeoClueUnit = QStringLiteral("geoclue.service");

    // the polkit prompt is shown while the call is pending, so don't time out under the user
    constexpr int CallTimeoutMs = 10 * 60 * 1000;

    /// returns whether systemd refused a call because polkit didn't authorize it
    bool isAuthenticationFailure(const QDBusMessage &reply) {
        const QString name = reply.errorName();
        return name == QLatin1String("org.freedesktop.DBus.Error.AccessDenied") ||
            name == QLatin1String("org.freedesktop.DBus.Error.InteractiveAuthorizationRequired");
    }
} // namespace

LocationState parseUnitFileState(const QString &state) {
    if (state == QLatin1String("masked")) {
        return LocationState::Disabled;
    }
    if (state == QLatin1String("masked-runtime")) {
        return LocationState::DisabledRuntime;
    }
    if (state.isEmpty() || state == QLatin1String("not-found") || state == QLatin1String("bad")) {
        return LocationState::Unavailable;
    }
    // static (the usual, as GeoClue is D-Bus activated), enabled, disabled, …
    return LocationState::Enabled;
}

Location::Location(QObject *parent, const KPluginMetaData &metaData) :
    KQuickConfigModule(parent, metaData) {
    // the switch applies right away, like the Tweaks KCM
    setButtons(NoAdditionalButton);
}

bool Location::available() const { return m_state != LocationState::Unavailable; }

bool Location::locationEnabled() const { return m_state == LocationState::Enabled; }

bool Location::loading() const { return m_loading; }

bool Location::busy() const { return m_busy; }

QString Location::errorMessage() const { return m_errorMessage; }

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
        if (reply.isError()) {
            // NoSuchUnit when GeoClue isn't installed
            if (reply.error().name() != QLatin1String("org.freedesktop.systemd1.NoSuchUnit")) {
                setErrorMessage(i18n("Could not read the state of location services: %1", reply.error().message()));
            }
            setState(LocationState::Unavailable);
            return;
        }
        setState(parseUnitFileState(reply.value()));
    });
}

void Location::setLocationEnabled(const bool enabled) {
    if (m_busy || !available() || enabled == locationEnabled()) {
        return;
    }
    setErrorMessage({});
    setBusy(true);

    if (enabled) {
        // a mask in /run has to be removed from /run
        const bool runtime = m_state == LocationState::DisabledRuntime;
        callSystemd(QStringLiteral("UnmaskUnitFiles"), {QStringList{GeoClueUnit}, runtime},
                    [this](const QDBusMessage &) {
                        callSystemd(QStringLiteral("Reload"), {}, [this](const QDBusMessage &) { changeFinished(); });
                    });
        return;
    }

    // persistent, and forced over anything already at that path
    callSystemd(QStringLiteral("MaskUnitFiles"), {QStringList{GeoClueUnit}, false, true}, [this](const QDBusMessage &) {
        callSystemd(QStringLiteral("Reload"), {}, [this](const QDBusMessage &) {
            // stop it too, otherwise apps that already have a location keep getting updates until it idles out
            callSystemd(QStringLiteral("StopUnit"), {GeoClueUnit, QStringLiteral("replace")},
                        [this](const QDBusMessage &) { changeFinished(); });
        });
    });
}

void Location::callSystemd(const QString &method, const QVariantList &arguments,
                           const std::function<void(const QDBusMessage &)> &next) {
    auto message = QDBusMessage::createMethodCall(SystemdService, SystemdPath, ManagerInterface, method);
    message.setArguments(arguments);
    // lets systemd ask polkit to show an authentication dialog
    message.setInteractiveAuthorizationAllowed(true);
    const auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, CallTimeoutMs),
                                                      this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, next](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusMessage reply = call->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            callFailed(reply);
            return;
        }
        next(reply);
    });
}

void Location::callFailed(const QDBusMessage &reply) {
    if (isAuthenticationFailure(reply)) {
        setErrorMessage(i18n("Authentication was cancelled or failed. No changes were made."));
    } else {
        setErrorMessage(i18n("Could not change location services: %1", reply.errorMessage()));
    }
    // a step may have gone through before the failure, so show what's actually set
    changeFinished();
}

void Location::changeFinished() {
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

void Location::setLoading(const bool loading) {
    if (m_loading == loading) {
        return;
    }
    m_loading = loading;
    Q_EMIT loadingChanged();
}

void Location::setBusy(const bool busy) {
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
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
