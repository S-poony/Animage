// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPalette>

// Registers every type the QML interface needs: the C++ classes and the two
// design-token singletons, Theme and Icons, which are QML files and so have to
// be registered with a loader rather than a class name.
//
// Called from the application's main() and from the offscreen harness; the two
// must stay identical or the tests would be driving a different interface.
void registerAnimageQmlTypes();

// The interface paints itself from Theme.qml and the App* wrappers, but a few
// Qt Quick Controls (RadioButton, Menu, ToolTip, scrollbars) still take their
// colours from the application palette. Handing them a palette that matches
// Theme.qml is what keeps them dark instead of light OS widgets; the wrappers
// are for the controls where behaviour needs the design tokens more precisely
// than a palette can say them.
QPalette animageDarkPalette();
