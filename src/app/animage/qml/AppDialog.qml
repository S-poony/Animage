// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// The base every custom dialog in the application uses: modal, centred over
// the whole window, themed padding, and a width that fits a small window. The
// dialog supplies its own contentItem and its own action buttons (AppDialog
// declares no standard buttons, so a dialog that forgets them is visibly
// missing them rather than silently inheriting a light platform footer); this
// file supplies the placement, the frame and the sizing.
Dialog {
    id: root

    parent: Overlay.overlay
    modal: true
    padding: Theme.spaceL
    standardButtons: Dialog.NoButton

    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(440, (parent ? parent.width : 640) - Theme.spaceXL * 2)

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surfaceHigh
        border.width: 1
        border.color: Theme.border
    }

    // The header text follows the theme rather than the platform style.
    header: Item {
        width: root.availableWidth
        height: Theme.controlHeight + Theme.spaceS

        Text {
            id: headerText
            anchors.fill: parent
            anchors.topMargin: Theme.spaceM
            anchors.leftMargin: root.padding
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontL
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
    }
}
