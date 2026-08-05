// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ids.h"
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
    PixelRect region;  // the area the fill covers: the canvas

    // What was actually solved, and how coarsely. Not the same as `region`:
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

    bool valid = false;
    int colours = 0;  // distinct scribble colours found

    // The fraction of the worst mark that the solver labelled with that mark's
    // own colour. This is the signal the design notes propose, and it does not
    // work: over every case in test_ctg it is exactly 1, because a seed is only
    // overruled when severing it beats isolating it and that needs a mark which
    // is nearly all edge. Kept because it is free and it is the honest reading
    // of "did the solver disagree with you", and recorded as a dead end so it
    // is not derived a third time.
    float confidence = 1.0f;

    // How much region the worst mark won for each pixel of itself.
    //
    // This is the one that separates, and it separates by a lot. A mark that
    // filled a shape wins many times its own area -- 17, 23, 65, 188 measured
    // across the tests -- while a mark carried onto blank paper wins nothing
    // but itself, because with no line art to follow the cut simply hugs the
    // seed. That case measures exactly 1.00.
    float spread = 1e9f;

    // Whether the marks this was solved from were made on this drawing or
    // carried to it. Nothing about the fill differs -- it is who to tell.
    bool inherited = false;

    // Mixed from the revisions of the scribble cel and every barrier cel. If
    // this still matches, nothing the fill depends on has moved.
    std::uint64_t inputs = 0;

    // Worth going to look at: built from marks made on some other drawing, and
    // a good part of one of them did not land in the region it was asking for.
    //
    // Only for carried marks, and that restriction is the point of the flag
    // rather than a hedge. A mark you made on the drawing in front of you
    // disagreeing with the solver is something you can watch happen; the same
    // mark carried to the ninety drawings after it is not, because you are not
    // looking at them. Flagging the one you can already see is how you teach
    // somebody to ignore flags.
    //
    // This does not wait for scribbles that move. Carrying a mark unchanged to
    // a drawing whose line art has moved under it is precisely how it ends up
    // half in the wrong region -- moving it is what would reduce that, not what
    // causes it -- so the flag is most useful in exactly the state where marks
    // are carried and nothing moves them.
    bool suspect() const;
};

// Below this a carried mark is reported as having filled nothing.
//
// This is a threshold, and this codebase distrusts those for good reasons --
// but the reason is that a hidden threshold moves the surprise when it changes
// *behaviour*, and this one changes nothing. The fill is identical either way.
// It decides only whether somebody is told to go and look, which the design
// notes allow by name.
//
// One and a half, and it is measured rather than derived, which is the honest
// description of it. Marks that filled a shape came out at 17, 23, 65 and 188;
// the tightest legitimate one at 1.82, which is a scribble made outside the
// line art where the rim cannot be overruled and so keeps roughly its own
// pixels; and a mark carried off its shape at exactly 1.00. Half way between
// the last two, on eight samples. If it starts crying wolf, this is the number,
// and the numbers to re-argue it are in the comment above `spread`.
inline constexpr float kCtgSpreadFloor = 1.5f;

inline bool CtgFill::suspect() const {
    return valid && inherited && colours > 0 && spread < kCtgSpreadFloor;
}

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
};

}  // namespace animage
