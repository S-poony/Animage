// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// A checkbox that follows the design tokens: an accent tick on a dark surface,
// with a label that can sit beside it. Used for layer visibility, show-marks
// and the colour-layer switches.
CheckBox {
    id: root

    property string labelText: ""

    implicitHeight: 22
    spacing: Theme.spaceS

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: root.leftPadding
        y: (parent.height - height) / 2
        radius: Theme.radiusSmall
        color: root.checked ? Theme.accentSoft : (root.hovered ? Theme.surfaceHigh : Theme.surface)
        border.width: 1
        border.color: root.checked ? Theme.accentBorder : (root.hovered ? Theme.borderStrong : Theme.border)

        Rectangle {
            visible: root.checked
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 2
            color: Theme.accent
        }
    }

    contentItem: Text {
        text: root.labelText
        visible: root.labelText !== ""
        color: root.enabled ? Theme.textSecondary : Theme.textDisabled
        font.pixelSize: Theme.fontM
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
    }
}
