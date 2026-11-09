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
        // levels added to um later still get a label, just untranslated
        default: return stability;
        }
    }

    // ask before anything that cannot be cleanly undone or that um flags as risky
    function request(tweakId, description, type, stability, warning, enable) {
        const risky = warning.length > 0 || (stability.length > 0 && stability !== "stable");
        if (enable && (risky || type !== "toggle")) {
            confirmDialog.tweakId = tweakId;
            confirmDialog.type = type;
            confirmDialog.title = description;
            confirmDialog.subtitle = confirmDialog.message(type, stability, warning);
            confirmDialog.open();
            return;
        }
        kcm.setTweakEnabled(tweakId, enable);
    }

    headerPaddingEnabled: false
    header: ColumnLayout {
        spacing: 0

        Kirigami.InlineMessage {
            Layout.fillWidth: true
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
            // no Cancel: tweaks run as root, where this KCM can't stop them
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

            // um couldn't tell whether it's on, so its state isn't shown or changed from here
            readonly property bool stateUnknown: stateError.length > 0

            width: ListView.view.width
            // read by screen readers. The content item below shows it.
            text: delegate.description
            Accessible.description: details.subtitle
            // toggling happens through the controls only. The row itself is not a button or a tab stop.
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
                    // the error is often longer than the row
                    QQC2.ToolTip.visible: delegate.stateUnknown && titleHover.hovered
                    QQC2.ToolTip.text: delegate.stateError
                }

                Kirigami.Chip {
                    visible: text.length > 0
                    text: root.stabilityLabel(delegate.stability)
                    closable: false
                    checkable: false
                    icon.name: "data-warning"
                }

                Kirigami.ContextualHelpButton {
                    visible: delegate.warning.length > 0
                    toolTipText: delegate.warning
                }

                // a tweak type added to um after this KCM was written: list it, but don't guess how to apply it
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
                        const wanted = checked;
                        // keep showing the real state until um reports the change succeeded
                        checked = Qt.binding(() => delegate.tweakEnabled);
                        root.request(delegate.tweakId, delegate.description, delegate.type,
                                     delegate.stability, delegate.warning, wanted);
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
                    onClicked: root.request(delegate.tweakId, delegate.description, delegate.type,
                                            delegate.stability, delegate.warning, true)
                }

                QQC2.Button {
                    visible: delegate.type === "script"
                    enabled: !kcm.running
                    text: i18nc("@action:button", "Run")
                    icon.name: "media-playback-start"
                    Accessible.name: i18nc("@action:button accessible name, %1 is a tweak", "Run “%1”", delegate.description)
                    onClicked: root.request(delegate.tweakId, delegate.description, delegate.type,
                                            delegate.stability, delegate.warning, true)
                }
            }
        }

        // also before the first load, when there's nothing to show yet
        Kirigami.LoadingPlaceholder {
            anchors.centerIn: parent
            visible: list.count === 0 && (kcm.loading || (!kcm.loaded && !kcm.listFailed))
        }

        // only once um answered, not before the first load or when loading failed
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

        // the banner says why, and stays reachable here once it's dismissed
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
            let parts = [];
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
                // only while it's shown, so the output isn't laid out again on every update while it's closed
                text: outputDialog.visible ? kcm.output : ""
                font.family: "monospace"
                wrapMode: TextEdit.Wrap
                // follow the output while an operation is running
                onTextChanged: cursorPosition = length
            }
        }
    }
}
