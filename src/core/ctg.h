// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ctg_fill.h"
#include "ctg_job.h"
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
//
// This header is the half that reads the document, and so belongs to the thread
// that owns it. The solve itself is in ctg_job.h and belongs to no thread at
// all.

// Everything a fill depends on, worked out without working the fill out.
struct CtgInputs {
    std::uint64_t hash = 0;
    bool valid = false;      // there is a CTG layer here with marks to show
    bool inherited = false;  // ...and they were made on another drawing
    ImageId from = kNoId;    // ...that one
    const Cel* scribbles = nullptr;
};

CtgInputs ctgInputsFor(const Document& doc, TrackId track, ImageId image, LayerId layer,
                       const CtgSettings& settings = {});

// Lifts one solve off the document, so it can be run anywhere afterwards --
// including on another thread while this one goes on editing. Cheap: it copies
// tile handles and a few layer properties, and no pixels at all.
//
// `budget` is the cell count the solve will be coarsened to fit, and zero means
// full resolution however large that is. The default is what a solve blocking
// the interface may cost; a solve that is not in anybody's way should ask for
// more.
CtgJob ctgJobFor(const Document& doc, TrackId track, ImageId image, LayerId layer,
                 const CtgSettings& settings = {},
                 long long budget = kInteractiveSolveBudget);

// Regenerates the fill if the scribbles or any barrier layer have changed since
// last time, and returns it. The cache lives in the document, keyed by
// (drawing, layer).
//
// Solves where it stands, so the caller waits for the max-flow. That is what
// the budget is for and it is why a fill asked for this way is capped.
const CtgFill& ctgFill(Document& doc, TrackId track, ImageId image, LayerId layer,
                       const CtgSettings& settings = {});

// The same solve without the cache. `want_tiles` false stops after the
// labelling and the verdict, skipping a write per pixel of the canvas -- which
// is two million of them at 1080p and does not get cheaper when the solve is
// coarse.
CtgFill solveCtgFill(const Document& doc, TrackId track, ImageId image, LayerId layer,
                     const CtgSettings& settings, bool want_tiles);

// One drawing of one layer that has not been judged, or has been judged and has
// moved since.
struct CtgToJudge {
    CtgKey key;
    std::uint64_t inputs = 0;
};

// Which drawings of the track need judging, and forgetting the verdicts of the
// ones that have gone.
//
// The walk, without the solving: both audits do this and only one of them has
// anywhere to solve. Skipping the drawings whose inputs have not moved is what
// makes running the pass over a whole track after every edit affordable -- one
// drawing changed, so one drawing is re-judged and the rest cost a hash apiece.
std::vector<CtgToJudge> ctgAuditWork(Document& doc, TrackId track,
                                     const CtgSettings& settings = auditSettings());

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

}  // namespace animage
