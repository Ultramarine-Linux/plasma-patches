// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "process.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    CommandResult capture(const QString &program, const QStringList &arguments,
                          const QProcessEnvironment &environment) {
        CommandResult result;
        if (program.isEmpty()) {
            return result;
        }
        QProcess process;
        process.setProcessEnvironment(environment);
        process.start(program, arguments);
        if (!process.waitForStarted(-1)) {
            return result;
        }
        result.started = true;
        process.waitForFinished(-1);
        result.out = process.readAllStandardOutput();
        result.err = process.readAllStandardError();
        result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
        return result;
    }

    bool commandOutput(const QString &program, const QStringList &arguments, QString *output, QString *error,
                       const QList<int> &allowedExitCodes) {
        const CommandResult result = capture(program, arguments);
        if (!result.started) {
            if (error) {
                *error = u"%1 could not be started"_s.arg(program);
            }
            return false;
        }
        if (result.exitCode != 0 && !allowedExitCodes.contains(result.exitCode)) {
            if (error) {
                *error = u"%1 failed: %2"_s.arg(QFileInfo(program).fileName(), QString::fromUtf8(result.err).trimmed());
            }
            return false;
        }
        if (output) {
            *output = QString::fromUtf8(result.out).trimmed();
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
