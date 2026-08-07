// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Animage

// The contextual options strip directly above the canvas: tool settings with
// consistent alignment, clean typography, quiet dividers, and a compact
// brush-size popover that puts primary control on the logarithmic slider.
Item {
    id: root
    objectName: "toolOptionsBar"

    property var controller: null

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spaceM
        anchors.rightMargin: Theme.spaceM
        spacing: Theme.spaceM

        // --- Colourize layer explanation ------------------------------------
        RowLayout {
            visible: controller && controller.onColourLayer
            spacing: Theme.spaceS
            Icon { size: Theme.iconSize; name: "palette"; color: Theme.flag }
            Text {
                text: "Colourize — scribble hints fill inside ticked line layers (see Inspector)."
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
                verticalAlignment: Text.AlignVCenter
            }
        }

        // --- Brush and Eraser tool settings ---------------------------------
        RowLayout {
            visible: !(controller && controller.onColourLayer)
            spacing: Theme.spaceM
            Layout.alignment: Qt.AlignVCenter

            // Active tool name
            Text {
                text: controller ? (controller.tool === AppController.Eraser ? "Eraser" : "Brush") : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
                font.weight: Font.DemiBold
                Layout.preferredWidth: 48
                Layout.alignment: Qt.AlignVCenter
            }

            // Divider
            Rectangle {
                width: 1
                height: 18
                color: Theme.border
                Layout.alignment: Qt.AlignVCenter
            }

            // Size label + trigger control
            RowLayout {
                spacing: Theme.spaceS
                Layout.alignment: Qt.AlignVCenter

                Text {
                    text: "Size"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    Layout.alignment: Qt.AlignVCenter
                }

                Rectangle {
                    id: sizeButton
                    objectName: "sizeTrigger"
                    height: 28
                    implicitWidth: sizeRow.implicitWidth + Theme.spaceM * 2
                    radius: Theme.radiusSmall
                    color: sizeMouse.containsMouse ? Theme.surfaceHover : Theme.surfaceHigh
                    border.width: 1
                    border.color: sizePopup.opened ? Theme.accentBorder : (sizeMouse.containsMouse ? Theme.borderStrong : Theme.border)
                    Layout.alignment: Qt.AlignVCenter

                    RowLayout {
                        id: sizeRow
                        anchors.centerIn: parent
                        spacing: Theme.spaceS

                        // Preview dot: bounded and normalized
                        Rectangle {
                            readonly property real radiusVal: controller ? controller.brushRadius : 6
                            readonly property real dotDiameter: Math.min(16, Math.max(6, 6 + 10 * (Math.log2(Math.max(1, radiusVal)) / 9)))
                            width: dotDiameter
                            height: dotDiameter
                            radius: dotDiameter / 2
                            color: Theme.text
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Text {
                            text: (controller ? Math.round(controller.brushRadius) : 6) + " px"
                            color: Theme.text
                            font.pixelSize: Theme.fontM
                            font.weight: Font.DemiBold
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Text {
                            text: "▾"
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontS
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    MouseArea {
                        id: sizeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sizePopup.open()
                        ToolTip.visible: containsMouse && !sizePopup.opened
                        ToolTip.text: "Brush size — click to adjust ([ and ] nudge, Alt+right-drag)"
                    }

                    // --- Compact Brush Size Popover -------------------------
                    Popup {
                        id: sizePopup
                        objectName: "brushSizePopup"
                        x: 0
                        y: parent.height + 4
                        width: 290
                        padding: Theme.spaceM
                        modal: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                        background: Item {
                            // Caret arrow pointing up at the trigger
                            Rectangle {
                                x: Math.min(parent.width - 24, Math.max(12, sizeButton.width / 2 - 5))
                                y: -5
                                width: 10
                                height: 10
                                rotation: 45
                                color: Theme.surfaceHigh
                                border.width: 1
                                border.color: Theme.border
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusMedium
                                color: Theme.surfaceHigh
                                border.width: 1
                                border.color: Theme.border
                            }
                        }

                        contentItem: ColumnLayout {
                            spacing: Theme.spaceS

                            // Row 1: Header + Bounded Normalized Preview
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32

                                Text {
                                    text: "Brush size"
                                    color: Theme.text
                                    font.pixelSize: Theme.fontM
                                    font.weight: Font.DemiBold
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Item { Layout.fillWidth: true }

                                // Normalized, capped preview circle
                                Rectangle {
                                    readonly property real rVal: controller ? controller.brushRadius : 6
                                    readonly property real previewDiam: Math.min(32, Math.max(8, Math.round(8 + 24 * (Math.log2(Math.max(1, rVal)) / 9))))
                                    width: previewDiam
                                    height: previewDiam
                                    radius: previewDiam / 2
                                    color: Theme.text
                                    border.width: 1
                                    border.color: Theme.borderStrong
                                    Layout.alignment: Qt.AlignVCenter
                                }
                            }

                            // Row 2: Logarithmic Slider + Editable Value
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceS

                                AppSlider {
                                    id: sizeSlider
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 9
                                    value: controller ? Math.min(9, Math.max(0, Math.log2(Math.max(1, controller.brushRadius)))) : 2.58
                                    onMoved: {
                                        const px = Math.round(Math.pow(2, value))
                                        controller.setBrushRadius(px)
                                    }
                                }

                                // Clean value display: click to type exact number
                                Item {
                                    id: valueBox
                                    objectName: "sizeValueEditor"
                                    Layout.preferredWidth: 54
                                    Layout.preferredHeight: 24
                                    property bool editing: false

                                    Text {
                                        id: valueDisplay
                                        visible: !valueBox.editing
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: (controller ? Math.round(controller.brushRadius) : 6) + " px"
                                        color: Theme.text
                                        font.pixelSize: Theme.fontM
                                        font.weight: Font.DemiBold
                                    }

                                    MouseArea {
                                        visible: !valueBox.editing
                                        anchors.fill: parent
                                        cursorShape: Qt.IBeamCursor
                                        onClicked: {
                                            valueBox.editing = true
                                            valueInput.forceActiveFocus()
                                            valueInput.selectAll()
                                        }
                                        ToolTip.visible: containsMouse && !valueBox.editing
                                        ToolTip.text: "Click to enter exact size"
                                    }

                                    TextInput {
                                        id: valueInput
                                        objectName: "sizeValueField"
                                        visible: valueBox.editing
                                        anchors.fill: parent
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                        text: controller ? Math.round(controller.brushRadius) : "6"
                                        color: Theme.text
                                        font.pixelSize: Theme.fontM
                                        font.weight: Font.DemiBold
                                        selectByMouse: true
                                        validator: IntValidator { bottom: 1; top: 512 }

                                        function commit() {
                                            if (text !== "" && acceptableInput) {
                                                const val = parseInt(text, 10)
                                                if (!isNaN(val) && val >= 1) controller.setBrushRadius(val)
                                            }
                                            valueBox.editing = false
                                        }

                                        onEditingFinished: commit()
                                        onActiveFocusChanged: {
                                            if (!activeFocus && valueBox.editing) commit()
                                        }
                                    }
                                }
                            }

                            // Row 3: Preset chips
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                spacing: Theme.spaceXS

                                Repeater {
                                    model: [4, 8, 16, 32, 64]
                                    delegate: Rectangle {
                                        id: chip
                                        required property int modelData
                                        readonly property bool isSelected: controller && Math.round(controller.brushRadius) === modelData

                                        Layout.preferredWidth: 36
                                        Layout.preferredHeight: 24
                                        radius: Theme.radiusSmall
                                        color: isSelected ? Theme.accentSoft : (chipMouse.containsMouse ? Theme.surfaceHover : "transparent")
                                        border.width: isSelected ? 1 : 0
                                        border.color: isSelected ? Theme.accentBorder : "transparent"

                                        Text {
                                            anchors.centerIn: parent
                                            text: chip.modelData
                                            color: chip.isSelected ? Theme.accent : (chipMouse.containsMouse ? Theme.text : Theme.textSecondary)
                                            font.pixelSize: Theme.fontM
                                            font.weight: chip.isSelected ? Font.DemiBold : Font.Normal
                                        }

                                        MouseArea {
                                            id: chipMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: controller.setBrushRadius(chip.modelData)
                                        }
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }

            // Divider
            Rectangle {
                width: 1
                height: 18
                color: Theme.border
                Layout.alignment: Qt.AlignVCenter
            }

            // Pressure -> opacity checkbox
            AppCheckBox {
                labelText: "Pressure → opacity"
                checked: controller ? controller.pressureOpacity : true
                onToggled: controller.setPressureOpacity(checked)
                Layout.alignment: Qt.AlignVCenter
                ToolTip.visible: hovered
                ToolTip.text: "Press harder for more opaque marks"
            }

            Item { Layout.fillWidth: true }
        }

        Item { Layout.fillWidth: true }
    }

    // Keep slider in sync with controller brush radius changes
    Connections {
        target: controller
        function onBrushChanged() {
            if (!controller) return
            const r = controller.brushRadius
            const logVal = Math.min(9, Math.max(0, Math.log2(Math.max(1, r))))
            sizeSlider.value = logVal
        }
    }
}
