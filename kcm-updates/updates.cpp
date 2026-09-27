// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
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
    for (const QString &entry: entries) {
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

    const QString um = executablePath(QStringLiteral("um"));
    if (um.isEmpty()) {
        setStatus(tr("um is not installed."));
        return;
    }

    m_running = true;
    Q_EMIT runningChanged();
    startProcess(um, {QStringLiteral("upgrade"), QStringLiteral("list"), QStringLiteral("--json")},
                 Operation::RefreshUm);
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

    const QString um = executablePath(QStringLiteral("um"));
    if (um.isEmpty()) {
        finishOperation(false, tr("um is not installed."));
        return;
    }
    startProcess(um, {QStringLiteral("upgrade"), QStringLiteral("apply"), QStringLiteral("--yes")}, Operation::ApplyUm);
}

void Updates::applyOfflineUpdates() {
    if (m_running || !m_offlineUpdatesEnabled) {
        return;
    }

    setStatus(tr("Offline updates are not supported by um yet."));
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
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;

    if (!success) {
        const QString backend = tr("um");
        const bool isNoUpdateMessage = m_lastProcessOutput.contains(QLatin1String("no updates"), Qt::CaseInsensitive) ||
            m_lastProcessOutput.contains(QLatin1String("nothing to update"), Qt::CaseInsensitive);
        const QString detail =
            m_lastProcessOutput.isEmpty() || isNoUpdateMessage ? QString() : tr(" %1").arg(m_lastProcessOutput);
        finishOperation(false,
                        tr("%1 update operation failed (exit code %2).%3").arg(backend).arg(exitCode).arg(detail));
        return;
    }

    if (!m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        Q_EMIT errorMessageChanged();
    }

    switch (operation) {
    case Operation::RefreshUm:
        m_updates.clear();
        parseUm(m_lastProcessOutput);
        finishOperation(true, tr("%n update(s) available.", nullptr, updateCount()));
        break;
    case Operation::ApplyUm:
        finishOperation(true, tr("Updates installed."));
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
    m_lastProcessOutput += text;

    switch (m_operation) {
    case Operation::RefreshUm:
        // Parse the complete JSON document after the process exits.
        break;
    default:
        setStatus(text.split(QLatin1Char('\n')).constLast().trimmed());
        break;
    }
}

void Updates::parseUm(const QString &output) {
    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8());
    if (!document.isObject()) {
        return;
    }
    QString technical;
    const QJsonArray updates = document.object().value(QStringLiteral("updates")).toArray();
    for (const QJsonValue &value: updates) {
        const QJsonObject item = value.toObject();
        const QString name = item.value(QStringLiteral("name")).toString();
        const QString source = item.value(QStringLiteral("source")).toString();
        const QString oldVersion = item.value(QStringLiteral("old_version")).toString();
        const QString newVersion = item.value(QStringLiteral("new_version")).toString();
        const QString icon = item.value(QStringLiteral("icon")).toString(QStringLiteral("package-x-generic"));
        m_updates.addUpdate({name, source, oldVersion, newVersion, icon});
        technical += tr("%1: %2 → %3 (%4)\n").arg(name, oldVersion, newVersion, source);
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
