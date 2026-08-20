// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ids.h"
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
    TileGrid tiles;

    // What bounds the fill, which today is the canvas.
    //
    // Named for what it is rather than for what it does. The other rectangle
    // here is `solved`, and "region" told the two apart by convention rather
    // than by name -- which is fine while nothing but a test reads it, and not
    // fine once the accessor does.
    //
    // It goes when the canvas stops bounding a fill at all: see
    // docs/colour-without-a-canvas.md, phase 3. Until then this is the thing
    // that says the colour stops at the frame.
    PixelRect canvas;

    // What was actually solved, and how coarsely. Not the same as `canvas`:
    // the solve covers only what has been drawn on and the labels are extended
    // outwards from it, and it is reduced until it fits the budget.
    //
    // Exposed because both are answers in their own right rather than internals
    // -- the step is the resolution of the result, and the rectangle is what
    // the drawing is understood to occupy. A test can then say "erasing a mark
    // put the solve back as it was" exactly, instead of hoping the coarsening
    // it caused happens to move a pixel.
    PixelRect solved;
    int step = 1;

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
// Bounded because a fill covers the canvas at full resolution: 1920x1080 is 135
// tiles, about 17 MB, for every drawing that has been looked at. Keeping them
// all is how playing a coloured shot through once becomes a gigabyte -- which
// only became easy to do when a drawing stopped needing scribbles of its own to
// have a fill.
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
    std::size_t tileCount() const { return tiles_; }

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
    std::size_t tiles_ = 0;
    std::uint64_t stores_ = 0;
    std::uint64_t generation_ = 0;
};

}  // namespace animage
