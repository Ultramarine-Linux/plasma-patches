// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

// ultramarine-office-installer: opens office documents when no office suite is
// installed, by offering to install one.
//
// Its desktop file claims the office MIME types, so it's the only handler until a
// suite is installed. KDE breaks ties between handlers by desktop file name, and
// GIO doesn't rank them at all, so it can still be picked over an installed suite.
// Then it passes the documents on without showing anything.

#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>

#include "installerdialog.h"
#include "launch.h"

using namespace Qt::StringLiterals;
using namespace OfficeInstaller;

int main(int argc, char **argv) {
    const QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("ultramarine_office_installer");
    QApplication::setApplicationName(u"ultramarine-office-installer"_s);
    QApplication::setDesktopFileName(u"org.ultramarine.OfficeInstaller"_s);
    QApplication::setWindowIcon(QIcon::fromTheme(u"system-software-install"_s));

    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("Installs an office suite to open office documents with."));
    parser.addHelpOption();
    parser.addPositionalArgument(u"files"_s, i18n("Documents to open once an office suite is installed."),
                                 u"[files...]"_s);
    parser.process(app);

    QList<QUrl> urls;
    for (const QStringList arguments = parser.positionalArguments(); const QString &argument: arguments) {
        urls.append(QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile));
    }

    if (openWithInstalledApps(urls, &app, [] { QApplication::quit(); })) {
        return QApplication::exec();
    }

    InstallerDialog dialog(urls);
    dialog.show();
    return QApplication::exec();
}
