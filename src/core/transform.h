// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <vector>

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

    // A mirror, as a sign rather than as a negative scale.
    //
    // The scale stays a magnitude -- it is what the bar's two fields show, and
    // "-100%" is not a thing anybody types into a per cent field -- so the sign
    // lives here and `matrixOf` multiplies it in. Nothing else in the arithmetic
    // has to know: the matrix carries the sign, and the resampler, the bounds
    // and the pivot all read the matrix.
    //
    // flip_x mirrors across the vertical axis through the pivot, so left becomes
    // right. flip_y is top and bottom.
    bool flip_x = false;
    bool flip_y = false;

    double pivot_x = 0.0;
    double pivot_y = 0.0;

    bool isIdentity() const {
        return dx == 0.0 && dy == 0.0 && rotation == 0.0 && scale_x == 1.0 && scale_y == 1.0 &&
               !flip_x && !flip_y;
    }

    // The sign each axis carries, which is the whole of what a flip is.
    double signX() const { return flip_x ? -1.0 : 1.0; }
    double signY() const { return flip_y ? -1.0 : 1.0; }

    // Whether this moves pixels without resampling them.
    //
    // Registration nudges are the most common transform in animation by a wide
    // margin, and they must never soften a line -- so the exact path is a branch
    // here rather than a special case of the general one that happens to come
    // out sharp.
    //
    // Exact comparisons on purpose. dx and dy are whole pixels by construction,
    // and a rotation of 1e-15 degrees is a rotation: coming out sharp for one
    // number and soft for the next would be worse than always resampling.
    //
    // A flip is not one of these, and saying so is load-bearing rather than
    // tidy: a mirror with the rotation at zero and the scale at one answers yes
    // to every other clause here, so without it the commit would take the
    // translation branch and hand back a drawing that had not been flipped at
    // all.
    bool isWholePixelTranslation() const {
        return rotation == 0.0 && scale_x == 1.0 && scale_y == 1.0 && dx == std::floor(dx) &&
               dy == std::floor(dy) && !flip_x && !flip_y;
    }

    // Whether this is a mirror that lands on the pixel grid, which is the other
    // exact path and issue #24's whole point.
    //
    // A mirror is the translation branch with a sign, and it must be built as
    // that rather than as a scale of -1 through the resampler: a bilinear read
    // at -1 carries a half-pixel phase error and gives back a blurred mirror
    // that nothing anywhere complains about. Flipping a drawing is not an
    // operation that should cost it anything.
    //
    // The extra clause is where the axis falls. Under a mirror a destination
    // pixel centre comes from source centre 2*pivot + d - centre, so it lands on
    // a centre exactly when twice the pivot is a whole number -- which it always
    // is in the interface, the pivot being the middle of a whole-pixel rectangle
    // -- but this is `core`, and a pivot a quarter of a pixel off would silently
    // resample everything if the predicate merely assumed it.
    bool isAxisMirror() const {
        if (!flip_x && !flip_y) return false;
        if (rotation != 0.0 || scale_x != 1.0 || scale_y != 1.0) return false;
        if (dx != std::floor(dx) || dy != std::floor(dy)) return false;
        if (flip_x && 2.0 * pivot_x != std::floor(2.0 * pivot_x)) return false;
        if (flip_y && 2.0 * pivot_y != std::floor(2.0 * pivot_y)) return false;
        return true;
    }

    // Exact, and the pivot counts.
    //
    // Wanted because a stored placement -- a reference layer's, which is the
    // only transform in the program that outlives its gesture -- is what the
    // pixels derived from it are keyed on. "Has this changed" therefore decides
    // whether a picture on screen is the right one, so it is asked of every
    // field rather than of the ones that look like they matter: the pivot moves
    // where a rotation goes, and two placements agreeing everywhere but there
    // are two different pictures.
    //
    // Exact comparison for the same reason isWholePixelTranslation uses one --
    // these numbers arrive from fields and handles, not from arithmetic that
    // drifts, and a tolerance here would be a cache that sometimes serves the
    // wrong frame.
    friend bool operator==(const Transform&, const Transform&) = default;
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

// What one commit is allowed to rasterise, in destination tiles.
//
// A tile is 128 KB, so this is two gigabytes -- four times the history's whole
// default budget, and twenty-seven full 4K cels. It is a scale of about ten on
// a full-frame HD drawing, six on 4K and thirty-four on a small lasso, and
// about two and a half seconds of resampling on the machine bench_transform was
// last run on. Nothing about it is derived: it is the largest number where the
// wait still reads as an operation rather than as a hang, and like the history
// budget it is one constant to move when somebody reports hitting it.
//
// It exists because the destination has no other bound. A scale is unbounded
// on both routes into it -- the bar's field goes to 10000% and a handle drag is
// bounded only by where the pointer can reach -- and the destination grows as
// its square, so a drawing that previews perfectly well can ask for hundreds of
// gigabytes on commit. See issue #40.
inline constexpr std::size_t kCommitTileBudget = 16384;

// Whether `transformTiles` would stay inside `tile_budget`, asked before it is
// called and cheaply enough to ask on every move of a drag.
//
// Counted from the source and not from the destination, which is the whole
// point of it. The destination is what has no bound, so walking it costs what
// it costs -- 64 ms at 10000% on an HD drawing, and four times that at 20000%.
// Walking the source instead is a walk over tiles that already exist, and
// stopping the moment the count passes the budget makes the worst case the
// budget: the same answer in the same fraction of a millisecond at 200% and at
// 10000%. An interface that has to ask sixty times a second can only ask a
// question shaped like that one.
//
// Conservative by construction, because the two ends round differently: a
// destination tile is wanted by `transformTiles` if the *axis-aligned box* of
// its inverse-mapped square reaches occupied source, and that box is wider than
// the square it came from. So each source tile's footprint is grown before it
// is counted, and this says no slightly sooner than the resampler would say
// yes. It never says yes to a commit the resampler would take past the budget,
// which is the direction that matters.
bool commitFitsInBudget(const TileGrid& source, const Transform& t,
                        std::size_t tile_budget = kCommitTileBudget);

// How much larger than itself a whole layer may be made in one bake.
//
// The ceiling for a layer is this times what the layer already holds, or
// `kCommitTileBudget`, whichever is larger -- so a small layer can still be
// scaled up as far as one drawing could, and a large one can be turned, nudged
// or adjusted without being refused for being large.
//
// Three, and it was two until the tests said what the count actually does.
//
// The number is not the growth an animator would recognise, because what it
// bounds is the *counted* destination, and that count is conservative on
// purpose: every source tile's footprint is grown by a whole tile before it is
// counted. On a large drawing that margin is a rim and the count of a plain
// rotation comes out near 1.2; on a small one the rim is most of the answer and
// the same rotation counts near 1.9. At two, whether you could turn your layer
// depended on how big the drawings were, which is not a rule anybody could hold
// in their head.
//
// Three clears a rotation at any size and still refuses a scale to 200%, which
// is four times the pixels before the margin is added -- so the two cases this
// has to tell apart stay on opposite sides of it. What it costs is the peak: a
// bake at the ceiling holds the new tiles plus the old ones the journal keeps
// for undo, so about four times the layer at once.
inline constexpr std::size_t kLayerGrowth = 3;

// Everything about a layer's grids that a fit check needs and the transform
// cannot change, gathered once.
//
// It exists for the drag. `drawnBounds` is the only expensive thing in a fit
// check -- it asks every tile whether it is fully transparent, which reads
// pixels until it finds one -- and asking it about forty drawings measured
// 16 ms, on the interface thread, once per tablet event. None of it depends on
// the five numbers being dragged, so the gesture works it out when it starts
// and hands the same answer back on every move.
//
// It owns its grids rather than pointing at the document's, and that is worth a
// line: a TileGrid copy is a hash map of shared tile handles and not a pixel,
// so the copy costs microseconds -- and what it buys is that the whole thing
// can be held by a live gesture, copied about and outlive an edit without a
// dangling pointer being possible at all.
struct LayerFootprint {
    std::vector<TileGrid> grids;
    // One per grid, in the same order. An empty grid gets an empty rectangle
    // and is skipped rather than refused.
    std::vector<PixelRect> drawn;
    // Occupied tiles across the whole layer, which is what the ceiling is
    // measured against.
    std::size_t occupied = 0;
};
// Null sources are dropped rather than kept as holes, so `grids` is exactly
// what a bake would walk.
LayerFootprint layerFootprint(const std::vector<const TileGrid*>& sources);

// The same question about a bake across a whole layer: every drawing of it,
// moved by the same numbers, in one command.
//
// **It bounds the growth and not the total**, and `tile_budget` is a floor
// under that rather than a cap on it -- see the definition for why the total
// was tried first and what the benchmark said about it. What has no bound of
// its own is the scale; the number of drawings is bounded by what is already in
// memory.
//
// Counted per cel and summed, never de-duplicated between them: two drawings
// landing a tile at the same coordinate are two tiles, because the coordinate
// is shared and the memory is not.
//
// **True is not a promise that the bake will succeed.** It bounds the ask
// against what the machine is already holding, which is a good bound and not a
// guarantee. `Document::transformLayer` is the other half: it catches the
// allocation failing and puts back every drawing it had already written.
//
// Null and empty grids are skipped rather than refused, so a layer that is
// simply absent at some drawings costs nothing to ask about.
bool commitFitsInBudget(const LayerFootprint& layer, const Transform& t,
                        std::size_t tile_budget = kCommitTileBudget);

}  // namespace animage
