// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QCommandLineParser>
#include <QString>
#include <QStringList>

namespace UltramarineUpdates {

    void setUpParser(QCommandLineParser &parser);

    QString checkOptions(const QString &command, const QCommandLineParser &parser);

    QStringList escalatedCommandLine(const QString &command, const QCommandLineParser &parser);

} // namespace UltramarineUpdates
