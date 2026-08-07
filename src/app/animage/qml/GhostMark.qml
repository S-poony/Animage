// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Animage

// One ghost "dot" in the temporal strip's onion-skin trail - purely visual.
// Its distance from the center exposure block is wired up by the caller:
// distance 0 is the closest (one drawing away), 1 is two away, and so on. The
// dot fades with distance and lights up once the side's onion-skin count
// reaches past it. Interaction (click and drag-across) lives on the surrounding
// trail so a drag can sweep several marks at once.
Item {
    id: root

    // 0 = nearest to the current drawing.
    property int distance: 0
    // How many ghosts this side currently shows.
    property int enabledCount: 0
    // "prev" or "next", for the tint.
    property string side: "prev"

    readonly property int stepOneBased: distance + 1
    readonly property bool active: enabledCount > distance
    // Fade with distance from the current drawing.
    readonly property real dim: active ? (1.0 - distance * 0.30) : 0.18

    width: 8
    height: 10

    Rectangle {
        anchors.centerIn: parent
        width: 7
        height: 7
        radius: 3.5
        color: active ? (side === "prev" ? Theme.carried : Theme.accent)
                      : Theme.textDisabled
        opacity: root.dim
        scale: active ? 1.0 : 0.8

        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
    }
}
