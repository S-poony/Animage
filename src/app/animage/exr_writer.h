// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include "compositor.h"

// Writing one composited frame as OpenEXR.
//
// This is the lossless half of the export. PNG converts on purpose -- sRGB,
// unpremultiplied, 16-bit integer, and about a third of the half values in
// [0,1] thrown away on the way. EXR converts not at all: its convention is
// linear light with premultiplied ("associated") alpha stored as half, which is
// exactly what a `Framebuffer` holds, so the bits that go in are the bits that
// come out.
//
// **The same frame as PNG and as EXR does not contain the same numbers**, and
// that is the point rather than a bug. Anything comparing them channel by
// channel will conclude one is broken.
//
// Kept apart from export_sequence.cpp because the implementation file is where
// tinyexr is compiled, and tinyexr is 385 KB of somebody else's C++ that has to
// be built with the warnings turned off. Nothing else should have to be.
namespace exporting {

// Writes `frame` to `path` as a single-part scanline EXR: four HALF channels
// named A, B, G, R -- alphabetical, which is the order readers expect -- ZIP
// compressed, with the data window equal to the display window.
//
// No conversion of any kind is applied. The caller has already decided what the
// pixels mean; see the note above about what that costs in comparability.
bool writeExrFrame(const animage::Framebuffer& frame, const QString& path, QString* error);

}  // namespace exporting
