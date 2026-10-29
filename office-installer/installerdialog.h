// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>
#include <QProcess>
#include <QUrl>

#include "suites.h"

class KMessageWidget;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QListWidget;
class QStackedWidget;

namespace OfficeInstaller {

    /// Asks which office suite to install, installs it and opens the documents with it.
    class InstallerDialog : public QDialog {
        Q_OBJECT

    public:
        explicit InstallerDialog(const QList<QUrl> &urls, QWidget *parent = nullptr);

        /// Ignored while installing: interrupting flatpak or PackageKit could leave a partial install.
        void reject() override;

    private:
        QWidget *createChoicePage();
        QWidget *createProgressPage();

        void updateSources() const;
        void install();
        void readOutput();
        void installFinished(int exitCode, QProcess::ExitStatus exitStatus);
        void openDocuments();
        void setBusy(bool busy) const;

        [[nodiscard]] const Suite &selectedSuite() const;
        [[nodiscard]] Source selectedSource() const;

        QList<QUrl> m_urls;
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
        QDialogButtonBox *m_buttons;

        QProcess *m_process = nullptr;
        /// The installer's last line of output, shown while installing and on failure.
        QString m_lastLine;
    };

} // namespace OfficeInstaller
