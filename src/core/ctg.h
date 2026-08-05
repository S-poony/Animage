// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ctg_fill.h"
#include "document.h"
#include "lazybrush.h"
#include "scribble.h"

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
    // Below this a pixel is not scribbled. Shared with the brush, which rounds
    // to it rather than writing anything softer -- see scribble.h.
    float scribble_alpha_threshold = kScribbleAlphaThreshold;

    // Solve at this fraction of full size. The plan's answer to interactivity
    // is a coarse pass while the pen is down and a full one when it lifts.
    int downscale = 1;

    LazyBrushOptions lazybrush;
};

// What the whole-track audit solves at.
//
// Deliberately coarse. The verdict is "did this mark fill a region or only
// itself", which is a judgement about areas and survives a blocky answer; the
// cost is what has to survive being paid once per drawing after every edit. A
// drawing solved at eight is roughly a sixty-fourth of the grid it would be at
// one, and the pass is what makes a flag mean "go and look at drawing 34"
// rather than "you are standing on a bad drawing".
inline CtgSettings auditSettings() {
    CtgSettings settings;
    settings.downscale = 8;
    return settings;
}


// Everything a fill depends on, worked out without working the fill out.
struct CtgInputs {
    std::uint64_t hash = 0;
    bool valid = false;      // there is a CTG layer here with marks to show
    bool inherited = false;  // ...and they were made on another drawing
    const Cel* scribbles = nullptr;
};

CtgInputs ctgInputsFor(const Document& doc, TrackId track, ImageId image, LayerId layer,
                       const CtgSettings& settings = {});

// Regenerates the fill if the scribbles or any barrier layer have changed since
// last time, and returns it. The cache lives in the document, keyed by
// (drawing, layer).
const CtgFill& ctgFill(Document& doc, TrackId track, ImageId image, LayerId layer,
                       const CtgSettings& settings = {});

// The same solve without the fill. `want_tiles` false stops after the labelling
// and the verdict, skipping a write per pixel of the canvas -- which is two
// million of them at 1080p and does not get cheaper when the solve is coarse.
// Touches no cache, so a coarse pass cannot evict a fine fill somebody is
// looking at.
CtgFill solveCtgFill(const Document& doc, TrackId track, ImageId image, LayerId layer,
                     const CtgSettings& settings, bool want_tiles);

// Judges every drawing of the track, so the timeline can flag the ones worth
// looking at without anybody having visited them.
//
// This is the whole point of the flag: it says which drawings to go and look at,
// and a flag that only appears once you have looked at a drawing has told you
// nothing you did not just find out. Painting a timeline is no more allowed to
// start a max-flow than compositing is, so the pass is run deliberately, from
// outside, and cheaply -- coarse by default, keeping only the numbers, and
// skipping every drawing whose inputs have not moved.
void auditCtgFills(Document& doc, TrackId track, const CtgSettings& settings = auditSettings());

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
