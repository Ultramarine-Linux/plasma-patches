// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

import org.kde.kirigami as Kirigami

Kirigami.InlineMessage {
    position: Kirigami.InlineMessage.Position.Header
    type: Kirigami.MessageType.Error
    text: kcm.errorMessage
    // not a binding: the close button sets visible = false, which would break it
    visible: false
    showCloseButton: true
    onTextChanged: visible = text.length > 0
    readonly property bool pageShown: parent !== null && parent.visible
    onVisibleChanged: if (!visible && pageShown) kcm.clearError()
    Component.onCompleted: visible = text.length > 0
    actions: Kirigami.Action {
        visible: kcm.errorRetryable
        text: i18nc("@action:button", "Try Again")
        icon.name: "view-refresh"
        enabled: !kcm.running
        onTriggered: kcm.refresh()
    }
}
