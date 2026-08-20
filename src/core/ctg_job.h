// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "ctg_fill.h"
#include "layer.h"
#include "lazybrush.h"
#include "scribble.h"
#include "tile.h"

namespace animage {

// A CTG solve with the document taken away from it.
//
// The solve is the expensive thing this program does -- a max-flow over a grid,
// about 1.3 s for a megapixel -- and for a long time it ran where the interface
// was waiting, which is why it was capped at roughly 512x512. A cap is not a
// solution, it is a way of losing quality quietly, so the solve had to move to
// another thread; and a solve on another thread may not read the document,
// because the document belongs to the thread that edits it.
//
// So the document is read once, on the thread that owns it, into a CtgJob: the
// scribbles, the layers they are cut against, and the canvas. That copy is
// nearly free, because a tile is immutable once shared and copying a grid
// copies handles. A drawing edited afterwards clones the tiles it touches --
// the same copy-on-write that makes duplicating a drawing free -- so the job
// goes on describing the drawing as it was when the job was taken. Nothing has
// to be locked and nothing is copied that is not written to.

struct CtgSettings {
    // Below this a pixel is not scribbled. Shared with the brush, which rounds
    // to it rather than writing anything softer -- see scribble.h.
    float scribble_alpha_threshold = kScribbleAlphaThreshold;

    // Solve at this fraction of full size. Coarse is what the first answer is
    // for: a blocky fill arriving quickly and a full one replacing it is the
    // shape the plan asks for.
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

// How many cells the first answer may cover.
//
// This was the cap on every solve, because every solve ran where the interface
// was waiting: a max-flow grows faster than its region, so an unbounded one on
// a large drawing does not take a while, it stops the program. It is still
// exactly right for the *first* answer -- 115 ms at this size, so the colour
// follows the pen closely enough to work with -- and it is now only the first
// answer.
inline constexpr long long kInteractiveSolveBudget = 512 * 512;

// And how many the answer that replaces it may cover.
//
// A bound still, but a bound on memory rather than on patience. A max-flow
// keeps something like ninety bytes a cell between the residuals, the search
// trees and the labelling, so four million cells is a few hundred megabytes
// while the solve runs. That is a real amount to ask for and not an absurd one;
// forty million would be absurd.
//
// Four million is 2048x2048, which means a 1080p drawing is solved at full
// resolution and a 4K one at half. It is the drawn area that is bounded, and
// the canvas has nothing to do with it -- so a figure in the corner of a big
// canvas gets full resolution whatever the canvas is, and a drawing with ink far
// off the frame is coarsened by the box round the ink and not by the frame.
inline constexpr long long kFullSolveBudget = 2048LL * 2048;

// Everything a solve reads. No pointer in here names anything the document can
// take away, which is the whole point of it.
struct CtgJob {
    bool valid = false;

    // No canvas. What a fill covers is the drawing and whatever the labels
    // extend to, and the picture that gets exported is somebody else's
    // rectangle -- see docs/colour-without-a-canvas.md, phase 3.

    // Marks and ink, as tiles: shared handles, and a tile is immutable while it
    // is shared.
    //
    // Pixels and nothing else. A barrier is where the ink is, so a source layer
    // contributes the same barrier whether it is shown at half opacity or not
    // shown at all -- which is also the only version of the rule that is true,
    // because a fill is keyed on the cels it was cut against and a layer's
    // opacity is not one of them. Hiding your line art to look at the colours
    // used to make the next fill leak, silently and only once something else
    // caused a re-solve.
    TileGrid scribbles;
    std::vector<TileGrid> sources;  // topmost first, as the layer lists them

    // The same layers on the drawing the marks were made on, when they were
    // made on another one and the layer is set to follow the motion. Empty
    // otherwise, and empty is what says "do not go looking for a shift".
    //
    // This is the whole of what estimating the motion needs, and it is why the
    // estimate can be derived rather than stored: both drawings are here, so
    // the answer can be worked out again whenever it is wanted and thrown away
    // with the fill. A stored transform would be a second thing to keep in step
    // with the drawings, and the first one -- the fill -- is the thing this
    // layer exists in order not to store.
    std::vector<TileGrid> origin_sources;

    CtgSettings settings;

    // The cell budget: the solve is coarsened until it fits. Zero is no bound
    // at all, which is what a solve running off the interface thread may ask
    // for.
    long long budget = kInteractiveSolveBudget;

    // Mixed from the revisions of the scribble cel and every barrier cel, and
    // carried through to the fill. If this still matches, nothing the fill
    // depends on has moved.
    std::uint64_t inputs = 0;

    // Whether the marks were made on this drawing or carried to it.
    bool inherited = false;
};

// Runs one. Touches nothing but the job, so it may run anywhere -- and it takes
// no document, which is not a convenience but the guarantee.
//
// `want_labels` false keeps only the verdict: the max-flow still runs, and what
// is thrown away is the labelling it produced. That used to be a saving on a
// write per pixel of the canvas as well, and is not any more -- there is no
// paint-out, and the labels the solve already holds *are* the fill. What is
// left is memory, which is what a whole-track audit needs: a judgement is a few
// floats, and a 1080p labelling is 4 MB per drawing.
//
// `abandon`, if given, is read as the solve runs: set it and the solve stops as
// soon as it notices and returns an invalid fill. A superseded solve is worth
// nothing, and the pen does not wait.
CtgFill solveCtgJob(const CtgJob& job, bool want_labels,
                    const std::atomic<bool>* abandon = nullptr);

// How much ink covers each cell: every source layer, flattened, where 0 is bare
// paper and 1 is solid.
//
// The reduction is an argument because the two callers want opposite ones, and
// that was the bug. A barrier must not lose a thin line -- a hole in it is a
// fill pouring out -- so it takes the *most* covered pixel in a cell, and a
// line passing anywhere through a cell still stops a cut there. A correlation
// wants the ink to weigh what there is of it, so that half a line under a cell
// counts half; taking the most makes any cell containing any ink read as solid,
// which at a coarse step is most of the drawing.
//
// estimateCtgShift used to build its level zero out of the barrier and inherit
// the barrier's reduction, while every level above it was built by averaging --
// and the code said so, in a comment naming the barrier's rule as the opposite
// one. Only one of them can be right for a correlation.
//
// Only where some source has a tile. That is exact and not a saving bought with
// accuracy: bare paper composites to fully transparent, which is a coverage of
// zero -- the identity for max() and for a sum alike. It matters because an
// empty region used to cost the same as a drawn one, and the cost has to follow
// the ink before the canvas stops bounding the region at all.
enum class InkReduce {
    Most,  // the most covered pixel in the cell
    Mean,  // the average over the cell
};

std::vector<float> ctgInkCoverage(const std::vector<TileGrid>& sources, const PixelRect& region,
                                  int step, InkReduce reduce_with);

// Builds the barrier the scribbles are cut against: every source layer,
// flattened, as intensity where 0 is solid line and 1 is bare paper. One minus
// the coverage above, reduced by the most.
//
// More than one source is allowed on purpose. TVPaint cuts against a single
// line-art layer; combining a rough with a clean closes most of the gaps that
// leak from either alone, which is the one improvement over it the design notes
// ask for by name.
std::vector<float> ctgBarrier(const std::vector<TileGrid>& sources, const PixelRect& region,
                              int step = 1);


// How far the ink moved between one drawing and another.
//
// One translation for the whole drawing, which is rung two of part two of
// docs/scribbles-through-time.md and deliberately the cheapest thing that could
// work: cel animation mostly translates between consecutive drawings, and a
// carried mark does not need to be placed accurately -- it needs most of its
// pixels in the right region, which is a far weaker thing to ask for than
// registration for compositing.
//
// Coarse to fine, on the ink coverage the barrier already produces: an
// exhaustive search at a few dozen cells across, then a refinement per level.
// The answer is in image pixels over `area`, which should be everything either
// drawing has been drawn on.
//
// Zero when there is nothing to match against -- one of the two drawings blank,
// or an area with no ink in it -- which is the same answer as "it did not move"
// and is the right way to fail: carrying unchanged is what happens then, and
// that is the behaviour this is an improvement on rather than a replacement
// for.
CtgShift estimateCtgShift(const std::vector<TileGrid>& from, const std::vector<TileGrid>& to,
                          const PixelRect& area);

}  // namespace animage
