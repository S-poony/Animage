// SPDX-License-Identifier: GPL-3.0-or-later
//
// The design tokens for the whole interface: colour, spacing, shape, type and
// motion in one place. No component hardcodes a value that belongs here; a
// redesign is a change to this file.
pragma Singleton

import QtQuick

QtObject {
    // --- the scheme ----------------------------------------------------------
    // The whole palette follows the OS. Theme.qml binds to `animageDark`
    // -- a boolean top-level property that main() seeds from a synchronous
    // resolveColorScheme() and brings up to date on every colorSchemeChanged
    // (see main.cpp). Binding here, rather than to Qt.styleHints.colorScheme
    // directly, is the startup fix: Qt reports that value as Unknown (or a
    // stale Light) before the async theme portal answers, and the palette-
    // luminance fallback gets the first frame right. Every token below binds
    // to `dark`, so the whole interface re-tints in place on an OS flip.
    //
    // `dark` is not readonly on purpose: the offscreen platform cannot be
    // told a color scheme, so the screenshot harness overrides it to render
    // the light theme for inspection. The application never touches it.
    property bool dark: typeof animageDark !== "undefined"
                        ? animageDark
                        : Qt.styleHints.colorScheme !== Qt.ColorScheme.Light

    // --- the light palette ---------------------------------------------------
    // The mirror image of the dark tokens below, picked whenever the OS asks
    // for light. Separate named properties keep the active palette below one
    // dark-or-light ternary per token instead of two palettes of ternaries.
    readonly property color backgroundLight:   "#eef0f4"  // behind everything
    readonly property color surfaceLight:     "#f7f8fa"  // panels
    readonly property color surfaceHighLight: "#ffffff"  // raised: toolbars, cards
    readonly property color surfaceHoverLight:"#e3e6ec"
    readonly property color canvasWellLight:  "#e9ebf0"  // the well the canvas sits in
    readonly property color overlayLight:     "#eef0f4"

    readonly property color borderLight:       "#d8dce4"
    readonly property color borderStrongLight: "#b9c0cc"
    readonly property color borderFocusLight:  "#8a5a44"

    readonly property color textLight:        "#1c2028"
    readonly property color textSecondaryLight: "#525a68"
    readonly property color textTertiaryLight:  "#7d8494"
    readonly property color textDisabledLight:  "#a8aebd"
    readonly property color textOnAccentLight:  "#f7efea"

    // The same orange family as the dark accent, stepped down a few shades so
    // it keeps its contrast on white.
    readonly property color accentLight:      "#a06a52"
    readonly property color accentHoverLight: "#8f5c46"
    readonly property color accentDownLight:  "#b57f68"
    readonly property color accentSoftLight:  "#1aa06a52"
    readonly property color accentBorderLight:"#3da06a52"
    readonly property color accentFocusLight: "#c99a83"

    readonly property color carriedLight:     "#3f7fc4"
    readonly property color flagLight:        "#a06a2e"
    readonly property color okLight:          "#2f9e5e"
    readonly property color dangerLight:      "#d64545"

    readonly property color checkerDarkLight:  "#d3d7df"
    readonly property color checkerLightLight: "#e6e9ee"
    readonly property color scrimLight:        "#66000000"

    // --- the active palette ---------------------------------------------------
    // Dark tokens keep their original names and values -- the black design the
    // app shipped with. The OS scheme flips them to the light twins above.
    readonly property color background:   dark ? "#0e0f13" : backgroundLight
    readonly property color surface:      dark ? "#17181e" : surfaceLight
    readonly property color surfaceHigh:  dark ? "#20222b" : surfaceHighLight
    readonly property color surfaceHover: dark ? "#272a35" : surfaceHoverLight
    readonly property color canvasWell:   dark ? "#101014" : canvasWellLight
    readonly property color overlay:      dark ? "#0e0f13" : overlayLight

    // --- lines -------------------------------------------------------------
    readonly property color border:       dark ? "#2b2e38" : borderLight
    readonly property color borderStrong: dark ? "#3d4250" : borderStrongLight
    readonly property color borderFocus:  dark ? "#b07a62" : borderFocusLight

    // --- text --------------------------------------------------------------
    readonly property color text:           dark ? "#e8eaf0" : textLight
    readonly property color textSecondary:  dark ? "#9aa1b0" : textSecondaryLight
    readonly property color textTertiary:   dark ? "#676e80" : textTertiaryLight
    readonly property color textDisabled:   dark ? "#4a4f5c" : textDisabledLight
    readonly property color textOnAccent:   dark ? "#1c120c" : textOnAccentLight

    // --- accent ------------------------------------------------------------
    readonly property color accent:      dark ? "#b07a62" : accentLight
    readonly property color accentHover: dark ? "#bd8a73" : accentHoverLight
    readonly property color accentDown:  dark ? "#9c6b56" : accentDownLight
    readonly property color accentSoft:  dark ? "#b07a6214" : accentSoftLight
    readonly property color accentBorder:dark ? "#b07a623d" : accentBorderLight
    readonly property color accentFocus: dark ? "#7a5745" : accentFocusLight

    // --- semantic ----------------------------------------------------------
    readonly property color carried:     dark ? "#5b9cd6" : carriedLight
    readonly property color flag:        dark ? "#b98248" : flagLight
    readonly property color ok:          dark ? "#45d483" : okLight
    readonly property color danger:      dark ? "#ff5d5d" : dangerLight

    // --- checker / scrim ---------------------------------------------------
    readonly property color checkerDark:  dark ? "#2a2d36" : checkerDarkLight
    readonly property color checkerLight: dark ? "#33363f" : checkerLightLight
    readonly property color scrim:        dark ? "#99000000" : scrimLight

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
