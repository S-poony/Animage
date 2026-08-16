// SPDX-License-Identifier: GPL-3.0-or-later
#include "selection.h"

#include <algorithm>
#include <cmath>

#include "color.h"

namespace animage {
namespace {

// Sub-rows per pixel row. Eight, because the vertical resolution of the edge is
// exactly this and nothing else -- the horizontal half is computed exactly, so
// this is the only place the rim can be stepped. Four was visibly banded on a
// near-horizontal edge and sixteen bought nothing that could be seen.
constexpr int kSubRows = 8;


}  // namespace

PixelRect Selection::bounds() const {
    if (isEmpty()) return {};

    double x0 = loop.front().x, x1 = loop.front().x;
    double y0 = loop.front().y, y1 = loop.front().y;
    for (const Vec2& point : loop) {
        x0 = std::min(x0, point.x);
        x1 = std::max(x1, point.x);
        y0 = std::min(y0, point.y);
        y1 = std::max(y1, point.y);
    }

    const int ix0 = static_cast<int>(std::floor(x0));
    const int iy0 = static_cast<int>(std::floor(y0));
    const int ix1 = static_cast<int>(std::ceil(x1));
    const int iy1 = static_cast<int>(std::ceil(y1));
    return {ix0, iy0, std::max(0, ix1 - ix0), std::max(0, iy1 - iy0)};
}

CoverageMask rasterise(const Selection& selection, const PixelRect& clip) {
    const PixelRect region = intersect(selection.bounds(), clip);
    if (region.isEmpty()) return {};

    std::vector<float> coverage(static_cast<std::size_t>(region.width) * region.height, 0.0f);
    std::vector<double> crossings;
    std::vector<float> row(static_cast<std::size_t>(region.width), 0.0f);

    const std::size_t points = selection.loop.size();
    const double share = 1.0 / kSubRows;

    for (int y = 0; y < region.height; ++y) {
        std::fill(row.begin(), row.end(), 0.0f);

        for (int sub = 0; sub < kSubRows; ++sub) {
            const double at = region.y + y + (sub + 0.5) * share;

            crossings.clear();
            for (std::size_t i = 0; i < points; ++i) {
                const Vec2& from = selection.loop[i];
                const Vec2& to = selection.loop[(i + 1) % points];
                // Half-open on purpose: a sub-row passing exactly through a
                // vertex must be counted once, not twice or not at all, or the
                // fill leaks along the whole row from there.
                if ((from.y <= at) == (to.y <= at)) continue;
                const double t = (at - from.y) / (to.y - from.y);
                crossings.push_back(from.x + t * (to.x - from.x));
            }
            if (crossings.size() < 2) continue;
            std::sort(crossings.begin(), crossings.end());

            // Even-odd: inside between the first and second crossing, the third
            // and fourth, and so on. A loop that crosses itself therefore has a
            // hole where it doubles back, which is what a lasso drawn in a
            // figure of eight looks like and is the rule people expect.
            for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
                double x0 = std::max(crossings[i], static_cast<double>(region.x));
                double x1 = std::min(crossings[i + 1],
                                     static_cast<double>(region.x + region.width));
                if (x1 <= x0) continue;

                const int first = static_cast<int>(std::floor(x0));
                const int last = static_cast<int>(std::ceil(x1)) - 1;
                const auto add = [&](int px, double amount) {
                    const int index = px - region.x;
                    if (index < 0 || index >= region.width) return;
                    row[static_cast<std::size_t>(index)] +=
                        static_cast<float>(amount * share);
                };

                if (first == last) {
                    add(first, x1 - x0);
                    continue;
                }
                // The two ends contribute their fraction and everything between
                // them a whole pixel. Rounding the ends to whole pixels instead
                // is the same mistake as a box filter on rounded boundaries: it
                // alternates between one and two and reads worse than no filter.
                add(first, first + 1 - x0);
                for (int px = first + 1; px < last; ++px) add(px, 1.0);
                add(last, x1 - last);
            }
        }

        float* destination = coverage.data() + static_cast<std::size_t>(y) * region.width;
        for (int x = 0; x < region.width; ++x) {
            destination[x] = std::clamp(row[static_cast<std::size_t>(x)], 0.0f, 1.0f);
        }
    }

    return CoverageMask(region, std::move(coverage));
}

Lift liftThrough(const TileGrid& source, const CoverageMask& mask) {
    Lift split;
    if (mask.isEmpty()) {
        split.remaining = source;
        return split;
    }

    const PixelRect& region = mask.region();

    for (const auto& [coord, tile] : source.tiles()) {
        if (!tile) continue;

        const PixelRect square{coord.x * kTileSize, coord.y * kTileSize, kTileSize, kTileSize};
        if (intersect(square, region).isEmpty()) {
            // The mask does not reach this tile, so nothing is picked up and the
            // handle is shared rather than copied. Lassoing a corner of a
            // drawing costs the tiles under the loop and nothing else.
            split.remaining.set(coord, tile);
            continue;
        }

        auto taken = std::make_shared<Tile>();
        auto left = std::make_shared<Tile>();
        bool any_taken = false;
        bool any_left = false;

        for (int y = 0; y < kTileSize; ++y) {
            for (int x = 0; x < kTileSize; ++x) {
                const Rgba pixel = tile->pixel(x, y);
                if (pixel.a == 0.0f && pixel.r == 0.0f && pixel.g == 0.0f && pixel.b == 0.0f) {
                    continue;
                }
                const float covered = mask.at(square.x + x, square.y + y);
                if (covered > 0.0f) {
                    taken->setPixel(x, y, {pixel.r * covered, pixel.g * covered,
                                           pixel.b * covered, pixel.a * covered});
                    any_taken = true;
                }
                if (covered < 1.0f) {
                    const float kept = 1.0f - covered;
                    left->setPixel(x, y, {pixel.r * kept, pixel.g * kept, pixel.b * kept,
                                          pixel.a * kept});
                    any_left = true;
                }
            }
        }

        if (any_taken) split.lifted.set(coord, std::move(taken));
        if (any_left) split.remaining.set(coord, std::move(left));
    }

    return split;
}

TileGrid mergeOver(TileGrid top, const TileGrid& bottom) {
    if (bottom.empty()) return top;

    TileGrid out;
    for (const auto& [coord, tile] : bottom.tiles()) {
        if (tile) out.set(coord, tile);
    }

    for (const auto& [coord, tile] : top.tiles()) {
        if (!tile) continue;
        const TileRef under = out.find(coord);
        if (!under) {
            out.set(coord, tile);
            continue;
        }

        auto merged = std::make_shared<Tile>(*under);
        for (int y = 0; y < kTileSize; ++y) {
            for (int x = 0; x < kTileSize; ++x) {
                const Rgba above = tile->pixel(x, y);
                if (above.a == 0.0f && above.r == 0.0f && above.g == 0.0f && above.b == 0.0f) {
                    continue;
                }
                merged->setPixel(x, y, over(above, merged->pixel(x, y)));
            }
        }
        out.set(coord, std::move(merged));
    }

    return out;
}

}  // namespace animage
