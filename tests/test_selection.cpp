// SPDX-License-Identifier: GPL-3.0-or-later
//
// The lasso's arithmetic: filling a loop, splitting a drawing along it, and
// putting the two halves back together. No Qt and no window -- the whole reason
// the polygon is in core is that a test can drive it headlessly.

#include <cmath>

#include "document.h"
#include "selection.h"
#include "testing.h"

using namespace animage;

namespace {

const Rgba kInk{0.0f, 0.0f, 0.0f, 1.0f};

Selection box(double left, double top, double right, double bottom) {
    return Selection{{{left, top}, {right, top}, {right, bottom}, {left, bottom}}};
}

TileGrid inkOver(const PixelRect& area) {
    Document doc;
    const TrackId track = doc.addTrack("t");
    const LayerId layer = doc.addLayer(track, "l");
    const ImageId image = doc.insertImage(track, 0);

    TileGrid grid;
    {
        ScopedCommand command(doc, "Fill");
        Cel* cel = doc.celForWriting(track, image, layer);
        for (int y = area.y; y < area.y + area.height; ++y) {
            for (int x = area.x; x < area.x + area.width; ++x) {
                Tile* tile = cel->writableTile(tileCoordFor(x, y), doc.journal());
                tile->setPixel(tileLocal(x), tileLocal(y), kInk);
            }
        }
        grid = cel->tiles();
    }
    return grid;
}

const PixelRect kWholeWorld{-2000, -2000, 4000, 4000};

void aRectangleFillsExactly() {
    TEST("a rectangular loop covers exactly the pixels inside it");
    const CoverageMask mask = rasterise(box(10.0, 20.0, 40.0, 50.0), kWholeWorld);

    CHECK(!mask.isEmpty());
    CHECK_EQ(mask.region().x, 10);
    CHECK_EQ(mask.region().y, 20);
    CHECK_EQ(mask.region().width, 30);
    CHECK_EQ(mask.region().height, 30);

    // Whole pixels inside, nothing outside, and the edge pixels are whole too
    // because the loop is on the pixel boundary rather than through a pixel.
    CHECK_NEAR(mask.at(10, 20), 1.0, 1e-5);
    CHECK_NEAR(mask.at(25, 35), 1.0, 1e-5);
    CHECK_NEAR(mask.at(39, 49), 1.0, 1e-5);
    CHECK_NEAR(mask.at(9, 35), 0.0, 1e-5);
    CHECK_NEAR(mask.at(40, 35), 0.0, 1e-5);
    CHECK_NEAR(mask.at(25, 19), 0.0, 1e-5);
    CHECK_NEAR(mask.at(25, 50), 0.0, 1e-5);
}

void anEdgeThroughAPixelIsPartlyCovered() {
    TEST("coverage is a fraction, not a yes or no");
    // Half a pixel in from each side, so the rim pixels are half covered on the
    // horizontal edges and half on the vertical ones.
    const CoverageMask mask = rasterise(box(10.5, 20.5, 40.5, 50.5), kWholeWorld);

    CHECK_NEAR(mask.at(10, 30), 0.5, 0.02);
    CHECK_NEAR(mask.at(40, 30), 0.5, 0.02);
    CHECK_NEAR(mask.at(25, 20), 0.5, 0.07);  // vertical resolution is the sub-row count
    CHECK_NEAR(mask.at(25, 50), 0.5, 0.07);
    CHECK_NEAR(mask.at(25, 35), 1.0, 1e-5);
}

void aTriangleIsFilledByTheEvenOddRule() {
    TEST("a triangle fills where it should and nowhere else");
    const Selection triangle{{{0.0, 0.0}, {100.0, 0.0}, {0.0, 100.0}}};
    const CoverageMask mask = rasterise(triangle, kWholeWorld);

    CHECK_NEAR(mask.at(5, 5), 1.0, 1e-5);    // well inside
    CHECK_NEAR(mask.at(90, 90), 0.0, 1e-5);  // past the hypotenuse
    CHECK_NEAR(mask.at(45, 45), 1.0, 1e-5);  // still inside: the edge is x + y = 100
    CHECK_NEAR(mask.at(47, 50), 1.0, 1e-5);
    CHECK_NEAR(mask.at(51, 50), 0.0, 1e-5);
    // And the pixel the edge runs through takes a share of itself rather than
    // all or nothing, which is the whole reason for a coverage mask.
    const float rim = mask.at(49, 50);
    CHECK(rim > 0.05f);
    CHECK(rim < 0.95f);
}

// A loop that crosses itself has a hole where it doubles back, which is what
// even-odd means and what a figure of eight looks like to anybody drawing one.
void aLoopThatCrossesItselfHasAHole() {
    TEST("an even-odd fill leaves a hole where the loop doubles back");
    // A square with a smaller square inside it, joined into one loop by walking
    // out and back along the same line.
    const Selection ring{{{0.0, 0.0},
                          {100.0, 0.0},
                          {100.0, 100.0},
                          {0.0, 100.0},
                          {0.0, 0.0},
                          {25.0, 25.0},
                          {75.0, 25.0},
                          {75.0, 75.0},
                          {25.0, 75.0},
                          {25.0, 25.0}}};
    const CoverageMask mask = rasterise(ring, kWholeWorld);

    CHECK_NEAR(mask.at(10, 50), 1.0, 1e-5);  // the ring
    CHECK_NEAR(mask.at(50, 50), 0.0, 1e-5);  // the hole
}

void liftingSplitsTheDrawingInTwo() {
    TEST("what is lifted and what stays add back up to what was there");
    const TileGrid source = inkOver({0, 0, 100, 100});
    const CoverageMask mask = rasterise(box(20.0, 20.0, 60.0, 60.0), kWholeWorld);
    const Lift split = liftThrough(source, mask);

    CHECK(split.lifted.pixel(30, 30).a > 0.99f);
    CHECK_EQ(split.remaining.pixel(30, 30).a, 0.0f);
    CHECK_EQ(split.lifted.pixel(80, 80).a, 0.0f);
    CHECK(split.remaining.pixel(80, 80).a > 0.99f);

    // Exactly, everywhere, and that is what premultiplication buys: with
    // straight alpha the two halves would each need their colour rescaling and
    // the sum would only be close.
    std::size_t wrong = 0;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            const float whole = source.pixel(x, y).a;
            const float parts = split.lifted.pixel(x, y).a + split.remaining.pixel(x, y).a;
            if (std::fabs(whole - parts) > 1e-3f) ++wrong;
        }
    }
    CHECK_EQ(wrong, std::size_t{0});
}

void aTileTheLoopDoesNotReachIsNotCopied() {
    TEST("a tile the loop does not reach is shared rather than copied");
    // Ink across four tiles, a loop inside one of them.
    const TileGrid source = inkOver({0, 0, 300, 300});
    const CoverageMask mask = rasterise(box(10.0, 10.0, 40.0, 40.0), kWholeWorld);
    const Lift split = liftThrough(source, mask);

    const TileCoord far{2, 2};
    CHECK(source.find(far) != nullptr);
    // The same tile, not a copy of it: lassoing a corner of a drawing has to
    // cost the tiles under the loop and nothing else.
    CHECK(split.remaining.find(far).get() == source.find(far).get());
    CHECK(split.lifted.find(far) == nullptr);
}

void mergingPutsTheHalvesBack() {
    TEST("merging the two halves gives back the drawing");
    const TileGrid source = inkOver({0, 0, 100, 100});
    const CoverageMask mask = rasterise(box(20.0, 20.0, 60.0, 60.0), kWholeWorld);
    const Lift split = liftThrough(source, mask);

    const TileGrid back = mergeOver(split.lifted, split.remaining);
    std::size_t wrong = 0;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            if (std::fabs(back.pixel(x, y).a - source.pixel(x, y).a) > 1e-3f) ++wrong;
        }
    }
    CHECK_EQ(wrong, std::size_t{0});

    // And merging over nothing hands back the very same tiles, which is what
    // keeps a whole-drawing translation bit-exact through the commit.
    const TileGrid alone = mergeOver(split.lifted, TileGrid{});
    for (const TileCoord& coord : split.lifted.coords()) {
        CHECK(alone.find(coord).get() == split.lifted.find(coord).get());
    }
}

void anEmptyLoopSelectsNothing() {
    TEST("a loop with nothing in it produces an empty mask");
    CHECK(rasterise(Selection{}, kWholeWorld).isEmpty());
    CHECK(rasterise(Selection{{{5.0, 5.0}, {9.0, 9.0}}}, kWholeWorld).isEmpty());
    // Away from the clip entirely.
    CHECK(rasterise(box(0.0, 0.0, 10.0, 10.0), {500, 500, 100, 100}).isEmpty());
}

// A legitimate selection can be a single eyelash: long, thin, and near-zero
// area. What separates a click from a lasso is the drag threshold and never the
// area, so a loop this thin still has to fill.
void aThinLoopStillFills() {
    TEST("a long thin loop is a selection like any other");
    const CoverageMask mask = rasterise(box(10.0, 40.0, 300.0, 41.0), kWholeWorld);
    CHECK(!mask.isEmpty());
    CHECK_NEAR(mask.at(150, 40), 1.0, 1e-5);
    CHECK_EQ(mask.region().height, 1);
}

}  // namespace

int main() {
    std::printf("selection:\n");
    aRectangleFillsExactly();
    anEdgeThroughAPixelIsPartlyCovered();
    aTriangleIsFilledByTheEvenOddRule();
    aLoopThatCrossesItselfHasAHole();
    liftingSplitsTheDrawingInTwo();
    aTileTheLoopDoesNotReachIsNotCopied();
    mergingPutsTheHalvesBack();
    anEmptyLoopSelectsNothing();
    aThinLoopStillFills();
    return testing::summarise("selection");
}
