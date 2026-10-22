// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickManagedConfigModule>

#include <QAbstractListModel>
#include <QProcess>

struct TweakItem {
    QString id;
    QString description;
    /// One of "toggle", "oneshot" or "script", as reported by um.
    QString type;
    QString stability;
    QString warning;
    bool enabled = false;
};

/**
 * Parses the JSON emitted by `um tweaks list --json`. Tweaks are grouped by type
 * (toggles, then one-time tweaks, then scripts) and sorted by description.
 * Returns false and leaves @p tweaks untouched if the output is not valid.
 */
bool parseTweaksOutput(const QString &output, QList<TweakItem> *tweaks);

class TweaksModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        DescriptionRole,
        TypeRole,
        StabilityRole,
        WarningRole,
        EnabledRole,
        BusyRole,
    };

    /// Creates the model that exposes the available tweaks to QML.
    explicit TweaksModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Returns the number of tweaks in the model.
    [[nodiscard]] int count() const;

    /// Replaces every tweak in the model.
    void setTweaks(QList<TweakItem> tweaks);

    /// Returns the tweak with @p id, or nullptr if there is none.
    [[nodiscard]] const TweakItem *find(const QString &id) const;

    /// Marks the tweak with @p id as the one being changed, clearing any other.
    void setBusyId(const QString &id);

Q_SIGNALS:
    /// Emitted when the number of tweaks changes.
    void countChanged();

private:
    QList<TweakItem> m_tweaks;
    QString m_busyId;
};

class Tweaks : public KQuickManagedConfigModule {
    Q_OBJECT
    Q_PROPERTY(TweaksModel *tweaks READ tweaks CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)

public:
    /// Creates the Ultramarine tweaks configuration module.
    explicit Tweaks(QObject *parent, const KPluginMetaData &metaData);

    /// Returns the model containing the available tweaks.
    TweaksModel *tweaks();

    /// Returns whether the tweak list is being loaded.
    [[nodiscard]] bool loading() const;

    /// Returns whether a tweak is being enabled, disabled or run.
    [[nodiscard]] bool running() const;

    /// Returns the latest progress line from the running operation.
    [[nodiscard]] QString status() const;

    /// Returns the current error shown in the UI banner.
    [[nodiscard]] QString errorMessage() const;

    /// Returns the full output of the last tweak operation.
    [[nodiscard]] QString output() const;

    /// Reloads the tweak list from um.
    Q_INVOKABLE void refresh();

    /// Enables or disables a toggle tweak, or applies a oneshot tweak.
    Q_INVOKABLE void setTweakEnabled(const QString &id, bool enabled);

    /// Runs a script tweak.
    Q_INVOKABLE void runTweak(const QString &id);

    /// Stops the running operation.
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void loadingChanged();
    void runningChanged();
    void statusChanged();
    void errorMessageChanged();
    void outputChanged();

private:
    /// Starts a privileged um invocation for tweak @p id.
    void startChange(const QString &id, const QStringList &arguments, const QString &progressMessage);

    void listFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void changeFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void readChangeOutput();

    void setLoading(bool loading);
    void setRunning(bool running);
    void setStatus(const QString &status);
    void setErrorMessage(const QString &message);

    TweaksModel m_tweaks;
    QProcess m_listProcess;
    QProcess m_changeProcess;
    QString m_status;
    QString m_errorMessage;
    QString m_output;
    bool m_loading = false;
    bool m_running = false;
    bool m_cancelled = false;
};
