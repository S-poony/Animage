// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Animage

ApplicationWindow {
    id: window

    title: controller.title
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1080
    minimumHeight: 680
    color: Theme.background

    // Native platform style (see main.cpp): macOS on mac, Windows on Windows,
    // Fusion/Basic on Linux — no custom chrome for standard controls.

    // --- the brain ----------------------------------------------------------
    AppController {
        id: controller
    }
    SceneSettingsModel {
        id: sceneSettings
    }

    // --- the dialogs ---------------------------------------------------------
    SceneSettingsDialog {
        id: sceneSettingsDialog
        controller: controller
        model: sceneSettings
    }
    ExportDialog {
        id: exportDialog
        controller: controller
    }
    PaletteDialog {
        id: paletteDialog
        controller: controller
    }
    ShortcutPalette {
        id: shortcutPalette
    }

    AppDialog {
        id: renameTrackDialog
        title: "Rename track"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: controller.renameCurrentTrack(trackNameField.text)
        contentItem: ColumnLayout {
            width: renameTrackDialog.availableWidth
            spacing: Theme.spaceM
            Text {
                text: "Track name:"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
            }
            AppTextField {
                id: trackNameField
                Layout.fillWidth: true
                placeholderText: "Track name"
                onAccepted: renameTrackDialog.accept()
            }
        }
        onOpened: {
            trackNameField.text = ""
            if (controller.trackCount > 0 && controller.tracksModel) {
                // Try to get current track name via model
                const idx = controller.currentTrackIndex
                if (idx >= 0) {
                    const m = controller.tracksModel
                    // Access via data() is not directly exposed, use a helper property
                    // Fall back to empty and let user type
                }
            }
            trackNameField.forceActiveFocus()
        }
    }

    // "May I leave the unsaved document?" -- raised by the controller when New,
    // Open or Close would lose work. Three answers, no way to lose the work by
    // accident: Save goes to the save-as dialog first, and the leave only
    // happens if that lands.
    AppDialog {
        id: leaveDialog
        title: "Save this project?"
        standardButtons: Dialog.NoButton
        property string question: ""

        contentItem: ColumnLayout {
            width: leaveDialog.availableWidth
            spacing: Theme.spaceL

            Text {
                text: leaveDialog.question
                color: Theme.text
                font.pixelSize: Theme.fontM
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceS

                Item { Layout.fillWidth: true }

                AppToolButton {
                    text: "Cancel"
                    onClicked: {
                        controller.respondSaveDecision(AppController.Cancel)
                        leaveDialog.close()
                    }
                }
                AppToolButton {
                    text: "Discard"
                    onClicked: {
                        controller.respondSaveDecision(AppController.Discard)
                        leaveDialog.close()
                    }
                }
                AppToolButton {
                    text: "Save"
                    highlighted: true
                    onClicked: {
                        controller.respondSaveDecision(AppController.Save)
                        leaveDialog.close()
                    }
                }
            }
        }
    }

    // --- file dialogs --------------------------------------------------------
    FileDialog {
        id: openFileDialog
        title: "Open project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Animage Project (*.animage scene.json)", "All files (*)"]
        onAccepted: controller.acceptOpenLocation(selectedFile)
    }
    FileDialog {
        id: saveFileDialog
        title: "Save project as"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Animage Project (*.animage)"]
        defaultSuffix: "animage"
        onAccepted: controller.acceptSaveLocation(selectedFile)
    }

    // --- export progress ------------------------------------------------------
    Rectangle {
        id: exportOverlay
        visible: exporting
        anchors.fill: parent
        z: 100
        color: Theme.scrim

        Rectangle {
            anchors.centerIn: parent
            width: 340
            height: 120
            radius: Theme.radiusLarge
            color: Theme.surfaceHigh
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spaceL
                spacing: Theme.spaceM

                Text {
                    text: "Exporting " + exportDone + " of " + exportTotal + " frames\u2026"
                    color: Theme.text
                    font.pixelSize: Theme.fontM
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, exportTotal)
                    value: exportDone
                }
                AppToolButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Cancel"
                    onClicked: controller.cancelExport()
                }
            }
        }
    }

    // --- native menu ---------------------------------------------------------
    // Platform-native MenuBar: macOS on mac, Windows on Windows, Fusion on Linux.
    // Uses Action.icon.name for native icons; no custom ToolBar.
    menuBar: MenuBar {
        background: Rectangle { color: Theme.background }
        palette.window: Theme.background
        palette.windowText: Theme.text

        Menu {
            title: qsTr("&File")
            Action { text: qsTr("&New"); icon.name: "document-new"; shortcut: "Ctrl+N"; onTriggered: controller.newProject() }
            Action { text: qsTr("&Open…"); icon.name: "document-open"; shortcut: "Ctrl+O"; onTriggered: controller.openProject() }
            MenuSeparator {}
            Action { text: qsTr("&Save"); icon.name: "document-save"; shortcut: "Ctrl+S"; onTriggered: controller.saveProject() }
            Action { text: qsTr("Save &As…"); icon.name: "document-save-as"; shortcut: "Ctrl+Shift+S"; onTriggered: controller.saveProjectAs() }
            MenuSeparator {}
            Action { text: qsTr("&Export…"); icon.name: "document-send"; shortcut: "Ctrl+E"; onTriggered: controller.exportSequences() }
            MenuSeparator {}
            Action { text: qsTr("&Quit"); shortcut: "Ctrl+Q"; onTriggered: window.close() }
        }
        Menu {
            title: qsTr("&Edit")
            Action { text: qsTr("&Undo"); icon.name: "edit-undo"; shortcut: "Ctrl+Z"; enabled: controller.canUndo; onTriggered: controller.undo() }
            Action { text: qsTr("&Redo"); icon.name: "edit-redo"; shortcut: "Ctrl+Shift+Z"; enabled: controller.canRedo; onTriggered: controller.redo() }
        }
        Menu {
            title: qsTr("&Scene")
            Action { text: qsTr("Scene &Settings…"); icon.name: "preferences-system"; onTriggered: sceneSettingsDialog.present(controller.framerate, controller.sceneWidth, controller.sceneHeight, controller.sceneLength) }
        }
        Menu {
            title: qsTr("&Track")
            Action { text: qsTr("&Add Track"); icon.name: "list-add"; onTriggered: controller.addTrack() }
            Action { text: qsTr("&Rename Track…"); onTriggered: renameTrackDialog.open() }
            Action { text: qsTr("&Delete Track"); enabled: controller.trackCount > 1; onTriggered: controller.removeCurrentTrack() }
            MenuSeparator {}
            Action { text: qsTr("&Overwrite Drawings"); checkable: true; checked: controller.overwrite; onTriggered: controller.setOverwrite(checked) }
            Menu {
                title: qsTr("Past the &Last Drawing")
                Action { text: qsTr("Show &Nothing"); checkable: true; checked: controller.trackEnd === 0; onTriggered: controller.setTrackEnd(0) }
                Action { text: qsTr("&Hold Last"); checkable: true; checked: controller.trackEnd === 1; onTriggered: controller.setTrackEnd(1) }
                Action { text: qsTr("&Cycle"); checkable: true; checked: controller.trackEnd === 2; onTriggered: controller.setTrackEnd(2) }
            }
        }
        Menu {
            title: qsTr("&View")
            Action { text: qsTr("Zoom &100%"); icon.name: "zoom-original"; shortcut: "1"; onTriggered: canvas.resetView() }
            Action { text: qsTr("&Fit Canvas"); icon.name: "zoom-fit-best"; shortcut: "0"; onTriggered: canvas.fitToCanvas() }
            Action { text: qsTr("Fit &Artwork"); shortcut: "Shift+0"; onTriggered: canvas.fitToDrawing() }
        }
        Menu {
            title: qsTr("&Help")
            Action { text: qsTr("&Shortcuts"); icon.name: "help-contents"; shortcut: "Ctrl+/"; onTriggered: shortcutPalette.open() }
        }
    }

    // The vertical tool rail: tools and the colour picker. Brush settings
    // that are not colour (size, pressure) live in the Tool Options strip
    // above the canvas.
    Rectangle {
        id: toolRail
        anchors.top: parent.top
        anchors.bottom: footerRow.top
        anchors.left: parent.left
        width: 56
        color: Theme.surface
        border.width: 0

        Rectangle {
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: 1
            color: Theme.border
            z: 10
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: Theme.spaceS
            anchors.bottomMargin: Theme.spaceS
            anchors.leftMargin: Theme.spaceXS
            anchors.rightMargin: Theme.spaceXS
            spacing: Theme.spaceS

            AppToolButton {
                Layout.alignment: Qt.AlignHCenter
                text: "Brush"
                icon.name: "document-edit-symbolic"
                icon.source: "qrc:/Animage/animage/icons/document-edit-symbolic.svg"
                display: AbstractButton.IconOnly
                checkable: true
                checked: controller.tool === AppController.Brush
                shortcutHint: "B"
                onClicked: controller.setTool(AppController.Brush)
                ToolTip.text: "Brush (B)"
            }
            AppToolButton {
                Layout.alignment: Qt.AlignHCenter
                text: "Eraser"
                icon.name: "edit-cut-symbolic"
                icon.source: "qrc:/Animage/animage/icons/edit-cut-symbolic.svg"
                display: AbstractButton.IconOnly
                checkable: true
                checked: controller.tool === AppController.Eraser
                shortcutHint: "E"
                onClicked: controller.setTool(AppController.Eraser)
                ToolTip.text: "Eraser (E)"
            }

            // Colour picker — lives with the tools, not squeezed into the
            // Tool Options strip. One always-visible well shows the solid
            // colour; on a colourize layer a compact hint/erase toggle
            // appears beneath it.
            ColumnLayout {
                visible: controller.tool === AppController.Brush
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.spaceS
                spacing: Theme.spaceXS

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 1
                    color: Theme.border
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "colour"
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontXS
                    font.letterSpacing: 1.0
                }

                Rectangle {
                    id: railColourWell
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    radius: Theme.radiusMedium
                    color: controller ? controller.solidColour : "#000000"
                    border.width: 1
                    border.color: wellMouse.containsMouse ? Theme.borderStrong : Theme.border

                    // Inner hairline so a black well still reads against the rail.
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.scrim
                        opacity: 0.9
                    }

                    // Hover lift — subtle but makes the well feel tappable.
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: wellMouse.containsMouse ? Theme.surfaceHover : "transparent"
                        opacity: wellMouse.containsMouse ? 0.14 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                    }

                    // When the brush is in transparent-hint mode the well
                    // keeps showing the solid colour (so the two positions
                    // stay visible) but is crossed out to signal the mode.
                    Item {
                        anchors.fill: parent
                        visible: controller ? controller.transparentSelected : false
                        opacity: 0.95
                        Canvas {
                            anchors.fill: parent
                            anchors.margins: 7
                            onPaint: {
                                const ctx = getContext("2d");
                                ctx.reset();
                                ctx.strokeStyle = Theme.text;
                                ctx.lineWidth = 2.2;
                                ctx.lineCap = "round";
                                ctx.beginPath();
                                ctx.moveTo(width - 1, 1);
                                ctx.lineTo(1, height - 1);
                                ctx.stroke();
                            }
                        }
                    }

                    MouseArea {
                        id: wellMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: paletteDialog.open()
                        ToolTip.visible: containsMouse
                        ToolTip.text: "Brush colour — click to change (Alt+click on canvas picks up)"
                    }
                }

                // Colourize-layer hint toggle — compact so the 56px rail stays tidy.
                AppToolButton {
                    visible: controller.onColourLayer
                    Layout.alignment: Qt.AlignHCenter
                    small: true
                    text: controller.transparentSelected ? "Erase" : "Hint"
                    icon.name: controller.transparentSelected ? "edit-cut-symbolic" : "document-edit-symbolic"
                    icon.source: controller.transparentSelected ? "qrc:/Animage/animage/icons/edit-cut-symbolic.svg" : "qrc:/Animage/animage/icons/document-edit-symbolic.svg"
                    display: AbstractButton.IconOnly
                    checkable: true
                    checked: controller.transparentSelected
                    onClicked: {
                        if (controller.transparentSelected) controller.chooseSolidColour()
                        else controller.chooseTransparent()
                    }
                    ToolTip.text: controller.transparentSelected ? "Erase hint — click for colour hint" : "Colour hint — click to erase"
                }
                Text {
                    visible: controller.onColourLayer
                    Layout.alignment: Qt.AlignHCenter
                    text: controller.transparentSelected ? "erase" : "hint"
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontXS
                    font.letterSpacing: 0.4
                }
            }

            // Empty frame — previously "Clear" in the timeline, now a left-rail tool
            // with an icon distinct from the eraser (edit-cut vs edit-clear-all).
            AppToolButton {
                Layout.alignment: Qt.AlignHCenter
                text: "Clear Frame"
                icon.name: "edit-clear-all-symbolic"
                icon.source: "qrc:/Animage/animage/icons/edit-clear-all-symbolic.svg"
                display: AbstractButton.IconOnly
                onClicked: controller.clearCurrentCel()
                ToolTip.text: "Empty the current layer on this frame only."
            }

            Item { Layout.fillHeight: true }

            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: Theme.spaceS
                text: "tools"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontXS
                font.letterSpacing: 1.1
            }
        }
    }

    // The tool options: what the active tool can do, above the canvas.
    // Colour lives in the left rail now, not here.
    ToolOptionsBar {
        id: toolOptions
        controller: controller
        anchors.top: parent.top
        anchors.left: toolRail.right
        anchors.right: layerPanel.left
        height: 42

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.border
        }
    }

    // The canvas, in its well.
    Rectangle {
        id: canvasWell
        anchors.top: toolOptions.bottom
        anchors.left: toolRail.right
        anchors.right: layerPanel.left
        anchors.bottom: footerRow.top
        color: Theme.canvasWell

        CanvasView {
            id: canvas
            objectName: "canvasView"
            anchors.fill: parent
            anchors.margins: 0
            focus: true
            // Space and Z are the canvas's own; see CanvasView::eventFilter.
        }

        // A blank new document gives no direction; this is the direction.
        // It sits over the canvas without taking input, and disappears the
        // moment anything is drawn anywhere.
        Text {
            id: emptyHint
            anchors.centerIn: parent
            visible: controller.tileCount === 0
            text: "Start drawing with the Brush\n\nPress Insert to add a drawing"
            color: Theme.textTertiary
            opacity: 0.85
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontXL
            z: 2
        }
    }

    LayerPanel {
        id: layerPanel
        objectName: "layerPanel"
        controller: controller
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: footerRow.top
    }

    // The footer: the timeline and, under it, the status line.
    Column {
        id: footerRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }

        TracksPanel {
            width: parent.width
            controller: controller
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }

        TimelinePanel {
            width: parent.width
            controller: controller
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }

        // --- the status line ---------------------------------------------------
        // What the user needs to navigate, not what the developer needs to
        // debug: where they are in the animation, the zoom and the frame rate.
        RowLayout {
            width: parent.width
            height: 26
            spacing: Theme.spaceM

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.border
            }

            Text {
                id: statusText
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spaceM
                elide: Text.ElideRight
                text: {
                    if (toastText !== "") return toastText
                    return controller.statusText
                }
                color: toastText !== "" ? Theme.accent : Theme.textTertiary
                font.pixelSize: Theme.fontXS
            }

            Text {
                visible: controller.colourPending
                text: "colouring\u2026"
                color: Theme.flag
                font.pixelSize: Theme.fontXS
            }

            Text {
                text: "fps"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontXS
            }
            AppSpinBox {
                Layout.preferredWidth: 72
                from: 1
                to: 120
                value: controller.framerate
                onValueModified: controller.setFramerate(value)
                ToolTip.visible: hovered
                ToolTip.text: "Frames per second"
            }

            AppToolButton {
                small: true
                Layout.rightMargin: Theme.spaceXS
                text: "Fit"
                icon.name: "zoom-fit-best"
                display: AbstractButton.IconOnly
                onClicked: canvas.fitToCanvas()
                ToolTip.text: "Fit the canvas to the window (0)"
            }

            Item {
                Layout.rightMargin: Theme.spaceM
                Layout.preferredHeight: 26
                implicitWidth: zoomText.implicitWidth

                Text {
                    id: zoomText
                    anchors.centerIn: parent
                    text: controller.zoomPercent + "%"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontXS
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    ToolTip.visible: containsMouse
                    ToolTip.text: "Zoom \u00b7 1 fits the canvas to the window, Shift+0 fits the drawing"
                }
            }
        }
    }

    // --- toast ---------------------------------------------------------------
    property string toastText: ""
    Timer {
        id: toastTimer
        interval: 4000
        onTriggered: window.toastText = ""
    }

    Connections {
        target: controller
        function onStatusMessage(message) {
            window.toastText = message
            toastTimer.restart()
        }
        function onSaveFileDialogRequested() {
            saveFileDialog.open()
        }
        function onOpenFolderDialogRequested() {
            openFileDialog.open()
        }
        function onExportDialogRequested() {
            exportDialog.open()
        }
        function onSceneSettingsRequested() {
            sceneSettingsDialog.present(controller.framerate,
                                        controller.sceneWidth,
                                        controller.sceneHeight,
                                        controller.sceneLength)
        }
        function onLeaveDecisionRequested(question) {
            leaveDialog.question = question
            leaveDialog.open()
        }
        function onCloseRequested() {
            window.closeApproved = true
            window.close()
        }
        function onExportProgress(done, total) {
            window.exporting = true
            window.exportTotal = total
            window.exportDone = done
        }
        function onExportFinished(ok, message) {
            window.exporting = false
            if (!ok && message !== "") window.toastText = "Export failed: " + message
            else window.toastText = "Exported"
            toastTimer.restart()
        }
    }

    property bool exporting: false
    property int exportTotal: 1
    property int exportDone: 0

    // --- wiring the canvas up ------------------------------------------------
    Component.onCompleted: controller.attachCanvas(canvas)

    // --- the shortcuts ---------------------------------------------------------
    // Space and Z are not here: they are held modifiers, and the canvas takes
    // them app-wide itself (see CanvasView::eventFilter).
    Shortcut { sequence: "Ctrl+N"; onActivated: controller.newProject() }
    Shortcut { sequence: "Ctrl+O"; onActivated: controller.openProject() }
    Shortcut { sequence: "Ctrl+S"; onActivated: controller.saveProject() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: controller.saveProjectAs() }
    Shortcut { sequence: "Ctrl+E"; onActivated: controller.exportSequences() }
    Shortcut { sequence: "Ctrl+Z"; onActivated: controller.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: controller.redo() }
    // Global single-key shortcuts are gated: they do not fire while a text
    // field owns focus, so renaming a layer or typing a number cannot trigger
    // canvas/timeline verbs.
    Shortcut { sequence: "Enter"; enabled: !window.textEditing(); onActivated: controller.togglePlayback() }
    Shortcut { sequence: "Left"; enabled: !window.textEditing(); onActivated: controller.stepFrame(-1) }
    Shortcut { sequence: "Right"; enabled: !window.textEditing(); onActivated: controller.stepFrame(1) }
    Shortcut { sequence: "Up"; enabled: !window.textEditing(); onActivated: controller.stepDrawing(-1) }
    Shortcut { sequence: "Down"; enabled: !window.textEditing(); onActivated: controller.stepDrawing(1) }
    Shortcut { sequence: "Insert"; enabled: !window.textEditing(); onActivated: controller.insertDrawing() }
    Shortcut { sequence: "Ctrl+D"; onActivated: controller.duplicateDrawing() }
    Shortcut { sequence: "Delete"; enabled: !window.textEditing(); onActivated: controller.deleteDrawing() }
    Shortcut { sequence: "+"; enabled: !window.textEditing(); onActivated: controller.holdLonger() }
    Shortcut { sequence: "-"; enabled: !window.textEditing(); onActivated: controller.holdShorter() }
    Shortcut { sequence: "B"; enabled: !window.textEditing(); onActivated: controller.setTool(AppController.Brush) }
    Shortcut { sequence: "E"; enabled: !window.textEditing(); onActivated: controller.setTool(AppController.Eraser) }
    Shortcut { sequence: "["; enabled: !window.textEditing(); onActivated: controller.nudgeBrushRadius(1 / 1.25) }
    Shortcut { sequence: "]"; enabled: !window.textEditing(); onActivated: controller.nudgeBrushRadius(1.25) }
    Shortcut { sequence: "1"; enabled: !window.textEditing(); onActivated: canvas.resetView() }
    Shortcut { sequence: "0"; enabled: !window.textEditing(); onActivated: canvas.fitToCanvas() }
    Shortcut { sequence: "Shift+0"; enabled: !window.textEditing(); onActivated: canvas.fitToDrawing() }
    Shortcut { sequence: "Ctrl+/"; onActivated: shortcutPalette.open() }

    function textEditing() {
        const focused = window.activeFocusItem
        if (!focused) return false
        return focused.hasOwnProperty("cursorPosition") ||
               focused instanceof TextInput ||
               focused instanceof TextEdit
    }

    // Closing goes through the controller: the unsaved-changes question is
    // asked exactly like New and Open ask it. The controller answers via
    // leaveDecisionRequested, and once it is ready it says closeRequested.
    property bool closeApproved: false
    onClosing: (close) => {
        if (!closeApproved) {
            close.accepted = false
            controller.requestClose()
        }
    }
}
