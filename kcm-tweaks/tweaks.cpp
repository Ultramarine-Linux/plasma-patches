// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <KLocalizedString>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <optional>
#include <ranges>
#include <utility>

#include <signal.h>

#include "detachedprocess.h"
#include "tweaks.h"
#include "umcli.h"

K_PLUGIN_CLASS_WITH_JSON(Tweaks, "kcm_ultramarine_tweaks.json")

#include "tweaks.moc"

namespace {
    int typeOrder(const QString &type) {
        if (type == QLatin1String("toggle")) {
            return 0;
        }
        if (type == QLatin1String("oneshot")) {
            return 1;
        }
        if (type == QLatin1String("script")) {
            return 2;
        }
        return 3;
    }

    constexpr std::chrono::milliseconds ChangeOutputPollInterval{200};
    constexpr int ImplicitTweaksOutputVersion = 1;

    std::optional<QJsonObject> parseJsonObject(const QString &output) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            return std::nullopt;
        }
        return document.object();
    }

    int outputVersion(const QJsonObject &output) {
        const QJsonValue version = output.value(QStringLiteral("version"));
        if (version.isUndefined()) {
            return ImplicitTweaksOutputVersion;
        }
        const double number = version.toDouble(-1);
        if (!version.isDouble() || number < 1 || number > INT_MAX || std::floor(number) != number) {
            return UnrecognizedTweaksOutputVersion;
        }
        return static_cast<int>(number);
    }

    QString lastUmError(const QString &output) {
        for (const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
             const QString &line: lines | std::views::reverse) {
            if (const QString stripped = stripGoLogTimestamp(line); stripped != line) {
                return stripped.trimmed();
            }
        }
        return {};
    }

    QString stripGoLogTimestamps(const QString &lines) {
        QStringList stripped;
        for (const QStringList split = lines.split(QLatin1Char('\n'), Qt::SkipEmptyParts); const QString &line: split) {
            stripped.append(stripGoLogTimestamp(line));
        }
        return stripped.join(QLatin1Char('\n'));
    }

    QString withoutAnsibleHeaderPadding(const QString &line) {
        QString trimmed = line.trimmed();
        while (trimmed.endsWith(QLatin1Char('*'))) {
            trimmed.chop(1);
        }
        return trimmed.trimmed();
    }

    QString listErrorMessage(const int exitCode, const QProcess::ExitStatus exitStatus, const QString &stdOut,
                             const QString &stdErr) {
        if (stdErr.contains(QLatin1String("flag provided but not defined"))) {
            return i18n("This version of um is too old to manage tweaks from System Settings.");
        }
        const bool exitedCleanly = exitStatus == QProcess::NormalExit && exitCode == 0;
        const int version = exitedCleanly ? tweaksOutputVersion(stdOut) : 0;
        if (version > SupportedTweaksOutputVersion) {
            return i18n("This version of um is too new for this version of System Settings. Update "
                        "System Settings to manage tweaks.");
        }
        if (version == UnrecognizedTweaksOutputVersion) {
            return i18n("um listed the tweaks with a format version System Settings doesn't recognize. "
                        "Check that um and System Settings are up to date.");
        }
        const QString error = stripGoLogTimestamps(stdErr);
        return error.isEmpty() ? i18n("Could not load the list of tweaks.")
                               : i18n("Could not load the list of tweaks: %1", error);
    }
} // namespace

int tweaksOutputVersion(const QString &output) {
    const std::optional<QJsonObject> object = parseJsonObject(output);
    return object ? outputVersion(*object) : 0;
}

bool parseTweaksOutput(const QString &output, QList<TweakItem> *tweaks) {
    const std::optional<QJsonObject> object = parseJsonObject(output);
    if (!object || outputVersion(*object) != SupportedTweaksOutputVersion) {
        return false;
    }
    const QJsonValue list = object->value(QStringLiteral("tweaks"));
    if (!list.isArray()) {
        return false;
    }

    QList<TweakItem> result;
    for (const QJsonArray array = list.toArray(); const auto &value: array) {
        const QJsonObject entry = value.toObject();
        TweakItem item;
        item.id = entry.value(QStringLiteral("id")).toString();
        if (item.id.isEmpty()) {
            continue;
        }
        item.description = entry.value(QStringLiteral("description")).toString();
        if (item.description.isEmpty()) {
            item.description = item.id;
        }
        item.type = entry.value(QStringLiteral("type")).toString();
        item.stability = entry.value(QStringLiteral("stability")).toString();
        item.warning = entry.value(QStringLiteral("warning")).toString();
        item.stateError = entry.value(QStringLiteral("state_error")).toString();
        item.enabled = item.stateError.isEmpty() && entry.value(QStringLiteral("enabled")).toBool();
        result.append(item);
    }

    std::ranges::stable_sort(result, [](const TweakItem &a, const TweakItem &b) {
        const int aOrder = typeOrder(a.type);
        if (const int bOrder = typeOrder(b.type); aOrder != bOrder) {
            return aOrder < bOrder;
        }
        return a.description.localeAwareCompare(b.description) < 0;
    });

    if (tweaks) {
        *tweaks = std::move(result);
    }
    return true;
}

QString stripGoLogTimestamp(const QString &line) {
    static const QRegularExpression timestamp(QStringLiteral(R"(^\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} )"));
    if (const QRegularExpressionMatch match = timestamp.match(line); match.hasMatch()) {
        return line.mid(match.capturedLength());
    }
    return line;
}

QString changeErrorMessage(const bool script, const int exitCode, const QProcess::ExitStatus exitStatus,
                           const QString &output) {
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        return {};
    }
    if (exitStatus != QProcess::NormalExit) {
        return script ? i18n("The maintenance task stopped unexpectedly. See the output for details.")
                      : i18n("The tweak could not be changed because um stopped unexpectedly. See the output for "
                             "details.");
    }
    if (UmCli::isAuthenticationFailure(exitCode)) {
        return i18n("Authentication was canceled or failed. No changes were made.");
    }
    if (const QString error = lastUmError(output); !error.isEmpty()) {
        return script ? i18n("The maintenance task failed: %1", error)
                      : i18n("The tweak could not be changed: %1", error);
    }
    return script ? i18n("The maintenance task failed (exit code %1). See the output for details.", exitCode)
                  : i18n("The tweak could not be changed (exit code %1). See the output for details.", exitCode);
}

QString composeTweaksError(const QString &changeError, const QString &listError) {
    if (changeError.isEmpty() || listError.isEmpty()) {
        return changeError.isEmpty() ? listError : changeError;
    }
    return changeError + QLatin1Char('\n') + listError;
}

TweaksModel::TweaksModel(QObject *parent) : QAbstractListModel(parent) {}

int TweaksModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_tweaks.size());
}

QVariant TweaksModel::data(const QModelIndex &index, const int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }

    const auto &[id, description, type, stability, warning, stateError, enabled] = m_tweaks.at(index.row());
    switch (role) {
    case IdRole:
        return id;
    case DescriptionRole:
    case Qt::DisplayRole:
        return description;
    case TypeRole:
        return type;
    case StabilityRole:
        return stability;
    case WarningRole:
        return warning;
    case StateErrorRole:
        return stateError;
    case EnabledRole:
        return enabled;
    case BusyRole:
        return id == m_busyId;
    default:
        return {};
    }
}

QHash<int, QByteArray> TweaksModel::roleNames() const {
    return {
        {IdRole, "tweakId"},
        {DescriptionRole, "description"},
        {TypeRole, "type"},
        {StabilityRole, "stability"},
        {WarningRole, "warning"},
        {StateErrorRole, "stateError"},
        {EnabledRole, "tweakEnabled"},
        {BusyRole, "busy"},
    };
}

int TweaksModel::count() const { return static_cast<int>(m_tweaks.size()); }

void TweaksModel::setTweaks(QList<TweakItem> tweaks) {
    const qsizetype oldCount = m_tweaks.size();
    beginResetModel();
    m_tweaks = std::move(tweaks);
    endResetModel();
    if (oldCount != m_tweaks.size()) {
        Q_EMIT countChanged();
    }
}

const TweakItem *TweaksModel::find(const QString &id) const {
    const auto it = std::ranges::find_if(m_tweaks, [&id](const TweakItem &item) { return item.id == id; });
    return it == m_tweaks.cend() ? nullptr : &*it;
}

void TweaksModel::setBusyId(const QString &id) {
    if (m_busyId == id) {
        return;
    }
    m_busyId = id;
    if (!m_tweaks.isEmpty()) {
        Q_EMIT dataChanged(index(0, 0), index(static_cast<int>(m_tweaks.size()) - 1, 0), {BusyRole});
    }
}

Tweaks::Tweaks(QObject *parent, const KPluginMetaData &metaData) :
    KQuickConfigModule(parent, metaData), m_tweaks(this), m_changeProcess(new QProcess(this)) {
    setButtons(NoAdditionalButton);

    DetachedProcess::detachFromTerminal(&m_listProcess);
    DetachedProcess::detachFromTerminal(m_changeProcess);

    connect(&m_listProcess, &QProcess::finished, this, &Tweaks::listFinished);
    connect(&m_listProcess, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setLoading(false);
            m_reloadAfterCurrentLoad = false;
            setListResult(i18n("Could not start um."));
        }
    });

    m_changeProcess->setProcessChannelMode(QProcess::MergedChannels);
    m_changeOutputPollTimer.setInterval(ChangeOutputPollInterval);
    connect(&m_changeOutputPollTimer, &QTimer::timeout, this, &Tweaks::readChangeOutput);
    connect(m_changeProcess, &QProcess::started, this, [this] { QFile::remove(m_changeOutputFile.fileName()); });
    connect(m_changeProcess, &QProcess::finished, this, &Tweaks::changeFinished);
    connect(m_changeProcess, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            closeChangeOutput();
            m_tweaks.setBusyId({});
            setRunning(false);
            setStatus({});
            setChangeError(i18n("Could not start um."));
        }
    });
}

Tweaks::~Tweaks() {
    m_listProcess.disconnect(this);
    killListProcessGroup();
    if (m_changeProcess->state() != QProcess::NotRunning) {
        closeChangeOutput();
        DetachedProcess::letFinishAfterModuleCloses(m_changeProcess, this);
    }
}

void Tweaks::killListProcessGroup() {
    if (m_listProcess.state() == QProcess::NotRunning) {
        return;
    }
    if (const qint64 sessionLeaderPid = m_listProcess.processId(); sessionLeaderPid > 0) {
        ::kill(static_cast<pid_t>(-sessionLeaderPid), SIGKILL);
    }
    m_listProcess.kill();
    m_listProcess.waitForFinished();
}

void Tweaks::load() {
    KQuickConfigModule::load();
    refresh();
}

TweaksModel *Tweaks::tweaks() { return &m_tweaks; }

bool Tweaks::loading() const { return m_loading; }

bool Tweaks::loaded() const { return m_loaded; }

bool Tweaks::listFailed() const { return m_listFailed; }

bool Tweaks::running() const { return m_running; }

const QString &Tweaks::status() const { return m_status; }

const QString &Tweaks::errorMessage() const { return m_errorMessage; }

const QString &Tweaks::output() const { return m_output; }

void Tweaks::refresh() {
    if (m_loading) {
        m_reloadAfterCurrentLoad = true;
        return;
    }
    const QString um = UmCli::executable();
    if (um.isEmpty()) {
        setListResult(i18n("um is not installed."));
        return;
    }
    setLoading(true);
    m_listProcess.start(um, {QStringLiteral("tweaks"), QStringLiteral("list"), QStringLiteral("--json")});
}

void Tweaks::setTweakEnabled(const QString &id, const bool enabled) {
    const TweakItem *tweak = m_tweaks.find(id);
    if (!tweak) {
        return;
    }
    if (tweak->type == QLatin1String("toggle")) {
        if (enabled) {
            startChange(id, QStringLiteral("enable"), i18n("Enabling “%1”…", tweak->description));
        } else {
            startChange(id, QStringLiteral("disable"), i18n("Disabling “%1”…", tweak->description));
        }
    } else if (tweak->type == QLatin1String("oneshot") && enabled) {
        startChange(id, QStringLiteral("enable"), i18n("Applying “%1”…", tweak->description));
    }
}

void Tweaks::runTweak(const QString &id) {
    const TweakItem *tweak = m_tweaks.find(id);
    if (!tweak || tweak->type != QLatin1String("script")) {
        return;
    }
    startChange(id, QStringLiteral("run"), i18n("Running “%1”…", tweak->description));
}

void Tweaks::startChange(const QString &id, const QString &verb, const QString &progressMessage) {
    if (m_running) {
        return;
    }
    const QString um = UmCli::executable();
    if (um.isEmpty()) {
        setChangeError(i18n("um is not installed."));
        return;
    }

    closeChangeOutput();
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    m_changeOutputFile.setFileName(QDir(runtimeDir)
                                       .filePath(QStringLiteral("kcm_ultramarine_tweaks-%1.log")
                                                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))));
    if (!m_changeOutputFile.open(QIODevice::ReadWrite | QIODevice::NewOnly | QIODevice::Unbuffered,
                                 QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        setChangeError(i18n("Could not create a file for um's output: %1", m_changeOutputFile.errorString()));
        return;
    }
    m_changeProcess->setStandardOutputFile(m_changeOutputFile.fileName());

    setChangeError({});
    m_output.clear();
    Q_EMIT outputChanged();
    m_changeDecoder.resetState();
    m_unfinishedChangeLine.clear();
    m_changeIsScript = verb == QLatin1String("run");
    m_tweaks.setBusyId(id);
    setRunning(true);
    setStatus(progressMessage);

    m_changeProcess->start(um, {QStringLiteral("tweaks"), verb, QStringLiteral("--yes"), id});
    m_changeOutputPollTimer.start();
}

void Tweaks::listFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    setLoading(false);
    const QString stdOut = QString::fromUtf8(m_listProcess.readAllStandardOutput());
    const QString stdErr = QString::fromUtf8(m_listProcess.readAllStandardError()).trimmed();
    if (std::exchange(m_reloadAfterCurrentLoad, false)) {
        refresh();
        return;
    }

    QList<TweakItem> items;
    if (exitStatus != QProcess::NormalExit || exitCode != 0 || !parseTweaksOutput(stdOut, &items)) {
        setListResult(listErrorMessage(exitCode, exitStatus, stdOut, stdErr));
        return;
    }
    m_tweaks.setTweaks(std::move(items));
    setListResult({});
}

void Tweaks::changeFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    readChangeOutput();
    closeChangeOutput();
    m_tweaks.setBusyId({});
    setRunning(false);
    setStatus({});

    if (const QString message = changeErrorMessage(m_changeIsScript, exitCode, exitStatus, m_output);
        !message.isEmpty()) {
        setChangeError(message);
    }

    refresh();
}

void Tweaks::readChangeOutput() {
    if (!m_changeOutputFile.isOpen()) {
        return;
    }
    const QString text = m_changeDecoder.decode(m_changeOutputFile.readAll());
    if (text.isEmpty()) {
        return;
    }
    m_output += text;
    Q_EMIT outputChanged();

    m_unfinishedChangeLine += text;
    const qsizetype end = m_unfinishedChangeLine.lastIndexOf(QLatin1Char('\n'));
    if (end < 0) {
        return;
    }
    const QString complete = m_unfinishedChangeLine.left(end);
    m_unfinishedChangeLine.remove(0, end + 1);
    showLastLineAsStatus(complete);
}

void Tweaks::closeChangeOutput() {
    m_changeOutputPollTimer.stop();
    if (m_changeOutputFile.isOpen()) {
        QFile::remove(m_changeOutputFile.fileName());
        m_changeOutputFile.close();
    }
}

void Tweaks::showLastLineAsStatus(const QString &completeLines) {
    for (const QStringList lines = completeLines.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
         const QString &rawLine: lines | std::views::reverse) {
        if (const QString line = withoutAnsibleHeaderPadding(rawLine); !line.isEmpty()) {
            setStatus(line);
            return;
        }
    }
}

void Tweaks::setLoading(const bool value) {
    if (m_loading == value) {
        return;
    }
    m_loading = value;
    Q_EMIT loadingChanged();
}

void Tweaks::setRunning(const bool value) {
    if (m_running == value) {
        return;
    }
    m_running = value;
    Q_EMIT runningChanged();
}

void Tweaks::setStatus(const QString &value) {
    if (m_status == value) {
        return;
    }
    m_status = value;
    Q_EMIT statusChanged();
}

void Tweaks::clearError() {
    m_listError.clear();
    m_changeError.clear();
    updateErrorMessage();
}

void Tweaks::setListResult(const QString &error) {
    m_listError = error;
    updateErrorMessage();
    const bool failed = !error.isEmpty();
    const bool nowLoaded = m_loaded || !failed;
    if (m_loaded == nowLoaded && m_listFailed == failed) {
        return;
    }
    m_loaded = nowLoaded;
    m_listFailed = failed;
    Q_EMIT listStateChanged();
}

void Tweaks::setChangeError(const QString &message) {
    m_changeError = message;
    updateErrorMessage();
}

void Tweaks::updateErrorMessage() {
    const QString message = composeTweaksError(m_changeError, m_listError);
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    Q_EMIT errorMessageChanged();
}
