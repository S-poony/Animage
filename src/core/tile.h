// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "half.h"

namespace animage {

inline constexpr int kTileSize = 128;
inline constexpr std::size_t kTilePixels = static_cast<std::size_t>(kTileSize) * kTileSize;

// Everything is stored and composited in linear RGB, premultiplied by alpha.
// Premultiplied because "over" is then a single multiply-add, and because it is
// the only representation where a half-transparent pixel does not carry a
// meaningless colour.
struct Rgba {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;

    friend bool operator==(const Rgba&, const Rgba&) = default;
};

// A tile is immutable once shared. Writers clone before touching one whose
// use_count is greater than one; shared_ptr gives us the atomic refcount the
// design calls for without hand-rolling it.
struct Tile {
    std::array<Half, kTilePixels * 4> rgba{};

    Rgba pixel(int x, int y) const {
        const std::size_t i = (static_cast<std::size_t>(y) * kTileSize + x) * 4;
        return {rgba[i].toFloat(), rgba[i + 1].toFloat(), rgba[i + 2].toFloat(),
                rgba[i + 3].toFloat()};
    }

    void setPixel(int x, int y, const Rgba& c) {
        const std::size_t i = (static_cast<std::size_t>(y) * kTileSize + x) * 4;
        rgba[i] = Half(c.r);
        rgba[i + 1] = Half(c.g);
        rgba[i + 2] = Half(c.b);
        rgba[i + 3] = Half(c.a);
    }

    bool isFullyTransparent() const {
        for (std::size_t i = 3; i < rgba.size(); i += 4) {
            if (rgba[i].bits != 0) return false;
        }
        return true;
    }
};

using TileRef = std::shared_ptr<const Tile>;

struct TileCoord {
    int x = 0;
    int y = 0;

    friend bool operator==(const TileCoord&, const TileCoord&) = default;
};

struct TileCoordHash {
    std::size_t operator()(const TileCoord& c) const noexcept {
        const std::uint64_t a = static_cast<std::uint32_t>(c.x);
        const std::uint64_t b = static_cast<std::uint32_t>(c.y);
        std::uint64_t h = a * 0x9e3779b97f4a7c15ull ^ (b + 0x165667b19e3779f9ull);
        h ^= h >> 29;
        h *= 0xbf58476d1ce4e5b9ull;
        h ^= h >> 32;
        return static_cast<std::size_t>(h);
    }
};

// Divides towards negative infinity, so tile coordinates are correct left of
// and above the origin. Plain integer division truncates towards zero and would
// put pixels -127..-1 and 0..127 in the same tile.
inline TileCoord tileCoordFor(int px, int py) {
    auto floorDiv = [](int v) {
        return (v >= 0) ? (v / kTileSize) : -(((-v) + kTileSize - 1) / kTileSize);
    };
    return {floorDiv(px), floorDiv(py)};
}

inline int tileLocal(int p) {
    const int m = p % kTileSize;
    return (m < 0) ? m + kTileSize : m;
}

// A sparse grid. An absent tile is fully transparent, and stays absent: an
// empty layer on a 500-image timeline costs nothing.
class TileGrid {
public:
    TileRef find(TileCoord c) const {
        auto it = tiles_.find(c);
        return (it == tiles_.end()) ? TileRef{} : it->second;
    }

    // Borrows the stored handle without touching its use count, which is what
    // the copy-on-write decision has to inspect.
    const TileRef* findSlot(TileCoord c) const {
        auto it = tiles_.find(c);
        return (it == tiles_.end()) ? nullptr : &it->second;
    }

    void set(TileCoord c, TileRef t) {
        if (t) {
            tiles_[c] = std::move(t);
        } else {
            tiles_.erase(c);
        }
    }

    Rgba pixel(int px, int py) const {
        const TileRef t = find(tileCoordFor(px, py));
        if (!t) return {};
        return t->pixel(tileLocal(px), tileLocal(py));
    }

    std::size_t tileCount() const { return tiles_.size(); }
    bool empty() const { return tiles_.empty(); }

    const std::unordered_map<TileCoord, TileRef, TileCoordHash>& tiles() const { return tiles_; }

    std::vector<TileCoord> coords() const {
        std::vector<TileCoord> out;
        out.reserve(tiles_.size());
        for (const auto& [c, _] : tiles_) out.push_back(c);
        return out;
    }

private:
    std::unordered_map<TileCoord, TileRef, TileCoordHash> tiles_;
};

}  // namespace animage
