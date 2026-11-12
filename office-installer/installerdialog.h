// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>
#include <QPointer>
#include <QUrl>

#include "suites.h"

class KMessageWidget;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QListWidget;
class QStackedWidget;

namespace OfficeInstaller {

    class Installer;

    class InstallerDialog : public QDialog {
        Q_OBJECT
        Q_CLASSINFO("D-Bus Interface", "org.ultramarine.OfficeInstaller")

    public:
        InstallerDialog(const QList<QUrl> &urls, const QList<Suite> &offered, const QString &incapableInstalledSuite,
                        QWidget *parent = nullptr);

        void reject() override;

    public Q_SLOTS:
        Q_SCRIPTABLE void addDocuments(const QStringList &urls);

    public:
        static constexpr auto DBusService = "org.ultramarine.OfficeInstaller";

        bool registerOnBus();
        static void unregisterFromBus();

    private:
        QWidget *createChoicePage();
        QWidget *createProgressPage();
        void fetchFlathubIcons();

        void updateSources() const;
        void install();
        void installFinished(bool success, bool canceled, const QString &error);
        void openDocuments();
        void setBusy(bool busy) const;

        [[nodiscard]] const Suite &selectedSuite() const;
        [[nodiscard]] Source selectedSource() const;

        QList<QUrl> m_urls;
        QString m_incapableInstalledSuite;
        QList<Suite> m_suites;
        bool m_imageBased;

        QStackedWidget *m_pages;
        KMessageWidget *m_error;
        QListWidget *m_suiteList;
        QWidget *m_sourceRow;
        QLabel *m_sourceLabel;
        QComboBox *m_sourceBox;
        QLabel *m_progressIcon;
        QLabel *m_progressTitle;
        QLabel *m_progressDetail;
        QLabel *m_subtitle = nullptr;
        QDialogButtonBox *m_buttons;

        QPointer<Installer> m_installer;
        bool m_waitingForAppsToStart = false;
    };

} // namespace OfficeInstaller
