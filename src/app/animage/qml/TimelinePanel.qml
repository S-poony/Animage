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
    readonly property int rulerHeight: 14
    readonly property int stripHeight: 28

    height: 110

    // A gesture on the strip stops the flicker, so a drag does not become a
    // scroll the moment the pointer moves a pixel too far.
    QtObject {
        id: gestures
        property bool dragging: false
        property bool stretching: false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceS
        anchors.topMargin: Theme.spaceXS
        spacing: Theme.spaceXS

        // --- transport — thin line, distinct compartments -----------------------
        // [ play ] | [ drawing: add duplicate delete ] [ onion ]
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceS

            // [ play ]
            AppToolButton {
                Layout.preferredHeight: Theme.toolButton
                Layout.preferredWidth: Theme.toolButton
                text: controller.playing ? "Stop" : "Play"
                icon.name: controller.playing ? "media-playback-stop" : "media-playback-start"
                display: AbstractButton.IconOnly
                checkable: true
                checked: controller.playing
                onClicked: controller.togglePlayback()
                ToolTip.text: controller.playing ? "Stop (Enter)" : "Play the timeline in a loop (Enter)"
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: Theme.toolButton
                color: Theme.border
            }

            // [ drawing: add duplicate delete ]
            Rectangle {
                color: Theme.surfaceHigh
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSmall
                Layout.preferredHeight: Theme.toolButton
                Layout.preferredWidth: drawingRow.implicitWidth + Theme.spaceS * 2
                RowLayout {
                    id: drawingRow
                    anchors.centerIn: parent
                    spacing: Theme.spaceXS
                    Text {
                        text: "Drawing"
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontXS
                        font.letterSpacing: 0.8
                    }
                    AppToolButton {
                        Layout.preferredHeight: Theme.iconButton
                        Layout.preferredWidth: Theme.iconButton
                        text: "Add Drawing"
                        icon.name: "list-add"
                        display: AbstractButton.IconOnly
                        onClicked: controller.insertDrawing()
                        ToolTip.text: "Add a new empty drawing after this one (Insert)\nA drawing is one image; a frame is a slot in time."
                    }
                    AppToolButton {
                        Layout.preferredHeight: Theme.iconButton
                        Layout.preferredWidth: Theme.iconButton
                        text: "Duplicate Drawing"
                        icon.name: "edit-copy"
                        display: AbstractButton.IconOnly
                        onClicked: controller.duplicateDrawing()
                        ToolTip.text: "Copy this drawing into a new one (Ctrl+D)\nA real copy, not a hold."
                    }
                    AppToolButton {
                        Layout.preferredHeight: Theme.iconButton
                        Layout.preferredWidth: Theme.iconButton
                        text: "Delete Drawing"
                        icon.name: "user-trash-symbolic"
                        icon.source: "qrc:/Animage/animage/icons/user-trash-symbolic.svg"
                        display: AbstractButton.IconOnly
                        enabled: controller.drawingCount > 1
                        onClicked: controller.deleteDrawing()
                        ToolTip.text: "Delete this drawing (Delete).\nTo keep it for fewer frames instead, shorten its exposure below."
                    }
                }
            }

            // [ onion ]
            TemporalStrip {
                id: temporalStrip
                controller: root.controller
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredHeight: Theme.toolButton
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

    }
}
