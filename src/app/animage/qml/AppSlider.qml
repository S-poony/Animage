// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// The slider the interface uses. Slider for behaviour (drag, keyboard, touch),
// our tokens for looks: a hairline groove with an accent fill, and a round
// handle that reads against the dark surface.
Slider {
    id: root

    implicitHeight: 22
    from: 0
    to: 100

    background: Rectangle {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: 4
        radius: 2
        color: Theme.borderStrong
        border.width: 0

        // The filled part: how much of the range has been used.
        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: 14
        height: 14
        radius: 7
        color: root.pressed ? Theme.accentHover : Theme.accent
        border.width: 0
        scale: root.pressed ? 1.15 : (root.hovered ? 1.08 : 1.0)

        Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
    }
}
