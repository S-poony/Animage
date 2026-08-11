// SPDX-License-Identifier: GPL-3.0-or-later
#include "transform.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "compositor.h"

namespace animage {
namespace {

constexpr double kPi = 3.14159265358979323846;

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

// Four neighbours, weighted. Correct on premultiplied pixels and only on
// premultiplied pixels: interpolating a straight colour against its alpha is
// what puts a rim of the wrong colour round everything soft.
Rgba bilinear(GridReader& source, double x, double y) {
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const float ax = static_cast<float>(x - fx);
    const float ay = static_cast<float>(y - fy);

    const Rgba p00 = source.at(x0, y0);
    const Rgba p10 = source.at(x0 + 1, y0);
    const Rgba p01 = source.at(x0, y0 + 1);
    const Rgba p11 = source.at(x0 + 1, y0 + 1);

    const float w00 = (1.0f - ax) * (1.0f - ay);
    const float w10 = ax * (1.0f - ay);
    const float w01 = (1.0f - ax) * ay;
    const float w11 = ax * ay;

    return {p00.r * w00 + p10.r * w10 + p01.r * w01 + p11.r * w11,
            p00.g * w00 + p10.g * w10 + p01.g * w01 + p11.g * w11,
            p00.b * w00 + p10.b * w10 + p01.b * w01 + p11.b * w11,
            p00.a * w00 + p10.a * w10 + p01.a * w01 + p11.a * w11};
}

// The average of the source pixels one destination pixel covers.
//
// Bilinear reads four neighbours whatever the reduction, so at four times down
// it reads four pixels out of every sixteen and line art -- which is thin, and
// mostly gaps -- simply falls through the holes. That is the same trap
// ctgBarrier records from the other end, and the same lesson as the sample
// grid: half a filter is worse than none.
//
// The footprint is read on a lattice rather than every pixel of it, bounded by
// boxSampleStride, which is the compositor's own answer to "a hundred reads for
// one pixel nobody can see a hundredth of" -- and its constant, so there is one
// decision about this in the program and not two.
// `x` and `y` are continuous source coordinates -- the middle of the footprint,
// where pixel i covers [i, i+1). One pixel's worth of footprint about the middle
// of pixel i is [i, i+1], which is pixel i and nothing else, which is what makes
// this agree with the direct read at 1:1 rather than smearing across two.
Rgba boxFilter(GridReader& source, double x, double y, double half_x, double half_y) {
    const int x0 = static_cast<int>(std::floor(x - half_x));
    const int x1 = static_cast<int>(std::ceil(x + half_x));
    const int y0 = static_cast<int>(std::floor(y - half_y));
    const int y1 = static_cast<int>(std::ceil(y + half_y));

    const int stride_x = boxSampleStride(SampleStep::fromRatio(2.0 * half_x));
    const int stride_y = boxSampleStride(SampleStep::fromRatio(2.0 * half_y));

    Rgba sum{};
    int taken = 0;
    for (int py = y0; py < y1; py += stride_y) {
        for (int px = x0; px < x1; px += stride_x) {
            const Rgba pixel = source.at(px, py);
            sum.r += pixel.r;
            sum.g += pixel.g;
            sum.b += pixel.b;
            sum.a += pixel.a;
            ++taken;
        }
    }
    if (taken == 0) return bilinear(source, x - 0.5, y - 0.5);

    const float share = 1.0f / static_cast<float>(taken);
    return {sum.r * share, sum.g * share, sum.b * share, sum.a * share};
}

}  // namespace

Matrix matrixOf(const Transform& t) {
    const double radians = t.rotation * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);

    Matrix m;
    m.a = cosine * t.scale_x;
    m.b = -sine * t.scale_y;
    m.c = sine * t.scale_x;
    m.d = cosine * t.scale_y;
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

    const PixelRect drawn = drawnBounds(source);
    if (drawn.isEmpty()) return {};

    const Matrix forward = matrixOf(t);
    const Matrix backward = inverseOf(forward);

    // A pixel at the edge of the destination reads a footprint that reaches
    // past it, so the box has to be grown by one before anything is written or
    // the outermost row comes out dimmer than the one inside it.
    PixelRect destination = transformedBounds(forward, drawn);
    destination = {destination.x - 1, destination.y - 1, destination.width + 2,
                   destination.height + 2};
    if (destination.isEmpty()) return {};

    // How far one destination pixel reaches back into the source, per axis. A
    // step of one along the destination x maps to (a, c) in the source and one
    // along y to (b, d), so the axis-aligned footprint is their sum -- which is
    // the rotated parallelogram's bounding box, and is what makes a rotation
    // read a slightly wider block than an axis-aligned reduction would.
    const double half_x = (std::abs(backward.a) + std::abs(backward.b)) / 2.0;
    const double half_y = (std::abs(backward.c) + std::abs(backward.d)) / 2.0;
    // Magnifying, or turning: the block under a destination pixel is one source
    // pixel or less, so there is nothing to average and everything to interpolate.
    const bool reducing = half_x > 0.5 || half_y > 0.5;

    GridReader reader(source);
    TileGrid out;

    const TileCoord first = tileCoordFor(destination.x, destination.y);
    const TileCoord last =
        tileCoordFor(destination.x + destination.width - 1, destination.y + destination.height - 1);

    for (int ty = first.y; ty <= last.y; ++ty) {
        for (int tx = first.x; tx <= last.x; ++tx) {
            auto made = std::make_shared<Tile>();
            bool any = false;

            for (int y = 0; y < kTileSize; ++y) {
                const int py = ty * kTileSize + y;
                if (py < destination.y || py >= destination.y + destination.height) continue;

                for (int x = 0; x < kTileSize; ++x) {
                    const int px = tx * kTileSize + x;
                    if (px < destination.x || px >= destination.x + destination.width) continue;

                    // The centre of the pixel, not its corner. Sampling the
                    // corner shifts the whole picture half a pixel up and left,
                    // which is invisible until it is compared with the original.
                    const Vec2 from = apply(backward, {px + 0.5, py + 0.5});
                    // Bilinear works between pixel centres and the box works in
                    // continuous coordinates, so one of them is offset by half
                    // a pixel and the other is not. Getting that wrong shifts
                    // the whole picture by half a pixel, which nothing shows
                    // until it is put beside the original.
                    const Rgba pixel = reducing
                                           ? boxFilter(reader, from.x, from.y, half_x, half_y)
                                           : bilinear(reader, from.x - 0.5, from.y - 0.5);
                    if (pixel.a == 0.0f && pixel.r == 0.0f && pixel.g == 0.0f &&
                        pixel.b == 0.0f) {
                        continue;
                    }
                    made->setPixel(x, y, pixel);
                    any = true;
                }
            }

            if (any) out.set({tx, ty}, std::move(made));
        }
    }

    return out;
}

}  // namespace animage
