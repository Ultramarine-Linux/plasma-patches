// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCMUtils

// saved together with the main page through System Settings' Apply button
KCMUtils.SimpleKCM {
    id: page

    title: i18nc("@title", "Advanced Update Settings")

    // errors from saving show here too, since this is where the settings are
    headerPaddingEnabled: false
    header: ErrorMessage {}

    function timeChoice(minutes) {
        const time = new Date(2000, 0, 1, Math.floor(minutes / 60), minutes % 60);
        return { minutes: minutes, text: time.toLocaleTimeString(Qt.locale(), Locale.ShortFormat) };
    }

    // half-hour steps, shown in the locale's time format. A saved time that isn't on
    // the grid (like 01:15 set elsewhere) is listed too, so it isn't changed by opening this page.
    function timeChoices(current) {
        const choices = [];
        for (let minutes = 0; minutes < 24 * 60; minutes += 30) {
            if (current > minutes - 30 && current < minutes && current % 30 !== 0) {
                choices.push(timeChoice(current));
            }
            choices.push(timeChoice(minutes));
        }
        if (current > 24 * 60 - 30 && current % 30 !== 0) {
            choices.push(timeChoice(current));
        }
        return choices;
    }

    function timeIndex(choices, minutes) {
        return Math.max(0, choices.findIndex(choice => choice.minutes === minutes));
    }

    Kirigami.FormLayout {
        QQC2.SpinBox {
            id: inactiveSpin

            Kirigami.FormData.label: i18nc("@label:spinbox", "Install when unused for:")
            from: 0
            to: 24 * 60
            stepSize: 5
            value: kcm.inactiveMinutes
            onValueModified: kcm.inactiveMinutes = value
            textFromValue: (value, locale) => i18ncp("@item:valuesuffix", "%1 minute", "%1 minutes", value)
            valueFromText: (text, locale) => parseInt(text) || 0
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            text: i18nc("@info", "Used for automatic updates and Apply Tonight. The device counts as in use while anyone is logged in with their screen unlocked.")
            font: Kirigami.Theme.smallFont
            wrapMode: Text.Wrap
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        QQC2.CheckBox {
            id: windowCheck

            Kirigami.FormData.label: i18nc("@label", "Update times:")
            text: i18nc("@option:check", "Only during these hours")
            checked: kcm.windowEnabled
            onToggled: kcm.windowEnabled = checked
        }

        // one labeled row each, so no sentence is split around the combo boxes
        QQC2.ComboBox {
            Kirigami.FormData.label: i18nc("@label:listbox the start of the update time range", "Starting at:")
            Accessible.name: i18nc("@label:listbox", "Start time")
            enabled: kcm.windowEnabled
            model: page.timeChoices(kcm.windowStart)
            textRole: "text"
            valueRole: "minutes"
            // depends on model too, so it follows when an off-grid time leaves the list
            currentIndex: page.timeIndex(model, kcm.windowStart)
            onActivated: kcm.windowStart = currentValue
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: i18nc("@label:listbox the end of the update time range", "Ending at:")
            Accessible.name: i18nc("@label:listbox", "End time")
            enabled: kcm.windowEnabled
            model: page.timeChoices(kcm.windowEnd)
            textRole: "text"
            valueRole: "minutes"
            currentIndex: page.timeIndex(model, kcm.windowEnd)
            onActivated: kcm.windowEnd = currentValue
        }

        // the helper rejects an empty window, so it can't be saved (see isSaveNeeded())
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            visible: !kcm.windowValid
            type: Kirigami.MessageType.Warning
            text: i18nc("@info", "Choose different start and end times.")
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            text: kcm.windowEnabled
                ? i18nc("@info", "Also used for Apply Tonight.")
                // the helper's default for Apply Tonight, 22:00-06:00 (see schedule.cpp)
                : i18nc("@info %1 and %2 are times of day", "Updates install whenever the device isn’t in use. Apply Tonight installs between %1 and %2.",
                        page.timeChoice(22 * 60).text, page.timeChoice(6 * 60).text)
            font: Kirigami.Theme.smallFont
            wrapMode: Text.Wrap
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18nc("@label", "System updates:")
            text: i18nc("@option:check", "Install while restarting")
            // system images always take effect when restarting
            enabled: !kcm.imageMode
            checked: kcm.imageMode || kcm.offlineUpdates
            onToggled: kcm.offlineUpdates = checked
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            text: kcm.imageMode
                ? i18nc("@info", "This system updates as a whole: the new version is prepared in the background and takes over the next time you restart. Your device is never restarted for you.")
                : i18nc("@info", "Updates are downloaded ahead of time and installed the next time you restart. Your device is never restarted for you.")
            font: Kirigami.Theme.smallFont
            wrapMode: Text.Wrap
        }
    }
}
