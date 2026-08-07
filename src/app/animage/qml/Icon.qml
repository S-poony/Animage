// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import Animage

// An icon drawn from the catalogue in Icons.qml. Use it anywhere a small
// vector glyph belongs: tool buttons, list rows, the status bar.
//
// Every path in the catalogue is drawn on a 24x24 artboard. This item scales
// the whole artboard down to `size` as one unit, so an icon never gets clipped
// or optically off-centre however small the button that holds it is.
Item {
    id: root

    property string name: ""
    property color color: Theme.text
    property real size: Theme.iconSize
    // The button that holds this icon dims it when disabled; named `active`
    // rather than `enabled` because QQuickItem already owns `enabled`.
    property bool active: true

    implicitWidth: size
    implicitHeight: size

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: root.size / 24
        antialiasing: true
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: root.active ? root.color : Theme.textDisabled
            strokeColor: "transparent"
            strokeWidth: 0
            PathSvg {
                path: Icons.path(root.name)
            }
        }
    }
}
