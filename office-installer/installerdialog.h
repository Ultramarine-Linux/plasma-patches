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

    /// asks which office suite to install, installs it and opens the documents with it
    class InstallerDialog : public QDialog {
        Q_OBJECT
        Q_CLASSINFO("D-Bus Interface", "org.ultramarine.OfficeInstaller")

    public:
        /**
         * offers @p offered to open @p urls with. @p installedSuite is the name of
         * an office suite that's installed but can't open them, or empty if there's none.
         */
        InstallerDialog(const QList<QUrl> &urls, const QList<Suite> &offered, const QString &installedSuite,
                        QWidget *parent = nullptr);

        /**
         * cancels the install while installing. While the documents are opening, only
         * hides the dialog until their apps have started.
         */
        void reject() override;

    public Q_SLOTS:
        /// documents from another launch of the installer, opened too once a suite is installed
        Q_SCRIPTABLE void addDocuments(const QStringList &urls);

    public:
        /// the session bus name that makes the dialog unique, and the interface addDocuments() is on
        static constexpr auto DBusService = "org.ultramarine.OfficeInstaller";

        /**
         * exports addDocuments() and takes DBusService. Returns false if another
         * installer has the name. The object is exported first, so a launch that
         * sees the name can always reach addDocuments().
         */
        bool registerOnBus();
        /**
         * gives up the name once the documents are being opened or the dialog is
         * closing, so later launches open their own dialog instead of handing
         * over documents that would never be opened
         */
        static void unregisterFromBus();

    private:
        QWidget *createChoicePage();
        QWidget *createProgressPage();
        /// downloads Flathub's AppStream data and swaps in the suites' icons once it's there
        void fetchIcons();

        void updateSources() const;
        void install();
        void installFinished(bool success, bool canceled, const QString &error);
        void openDocuments();
        void setBusy(bool busy) const;

        [[nodiscard]] const Suite &selectedSuite() const;
        [[nodiscard]] Source selectedSource() const;

        QList<QUrl> m_urls;
        QString m_installedSuite;
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
        /// waiting for the documents' apps to start
        bool m_opening = false;
    };

} // namespace OfficeInstaller
