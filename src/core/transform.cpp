// SPDX-License-Identifier: GPL-3.0-or-later
#include "transform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace animage {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Threads cost about as much to start as a small job costs to do, so only
// spread work that is worth spreading. The same shape as the compositor's own
// heuristic, and for the same reason.
int chooseWorkerCount(std::size_t tiles) {
    if (tiles < 4) return 1;
    const unsigned hardware = std::thread::hardware_concurrency();
    const int available = static_cast<int>(hardware ? hardware : 1u);
    return std::clamp(std::min(available, static_cast<int>(tiles / 2)), 1, 8);
}

// Reads a grid a pixel at a time, holding on to the tile it last looked in.
//
// A resampler reads four or more source pixels per destination pixel and walks
// them in scanline order, so all but one in every 128 land in the tile the last
// one did. Without this every read is a hash lookup.
class GridReader {
public:
    explicit GridReader(const TileGrid& grid) : grid_(grid) {}

    Rgba at(int px, int py) {
        const TileCoord coord = tileCoordFor(px, py);
        if (!looked_ || !(coord == held_at_)) {
            held_ = grid_.findSlot(coord);
            held_at_ = coord;
            looked_ = true;
        }
        if (!held_ || !*held_) return {};
        return (*held_)->pixel(tileLocal(px), tileLocal(py));
    }

private:
    const TileGrid& grid_;
    const TileRef* held_ = nullptr;
    TileCoord held_at_{};
    bool looked_ = false;
};

// One kernel radius, falling from one to nothing.
double tent(double x) {
    x = std::abs(x);
    return x < 1.0 ? 1.0 - x : 0.0;
}

// How wide the kernel is along one source axis, in source pixels: one when the
// drawing is magnified or only turned, and 1/scale when it shrinks.
//
// Zero is the case worth naming. The interface cannot offer it -- a drag stops
// at one per cent and the field's range starts there -- but this is `core`, and
// a scale of zero has no inverse: `inverseOf` hands back the identity rather
// than make every caller check. The kernel agrees with it and stays a pixel
// wide, which is a decision and not a fallback, and it keeps a support of
// infinity out of a cast that would be undefined.
double kernelSpread(double scale) {
    if (scale == 0.0) return 1.0;
    return std::max(1.0, 1.0 / std::abs(scale));
}

// The one filter, and there is deliberately no second one.
//
// A tent whose support along each *source* axis is `spread` source pixels:
// max(1, 1/scale). At a scale of one that is a single pixel and the kernel is
// exactly bilinear, whatever the rotation; below one it widens into a weighted
// reduction. So magnifying, turning and shrinking come out of one expression
// with no branch to be on the wrong side of -- which is what the version this
// replaces got wrong. It chose between interpolating and averaging a block from
// the axis-aligned box of a destination pixel's footprint, and that box exceeds
// one pixel for *any* rotation, however small: a seven-degree turn averaged a
// two- or three-pixel span, unweighted, so the sub-pixel position of every edge
// was rounded to a whole pixel and the brush's anti-aliasing came back as
// stair-steps. See "what a commit does to a line" in docs/handover.md.
//
// The support is axis-aligned in source space because `matrixOf` builds R * S:
// the scale sits next to the source, so the prefilter a reduction needs is
// separable along the source's own axes -- and a rotation, being rigid, needs
// no prefilter at all and only wants interpolating.
//
// Correct on premultiplied pixels and only on premultiplied pixels:
// interpolating a straight colour against its alpha is what puts a rim of the
// wrong colour round everything soft.
//
// `x` and `y` are continuous source coordinates, where pixel i covers [i, i+1)
// and is centred on i + 0.5. A worker holds one of these because the weight
// buffers are scratch: they are cleared and refilled per pixel, and exist only
// so that a reduction does not allocate once per pixel of it.
class Kernel {
public:
    Kernel(double spread_x, double spread_y)
        : spread_x_(spread_x), spread_y_(spread_y), over_x_(1.0 / spread_x),
          over_y_(1.0 / spread_y) {}

    Rgba at(GridReader& source, double x, double y) {
        // Which source pixel *centres* the support covers, which is why these
        // are half a pixel off the extent itself.
        const int x0 = static_cast<int>(std::ceil(x - spread_x_ - 0.5));
        const int x1 = static_cast<int>(std::floor(x + spread_x_ - 0.5));
        const int y0 = static_cast<int>(std::ceil(y - spread_y_ - 0.5));
        const int y1 = static_cast<int>(std::floor(y + spread_y_ - 0.5));

        across_.clear();
        for (int sx = x0; sx <= x1; ++sx) across_.push_back(tent((sx + 0.5 - x) * over_x_));
        down_.clear();
        for (int sy = y0; sy <= y1; ++sy) down_.push_back(tent((sy + 0.5 - y) * over_y_));

        double r = 0.0, g = 0.0, b = 0.0, a = 0.0, total = 0.0;
        for (int sy = y0, j = 0; sy <= y1; ++sy, ++j) {
            const double wy = down_[static_cast<std::size_t>(j)];
            if (wy == 0.0) continue;
            for (int sx = x0, i = 0; sx <= x1; ++sx, ++i) {
                const double w = across_[static_cast<std::size_t>(i)] * wy;
                if (w == 0.0) continue;
                const Rgba pixel = source.at(sx, sy);
                r += pixel.r * w;
                g += pixel.g * w;
                b += pixel.b * w;
                a += pixel.a * w;
                total += w;
            }
        }
        // Divided by the weight actually taken rather than by the one the
        // kernel promises. They agree to the last bit almost everywhere -- a
        // tent tiles to unity -- and where they do not, it is an edge of the
        // support, which is exactly where dimming a rim would show.
        if (total <= 0.0) return {};
        const double share = 1.0 / total;
        return {static_cast<float>(r * share), static_cast<float>(g * share),
                static_cast<float>(b * share), static_cast<float>(a * share)};
    }

private:
    double spread_x_;
    double spread_y_;
    double over_x_;
    double over_y_;
    std::vector<double> across_;
    std::vector<double> down_;
};

// The exact mirror, which is the same kind of thing translated() is and exists
// for the same reason: issue #24 asks for flipping, and a flip built as a scale
// of -1 through the resampler carries a half-pixel phase error and hands back a
// blurred mirror that nothing anywhere complains about.
//
// `dest = sign * source + shift` on each axis, with sign either +1 or -1, so
// this is a permutation of pixels and no arithmetic on any of them. Raw half
// copies rather than pixel() and setPixel(), which would be a half -> float ->
// half round trip per pixel on the one path whose whole claim is that it does
// not touch the numbers -- the same mistake translated() was measured making.
TileGrid mirrorTiles(const TileGrid& source, int sign_x, int shift_x, int sign_y, int shift_y) {
    std::unordered_map<TileCoord, std::shared_ptr<Tile>, TileCoordHash> built;

    for (const auto& [coord, tile] : source.tiles()) {
        if (!tile) continue;

        for (int ly = 0; ly < kTileSize; ++ly) {
            const int to_y = sign_y * (coord.y * kTileSize + ly) + shift_y;

            // Held across the row. A run of 128 destination pixels crosses at
            // most one tile boundary, so the map is asked once or twice a row
            // rather than once a pixel -- and a reference into an unordered_map
            // survives the insertions that follow it, which is what makes
            // holding one safe.
            std::shared_ptr<Tile>* held = nullptr;
            TileCoord held_at{};
            bool looked = false;

            for (int lx = 0; lx < kTileSize; ++lx) {
                const int to_x = sign_x * (coord.x * kTileSize + lx) + shift_x;
                const TileCoord where = tileCoordFor(to_x, to_y);
                if (!looked || !(where == held_at)) {
                    held = &built[where];
                    if (!*held) *held = std::make_shared<Tile>();
                    held_at = where;
                    looked = true;
                }

                const std::size_t from = (static_cast<std::size_t>(ly) * kTileSize + lx) * 4;
                const std::size_t into =
                    (static_cast<std::size_t>(tileLocal(to_y)) * kTileSize + tileLocal(to_x)) * 4;
                std::copy_n(tile->rgba.begin() + static_cast<std::ptrdiff_t>(from), 4,
                            (*held)->rgba.begin() + static_cast<std::ptrdiff_t>(into));
            }
        }
    }

    TileGrid out;
    for (auto& [coord, tile] : built) {
        if (tile && !tile->isFullyTransparent()) out.set(coord, std::move(tile));
    }
    return out;
}

}  // namespace

Matrix matrixOf(const Transform& t) {
    const double radians = t.rotation * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);

    // The mirror is a sign on the scale, applied here and nowhere else. Every
    // other piece of this file reads the matrix, so this one multiplication is
    // the whole of what a flip means to the arithmetic -- including the
    // resampler, which handles a negative determinant already because the
    // kernel takes the scale's magnitude.
    const double scale_x = t.scale_x * t.signX();
    const double scale_y = t.scale_y * t.signY();

    Matrix m;
    m.a = cosine * scale_x;
    m.b = -sine * scale_y;
    m.c = sine * scale_x;
    m.d = cosine * scale_y;
    // Rotate and scale about the pivot, then move: the pivot maps to itself
    // plus the translation, which is what makes dragging a corner handle leave
    // the opposite corner exactly where it was.
    m.tx = t.pivot_x + t.dx - (m.a * t.pivot_x + m.b * t.pivot_y);
    m.ty = t.pivot_y + t.dy - (m.c * t.pivot_x + m.d * t.pivot_y);
    return m;
}

Matrix inverseOf(const Matrix& m) {
    const double determinant = m.a * m.d - m.b * m.c;
    if (determinant == 0.0) return {};

    const double inverse = 1.0 / determinant;
    Matrix out;
    out.a = m.d * inverse;
    out.b = -m.b * inverse;
    out.c = -m.c * inverse;
    out.d = m.a * inverse;
    out.tx = -(out.a * m.tx + out.b * m.ty);
    out.ty = -(out.c * m.tx + out.d * m.ty);
    return out;
}

Vec2 apply(const Matrix& m, Vec2 p) {
    return {m.a * p.x + m.b * p.y + m.tx, m.c * p.x + m.d * p.y + m.ty};
}

void repivot(Transform& t, double x, double y) {
    const Matrix m = matrixOf(t);
    const double moved_x = t.pivot_x - x;
    const double moved_y = t.pivot_y - y;
    // d' = d + (I - RS)(P - Q): what the old pivot contributed through the
    // linear part has to be handed to the translation, or the mapping changes.
    t.dx += moved_x - (m.a * moved_x + m.b * moved_y);
    t.dy += moved_y - (m.c * moved_x + m.d * moved_y);
    t.pivot_x = x;
    t.pivot_y = y;
}

PixelRect transformedBounds(const Matrix& m, const PixelRect& rect) {
    if (rect.isEmpty()) return {};

    const double left = rect.x;
    const double top = rect.y;
    const double right = rect.x + rect.width;
    const double bottom = rect.y + rect.height;

    const Vec2 corners[4] = {apply(m, {left, top}), apply(m, {right, top}),
                             apply(m, {right, bottom}), apply(m, {left, bottom})};

    double x0 = corners[0].x, x1 = corners[0].x;
    double y0 = corners[0].y, y1 = corners[0].y;
    for (const Vec2& corner : corners) {
        x0 = std::min(x0, corner.x);
        x1 = std::max(x1, corner.x);
        y0 = std::min(y0, corner.y);
        y1 = std::max(y1, corner.y);
    }

    const int ix0 = static_cast<int>(std::floor(x0));
    const int iy0 = static_cast<int>(std::floor(y0));
    const int ix1 = static_cast<int>(std::ceil(x1));
    const int iy1 = static_cast<int>(std::ceil(y1));
    return {ix0, iy0, std::max(0, ix1 - ix0), std::max(0, iy1 - iy0)};
}

TileGrid transformTiles(const TileGrid& source, const Transform& t) {
    // The exact path. translated() is an index permutation and nothing else, so
    // the bits that come out are the bits that went in -- and dx = dy = 0 hands
    // back the very same tile handles, which is what makes an identity
    // transform free as well as lossless.
    if (t.isWholePixelTranslation()) {
        return translated(source, static_cast<int>(t.dx), static_cast<int>(t.dy));
    }

    // The other exact path, and the reason a flip is a sign rather than a scale
    // of -1: this is a permutation like the one above. A destination pixel
    // centre comes from source centre 2*pivot + d - centre, so the source pixel
    // is (2*pivot + d - 1) - destination -- and both constants are whole
    // numbers exactly when isAxisMirror says they are.
    if (t.isAxisMirror()) {
        const auto shiftOn = [](bool flip, double pivot, double delta) {
            return flip ? static_cast<int>(std::lround(2.0 * pivot + delta)) - 1
                        : static_cast<int>(std::lround(delta));
        };
        return mirrorTiles(source, t.flip_x ? -1 : 1, shiftOn(t.flip_x, t.pivot_x, t.dx),
                           t.flip_y ? -1 : 1, shiftOn(t.flip_y, t.pivot_y, t.dy));
    }

    const PixelRect drawn = drawnBounds(source);
    if (drawn.isEmpty()) return {};

    const Matrix forward = matrixOf(t);
    const Matrix backward = inverseOf(forward);

    // Taken from the transform's own numbers and never from the mapped
    // footprint, because a rotation's footprint is wider than a pixel while a
    // rotation reduces nothing. That substitution is the whole of the fix.
    const double spread_x = kernelSpread(t.scale_x);
    const double spread_y = kernelSpread(t.scale_y);

    // A pixel at the edge of the destination reads a footprint that reaches
    // past it, so the box has to be grown before anything is written or the
    // outermost row comes out dimmer than the one inside it. The kernel is one
    // destination pixel wide by construction, so the reach is one either way
    // when shrinking and `scale` when magnifying; the two are added rather than
    // taken separately because a rotation mixes the axes.
    PixelRect destination = transformedBounds(forward, drawn);
    const int grow = static_cast<int>(std::ceil(std::max(1.0, std::abs(t.scale_x)) +
                                                std::max(1.0, std::abs(t.scale_y)))) +
                     1;
    destination = {destination.x - grow, destination.y - grow, destination.width + 2 * grow,
                   destination.height + 2 * grow};
    if (destination.isEmpty()) return {};

    const TileCoord first = tileCoordFor(destination.x, destination.y);
    const TileCoord last =
        tileCoordFor(destination.x + destination.width - 1, destination.y + destination.height - 1);

    // Which destination tiles have anything under them at all.
    //
    // Line art is mostly paper: a drawing whose ink covers a third of the frame
    // has two thirds of its destination tiles reading nothing but transparency,
    // and reading it a pixel at a time is the most expensive way to find that
    // out. The inverse-mapped corners of a tile bound where it can possibly have
    // come from, and a tile the source does not occupy there cannot contribute.
    std::vector<TileCoord> wanted;
    for (int ty = first.y; ty <= last.y; ++ty) {
        for (int tx = first.x; tx <= last.x; ++tx) {
            const PixelRect square{tx * kTileSize, ty * kTileSize, kTileSize, kTileSize};
            // Grown by the filter's reach, or a tile whose source sits just
            // past its own edge loses the rim it should have had.
            PixelRect from = transformedBounds(backward, square);
            const int reach = static_cast<int>(std::ceil(std::max(spread_x, spread_y))) + 1;
            from = {from.x - reach, from.y - reach, from.width + 2 * reach,
                    from.height + 2 * reach};

            const TileCoord source_first = tileCoordFor(from.x, from.y);
            const TileCoord source_last =
                tileCoordFor(from.x + from.width - 1, from.y + from.height - 1);

            bool anything = false;
            for (int sy = source_first.y; sy <= source_last.y && !anything; ++sy) {
                for (int sx = source_first.x; sx <= source_last.x; ++sx) {
                    const TileRef* held = source.findSlot({sx, sy});
                    if (held && *held) {
                        anything = true;
                        break;
                    }
                }
            }
            if (anything) wanted.push_back({tx, ty});
        }
    }

    std::vector<std::shared_ptr<Tile>> produced(wanted.size());

    // One destination tile at a time, and the tiles are independent -- each
    // reads the source and writes only its own pixels -- so this splits exactly
    // the way the compositor already splits. Nothing outlives the call.
    const auto fill = [&](std::size_t begin, std::size_t end) {
        GridReader reader(source);  // one per worker: it holds a cursor
        Kernel kernel(spread_x, spread_y);  // and one of these: it holds scratch
        for (std::size_t i = begin; i < end; ++i) {
            const TileCoord coord = wanted[i];
            auto made = std::make_shared<Tile>();
            bool any = false;

            for (int y = 0; y < kTileSize; ++y) {
                const int py = coord.y * kTileSize + y;
                if (py < destination.y || py >= destination.y + destination.height) continue;

                for (int x = 0; x < kTileSize; ++x) {
                    const int px = coord.x * kTileSize + x;
                    if (px < destination.x || px >= destination.x + destination.width) continue;

                    // The centre of the pixel, not its corner. Sampling the
                    // corner shifts the whole picture half a pixel up and left,
                    // which is invisible until it is compared with the original.
                    // There is one convention now and nothing to keep in step
                    // with it: the kernel weighs source pixels by where their
                    // centres fall against this continuous coordinate. The two
                    // filters it replaces disagreed about that half pixel, and
                    // getting it wrong shifts the whole picture by half a pixel,
                    // which nothing shows until it is put beside the original.
                    const Vec2 from = apply(backward, {px + 0.5, py + 0.5});
                    const Rgba pixel = kernel.at(reader, from.x, from.y);
                    if (pixel.a == 0.0f && pixel.r == 0.0f && pixel.g == 0.0f &&
                        pixel.b == 0.0f) {
                        continue;
                    }
                    made->setPixel(x, y, pixel);
                    any = true;
                }
            }

            if (any) produced[i] = std::move(made);
        }
    };

    const int workers = chooseWorkerCount(wanted.size());
    if (workers <= 1) {
        fill(0, wanted.size());
    } else {
        const std::size_t band = (wanted.size() + workers - 1) / workers;
        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(workers) - 1);
        for (int w = 1; w < workers; ++w) {
            const std::size_t begin = std::min(wanted.size(), w * band);
            const std::size_t end = std::min(wanted.size(), begin + band);
            if (begin >= end) break;
            pool.emplace_back(fill, begin, end);
        }
        fill(0, std::min(wanted.size(), band));  // this thread takes the first band
        for (std::thread& worker : pool) worker.join();
    }

    TileGrid out;
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        if (produced[i]) out.set(wanted[i], std::move(produced[i]));
    }
    return out;
}

namespace {

// An answer that is not a count: the numbers are past what a tile coordinate
// can hold, so the destination cannot be described rather than merely being too
// large. Refused either way, and kept apart from a large count so that the sum
// across a layer cannot wrap around into a small one.
constexpr std::size_t kUncountable = static_cast<std::size_t>(-1);

// Whether the scale alone puts a commit past the budget, whatever is drawn.
//
// One occupied source tile covers about scale_x * scale_y destination tiles on
// its own, so a scale whose product is past the budget cannot fit however
// little ink there is. Asked first because it is also what keeps the arithmetic
// below inside an int: past this line a tile's footprint is at most a few
// million pixels across, and transformedBounds casts doubles to ints with
// nothing to catch an overflow.
bool scaleCouldFit(const Transform& t, std::size_t tile_budget) {
    const double widest = std::max(std::abs(t.scale_x), std::abs(t.scale_y));
    const double area = std::abs(t.scale_x) * std::abs(t.scale_y);
    const double budget = static_cast<double>(tile_budget);
    return widest <= budget && area <= budget;  // false for a NaN scale too
}

// How far the filter reaches, in source pixels, the way transformTiles grows
// the box it reads.
int filterReach(const Transform& t) {
    return static_cast<int>(
               std::ceil(std::max(kernelSpread(t.scale_x), kernelSpread(t.scale_y)))) +
           1;
}

// An upper bound on the destination tiles of one commit, from the box the
// destination lands in, read no further than `stop_at`.
//
// Cheap: no tile is looked at. It is what answers most of a drag, because
// anything comfortably under the budget needs no count at all.
//
// `drawn` is the source's own drawnBounds, handed in rather than taken here.
// That is the one number in this file that costs real time to work out --
// drawnBounds asks every tile whether it is fully transparent, which reads
// pixels until it finds one -- and it does not depend on the transform, so a
// drag must not pay for it again on every pointer move. See LayerFootprint.
std::size_t destinationTileBound(const PixelRect& drawn, const Transform& t,
                                 std::size_t stop_at) {
    const Matrix forward = matrixOf(t);

    // Where the whole destination lands, grown the way transformTiles grows the
    // box it walks, and kept in doubles: this is asked about scales whose box
    // does not fit in an int at all, and a refusal is the answer there rather
    // than a wrapped-around width.
    const Vec2 corners[4] = {
        apply(forward, {double(drawn.x), double(drawn.y)}),
        apply(forward, {double(drawn.x + drawn.width), double(drawn.y)}),
        apply(forward, {double(drawn.x + drawn.width), double(drawn.y + drawn.height)}),
        apply(forward, {double(drawn.x), double(drawn.y + drawn.height)})};
    const double grow =
        std::ceil(std::max(1.0, std::abs(t.scale_x)) + std::max(1.0, std::abs(t.scale_y))) + 1.0;
    double left = corners[0].x, right = corners[0].x;
    double top = corners[0].y, bottom = corners[0].y;
    for (const Vec2& corner : corners) {
        left = std::min(left, corner.x - grow);
        right = std::max(right, corner.x + grow);
        top = std::min(top, corner.y - grow);
        bottom = std::max(bottom, corner.y + grow);
    }

    // Every line that follows this casts a double to a tile coordinate. The
    // scale guard catches the gestures that get anywhere near here, but a
    // drawing already a long way from the origin multiplied by a scale in the
    // thousands would not be one of them.
    constexpr double kFurthest = 1.0e9;
    if (!(left > -kFurthest && top > -kFurthest && right < kFurthest && bottom < kFurthest)) {
        return kUncountable;
    }

    const double across = std::floor(right / kTileSize) - std::floor(left / kTileSize) + 1.0;
    const double down = std::floor(bottom / kTileSize) - std::floor(top / kTileSize) + 1.0;
    const double bound = across * down;
    return (bound >= static_cast<double>(stop_at)) ? stop_at : static_cast<std::size_t>(bound);
}

// Where one source tile's ink can land, in destination pixels.
//
// Grown by the filter's reach in *source* pixels, the way the resampler grows
// the box it reads, and then by a whole tile once it has landed -- which is the
// slack between an inverse-mapped square and the axis-aligned box the resampler
// tests against. A rotated 128-pixel square's box is 128 * (|cos| + |sin|)
// across, so the excess is under twenty-seven destination pixels; a tile of
// margin covers it several times over and costs a rim of tiles on a count that
// is already only being compared against a threshold.
PixelRect tileFootprint(const Matrix& forward, TileCoord coord, int reach) {
    const PixelRect square{coord.x * kTileSize - reach, coord.y * kTileSize - reach,
                           kTileSize + 2 * reach, kTileSize + 2 * reach};
    const PixelRect landed = transformedBounds(forward, square);
    return {landed.x - kTileSize, landed.y - kTileSize, landed.width + 2 * kTileSize,
            landed.height + 2 * kTileSize};
}

// How many destination tiles a dense bitmap may be spread over before the hash
// set is the better answer. 65,536 entries is 64 KB, which is nothing beside a
// single tile -- and past it the box is so much larger than the budget that the
// count stops early anyway.
constexpr std::size_t kBitmapCells = 1u << 16;

// The destination tiles of one commit, counted exactly and no further than
// `stop_at` -- past which the answer is only "at least this many", which is all
// a budget ever needs to know.
//
// Conservative by construction, because the two ends round differently: a
// destination tile is wanted by transformTiles if the *axis-aligned box* of its
// inverse-mapped square reaches occupied source, and that box is wider than the
// square it came from. So each source tile's footprint is grown before it is
// counted -- see tileFootprint -- and this counts slightly more than the
// resampler would produce. It never counts fewer, which is the direction that
// matters.
//
// **Marked in a bitmap rather than inserted into a set, where the destination
// box is small enough to address.** Hashing a tile coordinate was the whole
// cost of a whole-layer fit check: the count runs per drawing and is summed, so
// a rotation drag over a forty-drawing layer of line art measured 10.9 ms a
// pointer move against 0.3 ms for the same drag on one drawing -- on the
// interface thread, once per tablet event. The box a commit lands in is dense
// enough to index directly, so this marks a byte instead. It is a change of
// speed and not of answer: both paths count the same coordinates the same
// number of times, and the set is still there for the boxes too large to
// address.
std::size_t destinationTileCount(const TileGrid& source, const Transform& t,
                                 std::size_t stop_at) {
    const Matrix forward = matrixOf(t);
    const int reach = filterReach(t);

    // Where every footprint has to land, from the occupied tiles' own bounds
    // grown exactly as one tile's are. An affine takes a larger rectangle to a
    // larger axis-aligned box, so no tile's footprint can escape this one.
    bool any = false;
    TileCoord lowest{}, highest{};
    for (const auto& [coord, tile] : source.tiles()) {
        if (!tile) continue;
        if (!any) {
            lowest = highest = coord;
            any = true;
            continue;
        }
        lowest = {std::min(lowest.x, coord.x), std::min(lowest.y, coord.y)};
        highest = {std::max(highest.x, coord.x), std::max(highest.y, coord.y)};
    }
    if (!any) return 0;

    const PixelRect whole{lowest.x * kTileSize - reach, lowest.y * kTileSize - reach,
                          (highest.x - lowest.x + 1) * kTileSize + 2 * reach,
                          (highest.y - lowest.y + 1) * kTileSize + 2 * reach};
    const PixelRect landed = transformedBounds(forward, whole);
    const TileCoord origin = tileCoordFor(landed.x - kTileSize, landed.y - kTileSize);
    const TileCoord furthest = tileCoordFor(landed.x + landed.width + kTileSize - 1,
                                            landed.y + landed.height + kTileSize - 1);
    const long long across = static_cast<long long>(furthest.x) - origin.x + 1;
    const long long down = static_cast<long long>(furthest.y) - origin.y + 1;

    std::size_t counted = 0;
    // Sized once for the answer it can be asked to hold, because growing a hash
    // table repeatedly is most of what the fallback costs otherwise.
    std::unordered_set<TileCoord, TileCoordHash> touched;
    std::vector<std::uint8_t> marked;
    // A box that came back inside out is one whose corners overflowed an int on
    // the way here, which the scale guard makes unreachable and which the set
    // survives anyway. Addressing it would not.
    const bool dense = landed.width > 0 && landed.height > 0 && across > 0 && down > 0 &&
                       static_cast<std::size_t>(across * down) <= kBitmapCells;
    if (dense) {
        marked.assign(static_cast<std::size_t>(across * down), 0u);
    } else {
        touched.reserve(std::min<std::size_t>(stop_at, kBitmapCells) + 1);
    }

    for (const auto& [coord, tile] : source.tiles()) {
        if (!tile) continue;

        const PixelRect footprint = tileFootprint(forward, coord, reach);
        const TileCoord first = tileCoordFor(footprint.x, footprint.y);
        const TileCoord last = tileCoordFor(footprint.x + footprint.width - 1,
                                            footprint.y + footprint.height - 1);
        for (int ty = first.y; ty <= last.y; ++ty) {
            for (int tx = first.x; tx <= last.x; ++tx) {
                if (dense) {
                    std::uint8_t& cell =
                        marked[static_cast<std::size_t>(ty - origin.y) *
                                   static_cast<std::size_t>(across) +
                               static_cast<std::size_t>(tx - origin.x)];
                    if (cell) continue;
                    cell = 1u;
                    ++counted;
                } else {
                    if (!touched.insert({tx, ty}).second) continue;
                    ++counted;
                }
                // Inside the innermost loop, so that the work this does is
                // bounded by what it was asked for and not by the footprint it
                // is walking. One source tile at a large enough scale covers
                // more destination tiles than the whole budget by itself.
                if (counted >= stop_at) return stop_at;
            }
        }
    }
    return counted;
}

}  // namespace

bool commitFitsInBudget(const TileGrid& source, const Transform& t, std::size_t tile_budget) {
    // The two exact paths cannot grow the grid at all: a whole-pixel
    // translation re-keys the handles it was given and a mirror permutes them,
    // so neither allocates a tile the source had not already got. Asking the
    // general question about them would answer yes anyway, but slowly and for
    // the wrong reason.
    if (t.isWholePixelTranslation() || t.isAxisMirror()) return true;
    if (source.empty()) return true;
    if (!scaleCouldFit(t, tile_budget)) return false;

    // An upper bound on the tiles in the destination box is an upper bound on
    // the tiles with anything under them, so anything comfortably under the
    // budget is answered without counting a single tile. That is most of a
    // drag, which is what keeps this off the pen's latency path.
    const std::size_t bound = destinationTileBound(drawnBounds(source), t, tile_budget + 1);
    if (bound == kUncountable) return false;
    if (bound <= tile_budget) return true;

    return destinationTileCount(source, t, tile_budget + 1) <= tile_budget;
}

LayerFootprint layerFootprint(const std::vector<const TileGrid*>& sources) {
    LayerFootprint layer;
    layer.grids.reserve(sources.size());
    layer.drawn.reserve(sources.size());
    for (const TileGrid* source : sources) {
        if (!source) continue;
        layer.drawn.push_back(drawnBounds(*source));
        // Counted rather than taken from tileCount(), which counts entries and
        // not ink: an over-count here would *raise* the ceiling, and this is
        // the one number in the expression that has to lean the other way.
        for (const auto& [coord, tile] : source->tiles()) {
            if (tile) ++layer.occupied;
        }
        layer.grids.push_back(*source);
    }
    return layer;
}

bool commitFitsInBudget(const LayerFootprint& layer, const Transform& t,
                        std::size_t tile_budget) {
    // The exact paths again, and the reason is stronger here than for one
    // drawing: a registration nudge across a whole layer is the commonest thing
    // this feature is for, and it must never be the one that will not fit.
    if (t.isWholePixelTranslation() || t.isAxisMirror()) return true;

    const std::vector<TileGrid>& sources = layer.grids;
    const std::size_t occupied = layer.occupied;

    // Before the scale guard and not after, exactly as for one drawing: the
    // guard is about what one occupied tile costs, and a layer with nothing on
    // it anywhere has none. A bake of nothing fits in any budget, including one
    // that no scale would fit in.
    if (occupied == 0) return true;

    // **The ceiling bounds the growth and not the total, and that is a
    // decision.** The total is what a layer bake was first bounded by, and it
    // was wrong in a way the benchmark showed straight away: a plain seven
    // degrees on a forty-drawing 4K layer wants about 23,700 destination tiles
    // and was refused, so the feature did not work at all on the shots it is
    // most wanted for. The message it produced was wrong three ways over --
    // nothing was scaled, it was not a drawing, and scaling down would not have
    // helped.
    //
    // What actually has no bound is the *scale*. The bar goes to 10000% and a
    // handle drag is bounded only by where the pointer can reach, and the
    // destination grows as the square, so a drawing that previews perfectly
    // well can ask for hundreds of gigabytes. The number of drawings is not
    // that: forty cels at 100% ask for about what the layer already holds, and
    // the layer is in memory now or there would be nothing to look at. So the
    // ceiling is `kLayerGrowth` times what is there -- a bound measured against
    // a quantity this machine has already proved it can hold -- or the
    // single-drawing budget, whichever is larger, so that a small layer can
    // still be scaled up as far as one drawing could.
    //
    // It is not a guarantee, and the caller must not treat it as one. A bake's
    // peak is the new tiles plus the old ones, which the journal is holding for
    // undo, so it is about (1 + kLayerGrowth) times the layer -- and a large
    // enough layer can still exhaust a machine. What makes that survivable is
    // at the other end: Document::transformLayer catches the failure and puts
    // back every drawing it had already written.
    const std::size_t allowed = std::max(tile_budget, kLayerGrowth * occupied);
    if (!scaleCouldFit(t, allowed)) return false;

    // Summed and never de-duplicated between cels. Two drawings landing tiles
    // at the same coordinate are two tiles: the coordinate is shared, the
    // memory is not.
    std::size_t bounded = 0;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (sources[i].empty()) continue;
        const std::size_t bound = destinationTileBound(layer.drawn[i], t, allowed + 1);
        if (bound == kUncountable) return false;
        bounded += bound;
        if (bounded > allowed) break;
    }
    if (bounded <= allowed) return true;

    // Only now, and this is the one place a layer costs more to ask about than
    // a drawing does: the cheap bound is a box, and a hundred boxes overshoot a
    // hundred times, so an ordinary long shot reaches the exact count where one
    // drawing would not have. What bounds the work is still the ceiling --
    // whatever is left of it -- and not the number of drawings.
    std::size_t total = 0;
    for (const TileGrid& source : sources) {
        if (source.empty()) continue;
        total += destinationTileCount(source, t, allowed + 1 - total);
        if (total > allowed) return false;
    }
    return true;
}

}  // namespace animage
