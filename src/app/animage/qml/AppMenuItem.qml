// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// One item of an AppMenu: dark text on a transparent row, with the accent
// highlight while the pointer is over it. MenuItem for behaviour (keyboard
// navigation, trigger on release), our tokens for looks.
MenuItem {
    id: root

    implicitHeight: Theme.controlHeight

    contentItem: Text {
        text: root.text
        leftPadding: Theme.spaceM
        rightPadding: Theme.spaceM
        color: root.highlighted ? Theme.text : Theme.textSecondary
        font.pixelSize: Theme.fontM
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: root.highlighted ? Theme.accentSoft : "transparent"
    }
}
