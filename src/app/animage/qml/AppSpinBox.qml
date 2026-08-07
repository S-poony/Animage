// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// The number field the interface uses. SpinBox for behaviour, our tokens for
// looks: a dark field with the theme's borders, an accent focus ring, and the
// step buttons painted from the theme instead of whatever the platform style
// would put there. Callers keep the whole SpinBox API.
SpinBox {
    id: root

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(84, Theme.inputMinWidth)
    editable: true
    focusPolicy: Qt.ClickFocus
    font.pixelSize: Theme.fontM

    // Leave room for the two steppers so the number never sits under them.
    // Without this the centred TextInput overlaps the indicators at narrow
    // widths (the fps field at 64 px was clipping "28" into the chevrons).
    leftPadding: Theme.spaceS
    rightPadding: Math.round(root.implicitHeight * 0.72 * 2) + Theme.spaceS
    topPadding: 0
    bottomPadding: 0

    contentItem: TextInput {
        text: root.textFromValue(root.value, root.locale)
        font: root.font
        color: root.enabled ? Theme.text : Theme.textDisabled
        selectionColor: Theme.accent
        selectedTextColor: Theme.textOnAccent
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        readOnly: !root.editable
        validator: root.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        clip: true
        onEditingFinished: {
            root.value = root.valueFromText(text, root.locale)
        }
    }

    // The step buttons: two narrow squares tucked against the right edge,
    // inside rightPadding so contentItem never overlaps them.
    down.indicator: Rectangle {
        x: root.mirrored ? root.leftPadding : root.width - width * 2 - Theme.spaceXS
        y: root.topPadding
        width: Math.round(root.implicitHeight * 0.72)
        height: root.availableHeight
        radius: Theme.radiusSmall
        color: root.down.pressed ? Theme.surfaceHover
             : root.down.hovered ? Theme.surfaceHover : "transparent"

        Text {
            anchors.centerIn: parent
            text: "\u25be"
            color: root.enabled ? Theme.textSecondary : Theme.textDisabled
            font.pixelSize: Theme.fontS
        }
    }

    up.indicator: Rectangle {
        x: root.mirrored ? root.leftPadding + width : root.width - width - Theme.spaceXS
        y: root.topPadding
        width: Math.round(root.implicitHeight * 0.72)
        height: root.availableHeight
        radius: Theme.radiusSmall
        color: root.up.pressed ? Theme.surfaceHover
             : root.up.hovered ? Theme.surfaceHover : "transparent"

        Text {
            anchors.centerIn: parent
            text: "\u25b4"
            color: root.enabled ? Theme.textSecondary : Theme.textDisabled
            font.pixelSize: Theme.fontS
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceHigh
        border.width: 1
        border.color: root.activeFocus ? Theme.accentFocus
                    : root.hovered ? Theme.borderStrong : Theme.border
    }
}
