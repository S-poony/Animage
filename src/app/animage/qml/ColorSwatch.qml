// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// The two positions of the colour switch: the colour on the left, no colour on
// the right, and whichever is rimmed is what the brush is holding. Adjacent
// and unspaced, because two patches with a gap between them are two controls
// and these are two positions of one.
//
// The left half always shows the last real colour rather than whatever is in
// hand, so the control has two visible positions instead of one position and a
// blank. The right half only means something where a mark is a label, so off a
// colour layer it is greyed.
Row {
    id: root

    property color solidColour: "#000000"
    property bool transparentSelected: false
    property bool transparentEnabled: true

    signal chooseSolid()
    signal chooseTransparent()

    spacing: 0
    width: 44
    height: 22

    Rectangle {
        id: solidPatch
        width: 22
        height: root.height
        radius: Theme.radiusSmall
        color: root.solidColour
        border.width: 2
        border.color: !root.transparentSelected ? Theme.accentBorder : Theme.borderStrong

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.chooseSolid()
        }
        // A rim that does not change the size between the two positions.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1
            border.color: Theme.scrim
        }
    }

    Item {
        id: nonePatch
        width: 22
        height: root.height

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: root.transparentEnabled ? Theme.checkerDark : Theme.surface
            border.width: 2
            border.color: root.transparentEnabled && root.transparentSelected
                          ? Theme.accentBorder : Theme.border
        }

        // The slash over nothing that every program uses for "no colour".
        Canvas {
            anchors.fill: parent
            anchors.margins: 6
            visible: root.transparentEnabled
            opacity: 0.9
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = Theme.text;
                ctx.lineWidth = 2;
                ctx.lineCap = "round";
                ctx.beginPath();
                ctx.moveTo(width - 2, 2);
                ctx.lineTo(2, height - 2);
                ctx.stroke();
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            enabled: root.transparentEnabled
            onClicked: root.chooseTransparent()
        }
    }
}
