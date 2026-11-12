// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickConfigModule>

#include <QAbstractListModel>
#include <QFile>
#include <QProcess>
#include <QStringDecoder>
#include <QTimer>

#include <cstdint>

struct TweakItem {
    QString id;
    QString description;
    QString type;
    QString stability;
    QString warning;
    QString stateError;
    bool enabled = false;
};

constexpr int SupportedTweaksOutputVersion = 1;

constexpr int UnrecognizedTweaksOutputVersion = -1;

[[nodiscard]] int tweaksOutputVersion(const QString &output);

bool parseTweaksOutput(const QString &output, QList<TweakItem> *tweaks);

[[nodiscard]] QString stripGoLogTimestamp(const QString &line);

[[nodiscard]] QString changeErrorMessage(bool script, int exitCode, QProcess::ExitStatus exitStatus,
                                         const QString &output);

[[nodiscard]] QString composeTweaksError(const QString &changeError, const QString &listError);

class TweaksModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles : std::uint16_t {
        IdRole = Qt::UserRole + 1,
        DescriptionRole,
        TypeRole,
        StabilityRole,
        WarningRole,
        StateErrorRole,
        EnabledRole,
        BusyRole,
    };

    explicit TweaksModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const;

    void setTweaks(QList<TweakItem> tweaks);

    [[nodiscard]] const TweakItem *find(const QString &id) const;

    void setBusyId(const QString &id);

Q_SIGNALS:
    void countChanged();

private:
    QList<TweakItem> m_tweaks;
    QString m_busyId;
};

class Tweaks : public KQuickConfigModule {
    Q_OBJECT
    Q_PROPERTY(TweaksModel *tweaks READ tweaks CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY listStateChanged)
    Q_PROPERTY(bool listFailed READ listFailed NOTIFY listStateChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)

public:
    explicit Tweaks(QObject *parent, const KPluginMetaData &metaData);
    ~Tweaks() override;

    Tweaks(const Tweaks &) = delete;
    Tweaks &operator=(const Tweaks &) = delete;
    Tweaks(Tweaks &&) = delete;
    Tweaks &operator=(Tweaks &&) = delete;

    void load() override;

    TweaksModel *tweaks();

    [[nodiscard]] bool loading() const;

    [[nodiscard]] bool loaded() const;

    [[nodiscard]] bool listFailed() const;

    [[nodiscard]] bool running() const;

    [[nodiscard]] const QString &status() const;

    [[nodiscard]] const QString &errorMessage() const;

    [[nodiscard]] const QString &output() const;

    Q_INVOKABLE void refresh();

    Q_INVOKABLE void setTweakEnabled(const QString &id, bool enabled);

    Q_INVOKABLE void runTweak(const QString &id);

    Q_INVOKABLE void clearError();

Q_SIGNALS:
    void loadingChanged();
    void listStateChanged();
    void runningChanged();
    void statusChanged();
    void errorMessageChanged();
    void outputChanged();

private:
    void startChange(const QString &id, const QStringList &arguments, const QString &progressMessage);

    void listFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void changeFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void readChangeOutput();
    void closeChangeOutput();
    void showLastMeaningfulLineAsStatus(const QString &completeLines);

    void setLoading(bool value);
    void setRunning(bool value);
    void setStatus(const QString &value);
    void setListError(const QString &message);
    void setChangeError(const QString &message);
    void updateErrorMessage();
    void setListState(bool wasLoaded, bool failed);
    void killListProcessGroup();
    void detachRunningChange();

    TweaksModel m_tweaks;
    QProcess m_listProcess;
    QProcess *m_changeProcess;
    QFile m_changeOutputFile;
    QTimer m_changeOutputPollTimer;
    QStringDecoder m_changeDecoder{QStringDecoder::Utf8};
    QString m_unfinishedChangeLine;
    QString m_status;
    QString m_errorMessage;
    QString m_listError;
    QString m_changeError;
    QString m_output;
    bool m_loading = false;
    bool m_loaded = false;
    bool m_listFailed = false;
    bool m_running = false;
    bool m_changeIsScript = false;
    bool m_reloadAfterCurrentLoad = false;
};
