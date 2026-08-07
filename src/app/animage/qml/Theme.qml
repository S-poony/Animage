// SPDX-License-Identifier: GPL-3.0-or-later
//
// The design tokens for the whole interface: colour, spacing, shape, type and
// motion in one place. No component hardcodes a value that belongs here; a
// redesign is a change to this file.
pragma Singleton

import QtQuick
import QtQuick.Controls

QtObject {
    // `dark` is kept for offscreen harness compatibility (see harness.cpp)
    // but colors now follow the native platform palette via SystemPalette,
    // not custom brown/orange tokens.
    property bool dark: typeof animageDark !== "undefined"
                        ? animageDark
                        : Qt.styleHints.colorScheme !== Qt.ColorScheme.Light

    readonly property SystemPalette sys: SystemPalette { colorGroup: SystemPalette.Active }

    // --- native palette — all colors from SystemPalette, no custom brown ---
    readonly property color background:   sys.window
    readonly property color surface:      sys.window
    readonly property color surfaceHigh:  sys.base
    readonly property color surfaceHover: sys.alternateBase
    readonly property color canvasWell:   sys.base
    readonly property color overlay:      sys.window

    // --- lines -------------------------------------------------------------
    readonly property color border:       sys.mid
    readonly property color borderStrong: sys.dark
    readonly property color borderFocus:  sys.highlight

    // --- text --------------------------------------------------------------
    readonly property color text:           sys.windowText
    readonly property color textSecondary:  sys.text
    readonly property color textTertiary:   sys.placeholderText
    readonly property color textDisabled:   Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.4)
    readonly property color textOnAccent:   sys.highlightedText

    // --- accent — platform highlight, not custom brown ---------------------
    readonly property color accent:       sys.highlight
    readonly property color accentHover:  Qt.lighter(sys.highlight, 1.08)
    readonly property color accentDown:   Qt.darker(sys.highlight, 1.08)
    readonly property color accentSoft:   Qt.rgba(sys.highlight.r, sys.highlight.g, sys.highlight.b, 0.12)
    readonly property color accentBorder: Qt.rgba(sys.highlight.r, sys.highlight.g, sys.highlight.b, 0.24)
    readonly property color accentFocus:  Qt.rgba(sys.highlight.r, sys.highlight.g, sys.highlight.b, 0.40)

    // --- semantic — derived from highlight, not custom blue/orange ----------
    readonly property color carried:     sys.highlight
    readonly property color flag:        Qt.darker(sys.highlight, 1.15)
    readonly property color ok:          sys.highlight
    readonly property color danger:      "#d64545"

    // --- checker / scrim — neutral, palette-derived ------------------------
    readonly property color checkerDark:  Qt.darker(sys.base, 1.06)
    readonly property color checkerLight: sys.base
    readonly property color scrim:        "#66000000"

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
    // painted with a translucent black that reads as depth on either scheme.
    readonly property var elevation: [
        "0 1 2 0 rgba(0,0,0,0.3)",
        "0 2 6 0 rgba(0,0,0,0.4)"
    ]
}
