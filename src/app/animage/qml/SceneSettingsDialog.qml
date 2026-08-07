// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Animage

// Everything that belongs to the scene rather than to a track or a drawing:
// the framerate, and the canvas.
//
// The canvas is expressed twice over — a ratio and a resolution, or a width
// and a height — and each keeps the other true through SceneSettingsModel.
// The preview writes to the scene directly, around the history, so nothing is
// recorded until the dialog is accepted and cancelling puts back exactly what
// was there.
AppDialog {
    id: dialog

    property var controller: null
    property var model: null
    property int originalFramerate: 24
    property int originalWidth: 1920
    property int originalHeight: 1080

    title: "Scene settings"

    // Preview is debounced: dragging the resolution slider would otherwise ask
    // for a colour solve per tick.
    Timer {
        id: previewTimer
        interval: 150
        onTriggered: dialog.pushPreview()
    }

    function pushPreview() {
        controller.previewSceneSettings(model.framerate, model.width, model.height)
    }

    function present(fps, w, h) {
        originalFramerate = fps
        originalWidth = w
        originalHeight = h
        model.setAll(fps, w, h)
        open()
    }

    onOpened: pushPreview()
    onRejected: controller.restoreSceneSettings(originalFramerate, originalWidth, originalHeight)
    onAccepted: controller.commitSceneSettings(model.framerate, model.width, model.height)

    contentItem: ColumnLayout {
        width: dialog.availableWidth
        spacing: Theme.spaceM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceL

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXS

                Text {
                    text: "Framerate"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                }
                AppSpinBox {
                    Layout.fillWidth: true
                    from: 1
                    to: 120
                    value: model.framerate
                    onValueModified: {
                        model.setFramerate(value)
                        previewTimer.restart()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXS

                Text {
                    text: "Aspect ratio"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                }
                AppComboBox {
                    Layout.fillWidth: true
                    model: dialog.model ? dialog.model.aspectNames : []
                    currentIndex: dialog.model ? dialog.model.aspectIndex : 0
                    onActivated: {
                        if (dialog.model) {
                            dialog.model.setAspectIndex(currentIndex)
                            previewTimer.restart()
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceL

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXS
                Text {
                    text: "Ratio width"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                }
                AppSpinBox {
                    Layout.fillWidth: true
                    from: 1
                    to: 1000
                    value: Math.round(model.ratioWidth * 100) / 100
                    onValueModified: {
                        model.setRatioWidth(value)
                        previewTimer.restart()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXS
                Text {
                    text: "Ratio height"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                }
                AppSpinBox {
                    Layout.fillWidth: true
                    from: 1
                    to: 1000
                    value: Math.round(model.ratioHeight * 100) / 100
                    onValueModified: {
                        model.setRatioHeight(value)
                        previewTimer.restart()
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXS

            Text {
                text: "Resolution"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
            }
            AppSlider {
                Layout.fillWidth: true
                from: model.minResolution
                to: model.maxResolution
                stepSize: 16
                value: model.resolution
                onMoved: {
                    model.setResolution(value)
                    previewTimer.restart()
                }
            }
            Text {
                text: model.height + " \u00d7 " + model.width + " px"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontXS
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceL

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXS
                Text {
                    text: "Width"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                }
                AppSpinBox {
                    Layout.fillWidth: true
                    from: 16
                    to: 16384
                    value: model.width
                    onValueModified: {
                        model.setWidth(value)
                        previewTimer.restart()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXS
                Text {
                    text: "Height"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                }
                AppSpinBox {
                    Layout.fillWidth: true
                    from: 16
                    to: 16384
                    value: model.height
                    onValueModified: {
                        model.setHeight(value)
                        previewTimer.restart()
                    }
                }
            }
        }

        Text {
            text: "The canvas is what gets exported and what a colour fill is bounded by."
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
                text: "Apply"
                iconName: "check"
                highlighted: true
                onClicked: dialog.accept()
            }
        }
    }
}
