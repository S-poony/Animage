// SPDX-License-Identifier: GPL-3.0-or-later
//
// The arithmetic of moving a drawing. Invariants rather than appearances: every
// one of these is a decision the code makes, and a decision can be asserted
// exactly.

#include <cmath>

#include "document.h"
#include "transform.h"
#include "testing.h"

using namespace animage;

namespace {

// A grid with one known mark in it, written straight rather than brushed: what
// is being measured here is the resampler, and a soft rim would only make every
// comparison approximate for no reason.
TileGrid gridWith(const PixelRect& area, const Rgba& colour) {
    TileGrid grid;
    Document doc;  // for the journal a writable tile wants
    const TrackId track = doc.addTrack("t");
    const LayerId layer = doc.addLayer(track, "l");
    const ImageId image = doc.insertImage(track, 0);

    {
        ScopedCommand command(doc, "Fill");
        Cel* cel = doc.celForWriting(track, image, layer);
        for (int y = area.y; y < area.y + area.height; ++y) {
            for (int x = area.x; x < area.x + area.width; ++x) {
                Tile* tile = cel->writableTile(tileCoordFor(x, y), doc.journal());
                tile->setPixel(tileLocal(x), tileLocal(y), colour);
            }
        }
        grid = cel->tiles();
    }
    return grid;
}

// Bit equality, not near-equality. A path that says it does not resample has to
// answer for the bits: half arithmetic that happens to round back to the same
// number is not the same claim.
bool sameBits(const TileGrid& a, const TileGrid& b, int dx, int dy, const PixelRect& over) {
    for (int y = over.y; y < over.y + over.height; ++y) {
        for (int x = over.x; x < over.x + over.width; ++x) {
            const Rgba from = a.pixel(x, y);
            const Rgba to = b.pixel(x + dx, y + dy);
            if (!(from == to)) return false;
        }
    }
    return true;
}

std::size_t drawnPixels(const TileGrid& grid) {
    std::size_t count = 0;
    const PixelRect bounds = drawnBounds(grid);
    for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
        for (int x = bounds.x; x < bounds.x + bounds.width; ++x) {
            if (grid.pixel(x, y).a > 0.0f) ++count;
        }
    }
    return count;
}

const Rgba kInk{0.0f, 0.0f, 0.0f, 1.0f};

void identityReturnsTheSameBits() {
    TEST("an identity transform returns the same bits");
    const TileGrid source = gridWith({40, 40, 30, 20}, kInk);

    const TileGrid moved = transformTiles(source, Transform{});
    CHECK(sameBits(source, moved, 0, 0, {0, 0, 200, 200}));
    // And the very same tiles, not copies of them: an identity is free as well
    // as lossless, which is what lets the preview ask for one without thinking.
    CHECK_EQ(moved.tileCount(), source.tileCount());
    for (const TileCoord& coord : source.coords()) {
        CHECK(moved.find(coord).get() == source.find(coord).get());
    }
}

void aWholePixelTranslationDoesNotResample() {
    TEST("a whole-pixel translation returns the same bits and skips the resampler");

    Transform t;
    t.dx = 37.0;
    t.dy = -19.0;
    // The branch itself, asserted directly. Bit equality alone would not say
    // which path ran -- bilinear at an integer offset lands one weight on one
    // sample and is exact too -- and the claim being made is about the path.
    CHECK(t.isWholePixelTranslation());

    const TileGrid source = gridWith({40, 40, 30, 20}, kInk);
    const TileGrid moved = transformTiles(source, t);
    CHECK(sameBits(source, moved, 37, -19, {0, 0, 200, 200}));
    CHECK_EQ(drawnPixels(moved), drawnPixels(source));
}

void aFractionOfAPixelIsNotAWholeOne() {
    TEST("what counts as a whole-pixel translation");

    Transform whole;
    whole.dx = -3.0;
    CHECK(whole.isWholePixelTranslation());

    Transform part = whole;
    part.dx = -3.5;
    CHECK(!part.isWholePixelTranslation());

    // A rotation of nothing at all is still a rotation. Coming out sharp for
    // one number and soft for the next is worse than always resampling.
    Transform turned = whole;
    turned.rotation = 1e-12;
    CHECK(!turned.isWholePixelTranslation());

    Transform scaled = whole;
    scaled.scale_y = 1.0000001;
    CHECK(!scaled.isWholePixelTranslation());
}

void rotatingAQuarterTurnLandsWhereItShould() {
    TEST("a quarter turn about a pivot lands where the arithmetic says");

    Transform t;
    t.rotation = 90.0;
    t.pivot_x = 100.0;
    t.pivot_y = 100.0;

    const Matrix m = matrixOf(t);
    // Image y runs downwards, so a positive rotation is clockwise on screen:
    // a point to the right of the pivot goes below it.
    const Vec2 right = apply(m, {150.0, 100.0});
    CHECK_NEAR(right.x, 100.0, 1e-9);
    CHECK_NEAR(right.y, 150.0, 1e-9);

    // The pivot itself does not move, which is the whole reason it is there.
    const Vec2 pivot = apply(m, {100.0, 100.0});
    CHECK_NEAR(pivot.x, 100.0, 1e-9);
    CHECK_NEAR(pivot.y, 100.0, 1e-9);
}

void theInverseUndoesTheMatrix() {
    TEST("the inverse takes a point back where it came from");

    Transform t;
    t.rotation = 37.0;
    t.scale_x = 1.7;
    t.scale_y = 0.4;
    t.dx = 22.0;
    t.dy = -8.0;
    t.pivot_x = 60.0;
    t.pivot_y = 90.0;

    const Matrix forward = matrixOf(t);
    const Matrix backward = inverseOf(forward);
    const Vec2 there = apply(forward, {12.5, -33.0});
    const Vec2 back = apply(backward, there);
    CHECK_NEAR(back.x, 12.5, 1e-9);
    CHECK_NEAR(back.y, -33.0, 1e-9);
}

void repivotMovesNothing() {
    TEST("moving the pivot moves no pixel");

    Transform t;
    t.rotation = 23.0;
    t.scale_x = 1.4;
    t.scale_y = 0.8;
    t.dx = 11.0;
    t.dy = -4.0;
    t.pivot_x = 50.0;
    t.pivot_y = 50.0;

    const Vec2 probe{123.0, -45.0};
    const Vec2 was = apply(matrixOf(t), probe);

    // The corner a handle drag would scale about, and then the middle again.
    repivot(t, 200.0, 300.0);
    repivot(t, 50.0, 50.0);

    const Vec2 now = apply(matrixOf(t), probe);
    CHECK_NEAR(now.x, was.x, 1e-9);
    CHECK_NEAR(now.y, was.y, 1e-9);
    CHECK_NEAR(t.pivot_x, 50.0, 1e-9);
    CHECK_NEAR(t.pivot_y, 50.0, 1e-9);
}

void aHalfTurnPutsTheShapeOnTheOtherSide() {
    TEST("a half turn about the middle of a shape puts it back on itself");

    const PixelRect area{40, 40, 40, 20};
    const TileGrid source = gridWith(area, kInk);

    Transform t;
    t.rotation = 180.0;
    t.pivot_x = area.x + area.width / 2.0;
    t.pivot_y = area.y + area.height / 2.0;

    const TileGrid moved = transformTiles(source, t);
    // A rectangle turned about its own middle covers the same rectangle. The
    // count is what is asserted rather than the bits, because a half turn goes
    // through the resampler like any other angle -- it is not the exact path.
    const PixelRect bounds = drawnBounds(moved);
    CHECK(!bounds.isEmpty());
    CHECK(moved.pixel(area.x + 1, area.y + 1).a > 0.9f);
    CHECK(moved.pixel(area.x + area.width - 2, area.y + area.height - 2).a > 0.9f);
    CHECK_EQ(moved.pixel(area.x - 6, area.y - 6).a, 0.0f);
}

// The trap the design note names: bilinear reads four neighbours whatever the
// reduction, so at four times down it reads four pixels out of every sixteen --
// and line art, being thin, falls through the holes. This is that case, and the
// assertion is that the line is still there rather than that it is pretty.
void reducingKeepsAThinLine() {
    TEST("a thin line survives a four-times reduction");

    // A single-pixel line, 200 long, well away from the origin so that nothing
    // about the result depends on the pivot being where the tiles start.
    const TileGrid source = gridWith({50, 120, 200, 1}, kInk);

    Transform t;
    t.scale_x = 0.25;
    t.scale_y = 0.25;
    t.pivot_x = 150.0;
    t.pivot_y = 120.0;

    const TileGrid moved = transformTiles(source, t);
    const PixelRect bounds = drawnBounds(moved);
    CHECK(!bounds.isEmpty());

    // The line has no gaps in it. At a quarter, one source pixel in four is on
    // the line, so a filter that averages the block gives every destination
    // pixel a quarter of the ink while one that reads four neighbours gives
    // three quarters of them nothing at all -- which is a dotted line.
    //
    // Counted between the first and last lit column rather than across the
    // bounds, which are tile-aligned and mostly paper.
    int first = -1;
    int last = -1;
    int lit = 0;
    for (int x = bounds.x; x < bounds.x + bounds.width; ++x) {
        bool anything = false;
        for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
            if (moved.pixel(x, y).a > 0.0f) anything = true;
        }
        if (!anything) continue;
        if (first < 0) first = x;
        last = x;
        ++lit;
    }

    CHECK(first >= 0);
    // 200 source pixels at a quarter is 50, give or take the pixel at each end
    // that the footprint reaches into.
    CHECK(last - first + 1 >= 49);
    CHECK(last - first + 1 <= 52);
    CHECK_EQ(lit, last - first + 1);
}

// The other end of the same trap, and the one that shipped. A filter chosen
// from the mapped footprint rather than from the scale treats every rotation as
// a reduction -- the footprint's axis-aligned box is (|cos| + |sin|) / 2 wide,
// which exceeds half a pixel at any angle at all -- and averaging a block for a
// transform that reduces nothing is what turned an anti-aliased rim into
// stair-steps.
//
// Asserted on the ink rather than on the path, because "which filter ran" is
// not what was wrong with it: a one-pixel line has to survive being turned.
void turningKeepsALineDark() {
    TEST("a thin line stays dark when it is only turned");

    const TileGrid source = gridWith({50, 120, 200, 1}, kInk);

    for (const double angle : {1.0, 7.0, 45.0}) {
        Transform t;
        t.rotation = angle;
        t.pivot_x = 150.0;
        t.pivot_y = 120.0;

        const TileGrid moved = transformTiles(source, t);
        const PixelRect bounds = drawnBounds(moved);
        CHECK(!bounds.isEmpty());

        // The darkest pixel of *every* column, and not the darkest pixel there
        // is. A turn spreads a one-pixel line across two rows with the weight
        // of one between them, so whichever of the two is darker keeps at least
        // half -- everywhere along it, at every sub-pixel phase.
        //
        // Asserting the maximum over the whole line instead is what the first
        // version of this test did, and it passed against the filter it was
        // written to catch: a block average leaves a third where it spans three
        // pixels and a half where it spans two, the span alternates with the
        // phase, and 200 pixels of line contain plenty of both. What separates
        // the two is that one of them is dark *all along*, which is the same
        // property "a thin line survives a four-times reduction" is about --
        // that the line has not come back dashed.
        // Between the first and last lit column and not across the bounds,
        // which are tile-aligned and therefore mostly paper -- and inside them
        // by three, because the two ends of a line are where it is allowed to
        // be pale. Getting this wrong measures the end caps and fails whatever
        // the filter did.
        const auto darkestIn = [&](int x) {
            float darkest = 0.0f;
            for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
                darkest = std::max(darkest, moved.pixel(x, y).a);
            }
            return darkest;
        };

        int first = bounds.x;
        while (first < bounds.x + bounds.width && darkestIn(first) == 0.0f) ++first;
        int last = bounds.x + bounds.width - 1;
        while (last > first && darkestIn(last) == 0.0f) --last;

        double mass = 0.0;
        float palest = 1.0f;
        for (int x = first + 3; x <= last - 3; ++x) palest = std::min(palest, darkestIn(x));
        for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
            for (int x = bounds.x; x < bounds.x + bounds.width; ++x) mass += moved.pixel(x, y).a;
        }
        CHECK(palest >= 0.5f);
        // And it is the same line: a rotation is rigid, so none of the ink is
        // spent widening it.
        CHECK(mass > 190.0);
        CHECK(mass < 215.0);
    }
}

// A reduction has to weigh the source pixels it covers, not count them. The
// version this replaces read whole pixels between bounds rounded outward, so
// the block was two or three wide depending on where the destination pixel fell
// -- which wrote 26% too much ink at a quarter and 5% too little at nine
// tenths. The error changed sign because it was rounding rather than bias,
// which is why neither a "does it darken" nor a "does it lighten" test would
// have caught it.
void reducingKeepsTheInkItRead() {
    TEST("a reduction writes the ink it read, scaled");

    const TileGrid source = gridWith({50, 120, 200, 1}, kInk);
    double before = 0.0;
    const PixelRect drawn = drawnBounds(source);
    for (int y = drawn.y; y < drawn.y + drawn.height; ++y) {
        for (int x = drawn.x; x < drawn.x + drawn.width; ++x) before += source.pixel(x, y).a;
    }

    for (const double scale : {0.9, 0.5, 0.25}) {
        Transform t;
        t.scale_x = t.scale_y = scale;
        t.pivot_x = 150.0;
        t.pivot_y = 120.0;

        const TileGrid moved = transformTiles(source, t);
        double after = 0.0;
        const PixelRect bounds = drawnBounds(moved);
        for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
            for (int x = bounds.x; x < bounds.x + bounds.width; ++x) after += moved.pixel(x, y).a;
        }

        // Within a twentieth, which is loose enough for the pixel at each end
        // that the kernel reaches past and tight enough to fail the counting
        // version at every one of these three scales.
        const double wanted = before * scale * scale;
        CHECK(after > wanted * 0.95);
        CHECK(after < wanted * 1.05);
    }
}

// The interface cannot reach this -- a drag stops at one per cent and the
// field's range starts there -- but the resampler is in `core` and takes a
// Transform from anybody. A scale of zero has no inverse, so the kernel would
// ask for a support of one over nothing; the tests run under UBSan, which is
// what makes this two lines rather than a paragraph of argument.
void aScaleOfZeroIsSurvivable() {
    TEST("a scale of zero neither crashes nor reaches for infinity");

    const TileGrid source = gridWith({40, 40, 30, 20}, kInk);
    Transform t;
    t.scale_x = 0.0;
    t.scale_y = 0.0;
    t.pivot_x = 55.0;
    t.pivot_y = 50.0;

    const TileGrid flattened = transformTiles(source, t);
    // Whatever it hands back, it hands back: the claim is that asking is safe.
    CHECK(flattened.tileCount() < 1000u);
}

void transformedBoundsCoversEveryCorner() {
    TEST("transformed bounds maps all four corners");

    Transform t;
    t.rotation = 45.0;
    t.pivot_x = 0.0;
    t.pivot_y = 0.0;

    const PixelRect box = transformedBounds(matrixOf(t), {0, 0, 100, 100});
    // A square turned 45 degrees about its own corner is a diamond as wide as
    // the square's diagonal. Mapping two corners would have said 100 wide.
    const double diagonal = 100.0 * std::sqrt(2.0);
    CHECK(box.width >= static_cast<int>(diagonal) - 1);
    CHECK(box.height >= static_cast<int>(diagonal) - 1);
}

// The document half: a transform is one command, and it puts every tile back.
void committingATransformUndoesInOneStep() {
    TEST("a committed transform is one undo step and restores every tile");

    Document doc;
    const TrackId track = doc.addTrack("main");
    const LayerId layer = doc.addLayer(track, "ink");
    const ImageId image = doc.insertImage(track, 0);

    {
        ScopedCommand command(doc, "Draw");
        Cel* cel = doc.celForWriting(track, image, layer);
        for (int y = 40; y < 60; ++y) {
            for (int x = 40; x < 80; ++x) {
                Tile* tile = cel->writableTile(tileCoordFor(x, y), doc.journal());
                tile->setPixel(tileLocal(x), tileLocal(y), kInk);
            }
        }
    }

    const std::size_t before = doc.undoDepth();
    const TileGrid original = doc.celAt(track, image, layer)->tiles();

    Transform t;
    t.dx = 300.0;
    t.dy = 200.0;
    {
        ScopedCommand command(doc, "Transform");
        Cel* cel = doc.celForWriting(track, image, layer);
        cel->replaceTiles(transformTiles(cel->tiles(), t), doc.journal());
    }

    CHECK_EQ(doc.undoDepth(), before + 1);
    CHECK(doc.celAt(track, image, layer)->pixel(350, 250).a > 0.9f);
    CHECK_EQ(doc.celAt(track, image, layer)->pixel(50, 50).a, 0.0f);

    CHECK(doc.undo());
    CHECK(sameBits(original, doc.celAt(track, image, layer)->tiles(), 0, 0, {0, 0, 600, 500}));

    CHECK(doc.redo());
    CHECK(doc.celAt(track, image, layer)->pixel(350, 250).a > 0.9f);
    CHECK_EQ(doc.celAt(track, image, layer)->pixel(50, 50).a, 0.0f);
}

}  // namespace

int main() {
    std::printf("transform:\n");
    identityReturnsTheSameBits();
    aWholePixelTranslationDoesNotResample();
    aFractionOfAPixelIsNotAWholeOne();
    rotatingAQuarterTurnLandsWhereItShould();
    theInverseUndoesTheMatrix();
    repivotMovesNothing();
    aHalfTurnPutsTheShapeOnTheOtherSide();
    reducingKeepsAThinLine();
    turningKeepsALineDark();
    reducingKeepsTheInkItRead();
    aScaleOfZeroIsSurvivable();
    transformedBoundsCoversEveryCorner();
    committingATransformUndoesInOneStep();
    return testing::summarise("transform");
}
