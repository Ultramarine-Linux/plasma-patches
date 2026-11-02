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
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QTime>

#include "updates.h"
#include "updateshelper.h"

K_PLUGIN_CLASS_WITH_JSON(Updates, "kcm_ultramarine_updates.json")

#include "updates.moc"

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

qint64 UpdatesModel::totalDownloadSize() const {
    return std::accumulate(m_updates.cbegin(), m_updates.cend(), qint64{0},
                           [](const qint64 total, const UpdateItem &item) { return total + item.downloadSize; });
}

qint64 UpdatesModel::totalInstalledSizeChange() const {
    return std::accumulate(m_updates.cbegin(), m_updates.cend(), qint64{0},
                           [](const qint64 total, const UpdateItem &item) { return total + item.installedSizeChange; });
}

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

Updates::Updates(QObject *parent, const KPluginMetaData &metaData) :
    KQuickManagedConfigModule(parent, metaData), m_updates(this) {
    // Apply/Reset save the Automatic Updates and offline update settings through
    // the update helper. Updates themselves are installed with the page's own Apply button.
    setButtons(Apply);
    connect(&m_updates, &UpdatesModel::countChanged, this, &Updates::updateCountChanged);
    connect(&m_process, &QProcess::finished, this, &Updates::processFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &Updates::processError);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Updates::readProcessOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &Updates::readProcessOutput);
    refresh();
}

bool Updates::running() const { return m_running; }

bool Updates::scheduling() const { return m_scheduling; }

const QString &Updates::status() const { return m_status; }

const QString &Updates::errorMessage() const { return m_errorMessage; }

QString Updates::distributionVersion() {
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (!osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    for (const QStringList entries = QString::fromUtf8(osRelease.readAll()).split(QLatin1Char('\n'));
         const QString &entry: entries) {
        if (entry.startsWith(QLatin1String("VERSION_ID="))) {
            return entry.sliced(11).remove(QLatin1Char('"'));
        }
    }
    return {};
}

int Updates::updateCount() const { return m_updates.count(); }

int Updates::installableCount() const {
    return m_restartState.offlinePending ? m_updates.countExcluding(QStringLiteral("rpm")) : m_updates.count();
}

UpdatesModel *Updates::updates() { return &m_updates; }

const QString &Updates::technicalInformation() const { return m_technicalInformation; }

bool Updates::automaticUpdates() const { return m_upgradeSettings.automatic; }

void Updates::setAutomaticUpdates(const bool automatic) {
    if (m_upgradeSettings.automatic == automatic) {
        return;
    }
    m_upgradeSettings.automatic = automatic;
    Q_EMIT settingsValuesChanged();
    settingsChanged();
}

bool Updates::offlineUpdates() const { return m_upgradeSettings.offline; }

void Updates::setOfflineUpdates(const bool offline) {
    if (m_upgradeSettings.offline == offline) {
        return;
    }
    m_upgradeSettings.offline = offline;
    Q_EMIT settingsValuesChanged();
    settingsChanged();
}

int Updates::inactiveMinutes() const { return m_upgradeSettings.inactiveMinutes; }

void Updates::setInactiveMinutes(int minutes) {
    minutes = std::clamp(minutes, 0, 24 * 60);
    if (m_upgradeSettings.inactiveMinutes == minutes) {
        return;
    }
    m_upgradeSettings.inactiveMinutes = minutes;
    Q_EMIT settingsValuesChanged();
    settingsChanged();
}

bool Updates::windowEnabled() const { return m_upgradeSettings.windowEnabled; }

void Updates::setWindowEnabled(const bool enabled) {
    if (m_upgradeSettings.windowEnabled == enabled) {
        return;
    }
    m_upgradeSettings.windowEnabled = enabled;
    Q_EMIT settingsValuesChanged();
    settingsChanged();
}

int Updates::windowStart() const { return m_upgradeSettings.windowStart; }

void Updates::setWindowStart(int minutes) {
    minutes = std::clamp(minutes, 0, 24 * 60 - 1);
    if (m_upgradeSettings.windowStart == minutes) {
        return;
    }
    m_upgradeSettings.windowStart = minutes;
    Q_EMIT settingsValuesChanged();
    settingsChanged();
}

int Updates::windowEnd() const { return m_upgradeSettings.windowEnd; }

void Updates::setWindowEnd(int minutes) {
    minutes = std::clamp(minutes, 0, 24 * 60 - 1);
    if (m_upgradeSettings.windowEnd == minutes) {
        return;
    }
    m_upgradeSettings.windowEnd = minutes;
    Q_EMIT settingsValuesChanged();
    settingsChanged();
}

bool Updates::windowValid() const { return m_upgradeSettings.isValid(); }

bool Updates::scheduledTonight() const { return m_scheduledTonight; }

QDateTime Updates::scheduledTime() const { return m_scheduledTime; }

bool Updates::isSaveNeeded() const {
    // the helper rejects an empty window, and AdvancedSettings.qml points it out
    return m_upgradeSettings.isValid() && m_upgradeSettings != m_savedUpgradeSettings;
}

bool Updates::isDefaults() const { return m_upgradeSettings == UpgradeSettings{}; }

void Updates::runHelper(const QStringList &arguments,
                        const std::function<void(int exitCode, const QByteArray &out, const QByteArray &err)> &done) {
    const QString helper = UpdatesHelper::executable();
    if (helper.isEmpty()) {
        done(-1, {}, {});
        return;
    }
    auto *process = new QProcess(this);
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
    runHelper({QStringLiteral("settings"), QStringLiteral("--json")},
              [this](const int exitCode, const QByteArray &out, const QByteArray &err) {
                  UpgradeSettings loaded;
                  if (exitCode != 0 || !parseUpgradeSettings(out, &loaded)) {
                      // unreadable settings: show the defaults, and say so
                      loaded = UpgradeSettings{};
                      const QString detail = QString::fromLocal8Bit(err).trimmed();
                      setErrorMessage(
                          detail.isEmpty()
                              ? i18n("The update settings could not be loaded. Showing the defaults.")
                              : i18n("The update settings could not be loaded. Showing the defaults. %1", detail));
                  }
                  m_upgradeSettings = loaded;
                  m_savedUpgradeSettings = loaded;
                  Q_EMIT settingsValuesChanged();
                  settingsChanged();
              });
    refreshSchedule();
}

void Updates::save() {
    const UpgradeSettings wanted = m_upgradeSettings;
    runHelper(upgradeSettingsArguments(wanted),
              [this, wanted](const int exitCode, const QByteArray &, const QByteArray &err) {
                  if (exitCode == 0) {
                      m_savedUpgradeSettings = wanted;
                  } else if (Pkexec::isAuthenticationFailure(exitCode)) {
                      setErrorMessage(i18n("Authentication was cancelled or failed. The settings were not saved."));
                  } else {
                      const QString detail = QString::fromUtf8(err).trimmed();
                      setErrorMessage(detail.isEmpty() ? i18n("The settings could not be saved.")
                                                       : i18n("The settings could not be saved: %1", detail));
                  }
                  // keeps Apply enabled if saving failed
                  settingsChanged();
              });
}

void Updates::clearError() { setErrorMessage({}); }

void Updates::setErrorMessage(const QString &message) {
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    Q_EMIT errorMessageChanged();
}

void Updates::defaults() {
    m_upgradeSettings = UpgradeSettings{};
    Q_EMIT settingsValuesChanged();
    settingsChanged();
}

void Updates::refreshSchedule() {
    const quint64 request = ++m_scheduleRequest;
    runHelper({QStringLiteral("schedule"), QStringLiteral("--status"), QStringLiteral("--json")},
              [this, request](const int exitCode, const QByteArray &out, const QByteArray &) {
                  // a later request (like after Apply Tonight) is newer than this reply
                  if (request != m_scheduleRequest) {
                      return;
                  }
                  bool scheduled = false;
                  qint64 next = 0;
                  if (exitCode == 0) {
                      parseScheduleStatus(out, &scheduled, &next);
                  }
                  m_scheduledTonight = scheduled;
                  m_scheduledTime = next > 0 ? QDateTime::fromSecsSinceEpoch(next) : QDateTime();
                  Q_EMIT scheduleChanged();
                  if (!m_scheduleChanging) {
                      setScheduling(false);
                  }
              });
}

void Updates::setScheduling(const bool value) {
    if (m_scheduling == value) {
        return;
    }
    m_scheduling = value;
    Q_EMIT schedulingChanged();
}

void Updates::applyTonight() {
    if (m_running || m_scheduling) {
        return;
    }
    // stays set until the status read afterwards arrives, so the button can't be
    // pressed again (another authentication prompt) while it still shows the old state
    m_scheduleChanging = true;
    setScheduling(true);
    runHelper({QStringLiteral("schedule")}, [this](const int exitCode, const QByteArray &, const QByteArray &err) {
        m_scheduleChanging = false;
        if (Pkexec::isAuthenticationFailure(exitCode)) {
            setErrorMessage(i18n("Authentication was cancelled or failed. Updates were not scheduled."));
        } else if (exitCode != 0) {
            const QString detail = QString::fromLocal8Bit(err).trimmed();
            setErrorMessage(detail.isEmpty() ? i18n("Updates could not be scheduled.")
                                             : i18n("Updates could not be scheduled: %1", detail));
        }
        refreshSchedule();
    });
}

void Updates::cancelTonight() {
    if (m_scheduling) {
        return;
    }
    m_scheduleChanging = true;
    setScheduling(true);
    runHelper({QStringLiteral("schedule"), QStringLiteral("--cancel")},
              [this](const int exitCode, const QByteArray &, const QByteArray &err) {
                  m_scheduleChanging = false;
                  if (Pkexec::isAuthenticationFailure(exitCode)) {
                      setErrorMessage(
                          i18n("Authentication was cancelled or failed. Tonight's updates are still scheduled."));
                  } else if (exitCode != 0) {
                      const QString detail = QString::fromLocal8Bit(err).trimmed();
                      setErrorMessage(detail.isEmpty() ? i18n("Tonight's updates could not be cancelled.")
                                                       : i18n("Tonight's updates could not be cancelled: %1", detail));
                  }
                  refreshSchedule();
              });
}

qint64 Updates::downloadSize() const { return m_updates.totalDownloadSize(); }

qint64 Updates::installedSizeChange() const { return m_updates.totalInstalledSizeChange(); }

bool Updates::imageMode() {
    // the update helper decides the same way
    return QFile::exists(QStringLiteral("/run/ostree-booted"));
}

bool Updates::restartRequired() const { return m_restartState.required; }

bool Updates::offlinePending() const { return m_restartState.offlinePending; }

const QString &Updates::stagedVersion() const { return m_restartState.stagedVersion; }

void Updates::restart() {
    const QDBusMessage message =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
                                       QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("promptReboot"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        if (call->isError()) {
            setErrorMessage(i18n("Could not restart: %1", call->error().message()));
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
    if (m_restartState != RestartState{}) {
        m_restartState = {};
        Q_EMIT restartStateChanged();
        // installableCount depends on offlinePending
        Q_EMIT updateCountChanged();
    }
    setErrorMessage({});
    setStatus(i18n("Checking for updates…"));

    const QString helper = UpdatesHelper::executable();
    if (helper.isEmpty()) {
        setStatus(i18n("The update helper (ultramarine-updates) is not installed."));
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
    setErrorMessage({});
    setStatus(i18n("Applying updates…"));

    const QString helper = UpdatesHelper::executable();
    if (helper.isEmpty()) {
        finishOperation(false, i18n("The update helper (ultramarine-updates) is not installed."));
        return;
    }
    // the helper runs the system part through pkexec first and only then updates the
    // user's Flatpaks, Nix profile and widgets as us. Once it runs as root, it can't be
    // stopped from here, so there is no Cancel.
    startProcess(helper, {QStringLiteral("apply"), QStringLiteral("--yes")}, Operation::Install);
}

void Updates::startProcess(const QString &program, const QStringList &arguments, const Operation operation) {
    m_operation = operation;
    m_processStdout.clear();
    m_processStderr.clear();
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.start();
}

void Updates::processFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    readProcessOutput();
    const Operation operation = std::exchange(m_operation, Operation::None);
    const QByteArray stdOut = std::exchange(m_processStdout, {});
    const QByteArray stdErr = std::exchange(m_processStderr, {});
    const bool normalExit = exitStatus == QProcess::NormalExit;

    // only stderr is logged, since stdout is the (large) update listing or progress output
    if (const QString errors = QString::fromLocal8Bit(stdErr).trimmed(); !errors.isEmpty()) {
        qWarning().noquote() << "kcm-updates: ultramarine-updates:" << errors;
    }

    switch (operation) {
    case Operation::Refresh: {
        // `list` still prints a listing when it fails (exit code 1 if no source worked)
        m_updates.clear();
        parseListing(QString::fromUtf8(stdOut));
        const QString error = refreshErrorMessage(normalExit ? exitCode : -1, stdOut, stdErr);
        if (normalExit && exitCode == 0) {
            finishOperation(true, i18np("%1 update available.", "%1 updates available.", updateCount()));
            // some sources failed and the others are still listed. Don't clear the banner
            // otherwise, it may show a settings or schedule error that came in during the check.
            if (!error.isEmpty()) {
                setErrorMessage(error);
            }
        } else {
            finishOperation(false, error);
        }
        break;
    }
    case Operation::Install: {
        if (normalExit && exitCode == 0) {
            finishOperation(true, i18n("Updates installed."));
            // re-check so the page reflects what is still pending
            refresh();
            break;
        }
        // the helper asks for authentication before changing anything
        if (normalExit && Pkexec::isAuthenticationFailure(exitCode)) {
            finishOperation(false, i18n("Authentication was cancelled or failed. No updates were installed."));
            break;
        }
        QStringList parts;
        for (const QByteArray &output: {stdOut, stdErr}) {
            if (const QString text = QString::fromLocal8Bit(output).trimmed(); !text.isEmpty()) {
                parts.append(text);
            }
        }
        const QString output = parts.join(QLatin1Char('\n'));
        const bool isNoUpdateMessage = output.contains(QLatin1String("no updates"), Qt::CaseInsensitive) ||
            output.contains(QLatin1String("nothing to update"), Qt::CaseInsensitive);
        finishOperation(false,
                        output.isEmpty() || isNoUpdateMessage
                            ? i18n("Updating failed (exit code %1).", exitCode)
                            : i18n("Updating failed (exit code %1). %2", exitCode, output));
        break;
    }
    case Operation::None:
        break;
    }
}

void Updates::processError(const QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        m_operation = Operation::None;
        finishOperation(false, i18n("Could not start the update helper."));
    }
}

void Updates::readProcessOutput() {
    const QByteArray stdOut = m_process.readAllStandardOutput();
    const QByteArray stdErr = m_process.readAllStandardError();
    m_processStdout += stdOut;
    m_processStderr += stdErr;
    // a refresh's listing is parsed once the process exits, while installs show their latest line
    if (m_operation != Operation::Install || (stdOut.isEmpty() && stdErr.isEmpty())) {
        return;
    }
    // the whole buffer is decoded so a character split across reads isn't garbled
    const QByteArray &latest = stdErr.isEmpty() ? m_processStdout : m_processStderr;
    const QByteArray trimmed = latest.trimmed();
    if (const QString line = QString::fromLocal8Bit(trimmed.sliced(trimmed.lastIndexOf('\n') + 1)).trimmed();
        !line.isEmpty()) {
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
        // installableCount depends on offlinePending
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
    const QString detail = QString::fromLocal8Bit(stdErr).trimmed();
    return detail.isEmpty() ? i18n("Checking for updates failed (exit code %1).", exitCode)
                            : i18n("Checking for updates failed (exit code %1). %2", exitCode, detail);
}

QList<UpdateItem> parseUpdateListing(const QString &output, QString *technicalInformation, QStringList *errors,
                                     RestartState *restart) {
    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8());
    if (!document.isObject()) {
        return {};
    }

    QList<UpdateItem> result;
    for (const QJsonArray updates = document.object().value(QStringLiteral("updates")).toArray();
         const auto &value: updates) {
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
        // runtimes have no meaningful icon of their own, and anything without
        // an icon gets the generic one too
        if (item.icon.isEmpty() || item.kind == QLatin1String("runtime")) {
            item.icon = QStringLiteral("package-x-generic");
        }
        result.append(std::move(item));
    }

    // show what people recognize first: desktop apps and widgets, then packages, then runtimes
    const auto rank = [](const UpdateItem &item) {
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
    std::ranges::stable_sort(result, [&rank](const UpdateItem &a, const UpdateItem &b) { return rank(a) < rank(b); });

    QString technical;
    for (const UpdateItem &item: std::as_const(result)) {
        technical += QStringLiteral("%1: %2 → %3 (%4)\n").arg(item.name, item.oldVersion, item.newVersion, item.source);
    }

    if (technicalInformation) {
        *technicalInformation = technical;
    }
    if (errors) {
        errors->clear();
        for (const QJsonArray errorList = document.object().value(QStringLiteral("errors")).toArray();
             const auto &value: errorList) {
            if (const QString error = value.toString(); !error.isEmpty()) {
                errors->append(error);
            }
        }
    }
    if (restart) {
        restart->offlinePending = document.object().value(QStringLiteral("offline_pending")).toBool();
        // offline updates always need a restart, even if an older helper didn't say so
        restart->required =
            restart->offlinePending || document.object().value(QStringLiteral("restart_required")).toBool();
        restart->stagedVersion =
            restart->required ? document.object().value(QStringLiteral("staged_version")).toString() : QString();
    }
    return result;
}

bool UpgradeSettings::isValid() const { return !windowEnabled || windowStart != windowEnd; }

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
    const QTime start =
        QTime::fromString(object.value(QStringLiteral("window_start")).toString(), QStringLiteral("H:mm"));
    if (const QTime end =
            QTime::fromString(object.value(QStringLiteral("window_end")).toString(), QStringLiteral("H:mm"));
        start.isValid() && end.isValid()) {
        parsed.windowEnabled = true;
        parsed.windowStart = start.msecsSinceStartOfDay() / 60000;
        parsed.windowEnd = end.msecsSinceStartOfDay() / 60000;
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

bool parseScheduleStatus(const QByteArray &json, bool *scheduled, qint64 *next) {
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject object = document.object();
    if (scheduled) {
        *scheduled = object.value(QStringLiteral("scheduled")).toBool();
    }
    if (next) {
        *next = object.value(QStringLiteral("next")).toInteger();
    }
    return true;
}

QString updateSourceLabel(const UpdateItem &item) {
    if (item.backend == QLatin1String("rpm") || item.backend == QLatin1String("bootc") ||
        item.backend == QLatin1String("rpm-ostree")) {
        return i18n("System");
    }
    if (item.backend == QLatin1String("flatpak")) {
        return i18n("Flatpak");
    }
    if (item.backend == QLatin1String("nix")) {
        return i18n("Nix");
    }
    if (item.backend == QLatin1String("plasmoid")) {
        return i18nc("@info update source", "Plasma Widget");
    }
    return item.source;
}

void Updates::finishOperation(const bool success, const QString &message) {
    if (!success) {
        if (m_errorMessage != message) {
            m_errorMessage = message;
            Q_EMIT errorMessageChanged();
        }
    }
    if (m_running) {
        m_running = false;
        Q_EMIT runningChanged();
    }
    setStatus(message);
}

void Updates::setStatus(const QString &value) {
    if (m_status == value) {
        return;
    }
    m_status = value;
    Q_EMIT statusChanged();
}

void Updates::setTechnicalInformation(const QString &information) {
    if (m_technicalInformation == information) {
        return;
    }
    m_technicalInformation = information;
    Q_EMIT technicalInformationChanged();
}
