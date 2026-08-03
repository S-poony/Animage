// SPDX-License-Identifier: GPL-3.0-or-later
//
// The timeline operations M3's interface is built on. The stretch-to-hold drag
// in particular depends on many slot edits collapsing into one undo step, and
// that is not obvious from reading either piece on its own.

#include <algorithm>
#include <string>

#include "brush.h"
#include "document.h"
#include "testing.h"

using namespace animage;

namespace {

struct Fixture {
    Document doc;
    TimelineId timeline;
    LayerId layer;

    Fixture() {
        timeline = doc.addTimeline("main");
        layer = doc.addLayer(timeline, "layer 1");
    }
    const Timeline& tl() const { return *doc.scene().findTimeline(timeline); }
};

void paintDot(Document& doc, TimelineId timeline, ImageId image, LayerId layer, float x, float y) {
    ScopedCommand command(doc, "Dot");
    BrushSettings settings;
    settings.radius = 5.0f;
    settings.pressure_affects_opacity = false;
    Brush brush(settings);
    brush.begin(doc, timeline, image, layer, {x, y, 1.0f});
    brush.end();
}

// What a drag on the end of a held drawing does: many slot edits, one command.
void stretchingExposureIsOneUndoStep() {
    TEST("a whole exposure drag collapses into one undo step");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.timeline, 0);
    const ImageId b = f.doc.insertImage(f.timeline, 1);
    CHECK_EQ(f.tl().frameCount(), std::size_t{2});

    const std::size_t before = f.doc.undoDepth();

    // The drag: grow one frame at a time as the pointer moves, then shrink
    // back past the start, all inside a single command.
    f.doc.beginCommand("Change exposure");
    for (int i = 0; i < 6; ++i) f.doc.extendExposure(f.timeline, 0, 1);
    f.doc.removeSlot(f.timeline, 0);
    f.doc.removeSlot(f.timeline, 0);
    f.doc.endCommand();

    CHECK_EQ(f.doc.undoDepth(), before + 1);
    CHECK_EQ(f.tl().exposureOf(a), std::size_t{5});
    CHECK_EQ(f.tl().frameCount(), std::size_t{6});

    CHECK(f.doc.undo());
    CHECK_EQ(f.tl().frameCount(), std::size_t{2});
    CHECK_EQ(f.tl().exposureOf(a), std::size_t{1});
    CHECK_EQ(f.tl().imageAtSlot(0), a);
    CHECK_EQ(f.tl().imageAtSlot(1), b);

    CHECK(f.doc.redo());
    CHECK_EQ(f.tl().frameCount(), std::size_t{6});
    CHECK_EQ(f.tl().exposureOf(a), std::size_t{5});
}

// Holding a drawing must not touch a cel. This is the property that makes
// timing changes free, and it is easy to break by "helpfully" materialising
// something per frame.
void holdingCostsNothing() {
    TEST("holding a drawing allocates nothing");
    Fixture f;
    const ImageId image = f.doc.insertImage(f.timeline, 0);
    paintDot(f.doc, f.timeline, image, f.layer, 40.0f, 40.0f);

    const std::size_t cels = f.doc.celCount();
    const std::size_t tiles = f.doc.totalTileCount();

    f.doc.extendExposure(f.timeline, 0, 47);
    CHECK_EQ(f.tl().frameCount(), std::size_t{48});
    CHECK_EQ(f.tl().images.size(), std::size_t{1});
    CHECK_EQ(f.doc.celCount(), cels);
    CHECK_EQ(f.doc.totalTileCount(), tiles);
}

void shorteningKeepsTheDrawingUntilTheLastSlotGoes() {
    TEST("a drawing survives until its last slot is removed");
    Fixture f;
    const ImageId image = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 2);
    paintDot(f.doc, f.timeline, image, f.layer, 40.0f, 40.0f);

    f.doc.removeSlot(f.timeline, 0);
    CHECK(f.tl().findImage(image) != nullptr);
    f.doc.removeSlot(f.timeline, 0);
    CHECK(f.tl().findImage(image) != nullptr);
    CHECK_EQ(f.tl().exposureOf(image), std::size_t{1});

    f.doc.removeSlot(f.timeline, 0);
    CHECK(f.tl().findImage(image) == nullptr);
    CHECK_EQ(f.tl().frameCount(), std::size_t{0});
}

void duplicateLandsRightAfterTheOriginal() {
    TEST("a duplicated drawing lands in the next slot");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.timeline, 0);
    const ImageId b = f.doc.insertImage(f.timeline, 1);
    paintDot(f.doc, f.timeline, a, f.layer, 60.0f, 60.0f);

    const ImageId copy = f.doc.duplicateImage(f.timeline, 0);
    CHECK_EQ(f.tl().frameCount(), std::size_t{3});
    CHECK_EQ(f.tl().imageAtSlot(0), a);
    CHECK_EQ(f.tl().imageAtSlot(1), copy);
    CHECK_EQ(f.tl().imageAtSlot(2), b);

    CHECK(f.doc.undo());
    CHECK_EQ(f.tl().frameCount(), std::size_t{2});
    CHECK_EQ(f.tl().imageAtSlot(1), b);
}

// Onion skin walks distinct drawings outwards. A drawing held for many frames
// is one neighbour; the interface never has to know that.
void onionNeighboursSkipHeldFrames() {
    TEST("onion neighbours are distinct drawings, however long they are held");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 7);  // a held for 8
    const ImageId b = f.doc.insertImage(f.timeline, 8);
    f.doc.extendExposure(f.timeline, 8, 3);  // b held for 4
    const ImageId c = f.doc.insertImage(f.timeline, 12);
    CHECK_EQ(f.tl().frameCount(), std::size_t{13});

    // Standing in the middle of b's hold, one drawing each way.
    const std::vector<ImageId> back = f.tl().distinctNeighbours(10, 2, -1);
    CHECK_EQ(back.size(), std::size_t{1});
    CHECK_EQ(back[0], a);

    const std::vector<ImageId> forward = f.tl().distinctNeighbours(10, 2, +1);
    CHECK_EQ(forward.size(), std::size_t{1});
    CHECK_EQ(forward[0], c);

    // And from the very first frame there is nothing behind.
    CHECK_EQ(f.tl().distinctNeighbours(0, 3, -1).size(), std::size_t{0});
}

// Drawing on a held image shows on every frame of the hold: the thing the
// whole data model exists to make true.
void drawingOnAHoldShowsOnEveryFrameOfIt() {
    TEST("drawing on a held drawing changes every frame of the hold");
    Fixture f;
    const ImageId image = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 3);
    paintDot(f.doc, f.timeline, image, f.layer, 70.0f, 70.0f);

    for (std::size_t slot = 0; slot < 4; ++slot) {
        const Cel* cel = f.doc.celAt(f.timeline, f.tl().imageAtSlot(slot), f.layer);
        CHECK(cel != nullptr);
        CHECK_NEAR(cel->pixel(70, 70).a, 1.0, 1e-2);
    }
}

// "Hold shorter" and "delete drawing" are different operations and the
// interface offers both. Removing a slot shortens the hold; removing the
// drawing takes every frame it appears on.
void deletingADrawingTakesEveryFrameOfIt() {
    TEST("deleting a drawing removes every frame it is held on");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 3);  // a held for 4
    const ImageId b = f.doc.insertImage(f.timeline, 4);
    paintDot(f.doc, f.timeline, a, f.layer, 30.0f, 30.0f);
    CHECK_EQ(f.tl().frameCount(), std::size_t{5});

    const std::size_t before = f.doc.undoDepth();
    f.doc.removeDrawing(f.timeline, a);

    CHECK_EQ(f.tl().frameCount(), std::size_t{1});
    CHECK(f.tl().findImage(a) == nullptr);
    CHECK_EQ(f.tl().imageAtSlot(0), b);
    CHECK_EQ(f.doc.undoDepth(), before + 1);  // one step, not four

    CHECK(f.doc.undo());
    CHECK_EQ(f.tl().frameCount(), std::size_t{5});
    CHECK_EQ(f.tl().exposureOf(a), std::size_t{4});
    const Cel* restored = f.doc.celAt(f.timeline, a, f.layer);
    CHECK(restored != nullptr);
    CHECK_NEAR(restored->pixel(30, 30).a, 1.0, 1e-2);
}

void clearingALayerLeavesOtherDrawingsAlone() {
    TEST("clearing a layer affects only the drawing it is on");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.timeline, 0);
    const ImageId b = f.doc.insertImage(f.timeline, 1);
    paintDot(f.doc, f.timeline, a, f.layer, 20.0f, 20.0f);
    paintDot(f.doc, f.timeline, b, f.layer, 20.0f, 20.0f);

    f.doc.clearCel(f.timeline, a, f.layer);
    CHECK(f.doc.celAt(f.timeline, a, f.layer) == nullptr);
    const Cel* untouched = f.doc.celAt(f.timeline, b, f.layer);
    CHECK(untouched != nullptr);
    CHECK_NEAR(untouched->pixel(20, 20).a, 1.0, 1e-2);

    CHECK(f.doc.undo());
    const Cel* restored = f.doc.celAt(f.timeline, a, f.layer);
    CHECK(restored != nullptr);
    CHECK_NEAR(restored->pixel(20, 20).a, 1.0, 1e-2);
}

// "After this drawing" has to mean after the whole hold. Landing a new drawing
// in the middle of a ten-frame hold splits it in two, which is never what was
// meant by pressing insert.
void runBoundsCoverTheWholeHold() {
    TEST("run bounds cover the whole hold");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 9);  // a held for 10
    const ImageId b = f.doc.insertImage(f.timeline, 10);
    CHECK_EQ(f.tl().frameCount(), std::size_t{11});

    // From anywhere inside the hold, the same bounds.
    for (std::size_t slot = 0; slot < 10; ++slot) {
        const auto [first, last] = f.tl().runBounds(slot);
        CHECK_EQ(first, std::size_t{0});
        CHECK_EQ(last, std::size_t{9});
    }
    const auto [first, last] = f.tl().runBounds(10);
    CHECK_EQ(first, std::size_t{10});
    CHECK_EQ(last, std::size_t{10});

    // Inserting after the hold leaves it unbroken.
    const std::size_t after = f.tl().runBounds(4).second + 1;
    CHECK_EQ(after, std::size_t{10});
    const ImageId inserted = f.doc.insertImage(f.timeline, after);

    CHECK_EQ(f.tl().exposureOf(a), std::size_t{10});
    for (std::size_t slot = 0; slot < 10; ++slot) CHECK_EQ(f.tl().imageAtSlot(slot), a);
    CHECK_EQ(f.tl().imageAtSlot(10), inserted);
    CHECK_EQ(f.tl().imageAtSlot(11), b);
}

// Dragging a drawing in the timeline reorders it. Its holds travel with it:
// moving a drawing without its exposure would silently change the timing.
void movingADrawingCarriesItsHolds() {
    TEST("a moved drawing takes its holds with it");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 2);  // a held for 3
    const ImageId b = f.doc.insertImage(f.timeline, 3);
    const ImageId c = f.doc.insertImage(f.timeline, 4);
    paintDot(f.doc, f.timeline, a, f.layer, 25.0f, 25.0f);
    CHECK_EQ(f.tl().frameCount(), std::size_t{5});

    const std::size_t cels = f.doc.celCount();
    const std::size_t tiles = f.doc.totalTileCount();
    const std::size_t before = f.doc.undoDepth();

    // Drop a after both b and c: with a lifted out the timeline is [b, c], so
    // the destination index is 2.
    f.doc.moveDrawing(f.timeline, a, 2);

    CHECK_EQ(f.tl().frameCount(), std::size_t{5});
    CHECK_EQ(f.tl().imageAtSlot(0), b);
    CHECK_EQ(f.tl().imageAtSlot(1), c);
    CHECK_EQ(f.tl().imageAtSlot(2), a);
    CHECK_EQ(f.tl().imageAtSlot(3), a);
    CHECK_EQ(f.tl().imageAtSlot(4), a);
    CHECK_EQ(f.tl().exposureOf(a), std::size_t{3});

    // Reordering only: nothing was drawn, copied or thrown away.
    CHECK_EQ(f.doc.celCount(), cels);
    CHECK_EQ(f.doc.totalTileCount(), tiles);
    CHECK_EQ(f.doc.undoDepth(), before + 1);

    const Cel* kept = f.doc.celAt(f.timeline, a, f.layer);
    CHECK(kept != nullptr);
    CHECK_NEAR(kept->pixel(25, 25).a, 1.0, 1e-2);

    CHECK(f.doc.undo());
    CHECK_EQ(f.tl().imageAtSlot(0), a);
    CHECK_EQ(f.tl().imageAtSlot(3), b);
    CHECK_EQ(f.tl().imageAtSlot(4), c);
}

void movingADrawingToWhereItAlreadyIsDoesNothing() {
    TEST("dropping a drawing where it already is records no command");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.timeline, 0);
    f.doc.insertImage(f.timeline, 1);
    const std::size_t before = f.doc.undoDepth();

    f.doc.moveDrawing(f.timeline, a, 0);
    CHECK_EQ(f.doc.undoDepth(), before);
    CHECK_EQ(f.tl().imageAtSlot(0), a);

    // Past the end clamps rather than losing the drawing.
    f.doc.moveDrawing(f.timeline, a, 999);
    CHECK_EQ(f.tl().frameCount(), std::size_t{2});
    CHECK_EQ(f.tl().imageAtSlot(1), a);
}

// The interface only offers a drag from the numbered card, and runBounds is
// what tells it which slot that is.
void onlyTheFirstSlotOfARunIsTheCard() {
    TEST("only the first slot of a hold is the drawing's card");
    Fixture f;
    f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 4);
    f.doc.insertImage(f.timeline, 5);

    CHECK(f.tl().runBounds(0).first == 0);
    for (std::size_t slot = 1; slot <= 4; ++slot) {
        CHECK(f.tl().runBounds(slot).first != slot);  // a held frame, not a card
    }
    CHECK(f.tl().runBounds(5).first == 5);
}

// A drawing keeps the number it was born with. Deriving it from position meant
// dragging a drawing renumbered it, which is precisely the moment you need to
// know which one you are holding.
void drawingNumbersSurviveReordering() {
    TEST("a drawing keeps its number when the timing changes");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.timeline, 0);
    const ImageId b = f.doc.insertImage(f.timeline, 1);
    const ImageId c = f.doc.insertImage(f.timeline, 2);

    CHECK_EQ(f.tl().findImage(a)->number, 1);
    CHECK_EQ(f.tl().findImage(b)->number, 2);
    CHECK_EQ(f.tl().findImage(c)->number, 3);

    f.doc.moveDrawing(f.timeline, c, 0);  // drag the last drawing to the front
    CHECK_EQ(f.tl().imageAtSlot(0), c);
    CHECK_EQ(f.tl().findImage(c)->number, 3);  // still 3, though it is now first
    CHECK_EQ(f.tl().findImage(a)->number, 1);
    CHECK_EQ(f.tl().findImage(b)->number, 2);

    // Holding it does not change it either, and a copy is a new drawing.
    f.doc.extendExposure(f.timeline, 0, 3);
    CHECK_EQ(f.tl().findImage(c)->number, 3);
    const ImageId copy = f.doc.duplicateImage(f.timeline, 0);
    CHECK_EQ(f.tl().findImage(copy)->number, 4);

    // And a number is never reused, even after the drawing is gone.
    f.doc.removeDrawing(f.timeline, copy);
    const ImageId fresh = f.doc.insertImage(f.timeline, 0);
    CHECK_EQ(f.tl().findImage(fresh)->number, 5);
}

void layerNamesStayUnique() {
    TEST("layer names cannot collide");
    Fixture f;  // starts with "layer 1"
    const LayerId second = f.doc.addLayer(f.timeline, "layer 1");
    CHECK(second != kNoId);
    CHECK(f.tl().findLayer(second)->name != "layer 1");

    std::vector<std::string> names;
    for (const Layer& layer : f.tl().layers) names.push_back(layer.name);
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
}

}  // namespace

int main() {
    std::printf("timeline:\n");
    deletingADrawingTakesEveryFrameOfIt();
    clearingALayerLeavesOtherDrawingsAlone();
    runBoundsCoverTheWholeHold();
    movingADrawingCarriesItsHolds();
    movingADrawingToWhereItAlreadyIsDoesNothing();
    onlyTheFirstSlotOfARunIsTheCard();
    drawingNumbersSurviveReordering();
    layerNamesStayUnique();
    stretchingExposureIsOneUndoStep();
    holdingCostsNothing();
    shorteningKeepsTheDrawingUntilTheLastSlotGoes();
    duplicateLandsRightAfterTheOriginal();
    onionNeighboursSkipHeldFrames();
    drawingOnAHoldShowsOnEveryFrameOfIt();
    return testing::summarise("timeline");
}
