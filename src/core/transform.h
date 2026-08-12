// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

#include "tile.h"

namespace animage {

// Moving, rotating and scaling one drawing.
//
// Deliberately a similarity and not a general affine: these five numbers are
// exactly the ones on the transform bar, so what the interface offers and what
// the arithmetic can express are the same thing and cannot drift apart. Shear
// is not offered, so it is not representable.
//
// The pivot is what rotation and scale happen about, and it is a fact about the
// gesture rather than about the drawing -- dragging the top-left handle scales
// about the bottom-right corner. Held here so that the whole state of a live
// transform is these numbers and nothing else, which is what lets the numeric
// fields and the handles be two ways of editing one thing.
struct Transform {
    // Whole image pixels, always. A translation by half a pixel cannot be aimed
    // and cannot be undone, and it resamples: see isWholePixelTranslation.
    double dx = 0.0;
    double dy = 0.0;

    // Degrees, positive clockwise on screen. Image y runs downwards, so the
    // usual anticlockwise-positive convention would read backwards to a hand
    // dragging a handle.
    double rotation = 0.0;

    // Two, not one. The bar shows both because an edge handle scales one axis
    // and a corner handle scales both -- letting the handle decide is what frees
    // Shift to constrain rotation to fifteen-degree steps.
    double scale_x = 1.0;
    double scale_y = 1.0;

    double pivot_x = 0.0;
    double pivot_y = 0.0;

    bool isIdentity() const {
        return dx == 0.0 && dy == 0.0 && rotation == 0.0 && scale_x == 1.0 && scale_y == 1.0;
    }

    // Whether this moves pixels without resampling them.
    //
    // Registration nudges are the most common transform in animation by a wide
    // margin, and they must never soften a line -- so the exact path is a branch
    // here rather than a special case of the general one that happens to come
    // out sharp. An axis mirror is this same branch with a sign, which is what
    // makes flipping (#24) cheap later and why it must not be built on the
    // resampler.
    //
    // Exact comparisons on purpose. dx and dy are whole pixels by construction,
    // and a rotation of 1e-15 degrees is a rotation: coming out sharp for one
    // number and soft for the next would be worse than always resampling.
    bool isWholePixelTranslation() const {
        return rotation == 0.0 && scale_x == 1.0 && scale_y == 1.0 && dx == std::floor(dx) &&
               dy == std::floor(dy);
    }
};

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

// q.x = a*p.x + b*p.y + tx
// q.y = c*p.x + d*p.y + ty
struct Matrix {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;
};

Matrix matrixOf(const Transform& t);

// The inverse, which is what a resampler walks: a destination pixel asks where
// it came from. An identity matrix comes back for one that cannot be inverted --
// a scale of exactly zero -- because the alternative is a caller having to check
// what it can already avoid by not offering zero.
Matrix inverseOf(const Matrix& m);

Vec2 apply(const Matrix& m, Vec2 p);

// Moves the pivot without moving a single pixel.
//
// The pivot belongs to the gesture: dragging the top-left handle scales about
// the bottom-right corner, and dragging the box moves it about nothing at all.
// So it changes constantly, and every time it does the translation has to absorb
// the difference or the drawing jumps at the moment you touch a handle.
//
// It is also what keeps the numeric fields meaning one thing. Between gestures
// the pivot is put back to the middle of what was picked up, so "rotation 30"
// always means thirty degrees about the middle and never about whichever corner
// was last dragged.
void repivot(Transform& t, double x, double y);

// The smallest whole-pixel rectangle covering `rect` under `m`. All four corners
// are mapped: under rotation the axis-aligned box of the result is not the
// result of mapping two corners.
PixelRect transformedBounds(const Matrix& m, const PixelRect& rect);

// The whole of `source`, moved.
//
// Everything in the grid moves. `bounds` is not an argument because it would be
// a clip and there is nothing here to clip against: a selection has already been
// applied by whoever lifted these pixels into their own grid, which is what
// makes copy, cut and a lasso the same operation as this one with a different
// source.
//
// Premultiplied pixels are what makes the filtering honest -- interpolating
// straight alpha against colour produces a rim of the wrong colour, and there is
// nothing here doing that.
//
// One filter, whatever the transform: a tent whose support along each source
// axis is max(1, 1/scale) source pixels. At a scale of one that is bilinear
// however far the drawing has been turned, and below one it widens into a
// weighted reduction -- because a fixed four-neighbour read at a four-times
// reduction drops line art entirely, while averaging a block for a rotation,
// which reduces nothing, rounds every edge to a whole pixel and hands back the
// stair-steps this used to be reported for. The scale is read from `t` and never
// from the mapped footprint, which is the distinction the version before it
// missed. See "what a commit does to a line" in docs/handover.md.
TileGrid transformTiles(const TileGrid& source, const Transform& t);

}  // namespace animage
