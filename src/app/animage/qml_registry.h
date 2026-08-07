// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPalette>

class QGuiApplication;

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
// Theme.qml is what keeps them in step with the design; the wrappers are for
// the controls where behaviour needs the design tokens more precisely than a
// palette can say them.
//
// Theme.qml decides dark vs light from Qt.styleHints.colorScheme and repaints
// itself on an OS flip; the caller hands that same decision in here as a
// scheme so the underlying controls' palette joins the flip. No detection or
// switching happens in C++ -- this function just mirrors a scheme that QML
// already worked out.
QPalette animagePalette(Qt::ColorScheme scheme);

// The dark/light decision, resolved the way the OS actually reports it.
// QStyleHints::colorScheme is authoritative once it is Light or Dark, but on
// startup it can still be Unknown -- the GNOME portal reports it across an
// async DBus round-trip, and offscreen never reports it -- and that is the
// whole reason the app can boot in the wrong theme. Falling through to the
// luminance of the resolved system palette gives a reliable, synchronous
// answer: Qt always has a palette while it constructs the application, and a
// dark theme has a dark base. One small function, called only to seed the
// initial scheme; a later colorSchemeChanged keeps it live.
Qt::ColorScheme resolveColorScheme();
