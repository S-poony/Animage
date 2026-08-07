// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Animage

// The bottom panel: the transport, the frame verbs, and the timeline itself —
// one track as a strip of slots, a frame held over several slots shown as one
// block with a tail. Below the strip sit the frame controls: onion skin, how
// long the current frame is held, and the frame rate.
//
// The ruler is a scrub band and nothing else. Exposure edges live below it, so
// dragging along time can never resize a hold by accident.
Item {
    id: root

    property var controller: null
    readonly property int cellWidth: 26
    readonly property int rulerHeight: 20
    readonly property int stripHeight: 52

    height: 190

    // A gesture on the strip stops the flicker, so a drag does not become a
    // scroll the moment the pointer moves a pixel too far.
    QtObject {
        id: gestures
        property bool dragging: false
        property bool stretching: false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceM
        anchors.topMargin: Theme.spaceS
        spacing: Theme.spaceS

        // --- transport and the drawing verbs ---------------------------------
        // The words say what the buttons do; an animation program's most
        // important action is the one that cannot be mistaken. A drawing (an
        // image/cel) is the thing a frame shows; a frame is a slot in time, and
        // one drawing can be held across several frames -- see Exposure below.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXS

            AppToolButton {
                iconName: controller.playing ? "stop" : "play"
                iconColor: Theme.accent
                checked: controller.playing
                onClicked: controller.togglePlayback()
                ToolTip.text: controller.playing ? "Stop (Enter)" : "Play the timeline in a loop (Enter)"
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: 4
                Layout.bottomMargin: 4
                color: Theme.border
            }

            AppToolButton {
                iconName: "plus"
                text: "Add Drawing"
                onClicked: controller.insertDrawing()
                ToolTip.text: "Add a new empty drawing after this one (Insert)\nA drawing is one image; a frame is a slot in time."
            }
            AppToolButton {
                iconName: "duplicate"
                text: "Duplicate Drawing"
                onClicked: controller.duplicateDrawing()
                ToolTip.text: "Copy this drawing into a new one (Ctrl+D)\nA real copy, not a hold."
            }
            AppToolButton {
                iconName: "trash"
                text: "Delete Drawing"
                enabled: controller.drawingCount > 1
                onClicked: controller.deleteDrawing()
                ToolTip.text: "Delete this drawing (Delete).\nTo keep it for fewer frames instead, shorten its exposure below."
            }
            AppToolButton {
                iconName: "clear"
                onClicked: controller.clearCurrentCel()
                ToolTip.text: "Empty the current layer on this frame only."
            }

            Item { Layout.fillWidth: true }
        }

        // --- the strip -------------------------------------------------------
        Flickable {
            id: strip
            Layout.fillWidth: true
            Layout.preferredHeight: root.rulerHeight + root.stripHeight
            clip: true
            contentWidth: cellsRow.width
            contentHeight: root.rulerHeight + root.stripHeight
            boundsBehavior: Flickable.StopAtBounds
            interactive: !gestures.dragging && !gestures.stretching

            Column {
                id: cellsRow
                width: Math.max(implicitWidth, strip.width)
                spacing: 0

                // The ruler: a scrub band, numbered every five frames.
                Rectangle {
                    id: ruler
                    width: cellsRow.width
                    height: root.rulerHeight
                    color: Theme.surfaceHigh

                    Row {
                        spacing: 0
                        Repeater {
                            model: controller ? controller.timelineModel : null
                            delegate: Rectangle {
                                width: root.cellWidth
                                height: ruler.height
                                color: index === controller.currentSlot ? Theme.accent : "transparent"

                                Text {
                                    anchors.centerIn: parent
                                    visible: index % 5 === 0
                                    text: index + 1
                                    color: index === controller.currentSlot
                                           ? Theme.textOnAccent : Theme.textTertiary
                                    font.pixelSize: Theme.fontXS
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        preventStealing: true
                        cursorShape: Qt.PointingHandCursor
                        onPressed: (mouse) => controller.setCurrentSlot(Math.floor(mouse.x / root.cellWidth))
                        onPositionChanged: (mouse) => {
                            if (pressed) controller.setCurrentSlot(Math.floor(mouse.x / root.cellWidth))
                        }
                    }
                }

                // The strip itself: one cell per slot.
                Row {
                    id: cells
                    spacing: 0

                    Repeater {
                        model: controller ? controller.timelineModel : null
                        delegate: TimelineCell {
                            width: root.cellWidth
                            height: root.stripHeight
                            cellIndex: index
                            number: model.number
                            held: model.held
                            runStart: model.runStart
                            runEnd: model.runEnd
                            runStartSlot: model.runStartSlot
                            carried: model.carried
                            hasColour: model.hasColour
                            isCurrent: index === controller.currentSlot
                            cellWidth: root.cellWidth

                            onCellClicked: (slot) => controller.setCurrentSlot(slot)
                            onStretchPressed: (slot) => {
                                gestures.stretching = true
                                controller.beginStretch(slot)
                            }
                            onStretchMoved: (x) => controller.stretchTo(x, root.cellWidth)
                            onStretchReleased: () => {
                                gestures.stretching = false
                                controller.endStretch()
                            }
                            onDragStarted: (slot) => {
                                gestures.dragging = true
                                controller.beginTimelineDrag(slot)
                                insertionLine.x = 0
                            }
                            onDragMoved: (x) => {
                                const drop = controller.timelineDropIndexFor(x, root.cellWidth)
                                insertionLine.x = Math.max(0, drop) * root.cellWidth
                            }
                            onDragReleased: (x) => {
                                gestures.dragging = false
                                controller.endTimelineDrag(x, root.cellWidth)
                                insertionLine.x = -1000
                            }
                        }
                    }

                    // The insertion line shown while a frame is being moved.
                    Rectangle {
                        id: insertionLine
                        x: -1000
                        y: 0
                        width: 3
                        height: root.stripHeight
                        color: Theme.accent
                        visible: x >= 0
                        radius: 2
                    }
                }
            }
        }

        // --- the frame controls ---------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceS

            // Onion skin: how many frames on either side are shown ghosted.
            Text {
                text: "Onion skin"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
            }
            AppComboBox {
                id: onionCombo
                implicitWidth: 62
                model: ["Off", "1", "2", "3", "4", "5"]
                currentIndex: controller ? controller.onionCount : 0
                onActivated: controller.setOnionCount(currentIndex)
                ToolTip.visible: hovered
                ToolTip.text: "Ghost the drawings on the frames either side of this\n" +
                              "one so you can see where the motion is going. Off shows none."
            }

            Item { Layout.fillWidth: true }

            // Exposure: how many frames the drawing in front of you is held for.
            Text {
                text: "Exposure"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
            }
            AppToolButton {
                text: "\u2212"
                enabled: controller.currentHold > 1
                onClicked: controller.holdShorter()
                ToolTip.text: "Hold this drawing one frame less"
            }
            Text {
                text: controller ? (controller.currentHold === 1
                                   ? "1 frame" : controller.currentHold + " frames") : ""
                color: Theme.text
                font.pixelSize: Theme.fontM
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 56
            }
            AppToolButton {
                text: "+"
                onClicked: controller.holdLonger()
                ToolTip.text: "Hold this drawing one frame longer (+)\nRepeats the same drawing across time; costs nothing."
            }
        }
    }
}
