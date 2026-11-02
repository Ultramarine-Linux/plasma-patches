// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCMUtils
import org.kde.kirigamiaddons.formcard as FormCard

KCMUtils.SimpleKCM {
    id: root

    title: i18nc("@title", "Location Services")

    implicitWidth: Kirigami.Units.gridUnit * 32
    implicitHeight: Kirigami.Units.gridUnit * 30

    // the window shade behind the cards, like a FormCard.FormCardPage
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    headerPaddingEnabled: false
    header: Kirigami.InlineMessage {
        position: Kirigami.InlineMessage.Position.Header
        type: Kirigami.MessageType.Error
        text: kcm.errorMessage
        // not a binding: the close button sets visible = false, which would break it
        visible: false
        showCloseButton: true
        onTextChanged: visible = text.length > 0
        // only when the banner itself was closed, not when the page around it is hidden
        onVisibleChanged: if (!visible && parent && parent.visible) kcm.clearError()
        Component.onCompleted: visible = text.length > 0
    }

    ColumnLayout {
        visible: kcm.available
        spacing: 0

        // the big icon and a line on what this is for, like the top of a Plasma welcome page
        ColumnLayout {
            Layout.fillWidth: true
            // its width is capped by the heading's, so center the column itself too
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            Layout.bottomMargin: Kirigami.Units.gridUnit
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Kirigami.Units.iconSizes.huge
                implicitHeight: Kirigami.Units.iconSizes.huge
                source: "mark-location"
                opacity: kcm.locationEnabled ? 1 : 0.5

                Behavior on opacity {
                    NumberAnimation { duration: Kirigami.Units.shortDuration }
                }
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                Layout.alignment: Qt.AlignHCenter
                level: 2
                text: i18nc("@info", "Let apps know roughly where you are")
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }

        FormCard.FormCard {
            FormCard.FormSwitchDelegate {
                id: toggle

                // as the only row in its card, the hover tint lights up the whole card, which reads as
                // a different card rather than feedback. Pressing and keyboard focus still show.
                hoverEnabled: false
                icon.name: "find-location"
                text: i18nc("@option:check", "Location Services")
                description: kcm.busy ? (kcm.locationEnabled ? i18nc("@info", "Turning off…") : i18nc("@info", "Turning on…"))
                    : i18nc("@info", "Allow apps to request your approximate location")
                enabled: !kcm.busy && !kcm.loading
                checked: kcm.locationEnabled
                onToggled: {
                    const wanted = checked;
                    // keep showing the real state until systemd reports the change
                    checked = Qt.binding(() => kcm.locationEnabled);
                    kcm.setLocationEnabled(wanted);
                }
            }
        }

        FormCard.FormHeader {
            // the default sits right on top of the card above, so give the section room to breathe
            topPadding: Kirigami.Units.gridUnit
            bottomPadding: Kirigami.Units.largeSpacing
            title: i18nc("@title:group", "Privacy")
        }

        FormCard.FormCard {
            Layout.bottomMargin: Kirigami.Units.gridUnit

            FormCard.FormTextDelegate {
                icon.name: "network-wireless"
                text: i18nc("@info", "How?")
                description: i18nc("@info", "Nearby Wi-Fi networks are searched on beaconDB to find your location")
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormTextDelegate {
                icon.name: "mark-location"
                text: i18nc("@info", "What?")
                description: i18nc("@info", "Your approximate coordinates and how accurate they are")
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormTextDelegate {
                icon.name: "security-high"
                text: i18nc("@info", "Who?")
                description: i18nc("@info", "Only the apps that ask for it, like a website you visit")
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormTextDelegate {
                icon.name: "system-users"
                text: i18nc("@info", "Where?")
                description: i18nc("@info", "Your entire system")
            }
        }
    }

    Kirigami.LoadingPlaceholder {
        anchors.centerIn: parent
        visible: kcm.loading && !kcm.available
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: !kcm.loading && !kcm.available
        icon.name: "mark-location"
        text: i18nc("@info:placeholder", "Location services aren't installed")
        explanation: i18nc("@info:placeholder", "Install the geoclue2 package to let apps request your location.")
    }
}
