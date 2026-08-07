// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Animage

// The brush colour picker: the platform colour dialog, which is a modern
// colour wheel everywhere Qt runs. The controller converts between its sRGB
// and the document's linear light.
AppDialog {
    id: dialog

    property var controller: null

    title: "Brush colour"
    standardButtons: Dialog.NoButton
    width: 260

    onOpened: colorDialog.open()
    onClosed: colorDialog.close()

    ColorDialog {
        id: colorDialog
        title: dialog.title
        selectedColor: controller ? controller.brushColour : "#000000"
        onAccepted: {
            controller.chooseBrushColour(selectedColor)
            dialog.close()
        }
        onRejected: dialog.close()
    }
}
