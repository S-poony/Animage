// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "tile.h"
#include "transform.h"

namespace animage {

// A closed loop, in image coordinates.
//
// Image coordinates rather than screen ones, so it survives panning and zooming
// and means the same thing at any magnification. A polygon rather than a mask,
// because it is cheap, because it can be re-rasterised at any resolution, and
// because a scanline fill with an even-odd rule is sixty lines of Qt-free code
// that a test can drive headlessly.
//
// A selection here has no independent life. It cannot clip the brush -- you can
// draw anywhere whether or not something is selected -- so it is an argument to
// three operations, transform, copy and erase, and nothing else. That is why it
// wants no mode, no panel, no status of its own and no place in the saved
// project, and why an ordinary click can clear it without anybody losing work
// they cannot recreate in two seconds.
struct Selection {
    std::vector<Vec2> loop;

    // Fewer than three points cannot enclose anything.
    bool isEmpty() const { return loop.size() < 3; }
    PixelRect bounds() const;
};

// How much of each pixel a loop covers, from 0 to 1.
//
// Coverage and not a hard edge. A hard mask cut through line art gives the
// lifted content a jagged rim, and with premultiplied pixels the honest version
// is exact and costs nothing: lifted = src * c, remaining = src * (1 - c).
// Premultiplication is what makes that true, and it is one more thing that
// representation is paying for.
class CoverageMask {
public:
    CoverageMask() = default;
    CoverageMask(PixelRect region, std::vector<float> coverage)
        : region_(region), coverage_(std::move(coverage)) {}

    const PixelRect& region() const { return region_; }
    bool isEmpty() const { return region_.isEmpty(); }

    // Outside the region the answer is zero: a mask covers what it covers, and
    // a caller walking a tile that overlaps its edge should not have to know
    // where the edge is.
    float at(int px, int py) const {
        const int x = px - region_.x;
        const int y = py - region_.y;
        if (x < 0 || y < 0 || x >= region_.width || y >= region_.height) return 0.0f;
        return coverage_[static_cast<std::size_t>(y) * region_.width + x];
    }

private:
    PixelRect region_;
    std::vector<float> coverage_;
};

// The loop, filled with an even-odd rule and clipped to `clip`.
//
// Rows are sampled several times over and each span contributes its exact
// horizontal fraction, so a near-vertical edge is exact and a near-horizontal
// one is quantised to the number of sub-rows. That asymmetry is deliberate: the
// horizontal half costs nothing and the vertical half costs a factor.
CoverageMask rasterise(const Selection& selection, const PixelRect& clip);

// A grid split in two by a mask: what was picked up, and what stayed behind.
//
// Both are needed at once and neither can be derived from the other cheaply,
// which is why this is one function. A tile the mask does not reach is not
// copied at all -- the handle is shared into `remaining`, so lassoing a corner
// of a drawing costs the tiles under the loop and nothing else.
struct Lift {
    TileGrid lifted;
    TileGrid remaining;
};

Lift liftThrough(const TileGrid& source, const CoverageMask& mask);

// `top` over `bottom`, premultiplied. What a transform commits with once there
// is a selection: the moved pixels land back on the ones that stayed.
//
// Returns `top` untouched when there is nothing underneath, which is what keeps
// a whole-drawing translation bit-exact -- the exact path would otherwise be
// undone by the merge that follows it.
TileGrid mergeOver(TileGrid top, const TileGrid& bottom);

}  // namespace animage
