// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ids.h"
#include "scribble.h"
#include "tile.h"

namespace animage {

// How far the drawing moved between where a mark was made and where it is being
// used, in image pixels.
//
// Whole ones. A mark does not have to be placed precisely -- the region takes
// the colour with the greater share of its pixels inside -- so a fraction of a
// pixel is not a quantity any of this needs, and pretending otherwise would be
// asking a registration for accuracy nothing reads.
struct CtgShift {
    int x = 0;
    int y = 0;

    bool isZero() const { return x == 0 && y == 0; }
    friend bool operator==(const CtgShift&, const CtgShift&) = default;
};

// The regenerated fill of a CTG layer. Separated from ctg.h so that Document
// can hold a cache of these without depending on the solver -- and so that the
// solver can depend on Document, which it must.
//
// This is derived data throughout. Losing it costs a recompute and nothing
// else, which is the property that lets the layer store scribbles rather than
// pixels in the first place.
struct CtgFill {
    // The answer, rather than a picture of the answer.
    //
    // One label per solved cell, row-major over `solved` at `step`, and -1
    // where nothing reached. `palette` turns a label into a scribble key -- the
    // same quantised thing the seeding compared -- so between them they are the
    // whole fill, and the colour of any pixel can be worked out when it is
    // asked for. See ctgFillPixel.
    //
    // Two bytes a label and not one. Two bytes is 32767 colours, which no
    // drawing can reach: a mark is written hard, so each stroke lays one flat
    // colour and a drawing would need thirty-two thousand distinct ones. One
    // byte would be 127, and 128 would then need a rule -- a cap that fails is
    // a cap somebody has to design a failure for, and the memory it saves is
    // not needed. A 1080p fill is about 2.07M labels, which is 4 MB against the
    // 17 MB the tiles cost.
    std::vector<std::int16_t> labels;
    std::vector<std::uint32_t> palette;

    // The palette decoded once, so that reading a label is a table lookup
    // rather than three divisions. Kept beside the keys rather than instead of
    // them: a key is what identifies a colour -- it is what the seeding
    // compared and what a future recolour would name -- and this is only what
    // it looks like. Written together, in solveCtgJob, and the same length.
    std::vector<Rgba> palette_colours;

    // What was actually solved, and how coarsely.
    //
    // The only rectangle here, and nothing bounds the fill outside it: the
    // labels are extended outwards from this one and they run as far as
    // anything asks. See ctgFillPixel for why that is exact, and
    // docs/colour-without-a-canvas.md for what used to be here instead.
    //
    // The solve covers what has been drawn on plus a tile of margin, and it is
    // reduced until it fits the budget.
    //
    // Exposed because both are answers in their own right rather than internals
    // -- the step is the resolution of the result, and the rectangle is what
    // the drawing is understood to occupy. A test can then say "erasing a mark
    // put the solve back as it was" exactly, instead of hoping the coarsening
    // it caused happens to move a pixel.
    PixelRect solved;
    int step = 1;

    // How the labels are laid out, which is the one thing `solved` and `step`
    // do not say twice.
    int gridWidth() const { return step > 0 ? (solved.width + step - 1) / step : 0; }
    int gridHeight() const { return step > 0 ? (solved.height + step - 1) / step : 0; }

    // Whether every label on the ring the lookup clamps to is -1.
    //
    // Outside `solved` every answer comes from that ring, so when this is true
    // everything out there is transparent and a span can say so in one test
    // rather than per pixel. That is what buys back the shortcut the compositor
    // is built on -- an area the fill left empty had no tile and was skipped
    // before a channel was read, and a fill with no tiles has no absent one.
    //
    // Exact and not an approximation: it is a fact about the labels that were
    // computed, and on an ordinary drawing the ring is the background.
    bool outside_is_clear = true;

    // The marks this was solved from, travelling with the fill.
    //
    // Handles, so this is nearly free -- and a run of drawings inheriting one
    // scribble cel shares one set of pixels. It is here because reading a fill
    // then needs nothing but the fill: no document, no lookup, nothing for a
    // fourth reader to forget. A mark shows its own pixels whatever the solver
    // decided, and that guarantee now belongs to the same object as the labels
    // it overrules rather than to whoever remembers to ask.
    //
    // Read through `carried_by`, which is the same shift the seeding used. The
    // two are the same statement about the same mark, so a seed read in one
    // place and an override painted in another would put the mark's own pixels
    // somewhere the solver never saw it.
    TileGrid marks;

    // Where those marks are drawn: their own bounds, moved by `carried_by`, to
    // the nearest tile. Derived from the two above and cached here because it
    // is asked per row -- and working it out costs a scan of every pixel of
    // every mark tile, which is a per-gesture price and not a per-row one.
    //
    // Conservative rather than tight: a row inside it may still have no mark on
    // it, and one outside it certainly has none. That is the direction that
    // makes it safe to skip on.
    PixelRect marks_drawn;

    // Below this a pixel is not a mark. Carried rather than assumed, so the
    // accessor is a pure function of what the fill stores -- CtgSettings can
    // set it, and a fill answering by a different threshold from the one it was
    // solved with would disagree with its own seeding.
    float mark_threshold = kScribbleAlphaThreshold;

    // How many cells this solve was allowed. Kept beside what it achieved,
    // because the two answer different questions: `step` is how good this
    // answer is, and this is whether asking again could do better. A fill
    // coarsened by a small budget is worth solving again when there is time; a
    // fill coarsened by the largest budget there is, is finished.
    long long budget = 0;

    bool valid = false;
    int colours = 0;  // distinct scribble colours found

    // Two numbers about how well the worst mark landed. Both were built to
    // drive a flag in the timeline, both are free at solve time, and neither
    // turned out to be able to carry one -- see docs/handover.md, "the flag
    // that had to come out". They are kept because bench_carry reports them and
    // because they are the honest measurements the next attempt has to beat.
    //
    // `confidence` is the fraction of a mark the solver labelled with that
    // mark's own colour, which is what the design notes propose. Over every
    // case in test_ctg it is exactly 1: a seed is only overruled when severing
    // it beats isolating it, and that needs a mark which is nearly all edge.
    //
    // `spread` is how much region a mark won for each pixel of itself. A mark
    // that filled a shape wins many times its own area -- 17, 23, 65, 188
    // measured across the tests -- and a mark carried onto blank paper wins
    // nothing but itself, exactly 1.00. What it cannot do is tell either of
    // those from a mark that snugly fills a small region, which measures 1.96,
    // or from a mark that filled the *wrong* region, which measures higher than
    // a right one.
    float confidence = 1.0f;
    float spread = 1e9f;

    // Whether the marks this was solved from were made on this drawing or
    // carried to it. Nothing about the fill differs -- it is who to tell.
    bool inherited = false;

    // And how far they were moved on the way, when the layer follows the
    // motion. Zero means they were left where they were drawn, whether because
    // nothing moved, because nothing could be matched, or because the layer was
    // told not to.
    CtgShift carried_by;

    // Mixed from the revisions of the scribble cel and every barrier cel. If
    // this still matches, nothing the fill depends on has moved.
    std::uint64_t inputs = 0;

};

// What colour the fill has at one pixel, worked out when it is asked for.
//
// The reference, and the thing tests read. Everything the answer needs is in
// the fill: the labels over `solved`, the palette, the marks and the shift they
// were read through.
//
// Outside `solved` the label is the one on the ring, clamped inwards, and that
// extension is exact rather than an approximation. Outside the drawn area there
// is no line art, so everything out there is one connected stretch of blank
// paper: a cut cannot pass through it and it can only take one label, so
// whatever label reaches the border is the label of everything beyond it.
// Nothing in that argument names a rectangle, and there is no longer one here:
// a shape running off the frame is coloured out there too.
//
// A mark wins over the label wherever it was drawn, at full resolution however
// coarse the solve was. That is what lets somebody scribble into a region too
// narrow for the solve to spread through and still see the colour they asked
// for, and it is what leaves a mark visible on a drawing whose line art has
// gone.
Rgba ctgFillPixel(const CtgFill& fill, int x, int y);

// A run of pixels along one row: `count` of them, starting at `first_x` and
// every `stride` image pixels after it. Answers exactly what ctgFillPixel
// would, one call instead of `count` of them.
//
// This is what the compositor uses and where the constant is won. The coarse
// row is worked out once; a run of samples inside one cell is one label and
// therefore one colour; and the marks are then overwritten a tile at a time
// with the tile lookup hoisted across the run -- the same shape blendLayerRows
// already uses, and for the same reason.
//
// Pure, and it has to be: compositeGrids runs its bands on several threads over
// one pass list, so one fill is read concurrently by all of them. That rules
// out materialising tiles on first touch and keeping them, which would need a
// lock on the path this exists to make faster.
void ctgFillSpan(const CtgFill& fill, int y, int first_x, int stride, int count, Rgba* out);

// Which samples of that span can be anything but transparent.
//
// `[first, first + count)` of the span the same arguments would fill, and
// everything outside it is transparent -- exactly, not probably. Cheap: three
// rectangles and no per-sample work.
//
// This is what gives a fill back the shortcut the compositor is built on. An
// area a fill left empty had no tile, and the tile was skipped before a channel
// was read; a fill has no absent tile, so the compositor asks where the answers
// can be and leaves the rest of the row alone. Without it a colour layer costs
// the area of the viewport rather than the area of the fill, which is what the
// tiles never did.
//
// It can also come back covering the whole span, and that is an answer and not
// a failure: when a label on the ring is a colour, everything outside the solve
// takes it, in every direction and for ever.
struct CtgFillExtent {
    int first = 0;
    int count = 0;
};
CtgFillExtent ctgFillExtent(const CtgFill& fill, int y, int first_x, int stride, int count);

// What a fill belongs to: one drawing, one layer.
//
// It was keyed on the cel holding the scribbles, and that was a bijection for
// exactly as long as one drawing had one scribble cel. A drawing with no
// scribbles of its own now inherits the nearest earlier one's, so a whole run
// shares a cel -- and a run sharing one cache slot does not serve a wrong fill,
// because `inputs` mixes the barrier cels' revisions and those differ per
// drawing. It thrashes: one slot fought over by every drawing in the run,
// re-solved on every frame change, at a tenth of a second each.
struct CtgKey {
    ImageId image = kNoId;
    LayerId layer = kNoId;

    friend bool operator==(const CtgKey&, const CtgKey&) = default;
};

struct CtgKeyHash {
    std::size_t operator()(const CtgKey& key) const {
        std::uint64_t mixed = key.image * 0x9e3779b97f4a7c15ull;
        mixed ^= (key.layer + 0x9e3779b97f4a7c15ull) + (mixed << 6) + (mixed >> 2);
        return static_cast<std::size_t>(mixed);
    }
};

// A bounded store of fills, keeping the ones looked at most recently.
//
// Bounded because a fill is one label per solved cell for every drawing that
// has been looked at -- 4 MB at 1080p, 8 MB at 4K solved at half. Keeping them
// all is how playing a coloured shot through once becomes a gigabyte, which
// only became easy to do when a drawing stopped needing scribbles of its own to
// have a fill.
//
// Counted in bytes, which is the quantity that was meant. It budgeted in tiles
// while a fill was a picture, and there is no picture now; the same lesson as
// "what a band counted in coarse rows really costs" in docs/handover.md.
//
// Eviction costs a recompute and nothing else. That is not a consolation, it is
// the reason this is allowed to be a cache at all: the fill is derived, and the
// layer exists in order not to store it.
class CtgFillCache {
public:
    // References into the store survive an insertion, because the map is
    // node-based and the entry being returned is never the one evicted. A
    // caller may not hold one across a *second* solve, and none does.
    const CtgFill* find(const CtgKey& key) const;
    CtgFill& store(const CtgKey& key, CtgFill fill);

    void clear();

    // How many times the store has been emptied.
    //
    // Emptying it is how everything that a fill depends on but is not keyed on
    // says so -- which sources the layer is cut against, which way marks are
    // carried, and a whole document being replaced by another. None of those
    // move a cel revision, so the hash cannot see them and the cache is thrown
    // away instead.
    //
    // That was enough while a solve finished inside the call that started it.
    // It is not enough when the answer arrives later: a solve started before
    // the sources changed would land afterwards, match the hash, and be
    // installed as though it were current. Anything with a solve in flight
    // records this alongside the hash, so every present and future way of
    // saying "all of that is wrong now" invalidates the answers in the air as
    // well as the ones on the shelf.
    std::uint64_t generation() const { return generation_; }

    std::size_t size() const { return entries_.size(); }

    // What the fills held weigh. The labels and the palette, and not the marks:
    // those are handles shared with the cel and with every other drawing
    // inheriting it, so charging the cache for them would charge it for memory
    // it usually does not cause.
    std::size_t bytes() const { return bytes_; }

    // How many fills have been put in, which is how many solves have happened:
    // ctgFill stores exactly once per solve and returns from the cache
    // otherwise. Exposed because "did that re-solve?" is the question the key
    // exists to answer, and the only honest way to ask it is to count. Timing
    // it would be flaky and a wrong key does not fail, it only gets slow.
    std::uint64_t storeCount() const { return stores_; }

private:
    struct Entry {
        CtgFill fill;
        // Touched by a lookup, so the order reflects what is being looked at
        // and not only what was last solved -- scrubbing back and forth over a
        // few drawings must not evict the ones being scrubbed over.
        mutable std::uint64_t used = 0;
    };

    void evictDownToBudget(const CtgKey& keep);

    std::unordered_map<CtgKey, Entry, CtgKeyHash> entries_;
    mutable std::uint64_t clock_ = 0;
    std::size_t bytes_ = 0;
    std::uint64_t stores_ = 0;
    std::uint64_t generation_ = 0;
};

}  // namespace animage
