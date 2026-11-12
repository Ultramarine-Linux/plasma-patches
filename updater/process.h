// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <chrono>

namespace UltramarineUpdates {

    struct CommandResult {
        bool started = false;
        int exitCode = -1;
        QByteArray out;
        QByteArray err;

        [[nodiscard]] bool succeeded() const { return started && exitCode == 0; }
    };

    CommandResult capture(const QString &program, const QStringList &arguments,
                          const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment());

    CommandResult captureWithTimeout(const QString &program, const QStringList &arguments,
                                     std::chrono::milliseconds timeout);

    bool commandOutput(const QString &program, const QStringList &arguments, QString *output, QString *error,
                       const QList<int> &allowedExitCodes = {});

    QString localeIndependentOutput(const QString &program, const QStringList &arguments);

    int runForwarded(const QString &program, const QStringList &arguments);

    QString findExecutable(const QString &name, const QStringList &paths = {});

    QString lastLine(const QString &text);

} // namespace UltramarineUpdates
