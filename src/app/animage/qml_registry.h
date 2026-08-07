// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPalette>

class QGuiApplication;

// Registers every type the QML interface needs: the C++ classes and the
// Theme singleton (spacing/type tokens).
//
// Called from the application's main() and from the offscreen harness; the two
// must stay identical or the tests would be driving a different interface.
void registerAnimageQmlTypes();

// Kept for compatibility — native style now uses the system palette directly.
QPalette animagePalette(Qt::ColorScheme scheme);

// The dark/light decision, resolved the way the OS actually reports it.
// QStyleHints::colorScheme is authoritative once it is Light or Dark, but on
// startup it can still be Unknown -- the GNOME portal reports it across an
// async DBus round-trip, and offscreen never reports it -- and that is the
// whole reason the app can boot in the wrong theme. A synchronous portal
// Read (org.freedesktop.appearance color-scheme) is tried next -- it is
// available immediately even when styleHints is still Unknown -- and only if
// the portal is unreachable does the luminance of the resolved system palette
// act as final fallback. One small function, called only to seed the initial
// scheme; a later colorSchemeChanged keeps it live.
Qt::ColorScheme resolveColorScheme();
