// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Animage

// The dropdown menu the interface uses (the "+" in the layer panel). Menu for
// behaviour, our tokens for looks: a dark rounded surface instead of whatever
// the platform style would paint. Declare AppMenuItem children inside it:
//
//     AppMenu {
//         AppMenuItem { text: "Raster layer"; onTriggered: ... }
//     }
Menu {
    id: root

    padding: Theme.spaceXS

    background: Rectangle {
        radius: Theme.radiusMedium
        color: Theme.surfaceHigh
        border.width: 1
        border.color: Theme.border
    }
}
