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

// every pending update with its full source and versions
KCMUtils.ScrollViewKCM {
    id: page

    title: i18nc("@title", "Technical Information")

    actions: Kirigami.Action {
        text: i18nc("@action:button", "Copy")
        icon.name: "edit-copy"
        onTriggered: {
            clipboardHelper.text = kcm.technicalInformation;
            clipboardHelper.selectAll();
            clipboardHelper.copy();
        }
    }

    // Qt Quick has no clipboard API, so copy through a hidden text field
    TextEdit {
        id: clipboardHelper
        visible: false
    }

    view: ListView {
        model: kcm.updates
        reuseItems: true

        section.property: "kind"
        section.delegate: Kirigami.ListSectionHeader {
            required property string section

            width: ListView.view.width
            text: section === "app" ? i18nc("@title:group", "Apps")
                : section === "addon" ? i18nc("@title:group", "Add-ons")
                : section === "runtime" ? i18nc("@title:group", "Runtimes")
                : i18nc("@title:group", "System Packages")
        }

        delegate: QQC2.ItemDelegate {
            id: delegate

            // ItemDelegate already has an "icon" property, so read the roles through model
            required property var model

            width: ListView.view.width
            // read by screen readers. The content item below shows it.
            text: delegate.model.name
            Accessible.description: details.subtitle
            // rows only show information, so they do nothing when clicked and aren't tab stops
            down: false
            hoverEnabled: false
            focusPolicy: Qt.NoFocus

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                KD.IconTitleSubtitle {
                    id: details

                    Layout.fillWidth: true
                    icon.name: delegate.model.icon
                    title: delegate.text
                    subtitle: delegate.model.oldVersion.length > 0 && delegate.model.oldVersion !== delegate.model.newVersion
                        ? i18nc("@info %1 source, %2 old version, %3 new version", "%1 · %2 → %3",
                                delegate.model.source, delegate.model.oldVersion, delegate.model.newVersion)
                        : delegate.model.newVersion.length > 0
                            ? i18nc("@info %1 source, %2 version", "%1 · %2", delegate.model.source, delegate.model.newVersion)
                            : delegate.model.source
                }

                QQC2.Label {
                    visible: delegate.model.downloadSize > 0
                    text: Qt.locale().formattedDataSize(delegate.model.downloadSize, 1, Locale.DataSizeSIFormat)
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }
    }
}
