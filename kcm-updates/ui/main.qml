// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCMUtils

KCMUtils.SimpleKCM {
    id: root

    title: i18nc("@title", "Updates")

    readonly property int shownUpdates: kcm.previewLimit
    readonly property bool hasUpdates: kcm.updateCount > 0
    readonly property bool canApply: kcm.installableCount > 0
    readonly property real labelColumnWidth: Kirigami.Units.gridUnit * 9
    readonly property bool tonightActive: kcm.scheduledTonight || kcm.tonightPartial

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

    function formatSize(bytes) {
        return Qt.locale().formattedDataSize(Math.abs(bytes), 0, Locale.DataSizeSIFormat);
    }

    function sizeSummary() {
        if (kcm.running) {
            return kcm.status;
        }
        if (kcm.tonightPartial) {
            return i18nc("@info", "Some updates are still scheduled to install tonight");
        }
        if (!root.canApply && kcm.offlinePending) {
            return i18nc("@info", "Restart to install updates");
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
        if (parts.length === 2) {
            return i18nc("@info %1 is a download size, %2 an installed or freed size", "%1 · %2", parts[0], parts[1]);
        }
        return parts.length === 1 ? parts[0]
                                  : i18ncp("@info", "%1 update available", "%1 updates available", kcm.updateCount);
    }

    Shortcut {
        sequences: [StandardKey.Refresh]
        enabled: !kcm.running
        onActivated: kcm.refresh()
    }

    actions: Kirigami.Action {
        text: i18nc("@action:button check for updates again", "Refresh")
        icon.name: "view-refresh"
        enabled: !kcm.running
        onTriggered: kcm.refresh()
    }

    headerPaddingEnabled: false
    header: ErrorMessage {}

    Item {
        implicitHeight: content.implicitHeight + content.y * 2

        ColumnLayout {
            id: content

            anchors.horizontalCenter: parent.horizontalCenter
            y: Kirigami.Units.gridUnit * 2
            width: Math.min(parent.width - Kirigami.Units.gridUnit * 2, Kirigami.Units.gridUnit * 28)
            spacing: Kirigami.Units.largeSpacing * 2

            Kirigami.InlineMessage {
                readonly property bool wanted: kcm.successMessage.length > 0 && !kcm.restartRequired
                readonly property bool pageShown: parent !== null && parent.visible

                Layout.fillWidth: true
                type: Kirigami.MessageType.Positive
                text: kcm.successMessage
                showCloseButton: true
                // not a binding: the close button sets visible = false, which would break it
                visible: false
                onWantedChanged: visible = wanted
                Component.onCompleted: visible = wanted
                onVisibleChanged: if (!visible && wanted && pageShown) kcm.clearSuccess()
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: kcm.restartRequired
                type: Kirigami.MessageType.Positive
                icon.name: "system-reboot"
                text: kcm.offlinePending
                    ? i18nc("@info", "Updates are ready to install. Restart to install them.")
                    : kcm.stagedVersion.length > 0
                        ? i18nc("@info %1 is a system version", "Version %1 is ready. Restart to start using it.", kcm.stagedVersion)
                        : i18nc("@info", "A system update is ready. Restart to start using it.")
                actions: Kirigami.Action {
                    text: i18nc("@action:button", "Restart…")
                    icon.name: "system-reboot"
                    onTriggered: kcm.restart()
                }
            }

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

                        Item {
                            id: badge

                            readonly property bool dark: Kirigami.ColorUtils.brightnessForColor(Kirigami.Theme.backgroundColor)
                                                         === Kirigami.ColorUtils.Dark
                            readonly property bool showsWallpaper: wallpaper.status === Image.Ready
                            readonly property bool darkWallpaper: wallpaperColors.analyzed
                                ? Kirigami.ColorUtils.brightnessForColor(wallpaperColors.average) === Kirigami.ColorUtils.Dark
                                : dark

                            Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 3

                            Rectangle {
                                anchors.fill: parent
                                visible: !badge.showsWallpaper
                                radius: width / 2
                                color: Qt.alpha(Kirigami.Theme.textColor, 0.75)
                            }

                            Kirigami.ShadowedImage {
                                id: wallpaper

                                anchors.fill: parent
                                radius: width / 2
                                source: badge.dark ? kcm.darkWallpaper : kcm.wallpaper
                                fillMode: Image.PreserveAspectCrop
                                sourceSize.height: height * Screen.devicePixelRatio
                                asynchronous: true
                                border.width: 1
                                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.2)
                                onStatusChanged: if (status === Image.Ready) wallpaperColors.update()
                            }

                            Kirigami.ImageColors {
                                id: wallpaperColors

                                property bool analyzed: false

                                source: wallpaper
                                onPaletteChanged: analyzed = true
                            }

                            QQC2.Label {
                                anchors.centerIn: parent
                                text: kcm.distributionVersion
                                color: !badge.showsWallpaper ? Kirigami.Theme.backgroundColor
                                     : badge.darkWallpaper ? "white" : "#1b1e20"
                                font.bold: true
                                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.6
                                layer.enabled: badge.showsWallpaper
                                layer.effect: MultiEffect {
                                    shadowEnabled: true
                                    shadowColor: badge.darkWallpaper ? Qt.alpha("black", 0.8) : Qt.alpha("white", 0.9)
                                    shadowBlur: 0.5
                                    shadowHorizontalOffset: 0
                                    shadowVerticalOffset: 0
                                }
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
                                opacity: 0.7
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
                            enabled: !kcm.running && !kcm.scheduling && root.canApply
                            onClicked: kcm.applyUpdates()
                        }

                        QQC2.Button {
                            text: root.tonightActive ? i18nc("@action:button", "Don’t Apply Tonight")
                                                       : i18nc("@action:button install updates tonight", "Apply Tonight")
                            // a plain crescent, since the non-symbolic weather icon is a full moon
                            icon.name: root.tonightActive ? "dialog-cancel" : "weather-clear-night-symbolic"
                            enabled: !kcm.running && !kcm.scheduling && (root.canApply || root.tonightActive)
                            onClicked: root.tonightActive ? kcm.cancelTonight() : kcm.applyTonight()

                            QQC2.ToolTip.visible: hovered && !root.tonightActive
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

                    Kirigami.Heading {
                        Layout.bottomMargin: Kirigami.Units.smallSpacing
                        level: 3
                        text: i18nc("@title", "Software to Be Updated")
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

                            Repeater {
                                model: kcm.previewUpdates

                                delegate: RowLayout {
                                    id: updateRow

                                    required property string name
                                    required property string icon
                                    required property string sourceLabel
                                    required property string backend
                                    required property string oldVersion
                                    required property string newVersion

                                    function withoutRpmEpochAndRelease(version) {
                                        const noEpoch = version.includes(":") ? version.substring(version.indexOf(":") + 1) : version;
                                        return noEpoch.includes("-") ? noEpoch.substring(0, noEpoch.lastIndexOf("-")) : noEpoch;
                                    }
                                    function displayVersion(version) {
                                        return backend === "rpm" ? withoutRpmEpochAndRelease(version) : version;
                                    }
                                    readonly property bool systemImage: backend === "bootc" || backend === "rpm-ostree"
                                    readonly property string fromVersion: systemImage ? "" : displayVersion(oldVersion)
                                    readonly property string toVersion: displayVersion(newVersion)

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
                                            text: updateRow.fromVersion.length > 0 && updateRow.fromVersion !== updateRow.toVersion
                                                ? i18nc("@info %1 source, %2 old version, %3 new version", "%1 · %2 → %3",
                                                        updateRow.sourceLabel, updateRow.fromVersion, updateRow.toVersion)
                                                : updateRow.toVersion.length > 0
                                                    ? i18nc("@info %1 source, %2 new version", "%1 · %2", updateRow.sourceLabel, updateRow.toVersion)
                                                    : updateRow.sourceLabel
                                            font: Kirigami.Theme.smallFont
                                            opacity: 0.7
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }

                    QQC2.Label {
                        Layout.topMargin: Kirigami.Units.largeSpacing
                        Layout.bottomMargin: Kirigami.Units.smallSpacing
                        opacity: kcm.updateCount > root.shownUpdates ? 1 : 0
                        text: i18ncp("@info %1 is the number of updates not listed above", "and <b>%1</b> more update",
                                     "and <b>%1</b> more updates", Math.max(0, kcm.updateCount - root.shownUpdates))
                        textFormat: Text.StyledText
                    }

                    QQC2.ItemDelegate {
                        id: technicalButton

                        Layout.fillWidth: true
                        Layout.leftMargin: -leftPadding
                        Layout.rightMargin: -rightPadding
                        opacity: root.hasUpdates ? 1 : 0
                        enabled: root.hasUpdates && !kcm.running
                        text: i18nc("@action:button opens a list of all updates", "Technical Information")
                        onClicked: kcm.push("TechnicalInformation.qml")

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: technicalButton.text
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
                Layout.topMargin: Kirigami.Units.largeSpacing
                Layout.bottomMargin: Kirigami.Units.largeSpacing
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Kirigami.Units.gridUnit
                rowSpacing: Kirigami.Units.largeSpacing * 2

                QQC2.Label {
                    Layout.preferredWidth: root.labelColumnWidth
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: (automaticButton.implicitHeight - implicitHeight) / 2
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
                        opacity: 0.7
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
                        opacity: 0.7
                        wrapMode: Text.Wrap
                    }
                }

                QQC2.Label {
                    Layout.preferredWidth: root.labelColumnWidth
                    text: i18nc("@label", "Advanced")
                    wrapMode: Text.Wrap
                }

                QQC2.Button {
                    id: advancedButton
                    text: i18nc("@action:button", "Advanced Settings")
                    icon.name: "configure"
                    onClicked: kcm.push("AdvancedSettings.qml")
                }
            }
        }
    }
}
