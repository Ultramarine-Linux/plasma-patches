// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickConfigModule>

#include <QPointer>
#include <QProcess>

#include <cstdint>

enum class LocationState : std::uint8_t {
    NotReadYet,
    Enabled,
    Disabled,
    DisabledRuntime,
    Unavailable,
    ReadFailed,
};

[[nodiscard]] LocationState parseUnitFileState(const QString &state);

[[nodiscard]] LocationState parseUnitFileStateError(const QString &errorName);

[[nodiscard]] QString helperErrorMessage(int exitCode, QProcess::ExitStatus exitStatus, const QString &output);

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
    explicit Location(QObject *parent, const KPluginMetaData &metaData);
    ~Location() override;

    Location(const Location &) = delete;
    Location &operator=(const Location &) = delete;
    Location(Location &&) = delete;
    Location &operator=(Location &&) = delete;

    [[nodiscard]] bool available() const;

    [[nodiscard]] bool notInstalled() const;

    [[nodiscard]] bool readFailed() const;

    [[nodiscard]] bool locationEnabled() const;

    [[nodiscard]] bool loading() const;

    [[nodiscard]] bool busy() const;

    [[nodiscard]] const QString &errorMessage() const;

    Q_INVOKABLE void load() override;

    Q_INVOKABLE void setLocationEnabled(bool enabled);

    Q_INVOKABLE void clearError();

Q_SIGNALS:
    void stateChanged();
    void loadingChanged();
    void busyChanged();
    void errorMessageChanged();

private:
    void detachRunningPkexec();
    void helperFinished(int exitCode, QProcess::ExitStatus exitStatus);

    void setState(LocationState state);
    void setLoading(bool value);
    void setBusy(bool value);
    void setErrorMessage(const QString &message);

    LocationState m_state = LocationState::NotReadYet;
    QPointer<QProcess> m_runningPkexec;
    QString m_errorMessage;
    QString m_lastReadError;
    bool m_loading = false;
    bool m_busy = false;
    bool m_reloadAfterCurrentRead = false;
};
