// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Animage

// The track list: one row per track, index 0 on top (composites first).
// The current track is highlighted; clicking switches, and the Track menu
// handles add/rename/delete. This is the minimal multi-track UI that makes
// the feature visible — the full upstream timeline showed all tracks as rows
// under one ruler, which this can grow into.
Item {
    id: root
    property var controller: null
    height: 36

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spaceS
        anchors.rightMargin: Theme.spaceS
        spacing: Theme.spaceXS

        Text {
            text: "Tracks"
            color: Theme.textTertiary
            font.pixelSize: Theme.fontXS
            font.letterSpacing: 0.8
            Layout.alignment: Qt.AlignVCenter
        }

        Repeater {
            model: controller ? controller.tracksModel : null
            delegate: Rectangle {
                Layout.preferredHeight: 24
                Layout.preferredWidth: trackText.implicitWidth + 24
                radius: Theme.radiusSmall
                color: model.isCurrent ? Theme.accentSoft : (trackMouse.containsMouse ? Theme.surfaceHover : Theme.surfaceHigh)
                border.width: model.isCurrent ? 1 : 0
                border.color: Theme.accentBorder

                Text {
                    id: trackText
                    anchors.centerIn: parent
                    text: model.name
                    color: model.isCurrent ? Theme.accent : Theme.textSecondary
                    font.pixelSize: Theme.fontS
                    font.weight: model.isCurrent ? Font.DemiBold : Font.Normal
                }

                MouseArea {
                    id: trackMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: controller.setCurrentTrackIndex(index)
                }
            }
        }

        AppToolButton {
            Layout.preferredHeight: 24
            Layout.preferredWidth: 24
            text: "+"
            display: AbstractButton.TextOnly
            onClicked: controller.addTrack()
            ToolTip.text: "Add track (Track → Add Track)"
        }

        Item { Layout.fillWidth: true }
    }
}
