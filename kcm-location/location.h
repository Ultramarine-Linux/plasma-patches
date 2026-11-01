// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickConfigModule>

#include <QDBusMessage>

#include <cstdint>
#include <functional>

/// whether apps can ask GeoClue for the device's location
enum class LocationState : std::uint8_t {
    Enabled,
    /// geoclue.service is masked in /etc
    Disabled,
    /// geoclue.service is masked in /run, until the next boot
    DisabledRuntime,
    /// GeoClue isn't installed
    Unavailable,
};

/// maps systemd's unit file state for geoclue.service (as from GetUnitFileState) to a LocationState
[[nodiscard]] LocationState parseUnitFileState(const QString &state);

/**
 * Location Services: turns GeoClue on or off for the whole system by masking geoclue.service.
 * GeoClue is started over D-Bus whenever an app asks for it, so a mask is what keeps it off.
 * Taidan (the Ultramarine OOBE) does the same when location is turned off during setup.
 */
class Location : public KQuickConfigModule {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool locationEnabled READ locationEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    /// creates the Location Services configuration module
    explicit Location(QObject *parent, const KPluginMetaData &metaData);

    /// returns whether GeoClue is installed
    [[nodiscard]] bool available() const;

    /// returns whether apps can ask for the device's location
    [[nodiscard]] bool locationEnabled() const;

    /// returns whether the state is being read from systemd
    [[nodiscard]] bool loading() const;

    /// returns whether location services are being turned on or off
    [[nodiscard]] bool busy() const;

    /// returns the current error shown in the UI banner
    [[nodiscard]] QString errorMessage() const;

    /// reads geoclue.service's state again, called when the page is shown
    void load() override;

    /// turns location services on or off, asking for authentication through polkit
    Q_INVOKABLE void setLocationEnabled(bool enabled);

    /// clears the error message, for example when the user dismisses it
    Q_INVOKABLE void clearError();

Q_SIGNALS:
    void stateChanged();
    void loadingChanged();
    void busyChanged();
    void errorMessageChanged();

private:
    /// calls the systemd manager method @p method, then @p next with the reply if it succeeded
    void callSystemd(const QString &method, const QVariantList &arguments,
                     const std::function<void(const QDBusMessage &)> &next);
    /// shows why a call to systemd failed and reads the state again
    void callFailed(const QDBusMessage &reply);
    /// ends a change and reads the state again
    void changeFinished();

    void setState(LocationState state);
    void setLoading(bool loading);
    void setBusy(bool busy);
    void setErrorMessage(const QString &message);

    LocationState m_state = LocationState::Unavailable;
    QString m_errorMessage;
    bool m_loading = false;
    bool m_busy = false;
    /// load() was called while loading, read the state again once the current read finishes
    bool m_reloadPending = false;
};
