// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

// ultramarine-dgpu-launch: runs an app on the discrete GPU. The launchers'
// "Launch Using Dedicated Graphics Card" action passes the app's desktop file.
//
// It runs the desktop file's Exec line itself, with the discrete GPU's
// environment added. KIO's ApplicationLauncherJob (and so kioclient exec and
// PrefersNonDefaultGPU) starts DBusActivatable apps over D-Bus instead, which
// drops the environment, so they'd silently run on the integrated GPU.

#include <KIO/CommandLauncherJob>
#include <KIO/DesktopExecParser>
#include <KLocalizedString>
#include <KMessageBox>
#include <KService>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QUrl>

#include "gpuenvironment.h"

using namespace Qt::StringLiterals;

namespace {
    int fail(const QString &message) {
        KMessageBox::error(nullptr, message, i18nc("@title:window", "Launch Using Dedicated Graphics Card"));
        return 1;
    }
} // namespace

int main(int argc, char **argv) {
    // the launcher's startup notification, read before Qt's Wayland plugin uses up the token
    const QByteArray startupId = qEnvironmentVariableIsSet("XDG_ACTIVATION_TOKEN") ? qgetenv("XDG_ACTIVATION_TOKEN")
                                                                                   : qgetenv("DESKTOP_STARTUP_ID");
    qunsetenv("XDG_ACTIVATION_TOKEN");
    qunsetenv("DESKTOP_STARTUP_ID");

    const QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    QApplication::setApplicationName(u"ultramarine-dgpu-launch"_s);
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("Runs an app on the discrete GPU."));
    parser.addHelpOption();
    parser.addPositionalArgument(u"desktop-file"_s, i18n("The app's desktop file, as a path or file: URL."));
    parser.process(app);
    if (parser.positionalArguments().size() != 1) {
        parser.showHelp(1);
    }

    const QUrl url =
        QUrl::fromUserInput(parser.positionalArguments().constFirst(), QDir::currentPath(), QUrl::AssumeLocalFile);
    const auto service = KService::Ptr(new KService(url.toLocalFile()));
    if (!url.isLocalFile() || !service->isValid() || !service->isApplication()) {
        return fail(i18n("“%1” isn't an app.", url.toDisplayString(QUrl::PreferLocalFile)));
    }

    // handles field codes, Terminal=true and the like, the same way KIO does
    const KIO::DesktopExecParser execParser(*service, {});
    QStringList arguments = execParser.resultingArguments();
    if (arguments.isEmpty()) {
        return fail(execParser.errorMessage().isEmpty() ? i18n("%1 doesn't say how to start it.", service->name())
                                                        : execParser.errorMessage());
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(DgpuLaunch::discreteGpuEnvironment());

    const QString program = arguments.takeFirst();
    auto *job = new KIO::CommandLauncherJob(program, arguments);
    // starts it in its own app-*.service unit with the app's name and icon, like the launchers do
    job->setDesktopName(service->desktopEntryName());
    job->setWorkingDirectory(service->workingDirectory());
    job->setProcessEnvironment(environment);
    if (!startupId.isEmpty()) {
        job->setStartupId(startupId);
    }
    int exitCode = 0;
    QObject::connect(job, &KJob::result, &app, [&exitCode](const KJob *job) {
        if (job->error() != 0) {
            exitCode = fail(job->errorString());
        }
        QApplication::quit();
    });
    job->start();
    QApplication::exec();
    return exitCode;
}
