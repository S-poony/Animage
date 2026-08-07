// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Animage

// The unified temporal strip: onion skin and exposure as one continuous widget
// centred on the current drawing -- past frames to the left of the exposure
// block, future frames to the right. It maps straight onto the animator's
// mental model, `past frames ← CURRENT DRAWING → future frames`, rather than
// presenting two unrelated spinboxes.
//
// The middle block is the Exposure. Click to type an exact frame count, wheel
// to change it, or drag its right edge to lengthen/shorten it the same way a
// drawing's hold is dragged in the timeline. The dots either side are Onion
// Skin steps: the closest mark is one drawing away, the next is two, and so on,
// fading with distance. Click a mark (or drag across several) to set how many
// previous/next drawings are ghosted.
//
// A single "Onion" label opens an advanced popover for finer control.
Item {
    id: root

    objectName: "temporalStrip"

    property var controller: null

    // Number of ghost dots available on each side.
    readonly property int maxSteps: 3
    readonly property int dotSpacing: 10

    // The widget's natural width: centred around the exposure block. Set
    // enough room for the label, the side trails, and a grown exposure block
    // so a long hold does not clip the layout.
    implicitWidth: 218
    implicitHeight: 30
    // Pull the live values from the controller.
    readonly property int prevCount: controller ? controller.onionBefore : 0
    readonly property int nextCount: controller ? controller.onionAfter : 0
    readonly property int exposure: controller ? controller.currentHold : 1

    // Drag state while the exposure's right edge is being resized.
    property bool draggingExposure: false
    property int dragStartPointerX: 0
    property int dragStartExposure: 1

    // --- the advanced popover --------------------------------------------------
    // Opens above the well (negative y) so it does not cover the timeline.
    // Taller with explanatory text so first-time users understand ghosts and opacity.
    Popup {
        id: settingsPopup
        objectName: "onionSettingsPopup"
        x: -6
        y: -height - 8
        width: 300
        padding: 0
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Item {
            Rectangle {
                y: parent.height - 5
                x: Math.max(12, Math.min(parent.width - 24, root.width / 2 - 5))
                width: 10; height: 10
                rotation: 45
                color: Theme.surfaceHigh
                border.width: 1; border.color: Theme.border
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusMedium
                color: Theme.surfaceHigh
                border.width: 1; border.color: Theme.border
            }
        }

        contentItem: ColumnLayout {
            spacing: Theme.spaceM
            anchors.margins: Theme.spaceM
            anchors.fill: parent

            ColumnLayout {
                spacing: Theme.spaceXS
                Layout.topMargin: Theme.spaceM
                Layout.leftMargin: Theme.spaceM
                Layout.rightMargin: Theme.spaceM
                Layout.fillWidth: true
                Text {
                    text: "Onion Skin"
                    color: Theme.text
                    font.pixelSize: Theme.fontM
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "See nearby drawings as transparent ghosts to keep motion consistent. Previous shows earlier drawings, Next shows later ones."
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontS
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // --- Previous / Next steppers --------------------------------------
            RowLayout {
                Layout.leftMargin: Theme.spaceM
                Layout.rightMargin: Theme.spaceM
                Layout.fillWidth: true
                spacing: Theme.spaceS
                Text { text: "Previous"; color: Theme.textSecondary; font.pixelSize: Theme.fontM; Layout.fillWidth: true }
                AppToolButton {
                    text: "\u2212"; implicitWidth: 24; implicitHeight: 24
                    enabled: root.prevCount > 0
                    onClicked: root.controller.setOnionBefore(root.prevCount - 1)
                }
                Text { text: root.prevCount; color: Theme.text; font.pixelSize: Theme.fontM; font.weight: Font.DemiBold; Layout.preferredWidth: 20; horizontalAlignment: Text.AlignHCenter }
                AppToolButton {
                    text: "+"; implicitWidth: 24; implicitHeight: 24
                    enabled: root.prevCount < root.maxSteps
                    onClicked: root.controller.setOnionBefore(root.prevCount + 1)
                }
            }
            RowLayout {
                Layout.leftMargin: Theme.spaceM
                Layout.rightMargin: Theme.spaceM
                Layout.fillWidth: true
                spacing: Theme.spaceS
                Text { text: "Next"; color: Theme.textSecondary; font.pixelSize: Theme.fontM; Layout.fillWidth: true }
                AppToolButton {
                    text: "\u2212"; implicitWidth: 24; implicitHeight: 24
                    enabled: root.nextCount > 0
                    onClicked: root.controller.setOnionAfter(root.nextCount - 1)
                }
                Text { text: root.nextCount; color: Theme.text; font.pixelSize: Theme.fontM; font.weight: Font.DemiBold; Layout.preferredWidth: 20; horizontalAlignment: Text.AlignHCenter }
                AppToolButton {
                    text: "+"; implicitWidth: 24; implicitHeight: 24
                    enabled: root.nextCount < root.maxSteps
                    onClicked: root.controller.setOnionAfter(root.nextCount + 1)
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border;
                        Layout.leftMargin: Theme.spaceM; Layout.rightMargin: Theme.spaceM; Layout.topMargin: Theme.spaceS }

            // --- Nearest-ghost opacity (falloff is fixed by the canvas) --------
            ColumnLayout {
                Layout.leftMargin: Theme.spaceM
                Layout.rightMargin: Theme.spaceM
                Layout.fillWidth: true
                spacing: Theme.spaceXS
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceS
                    Text { text: "Opacity"; color: Theme.textSecondary; font.pixelSize: Theme.fontM; Layout.fillWidth: true }
                    Text { text: Math.round((controller ? controller.onionOpacity : 0.45) * 100) + "%"; color: Theme.text; font.pixelSize: Theme.fontM }
                }
                Text {
                    text: "How faint the ghosts are. 100% is solid, lower is more transparent."
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontXS
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            AppSlider {
                id: opacitySlider
                Layout.leftMargin: Theme.spaceM
                Layout.rightMargin: Theme.spaceM
                Layout.bottomMargin: Theme.spaceM
                Layout.fillWidth: true
                from: 10; to: 100
                value: controller ? Math.round(controller.onionOpacity * 100) : 45
                onMoved: { if (controller) controller.setOnionOpacity(value / 100.0) }
            }
            Text {
                text: "Tip: Previous/Next dots in the timeline toggle ghosts. Exposure in the strip sets hold length (drag the edge or click 1f to type)."
                color: Theme.textTertiary
                font.pixelSize: Theme.fontXS
                wrapMode: Text.WordWrap
                Layout.leftMargin: Theme.spaceM
                Layout.rightMargin: Theme.spaceM
                Layout.bottomMargin: Theme.spaceM
                Layout.fillWidth: true
            }
        }
    }

    // --- the widget itself ------------------------------------------------
    Rectangle {
        id: well
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: wellHover.hovered || settingsPopup.opened ? Theme.surfaceHover : Theme.surfaceHigh
        border.width: 1
        border.color: settingsPopup.opened ? Theme.accentBorder
                    : wellHover.hovered ? Theme.borderStrong : Theme.border

        Row {
            id: trailRow
            anchors.centerIn: parent
            spacing: Theme.spaceS

            // "Onion" label, the way into the advanced popover.
            Rectangle {
                id: onionLabel
                width: onionText.implicitWidth + 12
                height: root.implicitHeight - 4
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.radiusSmall
                color: labelMouse.containsMouse || settingsPopup.opened ? Theme.surfaceHigh : "transparent"
                Text {
                    id: onionText
                    anchors.centerIn: parent
                    text: "Onion"
                    color: labelMouse.containsMouse || settingsPopup.opened ? Theme.accent : Theme.textSecondary
                    font.pixelSize: Theme.fontS
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: labelMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: settingsPopup.open()
                    ToolTip.visible: containsMouse && !settingsPopup.opened
                    ToolTip.text: "Onion skin: ghosts of nearby drawings. Dots toggle Previous/Next ghosts. Open for opacity and more. Exposure (1f) sets hold length."
                    ToolTip.delay: 400
                }
            }

            Rectangle { width: 1; height: 16; color: Theme.border; anchors.verticalCenter: parent.verticalCenter }

            // --- Previous ghost trail ---------------------------------------
            Item {
                id: prevTrail
                width: prevRow.width
                height: root.implicitHeight
                anchors.verticalCenter: parent.verticalCenter

                Row {
                    id: prevRow
                    anchors.centerIn: parent
                    spacing: root.dotSpacing
                    Repeater {
                        model: root.maxSteps
                        delegate: GhostMark {
                            distance: modelData
                            enabledCount: root.prevCount
                            side: "prev"
                        }
                    }
                }

                // A single hover map turns a mark click or a drag across marks
                // into a count for that side.
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    function countForPoint(x) {
                        // Marks are maxSteps dots side by side, closest first.
                        const period = 8 + root.dotSpacing
                        const idx = Math.min(root.maxSteps - 1, Math.max(0, Math.floor(x / period)))
                        return idx + 1
                    }
                    onClicked: (mouse) => {
                        const target = countForPoint(mouse.x)
                        // Clicking the already-outermost enabled mark collapses
                        // that side by one; anything else grows to include it.
                        if (root.prevCount === target && target > 0) {
                            root.controller.setOnionBefore(target - 1)
                        } else {
                            root.controller.setOnionBefore(target)
                        }
                    }
                    onPositionChanged: (mouse) => {
                        if (pressed) root.controller.setOnionBefore(countForPoint(mouse.x))
                    }
                }
            }

            // --- Exposure block ---------------------------------------------
            Rectangle {
                id: exposureBlock
                height: root.implicitHeight - 6
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.radiusSmall
                // Width grows gently with exposure (log) but is capped so the
                // control keeps a physical feel without ballooning.
                width: Math.min(64, 36 + Math.round(Math.log2(Math.max(1, root.exposure))) * 5)
                color: Theme.surfaceHigh
                border.width: 1
                border.color: (dragGrip.hovered || root.draggingExposure) ? Theme.accentBorder
                             : expHover.hovered ? Theme.borderStrong : Theme.borderStrong

                Text {
                    id: exposureLabel
                    visible: !editBox.visible
                    anchors.centerIn: parent
                    text: root.exposure + "f"
                    color: Theme.text
                    font.pixelSize: Theme.fontM
                    font.weight: Font.DemiBold
                }

                // Whole block: click types an exact value, wheel nudges it.
                MouseArea {
                    id: expHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // Let the right-edge grip win when the pointer is over it.
                    onPressed: (mouse) => { if (mouse.x > width - 12) return; editBox.open() }
                    onWheel: (wheel) => {
                        const delta = wheel.angleDelta.y > 0 ? 1 : -1
                        root.controller.setCurrentHold(Math.max(1, root.exposure + delta))
                        wheel.accepted = true
                    }
                    ToolTip.visible: containsMouse && !editBox.visible
                    ToolTip.text: "Exposure: click \u2192 type, wheel \u2192 change"
                }

                // The grip on the right edge drags the hold out or in.
                MouseArea {
                    id: dragGrip
                    anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                    width: 12
                    visible: !editBox.visible
                    hoverEnabled: true
                    cursorShape: Qt.SplitHCursor
                    onPressed: (mouse) => {
                        if (!mouse) return
                        root.draggingExposure = true
                        root.dragStartExposure = root.exposure
                        // Absolute scene x: immune to the block resizing itself
                        // under the pointer while the drag is in flight.
                        const sp = mouse.scenePosition
                        root.dragStartPointerX = sp ? sp.x : mouse.x
                        mouse.accepted = true
                    }
                    onPositionChanged: (mouse) => {
                        if (!root.draggingExposure || !mouse) return
                        const sp = mouse.scenePosition
                        const curX = sp ? sp.x : mouse.x
                        // Half a frame per pixel keeps the drag from being twitchy.
                        const deltaX = curX - root.dragStartPointerX
                        const target = Math.max(1, root.dragStartExposure + Math.round(deltaX / 4.0))
                        if (target !== root.exposure) root.controller.setCurrentHold(target)
                    }
                    onReleased: {
                        root.draggingExposure = false
                    }
                }

                // Inline exact-value editor.
                Item {
                    id: editBox
                    visible: false
                    anchors.fill: parent
                    clip: true
                    TextInput {
                        id: editInput
                        anchors.fill: parent
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: Theme.text
                        font.pixelSize: Theme.fontM
                        font.weight: Font.DemiBold
                        selectByMouse: true
                        validator: IntValidator { bottom: 1; top: 999 }
                        onEditingFinished: commit()
                        onActiveFocusChanged: { if (!activeFocus && editBox.visible) commit() }
                        function commit() {
                            if (text !== "" && acceptableInput) {
                                const v = parseInt(text, 10)
                                if (!isNaN(v) && v >= 1) root.controller.setCurrentHold(v)
                            }
                            editBox.visible = false
                        }
                    }
                    function open() {
                        visible = true
                        editInput.text = root.exposure
                        editInput.forceActiveFocus()
                        editInput.selectAll()
                    }
                }
            }

            // --- Next ghost trail ---------------------------------------------
            Item {
                id: nextTrail
                width: nextRow.width
                height: root.implicitHeight
                anchors.verticalCenter: parent.verticalCenter

                Row {
                    id: nextRow
                    anchors.centerIn: parent
                    spacing: root.dotSpacing
                    Repeater {
                        model: root.maxSteps
                        delegate: GhostMark {
                            distance: modelData
                            enabledCount: root.nextCount
                            side: "next"
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    function countForPoint(x) {
                        const period = 8 + root.dotSpacing
                        const idx = Math.min(root.maxSteps - 1, Math.max(0, Math.floor(x / period)))
                        return idx + 1
                    }
                    onClicked: (mouse) => {
                        const target = countForPoint(mouse.x)
                        if (root.nextCount === target && target > 0) {
                            root.controller.setOnionAfter(target - 1)
                        } else {
                            root.controller.setOnionAfter(target)
                        }
                    }
                    onPositionChanged: (mouse) => {
                        if (pressed) root.controller.setOnionAfter(countForPoint(mouse.x))
                    }
                }
            }
        }
    }

    // Hover for the outer well: a passive HoverHandler so the tint does not
    // steal clicks meant for the trails and the exposure block.
    HoverHandler {
        id: wellHover
        objectName: "wellHover"
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        cursorShape: undefined
    }
}
