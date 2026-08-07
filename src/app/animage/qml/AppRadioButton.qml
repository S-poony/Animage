// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// The option button for mutually exclusive choices (the colourize layer's
// Result: Filled result / Colour hints). RadioButton for behaviour, our tokens
// for looks, matching AppCheckBox.
RadioButton {
    id: root

    implicitHeight: 22
    spacing: Theme.spaceS
    font.pixelSize: Theme.fontM

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: root.leftPadding
        y: (parent.height - height) / 2
        radius: 8
        color: root.checked ? Theme.accentSoft : (root.hovered ? Theme.surfaceHigh : Theme.surface)
        border.width: 1
        border.color: root.checked ? Theme.accentBorder : (root.hovered ? Theme.borderStrong : Theme.border)

        Rectangle {
            visible: root.checked
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            color: Theme.accent
        }
    }

    contentItem: Text {
        text: root.text
        color: root.enabled ? Theme.text : Theme.textDisabled
        font: root.font
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
    }
}
