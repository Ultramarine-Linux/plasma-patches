// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <KLocalizedString>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <ranges>
#include <utility>

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

    // how often the output of a running change is read
    constexpr int OutputPollMs = 200;

    /**
     * starts children without stdin and, where Qt can, in a new session without a controlling
     * terminal. um picks sudo when it has a terminal, which would prompt where nobody sees it
     * if System Settings was started from one. SIGPIPE is ignored too, which QProcess would
     * otherwise reset to the default (exit) in the child.
     */
    void detachFromTerminal(QProcess *process) {
        process->setStandardInputFile(QProcess::nullDevice());
        QProcess::UnixProcessFlags flags = QProcess::UnixProcessFlag::IgnoreSigPipe;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        flags |= QProcess::UnixProcessFlag::CreateNewSession;
#endif
        process->setUnixProcessParameters(flags);
    }

    /// returns the last line of @p output that um logged an error on, without the timestamp
    QString lastUmError(const QString &output) {
        for (const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
             const QString &line: lines | std::views::reverse) {
            if (const QString stripped = stripGoLogTimestamp(line); stripped != line) {
                return stripped.trimmed();
            }
        }
        return {};
    }
} // namespace

int tweaksOutputVersion(const QString &output) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return 0;
    }
    // um builds from before the version field was added print the same format as version 1
    return document.object().value(QStringLiteral("version")).toInt(1);
}

bool parseTweaksOutput(const QString &output, QList<TweakItem> *tweaks) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    // a newer format may change what the fields mean, so don't guess
    if (tweaksOutputVersion(output) != SupportedTweaksOutputVersion) {
        return false;
    }
    const QJsonValue list = document.object().value(QStringLiteral("tweaks"));
    if (!list.isArray()) {
        return false;
    }

    QList<TweakItem> result;
    for (const QJsonArray array = list.toArray(); const auto &value: array) {
        const QJsonObject object = value.toObject();
        TweakItem item;
        item.id = object.value(QStringLiteral("id")).toString();
        if (item.id.isEmpty()) {
            continue;
        }
        item.description = object.value(QStringLiteral("description")).toString();
        if (item.description.isEmpty()) {
            item.description = item.id;
        }
        item.type = object.value(QStringLiteral("type")).toString();
        item.stability = object.value(QStringLiteral("stability")).toString();
        item.warning = object.value(QStringLiteral("warning")).toString();
        item.stateError = object.value(QStringLiteral("state_error")).toString();
        // um reports enabled as false when it couldn't tell, which isn't the same as off
        item.enabled = item.stateError.isEmpty() && object.value(QStringLiteral("enabled")).toBool();
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
        // the exit code of a crashed process means nothing
        return script ? i18n("The maintenance task stopped unexpectedly. See the output for details.")
                      : i18n("The tweak could not be changed because um stopped unexpectedly. See the output for "
                             "details.");
    }
    if (UmCli::isAuthenticationFailure(exitCode)) {
        // um maps a playbook's or script's own 126 and 127 to 1, so these are always pkexec's
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
        return !m_busyId.isEmpty() && id == m_busyId;
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
    // tweaks are applied immediately through um, not through the Apply button
    setButtons(NoAdditionalButton);

    detachFromTerminal(&m_listProcess);
    detachFromTerminal(m_changeProcess);

    connect(&m_listProcess, &QProcess::finished, this, &Tweaks::listFinished);
    connect(&m_listProcess, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setLoading(false);
            m_reloadPending = false;
            setListError(i18n("Could not start um."));
            setListState(m_loaded, true);
        }
    });

    // stderr goes to the same file as stdout
    m_changeProcess->setProcessChannelMode(QProcess::MergedChannels);
    m_outputTimer.setInterval(OutputPollMs);
    connect(&m_outputTimer, &QTimer::timeout, this, &Tweaks::readChangeOutput);
    connect(m_changeProcess, &QProcess::started, this, [this] {
        // um has opened it by now, and the file goes away with the last handle
        QFile::remove(m_outputFile.fileName());
    });
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
    // ~QProcess kills a running list and waits for it, which emits finished(). As a member it's
    // destroyed after the members declared below it, so listFinished() would run on a
    // half-destroyed module. Listing is read-only, so stop it here instead.
    m_listProcess.disconnect(this);
    if (m_listProcess.state() != QProcess::NotRunning) {
        m_listProcess.kill();
        m_listProcess.waitForFinished();
    }

    if (m_changeProcess->state() == QProcess::NotRunning) {
        return;
    }
    // ~QProcess would kill um and wait up to 30 seconds for it, freezing System Settings. Once
    // um runs as root through pkexec it can't be killed from here, and stopping a playbook
    // halfway could leave the system half changed. So let it finish by itself. Its output
    // goes to an unlinked file rather than a pipe, so it can keep writing after this handle
    // is closed, and even after System Settings quits.
    closeChangeOutput();
    m_changeProcess->disconnect(this);
    m_changeProcess->setParent(nullptr);
    connect(m_changeProcess, &QProcess::finished, m_changeProcess, &QObject::deleteLater);
}

void Tweaks::load() {
    KQuickConfigModule::load();
    // the list is read again whenever the page is shown, since tweaks can change from a terminal
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
        // the list being loaded may predate a change, so load it again once it's done
        m_reloadPending = true;
        return;
    }
    const QString um = UmCli::executable();
    if (um.isEmpty()) {
        setListError(i18n("um is not installed."));
        setListState(m_loaded, true);
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
            startChange(id, {QStringLiteral("enable")}, i18n("Enabling “%1”…", tweak->description));
        } else {
            startChange(id, {QStringLiteral("disable")}, i18n("Disabling “%1”…", tweak->description));
        }
    } else if (tweak->type == QLatin1String("oneshot") && enabled) {
        startChange(id, {QStringLiteral("enable")}, i18n("Applying “%1”…", tweak->description));
    }
}

void Tweaks::runTweak(const QString &id) {
    const TweakItem *tweak = m_tweaks.find(id);
    if (!tweak || tweak->type != QLatin1String("script")) {
        return;
    }
    startChange(id, {QStringLiteral("run")}, i18n("Running “%1”…", tweak->description));
}

void Tweaks::startChange(const QString &id, const QStringList &arguments, const QString &progressMessage) {
    if (m_running) {
        return;
    }
    const QString um = UmCli::executable();
    if (um.isEmpty()) {
        setChangeError(i18n("um is not installed."));
        return;
    }

    // a fresh file per run, only readable by the user. NewOnly refuses anything already at the path.
    closeChangeOutput();
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    m_outputFile.setFileName(QDir(runtimeDir)
                                 .filePath(QStringLiteral("kcm_ultramarine_tweaks-%1.log")
                                               .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))));
    if (!m_outputFile.open(QIODevice::ReadWrite | QIODevice::NewOnly | QIODevice::Unbuffered,
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        setChangeError(i18n("Could not create a file for um's output: %1", m_outputFile.errorString()));
        return;
    }
    m_changeProcess->setStandardOutputFile(m_outputFile.fileName());

    setChangeError({});
    m_output.clear();
    Q_EMIT outputChanged();
    m_changeDecoder.resetState();
    m_partialLine.clear();
    m_runningScript = arguments.contains(QStringLiteral("run"));
    m_tweaks.setBusyId(id);
    setRunning(true);
    setStatus(progressMessage);

    // --yes skips um's terminal prompt. The KCM shows its own confirmation first.
    const QStringList umArguments =
        QStringList{QStringLiteral("tweaks")} + arguments + QStringList{QStringLiteral("--yes"), id};
    // um escalates through pkexec itself when it isn't run from a terminal. There is no
    // Cancel, since once the tweak runs as root, signals from this unprivileged process fail.
    m_changeProcess->start(um, umArguments);
    m_outputTimer.start();
}

void Tweaks::listFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    setLoading(false);
    const QString stdOut = QString::fromUtf8(m_listProcess.readAllStandardOutput());
    const QString stdErr = QString::fromUtf8(m_listProcess.readAllStandardError()).trimmed();
    if (std::exchange(m_reloadPending, false)) {
        // this list is already out of date
        refresh();
        return;
    }

    QList<TweakItem> items;
    if (exitStatus != QProcess::NormalExit || exitCode != 0 || !parseTweaksOutput(stdOut, &items)) {
        // older um releases have no --json flag, so point that out rather than showing a parse error
        if (stdErr.contains(QLatin1String("flag provided but not defined"))) {
            setListError(i18n("This version of um is too old to manage tweaks from System Settings."));
        } else if (exitCode == 0 && tweaksOutputVersion(stdOut) > SupportedTweaksOutputVersion) {
            setListError(i18n("This version of um is too new for this version of System Settings. Update "
                              "System Settings to manage tweaks."));
        } else {
            // um's errors go through Go's log package, which puts a timestamp in front
            QStringList errorLines;
            for (const QStringList lines = stdErr.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                 const QString &line: lines) {
                errorLines.append(stripGoLogTimestamp(line));
            }
            const QString error = errorLines.join(QLatin1Char('\n'));
            setListError(error.isEmpty() ? i18n("Could not load the list of tweaks.")
                                         : i18n("Could not load the list of tweaks: %1", error));
        }
        setListState(m_loaded, true);
        return;
    }
    m_tweaks.setTweaks(std::move(items));
    // a change's error stays, since loading the list again doesn't fix it
    setListError({});
    setListState(true, false);
}

void Tweaks::changeFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    readChangeOutput();
    closeChangeOutput();
    m_tweaks.setBusyId({});
    setRunning(false);
    setStatus({});

    m_partialLine.clear();
    if (const QString message = changeErrorMessage(m_runningScript, exitCode, exitStatus, m_output);
        !message.isEmpty()) {
        setChangeError(message);
    }

    // always reload: um records state only after a playbook succeeds
    refresh();
}

void Tweaks::readChangeOutput() {
    // stateful, so a UTF-8 sequence split across reads isn't mangled
    if (!m_outputFile.isOpen()) {
        return;
    }
    const QString text = m_changeDecoder.decode(m_outputFile.readAll());
    if (text.isEmpty()) {
        return;
    }
    m_output += text;
    Q_EMIT outputChanged();

    // only complete lines are shown, so the status doesn't flicker through partial ones
    m_partialLine += text;
    const qsizetype end = m_partialLine.lastIndexOf(QLatin1Char('\n'));
    if (end < 0) {
        return;
    }
    const QString complete = m_partialLine.left(end);
    m_partialLine.remove(0, end + 1);
    updateStatus(complete);
}

void Tweaks::closeChangeOutput() {
    m_outputTimer.stop();
    if (m_outputFile.isOpen()) {
        // normally unlinked already, unless um never started
        QFile::remove(m_outputFile.fileName());
        m_outputFile.close();
    }
}

void Tweaks::updateStatus(const QString &lines) {
    // the status shows the last meaningful line
    for (const QStringList split = lines.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
         const QString &rawLine: split | std::views::reverse) {
        // Ansible pads headers like "TASK [name] ****" with asterisks, so drop the padding
        QString line = rawLine.trimmed();
        while (line.endsWith(QLatin1Char('*'))) {
            line.chop(1);
        }
        line = line.trimmed();
        if (!line.isEmpty()) {
            if (m_running) {
                setStatus(line);
            }
            break;
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

void Tweaks::setListError(const QString &message) {
    m_listError = message;
    updateErrorMessage();
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

void Tweaks::setListState(const bool loaded, const bool failed) {
    if (m_loaded == loaded && m_listFailed == failed) {
        return;
    }
    m_loaded = loaded;
    m_listFailed = failed;
    Q_EMIT listStateChanged();
}
