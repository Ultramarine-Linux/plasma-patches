// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only


#include <KLocalizedString>
#include <KPluginFactory>
#include <algorithm>

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

qint64 UpdatesModel::totalDownloadSize() const {
    qint64 total = 0;
    for (const UpdateItem &item: m_updates) {
        total += item.downloadSize;
    }
    return total;
}

qint64 UpdatesModel::totalInstalledSizeChange() const {
    qint64 total = 0;
    for (const UpdateItem &item: m_updates) {
        total += item.installedSizeChange;
    }
    return total;
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

QString Updates::status() const { return m_status; }

QString Updates::errorMessage() const { return m_errorMessage; }

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

UpdatesModel *Updates::updates() { return &m_updates; }

QString Updates::technicalInformation() const { return m_technicalInformation; }

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

bool Updates::scheduledTonight() const { return m_scheduledTonight; }

QDateTime Updates::scheduledTime() const { return m_scheduledTime; }

bool Updates::isSaveNeeded() const { return m_upgradeSettings != m_savedUpgradeSettings; }

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
              [this](const int exitCode, const QByteArray &out, const QByteArray &) {
                  UpgradeSettings loaded;
                  if (exitCode != 0 || !parseUpgradeSettings(out, &loaded)) {
                      // Unreadable settings: show the defaults.
                      loaded = UpgradeSettings{};
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
                                                       : i18n("The settings could not be saved: %1").arg(detail));
                  }
                  // Keeps Apply enabled if saving failed.
                  settingsChanged();
              });
}

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
    runHelper({QStringLiteral("schedule"), QStringLiteral("--status"), QStringLiteral("--json")},
              [this](const int exitCode, const QByteArray &out, const QByteArray &) {
                  bool scheduled = false;
                  qint64 next = 0;
                  if (exitCode == 0) {
                      parseScheduleStatus(out, &scheduled, &next);
                  }
                  m_scheduledTonight = scheduled;
                  m_scheduledTime = next > 0 ? QDateTime::fromSecsSinceEpoch(next) : QDateTime();
                  Q_EMIT scheduleChanged();
              });
}

void Updates::applyTonight() {
    runHelper({QStringLiteral("schedule")}, [this](const int exitCode, const QByteArray &, const QByteArray &err) {
        if (Pkexec::isAuthenticationFailure(exitCode)) {
            setErrorMessage(i18n("Authentication was cancelled or failed. Updates were not scheduled."));
        } else if (exitCode != 0) {
            const QString detail = QString::fromUtf8(err).trimmed();
            setErrorMessage(detail.isEmpty() ? i18n("Updates could not be scheduled.")
                                             : i18n("Updates could not be scheduled: %1").arg(detail));
        }
        refreshSchedule();
    });
}

void Updates::cancelTonight() {
    runHelper({QStringLiteral("schedule"), QStringLiteral("--cancel")},
              [this](const int exitCode, const QByteArray &, const QByteArray &) {
                  if (Pkexec::isAuthenticationFailure(exitCode)) {
                      setErrorMessage(
                          i18n("Authentication was cancelled or failed. Tonight's updates are still scheduled."));
                  }
                  refreshSchedule();
              });
}

qint64 Updates::downloadSize() const { return m_updates.totalDownloadSize(); }

qint64 Updates::installedSizeChange() const { return m_updates.totalInstalledSizeChange(); }

void Updates::refresh() {
    if (m_running) {
        return;
    }

    m_updates.clear();
    setTechnicalInformation({});
    if (!m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        Q_EMIT errorMessageChanged();
    }
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
    if (m_running) {
        return;
    }

    m_running = true;
    Q_EMIT runningChanged();
    if (!m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        Q_EMIT errorMessageChanged();
    }
    setStatus(i18n("Applying updates…"));

    const QString helper = UpdatesHelper::executable();
    if (helper.isEmpty()) {
        finishOperation(false, i18n("The update helper (ultramarine-updates) is not installed."));
        return;
    }
    // The helper updates user-installation Flatpaks and Nix as us, then escalates through pkexec.
    startProcess(helper, {QStringLiteral("apply"), QStringLiteral("--yes")}, Operation::Install);
}


void Updates::cancel() {
    if (!m_running) {
        return;
    }
    m_process.kill();
    m_operation = Operation::None;
    finishOperation(false, i18n("Operation canceled."));
}

void Updates::startProcess(const QString &program, const QStringList &arguments, const Operation operation) {
    m_operation = operation;
    m_lastProcessOutput.clear();
    m_refreshStdout.clear();
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.start();
}

void Updates::processFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    readProcessOutput();
    const Operation operation = m_operation;
    m_operation = Operation::None;
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;

    if (!success && operation == Operation::Install && exitStatus == QProcess::NormalExit &&
        Pkexec::isAuthenticationFailure(exitCode)) {
        finishOperation(false, i18n("Authentication was cancelled or failed. No updates were installed."));
        return;
    }

    if (!success) {
        const bool isNoUpdateMessage = m_lastProcessOutput.contains(QLatin1String("no updates"), Qt::CaseInsensitive) ||
            m_lastProcessOutput.contains(QLatin1String("nothing to update"), Qt::CaseInsensitive);
        const QString detail = m_lastProcessOutput.isEmpty() || isNoUpdateMessage
            ? QString()
            : QStringLiteral(" %1").arg(m_lastProcessOutput);
        finishOperation(false, i18n("Updating failed (exit code %1).%2").arg(exitCode).arg(detail));
        return;
    }

    if (!m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        Q_EMIT errorMessageChanged();
    }

    switch (operation) {
    case Operation::Refresh:
        m_updates.clear();
        parseListing(QString::fromUtf8(m_refreshStdout));
        finishOperation(true, i18np("%1 update available.", "%1 updates available.", updateCount()));
        break;
    case Operation::Install:
        finishOperation(true, i18n("Updates installed."));
        // Re-check so the page reflects what is still pending.
        refresh();
        break;
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
    if (m_operation == Operation::Refresh) {
        m_refreshStdout += stdOut;
    }
    const QByteArray stdErr = m_process.readAllStandardError();
    if (const QString errors = QString::fromLocal8Bit(stdErr).trimmed(); !errors.isEmpty()) {
        // Only stderr is logged; stdout is the (large) update listing or progress output.
        qWarning().noquote() << "kcm-updates: ultramarine-updates:" << errors;
    }
    const QString text = QString::fromLocal8Bit(stdOut + stdErr).trimmed();
    if (text.isEmpty()) {
        return;
    }
    m_lastProcessOutput += text;

    switch (m_operation) {
    case Operation::Refresh:
        // Parse the complete JSON document after the process exits.
        break;
    default:
        setStatus(text.split(QLatin1Char('\n')).constLast().trimmed());
        break;
    }
}

void Updates::parseListing(const QString &output) {
    QString technical;
    QStringList errors;
    for (const QList<UpdateItem> parsedUpdates = parseUpdateListing(output, &technical, &errors);
         const UpdateItem &item: parsedUpdates) {
        m_updates.addUpdate(item);
    }
    setTechnicalInformation(technical);
    if (!errors.isEmpty()) {
        // The helper still lists updates from the sources that worked.
        m_errorMessage = i18n("Could not check for updates from: %1").arg(errors.join(QStringLiteral(", ")));
        Q_EMIT errorMessageChanged();
    }
}

QList<UpdateItem> parseUpdateListing(const QString &output, QString *technicalInformation, QStringList *errors) {
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
        // Runtimes have no meaningful icon of their own. Listings without a
        // kind, so their icons are kept as is.
        if (item.icon.isEmpty() || item.kind == QLatin1String("runtime")) {
            item.icon = QStringLiteral("package-x-generic");
        }
        result.append(std::move(item));
    }

    // Show what people recognize first: desktop apps, then packages, then runtimes.
    const auto rank = [](const UpdateItem &item) {
        if (item.kind == QLatin1String("app")) {
            return 0;
        }
        if (item.kind == QLatin1String("runtime")) {
            return 2;
        }
        return 1;
    };
    std::ranges::stable_sort(result, [&rank](const UpdateItem &a, const UpdateItem &b) { return rank(a) < rank(b); });

    QString technical;
    for (const UpdateItem &item: std::as_const(result)) {
        technical += i18n("%1: %2 → %3 (%4)\n").arg(item.name, item.oldVersion, item.newVersion, item.source);
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
    return result;
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
    const QTime start =
        QTime::fromString(object.value(QStringLiteral("window_start")).toString(), QStringLiteral("HH:mm"));
    if (const QTime end =
            QTime::fromString(object.value(QStringLiteral("window_end")).toString(), QStringLiteral("HH:mm"));
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

void Updates::setStatus(const QString &status) {
    if (m_status == status) {
        return;
    }
    m_status = status;
    Q_EMIT statusChanged();
}

void Updates::setTechnicalInformation(const QString &information) {
    if (m_technicalInformation == information) {
        return;
    }
    m_technicalInformation = information;
    Q_EMIT technicalInformationChanged();
}
