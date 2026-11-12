// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

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
    // closing a window like KIO's untrusted program prompt would otherwise kill the launch jobs
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("Installs an office suite to open office documents with."));
    parser.addHelpOption();
    parser.addPositionalArgument(u"files"_s, i18n("Documents to open once an office suite is installed."),
                                 u"[files...]"_s);
    parser.process(app);

    QList<QUrl> urls;
    for (const QStringList arguments = parser.positionalArguments(); const QString &argument: arguments) {
        if (argument.isEmpty()) {
            continue;
        }
        urls.append(QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile));
    }

    if (openWithInstalledApps(urls, &app, [](const QString &error, const QList<QUrl> & /*failed*/) {
            if (!error.isEmpty()) {
                KMessageBox::error(nullptr, error);
            }
            // a job can fail before the event loop runs, when quit() would do nothing
            QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
        })) {
        return QApplication::exec();
    }

    QList<Suite> offered = offeredSuites();
    QString incapableInstalledSuiteName;
    if (const KService::Ptr installed = findInstalledSuite()) {
        incapableInstalledSuiteName = installed->name();
        const QString executable = KIO::DesktopExecParser::executableName(installed->exec());
        const auto [first, last] = std::ranges::remove_if(
            offered, [&](const Suite &suite) { return isSuite(suite, installed->desktopEntryName(), executable); });
        offered.erase(first, last);
    }

    InstallerDialog dialog(urls, offered, incapableInstalledSuiteName);
    QObject::connect(&dialog, &QDialog::finished, &app, [] { QApplication::quit(); });
    if (!dialog.registerOnBus()) {
        if (forwardToRunningInstance(urls)) {
            return 0;
        }
        // the other installer let go of the name while opening its documents
        if (!dialog.registerOnBus()) {
            qWarning("another office suite installer still owns %s, not taking it over", InstallerDialog::DBusService);
        }
    }
    dialog.show();
    return QApplication::exec();
}
