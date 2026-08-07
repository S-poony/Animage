// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Animage

// Export: what first, then where. The dialog asks what to write; a folder
// dialog then asks where, because being asked to commit before knowing what
// you are committing to is backwards. The progress overlay reports the write.
AppDialog {
    id: dialog

    property var controller: null

    title: "Export sequences"

    property bool perLayer: true
    property bool flattened: false

    // The "Export…" button is the "what" half of the question; the folder
    // picker asks the "where" when it is pressed.
    onAccepted: {
        dialog.close()
        folderDialog.open()
    }

    FolderDialog {
        id: folderDialog
        title: "Export into"
        onAccepted: {
            const folder = folderDialog.selectedFolder
            if (folder.toString() === "") return
            controller.exportSequencesTo(folder, dialog.perLayer, dialog.flattened)
        }
    }

    contentItem: ColumnLayout {
        width: dialog.availableWidth
        spacing: Theme.spaceL

        Text {
            text: "16-bit PNG, over the canvas rectangle.\nHidden layers are not written."
            color: Theme.textSecondary
            font.pixelSize: Theme.fontM
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        AppCheckBox {
            labelText: "One sequence per layer"
            checked: true
            onToggled: dialog.perLayer = checked
        }
        AppCheckBox {
            labelText: "The flattened picture"
            onToggled: dialog.flattened = checked
        }

        Text {
            visible: !perLayer && !flattened
            text: "Nothing is selected to export."
            color: Theme.danger
            font.pixelSize: Theme.fontM
        }

        Text {
            text: "One folder per layer:  main_ink_0001.png, main_colour_0001.png \u2026"
            color: Theme.textTertiary
            font.pixelSize: Theme.fontXS
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // --- the action row --------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceS
            spacing: Theme.spaceS

            Item { Layout.fillWidth: true }

            AppToolButton {
                text: "Cancel"
                onClicked: dialog.reject()
            }
            AppToolButton {
                text: "Export\u2026"
                highlighted: true
                onClicked: dialog.accept()
            }
        }
    }
}
