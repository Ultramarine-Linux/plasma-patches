// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickManagedConfigModule>

#include <QAbstractListModel>
#include <QProcess>
#include <QStringList>

struct UpdateItem {
    QString name;
    QString source;
    QString oldVersion;
    QString newVersion;
    QString icon;
};

class UpdatesModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        SourceRole,
        OldVersionRole,
        NewVersionRole,
        IconRole,
    };

    /// Creates the model that exposes pending updates to QML.
    explicit UpdatesModel(QObject *parent = nullptr);

    /**
     * Returns the number of top-level update entries.
     */
    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;

    /// Returns the requested update value for a model index.
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

    /// Returns the names of the roles exposed to QML.
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Returns the number of updates in the model.
    [[nodiscard]] int count() const;

    /// Removes all updates from the model.
    void clear();

    /// Adds one update to the model.
    void addUpdate(UpdateItem item);

Q_SIGNALS:
    /// Emitted when the number of updates changes.
    void countChanged();

private:
    QList<UpdateItem> m_updates;
};

class Updates : public KQuickManagedConfigModule {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString distributionVersion READ distributionVersion CONSTANT)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY updateCountChanged)
    Q_PROPERTY(UpdatesModel *updates READ updates CONSTANT)
    Q_PROPERTY(QString technicalInformation READ technicalInformation NOTIFY technicalInformationChanged)
    Q_PROPERTY(bool offlineUpdatesEnabled READ offlineUpdatesEnabled WRITE setOfflineUpdatesEnabled NOTIFY offlineUpdatesEnabledChanged)

public:
    /**
     * Creates the Fedora updates configuration module.
     */
    explicit Updates(QObject *parent, const KPluginMetaData &metaData);

    /// Returns whether an update operation is running.
    [[nodiscard]] bool running() const;

    /// Returns the current operation status.
    [[nodiscard]] QString status() const;

    /// Returns the current error shown in the UI banner.
    [[nodiscard]] QString errorMessage() const;

    /// Returns the Fedora release version.
    [[nodiscard]] QString distributionVersion() const;

    /// Returns the number of available updates.
    [[nodiscard]] int updateCount() const;

    /// Returns the model containing available updates.
    UpdatesModel *updates();

    /// Returns the complete technical update information.
    [[nodiscard]] QString technicalInformation() const;

    /// Returns whether offline updates are enabled.
    [[nodiscard]] bool offlineUpdatesEnabled() const;

    /// Enables or disables offline update scheduling.
    void setOfflineUpdatesEnabled(bool enabled);

    /// Starts an update check.
    Q_INVOKABLE void refresh();

    /// Installs RPM and Flatpak updates immediately.
    Q_INVOKABLE void applyUpdates();

    /**
     * Schedules RPM updates for the next reboot and installs Flatpak updates
     * immediately.
     */
    Q_INVOKABLE void applyOfflineUpdates();

    /// Cancels the currently running update operation.
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    /// Emitted when an update operation starts or stops.
    void runningChanged();

    /// Emitted when the operation status changes.
    void statusChanged();

    /// Emitted when the error banner text changes.
    void errorMessageChanged();

    /// Emitted when the number of available updates changes.
    void updateCountChanged();

    /// Emitted when technical update information changes.
    void technicalInformationChanged();

    /// Emitted when offline update scheduling is toggled.
    void offlineUpdatesEnabledChanged();

private Q_SLOTS:
    /// Handles completion of the active update process.
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /// Handles errors reported while starting an update process.
    void processError(QProcess::ProcessError error);

    /// Reads and dispatches output from the active update process.
    void readProcessOutput();

private:
    enum class Operation {
        None,
        RefreshPackageKit,
        RefreshFlatpak,
        ApplyNowPackageKit,
        ApplyNowFlatpak,
        SchedulePackageKit,
    };

    /// Starts one backend process and records its operation type.
    void startProcess(const QString &program, const QStringList &arguments, Operation operation);

    /// Updates the user-facing operation status.
    void setStatus(const QString &status);

    /// Updates the technical information shown in the details dialog.
    void setTechnicalInformation(const QString &information);

    /// Completes an operation and updates its final status.
    void finishOperation(bool success, const QString &message);

    /// Parses PackageKit's update listing.
    void parsePackageKit(const QString &output);

    /// Parses Flatpak's update listing.
    void parseFlatpak(const QString &output);

    QProcess m_process;
    UpdatesModel m_updates;
    QString m_status;
    QString m_errorMessage;
    QString m_lastProcessOutput;
    QString m_technicalInformation;
    Operation m_operation = Operation::None;
    bool m_running = false;
    bool m_offlineUpdatesEnabled = true;
};
