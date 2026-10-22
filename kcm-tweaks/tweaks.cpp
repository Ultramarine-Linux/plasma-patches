// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <KLocalizedString>
#include <KPluginFactory>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

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
} // namespace

bool parseTweaksOutput(const QString &output, QList<TweakItem> *tweaks) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QJsonValue list = document.object().value(QStringLiteral("tweaks"));
    if (!list.isArray()) {
        return false;
    }

    QList<TweakItem> result;
    for (const QJsonValue &value: list.toArray()) {
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
        item.enabled = object.value(QStringLiteral("enabled")).toBool();
        result.append(item);
    }

    std::stable_sort(result.begin(), result.end(), [](const TweakItem &a, const TweakItem &b) {
        const int aOrder = typeOrder(a.type);
        const int bOrder = typeOrder(b.type);
        if (aOrder != bOrder) {
            return aOrder < bOrder;
        }
        return a.description.localeAwareCompare(b.description) < 0;
    });

    if (tweaks) {
        *tweaks = std::move(result);
    }
    return true;
}

TweaksModel::TweaksModel(QObject *parent) : QAbstractListModel(parent) {}

int TweaksModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_tweaks.size());
}

QVariant TweaksModel::data(const QModelIndex &index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }

    const TweakItem &item = m_tweaks.at(index.row());
    switch (role) {
    case IdRole:
        return item.id;
    case DescriptionRole:
    case Qt::DisplayRole:
        return item.description;
    case TypeRole:
        return item.type;
    case StabilityRole:
        return item.stability;
    case WarningRole:
        return item.warning;
    case EnabledRole:
        return item.enabled;
    case BusyRole:
        return !m_busyId.isEmpty() && item.id == m_busyId;
    default:
        return {};
    }
}

QHash<int, QByteArray> TweaksModel::roleNames() const {
    return {
        {IdRole, "tweakId"},      {DescriptionRole, "description"}, {TypeRole, "type"}, {StabilityRole, "stability"},
        {WarningRole, "warning"}, {EnabledRole, "tweakEnabled"},    {BusyRole, "busy"},
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
    const auto it =
        std::find_if(m_tweaks.cbegin(), m_tweaks.cend(), [&id](const TweakItem &item) { return item.id == id; });
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
    KQuickManagedConfigModule(parent, metaData), m_tweaks(this) {
    // Tweaks are applied immediately through um, not through the Apply button.
    setButtons(NoAdditionalButton);

    connect(&m_listProcess, &QProcess::finished, this, &Tweaks::listFinished);
    connect(&m_listProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setLoading(false);
            setErrorMessage(i18n("Could not start um."));
        }
    });

    m_changeProcess.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_changeProcess, &QProcess::finished, this, &Tweaks::changeFinished);
    connect(&m_changeProcess, &QProcess::readyReadStandardOutput, this, &Tweaks::readChangeOutput);
    connect(&m_changeProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_tweaks.setBusyId({});
            setRunning(false);
            setStatus({});
            setErrorMessage(i18n("Could not start um."));
        }
    });

    refresh();
}

TweaksModel *Tweaks::tweaks() { return &m_tweaks; }

bool Tweaks::loading() const { return m_loading; }

bool Tweaks::running() const { return m_running; }

QString Tweaks::status() const { return m_status; }

QString Tweaks::errorMessage() const { return m_errorMessage; }

QString Tweaks::output() const { return m_output; }

void Tweaks::refresh() {
    if (m_loading) {
        return;
    }
    const QString um = UmCli::executable();
    if (um.isEmpty()) {
        setErrorMessage(i18n("um is not installed."));
        return;
    }
    setLoading(true);
    m_listProcess.start(um, {QStringLiteral("tweaks"), QStringLiteral("list"), QStringLiteral("--json")});
}

void Tweaks::setTweakEnabled(const QString &id, bool enabled) {
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

void Tweaks::cancel() {
    if (!m_running) {
        return;
    }
    // pkexec forwards SIGTERM to the privileged child.
    m_cancelled = true;
    m_changeProcess.terminate();
}

void Tweaks::startChange(const QString &id, const QStringList &arguments, const QString &progressMessage) {
    if (m_running) {
        return;
    }
    const QString um = UmCli::executable();
    if (um.isEmpty()) {
        setErrorMessage(i18n("um is not installed."));
        return;
    }

    setErrorMessage({});
    m_output.clear();
    Q_EMIT outputChanged();
    m_cancelled = false;
    m_tweaks.setBusyId(id);
    setRunning(true);
    setStatus(progressMessage);

    // --yes skips um's terminal prompt; the KCM shows its own confirmation first.
    const QStringList umArguments =
        QStringList{QStringLiteral("tweaks")} + arguments + QStringList{QStringLiteral("--yes"), id};
    // um escalates through pkexec itself when it isn't run from a terminal.
    m_changeProcess.start(um, umArguments);
}

void Tweaks::listFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    setLoading(false);
    const QString stdOut = QString::fromUtf8(m_listProcess.readAllStandardOutput());
    const QString stdErr = QString::fromUtf8(m_listProcess.readAllStandardError()).trimmed();

    QList<TweakItem> items;
    if (exitStatus != QProcess::NormalExit || exitCode != 0 || !parseTweaksOutput(stdOut, &items)) {
        // Older um releases have no --json flag; point that out rather than showing a parse error.
        if (stdErr.contains(QLatin1String("flag provided but not defined"))) {
            setErrorMessage(i18n("This version of um is too old to manage tweaks from System Settings."));
        } else {
            setErrorMessage(stdErr.isEmpty() ? i18n("Could not load the list of tweaks.")
                                             : i18n("Could not load the list of tweaks: %1", stdErr));
        }
        return;
    }
    m_tweaks.setTweaks(std::move(items));
}

void Tweaks::changeFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    readChangeOutput();
    m_tweaks.setBusyId({});
    setRunning(false);
    setStatus({});

    if (m_cancelled) {
        setErrorMessage(i18n("The operation was cancelled. The tweak may have been partially applied."));
    } else if (exitStatus == QProcess::NormalExit && UmCli::isAuthenticationFailure(exitCode)) {
        setErrorMessage(i18n("Authentication was cancelled or failed. No changes were made."));
    } else if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        setErrorMessage(i18n("The tweak could not be changed (exit code %1). See the output for details.", exitCode));
    }

    // Always reload: um records state only after a playbook succeeds.
    refresh();
}

void Tweaks::readChangeOutput() {
    const QString text = QString::fromUtf8(m_changeProcess.readAllStandardOutput());
    if (text.isEmpty()) {
        return;
    }
    m_output += text;
    Q_EMIT outputChanged();

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        // Ansible pads headers like "TASK [name] ****" with asterisks; drop the padding.
        QString line = it->trimmed();
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

void Tweaks::setLoading(bool loading) {
    if (m_loading == loading) {
        return;
    }
    m_loading = loading;
    Q_EMIT loadingChanged();
}

void Tweaks::setRunning(bool running) {
    if (m_running == running) {
        return;
    }
    m_running = running;
    Q_EMIT runningChanged();
}

void Tweaks::setStatus(const QString &status) {
    if (m_status == status) {
        return;
    }
    m_status = status;
    Q_EMIT statusChanged();
}

void Tweaks::setErrorMessage(const QString &message) {
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    Q_EMIT errorMessageChanged();
}
