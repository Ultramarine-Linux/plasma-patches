// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

import org.kde.kirigami as Kirigami

// the error banner at the top of every page of this module
Kirigami.InlineMessage {
    position: Kirigami.InlineMessage.Position.Header
    type: Kirigami.MessageType.Error
    text: kcm.errorMessage
    // not a binding: the close button sets visible = false, which would break it
    visible: false
    // closing the banner dismisses the error, for errors that checking again doesn't fix too
    showCloseButton: true
    onTextChanged: visible = text.length > 0
    // only when the banner itself was closed, not when the page around it is hidden
    onVisibleChanged: if (!visible && parent && parent.visible) kcm.clearError()
    Component.onCompleted: visible = text.length > 0
    // only failed checks can be retried by checking again. A failed save stays
    // pending, so Apply saves it again.
    actions: Kirigami.Action {
        visible: kcm.errorRetryable
        text: i18nc("@action:button", "Try Again")
        icon.name: "view-refresh"
        enabled: !kcm.running
        onTriggered: kcm.refresh()
    }
}
