// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Animage

// The main button the interface uses. Icon and optional label, with hover,
// pressed, checked and highlighted states, and automatic width calculation.
// The label, when there is one, sits on the button's left padding and the icon
// leads it, so the row reads as one unit instead of an icon floating in a
// button. `highlighted` is implemented here (the Basic style ignores it) as
// the accent fill: it is the "this is the primary action" look.
//
// Sizes come from the theme, never from the caller: an icon-only button is
// Theme.toolButton square with a Theme.iconSizeTool icon, a text button is
// Theme.controlHeight tall with a Theme.iconSize icon. The compact
// `small: true` form is for rows that want a 24px square.
Button {
    id: root

    property string iconName: ""
    property color iconColor: Theme.text
    property string shortcutHint: ""
    property bool rounded: true
    property bool small: false

    // The horizontal padding of the label, and the gap between icon and label.
    readonly property int labelPadding: Theme.spaceM
    readonly property int iconGap: Theme.spaceS

    implicitWidth: {
        if (root.text !== "")
            return contentRow.implicitWidth + leftPadding + rightPadding
        return root.small ? Theme.iconButton : Theme.toolButton
    }
    implicitHeight: {
        if (root.text !== "") return Theme.controlHeight
        return root.small ? Theme.iconButton : Theme.toolButton
    }

    // Real box-model padding — the content knows its insets, so
    // implicitWidth/Height are content + padding, not simulation.
    padding: root.text !== "" ? Theme.spaceS : 4
    leftPadding: root.text !== "" ? Theme.spaceM : padding
    rightPadding: root.text !== "" ? Theme.spaceM : padding
    topPadding: padding
    bottomPadding: padding

    checkable: false
    hoverEnabled: true

    background: Rectangle {
        radius: root.rounded ? Theme.radiusMedium : Theme.radiusSmall
        color: {
            if (!root.enabled) return "transparent";
            if (root.highlighted) return Theme.accent;
            if (root.checked) return Theme.accentSoft;
            if (root.down) return Theme.surfaceHover;
            if (root.hovered) return Theme.surfaceHover;
            return "transparent";
        }
        border.width: root.highlighted ? 1 : (root.checked ? 1 : 0)
        border.color: root.highlighted ? Theme.accentHover : (root.checked ? Theme.accentBorder : "transparent")
    }

    contentItem: RowLayout {
        id: contentRow
        spacing: root.iconGap

        Icon {
            id: icon
            Layout.alignment: Qt.AlignCenter
            size: root.text !== ""
                  ? Theme.iconSize
                  : (root.small ? Theme.iconSizeSmall : Theme.iconSizeTool)
            name: root.iconName
            color: root.enabled
                   ? (root.highlighted ? Theme.textOnAccent
                      : (root.checked ? Theme.accent : root.iconColor))
                   : Theme.textDisabled
            active: root.enabled
            visible: root.iconName !== ""
        }

        Text {
            Layout.alignment: Qt.AlignCenter
            text: root.text
            visible: root.text !== ""
            color: root.enabled
                   ? (root.highlighted ? Theme.textOnAccent
                      : (root.checked ? Theme.accent : Theme.text))
                   : Theme.textDisabled
            font.pixelSize: Theme.fontM
        }
    }

    ToolTip.delay: 400
    ToolTip.visible: hovered && ToolTip.text !== ""
    ToolTip.text: root.shortcutHint !== ""
                     ? (root.text !== "" ? root.text + " (" + root.shortcutHint + ")" : root.shortcutHint)
                     : root.text
}
