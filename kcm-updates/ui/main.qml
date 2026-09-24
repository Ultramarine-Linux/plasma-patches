import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCMUtils

KCMUtils.ScrollViewKCM {
    id: root

    property bool automaticUpdates: true

    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Kirigami.Units.largeSpacing
        anchors.bottomMargin: Kirigami.Units.largeSpacing
        width: Math.min(parent.width - Kirigami.Units.largeSpacing * 4, Kirigami.Units.gridUnit * 54)
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            text: kcm.errorMessage
            visible: text.length > 0
        }

        Kirigami.Heading {
            text: i18n("Updates")
            level: 1
        }

        RowLayout {
            id: updateSummary
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Kirigami.Units.largeSpacing * 2

            ColumnLayout {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 17
                Layout.minimumWidth: Kirigami.Units.gridUnit * 17
                Layout.alignment: Qt.AlignTop
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    spacing: Kirigami.Units.largeSpacing

                    Rectangle {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                        radius: width / 2
                        color: Kirigami.Theme.alternateBackgroundColor

                        Controls.Label {
                            anchors.centerIn: parent
                            text: kcm.distributionVersion
                            font.bold: true
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.25
                            color: Kirigami.Theme.highlightColor
                        }
                    }

                    ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Controls.Label {
                            text: i18n("Regular Updates")
                            font.bold: true
                        }

                        Controls.Label {
                            text: i18n("%1 updates available", kcm.updateCount)
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }
                }

                Controls.Label {
                    text: i18n("Occasional updates for your system and apps to keep things running smoothly and secure.")
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 25
                Layout.alignment: Qt.AlignTop
                spacing: Kirigami.Units.smallSpacing

                Controls.Label {
                    text: i18n("Software to Be Updated")
                    font.bold: true
                }

                Repeater {
                    model: kcm.updates

                    delegate: RowLayout {
                        required property int index
                        required property string name
                        required property string source
                        required property string oldVersion
                        required property string newVersion
                        required property string icon

                        visible: index < 5
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: icon
                            Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                            Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Controls.Label {
                                text: name
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Controls.Label {
                                text: source + " · " + oldVersion + " → " + newVersion
                                color: Kirigami.Theme.disabledTextColor
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                Controls.Label {
                    visible: kcm.updateCount > 5
                    text: i18n("and %1 More", Math.max(0, kcm.updateCount - 5))
                    color: Kirigami.Theme.linkColor
                }

                Controls.Button {
                    text: i18n("Technical Information")
                    flat: true
                    enabled: kcm.technicalInformation.length > 0
                    onClicked: detailsDialog.open()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignRight
            spacing: Kirigami.Units.smallSpacing

            Item { Layout.fillWidth: true }

            Controls.Button {
                text: i18n("Apply")
                icon.name: "download"
                enabled: !kcm.running && kcm.updateCount > 0
                onClicked: kcm.applyUpdates()
            }

            Controls.Button {
                text: i18n("Apply Tonight")
                icon.name: "weather-clear-night"
                enabled: !kcm.running && kcm.updateCount > 0 && kcm.offlineUpdatesEnabled
                onClicked: kcm.applyOfflineUpdates()
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 13
                Layout.minimumWidth: Kirigami.Units.gridUnit * 13
                Layout.alignment: Qt.AlignTop

                Controls.Label {
                    text: i18n("Automatic Updates")
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Controls.RadioButton {
                    text: i18n("Let your device decide")
                    checked: root.automaticUpdates
                    onClicked: root.automaticUpdates = true
                }

                Controls.Label {
                    text: i18n("Updates automatically install when you’re not active")
                    leftPadding: Kirigami.Units.gridUnit * 2
                    color: Kirigami.Theme.disabledTextColor
                }

                Controls.RadioButton {
                    text: i18n("Ask me")
                    checked: !root.automaticUpdates
                    onClicked: root.automaticUpdates = false
                }

                Controls.Label {
                    text: i18n("You’re notified when updates are ready to go")
                    leftPadding: Kirigami.Units.gridUnit * 2
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Controls.Label {
                text: i18n("Advanced update settings")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 13
            }

            Controls.Button {
                id: advancedButton
                text: i18n("Advanced Settings")
                icon.name: "settings-configure"
                onClicked: advancedMenu.popup(advancedButton, Qt.point(0, advancedButton.height))
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Item { Layout.preferredWidth: Kirigami.Units.gridUnit * 13 }

            Controls.Button {
                text: i18n("Check for Updates")
                icon.name: "view-refresh"
                enabled: !kcm.running
                onClicked: kcm.refresh()
            }
        }
    }

    Controls.Menu {
        id: advancedMenu

        Controls.MenuItem {
            text: i18n("Allow offline updates")
            checkable: true
            checked: kcm.offlineUpdatesEnabled
            onTriggered: kcm.offlineUpdatesEnabled = checked
        }
    }

    Controls.Dialog {
        id: detailsDialog
        title: i18n("Technical Information")
        modal: true
        standardButtons: Controls.Dialog.Close
        width: Math.min(root.width - Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 42)

        Controls.ScrollView {
            anchors.fill: parent

            Controls.TextArea {
                readOnly: true
                text: kcm.technicalInformation
                wrapMode: TextEdit.Wrap
                selectByMouse: true
            }
        }
    }
}
