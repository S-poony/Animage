// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>

// The colours that are **not** taken from the system palette, and why each one
// is not.
//
// Everything structural in this program is derived from `QPalette` -- the
// timeline's cells, gutters and outlines all come from `Base` and `Highlight`,
// which is what makes them right in a dark theme without a second set of
// numbers. See the palette in timeline_widget.cpp, and the two ways building on
// `Window` went wrong that are recorded there.
//
// These two are the exceptions, and the test they had to pass to be exceptions
// is the same one: **the meaning is not a role a system palette has**, so there
// is nothing to derive them from and a theme cannot be asked what colour it
// would like them to be. "Carried" and "the shot ends here" are facts about
// this program.
//
// They live in a header of their own because each now has a second caller, and
// a colour written twice is a colour that will be changed once.
namespace marks {

// The colour of the colour-through-time machinery: cells whose marks were
// carried from an earlier drawing, and a colour layer's row in the layer panel.
//
// The layer panel is the second caller and is issue #84: a colour layer read as
// an ordinary raster row, so nothing said that a mark made there is a label
// rather than paint. It is the *same* fact as a carried cell, one panel over,
// which is why it is the same blue rather than a new one.
inline const QColor kCarried(0x5b, 0x9c, 0xd6);

// "The shot ends here."
inline const QColor kBoundary(0xd0, 0x45, 0x3c);

}  // namespace marks
