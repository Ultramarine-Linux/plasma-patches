// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigami.delegates as KD
import org.kde.kcmutils as KCMUtils

KCMUtils.ScrollViewKCM {
    id: root

    title: i18nc("@title", "Tweaks")

    implicitWidth: Kirigami.Units.gridUnit * 38
    implicitHeight: Kirigami.Units.gridUnit * 30

    readonly property var knownTypes: ["toggle", "oneshot", "script"]

    function stabilityLabel(stability) {
        switch (stability) {
        case "gfl": return i18nc("@info tweak stability", "Use at your own risk");
        case "devel": return i18nc("@info tweak stability", "In development");
        case "alpha": return i18nc("@info tweak stability", "Alpha");
        case "beta": return i18nc("@info tweak stability", "Beta");
        case "":
        case "stable": return "";
        default: return stability;
        }
    }

    function requestChange(tweak, enable) {
        const risky = tweak.warning.length > 0 || (tweak.stability.length > 0 && tweak.stability !== "stable");
        if (!enable || (!risky && tweak.type === "toggle")) {
            kcm.setTweakEnabled(tweak.tweakId, enable);
            return;
        }
        confirmDialog.tweakId = tweak.tweakId;
        confirmDialog.type = tweak.type;
        confirmDialog.title = tweak.description;
        confirmDialog.subtitle = confirmDialog.message(tweak.type, tweak.stability, tweak.warning);
        confirmDialog.open();
    }

    headerPaddingEnabled: false
    header: ColumnLayout {
        spacing: 0

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            type: Kirigami.MessageType.Error
            text: kcm.errorMessage
            // not a binding, the close button assigns visible
            visible: false
            showCloseButton: true
            onTextChanged: visible = text.length > 0
            onVisibleChanged: if (!visible && parent && parent.visible) kcm.clearError()
            Component.onCompleted: visible = text.length > 0
            actions: Kirigami.Action {
                text: i18nc("@action:button", "Show Output")
                icon.name: "utilities-terminal"
                visible: kcm.output.length > 0
                onTriggered: outputDialog.open()
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            type: Kirigami.MessageType.Information
            icon.source: "view-refresh"
            text: kcm.status
            visible: kcm.running
            actions: Kirigami.Action {
                text: i18nc("@action:button", "Show Output")
                icon.name: "utilities-terminal"
                onTriggered: outputDialog.open()
            }
        }
    }

    view: ListView {
        id: list

        model: kcm.tweaks
        reuseItems: true

        section.property: "type"
        section.delegate: Kirigami.ListSectionHeader {
            required property string section

            width: ListView.view.width
            text: section === "toggle" ? i18nc("@title:group", "System Tweaks")
                : section === "oneshot" ? i18nc("@title:group", "One-Time Changes")
                : section === "script" ? i18nc("@title:group", "Maintenance Tasks")
                : i18nc("@title:group", "Other")
        }

        delegate: QQC2.ItemDelegate {
            id: delegate

            required property string tweakId
            required property string description
            required property string type
            required property string stability
            required property string warning
            required property string stateError
            required property bool tweakEnabled
            required property bool busy

            readonly property bool stateUnknown: stateError.length > 0

            width: ListView.view.width
            text: delegate.description
            Accessible.description: details.subtitle
            down: false
            hoverEnabled: false
            focusPolicy: Qt.NoFocus

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                KD.TitleSubtitle {
                    id: details

                    Layout.fillWidth: true
                    title: delegate.description
                    subtitle: delegate.stateUnknown ? i18nc("@info", "State unknown: %1", delegate.stateError)
                        : delegate.type === "oneshot" && delegate.tweakEnabled ? i18nc("@info", "Already applied")
                        : delegate.tweakId
                    elide: Text.ElideRight

                    HoverHandler {
                        id: titleHover
                    }
                    QQC2.ToolTip.visible: delegate.stateUnknown && titleHover.hovered
                    QQC2.ToolTip.text: delegate.stateError
                }

                Kirigami.Chip {
                    visible: text.length > 0
                    text: root.stabilityLabel(delegate.stability)
                    closable: false
                    checkable: false
                    icon.name: "data-warning"
                    icon.width: Kirigami.Units.iconSizes.sizeForLabels
                    icon.height: Kirigami.Units.iconSizes.sizeForLabels
                    leftPadding: rightPadding
                }

                Kirigami.ContextualHelpButton {
                    visible: delegate.warning.length > 0
                    toolTipText: delegate.warning
                }

                QQC2.Label {
                    visible: !root.knownTypes.includes(delegate.type)
                    text: i18nc("@info", "Use “um tweaks” in a terminal")
                    color: Kirigami.Theme.disabledTextColor
                }

                QQC2.BusyIndicator {
                    visible: delegate.busy
                    running: visible
                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                }

                QQC2.Switch {
                    visible: delegate.type === "toggle"
                    enabled: !kcm.running && !delegate.stateUnknown
                    checked: delegate.tweakEnabled
                    Accessible.name: delegate.description
                    onToggled: {
                        const requestedState = checked;
                        checked = Qt.binding(() => delegate.tweakEnabled);
                        root.requestChange(delegate, requestedState);
                    }
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: delegate.tweakEnabled ? i18nc("@info:tooltip", "Disable this tweak")
                                                             : i18nc("@info:tooltip", "Enable this tweak")
                }

                QQC2.Button {
                    visible: delegate.type === "oneshot"
                    enabled: !kcm.running && !delegate.tweakEnabled && !delegate.stateUnknown
                    text: delegate.tweakEnabled ? i18nc("@action:button", "Applied")
                                                : i18nc("@action:button", "Apply")
                    icon.name: delegate.tweakEnabled ? "dialog-ok-apply" : "run-build"
                    Accessible.name: delegate.tweakEnabled
                        ? i18nc("@action:button accessible name, %1 is a tweak", "“%1” applied", delegate.description)
                        : i18nc("@action:button accessible name, %1 is a tweak", "Apply “%1”", delegate.description)
                    onClicked: root.requestChange(delegate, true)
                }

                QQC2.Button {
                    visible: delegate.type === "script"
                    enabled: !kcm.running
                    text: i18nc("@action:button", "Run")
                    icon.name: "media-playback-start"
                    Accessible.name: i18nc("@action:button accessible name, %1 is a tweak", "Run “%1”", delegate.description)
                    onClicked: root.requestChange(delegate, true)
                }
            }
        }

        Kirigami.LoadingPlaceholder {
            anchors.centerIn: parent
            visible: list.count === 0 && (kcm.loading || (!kcm.loaded && !kcm.listFailed))
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: !kcm.loading && list.count === 0 && kcm.loaded && !kcm.listFailed
            icon.name: "preferences-system-ultramarine"
            text: i18nc("@info:placeholder", "No tweaks available")
            helpfulAction: Kirigami.Action {
                text: i18nc("@action:button", "Reload")
                icon.name: "view-refresh"
                onTriggered: kcm.refresh()
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: !kcm.loading && list.count === 0 && kcm.listFailed
            icon.name: "dialog-error"
            text: i18n("Could not load the list of tweaks.")
            helpfulAction: Kirigami.Action {
                text: i18nc("@action:button", "Reload")
                icon.name: "view-refresh"
                onTriggered: kcm.refresh()
            }
        }
    }

    Kirigami.PromptDialog {
        id: confirmDialog

        property string tweakId
        property string type

        function message(type, stability, warning) {
            const parts = [];
            if (stability === "gfl") {
                parts.push(i18nc("@info", "This tweak comes with no guarantees. Use it at your own risk."));
            } else if (stability === "devel") {
                parts.push(i18nc("@info", "This tweak is still in development and may be unstable."));
            } else if (stability.length > 0 && stability !== "stable") {
                parts.push(i18nc("@info", "This tweak is marked as “%1” and may be unstable.",
                                 root.stabilityLabel(stability)));
            }
            if (warning.length > 0) {
                parts.push(warning);
            }
            if (type === "oneshot") {
                parts.push(i18nc("@info", "This change cannot be undone from here."));
            } else if (type === "script") {
                parts.push(i18nc("@info", "This will run a maintenance script with administrator privileges."));
            }
            return parts.join("\n\n");
        }

        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: confirmDialog.type === "script" ? i18nc("@action:button", "Run")
                    : confirmDialog.type === "oneshot" ? i18nc("@action:button", "Apply")
                    : i18nc("@action:button", "Enable")
                icon.name: "dialog-ok"
                onTriggered: {
                    if (confirmDialog.type === "script") {
                        kcm.runTweak(confirmDialog.tweakId);
                    } else {
                        kcm.setTweakEnabled(confirmDialog.tweakId, true);
                    }
                    confirmDialog.close();
                }
            },
            Kirigami.Action {
                text: i18nc("@action:button", "Cancel")
                icon.name: "dialog-cancel"
                onTriggered: confirmDialog.close()
            }
        ]
    }

    Kirigami.Dialog {
        id: outputDialog

        title: i18nc("@title:window", "Output")
        standardButtons: Kirigami.Dialog.Close
        preferredWidth: Kirigami.Units.gridUnit * 40
        preferredHeight: Kirigami.Units.gridUnit * 25

        QQC2.ScrollView {
            QQC2.TextArea {
                readOnly: true
                text: outputDialog.visible ? kcm.output : ""
                font.family: "monospace"
                wrapMode: TextEdit.Wrap
                onTextChanged: cursorPosition = length
            }
        }
    }
}
