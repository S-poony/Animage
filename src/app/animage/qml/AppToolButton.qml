// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// Platform button with native icons (icon.name) but themed background so
// dark theme does not stay white. Unchecked buttons are transparent and show
// the dark window behind them; hover/pressed use Theme.surfaceHover; checked
// and highlighted use accent. This matches the pre-native themed button but
// keeps icon.name / icon.source fallback for Adwaita/hicolor.
Button {
    id: root

    property string shortcutHint: ""
    property bool small: false
    property bool rounded: true
    property alias iconName: root.icon.name

    hoverEnabled: true

    // Square icon buttons: brush, eraser, layer add/delete/up/down, hide, play
    // and the pressure toggle are all IconOnly. Make them square so the hit
    // area matches the design tokens.
    Binding { target: root; property: "implicitWidth"; value: small ? Theme.iconButton : Theme.toolButton; when: root.display === AbstractButton.IconOnly }
    Binding { target: root; property: "implicitHeight"; value: small ? Theme.iconButton : Theme.toolButton; when: root.display === AbstractButton.IconOnly }

    // Icon sizing: small buttons need larger than Theme.iconSizeSmall (14) to avoid
    // tiny black-dot appearance at 24 px; use 16 for small, 20 for regular.
    icon.width: small ? 16 : Theme.iconSizeTool
    icon.height: small ? 16 : Theme.iconSizeTool
    // Checked state needs contrast: use accent background with on-accent icon
    palette.button: (checked && checkable) ? Theme.accent : undefined
    palette.buttonText: (checked && checkable) ? Theme.textOnAccent : undefined
    icon.color: (checked && checkable) ? Theme.textOnAccent : undefined

    background: Rectangle {
        radius: root.rounded ? Theme.radiusMedium : Theme.radiusSmall
        color: {
            if (!root.enabled) return "transparent"
            if (root.highlighted) return Theme.accent
            if (root.checked && root.checkable) return Theme.accentSoft
            if (root.down) return Theme.surfaceHover
            if (root.hovered) return Theme.surfaceHover
            return "transparent"
        }
        border.width: root.highlighted ? 1 : (root.checked && root.checkable ? 1 : 0)
        border.color: root.highlighted ? Theme.accentHover : (root.checked && root.checkable ? Theme.accentBorder : "transparent")
    }

    ToolTip.delay: 400
    ToolTip.visible: hovered && ToolTip.text !== ""
    ToolTip.text: root.shortcutHint !== ""
                     ? (root.text !== "" ? root.text + " (" + root.shortcutHint + ")" : root.shortcutHint)
                     : root.text
}
