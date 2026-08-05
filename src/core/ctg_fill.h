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
    PixelRect region;  // the area that was solved

    bool valid = false;
    int colours = 0;  // distinct scribble colours found

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
