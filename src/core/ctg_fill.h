// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "tile.h"

namespace animage {

// The regenerated fill of a CTG layer. Separated from ctg.h so that Document
// can hold a cache of these without depending on the solver -- and so that the
// solver can depend on Document, which it must.
//
// This is derived data throughout. Losing it costs a recompute and nothing
// else, which is the property that lets the layer store scribbles rather than
// pixels in the first place.
struct CtgFill {
    TileGrid tiles;
    PixelRect region;  // the area that was solved

    bool valid = false;
    int colours = 0;  // distinct scribble colours found

    // Mixed from the revisions of the scribble cel and every barrier cel. If
    // this still matches, nothing the fill depends on has moved.
    std::uint64_t inputs = 0;
};

}  // namespace animage
