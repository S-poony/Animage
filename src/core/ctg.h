// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ctg_fill.h"
#include "document.h"
#include "lazybrush.h"

namespace animage {

// The CTG layer -- "colours and textures to generate", TVPaint's name for it.
//
// A CTG layer holds scribbles, not colour. Its cel is an ordinary tile grid
// drawn on with the ordinary brush, and what it stores is the marks you made,
// not the fill they imply. The fill is regenerated from the scribbles and the
// line art underneath, and cached until either moves.
//
// Storing the marks rather than the result is what makes the layer worth
// having: change a scribble and the whole region recolours, the file stays
// small next to a flat fill, and there is no way for the fill to fall out of
// step with the drawing, because it is never the thing being kept.
//
// A stroke on a CTG layer is thresholded rather than blended. A scribble is a
// label and a pixel either carries it or it does not; a half-transparent
// scribble pixel would be half a vote for a colour, which means nothing.

struct CtgSettings {
    // Below this the stroke's antialiased rim is not counted as scribbled. A
    // label is not a quantity.
    float scribble_alpha_threshold = 0.5f;

    // Solve at this fraction of full size. The plan's answer to interactivity
    // is a coarse pass while the pen is down and a full one when it lifts.
    int downscale = 1;

    // The longest hole in the line art a fill will jump, in *image* pixels.
    //
    // The solver counts in cells of whatever grid it was handed, and that grid
    // is coarser on a big drawing, so a tolerance stated there would mean
    // different things at different sizes -- the same drawing scanned twice as
    // large would jump gaps twice as wide. Stated here in image pixels and
    // divided by the sampling step on the way in, it means one thing.
    float gap_tolerance_pixels = 32.0f;

    LazyBrushOptions lazybrush;
};


// Regenerates the fill if the scribbles or any barrier layer have changed since
// last time, and returns it. The cache lives in the document, keyed by the
// scribble cel.
const CtgFill& ctgFill(Document& doc, TrackId track, ImageId image, LayerId layer,
                       const CtgSettings& settings = {});

// Builds the barrier the scribbles are cut against: every source layer of the
// CTG layer, flattened, as intensity where 0 is solid line and 1 is bare paper.
//
// More than one source is allowed on purpose. TVPaint cuts against a single
// line-art layer; combining a rough with a clean closes most of the gaps that
// leak from either alone, which is the one improvement over it the design notes
// ask for by name.
std::vector<float> ctgBarrier(const Document& doc, TrackId track, ImageId image,
                              const std::vector<LayerId>& sources, const PixelRect& region,
                              int step = 1);

}  // namespace animage
