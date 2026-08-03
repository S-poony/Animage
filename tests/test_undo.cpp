// SPDX-License-Identifier: GPL-3.0-or-later

#include "document.h"
#include "testing.h"

using namespace animage;

namespace {

void paint(Document& doc, TimelineId timeline, ImageId image, LayerId layer, int px, int py,
           const Rgba& colour) {
    Cel* cel = doc.celForWriting(timeline, image, layer);
    if (!cel) return;
    Tile* tile = cel->writableTile(tileCoordFor(px, py), doc.journal());
    tile->setPixel(tileLocal(px), tileLocal(py), colour);
}

Rgba read(const Document& doc, TimelineId timeline, ImageId image, LayerId layer, int px,
          int py) {
    const Cel* cel = doc.celAt(timeline, image, layer);
    return cel ? cel->pixel(px, py) : Rgba{};
}

const Rgba kRed{1.0f, 0.0f, 0.0f, 1.0f};
const Rgba kGreen{0.0f, 1.0f, 0.0f, 1.0f};

struct Fixture {
    Document doc;
    TimelineId timeline;
    LayerId layer;

    Fixture() {
        timeline = doc.addTimeline("main");
        layer = doc.addLayer(timeline, "rough");
    }
    const Timeline& tl() const { return *doc.scene().findTimeline(timeline); }
};

void strokeUndoRedo() {
    TEST("a stroke undoes and redoes");
    Fixture f;
    const ImageId image = f.doc.insertImage(f.timeline, 0);

    {
        ScopedCommand command(f.doc, "Stroke");
        paint(f.doc, f.timeline, image, f.layer, 20, 20, kRed);
    }
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 20, 20).r, 1.0, 1e-3);

    CHECK(f.doc.undo());
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 20, 20).a, 0.0, 1e-3);

    CHECK(f.doc.redo());
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 20, 20).r, 1.0, 1e-3);
}

// Two dabs on the same tile inside one command must restore the state from
// before the command, not from before the second dab.
void oneCommandOneSnapshotPerTile() {
    TEST("repeated dabs on one tile snapshot it once");
    Fixture f;
    const ImageId image = f.doc.insertImage(f.timeline, 0);

    {
        ScopedCommand command(f.doc, "Stroke");
        paint(f.doc, f.timeline, image, f.layer, 1, 1, kRed);
        paint(f.doc, f.timeline, image, f.layer, 2, 2, kRed);
        paint(f.doc, f.timeline, image, f.layer, 3, 3, kRed);
    }

    CHECK(f.doc.undo());
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 1, 1).a, 0.0, 1e-3);
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 2, 2).a, 0.0, 1e-3);
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 3, 3).a, 0.0, 1e-3);
}

// The awkward one from the plan: draw on an image, delete the image, then undo
// back past the deletion to the stroke. It only works because CelIds are never
// reused and the history counts as a reference on the cel.
void undoAcrossImageDeletion() {
    TEST("undoing a stroke after the image was deleted restores the content");
    Fixture f;

    const ImageId image = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 1);  // slots 0 and 1 are the same image

    {
        ScopedCommand command(f.doc, "Stroke");
        paint(f.doc, f.timeline, image, f.layer, 7, 7, kRed);
    }
    const CelId cel_id = f.tl().findImage(image)->celFor(f.layer);
    CHECK(cel_id != kNoId);

    // Delete every slot showing it. The image record goes, and the cel's image
    // refcount falls to zero.
    f.doc.removeSlot(f.timeline, 1);
    f.doc.removeSlot(f.timeline, 0);
    CHECK(f.tl().findImage(image) == nullptr);
    CHECK_EQ(f.doc.cel(cel_id)->imageRefcount(), 0);

    // The cel must survive: undo can still bring the image back.
    CHECK_EQ(f.doc.collectGarbage(), std::size_t{0});
    CHECK(f.doc.cel(cel_id) != nullptr);

    CHECK(f.doc.undo());  // undo second removal
    CHECK(f.doc.undo());  // undo first removal
    CHECK(f.tl().findImage(image) != nullptr);
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 7, 7).r, 1.0, 1e-3);

    CHECK(f.doc.undo());  // undo the stroke itself
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 7, 7).a, 0.0, 1e-3);

    CHECK(f.doc.redo());
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 7, 7).r, 1.0, 1e-3);
}

// A cel id is never handed out twice, even after the cel is collected.
void celIdsAreNeverReused() {
    TEST("cel ids are never reused");
    Fixture f;

    const ImageId first = f.doc.insertImage(f.timeline, 0);
    {
        ScopedCommand command(f.doc, "Stroke");
        paint(f.doc, f.timeline, first, f.layer, 1, 1, kRed);
    }
    const CelId original = f.tl().findImage(first)->celFor(f.layer);

    f.doc.removeSlot(f.timeline, 0);
    // Drop the history so the cel is genuinely unreachable and collectable.
    Document& doc = f.doc;
    while (doc.undo()) {
    }
    while (doc.redo()) {
    }
    doc.removeSlot(f.timeline, 0);

    const ImageId second = doc.insertImage(f.timeline, 0);
    {
        ScopedCommand command(doc, "Stroke");
        paint(doc, f.timeline, second, f.layer, 1, 1, kGreen);
    }
    const CelId fresh = f.tl().findImage(second)->celFor(f.layer);
    CHECK(fresh != original);
    CHECK(fresh > original);
}

void addLayerUndoes() {
    TEST("adding and removing a layer undoes");
    Fixture f;
    const ImageId image = f.doc.insertImage(f.timeline, 0);

    const LayerId clean = f.doc.addLayer(f.timeline, "clean", 0);
    CHECK_EQ(f.tl().layers.size(), std::size_t{2});

    {
        ScopedCommand command(f.doc, "Stroke");
        paint(f.doc, f.timeline, image, clean, 4, 4, kGreen);
    }
    CHECK_NEAR(read(f.doc, f.timeline, image, clean, 4, 4).g, 1.0, 1e-3);

    f.doc.removeLayer(f.timeline, clean);
    CHECK_EQ(f.tl().layers.size(), std::size_t{1});
    CHECK(f.tl().findImage(image)->cels.empty());

    CHECK(f.doc.undo());
    CHECK_EQ(f.tl().layers.size(), std::size_t{2});
    CHECK_NEAR(read(f.doc, f.timeline, image, clean, 4, 4).g, 1.0, 1e-3);

    CHECK(f.doc.undo());  // the stroke
    CHECK_NEAR(read(f.doc, f.timeline, image, clean, 4, 4).a, 0.0, 1e-3);

    CHECK(f.doc.undo());  // adding the layer
    CHECK_EQ(f.tl().layers.size(), std::size_t{1});
}

// Editing one frame of an exposed image and undoing must restore every frame at
// once, because they share the cel.
void undoOnExposedImageRestoresAllFrames() {
    TEST("undo on an exposed image restores every frame together");
    Fixture f;

    const ImageId image = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 2);
    {
        ScopedCommand command(f.doc, "Stroke");
        paint(f.doc, f.timeline, image, f.layer, 9, 9, kRed);
    }
    CHECK(f.doc.undo());

    for (std::size_t slot = 0; slot < 3; ++slot) {
        CHECK_NEAR(read(f.doc, f.timeline, f.tl().imageAtSlot(slot), f.layer, 9, 9).a, 0.0, 1e-3);
    }
}

// A new command after an undo discards the redo branch.
void newCommandClearsRedo() {
    TEST("a new command clears the redo stack");
    Fixture f;
    const ImageId image = f.doc.insertImage(f.timeline, 0);

    {
        ScopedCommand command(f.doc, "Stroke");
        paint(f.doc, f.timeline, image, f.layer, 1, 1, kRed);
    }
    CHECK(f.doc.undo());
    CHECK(f.doc.canRedo());

    {
        ScopedCommand command(f.doc, "Other stroke");
        paint(f.doc, f.timeline, image, f.layer, 50, 50, kGreen);
    }
    CHECK(!f.doc.canRedo());
}

void nestedCommandsCollapse() {
    TEST("nested commands collapse into one undo step");
    Fixture f;
    const ImageId image = f.doc.insertImage(f.timeline, 0);
    const std::size_t before = f.doc.undoDepth();

    {
        ScopedCommand outer(f.doc, "Compound");
        f.doc.addLayer(f.timeline, "a");
        f.doc.addLayer(f.timeline, "b");
        paint(f.doc, f.timeline, image, f.layer, 2, 2, kRed);
    }

    CHECK_EQ(f.doc.undoDepth(), before + 1);
    CHECK_EQ(f.tl().layers.size(), std::size_t{3});
    CHECK(f.doc.undo());
    CHECK_EQ(f.tl().layers.size(), std::size_t{1});
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 2, 2).a, 0.0, 1e-3);
}

}  // namespace

int main() {
    std::printf("undo:\n");
    strokeUndoRedo();
    oneCommandOneSnapshotPerTile();
    undoAcrossImageDeletion();
    celIdsAreNeverReused();
    addLayerUndoes();
    undoOnExposedImageRestoresAllFrames();
    newCommandClearsRedo();
    nestedCommandsCollapse();
    return testing::summarise("undo");
}
