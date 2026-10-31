// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCMUtils

// Saved together with the main page through System Settings' Apply button.
KCMUtils.SimpleKCM {
    id: page

    title: i18nc("@title", "Advanced Update Settings")

    // Half-hour steps, shown in the locale's time format.
    readonly property var timeChoices: {
        const choices = [];
        for (let minutes = 0; minutes < 24 * 60; minutes += 30) {
            const time = new Date(2000, 0, 1, Math.floor(minutes / 60), minutes % 60);
            choices.push({ minutes: minutes, text: time.toLocaleTimeString(Qt.locale(), Locale.ShortFormat) });
        }
        return choices;
    }

    function timeIndex(minutes) {
        return Math.max(0, Math.min(timeChoices.length - 1, Math.round(minutes / 30)));
    }

    Kirigami.FormLayout {
        QQC2.SpinBox {
            id: inactiveSpin

            Kirigami.FormData.label: i18nc("@label:spinbox", "Install when unused for:")
            enabled: kcm.automaticUpdates
            from: 0
            to: 240
            stepSize: 5
            value: kcm.inactiveMinutes
            onValueModified: kcm.inactiveMinutes = value
            textFromValue: (value, locale) => i18ncp("@item:valuesuffix", "%1 minute", "%1 minutes", value)
            valueFromText: (text, locale) => parseInt(text) || 0
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            text: kcm.automaticUpdates
                ? i18nc("@info", "The device counts as in use while anyone is logged in with their screen unlocked.")
                : i18nc("@info", "Only used when updates install automatically.")
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

        RowLayout {
            enabled: kcm.windowEnabled
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: i18nc("@label:listbox the start of a time range", "From")
            }

            QQC2.ComboBox {
                model: page.timeChoices
                textRole: "text"
                valueRole: "minutes"
                currentIndex: page.timeIndex(kcm.windowStart)
                onActivated: kcm.windowStart = currentValue
            }

            QQC2.Label {
                text: i18nc("@label:listbox the end of a time range", "to")
            }

            QQC2.ComboBox {
                model: page.timeChoices
                textRole: "text"
                valueRole: "minutes"
                currentIndex: page.timeIndex(kcm.windowEnd)
                onActivated: kcm.windowEnd = currentValue
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            text: kcm.windowEnabled
                ? i18nc("@info", "Also used for Apply Tonight.")
                : i18nc("@info", "Updates install whenever the device isn’t in use. Apply Tonight installs between 10 PM and 6 AM.")
            font: Kirigami.Theme.smallFont
            wrapMode: Text.Wrap
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18nc("@label", "System updates:")
            text: i18nc("@option:check", "Install while restarting")
            // System images always take effect when restarting.
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
