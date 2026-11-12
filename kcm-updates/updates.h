// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickManagedConfigModule>

#include <QSortFilterProxyModel>
#include <QUrl>

#include <QProcess>
#include <cstdint>

#include <functional>

struct UpdateItem {
    QString name;
    QString source;
    QString oldVersion;
    QString newVersion;
    QString icon;
    qint64 downloadSize = 0;
    qint64 installedSizeChange = 0;
    QString kind;
    QString backend;
    QString appId;
};

QString updateSourceLabel(const UpdateItem &item);

struct UpgradeSettings {
    bool automatic = true;
    bool offline = false;
    int inactiveMinutes = 5;
    bool windowEnabled = false;
    int windowStart = 60;
    int windowEnd = 5 * 60;
    [[nodiscard]] bool isValid() const;
    bool operator==(const UpgradeSettings &other) const;
};

bool parseUpgradeSettings(const QByteArray &json, UpgradeSettings *settings);

QStringList upgradeSettingsArguments(const UpgradeSettings &settings);

bool parseScheduleStatus(const QByteArray &json, bool *scheduled, bool *partial = nullptr);

struct RestartState {
    bool required = false;
    bool offlinePending = false;
    QString stagedVersion;
    bool operator==(const RestartState &) const = default;
};

QList<UpdateItem> parseUpdateListing(const QString &output, QString *technicalInformation = nullptr,
                                     QStringList *errors = nullptr, RestartState *restart = nullptr);

QString refreshErrorMessage(int exitCode, const QByteArray &stdOut, const QByteArray &stdErr);

QString installErrorMessage(int exitCode, QProcess::ExitStatus exitStatus, const QByteArray &stdErr);

QString composeErrorMessage(const QString &actionError, const QString &refreshError);

QString helperText(QByteArrayView output);

QString lastOutputLine(QByteArrayView output);

class UpdatesModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
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

    explicit UpdatesModel(QObject *parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const;
    [[nodiscard]] int countExcluding(const QString &backend) const;
    [[nodiscard]] qint64 totalDownloadSize() const;
    [[nodiscard]] qint64 totalInstalledSizeChange() const;

    void clear();
    void addUpdate(UpdateItem item);

Q_SIGNALS:
    void countChanged();

private:
    QList<UpdateItem> m_updates;
};

class UpdatesPreviewModel final : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit UpdatesPreviewModel(QAbstractItemModel *source, int limit, QObject *parent = nullptr);
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
    Q_PROPERTY(QUrl wallpaper READ wallpaper CONSTANT)
    Q_PROPERTY(QUrl darkWallpaper READ darkWallpaper CONSTANT)
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
    explicit Updates(QObject *parent, const KPluginMetaData &metaData);
    ~Updates() override;

    Updates(const Updates &) = delete;
    Updates &operator=(const Updates &) = delete;
    Updates(Updates &&) = delete;
    Updates &operator=(Updates &&) = delete;

    [[nodiscard]] bool running() const;
    [[nodiscard]] bool scheduling() const;
    [[nodiscard]] const QString &status() const;
    [[nodiscard]] const QString &errorMessage() const;
    [[nodiscard]] bool errorRetryable() const;
    [[nodiscard]] const QString &successMessage() const;

    [[nodiscard]] static QString distributionVersion();
    [[nodiscard]] static QUrl wallpaper();
    [[nodiscard]] static QUrl darkWallpaper();

    [[nodiscard]] int updateCount() const;
    [[nodiscard]] int installableCount() const;
    UpdatesModel *updates();
    QAbstractItemModel *previewUpdates();
    [[nodiscard]] int previewLimit() const;
    [[nodiscard]] const QString &technicalInformation() const;

    [[nodiscard]] bool automaticUpdates() const;
    void setAutomaticUpdates(bool automatic);
    [[nodiscard]] bool offlineUpdates() const;
    void setOfflineUpdates(bool offline);
    [[nodiscard]] int inactiveMinutes() const;
    void setInactiveMinutes(int minutes);
    [[nodiscard]] bool windowEnabled() const;
    void setWindowEnabled(bool enabled);
    [[nodiscard]] int windowStart() const;
    void setWindowStart(int minutes);
    [[nodiscard]] int windowEnd() const;
    void setWindowEnd(int minutes);
    [[nodiscard]] bool windowValid() const;

    [[nodiscard]] bool scheduledTonight() const;
    [[nodiscard]] bool tonightPartial() const;

    void load() override;
    void save() override;
    void defaults() override;
    [[nodiscard]] qint64 downloadSize() const;
    [[nodiscard]] qint64 installedSizeChange() const;

    [[nodiscard]] static bool imageMode();
    [[nodiscard]] bool restartRequired() const;
    [[nodiscard]] bool offlinePending() const;
    [[nodiscard]] const QString &stagedVersion() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void applyUpdates();
    Q_INVOKABLE void applyTonight();
    Q_INVOKABLE void cancelTonight();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void clearSuccess();

Q_SIGNALS:
    void runningChanged();
    void schedulingChanged();
    void statusChanged();
    void errorMessageChanged();
    void successMessageChanged();
    void updateCountChanged();
    void settingsValuesChanged();
    void scheduleChanged();
    void technicalInformationChanged();
    void restartStateChanged();

private Q_SLOTS:
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void processError(QProcess::ProcessError error);
    void readProcessOutput();

private:
    enum class Operation : std::uint8_t {
        None,
        Refresh,
        Install,
    };

    QProcess *createProcess(bool changesSystem);
    void startProcess(const QString &program, const QStringList &arguments, Operation operation);
    void runHelper(const QStringList &arguments, bool changesSystem,
                   const std::function<void(int exitCode, const QByteArray &out, const QByteArray &err)> &done);
    void finishOperation(const QString &statusText);
    void parseListing(const QString &output);

    void setStatus(const QString &value);
    void setTechnicalInformation(const QString &information);
    void setRefreshError(const QString &message);
    void setActionError(const QString &message);
    void updateErrorMessage();
    void setSuccessMessage(const QString &message);
    void showPendingSuccess();

    void loadSettings(bool keepEdits);
    void notifySettingsEdited();
    void refreshSchedule();
    void changeSchedule(const QStringList &arguments, const QString &authenticationFailedMessage,
                        const std::function<QString(const QString &detail)> &failedMessage);
    void setScheduling(bool value);
    [[nodiscard]] bool isSaveNeeded() const override;
    [[nodiscard]] bool isDefaults() const override;

    QProcess *m_process = nullptr;
    Operation m_operation = Operation::None;
    bool m_running = false;
    QByteArray m_processStdout;
    QByteArray m_processStderr;

    UpdatesModel m_updates;
    UpdatesPreviewModel m_previewUpdates;
    QString m_technicalInformation;
    RestartState m_restartState;

    QString m_status;
    QString m_errorMessage;
    QString m_actionError;
    QString m_refreshError;
    bool m_errorRetryable = false;
    QString m_successMessage;
    bool m_showSuccessAfterNextCheck = false;

    UpgradeSettings m_upgradeSettings;
    UpgradeSettings m_savedUpgradeSettings;
    bool m_saving = false;
    bool m_settingsReadInFlight = false;
    bool m_loadAfterSave = false;
    bool m_reloadSavedAfterSave = false;
    quint64 m_latestSettingsRequest = 0;

    bool m_scheduledTonight = false;
    bool m_tonightPartial = false;
    bool m_scheduleChangeInFlight = false;
    bool m_scheduling = false;
    quint64 m_latestScheduleRequest = 0;
};
