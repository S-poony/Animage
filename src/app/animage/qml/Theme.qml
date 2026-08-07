// SPDX-License-Identifier: GPL-3.0-or-later
//
// The design tokens for the whole interface: colour, spacing, shape, type and
// motion in one place. No component hardcodes a value that belongs here; a
// redesign is a change to this file.
pragma Singleton

import QtQuick

QtObject {
    // --- surfaces ----------------------------------------------------------
    // Kept exactly as the original black theme — user asked to keep the
    // black background and only desaturate the orange.
    readonly property color background:   "#0e0f13"  // behind everything
    readonly property color surface:     "#17181e"  // panels
    readonly property color surfaceHigh: "#20222b"  // raised: toolbars, cards
    readonly property color surfaceHover:"#272a35"
    readonly property color canvasWell:  "#101014"  // the well the canvas sits in
    readonly property color overlay:     "#0e0f13"

    // --- lines -------------------------------------------------------------
    readonly property color border:       "#2b2e38"
    readonly property color borderStrong: "#3d4250"
    readonly property color borderFocus:  "#d87854"

    // --- text --------------------------------------------------------------
    readonly property color text:        "#e8eaf0"
    readonly property color textSecondary: "#9aa1b0"
    readonly property color textTertiary:  "#676e80"
    readonly property color textDisabled:  "#4a4f5c"
    readonly property color textOnAccent:  "#1c120c"

    // --- accent ------------------------------------------------------------
    // Desaturated clay orange — same family as #ff7847 but lower chroma so
    // Export, selections and slider thumbs don't shout against the black.
    readonly property color accent:      "#d87854"
    readonly property color accentHover: "#e1835e"
    readonly property color accentDown:  "#c56545"
    readonly property color accentSoft:  "#d878541a"
    readonly property color accentBorder:"#d8785452"
    readonly property color accentFocus: "#a85e45"

    // --- semantic ----------------------------------------------------------
    readonly property color carried:     "#5b9cd6"  // marks carried to a drawing
    readonly property color flag:        "#b98248"  // desaturated — was #e07a1e
    readonly property color ok:          "#45d483"
    readonly property color danger:      "#ff5d5d"

    // --- checker / scrim (was hardcoded in LayerPanel/ColorSwatch/Main) ---
    readonly property color checkerDark:  "#2a2d36"
    readonly property color checkerLight: "#33363f"
    readonly property color scrim:        "#99000000"

    // --- shape -------------------------------------------------------------
    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12

    // --- spacing -----------------------------------------------------------
    readonly property int spaceXXS: 2
    readonly property int spaceXS: 4
    readonly property int spaceS: 8
    readonly property int spaceM: 12
    readonly property int spaceL: 16
    readonly property int spaceXL: 24

    // --- type --------------------------------------------------------------
    readonly property int fontXS: 10
    readonly property int fontS: 11
    readonly property int fontM: 12
    readonly property int fontL: 13
    readonly property int fontXL: 15
    readonly property int fontTitle: 17

    // --- motion ------------------------------------------------------------
    readonly property int durationFast: 100
    readonly property int duration: 160
    readonly property int durationSlow: 260

    // --- component sizes ---------------------------------------------------
    // One place for the measurements every component shares; a view that needs
    // a different number is either a new token here or a bug.
    readonly property int toolButton: 36        // icon-only tool buttons, square
    readonly property int iconButton: 24        // compact inline icon buttons
    readonly property int controlHeight: 32     // text buttons and controls
    readonly property int iconSize: 18          // icon inside a text button
    readonly property int iconSizeTool: 20      // icon inside an icon-only tool button
    readonly property int iconSizeSmall: 14     // icon inside a compact 24px button
    readonly property int panelHeaderHeight: 28
    readonly property int inputMinWidth: 96

    // A shadow used to lift floating things (dialogs, the shortcut palette)
    // off the surface. The scene graph cannot spread shadows, so these are
    // painted with a translucent black that reads as depth on dark surfaces.
    readonly property var elevation: [
        "0 1 2 0 rgba(0,0,0,0.3)",
        "0 2 6 0 rgba(0,0,0,0.4)"
    ]
}
