// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "cli.h"

#include <KLocalizedString>

#include <QHash>
#include <QSet>

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    namespace {
        const QSet<QString> optionsTakingValue{u"automatic"_s, u"offline"_s, u"inactive-minutes"_s, u"window"_s};

        QString incompatible(const QString &option, const QString &with) {
            return i18nc("@info:shell %1 is an option, %2 a command", "--%1 can't be used with %2.", option, with);
        }
    } // namespace

    void setUpParser(QCommandLineParser &parser) {
        parser.setApplicationDescription(
            u"Lists and installs RPM (or bootc image), Flatpak, Nix and Plasma widget updates.\n\n"
            u"Commands:\n"
            u"  list            List available updates (--json)\n"
            u"  apply           Install available updates (--yes)\n"
            u"  settings        Show or change automatic update settings (--json, "
            u"--automatic, --offline, --inactive-minutes, --window)\n"
            u"  schedule        Install updates tonight, when the device isn't in use "
            u"(--cancel, --status, --json)\n"
            u"  run-scheduled   Install system updates if the device isn't in use (system "
            u"timers; --tonight)\n"
            u"  run-user        Install your updates or notify about them (user timers; "
            u"--tonight)"_s);
        parser.addHelpOption();
        parser.addPositionalArgument(u"command"_s, u"The command to run."_s);
        parser.addOptions({
            {u"json"_s, u"Output JSON."_s},
            {{u"y"_s, u"yes"_s}, u"Don't ask for confirmation."_s},
            {u"automatic"_s, u"Install updates automatically when the device isn't in use (false: only notify)."_s,
             u"true|false"_s},
            {u"offline"_s, u"Install system updates while restarting instead of on the running system."_s,
             u"true|false"_s},
            {u"inactive-minutes"_s, u"Install once no one has used the device for this many minutes."_s, u"minutes"_s},
            {u"window"_s, u"Only install between these times, e.g. 01:00-05:00 (empty: any time)."_s, u"HH:MM-HH:MM"_s},
            {u"cancel"_s, u"Cancel tonight's updates."_s},
            {u"status"_s, u"Show whether updates are scheduled."_s},
            {u"tonight"_s, u"Run for Apply Tonight, even if automatic updates are off."_s},
        });
    }

    QString checkOptions(const QString &command, const QCommandLineParser &parser) {
        static const QHash<QString, QSet<QString>> allowedOptions{
            {u"list"_s, {u"json"_s}},
            {u"apply"_s, {u"y"_s, u"yes"_s}},
            {u"settings"_s, {u"json"_s, u"automatic"_s, u"offline"_s, u"inactive-minutes"_s, u"window"_s}},
            {u"schedule"_s, {u"cancel"_s, u"status"_s, u"json"_s}},
            {u"run-scheduled"_s, {u"tonight"_s}},
            {u"run-user"_s, {u"tonight"_s}},
        };
        const auto allowed = allowedOptions.constFind(command);
        if (allowed == allowedOptions.constEnd()) {
            return i18nc("@info:shell %1 is a command", "Unknown command \"%1\". See --help.", command);
        }
        for (const QStringList names = parser.optionNames(); const QString &name: names) {
            if (!allowed->contains(name)) {
                return incompatible(name, command);
            }
        }
        if (command == "schedule"_L1) {
            const bool cancel = parser.isSet(u"cancel"_s);
            if (parser.isSet(u"status"_s) && cancel) {
                return incompatible(u"cancel"_s, u"--status"_s);
            }
            if (!parser.isSet(u"status"_s) && parser.isSet(u"json"_s)) {
                return incompatible(u"json"_s, cancel ? u"--cancel"_s : command);
            }
        }
        return {};
    }

    QStringList escalatedCommandLine(const QString &command, const QCommandLineParser &parser) {
        QStringList arguments{command};
        for (const QStringList names = parser.optionNames(); const QString &name: names) {
            if (optionsTakingValue.contains(name)) {
                arguments << u"--%1=%2"_s.arg(name, parser.value(name));
            } else {
                arguments << (name == "y"_L1 ? u"--yes"_s : u"--"_s + name);
            }
        }
        return arguments;
    }

} // namespace UltramarineUpdates
