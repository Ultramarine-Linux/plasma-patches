// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickManagedConfigModule>

#include <QAbstractListModel>
#include <QSortFilterProxyModel>

#include <QProcess>
#include <cstdint>

#include <functional>

struct UpdateItem {
    QString name;
    /// full source as reported by the update helper, e.g. "RPM (updates)" or "Flatpak (flathub)"
    QString source;
    QString oldVersion;
    QString newVersion;
    QString icon;
    /// download size in bytes, 0 if unknown
    qint64 downloadSize = 0;
    /// change in disk usage once installed, in bytes (negative if it shrinks), 0 if unknown
    qint64 installedSizeChange = 0;
    /// "app", "addon", "package" or "runtime". Apps are listed first, then add-ons.
    QString kind;
    /// package manager: "rpm", "bootc", "rpm-ostree", "flatpak", "nix" or "plasmoid"
    QString backend;
    /// AppStream ID for apps, when known
    QString appId;
};

/// returns the short source shown under each update, e.g. "System" for RPMs
QString updateSourceLabel(const UpdateItem &item);

/// system-wide automatic update settings, as stored by `ultramarine-updates settings`
struct UpgradeSettings {
    /// "Let your device decide" (true) or "Ask me" (false)
    bool automatic = true;
    /// install system updates while restarting instead of on the running system
    bool offline = false;
    /// install once no one has used the device for this long (also used by Apply Tonight)
    int inactiveMinutes = 5;
    /// only install between windowStart and windowEnd (minutes after midnight)
    bool windowEnabled = false;
    int windowStart = 60;
    int windowEnd = 5 * 60;

    /// returns false if the window starts and ends at the same time, which the helper rejects
    [[nodiscard]] bool isValid() const;

    /// compares what the helper would store, so the window times only count while the window is used
    bool operator==(const UpgradeSettings &other) const;
};

/// parses `ultramarine-updates settings --json`. Returns false if the output isn't valid.
bool parseUpgradeSettings(const QByteArray &json, UpgradeSettings *settings);

/// returns the `ultramarine-updates settings` arguments that save @p settings
QStringList upgradeSettingsArguments(const UpgradeSettings &settings);

/**
 * parses `ultramarine-updates schedule --status --json` into whether Apply Tonight is
 * scheduled, and whether only part of it still is (@p partial). Apply Tonight has a system
 * and a user part, and one can be done or canceled while the other is still waiting.
 */
bool parseScheduleStatus(const QByteArray &json, bool *scheduled, bool *partial = nullptr);

/// installed updates that are waiting for a restart, from the update listing
struct RestartState {
    /// restarting finishes installed updates: a staged system image, or offline updates
    bool required = false;
    /// system updates are downloaded and install while restarting (offline updates)
    bool offlinePending = false;
    /// image mode: the version running after the restart, if known
    QString stagedVersion;

    bool operator==(const RestartState &) const = default;
};

/**
 * parses the JSON update listing emitted by `ultramarine-updates list --json`. Desktop
 * apps come first, then add-ons (Plasma widgets), then other packages, then runtimes. The helper's order is kept
 * otherwise. Sources the helper could not query are returned in @p errors, and whether
 * a restart finishes installed updates in @p restart, if given.
 */
QList<UpdateItem> parseUpdateListing(const QString &output, QString *technicalInformation = nullptr,
                                     QStringList *errors = nullptr, RestartState *restart = nullptr);

/**
 * returns the error to show once `ultramarine-updates list --json` exited with @p exitCode
 * (-1 if it crashed): why the last offline update failed, the sources it could not
 * query, or why checking failed. Empty if there is nothing to report.
 */
QString refreshErrorMessage(int exitCode, const QByteArray &stdOut, const QByteArray &stdErr);

/**
 * returns the error to show once `ultramarine-updates apply` failed. Only the last lines
 * of stderr are included, since the rest is the package manager's whole log.
 */
QString installErrorMessage(int exitCode, QProcess::ExitStatus exitStatus, const QByteArray &stdErr);

/**
 * returns the error banner text: the error from installing, saving or scheduling
 * (@p actionError) above the error from checking for updates (@p refreshError)
 */
QString composeErrorMessage(const QString &actionError, const QString &refreshError);

/// decodes helper output, which is always UTF-8, and trims it
QString helperText(const QByteArray &output);

class UpdatesModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    // model roles start at Qt::UserRole (256), so 16 bits are enough
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

    /// creates the model that exposes pending updates to QML
    explicit UpdatesModel(QObject *parent = nullptr);

    /**
     * returns the number of top-level update entries
     */
    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;

    /// returns the requested update value for a model index
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

    /// returns the names of the roles exposed to QML
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// returns the number of updates in the model
    [[nodiscard]] int count() const;

    /// returns the number of updates that aren't from @p backend
    [[nodiscard]] int countExcluding(const QString &backend) const;

    /// returns the sum of all download sizes, in bytes
    [[nodiscard]] qint64 totalDownloadSize() const;

    /// returns the sum of all installed size changes, in bytes
    [[nodiscard]] qint64 totalInstalledSizeChange() const;

    /// removes all updates from the model
    void clear();

    /// adds one update to the model
    void addUpdate(UpdateItem item);

Q_SIGNALS:
    /// emitted when the number of updates changes
    void countChanged();

private:
    QList<UpdateItem> m_updates;
};

/// the first few rows of an update model, so the main page doesn't create a delegate per update.
/// UpdatesModel only appends rows or clears, so earlier rows never shift past the limit.
class UpdatesPreviewModel final : public QSortFilterProxyModel {
    Q_OBJECT

public:
    /// shows at most @p limit rows of @p source
    explicit UpdatesPreviewModel(QAbstractItemModel *source, int limit, QObject *parent = nullptr);

    /// returns the maximum number of rows shown
    [[nodiscard]] int limit() const;

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_limit;
};

class Updates : public KQuickManagedConfigModule {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool scheduling READ scheduling NOTIFY schedulingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool errorRetryable READ errorRetryable NOTIFY errorMessageChanged)
    Q_PROPERTY(QString successMessage READ successMessage NOTIFY successMessageChanged)
    Q_PROPERTY(QString distributionVersion READ distributionVersion CONSTANT)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY updateCountChanged)
    Q_PROPERTY(int installableCount READ installableCount NOTIFY updateCountChanged)
    Q_PROPERTY(UpdatesModel *updates READ updates CONSTANT)
    Q_PROPERTY(QAbstractItemModel *previewUpdates READ previewUpdates CONSTANT)
    Q_PROPERTY(int previewLimit READ previewLimit CONSTANT)
    Q_PROPERTY(QString technicalInformation READ technicalInformation NOTIFY technicalInformationChanged)
    Q_PROPERTY(bool automaticUpdates READ automaticUpdates WRITE setAutomaticUpdates NOTIFY settingsValuesChanged)
    Q_PROPERTY(bool offlineUpdates READ offlineUpdates WRITE setOfflineUpdates NOTIFY settingsValuesChanged)
    Q_PROPERTY(int inactiveMinutes READ inactiveMinutes WRITE setInactiveMinutes NOTIFY settingsValuesChanged)
    Q_PROPERTY(bool windowEnabled READ windowEnabled WRITE setWindowEnabled NOTIFY settingsValuesChanged)
    Q_PROPERTY(int windowStart READ windowStart WRITE setWindowStart NOTIFY settingsValuesChanged)
    Q_PROPERTY(int windowEnd READ windowEnd WRITE setWindowEnd NOTIFY settingsValuesChanged)
    Q_PROPERTY(bool windowValid READ windowValid NOTIFY settingsValuesChanged)
    Q_PROPERTY(bool scheduledTonight READ scheduledTonight NOTIFY scheduleChanged)
    Q_PROPERTY(bool tonightPartial READ tonightPartial NOTIFY scheduleChanged)
    Q_PROPERTY(qint64 downloadSize READ downloadSize NOTIFY updateCountChanged)
    Q_PROPERTY(qint64 installedSizeChange READ installedSizeChange NOTIFY updateCountChanged)
    Q_PROPERTY(bool imageMode READ imageMode CONSTANT)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartStateChanged)
    Q_PROPERTY(bool offlinePending READ offlinePending NOTIFY restartStateChanged)
    Q_PROPERTY(QString stagedVersion READ stagedVersion NOTIFY restartStateChanged)

public:
    /**
     * creates the Ultramarine updates configuration module
     */
    explicit Updates(QObject *parent, const KPluginMetaData &metaData);

    /// stops read-only helper calls and releases changing ones (see the definition)
    ~Updates() override;

    Updates(const Updates &) = delete;
    Updates &operator=(const Updates &) = delete;
    Updates(Updates &&) = delete;
    Updates &operator=(Updates &&) = delete;

    /// returns whether an update operation is running
    [[nodiscard]] bool running() const;

    /// returns whether Apply Tonight is being scheduled or canceled
    [[nodiscard]] bool scheduling() const;

    /// returns the current operation status
    [[nodiscard]] const QString &status() const;

    /// returns the current error shown in the UI banner
    [[nodiscard]] const QString &errorMessage() const;

    /// returns whether checking again could fix the error, so the banner offers Try Again
    [[nodiscard]] bool errorRetryable() const;

    /// returns the message shown after updates were installed, until it is dismissed
    [[nodiscard]] const QString &successMessage() const;

    /// returns the distribution version (VERSION_ID in os-release)
    [[nodiscard]] static QString distributionVersion();

    /// returns the number of available updates
    [[nodiscard]] int updateCount() const;

    /// returns the number of updates Apply would install. RPM updates that are already
    /// downloaded and install while restarting (offline updates) are listed but don't count.
    [[nodiscard]] int installableCount() const;

    /// returns the model containing available updates
    UpdatesModel *updates();

    /// returns the first previewLimit() updates, for the main page
    QAbstractItemModel *previewUpdates();

    /// returns the number of updates listed on the main page
    [[nodiscard]] int previewLimit() const;

    /// returns the complete technical update information
    [[nodiscard]] const QString &technicalInformation() const;

    /// returns whether updates install automatically ("Let your device decide")
    [[nodiscard]] bool automaticUpdates() const;
    void setAutomaticUpdates(bool automatic);

    /// returns whether system updates are installed while restarting
    [[nodiscard]] bool offlineUpdates() const;
    void setOfflineUpdates(bool offline);

    /// how long no one must have used the device before updates install
    [[nodiscard]] int inactiveMinutes() const;
    void setInactiveMinutes(int minutes);

    /// optional time window for installing updates (minutes after midnight)
    [[nodiscard]] bool windowEnabled() const;
    void setWindowEnabled(bool enabled);
    [[nodiscard]] int windowStart() const;
    void setWindowStart(int minutes);
    [[nodiscard]] int windowEnd() const;
    void setWindowEnd(int minutes);
    /// returns false if the window starts and ends at the same time
    [[nodiscard]] bool windowValid() const;

    /// returns whether Apply Tonight is scheduled
    [[nodiscard]] bool scheduledTonight() const;

    /// returns whether only part of Apply Tonight is still scheduled, which can be canceled too
    [[nodiscard]] bool tonightPartial() const;

    /// loads the settings and Apply Tonight state from the update helper
    void load() override;

    /// saves the settings through the update helper, which asks for authentication
    void save() override;

    /// resets the settings to the defaults (not saved until Apply)
    void defaults() override;

    /// returns the total download size of all updates, in bytes
    [[nodiscard]] qint64 downloadSize() const;

    /// returns the total change in disk usage of all updates, in bytes
    [[nodiscard]] qint64 installedSizeChange() const;

    /**
     * returns whether this is an image mode (bootc) system. Its system updates are
     * new images, which are always staged and take effect when restarting, so the
     * offline update setting doesn't apply.
     */
    [[nodiscard]] static bool imageMode();

    /// returns whether installed updates take effect after restarting
    [[nodiscard]] bool restartRequired() const;

    /// returns whether system updates install the next time the device restarts
    [[nodiscard]] bool offlinePending() const;

    /// returns the system version after restarting, if known
    [[nodiscard]] const QString &stagedVersion() const;

    /// starts an update check
    Q_INVOKABLE void refresh();

    /**
     * installs every listed update now. The helper asks for authentication and does the
     * system part first (staged for the next restart with offline updates or in image
     * mode), then updates the user's Flatpaks, Nix profile and Plasma widgets.
     */
    Q_INVOKABLE void applyUpdates();

    /// schedules all updates to install tonight while the device isn't in use
    Q_INVOKABLE void applyTonight();

    /// cancels Apply Tonight
    Q_INVOKABLE void cancelTonight();

    /// asks to restart through Plasma's logout screen, so work can be saved first
    Q_INVOKABLE void restart();

    /// clears the error message, for example when the user dismisses it
    Q_INVOKABLE void clearError();

    /// clears the success message once the user dismisses it
    Q_INVOKABLE void clearSuccess();

Q_SIGNALS:
    /// emitted when an update operation starts or stops
    void runningChanged();

    /// emitted when Apply Tonight starts or stops being scheduled or canceled
    void schedulingChanged();

    /// emitted when the operation status changes
    void statusChanged();

    /// emitted when the error banner text changes
    void errorMessageChanged();

    /// emitted when the success message changes
    void successMessageChanged();

    /// emitted when the number of available updates changes
    void updateCountChanged();

    /// emitted when the automatic update settings change
    void settingsValuesChanged();

    /// emitted when Apply Tonight is scheduled or canceled
    void scheduleChanged();

    /// emitted when technical update information changes
    void technicalInformationChanged();

    /// emitted when updates start or stop waiting for a restart
    void restartStateChanged();

private Q_SLOTS:
    /// handles completion of the active update process
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /// handles errors reported while starting an update process
    void processError(QProcess::ProcessError error);

    /// reads and dispatches output from the active update process
    void readProcessOutput();

private:
    enum class Operation : std::uint8_t {
        None,
        Refresh,
        Install,
    };

    /**
     * creates a helper process owned by this module. A process that @p changesSystem
     * keeps running if the module closes (see ~Updates()).
     */
    QProcess *createProcess(bool changesSystem);

    /// starts one backend process and records its operation type
    void startProcess(const QString &program, const QStringList &arguments, Operation operation);

    /// updates the user-facing operation status
    void setStatus(const QString &value);

    /// updates the technical information shown in the details dialog
    void setTechnicalInformation(const QString &information);

    /// completes an operation and shows @p statusText. Callers set any error themselves.
    void finishOperation(const QString &statusText);

    /// sets the error from checking for updates, which the next check replaces
    void setRefreshError(const QString &message);

    /// sets the error from installing, saving or scheduling, which stays until it's dismissed
    void setActionError(const QString &message);

    /// composes errorMessage() from both errors
    void updateErrorMessage();

    /// reads the settings. With @p keepEdits, only the saved state is updated, not what's shown.
    void loadSettings(bool keepEdits);

    /// parses the helper's structured update listing
    void parseListing(const QString &output);

    /// the running list or apply call, if any
    QProcess *m_process = nullptr;
    UpdatesModel m_updates;
    UpdatesPreviewModel m_previewUpdates;
    QString m_status;
    /// shown in the banner, see composeErrorMessage()
    QString m_errorMessage;
    QString m_actionError;
    QString m_refreshError;
    /// only a refresh error is shown, so checking again may fix it
    bool m_errorRetryable = false;
    QString m_successMessage;
    /// raw output of the running operation, decoded once it exits so multi-byte
    /// characters split across reads survive and stdout still parses as JSON
    QByteArray m_processStdout;
    QByteArray m_processStderr;
    QString m_technicalInformation;
    Operation m_operation = Operation::None;
    bool m_running = false;
    /// runs the update helper with @p arguments and calls @p done when it exits (exit
    /// code -1 if it couldn't start or crashed). The helper escalates through pkexec if
    /// needed. Calls that @p changesSystem keep running if the module closes.
    void runHelper(const QStringList &arguments, bool changesSystem,
                   const std::function<void(int exitCode, const QByteArray &out, const QByteArray &err)> &done);

    /// sets the message shown after updates were installed
    void setSuccessMessage(const QString &message);

    /// reloads whether Apply Tonight is scheduled. Only the latest request's reply is used.
    void refreshSchedule();

    void setScheduling(bool value);

    [[nodiscard]] bool isSaveNeeded() const override;
    [[nodiscard]] bool isDefaults() const override;

    RestartState m_restartState;
    UpgradeSettings m_upgradeSettings;
    UpgradeSettings m_savedUpgradeSettings;
    bool m_scheduledTonight = false;
    bool m_tonightPartial = false;
    /// a `settings` save is in flight
    bool m_saving = false;
    /// a `settings --json` read is in flight
    bool m_loading = false;
    /// load() (Reset) was called while saving, so it runs once the save is done
    bool m_loadAfterSave = false;
    /// save() made a settings read stale, so the saved state is read again afterwards
    bool m_reloadSavedAfterSave = false;
    /// only the latest settings read's reply is used, and a save makes reads in flight stale
    quint64 m_settingsRequest = 0;
    /// a `schedule` (or `schedule --cancel`) call is in flight
    bool m_scheduleChanging = false;
    /// true from the start of a schedule change until the status read after it arrives
    bool m_scheduling = false;
    quint64 m_scheduleRequest = 0;
};
