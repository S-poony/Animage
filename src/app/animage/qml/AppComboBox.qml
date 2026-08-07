// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// The dropdown the interface uses. ComboBox for behaviour, our tokens for
// looks: the closed control and the open popup are both dark, so the menu that
// drops out of the Onion skin control does not arrive as a light OS widget.
// Callers keep the whole ComboBox API (model, currentIndex, onActivated, ...).
ComboBox {
    id: root

    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontM

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceHigh
        border.width: 1
        border.color: root.activeFocus ? Theme.accentFocus
                    : root.hovered ? Theme.borderStrong : Theme.border
    }

    contentItem: Text {
        leftPadding: Theme.spaceM
        rightPadding: root.indicator.width + root.spacing
        text: root.displayText
        color: root.enabled ? Theme.text : Theme.textDisabled
        font: root.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Item {
        x: root.width - width - Theme.spaceM
        y: root.topPadding + (root.availableHeight - height) / 2
        width: 12
        height: 12

        Text {
            anchors.centerIn: parent
            text: "\u25be"
            color: root.enabled ? Theme.textSecondary : Theme.textDisabled
            font.pixelSize: Theme.fontS
        }
    }

    // The dark popup: the same surfaces as the panel that opened it.
    popup: Popup {
        y: root.height + 2
        width: root.width
        implicitHeight: contentItem.implicitHeight
        padding: Theme.spaceXS

        background: Rectangle {
            radius: Theme.radiusMedium
            color: Theme.surfaceHigh
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }

    delegate: ItemDelegate {
        width: root.popup.availableWidth
        implicitHeight: Theme.controlHeight
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            leftPadding: Theme.spaceM
            rightPadding: Theme.spaceM
            text: root.textRole ? model[root.textRole] : modelData
            color: root.highlightedIndex === index ? Theme.text : Theme.textSecondary
            font: root.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: Theme.radiusSmall
            color: root.highlightedIndex === index ? Theme.accentSoft : "transparent"
        }
    }
}
