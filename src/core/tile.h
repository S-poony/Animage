// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
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

// A rectangle in image pixels. Lives here rather than in the compositor
// because tiles, fills and bounds all speak in these.
struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool isEmpty() const { return width <= 0 || height <= 0; }
};

// The two things everybody does with a pair of these, and the test that goes
// with them. They were private to three translation units -- the canvas, the
// colour job and the selection, the last two character for character identical
// -- which made the empty-rectangle conventions three decisions that happened
// to agree rather than one decision. They agree because it is written here:
//
//   intersect -- no overlap is the empty rectangle, never a negative one, so
//                the result is always safe to loop over.
//   unite     -- empty is the identity, so folding over a list of regions can
//                start from {} and a region nobody wrote to costs nothing.
inline PixelRect intersect(const PixelRect& a, const PixelRect& b) {
    const int x0 = std::max(a.x, b.x);
    const int y0 = std::max(a.y, b.y);
    const int x1 = std::min(a.x + a.width, b.x + b.width);
    const int y1 = std::min(a.y + a.height, b.y + b.height);
    if (x1 <= x0 || y1 <= y0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

inline PixelRect unite(const PixelRect& a, const PixelRect& b) {
    if (a.isEmpty()) return b;
    if (b.isEmpty()) return a;
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.width, b.x + b.width);
    const int y1 = std::max(a.y + a.height, b.y + b.height);
    return {x0, y0, x1 - x0, y1 - y0};
}

// Whether `inner` is wholly inside `outer`. An empty `inner` is contained
// wherever it sits, which is what the cache check wants: nothing to cover is
// covered.
inline bool contains(const PixelRect& outer, const PixelRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

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
// empty layer on a 500-image track costs nothing.
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

// Where anything has actually been drawn, to the nearest tile, or an empty
// rectangle if nothing has.
//
// Emptied tiles are skipped, and that is not tidiness. It is what the box a
// transform draws round a drawing with no selection is made of, and it is what
// picks a colour solve's resolution -- and erasing used to clear a tile's pixels
// while leaving the tile in the grid, so bounds taken from tile coordinates
// alone went on describing a mark that was no longer there. A stray scribble out
// in a corner, made and rubbed out, left every later solve permanently coarser
// than before it was ever drawn, invisibly, because the region is not something
// you can see.
//
// The grid lets go of an emptied tile at the end of the command that emptied it
// now, so this is the second line of defence rather than the fix -- a grid that
// never went through a command, a CtgJob's copy or a fill's tiles, can still
// hold one. Cheap either way: isFullyTransparent stops at the first pixel that
// is there, so a tile holding anything costs one compare.
inline PixelRect drawnBounds(const TileGrid& grid) {
    bool any = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (const auto& [coord, tile] : grid.tiles()) {
        if (!tile || tile->isFullyTransparent()) continue;
        const int left = coord.x * kTileSize;
        const int top = coord.y * kTileSize;
        if (!any) {
            x0 = left;
            y0 = top;
            x1 = left + kTileSize;
            y1 = top + kTileSize;
            any = true;
            continue;
        }
        x0 = std::min(x0, left);
        y0 = std::min(y0, top);
        x1 = std::max(x1, left + kTileSize);
        y1 = std::max(y1, top + kTileSize);
    }
    return any ? PixelRect{x0, y0, x1 - x0, y1 - y0} : PixelRect{};
}

// The same rectangle to the pixel rather than to the tile.
//
// Which of the two you want is not a matter of taste. A solve region wants
// drawnBounds: it is choosing a resolution and a rim, both of which are about
// how much room the work needs, and reading every pixel to save part of a tile
// would cost more than it saves. A box drawn on screen wants this one, because
// a rectangle 128 pixels bigger than the drawing on every side is visibly not
// the drawing -- it is the tiles, which are an implementation detail nobody
// asked to see.
//
// Reads every pixel of every occupied tile, so it is for the beginning of a
// gesture and not for a loop. A tile whose rows are scanned from the outside in
// stops as soon as it has bounded itself, which is most of the cost on line art.
inline PixelRect paintedBounds(const TileGrid& grid) {
    bool any = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    for (const auto& [coord, tile] : grid.tiles()) {
        if (!tile) continue;

        int left = kTileSize, top = kTileSize, right = -1, bottom = -1;
        for (int y = 0; y < kTileSize; ++y) {
            const std::size_t row = static_cast<std::size_t>(y) * kTileSize * 4;
            for (int x = 0; x < kTileSize; ++x) {
                if (tile->rgba[row + static_cast<std::size_t>(x) * 4 + 3].bits == 0) continue;
                if (x < left) left = x;
                if (x > right) right = x;
                if (y < top) top = y;
                bottom = y;
            }
        }
        if (right < 0) continue;  // an emptied tile the grid has not let go of

        const int base_x = coord.x * kTileSize;
        const int base_y = coord.y * kTileSize;
        if (!any) {
            x0 = base_x + left;
            y0 = base_y + top;
            x1 = base_x + right + 1;
            y1 = base_y + bottom + 1;
            any = true;
            continue;
        }
        x0 = std::min(x0, base_x + left);
        y0 = std::min(y0, base_y + top);
        x1 = std::max(x1, base_x + right + 1);
        y1 = std::max(y1, base_y + bottom + 1);
    }

    return any ? PixelRect{x0, y0, x1 - x0, y1 - y0} : PixelRect{};
}

// The same pixels, moved.
//
// A genuine copy and not a view, because a tile is immutable once shared and an
// arbitrary offset is not tile-aligned: nothing here can be expressed by
// handing out the same handles under different coordinates. Cheap anyway, in
// the one place it is used -- a colour layer's marks are a few tiles, against
// line art that is hundreds.
inline TileGrid translated(const TileGrid& grid, int dx, int dy) {
    if (dx == 0 && dy == 0) return grid;

    TileGrid moved;

    // A whole number of tiles: the handles are re-keyed and not one pixel is
    // copied. Rare in practice and free when it happens.
    if (dx % kTileSize == 0 && dy % kTileSize == 0) {
        const int shift_x = dx / kTileSize;
        const int shift_y = dy / kTileSize;
        for (const auto& [coord, tile] : grid.tiles()) {
            if (tile) moved.set({coord.x + shift_x, coord.y + shift_y}, tile);
        }
        return moved;
    }

    // Otherwise, scattered: each source tile lands across up to four
    // destination tiles, and each overlap is a run of whole rows.
    //
    // Written as block copies and not as a walk over the destination asking the
    // grid for each pixel, which is what this was. That version was one hash
    // lookup and one half-to-float-to-half round trip per pixel -- 289 ms to
    // nudge a 4K drawing by three pixels, on the path that exists precisely
    // because a registration nudge must cost nothing. Measured in
    // bench_transform. The translation is injective, so no two source tiles can
    // write the same destination pixel and nothing has to be blended.
    std::unordered_map<TileCoord, std::shared_ptr<Tile>, TileCoordHash> built;

    for (const auto& [coord, tile] : grid.tiles()) {
        if (!tile) continue;

        const int from_x = coord.x * kTileSize;
        const int from_y = coord.y * kTileSize;
        const int to_x = from_x + dx;
        const int to_y = from_y + dy;

        const TileCoord first = tileCoordFor(to_x, to_y);
        const TileCoord last = tileCoordFor(to_x + kTileSize - 1, to_y + kTileSize - 1);

        for (int ty = first.y; ty <= last.y; ++ty) {
            for (int tx = first.x; tx <= last.x; ++tx) {
                const int tile_x = tx * kTileSize;
                const int tile_y = ty * kTileSize;
                const int x0 = std::max(to_x, tile_x);
                const int y0 = std::max(to_y, tile_y);
                const int x1 = std::min(to_x + kTileSize, tile_x + kTileSize);
                const int y1 = std::min(to_y + kTileSize, tile_y + kTileSize);
                if (x1 <= x0 || y1 <= y0) continue;

                auto& destination = built[{tx, ty}];
                if (!destination) destination = std::make_shared<Tile>();

                const std::size_t run = static_cast<std::size_t>(x1 - x0) * 4;
                for (int y = y0; y < y1; ++y) {
                    const std::size_t source_index =
                        (static_cast<std::size_t>(y - to_y) * kTileSize + (x0 - to_x)) * 4;
                    const std::size_t destination_index =
                        (static_cast<std::size_t>(y - tile_y) * kTileSize + (x0 - tile_x)) * 4;
                    std::copy_n(tile->rgba.begin() + static_cast<std::ptrdiff_t>(source_index),
                                run,
                                destination->rgba.begin() +
                                    static_cast<std::ptrdiff_t>(destination_index));
                }
            }
        }
    }

    for (auto& [coord, tile] : built) {
        if (tile && !tile->isFullyTransparent()) moved.set(coord, std::move(tile));
    }
    return moved;
}

}  // namespace animage
