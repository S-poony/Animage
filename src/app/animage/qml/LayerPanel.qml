// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Animage

// The right-hand panel, split into two concepts that were one before:
//
//   * the top half is the layer stack: one row per layer with visibility,
//     a thumbnail, a name and a lock, and a compact verb row underneath.
//     Two add buttons sit there -- "+" for a raster layer, palette for a
//     colourize layer -- so adding either kind is one click, no menu.
//
//   * the bottom half is the inspector for the selected layer: Opacity for a
//     raster layer, and the guided Colourize panel -- boundaries, result,
//     and the across-frames options behind progressive disclosure -- for a
//     colourize layer. It scrolls, so a tall colourize panel can never
//     overflow the panel the way the old fixed box could.
Item {
    id: root

    property var controller: null

    width: 300
    // The right panel scrolls; keep a sane strip of canvas at any window width.
    // Main.qml minimum is 1080, rail is 56 → inspector never crowds the canvas.
    property int minimumWidth: 264
    property int maximumWidth: 380

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceM
        anchors.topMargin: Theme.spaceS
        spacing: Theme.spaceS

        // --- the stack header ----------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.panelHeaderHeight
            spacing: Theme.spaceS

            Text {
                text: "Layers"
                font.pixelSize: Theme.fontS
                font.letterSpacing: 1.1
                color: Theme.textTertiary
            }

            Item { Layout.fillWidth: true }
        }

        // --- the stack -----------------------------------------------------
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 100
            clip: true
            spacing: 2
            model: controller ? controller.layersModel : null

            delegate: Rectangle {
                id: row
                width: list.width
                height: 36
                radius: Theme.radiusMedium
                color: {
                    if (index === controller.currentLayerIndex) {
                        if (ma.containsMouse) return Theme.surfaceHover;
                        return Theme.accentSoft;
                    }
                    if (ma.containsMouse) return Theme.surfaceHover;
                    return "transparent";
                }
                border.width: index === controller.currentLayerIndex ? 1 : 0
                border.color: Theme.accentBorder

                // Selection and rename live behind the row's controls, so the
                // eye, the lock and the name field get their own clicks.
                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: controller.selectLayerIndex(index)
                    onDoubleClicked: {
                        controller.selectLayerIndex(index)
                        row.editing = true
                        nameField.forceActiveFocus()
                        nameField.selectAll()
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spaceXS
                    anchors.rightMargin: Theme.spaceXS
                    spacing: Theme.spaceXS

                    // The eye: show or hide the layer in the picture.
                    AppToolButton {
                        small: true
                        text: model.visible ? "Hide" : "Show"
                        icon.name: model.visible ? "view-reveal-symbolic" : "view-conceal-symbolic"
                        icon.source: model.visible ? "qrc:/Animage/animage/icons/view-reveal-symbolic.svg" : "qrc:/Animage/animage/icons/view-conceal-symbolic.svg"
                        display: AbstractButton.IconOnly
                        checkable: true
                        checked: model.visible
                        onClicked: controller.setLayerVisible(index, !model.visible)
                        ToolTip.text: model.visible ? "Hide layer" : "Show layer"
                    }

                    // The thumbnail well. A real per-layer preview is a render
                    // pass the prototype does not have; the well is drawn so a
                    // row reads as a picture, and a colourize layer is marked
                    // so it is never mistaken for a raster layer.
                    Rectangle {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 24
                        radius: Theme.radiusSmall
                        color: "transparent"
                        border.width: 1
                        border.color: model.isCtg ? Theme.flag : Theme.border
                        clip: true

                        Canvas {
                            anchors.fill: parent
                            opacity: model.visible ? 1.0 : 0.35
                            onPaint: {
                                const ctx = getContext("2d");
                                ctx.reset();
                                const s = 4;
                                for (let y = 0; y < height; y += s) {
                                    for (let x = 0; x < width; x += s) {
                                        if (((x / s) + (y / s)) % 2 === 0) {
                                            ctx.fillStyle = Theme.checkerDark;
                                            ctx.fillRect(x, y, s, s);
                                        }
                                    }
                                }
                            }
                        }

                        // A colourize layer's marks are scribbles, not pixels:
                        // a little dash stands in for the fill.
                        Rectangle {
                            visible: model.isCtg
                            anchors.centerIn: parent
                            width: 14
                            height: 3
                            radius: 1.5
                            color: model.carried ? Theme.carried : Theme.textTertiary
                        }
                    }

                    // The name. Double-click to rename.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            Layout.fillWidth: true
                            visible: !row.editing
                            text: model.name
                            elide: Text.ElideRight
                            color: model.visible ? Theme.text : Theme.textDisabled
                            font.pixelSize: Theme.fontM
                            font.weight: index === controller.currentLayerIndex
                                         ? Font.DemiBold : Font.Normal
                        }

                        AppTextField {
                            id: nameField
                            Layout.fillWidth: true
                            visible: row.editing
                            text: model.name
                            height: 18
                            leftPadding: 2
                            rightPadding: 2
                            verticalAlignment: Text.AlignVCenter

                            onEditingFinished: row.commitRename()
                            onActiveFocusChanged: {
                                if (!activeFocus && row.editing) row.commitRename()
                            }
                        }

                        Text {
                            visible: model.isCtg && !row.editing
                            Layout.fillWidth: true
                            text: model.carried ? "carried here" : "colourize layer"
                            color: model.carried ? Theme.carried : Theme.textTertiary
                            font.pixelSize: Theme.fontXS
                        }
                    }

                    // The lock, with its ordinary meaning: locked layers are
                    // not drawn on until they are unlocked.
                    AppToolButton {
                        small: true
                        text: model.locked ? "Unlock" : "Lock"
                        icon.name: model.locked ? "system-lock-screen-symbolic" : "changes-allow-symbolic"
                        icon.source: model.locked ? "qrc:/Animage/animage/icons/system-lock-screen-symbolic.svg" : "qrc:/Animage/animage/icons/changes-allow-symbolic.svg"
                        display: AbstractButton.IconOnly
                        checkable: true
                        checked: model.locked
                        onClicked: controller.setLayerLocked(index, !model.locked)
                        ToolTip.text: model.locked ? "Unlock layer" :
                                        "Lock the layer so it cannot be drawn on"
                    }
                }

                // Renaming, as a small state on the row: double-click the row
                // to edit the name in place.
                property bool editing: false
                function commitRename() {
                    if (!row.editing) return
                    row.editing = false
                    let name = nameField.text.replace(/^\u2190 /, "")
                    if (name.trim() !== "" && name !== model.name)
                        controller.setLayerName(index, name)
                }
            }
        }

        // --- the stack verbs: add, move, delete ----------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXS

            AppToolButton {
                small: true
                text: "Add"
                icon.name: "list-add"
                display: AbstractButton.IconOnly
                onClicked: controller.addLayer()
                ToolTip.text: "New raster layer — draw on it"
            }
            AppToolButton {
                small: true
                text: "Colour"
                icon.name: "applications-graphics"
                display: AbstractButton.IconOnly
                onClicked: controller.addColourLayer()
                ToolTip.text: "New colourize layer — scribble colour hints\nthat fill the regions bounded by the line art below"
            }
            AppToolButton {
                small: true
                text: "Up"
                icon.name: "go-up"
                display: AbstractButton.IconOnly
                enabled: controller.currentLayerIndex > 0
                onClicked: controller.moveCurrentLayer(-1)
                ToolTip.text: "Move the layer up (in front)"
            }
            AppToolButton {
                small: true
                text: "Down"
                icon.name: "go-down"
                display: AbstractButton.IconOnly
                enabled: controller.currentLayerIndex >= 0 &&
                         controller.currentLayerIndex < controller.layerCount - 1
                onClicked: controller.moveCurrentLayer(1)
                ToolTip.text: "Move the layer down (behind)"
            }
            AppToolButton {
                small: true
                text: "Delete"
                icon.name: "user-trash-symbolic"
                icon.source: "qrc:/Animage/animage/icons/user-trash-symbolic.svg"
                display: AbstractButton.IconOnly
                enabled: controller.layerCount > 1
                onClicked: controller.removeCurrentLayer()
                ToolTip.text: "Delete this layer from every frame"
            }

            Item { Layout.fillWidth: true }

            Text {
                text: controller ? (controller.currentLayerIndex + 1) + " / " + controller.layerCount : ""
                color: Theme.textTertiary
                font.pixelSize: Theme.fontXS
            }
        }

        // --- the inspector ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceXS
            Layout.preferredHeight: Theme.panelHeaderHeight
            spacing: Theme.spaceS

            Text {
                text: "Inspector"
                font.pixelSize: Theme.fontS
                font.letterSpacing: 1.1
                color: Theme.textTertiary
            }

            Item { Layout.fillWidth: true }
        }

        // The inspector is a scroller because the colourize panel is tall and
        // the window can be short: content scrolls instead of overflowing.
        Flickable {
            id: inspectorScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80
            clip: true
            contentWidth: width
            contentHeight: inspectorColumn.height

            Column {
                id: inspectorColumn
                width: parent.width
                spacing: Theme.spaceM

                // --- raster layer: just the opacity --------------------------
                ColumnLayout {
                    width: parent.width
                    visible: !controller.onColourLayer
                    spacing: Theme.spaceS

                    Text {
                        text: controller ? (controller.currentLayerIndex >= 0
                                           ? "Raster layer" : "No layer") : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS
                        font.letterSpacing: 1.0
                    }

                    RowLayout {
                        width: parent.width
                        spacing: Theme.spaceS

                        Text {
                            text: "Opacity"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontM
                        }

                        AppSlider {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: controller ? controller.layerOpacity : 100
                            enabled: controller !== null
                            onMoved: controller.setLayerOpacity(value)
                            onPressedChanged: {
                                if (pressed) controller.beginOpacityDrag();
                                else controller.endOpacityDrag();
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: "How transparent this layer is"
                        }

                        Text {
                            text: controller ? controller.layerOpacity + "%" : ""
                            color: Theme.text
                            font.pixelSize: Theme.fontM
                            font.weight: Font.DemiBold
                        }
                    }
                }

                // --- colourize layer: the guided panel ------------------------
                ColumnLayout {
                    width: parent.width
                    visible: controller.onColourLayer
                    spacing: Theme.spaceS

                    Text {
                        text: "COLOURIZE"
                        color: Theme.flag
                        font.pixelSize: Theme.fontS
                        font.letterSpacing: 1.1
                    }

                    Text {
                        text: "Scribble hints — they fill inside the ticked line layers."
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontM
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceXS
                        spacing: Theme.spaceXS
                        Text {
                            text: "Boundaries"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontS
                            font.letterSpacing: 1.0
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: !controller.ctgSourcesModel.empty
                            text: controller.ctgSourcesModel.count === 1
                                  ? "1 line layer"
                                  : controller.ctgSourcesModel.count + " line layers"
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontXS
                        }
                    }

                    // Empty: no raster layers at all — nothing to bound against.
                    Rectangle {
                        visible: controller.ctgSourcesModel.empty
                        Layout.fillWidth: true
                        radius: Theme.radiusSmall
                        color: Theme.surfaceHigh
                        border.width: 1
                        border.color: Theme.border
                        implicitHeight: emptyCol.implicitHeight + Theme.spaceM * 2
                        ColumnLayout {
                            id: emptyCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.spaceM
                            spacing: Theme.spaceXS
                            Text {
                                text: "No line layers yet"
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontM
                                font.weight: Font.DemiBold
                                Layout.fillWidth: true
                            }
                            Text {
                                text: "Add a raster layer to draw your line art, then tick it here."
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontM
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            AppToolButton {
                                Layout.topMargin: Theme.spaceXS
                                text: "Add raster layer"
                                small: true
                                onClicked: controller.addLayer()
                            }
                        }
                    }

                    // Warning: layers exist but none ticked — fill would be unbounded.
                    Text {
                        visible: !controller.ctgSourcesModel.empty && !controller.ctgSourcesModel.hasSelection
                        text: "Nothing ticked — tick a line layer or the fill has no boundaries."
                        color: Theme.danger
                        font.pixelSize: Theme.fontM
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    ListView {
                        visible: !controller.ctgSourcesModel.empty
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 88)
                        clip: true
                        spacing: 1
                        model: controller ? controller.ctgSourcesModel : null
                        delegate: RowLayout {
                            height: 24
                            spacing: Theme.spaceXS
                            AppCheckBox {
                                Layout.preferredWidth: 20
                                checked: model.checked
                                onToggled: controller.setCtgSource(model.layerIndex, checked)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: model.name
                                elide: Text.ElideRight
                                color: model.checked ? Theme.text : Theme.textSecondary
                                font.pixelSize: Theme.fontM
                            }
                        }
                    }

                    // --- result ----------------------------------------------
                    Text {
                        text: "Result"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS
                        font.letterSpacing: 1.0
                        Layout.topMargin: Theme.spaceXS
                    }

                    RowLayout {
                        width: parent.width
                        spacing: Theme.spaceS

                        AppRadioButton {
                            text: "Filled result"
                            checked: !controller.layerShowScribbles
                            onToggled: {
                                if (checked) controller.setLayerShowScribbles(
                                        controller.currentLayerIndex, false)
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: "Show the fill the scribbles produce"
                        }
                        AppRadioButton {
                            text: "Colour hints"
                            checked: controller.layerShowScribbles
                            onToggled: {
                                if (checked) controller.setLayerShowScribbles(
                                        controller.currentLayerIndex, true)
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: "Show the raw colour scribbles instead of the fill"
                        }
                    }

                    // --- across frames: behind progressive disclosure ----------
                    ColumnLayout {
                        width: parent.width
                        spacing: Theme.spaceS

                        AppToolButton {
                            text: acrossOpen.visible ? "Across frames \u25be" : "Across frames \u25b8"
                            Layout.fillWidth: true
                            onClicked: acrossOpen.visible = !acrossOpen.visible
                            ToolTip.text: "How colour hints are carried to frames with none"
                        }

                        ColumnLayout {
                            id: acrossOpen
                            visible: false
                            width: parent.width
                            spacing: Theme.spaceS

                            AppCheckBox {
                                labelText: "Propagate colour hints"
                                Layout.fillWidth: true
                                checked: controller.ctgInherit
                                onToggled: controller.setCtgInherit(checked)
                                ToolTip.visible: hovered
                                ToolTip.text: "Colour the first frame of a run and the whole\n" +
                                              "run is coloured. Off, a frame with no marks of\n" +
                                              "its own is simply empty."
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceS

                                Text {
                                    text: "Direction"
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontM
                                }

                                AppComboBox {
                                    Layout.fillWidth: true
                                    model: ["Earlier \u2192 Later", "Later \u2192 Earlier", "Nearest"]
                                    currentIndex: controller.ctgDirection
                                    enabled: controller.ctgInherit
                                    onActivated: controller.setCtgDirection(currentIndex)
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Earlier\u2192Later: a frame shows the nearest\n" +
                                                  "earlier frame's hints. Later\u2192Earlier: the\n" +
                                                  "nearest later one's. Nearest: whichever is\n" +
                                                  "fewer frames off."
                                }
                            }

                            AppCheckBox {
                                labelText: "Follow line-art motion"
                                Layout.fillWidth: true
                                checked: controller.ctgFollow
                                enabled: controller.ctgInherit
                                onToggled: controller.setCtgFollow(checked)
                                ToolTip.visible: hovered
                                ToolTip.text: "Where hints are carried to a frame, move them by\n" +
                                              "however far the line art has moved between the two."
                            }
                        }
                    }
                }
            }
        }
    }

}
