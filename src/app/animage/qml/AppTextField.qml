// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// The text field the interface uses (double-click a layer name to rename it).
// TextField for behaviour, our tokens for looks: dark surface, theme borders,
// accent ring while editing.
TextField {
    id: root

    implicitHeight: Theme.controlHeight - 6
    font.pixelSize: Theme.fontM
    color: Theme.text
    selectedTextColor: Theme.textOnAccent
    selectionColor: Theme.accent
    selectByMouse: true

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceHigh
        border.width: 1
        border.color: root.activeFocus ? Theme.accentFocus
                    : root.hovered ? Theme.borderStrong : Theme.border
    }
}
