// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickManagedConfigModule>

#include <QAbstractListModel>

#include <QDateTime>
#include <QProcess>
#include <cstdint>

#include <functional>

struct UpdateItem {
    QString name;
    /// Full source as reported by the update helper, e.g. "RPM (updates)" or "Flatpak (flathub)".
    QString source;
    QString oldVersion;
    QString newVersion;
    QString icon;
    /// Download size in bytes, 0 if unknown.
    qint64 downloadSize = 0;
    /// Change in disk usage once installed, in bytes (negative if it shrinks), 0 if unknown.
    qint64 installedSizeChange = 0;
    /// "app", "addon", "package" or "runtime". Apps are listed first, then add-ons.
    QString kind;
    /// Package manager: "rpm", "bootc", "rpm-ostree", "flatpak", "nix" or "plasmoid".
    QString backend;
    /// AppStream ID for apps, when known.
    QString appId;
};

/// Returns the short source shown under each update, e.g. "System" for RPMs.
QString updateSourceLabel(const UpdateItem &item);

/// System-wide automatic update settings, as stored by `ultramarine-updates settings`.
struct UpgradeSettings {
    /// "Let your device decide" (true) or "Ask me" (false).
    bool automatic = true;
    /// Install system updates while restarting instead of on the running system.
    bool offline = false;
    /// Install once no one has used the device for this long.
    int inactiveMinutes = 30;
    /// Only install between windowStart and windowEnd (minutes after midnight).
    bool windowEnabled = false;
    int windowStart = 60;
    int windowEnd = 5 * 60;

    bool operator==(const UpgradeSettings &) const = default;
};

/// Parses `ultramarine-updates settings --json`. Returns false if the output isn't valid.
bool parseUpgradeSettings(const QByteArray &json, UpgradeSettings *settings);

/// Returns the `ultramarine-updates settings` arguments that save @p settings.
QStringList upgradeSettingsArguments(const UpgradeSettings &settings);

/// Parses `ultramarine-updates schedule --status --json` into whether Apply Tonight is
/// scheduled and when it next runs (Unix time, 0 if unknown).
bool parseScheduleStatus(const QByteArray &json, bool *scheduled, qint64 *next);

/// Installed updates that are waiting for a restart, from the update listing.
struct RestartState {
    /// Image mode: a new system image is staged and becomes the system when restarting.
    bool required = false;
    /// The version running after the restart, if known.
    QString stagedVersion;
};

/**
 * Parses the JSON update listing emitted by `ultramarine-updates list --json`. Desktop
 * apps come first, then add-ons (Plasma widgets), then other packages, then runtimes; the helper's order is kept
 * otherwise. Sources the helper could not query are returned in @p errors, and whether
 * a restart finishes installed updates in @p restart, if given.
 */
QList<UpdateItem> parseUpdateListing(const QString &output, QString *technicalInformation = nullptr,
                                     QStringList *errors = nullptr, RestartState *restart = nullptr);

class UpdatesModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    // Model roles start at Qt::UserRole (256), so 16 bits are enough.
    enum Roles : std::uint16_t {
        NameRole = Qt::UserRole + 1,
        SourceRole,
        OldVersionRole,
        NewVersionRole,
        IconRole,
        KindRole,
        BackendRole,
        AppIdRole,
        SourceLabelRole,
        DownloadSizeRole,
        InstalledSizeChangeRole,
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

    /// Returns the sum of all download sizes, in bytes.
    [[nodiscard]] qint64 totalDownloadSize() const;

    /// Returns the sum of all installed size changes, in bytes.
    [[nodiscard]] qint64 totalInstalledSizeChange() const;

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
    Q_PROPERTY(bool automaticUpdates READ automaticUpdates WRITE setAutomaticUpdates NOTIFY settingsValuesChanged)
    Q_PROPERTY(bool offlineUpdates READ offlineUpdates WRITE setOfflineUpdates NOTIFY settingsValuesChanged)
    Q_PROPERTY(int inactiveMinutes READ inactiveMinutes WRITE setInactiveMinutes NOTIFY settingsValuesChanged)
    Q_PROPERTY(bool windowEnabled READ windowEnabled WRITE setWindowEnabled NOTIFY settingsValuesChanged)
    Q_PROPERTY(int windowStart READ windowStart WRITE setWindowStart NOTIFY settingsValuesChanged)
    Q_PROPERTY(int windowEnd READ windowEnd WRITE setWindowEnd NOTIFY settingsValuesChanged)
    Q_PROPERTY(bool scheduledTonight READ scheduledTonight NOTIFY scheduleChanged)
    Q_PROPERTY(QDateTime scheduledTime READ scheduledTime NOTIFY scheduleChanged)
    Q_PROPERTY(qint64 downloadSize READ downloadSize NOTIFY updateCountChanged)
    Q_PROPERTY(qint64 installedSizeChange READ installedSizeChange NOTIFY updateCountChanged)
    Q_PROPERTY(bool imageMode READ imageMode CONSTANT)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartStateChanged)
    Q_PROPERTY(QString stagedVersion READ stagedVersion NOTIFY restartStateChanged)

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
    [[nodiscard]] static QString distributionVersion();

    /// Returns the number of available updates.
    [[nodiscard]] int updateCount() const;

    /// Returns the model containing available updates.
    UpdatesModel *updates();

    /// Returns the complete technical update information.
    [[nodiscard]] QString technicalInformation() const;

    /// Returns whether updates install automatically ("Let your device decide").
    [[nodiscard]] bool automaticUpdates() const;
    void setAutomaticUpdates(bool automatic);

    /// Returns whether system updates are installed while restarting.
    [[nodiscard]] bool offlineUpdates() const;
    void setOfflineUpdates(bool offline);

    /// How long no one must have used the device before updates install.
    [[nodiscard]] int inactiveMinutes() const;
    void setInactiveMinutes(int minutes);

    /// Optional time window for installing updates (minutes after midnight).
    [[nodiscard]] bool windowEnabled() const;
    void setWindowEnabled(bool enabled);
    [[nodiscard]] int windowStart() const;
    void setWindowStart(int minutes);
    [[nodiscard]] int windowEnd() const;
    void setWindowEnd(int minutes);

    /// Returns whether Apply Tonight is scheduled, and when it next runs.
    [[nodiscard]] bool scheduledTonight() const;
    [[nodiscard]] QDateTime scheduledTime() const;

    /// Loads the settings and Apply Tonight state from the update helper.
    void load() override;

    /// Saves the settings through the update helper, which asks for authentication.
    void save() override;

    /// Resets the settings to the defaults (not saved until Apply).
    void defaults() override;

    /// Returns the total download size of all updates, in bytes.
    [[nodiscard]] qint64 downloadSize() const;

    /// Returns the total change in disk usage of all updates, in bytes.
    [[nodiscard]] qint64 installedSizeChange() const;

    /**
     * Returns whether this is an image mode (bootc) system. Its system updates are
     * new images, which are always staged and take effect when restarting, so the
     * offline update setting doesn't apply.
     */
    [[nodiscard]] static bool imageMode();

    /// Returns whether installed updates take effect after restarting.
    [[nodiscard]] bool restartRequired() const;

    /// Returns the system version after restarting, if known.
    [[nodiscard]] QString stagedVersion() const;

    /// Starts an update check.
    Q_INVOKABLE void refresh();

    /// Installs RPM and Flatpak updates immediately.
    Q_INVOKABLE void applyUpdates();

    /**
     * Schedules RPM updates for the next reboot and installs Flatpak updates
     * immediately.
     */
    Q_INVOKABLE void applyTonight();

    /// Cancels Apply Tonight.
    Q_INVOKABLE void cancelTonight();

    /// Cancels the currently running update operation.
    Q_INVOKABLE void cancel();

    /// Asks to restart through Plasma's logout screen, so work can be saved first.
    Q_INVOKABLE void restart();

Q_SIGNALS:
    /// Emitted when an update operation starts or stops.
    void runningChanged();

    /// Emitted when the operation status changes.
    void statusChanged();

    /// Emitted when the error banner text changes.
    void errorMessageChanged();

    /// Emitted when the number of available updates changes.
    void updateCountChanged();

    /// Emitted when the automatic update settings change.
    void settingsValuesChanged();

    /// Emitted when Apply Tonight is scheduled or cancelled.
    void scheduleChanged();

    /// Emitted when technical update information changes.
    void technicalInformationChanged();

    /// Emitted when updates start or stop waiting for a restart.
    void restartStateChanged();


private Q_SLOTS:
    /// Handles completion of the active update process.
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /// Handles errors reported while starting an update process.
    void processError(QProcess::ProcessError error);

    /// Reads and dispatches output from the active update process.
    void readProcessOutput();

private:
    enum class Operation : std::uint8_t {
        None,
        Refresh,
        Install,
    };

    /// Starts one backend process and records its operation type.
    void startProcess(const QString &program, const QStringList &arguments, Operation operation);

    /// Updates the user-facing operation status.
    void setStatus(const QString &status);

    /// Updates the technical information shown in the details dialog.
    void setTechnicalInformation(const QString &information);

    /// Completes an operation and updates its final status.
    void finishOperation(bool success, const QString &message);

    /// Parses the helper's structured update listing.
    void parseListing(const QString &output);

    QProcess m_process;
    UpdatesModel m_updates;
    QString m_status;
    QString m_errorMessage;
    QString m_lastProcessOutput;
    /// Raw stdout of a refresh; kept apart from stderr so it parses as JSON.
    QByteArray m_refreshStdout;
    QString m_technicalInformation;
    Operation m_operation = Operation::None;
    bool m_running = false;
    /// Runs the update helper with @p arguments and calls @p done when it exits (exit
    /// code -1 if it couldn't start). The helper escalates through pkexec if needed.
    void runHelper(const QStringList &arguments,
                   const std::function<void(int exitCode, const QByteArray &out, const QByteArray &err)> &done);

    /// Shows @p message in the error banner.
    void setErrorMessage(const QString &message);

    /// Reloads whether Apply Tonight is scheduled.
    void refreshSchedule();

    [[nodiscard]] bool isSaveNeeded() const override;
    [[nodiscard]] bool isDefaults() const override;

    RestartState m_restartState;
    UpgradeSettings m_upgradeSettings;
    UpgradeSettings m_savedUpgradeSettings;
    bool m_scheduledTonight = false;
    QDateTime m_scheduledTime;
};
