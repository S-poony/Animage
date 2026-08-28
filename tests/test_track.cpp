// SPDX-License-Identifier: GPL-3.0-or-later
//
// The track operations M3's interface is built on. The stretch-to-hold drag
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
    TrackId track;
    LayerId layer;

    Fixture() {
        track = doc.addTrack("main");
        layer = doc.addLayer(track, "layer 1");
    }
    const Track& tl() const { return *doc.scene().findTrack(track); }
};

void paintDot(Document& doc, TrackId track, ImageId image, LayerId layer, float x, float y) {
    ScopedCommand command(doc, "Dot");
    BrushSettings settings;
    settings.radius = 5.0f;
    settings.pressure_affects_opacity = false;
    Brush brush(settings);
    brush.begin(doc, track, image, layer, {x, y, 1.0f});
    brush.end();
}

// What a drag on the end of a held drawing does: many slot edits, one command.
void stretchingExposureIsOneUndoStep() {
    TEST("a whole exposure drag collapses into one undo step");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.track, 0);
    const ImageId b = f.doc.insertImage(f.track, 1);
    CHECK_EQ(f.tl().frameCount(), std::size_t{2});

    const std::size_t before = f.doc.undoDepth();

    // The drag: grow one frame at a time as the pointer moves, then shrink
    // back past the start, all inside a single command.
    f.doc.beginCommand("Change exposure");
    for (int i = 0; i < 6; ++i) f.doc.extendExposure(f.track, 0, 1);
    f.doc.removeSlot(f.track, 0);
    f.doc.removeSlot(f.track, 0);
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
    const ImageId image = f.doc.insertImage(f.track, 0);
    paintDot(f.doc, f.track, image, f.layer, 40.0f, 40.0f);

    const std::size_t cels = f.doc.celCount();
    const std::size_t tiles = f.doc.totalTileCount();

    f.doc.extendExposure(f.track, 0, 47);
    CHECK_EQ(f.tl().frameCount(), std::size_t{48});
    CHECK_EQ(f.tl().images.size(), std::size_t{1});
    CHECK_EQ(f.doc.celCount(), cels);
    CHECK_EQ(f.doc.totalTileCount(), tiles);
}

void shorteningKeepsTheDrawingUntilTheLastSlotGoes() {
    TEST("a drawing survives until its last slot is removed");
    Fixture f;
    const ImageId image = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 2);
    paintDot(f.doc, f.track, image, f.layer, 40.0f, 40.0f);

    f.doc.removeSlot(f.track, 0);
    CHECK(f.tl().findImage(image) != nullptr);
    f.doc.removeSlot(f.track, 0);
    CHECK(f.tl().findImage(image) != nullptr);
    CHECK_EQ(f.tl().exposureOf(image), std::size_t{1});

    f.doc.removeSlot(f.track, 0);
    CHECK(f.tl().findImage(image) == nullptr);
    CHECK_EQ(f.tl().frameCount(), std::size_t{0});
}

void duplicateLandsRightAfterTheOriginal() {
    TEST("a duplicated drawing lands in the next slot");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.track, 0);
    const ImageId b = f.doc.insertImage(f.track, 1);
    paintDot(f.doc, f.track, a, f.layer, 60.0f, 60.0f);

    const ImageId copy = f.doc.duplicateImage(f.track, 0);
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
    const ImageId a = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 7);  // a held for 8
    const ImageId b = f.doc.insertImage(f.track, 8);
    f.doc.extendExposure(f.track, 8, 3);  // b held for 4
    const ImageId c = f.doc.insertImage(f.track, 12);
    CHECK_EQ(f.tl().frameCount(), std::size_t{13});

    // Standing in the middle of b's hold, one drawing each way.
    CHECK_EQ(f.tl().imageAtSlot(10), b);
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
    const ImageId image = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 3);
    paintDot(f.doc, f.track, image, f.layer, 70.0f, 70.0f);

    for (std::size_t slot = 0; slot < 4; ++slot) {
        const Cel* cel = f.doc.celAt(f.track, f.tl().imageAtSlot(slot), f.layer);
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
    const ImageId a = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 3);  // a held for 4
    const ImageId b = f.doc.insertImage(f.track, 4);
    paintDot(f.doc, f.track, a, f.layer, 30.0f, 30.0f);
    CHECK_EQ(f.tl().frameCount(), std::size_t{5});

    const std::size_t before = f.doc.undoDepth();
    f.doc.removeDrawing(f.track, a);

    CHECK_EQ(f.tl().frameCount(), std::size_t{1});
    CHECK(f.tl().findImage(a) == nullptr);
    CHECK_EQ(f.tl().imageAtSlot(0), b);
    CHECK_EQ(f.doc.undoDepth(), before + 1);  // one step, not four

    CHECK(f.doc.undo());
    CHECK_EQ(f.tl().frameCount(), std::size_t{5});
    CHECK_EQ(f.tl().exposureOf(a), std::size_t{4});
    const Cel* restored = f.doc.celAt(f.track, a, f.layer);
    CHECK(restored != nullptr);
    CHECK_NEAR(restored->pixel(30, 30).a, 1.0, 1e-2);
}

void clearingALayerLeavesOtherDrawingsAlone() {
    TEST("clearing a layer affects only the drawing it is on");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.track, 0);
    const ImageId b = f.doc.insertImage(f.track, 1);
    paintDot(f.doc, f.track, a, f.layer, 20.0f, 20.0f);
    paintDot(f.doc, f.track, b, f.layer, 20.0f, 20.0f);

    f.doc.clearCel(f.track, a, f.layer);
    CHECK(f.doc.celAt(f.track, a, f.layer) == nullptr);
    const Cel* untouched = f.doc.celAt(f.track, b, f.layer);
    CHECK(untouched != nullptr);
    CHECK_NEAR(untouched->pixel(20, 20).a, 1.0, 1e-2);

    CHECK(f.doc.undo());
    const Cel* restored = f.doc.celAt(f.track, a, f.layer);
    CHECK(restored != nullptr);
    CHECK_NEAR(restored->pixel(20, 20).a, 1.0, 1e-2);
}

// "After this drawing" has to mean after the whole hold. Landing a new drawing
// in the middle of a ten-frame hold splits it in two, which is never what was
// meant by pressing insert.
void runBoundsCoverTheWholeHold() {
    TEST("run bounds cover the whole hold");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 9);  // a held for 10
    const ImageId b = f.doc.insertImage(f.track, 10);
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
    const ImageId inserted = f.doc.insertImage(f.track, after);

    CHECK_EQ(f.tl().exposureOf(a), std::size_t{10});
    for (std::size_t slot = 0; slot < 10; ++slot) CHECK_EQ(f.tl().imageAtSlot(slot), a);
    CHECK_EQ(f.tl().imageAtSlot(10), inserted);
    CHECK_EQ(f.tl().imageAtSlot(11), b);
}

// Dragging a drawing in the track reorders it. Its holds travel with it:
// moving a drawing without its exposure would silently change the timing.
void movingADrawingCarriesItsHolds() {
    TEST("a moved drawing takes its holds with it");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 2);  // a held for 3
    const ImageId b = f.doc.insertImage(f.track, 3);
    const ImageId c = f.doc.insertImage(f.track, 4);
    paintDot(f.doc, f.track, a, f.layer, 25.0f, 25.0f);
    CHECK_EQ(f.tl().frameCount(), std::size_t{5});

    const std::size_t cels = f.doc.celCount();
    const std::size_t tiles = f.doc.totalTileCount();
    const std::size_t before = f.doc.undoDepth();

    // Drop a after both b and c: with a lifted out the track is [b, c], so
    // the destination index is 2.
    f.doc.moveDrawing(f.track, a, 2);

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

    const Cel* kept = f.doc.celAt(f.track, a, f.layer);
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
    const ImageId a = f.doc.insertImage(f.track, 0);
    f.doc.insertImage(f.track, 1);
    const std::size_t before = f.doc.undoDepth();

    f.doc.moveDrawing(f.track, a, 0);
    CHECK_EQ(f.doc.undoDepth(), before);
    CHECK_EQ(f.tl().imageAtSlot(0), a);

    // Past the end clamps rather than losing the drawing.
    f.doc.moveDrawing(f.track, a, 999);
    CHECK_EQ(f.tl().frameCount(), std::size_t{2});
    CHECK_EQ(f.tl().imageAtSlot(1), a);
}

// The interface only offers a drag from the numbered card, and runBounds is
// what tells it which slot that is.
void onlyTheFirstSlotOfARunIsTheCard() {
    TEST("only the first slot of a hold is the drawing's card");
    Fixture f;
    f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 4);
    f.doc.insertImage(f.track, 5);

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
    const ImageId a = f.doc.insertImage(f.track, 0);
    const ImageId b = f.doc.insertImage(f.track, 1);
    const ImageId c = f.doc.insertImage(f.track, 2);

    CHECK_EQ(f.tl().findImage(a)->number, 1);
    CHECK_EQ(f.tl().findImage(b)->number, 2);
    CHECK_EQ(f.tl().findImage(c)->number, 3);

    f.doc.moveDrawing(f.track, c, 0);  // drag the last drawing to the front
    CHECK_EQ(f.tl().imageAtSlot(0), c);
    CHECK_EQ(f.tl().findImage(c)->number, 3);  // still 3, though it is now first
    CHECK_EQ(f.tl().findImage(a)->number, 1);
    CHECK_EQ(f.tl().findImage(b)->number, 2);

    // Holding it does not change it either, and a copy is a new drawing.
    f.doc.extendExposure(f.track, 0, 3);
    CHECK_EQ(f.tl().findImage(c)->number, 3);
    const ImageId copy = f.doc.duplicateImage(f.track, 0);
    CHECK_EQ(f.tl().findImage(copy)->number, 4);

    // A number *is* reused once the drawing carrying it has gone: 4 is free
    // again, so the next drawing is 4 and not 5. See Track::nextDrawingNumber.
    f.doc.removeDrawing(f.track, copy);
    const ImageId fresh = f.doc.insertImage(f.track, 0);
    CHECK_EQ(f.tl().findImage(fresh)->number, 4);
}

// Reported as a timeline bug and it was one: delete drawing 2 and the next
// drawing you made came back as 3, with no 2 in the track at all.
void anewDrawingTakesTheLowestFreeNumber() {
    TEST("a new drawing fills the lowest gap in the numbering");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.track, 0);
    const ImageId b = f.doc.insertImage(f.track, 1);
    const ImageId c = f.doc.insertImage(f.track, 2);
    CHECK_EQ(f.tl().findImage(b)->number, 2);

    // The reported case: take the middle one out and make another.
    f.doc.removeDrawing(f.track, b);
    const ImageId filled = f.doc.insertImage(f.track, 1);
    CHECK_EQ(f.tl().findImage(filled)->number, 2);

    // With nothing free, it carries on counting.
    const ImageId next = f.doc.insertImage(f.track, 3);
    CHECK_EQ(f.tl().findImage(next)->number, 4);

    // A gap at the front is a gap like any other.
    f.doc.removeDrawing(f.track, a);
    const ImageId first = f.doc.insertImage(f.track, 0);
    CHECK_EQ(f.tl().findImage(first)->number, 1);

    // And two drawings never share a number, which is the property the card
    // depends on to identify anything at all.
    std::vector<int> numbers;
    for (const auto& [id, image] : f.tl().images) numbers.push_back(image.number);
    std::sort(numbers.begin(), numbers.end());
    CHECK(std::adjacent_find(numbers.begin(), numbers.end()) == numbers.end());
    CHECK_EQ(f.tl().findImage(c)->number, 3);  // untouched throughout
}

// Undo puts the drawing back, so it also puts its number back in use -- and the
// next drawing has to see that rather than handing the same one out twice.
void undoingADeletionPutsItsNumberBackInUse() {
    TEST("a number freed by a deletion is taken again only while it is free");
    Fixture f;
    f.doc.insertImage(f.track, 0);
    const ImageId b = f.doc.insertImage(f.track, 1);
    CHECK_EQ(f.tl().findImage(b)->number, 2);

    f.doc.removeDrawing(f.track, b);
    CHECK(f.doc.undo());  // 2 is back
    CHECK_EQ(f.tl().findImage(b)->number, 2);

    const ImageId made = f.doc.insertImage(f.track, 2);
    CHECK_EQ(f.tl().findImage(made)->number, 3);
}

// --- overwriting drawings ------------------------------------------------
//
// The worked example from the issue, arithmetic and all: Drawing1 held 11, the
// playhead on frame 4, press insert. Drawing1 keeps 3 frames, the new drawing
// takes the other 8, and the track is still 11 frames long.
void overwritingSpendsTheHoldRatherThanLengtheningTheTrack() {
    TEST("adding a drawing over a hold spends it instead of lengthening the track");
    Fixture f;
    const ImageId one = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 10);  // held 11
    CHECK_EQ(f.tl().frameCount(), std::size_t{11});

    TrackProperties props = f.tl().properties();
    props.overwrite_drawings = true;
    f.doc.updateTrack(f.track, props);

    const std::size_t before = f.doc.undoDepth();
    const ImageId two = f.doc.addDrawing(f.track, 3);  // frame 4, counting from 1

    CHECK_EQ(f.tl().frameCount(), std::size_t{11});  // the shot is the length it was
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{3});
    CHECK_EQ(f.tl().exposureOf(two), std::size_t{8});
    CHECK_EQ(f.tl().firstSlotOf(two), std::size_t{3});
    for (std::size_t slot = 0; slot < 3; ++slot) CHECK_EQ(f.tl().imageAtSlot(slot), one);
    for (std::size_t slot = 3; slot < 11; ++slot) CHECK_EQ(f.tl().imageAtSlot(slot), two);

    CHECK_EQ(f.doc.undoDepth(), before + 1);
    CHECK(f.doc.undo());
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{11});
    CHECK(f.tl().findImage(two) == nullptr);
}

// Switched off, the shot lengthens exactly as it did before the setting
// existed. On is the default now, so this is the path that has to be asked for.
void withoutOverwritingAddingADrawingStillLengthensTheTrack() {
    TEST("without overwriting a new drawing goes in after the hold");
    Fixture f;
    const ImageId one = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 10);

    TrackProperties props = f.tl().properties();
    props.overwrite_drawings = false;
    f.doc.updateTrack(f.track, props);

    const ImageId two = f.doc.addDrawing(f.track, 3);
    CHECK_EQ(f.tl().frameCount(), std::size_t{12});
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{11});
    CHECK_EQ(f.tl().firstSlotOf(two), std::size_t{11});
}

// The exception to the rule, and the reason it is an exception: taking the rest
// of the hold from its *first* frame would take the whole drawing.
void overwritingNeverTakesADrawingsLastFrame() {
    TEST("overwriting leaves the drawing it lands on at least one frame");
    Fixture f;
    const ImageId one = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 3);  // held 4
    TrackProperties props = f.tl().properties();
    props.overwrite_drawings = true;
    f.doc.updateTrack(f.track, props);

    // Standing on the first frame of the hold: the new drawing starts one later.
    const ImageId two = f.doc.addDrawing(f.track, 0);
    CHECK_EQ(f.tl().frameCount(), std::size_t{4});
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{1});
    CHECK_EQ(f.tl().exposureOf(two), std::size_t{3});
    CHECK_EQ(f.tl().firstSlotOf(two), std::size_t{1});

    // And a hold of one frame has nothing to spare at all, so rather than wipe
    // it the track goes back to getting longer.
    const std::size_t frames = f.tl().frameCount();
    const ImageId three = f.doc.addDrawing(f.track, 0);
    CHECK_EQ(f.tl().frameCount(), frames + 1);
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{1});
    CHECK_EQ(f.tl().firstSlotOf(three), std::size_t{1});
}

void duplicatingOverwritesTheSameWay() {
    TEST("duplicating a drawing overwrites the hold too");
    Fixture f;
    const ImageId one = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 10);  // held 11
    paintDot(f.doc, f.track, one, f.layer, 40.0f, 40.0f);
    TrackProperties props = f.tl().properties();
    props.overwrite_drawings = true;
    f.doc.updateTrack(f.track, props);

    const ImageId copy = f.doc.duplicateDrawing(f.track, 3);
    CHECK_EQ(f.tl().frameCount(), std::size_t{11});
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{3});
    CHECK_EQ(f.tl().exposureOf(copy), std::size_t{8});

    // A copy and not a hold: the two drawings have cels of their own.
    const Cel* original = f.doc.celAt(f.track, one, f.layer);
    const Cel* duplicate = f.doc.celAt(f.track, copy, f.layer);
    CHECK(original != nullptr && duplicate != nullptr);
    CHECK(original != duplicate);
    CHECK_NEAR(duplicate->pixel(40, 40).a, 1.0, 1e-2);
}

// **A drawing carries a picture in two different ways, and a copy has to take
// both.** A raster layer has a cel; a reference layer has an entry in
// `source_frames` naming which frame of the imported file it shows. Copying
// only the cels produced a duplicate with the drawn part intact and the
// imported part gone -- which on a track that is nothing but an import looks
// like the command doing nothing at all, and was reported that way.
//
// The mixed track is the case worth pinning rather than the pure one, because
// it is the one where the failure hides: half the drawing came through, so it
// reads as having worked.
void duplicatingADrawingTakesTheImportedPictureWithIt() {
    TEST("duplicating a drawing takes its imported picture as well as its cels");
    Fixture f;
    const LayerId imported =
        f.doc.addLayer(f.track, "modelsheet", 1, LayerKind::Reference);

    const ImageId one = f.doc.insertImage(f.track, 0);
    paintDot(f.doc, f.track, one, f.layer, 40.0f, 40.0f);
    f.doc.setSourceFrame(f.track, one, imported, 7);
    CHECK_EQ(f.tl().findImage(one)->sourceFrameFor(imported), 7);

    const ImageId copy = f.doc.duplicateDrawing(f.track, 0);
    CHECK(copy != kNoId);
    const Image* made = f.tl().findImage(copy);
    CHECK(made != nullptr);

    // The drawn half: its own cel, not the original's.
    const Cel* original = f.doc.celAt(f.track, one, f.layer);
    const Cel* duplicate = f.doc.celAt(f.track, copy, f.layer);
    CHECK(original != nullptr && duplicate != nullptr);
    CHECK(original != duplicate);
    CHECK_NEAR(duplicate->pixel(40, 40).a, 1.0, 1e-2);

    // And the imported half, which is the part that used to go missing. Not
    // copied pixels -- a reference layer has none -- but the same frame of the
    // same file, which is what holding an imported frame twice means.
    CHECK_EQ(made->sourceFrameFor(imported), 7);
}

// The same thing on a track that is nothing but an import, which is how it was
// found: the whole drawing came back blank, so the command looked like a
// refusal that had forgotten to say why.
void duplicatingAnImportOnItsOwnTrackIsNotABlankDrawing() {
    TEST("duplicating an import on its own track is not a blank drawing");
    Document doc;
    const TrackId track = doc.addTrack("modelsheet");
    const LayerId imported =
        doc.addLayer(track, "modelsheet", 0, LayerKind::Reference);
    const ImageId one = doc.insertImage(track, 0);
    doc.setSourceFrame(track, one, imported, 0);

    const ImageId copy = doc.duplicateDrawing(track, 0);
    const Track& line = *doc.scene().findTrack(track);
    CHECK(copy != kNoId);
    CHECK_EQ(line.frameCount(), std::size_t{2});
    CHECK_EQ(line.findImage(copy)->sourceFrameFor(imported), 0);
}

// --- duplicating a whole track ---------------------------------------------
//
// **Every id inside a track means something only within it, and three of them
// point at each other.** `Image::cels` and `Image::source_frames` are keyed on
// layer ids, `Track::slots` names image ids, and a colour layer's `ctg_sources`
// names the line-art layers it is cut against. A copy that reused any of them
// would be a second track wired to the first one's insides.
void aDuplicatedTrackHasItsOwnDrawingsAndItsOwnPixels() {
    TEST("a duplicated track has its own drawings and its own pixels");
    Fixture f;
    const ImageId one = f.doc.insertImage(f.track, 0);
    f.doc.insertImage(f.track, 1);
    f.doc.extendExposure(f.track, 0, 2);  // a hold, so the slot shape is worth checking
    paintDot(f.doc, f.track, one, f.layer, 40.0f, 40.0f);

    const TrackId copy_id = f.doc.duplicateTrack(f.track);
    CHECK(copy_id != kNoId);
    CHECK(copy_id != f.track);
    const Track& original = f.tl();
    const Track& copy = *f.doc.scene().findTrack(copy_id);

    // Directly under the one it came from, which is where a duplicate belongs.
    CHECK_EQ(f.doc.scene().tracks.size(), std::size_t{2});
    CHECK_EQ(f.doc.scene().tracks[0].id, f.track);
    CHECK_EQ(f.doc.scene().tracks[1].id, copy_id);
    CHECK_EQ(copy.name, original.name + " copy");

    // The same shape in time, holds and all.
    CHECK_EQ(copy.slots.size(), original.slots.size());
    CHECK_EQ(copy.frameCount(), original.frameCount());
    CHECK_EQ(copy.images.size(), original.images.size());
    for (std::size_t i = 0; i < copy.slots.size(); ++i) {
        // The same *pattern* of repeats -- a hold is the same id in consecutive
        // slots -- made of entirely different ids.
        CHECK_EQ(copy.slots[i] == copy.slots[0], original.slots[i] == original.slots[0]);
        CHECK(copy.slots[i] != original.slots[i]);
    }

    // And its own pixels: the dot came across, in a cel of its own.
    const LayerId copied_layer = copy.layers.front().id;
    CHECK(copied_layer != f.layer);
    const Cel* here = f.doc.celAt(f.track, one, f.layer);
    const Cel* there = f.doc.celAt(copy_id, copy.slots[0], copied_layer);
    CHECK(here != nullptr && there != nullptr);
    CHECK(here != there);
    CHECK_NEAR(there->pixel(40, 40).a, 1.0, 1e-2);
}

// **The one that would not look wrong until somebody drew on the original.** A
// colour layer's sources name the line art it is cut against, within its own
// track. Left alone in a copy they would go on naming the *first* track's
// layers, and the copy's fills would be cut against a drawing somewhere else.
void aDuplicatedColourLayerIsCutAgainstItsOwnTracksLineArt() {
    TEST("a duplicated colour layer is cut against its own track's line art");
    Fixture f;
    const LayerId colour = f.doc.addLayer(f.track, "colour", 0, LayerKind::Ctg);
    Layer wired = *f.tl().findLayer(colour);
    wired.ctg_sources = {f.layer};
    f.doc.updateLayer(f.track, colour, wired);
    CHECK_EQ(f.tl().findLayer(colour)->ctg_sources.front(), f.layer);

    const TrackId copy_id = f.doc.duplicateTrack(f.track);
    const Track& copy = *f.doc.scene().findTrack(copy_id);

    // Find the copy's two layers by name, since their ids are new by design.
    const Layer* copied_colour = nullptr;
    const Layer* copied_line = nullptr;
    for (const Layer& layer : copy.layers) {
        if (layer.kind == LayerKind::Ctg) copied_colour = &layer;
        else copied_line = &layer;
    }
    CHECK(copied_colour != nullptr && copied_line != nullptr);
    CHECK_EQ(copied_colour->ctg_sources.size(), std::size_t{1});
    // Its own line art, and specifically not the original's.
    CHECK_EQ(copied_colour->ctg_sources.front(), copied_line->id);
    CHECK(copied_colour->ctg_sources.front() != f.layer);
}

// The other thing a drawing carries a picture with -- see copyOfImage, where
// forgetting it made a duplicated drawing come back blank.
void aDuplicatedTrackKeepsItsImportedPictures() {
    TEST("a duplicated track keeps its imported pictures");
    Fixture f;
    const LayerId imported = f.doc.addLayer(f.track, "board", 1, LayerKind::Reference);
    const ImageId one = f.doc.insertImage(f.track, 0);
    f.doc.setSourceFrame(f.track, one, imported, 5);

    const TrackId copy_id = f.doc.duplicateTrack(f.track);
    const Track& copy = *f.doc.scene().findTrack(copy_id);
    const Layer* copied = nullptr;
    for (const Layer& layer : copy.layers)
        if (layer.kind == LayerKind::Reference) copied = &layer;
    CHECK(copied != nullptr);
    CHECK(copied->id != imported);
    CHECK_EQ(copy.findImage(copy.slots[0])->sourceFrameFor(copied->id), 5);
    // And keyed on the copy's layer, not on the one it was copied from.
    CHECK_EQ(copy.findImage(copy.slots[0])->sourceFrameFor(imported), Image::kNoSourceFrame);
}

void duplicatingATrackUndoesInOneStep() {
    TEST("duplicating a track undoes in one step");
    Fixture f;
    const ImageId one = f.doc.insertImage(f.track, 0);
    paintDot(f.doc, f.track, one, f.layer, 40.0f, 40.0f);

    const std::size_t before = f.doc.undoDepth();
    f.doc.duplicateTrack(f.track);
    CHECK_EQ(f.doc.scene().tracks.size(), std::size_t{2});
    CHECK_EQ(f.doc.undoDepth(), before + 1);

    f.doc.undo();
    CHECK_EQ(f.doc.scene().tracks.size(), std::size_t{1});
    // The original is untouched by the copy having been made and unmade.
    CHECK(f.doc.celAt(f.track, one, f.layer) != nullptr);
    CHECK_NEAR(f.doc.celAt(f.track, one, f.layer)->pixel(40, 40).a, 1.0, 1e-2);

    f.doc.redo();
    CHECK_EQ(f.doc.scene().tracks.size(), std::size_t{2});
}

// A soundtrack is a file name and four numbers, so its copy is cheap -- and it
// is also, today, the only way a scene gets a second soundtrack.
void aDuplicatedSoundtrackPointsAtTheSameFile() {
    TEST("a duplicated soundtrack points at the same file");
    Document doc;
    AudioPlacement placed;
    placed.offset_frames = 12.5;
    placed.gain = 0.4;
    const TrackId first = doc.addAudioTrack("dialogue", "take-3.wav");
    doc.setAudioTrackPlacement(first, placed);

    const TrackId second = doc.duplicateAudioTrack(first);
    CHECK(second != kNoId);
    CHECK(second != first);
    CHECK_EQ(doc.scene().audio_tracks.size(), std::size_t{2});

    const AudioTrack* copy = doc.scene().findAudioTrack(second);
    CHECK(copy != nullptr);
    CHECK_EQ(copy->source, std::string("take-3.wav"));
    CHECK_EQ(copy->name, std::string("dialogue copy"));
    CHECK_NEAR(copy->placement.offset_frames, 12.5, 1e-9);
    CHECK_NEAR(copy->placement.gain, 0.4, 1e-9);

    doc.undo();
    CHECK_EQ(doc.scene().audio_tracks.size(), std::size_t{1});
    // And the one it was copied from is where it was.
    CHECK_EQ(doc.scene().findAudioTrack(first)->source, std::string("take-3.wav"));
}

// The issue's second example. Drawing1 held 11 then Drawing2 held 1; drop
// Drawing2 on frame 4 and it owns everything from there, while the frame it
// came from is absorbed by the drawing beside it rather than disappearing.
void movingOverAHoldTakesTheRestOfItAndLeavesNoGap() {
    TEST("a drawing moved over a hold takes the rest of it");
    Fixture f;
    const ImageId one = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 10);  // held 11
    const ImageId two = f.doc.insertImage(f.track, 11);
    CHECK_EQ(f.tl().frameCount(), std::size_t{12});

    TrackProperties props = f.tl().properties();
    props.overwrite_drawings = true;
    f.doc.updateTrack(f.track, props);

    f.doc.moveDrawingOver(f.track, two, 3);  // dropped on frame 4

    CHECK_EQ(f.tl().frameCount(), std::size_t{12});  // and no frame was lost
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{3});
    CHECK_EQ(f.tl().exposureOf(two), std::size_t{9});
    // One contiguous run each, which is what every walk over the slots assumes.
    CHECK_EQ(f.tl().runBounds(f.tl().firstSlotOf(two)).second, std::size_t{11});

    CHECK(f.doc.undo());
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{11});
    CHECK_EQ(f.tl().imageAtSlot(11), two);
}

// Reported: on `1...2....3.....`, nudging the first drawing one frame swapped it
// with the second -- drawing 2 left holding a single frame and drawing 1 holding
// everything up to drawing 3.
//
// Two faults in one. The run being landed in was read from a copy the drawing
// had already been lifted out of, so it measured as both runs together; and a
// drag that lands inside the drawing's own hold was treated as a move at all.
void nudgingADrawingAlongItsOwnHold() {
    TEST("a drawing dragged along its own hold starts where it was dropped");
    Fixture f;
    const ImageId one = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 3);  // 1 held 4
    const ImageId two = f.doc.insertImage(f.track, 4);
    f.doc.extendExposure(f.track, 4, 4);  // 2 held 5
    const ImageId three = f.doc.insertImage(f.track, 9);
    f.doc.extendExposure(f.track, 9, 5);  // 3 held 6
    CHECK_EQ(f.tl().frameCount(), std::size_t{15});
    CHECK(f.tl().overwrite_drawings);

    // The reported case, and the one thing this cannot do: the first drawing
    // has nowhere to put the frames it would give up, so it does not move.
    const std::size_t before = f.doc.undoDepth();
    f.doc.moveDrawingOver(f.track, one, 1);
    CHECK_EQ(f.doc.undoDepth(), before);  // nothing happened, not even an undo step
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{4});
    CHECK_EQ(f.tl().exposureOf(two), std::size_t{5});  // and no swap
    CHECK_EQ(f.tl().firstSlotOf(one), std::size_t{0});

    // Every other drawing moves along its own hold one frame at a time, and the
    // hold in front grows by exactly what this one gave up.
    f.doc.moveDrawingOver(f.track, two, 5);
    CHECK_EQ(f.tl().frameCount(), std::size_t{15});
    CHECK_EQ(f.tl().firstSlotOf(two), std::size_t{5});
    CHECK_EQ(f.tl().exposureOf(two), std::size_t{4});
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{5});
    CHECK_EQ(f.tl().exposureOf(three), std::size_t{6});  // untouched

    // Dropped exactly where it starts, nothing happens.
    const std::size_t settled = f.doc.undoDepth();
    f.doc.moveDrawingOver(f.track, two, 5);
    CHECK_EQ(f.doc.undoDepth(), settled);

    // And dragged clear of its own hold it takes the rest of the hold it lands
    // in, rather than the whole of it.
    f.doc.moveDrawingOver(f.track, three, 6);
    CHECK_EQ(f.tl().frameCount(), std::size_t{15});
    CHECK_EQ(f.tl().firstSlotOf(three), std::size_t{6});
    CHECK_EQ(f.tl().exposureOf(three), std::size_t{9});
    CHECK_EQ(f.tl().exposureOf(two), std::size_t{1});
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{5});
}

// Leaving from the very start there is no drawing before to take the frames, so
// the one after does.
void framesLeftAtTheStartGoToTheDrawingAfterThem() {
    TEST("frames vacated at the start of a track go to the drawing after them");
    Fixture f;
    const ImageId one = f.doc.insertImage(f.track, 0);
    const ImageId two = f.doc.insertImage(f.track, 1);
    f.doc.extendExposure(f.track, 1, 9);  // two held 10, after one held 1
    TrackProperties props = f.tl().properties();
    props.overwrite_drawings = true;
    f.doc.updateTrack(f.track, props);
    CHECK_EQ(f.tl().frameCount(), std::size_t{11});

    f.doc.moveDrawingOver(f.track, one, 8);

    CHECK_EQ(f.tl().frameCount(), std::size_t{11});
    CHECK_EQ(f.tl().imageAtSlot(0), two);  // its frame was taken over
    CHECK_EQ(f.tl().exposureOf(two), std::size_t{8});
    CHECK_EQ(f.tl().exposureOf(one), std::size_t{3});
    CHECK_EQ(f.tl().firstSlotOf(one), std::size_t{8});
}

// Dropping a drawing back into the gap it left has to be the same as not having
// moved it, or a drag that goes nowhere would still change the timing.
void movingOverToWhereItAlreadyIsDoesNothing() {
    TEST("dropping a drawing over where it already is records no command");
    Fixture f;
    f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 2);
    const ImageId two = f.doc.insertImage(f.track, 3);
    f.doc.extendExposure(f.track, 3, 2);
    TrackProperties props = f.tl().properties();
    props.overwrite_drawings = true;
    f.doc.updateTrack(f.track, props);

    const std::size_t before = f.doc.undoDepth();
    f.doc.moveDrawingOver(f.track, two, 3);
    CHECK_EQ(f.doc.undoDepth(), before);
    CHECK_EQ(f.tl().firstSlotOf(two), std::size_t{3});
    CHECK_EQ(f.tl().exposureOf(two), std::size_t{3});
}

// The property that falls out of never taking a drawing's last frame, and the
// reason nothing here has to tidy a retired drawing away: overwriting spends
// frames and never a whole drawing. Worth pinning rather than deducing, because
// it is the thing a later change to the rule would quietly break.
void overwritingNeverLosesADrawing() {
    TEST("no drawing is lost to overwriting, however it is spent");
    Fixture f;
    const ImageId one = f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 3);
    const ImageId two = f.doc.insertImage(f.track, 4);
    f.doc.extendExposure(f.track, 4, 3);  // two held 4, after one held 4
    paintDot(f.doc, f.track, two, f.layer, 50.0f, 50.0f);
    TrackProperties props = f.tl().properties();
    props.overwrite_drawings = true;
    f.doc.updateTrack(f.track, props);

    const ImageId three = f.doc.addDrawing(f.track, 1);
    // Every way of spending frames, over a drawing held one frame and a drawing
    // held several, from the front of a hold and from the middle of one.
    f.doc.moveDrawingOver(f.track, three, 4);
    f.doc.moveDrawingOver(f.track, two, 0);
    f.doc.addDrawing(f.track, 2);
    f.doc.duplicateDrawing(f.track, 5);
    f.doc.moveDrawingOver(f.track, one, 7);

    for (const ImageId id : {one, two, three}) {
        CHECK(f.tl().findImage(id) != nullptr);
        CHECK(f.tl().exposureOf(id) >= std::size_t{1});
    }
    // And the shot is the length it started at, throughout.
    CHECK_EQ(f.tl().frameCount(), std::size_t{8});

    const Cel* kept = f.doc.celAt(f.track, two, f.layer);
    CHECK(kept != nullptr);
    CHECK_NEAR(kept->pixel(50, 50).a, 1.0, 1e-2);
}

void trackPropertiesAreOneUndoStep() {
    TEST("changing a track's settings is one undo step");
    Fixture f;
    f.doc.insertImage(f.track, 0);

    TrackProperties props = f.tl().properties();
    props.overwrite_drawings = false;
    props.name = "background";
    f.doc.updateTrack(f.track, props);
    CHECK(!f.tl().overwrite_drawings);
    CHECK_EQ(f.tl().name, std::string("background"));

    CHECK(f.doc.undo());
    CHECK(f.tl().overwrite_drawings);
    CHECK_EQ(f.tl().name, std::string("main"));
}

// Restacking, which is what dragging a row's name in the timeline does.
//
// The three things worth pinning are that it is the drawings that stay put --
// a track's time is its own and has nothing to do with what it is stacked
// against -- that undo puts the order back, and that the inverse is exact for a
// move in either direction. The last is the one that would break silently: `to`
// is counted with the track already taken out, so moveTrack(to, from) is the
// undo only if both ends agree about that, and a move up and a move down
// disagree by one if they do not.
void restackingATrackMovesNoDrawing() {
    TEST("restacking a track reorders the stack and moves no drawing");
    Fixture f;
    f.doc.insertImage(f.track, 0);
    const TrackId second = f.doc.addTrack("second");
    const TrackId third = f.doc.addTrack("third");
    f.doc.insertImage(third, 0);
    f.doc.extendExposure(third, 0, 3);

    const auto order = [&] {
        std::vector<TrackId> ids;
        for (const Track& track : f.doc.scene().tracks) ids.push_back(track.id);
        return ids;
    };
    CHECK((order() == std::vector<TrackId>{f.track, second, third}));

    // The bottom one to the top, which is what putting a background behind a
    // character is.
    f.doc.moveTrack(2, 0);
    CHECK((order() == std::vector<TrackId>{third, f.track, second}));

    // Nothing about the track itself moved with it.
    const Track* moved = f.doc.scene().findTrack(third);
    CHECK_EQ(moved->frameCount(), std::size_t{4});
    CHECK_EQ(moved->images.size(), std::size_t{1});

    CHECK(f.doc.undo());
    CHECK((order() == std::vector<TrackId>{f.track, second, third}));
    CHECK(f.doc.redo());
    CHECK((order() == std::vector<TrackId>{third, f.track, second}));

    // And the other way round, which is the direction that catches an inverse
    // off by one.
    f.doc.undo();
    f.doc.moveTrack(0, 2);
    CHECK((order() == std::vector<TrackId>{second, third, f.track}));
    CHECK(f.doc.undo());
    CHECK((order() == std::vector<TrackId>{f.track, second, third}));

    // A move to where it already is, and one off the end, are both nothing at
    // all -- not an undo step that does nothing, which is worse: it eats a
    // Ctrl+Z that was meant for the stroke before it.
    const std::size_t depth = f.doc.undoDepth();
    f.doc.moveTrack(1, 1);
    f.doc.moveTrack(0, 9);
    CHECK_EQ(f.doc.undoDepth(), depth);
    CHECK((order() == std::vector<TrackId>{f.track, second, third}));
}

// --- what a track shows past its last drawing ------------------------------
//
// Issue #20. Tracks share one timeline and are not obliged to be the same
// length, so this is an ordinary question rather than an edge case.
void whatATrackShowsPastItsEnd() {
    TEST("a track holds, cycles or shows nothing past its last drawing");
    Fixture f;
    const ImageId a = f.doc.insertImage(f.track, 0);
    const ImageId b = f.doc.insertImage(f.track, 1);
    const ImageId c = f.doc.insertImage(f.track, 2);
    CHECK_EQ(f.tl().frameCount(), std::size_t{3});

    // Nothing, which is the default and what it did before there was a choice.
    CHECK_EQ(f.tl().imageShownAt(2), c);
    CHECK_EQ(f.tl().imageShownAt(3), kNoId);
    CHECK_EQ(f.tl().imageShownAt(99), kNoId);

    TrackProperties props = f.tl().properties();
    props.end = TrackEnd::HoldLast;
    f.doc.updateTrack(f.track, props);
    CHECK_EQ(f.tl().imageShownAt(3), c);
    CHECK_EQ(f.tl().imageShownAt(99), c);

    props.end = TrackEnd::Cycle;
    f.doc.updateTrack(f.track, props);
    CHECK_EQ(f.tl().imageShownAt(3), a);
    CHECK_EQ(f.tl().imageShownAt(4), b);
    CHECK_EQ(f.tl().imageShownAt(5), c);
    CHECK_EQ(f.tl().imageShownAt(6), a);

    // What it *holds* is untouched by any of it. The two differ only past the
    // end, and that separation is what lets a layer's own export stop where the
    // track does while the flattened picture goes on.
    CHECK_EQ(f.tl().imageAtSlot(2), c);
    CHECK_EQ(f.tl().imageAtSlot(3), kNoId);
    CHECK_EQ(f.tl().frameCount(), std::size_t{3});

    // And it never makes the shot longer: the scene is as long as the longest
    // track, so a lone cycling track cycles over nothing at all.
    CHECK_EQ(f.doc.scene().timelineFrames(), std::size_t{3});
}

// A hold repeats a drawing inside the track; the end behaviour repeats it past
// the end. Both are the same ImageId showing again, which is the model's
// central bet -- so an empty track has nothing to repeat either way.
void anEmptyTrackShowsNothingWhateverItsEnd() {
    TEST("an empty track shows nothing however its end is set");
    Fixture f;
    TrackProperties props = f.tl().properties();
    props.end = TrackEnd::Cycle;
    f.doc.updateTrack(f.track, props);

    CHECK_EQ(f.tl().frameCount(), std::size_t{0});
    CHECK_EQ(f.tl().imageShownAt(0), kNoId);
    CHECK_EQ(f.tl().imageShownAt(7), kNoId);

    props.end = TrackEnd::HoldLast;
    f.doc.updateTrack(f.track, props);
    CHECK_EQ(f.tl().imageShownAt(3), kNoId);
}

// A shot's length said outright, which is what makes a cycle worth having: a
// four-drawing walk cycles over sixty frames because the scene says sixty, and
// with nothing to say it the walk is the longest track and cycles over nothing.
void theSceneCanBeToldHowLongTheShotIs() {
    TEST("the shot is as long as the scene says, or as its longest track");
    Fixture f;
    f.doc.insertImage(f.track, 0);
    f.doc.extendExposure(f.track, 0, 3);  // four frames of walk
    CHECK(!f.doc.scene().fixed_length);    // off by default
    CHECK_EQ(f.doc.scene().shotFrames(), std::size_t{4});

    TrackProperties props = f.tl().properties();
    props.end = TrackEnd::Cycle;
    f.doc.updateTrack(f.track, props);
    // Cycling over nothing, because with nothing saying otherwise this track is
    // the whole shot.
    CHECK_EQ(f.doc.scene().shotFrames(), std::size_t{4});

    // Told it is sixty, the walk cycles over sixty.
    f.doc.setSceneLength(true, 60);
    CHECK_EQ(f.doc.scene().shotFrames(), std::size_t{60});
    CHECK_EQ(f.doc.scene().timelineFrames(), std::size_t{60});
    CHECK_EQ(f.tl().imageShownAt(59), f.tl().imageAtSlot(3));  // 59 % 4 is 3
    CHECK_EQ(f.tl().frameCount(), std::size_t{4});             // the track is untouched

    // A cap, not a wall. Set shorter than the track and the shot really is
    // shorter -- the drawings out past it are not destroyed and not hidden, they
    // are simply not in the shot, and the timeline still reaches them.
    f.doc.setSceneLength(true, 2);
    CHECK_EQ(f.doc.scene().shotFrames(), std::size_t{2});
    CHECK_EQ(f.doc.scene().timelineFrames(), std::size_t{4});
    CHECK_EQ(f.tl().frameCount(), std::size_t{4});
    CHECK(f.tl().imageAtSlot(3) != kNoId);

    // Switched off, it goes back to whatever the tracks make it, and the number
    // it was set to is kept rather than thrown away.
    f.doc.setSceneLength(false, 2);
    CHECK_EQ(f.doc.scene().shotFrames(), std::size_t{4});
    CHECK_EQ(f.doc.scene().length, 2);

    // And nothing a track does may move it: the scene sits above the tracks.
    f.doc.setSceneLength(true, 60);
    f.doc.addDrawing(f.track, 0);
    f.doc.extendExposure(f.track, 0, 20);
    CHECK_EQ(f.doc.scene().shotFrames(), std::size_t{60});
    CHECK(f.doc.scene().fixed_length);

    // One undo step like any other scene setting, and it puts back both halves.
    const std::size_t depth = f.doc.undoDepth();
    f.doc.setSceneLength(false, 12);
    CHECK(f.doc.undo());
    CHECK(f.doc.scene().fixed_length);
    CHECK_EQ(f.doc.scene().length, 60);
    CHECK_EQ(f.doc.undoDepth(), depth);
}

void layerNamesStayUnique() {
    TEST("layer names cannot collide");
    Fixture f;  // starts with "layer 1"
    const LayerId second = f.doc.addLayer(f.track, "layer 1");
    CHECK(second != kNoId);
    CHECK(f.tl().findLayer(second)->name != "layer 1");

    std::vector<std::string> names;
    for (const Layer& layer : f.tl().layers) names.push_back(layer.name);
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
}

}  // namespace

int main() {
    std::printf("track:\n");
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
    overwritingSpendsTheHoldRatherThanLengtheningTheTrack();
    withoutOverwritingAddingADrawingStillLengthensTheTrack();
    overwritingNeverTakesADrawingsLastFrame();
    duplicatingOverwritesTheSameWay();
    aDuplicatedTrackHasItsOwnDrawingsAndItsOwnPixels();
    aDuplicatedColourLayerIsCutAgainstItsOwnTracksLineArt();
    aDuplicatedTrackKeepsItsImportedPictures();
    duplicatingATrackUndoesInOneStep();
    aDuplicatedSoundtrackPointsAtTheSameFile();
    duplicatingADrawingTakesTheImportedPictureWithIt();
    duplicatingAnImportOnItsOwnTrackIsNotABlankDrawing();
    movingOverAHoldTakesTheRestOfItAndLeavesNoGap();
    nudgingADrawingAlongItsOwnHold();
    framesLeftAtTheStartGoToTheDrawingAfterThem();
    movingOverToWhereItAlreadyIsDoesNothing();
    overwritingNeverLosesADrawing();
    trackPropertiesAreOneUndoStep();
    restackingATrackMovesNoDrawing();
    anewDrawingTakesTheLowestFreeNumber();
    undoingADeletionPutsItsNumberBackInUse();
    whatATrackShowsPastItsEnd();
    anEmptyTrackShowsNothingWhateverItsEnd();
    theSceneCanBeToldHowLongTheShotIs();
    return testing::summarise("track");
}
