// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ids.h"
#include "tile.h"

namespace animage {

// Recorded before a tile is replaced, so undo can put the old one back. A
// stroke that crosses four tiles costs four pointers, not four megabytes.
struct TileSnapshot {
    CelId cel = kNoId;
    TileCoord coord;
    TileRef tile;  // null means the tile was absent
};

// Collects the tiles a single command displaced. Each (cel, tile) is recorded
// once: a stroke that scrubs back and forth over one tile must keep the state
// from before the stroke, not from the previous dab.
class TileJournal {
public:
    bool alreadyRecorded(CelId cel, TileCoord c) const { return seen_.contains(key(cel, c)); }

    void record(CelId cel, TileCoord c, TileRef previous) {
        if (!seen_.insert(key(cel, c)).second) return;
        entries_.push_back(TileSnapshot{cel, c, std::move(previous)});
    }

    bool empty() const { return entries_.empty(); }
    std::size_t size() const { return entries_.size(); }
    const std::vector<TileSnapshot>& entries() const { return entries_; }

    std::vector<TileSnapshot> take() {
        seen_.clear();
        return std::exchange(entries_, {});
    }

private:
    static std::uint64_t key(CelId cel, TileCoord c) {
        const std::uint64_t xy = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.x)) << 32) |
                                 static_cast<std::uint32_t>(c.y);
        return cel * 0x9e3779b97f4a7c15ull ^ xy;
    }

    std::vector<TileSnapshot> entries_;
    std::unordered_set<std::uint64_t> seen_;
};

// The only object in the model that holds pixels.
class Cel {
public:
    explicit Cel(CelId id) : id_(id) {}

    // Copy for duplicate-and-paste. The tile handles are shared, so the copy is
    // nearly free; the first write to either side clones the tiles it touches
    // and the two cels become genuinely independent.
    Cel(CelId id, TileGrid grid) : id_(id), grid_(std::move(grid)) {}

    CelId id() const { return id_; }

    const TileGrid& tiles() const { return grid_; }

    // Number of Images that map a layer to this cel. An exposed image counts
    // once, not once per slot: the slots list may repeat an ImageId freely.
    int imageRefcount() const { return image_refcount_; }
    void addImageRef() { ++image_refcount_; }
    void releaseImageRef() { --image_refcount_; }

    Rgba pixel(int px, int py) const { return grid_.pixel(px, py); }

    // Returns a tile that is safe to write to, cloning it first if anyone else
    // can still see the current one.
    Tile* writableTile(TileCoord c, TileJournal& journal);

    // Undo/redo. Swaps the stored tile with the live one, which makes the
    // operation its own inverse.
    void swapTile(TileCoord c, TileRef& other);

private:
    CelId id_;
    TileGrid grid_;
    int image_refcount_ = 0;
};

}  // namespace animage
