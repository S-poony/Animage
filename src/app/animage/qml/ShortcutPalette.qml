// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Animage

// The shortcut cheat-sheet: everything the interface can do with the keyboard,
// in one place where it can be looked up. Opened with the ? button or by
// pressing Ctrl+/ — the shortcut most programs teach their users first.
AppDialog {
    id: dialog

    title: "Shortcuts"
    width: 460
    height: 460

    ListView {
        anchors.fill: parent
        anchors.topMargin: Theme.spaceXS
        clip: true
        spacing: Theme.spaceXS
        model: ListModel {
            ListElement { key: "Ctrl+N"; what: "New animation" }
            ListElement { key: "Ctrl+O"; what: "Open project" }
            ListElement { key: "Ctrl+S"; what: "Save" }
            ListElement { key: "Ctrl+Shift+S"; what: "Save as" }
            ListElement { key: "Ctrl+E"; what: "Export sequences" }
            ListElement { key: "Ctrl+Z"; what: "Undo" }
            ListElement { key: "Ctrl+Shift+Z"; what: "Redo" }
            ListElement { key: "Enter"; what: "Play / stop" }
            ListElement { key: "Left / Right"; what: "Previous / next frame" }
            ListElement { key: "Up / Down"; what: "Jump to previous / next drawing" }
            ListElement { key: "Insert"; what: "Add a drawing" }
            ListElement { key: "Ctrl+D"; what: "Duplicate drawing" }
            ListElement { key: "Delete"; what: "Delete drawing" }
            ListElement { key: "+ / -"; what: "Hold longer / shorter" }
            ListElement { key: "B / E"; what: "Brush / eraser" }
            ListElement { key: "[ / ]"; what: "Smaller / larger brush" }
            ListElement { key: "Alt+click"; what: "Pick a colour from the drawing" }
            ListElement { key: "Alt+right-drag"; what: "Resize the brush" }
            ListElement { key: "Space"; what: "Hold to pan" }
            ListElement { key: "Z"; what: "Hold to scrubby-zoom" }
            ListElement { key: "0 / 1"; what: "Fit canvas / actual size" }
            ListElement { key: "Shift+0"; what: "Fit the artwork to the window" }
            ListElement { key: "Ctrl+/"; what: "This list" }
        }

        delegate: RowLayout {
            width: dialog.availableWidth
            spacing: Theme.spaceL

            Rectangle {
                Layout.preferredWidth: 108
                height: 24
                radius: Theme.radiusSmall
                color: Theme.surfaceHigh
                border.width: 1
                border.color: Theme.border

                Text {
                    anchors.centerIn: parent
                    text: model.key
                    color: Theme.accent
                    font.pixelSize: Theme.fontM
                    font.weight: Font.DemiBold
                }
            }

            Text {
                Layout.fillWidth: true
                text: model.what
                color: Theme.text
                font.pixelSize: Theme.fontM
            }
        }
    }

    footer: Item {
        width: dialog.availableWidth
        height: Theme.controlHeight + Theme.spaceM

        AppToolButton {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            text: "Close"
            onClicked: dialog.close()
        }
    }
}
