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

#include <KIO/DesktopExecParser>
#include <KLocalizedString>
#include <KMessageBox>

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>

#include <algorithm>

#include "installerdialog.h"
#include "launch.h"

using namespace Qt::StringLiterals;
using namespace OfficeInstaller;

namespace {
    /// hands @p urls to the installer that's already open. False if it couldn't take them
    bool forwardToRunningInstance(const QList<QUrl> &urls) {
        QStringList arguments;
        arguments.reserve(urls.size());
        std::ranges::transform(urls, std::back_inserter(arguments), [](const QUrl &url) { return url.toString(); });
        const auto service = QLatin1StringView(InstallerDialog::DBusService);
        QDBusMessage message = QDBusMessage::createMethodCall(service, u"/"_s, service, u"addDocuments"_s);
        message << arguments;
        return QDBusConnection::sessionBus().call(message).type() != QDBusMessage::ErrorMessage;
    }
} // namespace

int main(int argc, char **argv) {
    const QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
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
        // an empty argument (an empty %U from a broken launcher) would become a URL with no path
        if (argument.isEmpty()) {
            continue;
        }
        urls.append(QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile));
    }

    if (openWithInstalledApps(urls, &app, [](const QString &error) {
            if (!error.isEmpty()) {
                KMessageBox::error(nullptr, error);
            }
            QApplication::quit();
        })) {
        return QApplication::exec();
    }

    // a suite is installed but can't open these formats (ONLYOFFICE and WordPerfect, say):
    // offer the others
    QList<Suite> offered = suites();
    QString installedName;
    if (const KService::Ptr installed = findInstalledSuite()) {
        installedName = installed->name();
        const QString executable = KIO::DesktopExecParser::executableName(installed->exec());
        const auto [first, last] = std::ranges::remove_if(
            offered, [&](const Suite &suite) { return isSuite(suite, installed->desktopEntryName(), executable); });
        offered.erase(first, last);
    }

    // only one installer at a time, so two installs can't run at once. If the other one is
    // just opening its documents or closing, it has let go of the name and this one carries on
    InstallerDialog dialog(urls, offered, installedName);
    if (!dialog.registerOnBus()) {
        if (forwardToRunningInstance(urls)) {
            return 0;
        }
        // it's opening its documents or closing. The name isn't queued for us, so take it
        // if it's free by now, otherwise carry on without it
        if (!dialog.registerOnBus()) {
            qWarning("another office suite installer still owns %s, not taking it over", InstallerDialog::DBusService);
        }
    }
    dialog.show();
    return QApplication::exec();
}
