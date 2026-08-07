// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Animage

// One frame of the timeline: either the numbered card of a drawing (its run
// start) or a held tail of the same drawing. The card is what drags to
// reorder; the right edge of a run is what drags to re-time.
//
// Interaction is reported to the panel as signals; this component owns no
// document knowledge at all.
Item {
    id: cell

    property int cellIndex: 0
    property int number: 0
    property bool held: false
    property bool runStart: false
    property bool runEnd: false
    property int runStartSlot: 0
    property bool carried: false
    property bool hasColour: false
    property bool isCurrent: false
    property int cellWidth: 26

    signal cellClicked(int slot)
    signal stretchPressed(int runStartSlot)
    signal stretchMoved(int x)
    signal stretchReleased()
    signal dragStarted(int slot)
    signal dragMoved(int x)
    signal dragReleased(int x)

    // The face of the cell.
    Rectangle {
        anchors.fill: parent
        color: cell.held ? Theme.surface : Theme.surfaceHigh
        border.width: 0
    }

    // A held drawing is one block with a tail, not a repeated cell: a tick in
    // the middle instead of a number.
    Rectangle {
        visible: cell.held
        anchors.centerIn: parent
        width: 1
        height: parent.height - 12
        color: Theme.borderStrong
    }

    // The numbered card.
    Rectangle {
        anchors.fill: parent
        anchors.margins: cell.held ? 0 : 1
        radius: Theme.radiusSmall
        visible: !cell.held
        color: "transparent"
        border.width: 1
        border.color: cell.isCurrent ? Theme.accent : Theme.borderStrong

        Text {
            anchors.centerIn: parent
            text: cell.number
            color: cell.isCurrent ? Theme.text : Theme.textSecondary
            font.pixelSize: Theme.fontM
            font.weight: Font.DemiBold
        }
    }

    // Marks that were made on another drawing and carried here. Invisible when
    // it works, so it has to be told: a bar under the number.
    Rectangle {
        visible: cell.carried
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 3
        anchors.rightMargin: 3
        anchors.bottomMargin: 2
        height: 2
        radius: 1
        color: Theme.carried
    }

    // The current playhead, a rim over whatever else the cell shows.
    Rectangle {
        visible: cell.isCurrent
        anchors.fill: parent
        anchors.margins: 1
        radius: Theme.radiusSmall
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
    }

    // --- clicking and reordering -------------------------------------------
    MouseArea {
        id: card
        visible: cell.runStart
        anchors.fill: parent
        drag.target: card
        drag.axis: Drag.XAxis
        drag.threshold: 6
        cursorShape: Qt.OpenHandCursor
        preventStealing: true

        // Becomes true the first time the drag actually moves; used to fire
        // dragStarted exactly once, on the move that passes the threshold.
        property bool started: false

        onPressed: (mouse) => cell.cellClicked(cell.cellIndex)
        onPositionChanged: (mouse) => {
            if (!card.drag.active) return
            if (!card.started) {
                card.started = true
                cell.dragStarted(cell.cellIndex)
            }
            cell.dragMoved(card.x + cell.cellWidth / 2)
        }
        onReleased: {
            if (card.started) cell.dragReleased(card.x + cell.cellWidth / 2)
            card.started = false
            card.x = 0
        }
    }

    // --- stretching the run's right edge ------------------------------------
    // Thin visual handle, but a generous hit region so it is not a
    // pixel-hunt. The visible bar stays narrow; the MouseArea overlaps.
    Rectangle {
        visible: cell.runEnd
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 2
        color: cell.isCurrent ? Theme.accent : Theme.borderStrong
        opacity: stretch.containsMouse || stretch.pressed ? 1 : 0.6
    }
    MouseArea {
        id: stretch
        visible: cell.runEnd
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        // Hit region wider than the visible handle.
        width: 16
        drag.target: stretch
        drag.axis: Drag.XAxis
        cursorShape: Qt.SplitHCursor
        preventStealing: true
        z: 2

        onPressed: (mouse) => cell.stretchPressed(cell.runStartSlot)
        onPositionChanged: (mouse) => {
            if (stretch.drag.active)
                cell.stretchMoved(cell.cellIndex * cell.cellWidth + stretch.x + stretch.width)
        }
        onReleased: {
            if (stretch.drag.active) cell.stretchReleased()
            stretch.x = 0
        }
    }
}
