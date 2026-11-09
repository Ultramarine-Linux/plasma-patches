// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QCommandLineParser>
#include <QString>
#include <QStringList>

/// the helper's command line, kept apart from main() so it can be tested
namespace UltramarineUpdates {

    /// adds the helper's options and help text to @p parser
    void setUpParser(QCommandLineParser &parser);

    /// checks that @p command exists and accepts every option given. Empty if it does.
    QString checkOptions(const QString &command, const QCommandLineParser &parser);

    /**
     * this command line, rebuilt for escalating with @p command first: the polkit
     * actions match the helper's path and argv[1]. Options keep their values, and
     * -y becomes --yes.
     */
    QStringList escalatedCommandLine(const QString &command, const QCommandLineParser &parser);

} // namespace UltramarineUpdates
