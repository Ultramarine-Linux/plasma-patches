// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "process.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <KLocalizedString>

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    namespace {
        CommandResult run(const QString &program, const QStringList &arguments, const QProcessEnvironment &environment,
                          const int timeoutMs) {
            CommandResult result;
            if (program.isEmpty()) {
                return result;
            }
            QProcess process;
            process.setProcessEnvironment(environment);
            // nothing is ever written to it, so a program asking for input gets EOF instead of hanging
            process.setStandardInputFile(QProcess::nullDevice());
            process.start(program, arguments);
            if (!process.waitForStarted(-1)) {
                return result;
            }
            result.started = true;
            const bool finished = process.waitForFinished(timeoutMs);
            if (!finished) {
                process.kill();
                process.waitForFinished(-1);
            }
            result.out = process.readAllStandardOutput();
            result.err = process.readAllStandardError();
            result.exitCode = finished && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
            return result;
        }
    } // namespace

    CommandResult capture(const QString &program, const QStringList &arguments,
                          const QProcessEnvironment &environment) {
        return run(program, arguments, environment, -1);
    }

    CommandResult captureWithTimeout(const QString &program, const QStringList &arguments,
                                     const std::chrono::milliseconds timeout) {
        return run(program, arguments, QProcessEnvironment::systemEnvironment(), static_cast<int>(timeout.count()));
    }

    bool commandOutput(const QString &program, const QStringList &arguments, QString *output, QString *error,
                       const QList<int> &allowedExitCodes) {
        const auto [started, exitCode, out, err] = capture(program, arguments);
        if (!started) {
            if (error) {
                *error = i18nc("@info:status %1 is a program", "%1 could not be started", program);
            }
            return false;
        }
        if (exitCode != 0 && !allowedExitCodes.contains(exitCode)) {
            if (error) {
                *error = i18nc("@info:status %1 is a program, %2 its error message", "%1 failed: %2",
                               QFileInfo(program).fileName(), QString::fromUtf8(err).trimmed());
            }
            return false;
        }
        if (output) {
            *output = QString::fromUtf8(out).trimmed();
        }
        return true;
    }

    QString localeIndependentOutput(const QString &program, const QStringList &arguments) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(u"LC_ALL"_s, u"C"_s);
        return QString::fromUtf8(capture(program, arguments, environment).out);
    }

    int runForwarded(const QString &program, const QStringList &arguments) {
        if (program.isEmpty()) {
            return -1;
        }
        QProcess process;
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        process.setInputChannelMode(QProcess::ForwardedInputChannel);
        // QProcess resets SIGPIPE to the default in children. Keep main()'s SIG_IGN, so dnf or
        // flatpak keep installing (with failing writes) if the KCM closes our output.
        QProcess::UnixProcessParameters parameters;
        parameters.flags = QProcess::UnixProcessFlag::IgnoreSigPipe;
        process.setUnixProcessParameters(parameters);
        process.start(program, arguments);
        if (!process.waitForStarted(-1)) {
            return -1;
        }
        process.waitForFinished(-1);
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    }

    QString findExecutable(const QString &name, const QStringList &paths) {
        for (const QString &path: paths) {
            if (const QFileInfo info(path); info.isFile() && info.isExecutable()) {
                return path;
            }
        }
        return QStandardPaths::findExecutable(name);
    }

    QString lastLine(const QString &text) {
        const QStringList lines = text.trimmed().split(u'\n');
        return lines.constLast().trimmed();
    }

} // namespace UltramarineUpdates
