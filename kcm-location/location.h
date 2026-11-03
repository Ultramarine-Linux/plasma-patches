// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickConfigModule>

#include <QPointer>
#include <QProcess>

#include <cstdint>

/// whether apps can ask GeoClue for the device's location
enum class LocationState : std::uint8_t {
    /// not read from systemd yet
    Unknown,
    Enabled,
    /// geoclue.service is masked in /etc
    Disabled,
    /// geoclue.service is masked in /run, until the next boot
    DisabledRuntime,
    /// GeoClue isn't installed
    Unavailable,
    /// systemd couldn't be asked, so the state isn't known
    ReadFailed,
};

/// maps systemd's unit file state for geoclue.service (as from GetUnitFileState) to a LocationState
[[nodiscard]] LocationState parseUnitFileState(const QString &state);

/**
 * maps the D-Bus error GetUnitFileState failed with to a LocationState: Unavailable when
 * geoclue.service doesn't exist, ReadFailed for anything else
 */
[[nodiscard]] LocationState parseUnitFileStateError(const QString &errorName);

/**
 * returns the message to show after the location helper finished, or an empty string if it
 * succeeded. @p output is what it printed.
 */
[[nodiscard]] QString helperErrorMessage(int exitCode, QProcess::ExitStatus exitStatus, const QString &output);

/**
 * Location Services: turns GeoClue on or off for the whole system by masking geoclue.service.
 * GeoClue is started over D-Bus whenever an app asks for it, so a mask is what keeps it off.
 * The state is read from systemd over D-Bus. Changes go through ultramarine-location-helper,
 * run as root with pkexec, so they're one polkit action.
 */
class Location : public KQuickConfigModule {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool notInstalled READ notInstalled NOTIFY stateChanged)
    Q_PROPERTY(bool readFailed READ readFailed NOTIFY stateChanged)
    Q_PROPERTY(bool locationEnabled READ locationEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    /// creates the Location Services configuration module
    explicit Location(QObject *parent, const KPluginMetaData &metaData);
    ~Location() override;

    Location(const Location &) = delete;
    Location &operator=(const Location &) = delete;
    Location(Location &&) = delete;
    Location &operator=(Location &&) = delete;

    /// returns whether GeoClue is installed and its state is known
    [[nodiscard]] bool available() const;

    /// returns whether GeoClue isn't installed
    [[nodiscard]] bool notInstalled() const;

    /// returns whether the state couldn't be read from systemd
    [[nodiscard]] bool readFailed() const;

    /// returns whether apps can ask for the device's location
    [[nodiscard]] bool locationEnabled() const;

    /// returns whether the state is being read from systemd
    [[nodiscard]] bool loading() const;

    /// returns whether location services are being turned on or off
    [[nodiscard]] bool busy() const;

    /// returns the current error shown in the UI banner
    [[nodiscard]] const QString &errorMessage() const;

    /// reads geoclue.service's state again, called when the page is shown
    Q_INVOKABLE void load() override;

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
    /// shows how the helper did and reads the state again
    void helperFinished(int exitCode, QProcess::ExitStatus exitStatus);

    void setState(LocationState state);
    void setLoading(bool value);
    void setBusy(bool value);
    void setErrorMessage(const QString &message);

    LocationState m_state = LocationState::Unknown;
    /// the running pkexec, if any
    QPointer<QProcess> m_helper;
    QString m_errorMessage;
    bool m_loading = false;
    bool m_busy = false;
    /// load() was called while loading, read the state again once the current read finishes
    bool m_reloadPending = false;
};
