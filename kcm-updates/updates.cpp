// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <KPluginFactory>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QStandardPaths>
#include <QDebug>
#include <QFile>
#include <QStringList>

#include "updates.h"

K_PLUGIN_CLASS_WITH_JSON(Updates, "kcm_updates.json")

#include "updates.moc"

namespace {
    QString executablePath(const QString &name) { return QStandardPaths::findExecutable(name); }

    QStringList lines(const QString &output) { return output.split(QLatin1Char('\n'), Qt::SkipEmptyParts); }

    QString commandOutput(const QString &program, const QStringList &arguments) {
        QProcess process;
        process.start(program, arguments);
        if (!process.waitForFinished(5000) || process.exitCode() != 0) {
            return {};
        }
        return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    }
} // namespace

UpdatesModel::UpdatesModel(QObject *parent) : QAbstractListModel(parent) {}

int UpdatesModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : static_cast<int>(m_updates.size()); }

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
    default:
        return {};
    }
}

QHash<int, QByteArray> UpdatesModel::roleNames() const {
    return {
        {NameRole, "name"}, {SourceRole, "source"}, {OldVersionRole, "oldVersion"}, {NewVersionRole, "newVersion"},
        {IconRole, "icon"},
    };
}

int UpdatesModel::count() const { return static_cast<int>(m_updates.size()); }

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
    const KConfigGroup config(KSharedConfig::openConfig(QStringLiteral("kcm-updatesrc")), QStringLiteral("Updates"));
    m_offlineUpdatesEnabled = config.readEntry(QStringLiteral("OfflineUpdatesEnabled"), true);
    connect(&m_process, &QProcess::finished, this, &Updates::processFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &Updates::processError);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Updates::readProcessOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &Updates::readProcessOutput);
    refresh();
}

bool Updates::running() const { return m_running; }

QString Updates::status() const { return m_status; }

QString Updates::errorMessage() const { return m_errorMessage; }

QString Updates::distributionVersion() const {
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (!osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QStringList entries = QString::fromUtf8(osRelease.readAll()).split(QLatin1Char('\n'));
    for (const QString &entry : entries) {
        if (entry.startsWith(QLatin1String("VERSION_ID="))) {
            return entry.sliced(11).remove(QLatin1Char('"'));
        }
    }
    return {};
}

int Updates::updateCount() const { return m_updates.count(); }

UpdatesModel *Updates::updates() { return &m_updates; }

QString Updates::technicalInformation() const { return m_technicalInformation; }

bool Updates::offlineUpdatesEnabled() const { return m_offlineUpdatesEnabled; }

void Updates::setOfflineUpdatesEnabled(bool enabled) {
    if (m_offlineUpdatesEnabled == enabled) {
        return;
    }
    m_offlineUpdatesEnabled = enabled;
    KConfigGroup config(KSharedConfig::openConfig(QStringLiteral("kcm-updatesrc")), QStringLiteral("Updates"));
    config.writeEntry(QStringLiteral("OfflineUpdatesEnabled"), enabled);
    config.sync();
    Q_EMIT offlineUpdatesEnabledChanged();
}

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
    setStatus(tr("Checking for updates…"));

    const QString pkcon = executablePath(QStringLiteral("pkcon"));
    if (pkcon.isEmpty()) {
        setStatus(tr("PackageKit (pkcon) is not installed."));
        return;
    }

    m_running = true;
    Q_EMIT runningChanged();
    startProcess(pkcon, {QStringLiteral("get-updates"), QStringLiteral("--plain")}, Operation::RefreshPackageKit);
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
    setStatus(tr("Applying updates…"));

    const QString pkcon = executablePath(QStringLiteral("pkcon"));
    if (!pkcon.isEmpty()) {
        startProcess(pkcon, {QStringLiteral("update"), QStringLiteral("-y")}, Operation::ApplyNowPackageKit);
    } else {
        const QString flatpak = executablePath(QStringLiteral("flatpak"));
        if (!flatpak.isEmpty()) {
            startProcess(flatpak, {QStringLiteral("update"), QStringLiteral("--noninteractive")}, Operation::ApplyNowFlatpak);
        } else {
            finishOperation(false, tr("No Fedora update tools were found."));
        }
    }
}

void Updates::applyOfflineUpdates() {
    if (m_running || !m_offlineUpdatesEnabled) {
        return;
    }

    const QString pkcon = executablePath(QStringLiteral("pkcon"));
    if (pkcon.isEmpty()) {
        setStatus(tr("PackageKit is required to schedule an offline update."));
        return;
    }

    m_running = true;
    Q_EMIT runningChanged();
    if (!m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        Q_EMIT errorMessageChanged();
    }
    setStatus(tr("Scheduling updates for the next reboot…"));
    startProcess(pkcon, {QStringLiteral("offline-trigger")}, Operation::SchedulePackageKit);
}

void Updates::cancel() {
    if (!m_running) {
        return;
    }
    m_process.kill();
    m_operation = Operation::None;
    finishOperation(false, tr("Operation canceled."));
}

void Updates::startProcess(const QString &program, const QStringList &arguments, Operation operation) {
    m_operation = operation;
    m_lastProcessOutput.clear();
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.start();
}

void Updates::processFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    readProcessOutput();
    const Operation operation = m_operation;
    m_operation = Operation::None;
    const bool success = exitStatus == QProcess::NormalExit
        && (exitCode == 0
            || (exitCode == 5 && m_operation != Operation::SchedulePackageKit));

    if (!success) {
        const QString backend = m_operation == Operation::RefreshPackageKit ? tr("PackageKit")
                                                                             : tr("Flatpak");
        const bool isNoUpdateMessage = m_lastProcessOutput.contains(QLatin1String("no updates"), Qt::CaseInsensitive)
            || m_lastProcessOutput.contains(QLatin1String("nothing to update"), Qt::CaseInsensitive);
        const QString detail = m_lastProcessOutput.isEmpty() || isNoUpdateMessage ? QString() : tr(" %1").arg(m_lastProcessOutput);
        finishOperation(false, tr("%1 update operation failed (exit code %2).%3").arg(backend).arg(exitCode).arg(detail));
        return;
    }

    if (!m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        Q_EMIT errorMessageChanged();
    }

    switch (operation) {
    case Operation::RefreshPackageKit: {
        const QString flatpak = executablePath(QStringLiteral("flatpak"));
        if (!flatpak.isEmpty()) {
            startProcess(flatpak,
                         {QStringLiteral("remote-ls"), QStringLiteral("--updates"),
                          QStringLiteral("--columns=application,name,version,branch")},
                         Operation::RefreshFlatpak);
        } else {
            finishOperation(true, tr("Update check complete."));
        }
        break;
    }
    case Operation::RefreshFlatpak:
        finishOperation(true, tr("%n update(s) available.", nullptr, updateCount()));
        break;
    case Operation::ApplyNowPackageKit: {
        const QString flatpak = executablePath(QStringLiteral("flatpak"));
        if (!flatpak.isEmpty()) {
            startProcess(flatpak, {QStringLiteral("update"), QStringLiteral("--noninteractive")},
                         Operation::ApplyNowFlatpak);
        } else {
            finishOperation(true, tr("Updates installed."));
        }
        break;
    }
    case Operation::ApplyNowFlatpak:
        finishOperation(true, tr("Updates installed."));
        break;
    case Operation::SchedulePackageKit:
        if (const QString flatpak = executablePath(QStringLiteral("flatpak")); !flatpak.isEmpty()) {
            startProcess(flatpak, {QStringLiteral("update"), QStringLiteral("--noninteractive")}, Operation::ApplyNowFlatpak);
        } else {
            finishOperation(true, tr("RPM updates scheduled for the next reboot."));
        }
        break;
    case Operation::None:
        break;
    }
}

void Updates::processError(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        m_operation = Operation::None;
        finishOperation(false, tr("Could not start the Fedora update tool."));
    }
}

void Updates::readProcessOutput() {
    const QByteArray output = m_process.readAllStandardOutput() + m_process.readAllStandardError();
    const QString text = QString::fromLocal8Bit(output).trimmed();
    if (text.isEmpty()) {
        return;
    }
    qWarning().noquote() << "kcm-updates:" << text;
    m_lastProcessOutput = text.split(QLatin1Char('\n')).constLast().trimmed();

    switch (m_operation) {
    case Operation::RefreshPackageKit:
        parsePackageKit(text);
        break;
    case Operation::RefreshFlatpak:
        parseFlatpak(text);
        break;
    default:
        setStatus(text.split(QLatin1Char('\n')).constLast().trimmed());
        break;
    }
}

void Updates::parsePackageKit(const QString &output) {
    QString technical = m_technicalInformation;
    for (const QString &line: lines(output)) {
        if (line.startsWith(QLatin1String("Package"), Qt::CaseInsensitive)) {
            continue;
        }

        const QStringList fields = line.split(QLatin1Char(';'));
        if (fields.size() < 4) {
            continue;
        }

        // PackageKit package IDs are name;version;architecture;repository.
        const QString &name = fields.at(0);
        const QString &newVersion = fields.at(1);
        const QString &architecture = fields.at(2);
        const QString &repository = fields.at(3);
        const QString oldVersion = commandOutput(executablePath(QStringLiteral("rpm")),
                                                 {QStringLiteral("-q"), QStringLiteral("--qf"), QStringLiteral("%{EVR}"), name});
        m_updates.addUpdate(
            {name, tr("Fedora (%1)").arg(repository), oldVersion.isEmpty() ? tr("installed") : oldVersion, newVersion,
             QStringLiteral("package-x-generic")});
        technical += tr("Fedora: %1 → %2 (%3, %4)\n").arg(name, oldVersion.isEmpty() ? tr("installed") : oldVersion, newVersion,
                                                            architecture);
    }
    setTechnicalInformation(technical);
}

void Updates::parseFlatpak(const QString &output) {
    QString technical = m_technicalInformation;
    for (const QString &line: lines(output)) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 3 || line.startsWith(QLatin1String("Application"), Qt::CaseInsensitive)) {
            continue;
        }
        const QString &appId = fields.at(0);
        const QString name = fields.at(1).isEmpty() ? appId : fields.at(1);
        const QString &version = fields.at(2);
        const QString oldVersion = commandOutput(executablePath(QStringLiteral("flatpak")),
                                                 {QStringLiteral("info"), QStringLiteral("--show-version"), appId});
        m_updates.addUpdate(
            {name, tr("Flatpak"), oldVersion.isEmpty() ? tr("installed") : oldVersion, version,
             QStringLiteral("application-x-executable")});
        technical += tr("Flatpak: %1 → %2 (%3)\n").arg(name, oldVersion.isEmpty() ? tr("installed") : oldVersion, version);
    }
    setTechnicalInformation(technical);
}

void Updates::finishOperation(bool success, const QString &message) {
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
