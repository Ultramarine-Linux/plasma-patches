// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <KLocalizedString>
#include <algorithm>
#include <numeric>
#include <utility>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QTime>

#include "detachedprocess.h"
#include "updates.h"
#include "updateshelper.h"

K_PLUGIN_CLASS_WITH_JSON(Updates, "kcm_ultramarine_updates.json")

#include "updates.moc"

namespace {
    constexpr int previewCount = 5;
    constexpr int minutesPerDay = 24 * 60;
    constexpr auto changesSystemProperty = "ultramarineChangesSystem";

    template<typename T>
    bool assign(T &member, const T &value) {
        if (member == value) {
            return false;
        }
        member = value;
        return true;
    }

    qint64 sum(const QList<UpdateItem> &updates, qint64 UpdateItem::*field) {
        return std::accumulate(updates.cbegin(), updates.cend(), qint64{0},
                               [field](const qint64 total, const UpdateItem &item) { return total + item.*field; });
    }

    QString helperNotInstalledMessage() { return i18n("The update helper (ultramarine-updates) is not installed."); }

    QUrl releaseBackground(const QString &name) {
        const QString path = QStringLiteral("/usr/share/backgrounds/") + name;
        return QFileInfo::exists(path) ? QUrl::fromLocalFile(path) : QUrl();
    }
} // namespace

UpdatesModel::UpdatesModel(QObject *parent) : QAbstractListModel(parent) {}

int UpdatesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_updates.size());
}

QVariant UpdatesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_updates.size())) {
        return {};
    }

    const UpdateItem &item = m_updates.at(index.row());
    switch (role) {
    case NameRole:
        return item.name;
    case SourceRole:
        return item.source;
    case OldVersionRole:
        return item.oldVersion;
    case NewVersionRole:
        return item.newVersion;
    case IconRole:
        return item.icon;
    case KindRole:
        return item.kind;
    case BackendRole:
        return item.backend;
    case AppIdRole:
        return item.appId;
    case SourceLabelRole:
        return updateSourceLabel(item);
    case DownloadSizeRole:
        return item.downloadSize;
    case InstalledSizeChangeRole:
        return item.installedSizeChange;
    default:
        return {};
    }
}

QHash<int, QByteArray> UpdatesModel::roleNames() const {
    return {
        {NameRole, "name"},
        {SourceRole, "source"},
        {OldVersionRole, "oldVersion"},
        {NewVersionRole, "newVersion"},
        {IconRole, "icon"},
        {KindRole, "kind"},
        {BackendRole, "backend"},
        {AppIdRole, "appId"},
        {SourceLabelRole, "sourceLabel"},
        {DownloadSizeRole, "downloadSize"},
        {InstalledSizeChangeRole, "installedSizeChange"},
    };
}

int UpdatesModel::count() const { return static_cast<int>(m_updates.size()); }

int UpdatesModel::countExcluding(const QString &backend) const {
    return static_cast<int>(
        std::ranges::count_if(m_updates, [&backend](const UpdateItem &item) { return item.backend != backend; }));
}

qint64 UpdatesModel::totalDownloadSize() const { return sum(m_updates, &UpdateItem::downloadSize); }

qint64 UpdatesModel::totalInstalledSizeChange() const { return sum(m_updates, &UpdateItem::installedSizeChange); }

void UpdatesModel::clear() {
    if (m_updates.isEmpty()) {
        return;
    }
    beginResetModel();
    m_updates.clear();
    endResetModel();
    Q_EMIT countChanged();
}

void UpdatesModel::addUpdate(UpdateItem item) {
    const int row = static_cast<int>(m_updates.size());
    beginInsertRows({}, row, row);
    m_updates.append(std::move(item));
    endInsertRows();
    Q_EMIT countChanged();
}

UpdatesPreviewModel::UpdatesPreviewModel(QAbstractItemModel *source, const int limit, QObject *parent) :
    QSortFilterProxyModel(parent), m_limit(limit) {
    setSourceModel(source);
}

int UpdatesPreviewModel::limit() const { return m_limit; }

bool UpdatesPreviewModel::filterAcceptsRow(const int sourceRow, const QModelIndex &sourceParent) const {
    return !sourceParent.isValid() && sourceRow < m_limit;
}

Updates::Updates(QObject *parent, const KPluginMetaData &metaData) :
    KQuickManagedConfigModule(parent, metaData), m_updates(this), m_previewUpdates(&m_updates, previewCount, this) {
    setButtons(Apply);
    connect(&m_updates, &UpdatesModel::countChanged, this, &Updates::updateCountChanged);
    refresh();
}

Updates::~Updates() {
    // killing a process that changes the system would strand the root part pkexec started
    for (QProcess *process: findChildren<QProcess *>(Qt::FindDirectChildrenOnly)) {
        if (process->property(changesSystemProperty).toBool() && process->state() != QProcess::NotRunning) {
            DetachedProcess::letFinishAfterModuleCloses(process, this);
        } else {
            process->disconnect(this);
            delete process;
        }
    }
}

QProcess *Updates::createProcess(const bool changesSystem) {
    auto *process = new QProcess(this);
    process->setProperty(changesSystemProperty, changesSystem);
    process->setStandardInputFile(QProcess::nullDevice());
    return process;
}

bool Updates::running() const { return m_running; }

bool Updates::scheduling() const { return m_scheduling; }

const QString &Updates::status() const { return m_status; }

const QString &Updates::errorMessage() const { return m_errorMessage; }

bool Updates::errorRetryable() const { return m_errorRetryable; }

const QString &Updates::successMessage() const { return m_successMessage; }

QString Updates::distributionVersion() {
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (!osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QLatin1String versionIdKey("VERSION_ID=");
    for (const QStringList entries = QString::fromUtf8(osRelease.readAll()).split(QLatin1Char('\n'));
         const QString &entry: entries) {
        if (entry.startsWith(versionIdKey)) {
            return entry.sliced(versionIdKey.size()).remove(QLatin1Char('"'));
        }
    }
    return {};
}

QUrl Updates::wallpaper() { return releaseBackground(QStringLiteral("default.png")); }

QUrl Updates::darkWallpaper() {
    const QUrl dark = releaseBackground(QStringLiteral("default-dark.png"));
    return dark.isEmpty() ? wallpaper() : dark;
}

int Updates::updateCount() const { return m_updates.count(); }

int Updates::installableCount() const {
    return m_restartState.offlinePending ? m_updates.countExcluding(QStringLiteral("rpm")) : m_updates.count();
}

UpdatesModel *Updates::updates() { return &m_updates; }

QAbstractItemModel *Updates::previewUpdates() { return &m_previewUpdates; }

int Updates::previewLimit() const { return m_previewUpdates.limit(); }

const QString &Updates::technicalInformation() const { return m_technicalInformation; }

bool Updates::automaticUpdates() const { return m_upgradeSettings.automatic; }

void Updates::setAutomaticUpdates(const bool automatic) {
    if (assign(m_upgradeSettings.automatic, automatic)) {
        notifySettingsEdited();
    }
}

bool Updates::offlineUpdates() const { return m_upgradeSettings.offline; }

void Updates::setOfflineUpdates(const bool offline) {
    if (assign(m_upgradeSettings.offline, offline)) {
        notifySettingsEdited();
    }
}

int Updates::inactiveMinutes() const { return m_upgradeSettings.inactiveMinutes; }

void Updates::setInactiveMinutes(const int minutes) {
    if (assign(m_upgradeSettings.inactiveMinutes, std::clamp(minutes, 0, minutesPerDay))) {
        notifySettingsEdited();
    }
}

bool Updates::windowEnabled() const { return m_upgradeSettings.windowEnabled; }

void Updates::setWindowEnabled(const bool enabled) {
    if (assign(m_upgradeSettings.windowEnabled, enabled)) {
        notifySettingsEdited();
    }
}

int Updates::windowStart() const { return m_upgradeSettings.windowStart; }

void Updates::setWindowStart(const int minutes) {
    if (assign(m_upgradeSettings.windowStart, std::clamp(minutes, 0, minutesPerDay - 1))) {
        notifySettingsEdited();
    }
}

int Updates::windowEnd() const { return m_upgradeSettings.windowEnd; }

void Updates::setWindowEnd(const int minutes) {
    if (assign(m_upgradeSettings.windowEnd, std::clamp(minutes, 0, minutesPerDay - 1))) {
        notifySettingsEdited();
    }
}

bool Updates::windowValid() const { return m_upgradeSettings.isValid(); }

bool Updates::scheduledTonight() const { return m_scheduledTonight; }

bool Updates::tonightPartial() const { return m_tonightPartial; }

bool Updates::isSaveNeeded() const {
    return m_upgradeSettings.isValid() && m_upgradeSettings != m_savedUpgradeSettings;
}

bool Updates::isDefaults() const { return m_upgradeSettings == UpgradeSettings{}; }

void Updates::runHelper(const QStringList &arguments, const bool changesSystem,
                        const std::function<void(int exitCode, const QByteArray &out, const QByteArray &err)> &done) {
    const QString helper = UpdatesHelper::executable();
    if (helper.isEmpty()) {
        done(-1, {}, {});
        return;
    }
    QProcess *process = createProcess(changesSystem);
    connect(process, &QProcess::finished, this, [process, done](const int exitCode, const QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit ? exitCode : -1, process->readAllStandardOutput(),
             process->readAllStandardError());
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](const QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            done(-1, {}, {});
            process->deleteLater();
        }
    });
    process->start(helper, arguments);
}

void Updates::load() {
    if (m_saving) {
        m_loadAfterSave = true;
        return;
    }
    loadSettings(false);
    refreshSchedule();
}

void Updates::loadSettings(const bool keepEdits) {
    const quint64 request = ++m_latestSettingsRequest;
    m_settingsReadInFlight = true;
    runHelper({QStringLiteral("settings"), QStringLiteral("--json")}, false,
              [this, request, keepEdits](const int exitCode, const QByteArray &out, const QByteArray &err) {
                  if (request != m_latestSettingsRequest) {
                      return;
                  }
                  m_settingsReadInFlight = false;
                  UpgradeSettings loaded;
                  if (exitCode != 0 || !parseUpgradeSettings(out, &loaded)) {
                      loaded = UpgradeSettings{};
                      const QString detail = helperText(err);
                      setActionError(
                          detail.isEmpty()
                              ? i18n("The update settings could not be loaded. Showing the defaults.")
                              : i18n("The update settings could not be loaded. Showing the defaults. %1", detail));
                  }
                  m_savedUpgradeSettings = loaded;
                  if (!keepEdits) {
                      m_upgradeSettings = loaded;
                      Q_EMIT settingsValuesChanged();
                  }
                  settingsChanged();
              });
}

void Updates::save() {
    if (m_saving) {
        return;
    }
    m_saving = true;
    if (std::exchange(m_settingsReadInFlight, false)) {
        ++m_latestSettingsRequest;
        m_reloadSavedAfterSave = true;
    }
    const UpgradeSettings wanted = m_upgradeSettings;
    runHelper(upgradeSettingsArguments(wanted), true,
              [this, wanted](const int exitCode, const QByteArray &, const QByteArray &err) {
                  m_saving = false;
                  if (exitCode == 0) {
                      m_savedUpgradeSettings = wanted;
                  } else if (Pkexec::isAuthenticationFailure(exitCode)) {
                      setActionError(i18n("Authentication was canceled or failed. The settings were not saved."));
                  } else {
                      const QString detail = helperText(err);
                      setActionError(detail.isEmpty() ? i18n("The settings could not be saved.")
                                                      : i18n("The settings could not be saved: %1", detail));
                  }
                  const bool reloadSaved = std::exchange(m_reloadSavedAfterSave, false);
                  if (std::exchange(m_loadAfterSave, false)) {
                      load();
                  } else if (reloadSaved) {
                      const bool editsDifferFromSaved = exitCode != 0 || m_upgradeSettings != wanted;
                      loadSettings(editsDifferFromSaved);
                  }
                  settingsChanged();
              });
}

void Updates::clearError() {
    m_actionError.clear();
    m_refreshError.clear();
    updateErrorMessage();
}

void Updates::setActionError(const QString &message) {
    m_actionError = message;
    updateErrorMessage();
}

void Updates::setRefreshError(const QString &message) {
    m_refreshError = message;
    updateErrorMessage();
}

void Updates::updateErrorMessage() {
    const QString message = composeErrorMessage(m_actionError, m_refreshError);
    const bool onlyCheckingFailed = m_actionError.isEmpty() && !m_refreshError.isEmpty();
    if (m_errorMessage == message && m_errorRetryable == onlyCheckingFailed) {
        return;
    }
    m_errorMessage = message;
    m_errorRetryable = onlyCheckingFailed;
    Q_EMIT errorMessageChanged();
}

void Updates::clearSuccess() { setSuccessMessage({}); }

void Updates::showPendingSuccess() {
    if (std::exchange(m_showSuccessAfterNextCheck, false)) {
        setSuccessMessage(i18n("Updates installed."));
    }
}

void Updates::setSuccessMessage(const QString &message) {
    if (assign(m_successMessage, message)) {
        Q_EMIT successMessageChanged();
    }
}

void Updates::defaults() {
    m_upgradeSettings = UpgradeSettings{};
    notifySettingsEdited();
}

void Updates::notifySettingsEdited() {
    Q_EMIT settingsValuesChanged();
    settingsChanged();
}

void Updates::refreshSchedule() {
    const quint64 request = ++m_latestScheduleRequest;
    runHelper({QStringLiteral("schedule"), QStringLiteral("--status"), QStringLiteral("--json")}, false,
              [this, request](const int exitCode, const QByteArray &out, const QByteArray &) {
                  if (request != m_latestScheduleRequest) {
                      return;
                  }
                  bool scheduled = false;
                  bool partial = false;
                  if (exitCode == 0) {
                      parseScheduleStatus(out, &scheduled, &partial);
                  }
                  if (m_scheduledTonight != scheduled || m_tonightPartial != partial) {
                      m_scheduledTonight = scheduled;
                      m_tonightPartial = partial;
                      Q_EMIT scheduleChanged();
                  }
                  if (!m_scheduleChangeInFlight) {
                      setScheduling(false);
                  }
              });
}

void Updates::setScheduling(const bool value) {
    if (assign(m_scheduling, value)) {
        Q_EMIT schedulingChanged();
    }
}

void Updates::applyTonight() {
    changeSchedule({QStringLiteral("schedule")},
                   i18n("Authentication was canceled or failed. Updates were not scheduled."),
                   [](const QString &detail) {
                       return detail.isEmpty() ? i18n("Updates could not be scheduled.")
                                               : i18n("Updates could not be scheduled: %1", detail);
                   });
}

void Updates::cancelTonight() {
    changeSchedule({QStringLiteral("schedule"), QStringLiteral("--cancel")},
                   i18n("Authentication was canceled or failed. Tonight's updates are still scheduled."),
                   [](const QString &detail) {
                       return detail.isEmpty() ? i18n("Tonight's updates could not be canceled.")
                                               : i18n("Tonight's updates could not be canceled: %1", detail);
                   });
}

void Updates::changeSchedule(const QStringList &arguments, const QString &authenticationFailedMessage,
                             const std::function<QString(const QString &detail)> &failedMessage) {
    if (m_running || m_scheduling) {
        return;
    }
    m_scheduleChangeInFlight = true;
    setScheduling(true);
    runHelper(arguments, true,
              [this, authenticationFailedMessage, failedMessage](const int exitCode, const QByteArray &,
                                                                 const QByteArray &err) {
                  m_scheduleChangeInFlight = false;
                  if (Pkexec::isAuthenticationFailure(exitCode)) {
                      setActionError(authenticationFailedMessage);
                  } else if (exitCode != 0) {
                      setActionError(failedMessage(helperText(err)));
                  }
                  refreshSchedule();
              });
}

qint64 Updates::downloadSize() const { return m_updates.totalDownloadSize(); }

qint64 Updates::installedSizeChange() const { return m_updates.totalInstalledSizeChange(); }

bool Updates::imageMode() { return QFile::exists(QStringLiteral("/run/ostree-booted")); }

bool Updates::restartRequired() const { return m_restartState.required; }

bool Updates::offlinePending() const { return m_restartState.offlinePending; }

const QString &Updates::stagedVersion() const { return m_restartState.stagedVersion; }

void Updates::restart() {
    const QDBusMessage message =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
                                       QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("promptReboot"));
    const auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        if (call->isError()) {
            setActionError(i18n("Could not restart: %1", call->error().message()));
        }
        call->deleteLater();
    });
}

void Updates::refresh() {
    if (m_running) {
        return;
    }

    m_updates.clear();
    setTechnicalInformation({});
    setRefreshError({});
    setStatus(i18n("Checking for updates…"));

    const QString helper = UpdatesHelper::executable();
    if (helper.isEmpty()) {
        const QString message = helperNotInstalledMessage();
        setRefreshError(message);
        setStatus(message);
        showPendingSuccess();
        return;
    }

    m_running = true;
    Q_EMIT runningChanged();
    startProcess(helper, {QStringLiteral("list"), QStringLiteral("--json")}, Operation::Refresh);
}

void Updates::applyUpdates() {
    if (m_running || m_scheduling) {
        return;
    }

    m_running = true;
    Q_EMIT runningChanged();
    clearError();
    m_showSuccessAfterNextCheck = false;
    setSuccessMessage({});
    setStatus(i18n("Applying updates…"));

    const QString helper = UpdatesHelper::executable();
    if (helper.isEmpty()) {
        const QString message = helperNotInstalledMessage();
        setActionError(message);
        finishOperation(message);
        return;
    }
    startProcess(helper, {QStringLiteral("apply"), QStringLiteral("--yes")}, Operation::Install);
}

void Updates::startProcess(const QString &program, const QStringList &arguments, const Operation operation) {
    m_operation = operation;
    m_processStdout.clear();
    m_processStderr.clear();
    m_process = createProcess(operation == Operation::Install);
    connect(m_process, &QProcess::finished, this, &Updates::processFinished);
    connect(m_process, &QProcess::errorOccurred, this, &Updates::processError);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Updates::readProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &Updates::readProcessOutput);
    m_process->start(program, arguments);
}

void Updates::processFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    readProcessOutput();
    std::exchange(m_process, nullptr)->deleteLater();
    const Operation operation = std::exchange(m_operation, Operation::None);
    const QByteArray stdOut = std::exchange(m_processStdout, {});
    const QByteArray stdErr = std::exchange(m_processStderr, {});
    const bool normalExit = exitStatus == QProcess::NormalExit;

    if (const QString errors = helperText(stdErr); !errors.isEmpty()) {
        qWarning().noquote() << "kcm-updates: ultramarine-updates:" << errors;
    }

    switch (operation) {
    case Operation::Refresh: {
        m_updates.clear();
        parseListing(QString::fromUtf8(stdOut));
        const QString error = refreshErrorMessage(normalExit ? exitCode : -1, stdOut, stdErr);
        setRefreshError(error);
        finishOperation(normalExit && exitCode == 0
                            ? i18np("%1 update available.", "%1 updates available.", updateCount())
                            : error);
        showPendingSuccess();
        break;
    }
    case Operation::Install: {
        if (normalExit && exitCode == 0) {
            finishOperation(i18n("Updates installed."));
            m_showSuccessAfterNextCheck = true;
        } else {
            const QString message = installErrorMessage(exitCode, exitStatus, stdErr);
            setActionError(message);
            finishOperation(message);
        }
        const bool systemMayHaveChanged = !normalExit || !Pkexec::isAuthenticationFailure(exitCode);
        if (systemMayHaveChanged) {
            refresh();
            refreshSchedule();
        }
        break;
    }
    case Operation::None:
        break;
    }
}

void Updates::processError(const QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || !m_process) {
        return;
    }
    const bool refreshing = std::exchange(m_operation, Operation::None) == Operation::Refresh;
    std::exchange(m_process, nullptr)->deleteLater();
    const QString message = i18n("Could not start the update helper.");
    if (refreshing) {
        setRefreshError(message);
    } else {
        setActionError(message);
    }
    finishOperation(message);
    if (refreshing) {
        showPendingSuccess();
    }
}

void Updates::readProcessOutput() {
    if (!m_process) {
        return;
    }
    const QByteArray stdOut = m_process->readAllStandardOutput();
    const QByteArray stdErr = m_process->readAllStandardError();
    m_processStdout += stdOut;
    m_processStderr += stdErr;
    if (m_operation != Operation::Install || (stdOut.isEmpty() && stdErr.isEmpty())) {
        return;
    }
    if (const QString line = lastOutputLine(stdErr.isEmpty() ? m_processStdout : m_processStderr); !line.isEmpty()) {
        setStatus(line);
    }
}

void Updates::parseListing(const QString &output) {
    QString technical;
    RestartState restartState;
    for (const QList<UpdateItem> parsedUpdates = parseUpdateListing(output, &technical, nullptr, &restartState);
         const UpdateItem &item: parsedUpdates) {
        m_updates.addUpdate(item);
    }
    setTechnicalInformation(technical);
    if (restartState != m_restartState) {
        m_restartState = restartState;
        Q_EMIT restartStateChanged();
        Q_EMIT updateCountChanged();
    }
}

QString refreshErrorMessage(const int exitCode, const QByteArray &stdOut, const QByteArray &stdErr) {
    QStringList errors;
    const QJsonDocument document = QJsonDocument::fromJson(stdOut);
    const bool listed = document.isObject();
    if (listed) {
        parseUpdateListing(QString::fromUtf8(stdOut), nullptr, &errors);
    }
    QStringList messages;
    if (const QString offlineError = document.object().value(QStringLiteral("offline_error")).toString();
        !offlineError.isEmpty()) {
        messages << i18n("The updates could not be installed while restarting: %1", offlineError);
    }
    if (!errors.isEmpty()) {
        messages << i18n("Could not check for updates from: %1", errors.join(QStringLiteral(", ")));
    }
    if (!messages.isEmpty()) {
        return messages.join(QLatin1Char('\n'));
    }
    if (exitCode == 0) {
        return listed ? QString() : i18n("Checking for updates failed: the update helper’s output could not be read.");
    }
    if (exitCode < 0) {
        return i18n("Checking for updates failed: the update helper stopped unexpectedly.");
    }
    const QString detail = helperText(stdErr);
    return detail.isEmpty() ? i18n("Checking for updates failed (exit code %1).", exitCode)
                            : i18n("Checking for updates failed (exit code %1). %2", exitCode, detail);
}

QString installErrorMessage(const int exitCode, const QProcess::ExitStatus exitStatus, const QByteArray &stdErr) {
    if (exitStatus != QProcess::NormalExit) {
        return i18n("Updating failed: the update helper stopped unexpectedly.");
    }
    if (Pkexec::isAuthenticationFailure(exitCode)) {
        return i18n("Authentication was canceled or failed. No updates were installed.");
    }
    constexpr qsizetype shownStderrLines = 10;
    QStringList lines = helperText(stdErr).split(QLatin1Char('\n'));
    if (lines.size() > shownStderrLines) {
        lines = lines.sliced(lines.size() - shownStderrLines);
    }
    const QString detail = lines.join(QLatin1Char('\n')).trimmed();
    return detail.isEmpty() ? i18n("Updating failed (exit code %1).", exitCode)
                            : i18n("Updating failed (exit code %1). %2", exitCode, detail);
}

QString composeErrorMessage(const QString &actionError, const QString &refreshError) {
    if (actionError.isEmpty() || refreshError.isEmpty()) {
        return actionError.isEmpty() ? refreshError : actionError;
    }
    return actionError + QLatin1Char('\n') + refreshError;
}

QString helperText(const QByteArray &output) { return QString::fromUtf8(output).trimmed(); }

QString lastOutputLine(const QByteArray &output) {
    const QByteArray trimmed = output.trimmed();
    return helperText(trimmed.sliced(trimmed.lastIndexOf('\n') + 1));
}

QList<UpdateItem> parseUpdateListing(const QString &output, QString *technicalInformation, QStringList *errors,
                                     RestartState *restart) {
    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8());
    if (!document.isObject()) {
        return {};
    }

    const QJsonObject root = document.object();
    QList<UpdateItem> result;
    for (const QJsonArray updates = root.value(QStringLiteral("updates")).toArray(); const auto &value: updates) {
        const QJsonObject object = value.toObject();
        UpdateItem item;
        item.name = object.value(QStringLiteral("name")).toString();
        item.source = object.value(QStringLiteral("source")).toString();
        item.oldVersion = object.value(QStringLiteral("old_version")).toString();
        item.newVersion = object.value(QStringLiteral("new_version")).toString();
        item.icon = object.value(QStringLiteral("icon")).toString();
        item.kind = object.value(QStringLiteral("kind")).toString();
        item.backend = object.value(QStringLiteral("backend")).toString();
        item.appId = object.value(QStringLiteral("app_id")).toString();
        item.downloadSize = object.value(QStringLiteral("download_size")).toInteger();
        item.installedSizeChange = object.value(QStringLiteral("installed_size_change")).toInteger();
        if (item.icon.isEmpty() || item.kind == QLatin1String("runtime")) {
            item.icon = QStringLiteral("package-x-generic");
        }
        result.append(std::move(item));
    }

    const auto mostRecognizableFirst = [](const UpdateItem &item) {
        if (item.kind == QLatin1String("app")) {
            return 0;
        }
        if (item.kind == QLatin1String("addon")) {
            return 1;
        }
        if (item.kind == QLatin1String("runtime")) {
            return 3;
        }
        return 2;
    };
    std::ranges::stable_sort(result, {}, mostRecognizableFirst);

    if (technicalInformation) {
        technicalInformation->clear();
        for (const UpdateItem &item: std::as_const(result)) {
            *technicalInformation +=
                QStringLiteral("%1: %2 → %3 (%4)\n").arg(item.name, item.oldVersion, item.newVersion, item.source);
        }
    }
    if (errors) {
        errors->clear();
        for (const QJsonArray errorList = root.value(QStringLiteral("errors")).toArray();
             const auto &value: errorList) {
            if (const QString error = value.toString(); !error.isEmpty()) {
                errors->append(error);
            }
        }
    }
    if (restart) {
        restart->offlinePending = root.value(QStringLiteral("offline_pending")).toBool();
        restart->required = restart->offlinePending || root.value(QStringLiteral("restart_required")).toBool();
        restart->stagedVersion =
            restart->required ? root.value(QStringLiteral("staged_version")).toString() : QString();
    }
    return result;
}

bool UpgradeSettings::isValid() const { return !windowEnabled || windowStart != windowEnd; }

bool UpgradeSettings::operator==(const UpgradeSettings &other) const {
    return automatic == other.automatic && offline == other.offline && inactiveMinutes == other.inactiveMinutes &&
        windowEnabled == other.windowEnabled &&
        (!windowEnabled || (windowStart == other.windowStart && windowEnd == other.windowEnd));
}

bool parseUpgradeSettings(const QByteArray &json, UpgradeSettings *settings) {
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject object = document.object();
    UpgradeSettings parsed;
    parsed.automatic = object.value(QStringLiteral("automatic")).toBool(parsed.automatic);
    parsed.offline = object.value(QStringLiteral("offline")).toBool(parsed.offline);
    parsed.inactiveMinutes = object.value(QStringLiteral("inactive_minutes")).toInt(parsed.inactiveMinutes);
    const auto clockTime = [&object](const QString &key) {
        return QTime::fromString(object.value(key).toString(), QStringLiteral("H:mm"));
    };
    const QTime start = clockTime(QStringLiteral("window_start"));
    if (const QTime end = clockTime(QStringLiteral("window_end")); start.isValid() && end.isValid()) {
        parsed.windowEnabled = true;
        parsed.windowStart = start.hour() * 60 + start.minute();
        parsed.windowEnd = end.hour() * 60 + end.minute();
    }
    if (settings) {
        *settings = parsed;
    }
    return true;
}

QStringList upgradeSettingsArguments(const UpgradeSettings &settings) {
    const auto boolValue = [](const bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); };
    const auto clock = [](const int minutes) {
        return QStringLiteral("%1:%2")
            .arg(minutes / 60, 2, 10, QLatin1Char('0'))
            .arg(minutes % 60, 2, 10, QLatin1Char('0'));
    };
    const QString window =
        settings.windowEnabled ? clock(settings.windowStart) + QLatin1Char('-') + clock(settings.windowEnd) : QString();
    return {
        QStringLiteral("settings"),
        QStringLiteral("--automatic=") + boolValue(settings.automatic),
        QStringLiteral("--offline=") + boolValue(settings.offline),
        QStringLiteral("--inactive-minutes=") + QString::number(settings.inactiveMinutes),
        QStringLiteral("--window=") + window,
    };
}

bool parseScheduleStatus(const QByteArray &json, bool *scheduled, bool *partial) {
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject status = document.object();
    if (scheduled) {
        *scheduled = status.value(QStringLiteral("scheduled")).toBool();
    }
    if (partial) {
        *partial = status.value(QStringLiteral("partial")).toBool();
    }
    return true;
}

QString updateSourceLabel(const UpdateItem &item) {
    if (item.backend == QLatin1String("rpm") || item.backend == QLatin1String("bootc") ||
        item.backend == QLatin1String("rpm-ostree")) {
        return i18nc("@info update source, the operating system", "System");
    }
    if (item.backend == QLatin1String("flatpak")) {
        return QStringLiteral("Flatpak");
    }
    if (item.backend == QLatin1String("nix")) {
        return QStringLiteral("Nix");
    }
    if (item.backend == QLatin1String("plasmoid")) {
        return i18nc("@info update source", "Plasma Widget");
    }
    return item.source;
}

void Updates::finishOperation(const QString &statusText) {
    if (m_running) {
        m_running = false;
        Q_EMIT runningChanged();
    }
    setStatus(statusText);
}

void Updates::setStatus(const QString &value) {
    if (assign(m_status, value)) {
        Q_EMIT statusChanged();
    }
}

void Updates::setTechnicalInformation(const QString &information) {
    if (assign(m_technicalInformation, information)) {
        Q_EMIT technicalInformationChanged();
    }
}
