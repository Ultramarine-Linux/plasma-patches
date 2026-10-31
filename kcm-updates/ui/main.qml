// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCMUtils

KCMUtils.SimpleKCM {
    id: root

    title: i18nc("@title", "Updates")

    // Number of updates listed under "Software to Be Updated".
    readonly property int shownUpdates: 5
    readonly property bool hasUpdates: kcm.updateCount > 0
    readonly property real labelColumnWidth: Kirigami.Units.gridUnit * 9

    // Every row of "Software to Be Updated" has the same height, and the list
    // always reserves room for shownUpdates rows, so the page doesn't move
    // while updates load or when there are only a few.
    readonly property real updateRowHeight: Math.max(Kirigami.Units.iconSizes.medium,
                                                     defaultMetrics.height + smallMetrics.height)

    FontMetrics {
        id: defaultMetrics
        font: Kirigami.Theme.defaultFont
    }

    FontMetrics {
        id: smallMetrics
        font: Kirigami.Theme.smallFont
    }

    // "500 MB", like the design (SI units, as package managers report them).
    function formatSize(bytes) {
        return Qt.locale().formattedDataSize(Math.abs(bytes), 0, Locale.DataSizeSIFormat);
    }

    function sizeSummary() {
        if (kcm.running) {
            return kcm.status;
        }
        if (!root.hasUpdates && kcm.restartRequired) {
            return i18nc("@info", "Restart to finish updating");
        }
        if (!root.hasUpdates) {
            return i18nc("@info", "Your system is up to date");
        }
        if (kcm.scheduledTonight) {
            return i18nc("@info", "Installing tonight when the device isn’t in use");
        }
        const parts = [];
        if (kcm.downloadSize > 0) {
            parts.push(i18nc("@info %1 is a size like 500 MB", "%1 Download", root.formatSize(kcm.downloadSize)));
        }
        if (kcm.installedSizeChange > 0) {
            parts.push(i18nc("@info %1 is a size like 20 MB", "%1 Installed", root.formatSize(kcm.installedSizeChange)));
        } else if (kcm.installedSizeChange < 0) {
            parts.push(i18nc("@info %1 is a size like 20 MB", "%1 Freed", root.formatSize(kcm.installedSizeChange)));
        }
        return parts.length > 0 ? parts.join(" - ")
                                : i18ncp("@info", "%1 update available", "%1 updates available", kcm.updateCount);
    }

    // Updates are checked when the page opens; F5 checks again.
    Shortcut {
        sequences: [StandardKey.Refresh]
        enabled: !kcm.running
        onActivated: kcm.refresh()
    }

    headerPaddingEnabled: false
    header: Kirigami.InlineMessage {
        position: Kirigami.InlineMessage.Position.Header
        type: Kirigami.MessageType.Error
        text: kcm.errorMessage
        visible: text.length > 0
        showCloseButton: true
        actions: Kirigami.Action {
            text: i18nc("@action:button", "Try Again")
            icon.name: "view-refresh"
            enabled: !kcm.running
            onTriggered: kcm.refresh()
        }
    }

    // SimpleKCM stretches its content to the page width; keep the design's
    // narrow, centered column inside it.
    Item {
        implicitHeight: content.implicitHeight

        ColumnLayout {
            id: content

            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - Kirigami.Units.gridUnit * 2, Kirigami.Units.gridUnit * 28)
            spacing: Kirigami.Units.largeSpacing * 2

            Item {
                implicitHeight: Kirigami.Units.gridUnit
            }

            // Image mode: a new system image is staged and takes over when restarting.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: kcm.restartRequired
                type: Kirigami.MessageType.Positive
                icon.name: "system-reboot"
                text: kcm.stagedVersion.length > 0
                    ? i18nc("@info %1 is a system version", "Version %1 is ready. Restart to start using it.", kcm.stagedVersion)
                    : i18nc("@info", "A system update is ready. Restart to start using it.")
                actions: Kirigami.Action {
                    text: i18nc("@action:button", "Restart…")
                    icon.name: "system-reboot"
                    onTriggered: kcm.restart()
                }
            }

            // --- Pending updates -------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.gridUnit * 2

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 13
                    spacing: Kirigami.Units.largeSpacing

                    RowLayout {
                        spacing: Kirigami.Units.largeSpacing

                        // Release badge, e.g. "45".
                        Rectangle {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                            radius: width / 2
                            color: Qt.alpha(Kirigami.Theme.textColor, 0.75)

                            QQC2.Label {
                                anchors.centerIn: parent
                                text: kcm.distributionVersion
                                color: Kirigami.Theme.backgroundColor
                                font.bold: true
                                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.6
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Kirigami.Heading {
                                Layout.fillWidth: true
                                level: 1
                                text: i18nc("@title", "Regular Updates")
                                wrapMode: Text.Wrap
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: root.sizeSummary()
                                font: Kirigami.Theme.smallFont
                                elide: Text.ElideRight
                            }
                        }
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18nc("@info", "Occasional updates for your system and apps to keep things running smoothly and secure.")
                        wrapMode: Text.Wrap
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    RowLayout {
                        spacing: Kirigami.Units.largeSpacing

                        QQC2.Button {
                            text: i18nc("@action:button install updates now", "Apply")
                            icon.name: "download"
                            enabled: !kcm.running && root.hasUpdates
                            onClicked: kcm.applyUpdates()
                        }

                        QQC2.Button {
                            text: kcm.scheduledTonight ? i18nc("@action:button", "Don’t Apply Tonight")
                                                       : i18nc("@action:button install updates tonight", "Apply Tonight")
                            // A plain crescent; the non-symbolic weather icon is a full moon.
                            icon.name: kcm.scheduledTonight ? "dialog-cancel" : "weather-clear-night-symbolic"
                            enabled: !kcm.running && (root.hasUpdates || kcm.scheduledTonight)
                            onClicked: kcm.scheduledTonight ? kcm.cancelTonight() : kcm.applyTonight()

                            QQC2.ToolTip.visible: hovered && !kcm.scheduledTonight
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            QQC2.ToolTip.text: i18nc("@info:tooltip", "Updates install tonight once the device hasn’t been used for a while and is plugged in")
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    Layout.alignment: Qt.AlignTop
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.bottomMargin: Kirigami.Units.smallSpacing
                        text: i18nc("@title", "Software to Be Updated")
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.15
                    }

                    Item {
                        id: updateList

                        Layout.fillWidth: true
                        Layout.preferredHeight: root.shownUpdates * root.updateRowHeight
                                                + (root.shownUpdates - 1) * updateRows.spacing

                        QQC2.BusyIndicator {
                            anchors.centerIn: parent
                            visible: kcm.running && !root.hasUpdates
                            running: visible
                        }

                        // Kept small: a full PlaceholderMessage would dwarf the list.
                        RowLayout {
                            anchors.centerIn: parent
                            visible: !kcm.running && !root.hasUpdates
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: "checkmark"
                                color: Kirigami.Theme.disabledTextColor
                                isMask: true
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            }

                            QQC2.Label {
                                text: i18nc("@info", "Nothing right now")
                                color: Kirigami.Theme.disabledTextColor
                            }
                        }

                        ColumnLayout {
                            id: updateRows

                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: Kirigami.Units.largeSpacing

                            // Desktop apps and widgets come first, see parseUpdateListing().
                            Repeater {
                                model: kcm.updates

                                delegate: RowLayout {
                                    id: updateRow

                                    required property int index
                                    required property string name
                                    required property string icon
                                    required property string sourceLabel
                                    required property string backend
                                    required property string oldVersion
                                    required property string newVersion

                                    // RPM versions are "epoch:version-release"; people care about the version.
                                    function shortVersion(v) {
                                        if (backend !== "rpm") {
                                            return v;
                                        }
                                        const noEpoch = v.includes(":") ? v.substring(v.indexOf(":") + 1) : v;
                                        return noEpoch.includes("-") ? noEpoch.substring(0, noEpoch.lastIndexOf("-")) : noEpoch;
                                    }
                                    // Image versions (44.20261030.0) are too long for both to fit;
                                    // the new one matters. Technical Information has both.
                                    readonly property string fromVersion: backend === "bootc" || backend === "rpm-ostree"
                                        ? "" : shortVersion(oldVersion)
                                    readonly property string toVersion: shortVersion(newVersion)

                                    visible: index < root.shownUpdates
                                    Layout.preferredHeight: root.updateRowHeight
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.largeSpacing

                                    Kirigami.Icon {
                                        source: updateRow.icon
                                        fallback: "package-x-generic"
                                        Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                        Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0

                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            text: updateRow.name
                                            elide: Text.ElideRight
                                        }

                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            // Flatpak doesn't always report versions, and release-only RPM
                                            // updates (1.0-1 → 1.0-2) have the same short version.
                                            text: updateRow.fromVersion.length > 0 && updateRow.fromVersion !== updateRow.toVersion
                                                ? i18nc("@info %1 source, %2 old version, %3 new version", "%1 - %2 → %3",
                                                        updateRow.sourceLabel, updateRow.fromVersion, updateRow.toVersion)
                                                : updateRow.toVersion.length > 0
                                                    ? i18nc("@info %1 source, %2 new version", "%1 - %2", updateRow.sourceLabel, updateRow.toVersion)
                                                    : updateRow.sourceLabel
                                            font: Kirigami.Theme.smallFont
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }

                    QQC2.Label {
                        // Hidden with opacity so the space stays reserved.
                        opacity: kcm.updateCount > root.shownUpdates ? 1 : 0
                        text: i18nc("@info %1 is a number of updates", "and <b>%1</b> More",
                                    Math.max(0, kcm.updateCount - root.shownUpdates))
                        textFormat: Text.StyledText
                    }

                    QQC2.ItemDelegate {
                        Layout.fillWidth: true
                        Layout.leftMargin: -leftPadding
                        opacity: root.hasUpdates ? 1 : 0
                        enabled: root.hasUpdates && !kcm.running
                        onClicked: kcm.push("TechnicalInformation.qml")

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: i18nc("@action:button opens a list of all updates", "Technical Information")
                                elide: Text.ElideRight
                            }

                            Kirigami.Icon {
                                source: "go-next-symbolic"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            }
                        }
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            // --- Settings (saved with System Settings' Apply button) -------------
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Kirigami.Units.gridUnit
                rowSpacing: Kirigami.Units.largeSpacing * 2

                QQC2.Label {
                    Layout.preferredWidth: root.labelColumnWidth
                    Layout.alignment: Qt.AlignTop
                    text: i18nc("@label", "Automatic Updates")
                    wrapMode: Text.Wrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.RadioButton {
                        id: automaticButton
                        text: i18nc("@option:radio", "Let your device decide")
                        checked: kcm.automaticUpdates
                        onToggled: kcm.automaticUpdates = true
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        leftPadding: automaticButton.indicator.width + automaticButton.spacing
                        text: i18nc("@info", "Updates automatically install when you’re not active")
                        font: Kirigami.Theme.smallFont
                        wrapMode: Text.Wrap
                    }

                    QQC2.RadioButton {
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        text: i18nc("@option:radio", "Ask me")
                        checked: !kcm.automaticUpdates
                        onToggled: kcm.automaticUpdates = false
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        leftPadding: automaticButton.indicator.width + automaticButton.spacing
                        text: i18nc("@info", "You’re notified when updates are ready to go")
                        font: Kirigami.Theme.smallFont
                        wrapMode: Text.Wrap
                    }
                }

                QQC2.Label {
                    Layout.preferredWidth: root.labelColumnWidth
                    text: i18nc("@label", "Advanced update settings")
                    wrapMode: Text.Wrap
                }

                QQC2.Button {
                    id: advancedButton
                    text: i18nc("@action:button", "Advanced Settings")
                    icon.name: "preferences-system"
                    onClicked: kcm.push("AdvancedSettings.qml")
                }
            }
        }
    }
}
