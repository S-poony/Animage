// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tests the prototype plan lists for M1. If these hold, the data model is
// right and the rest is mechanism.

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

const Rgba kRed{1.0f, 0.0f, 0.0f, 1.0f};
const Rgba kBlue{0.0f, 0.0f, 1.0f, 1.0f};

// 1. Drawing on an image that is exposed three times changes all three
//    positions, because they are the same ImageId and therefore the same cel.
void exposureSharesOneCel() {
    TEST("drawing on a 3x-exposed image affects all three frames");
    Fixture f;

    const ImageId image = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 2);
    CHECK_EQ(f.tl().frameCount(), std::size_t{3});
    CHECK_EQ(f.tl().exposureOf(image), std::size_t{3});
    CHECK_EQ(f.tl().images.size(), std::size_t{1});

    {
        ScopedCommand command(f.doc, "Stroke");
        paint(f.doc, f.timeline, image, f.layer, 10, 10, kRed);
    }

    for (std::size_t slot = 0; slot < 3; ++slot) {
        const Rgba pixel = read(f.doc, f.timeline, f.tl().imageAtSlot(slot), f.layer, 10, 10);
        CHECK_NEAR(pixel.r, 1.0, 1e-3);
        CHECK_NEAR(pixel.a, 1.0, 1e-3);
    }

    // One drawing, one cel, one tile -- not three copies.
    CHECK_EQ(f.doc.celCount(), std::size_t{1});
    CHECK_EQ(f.doc.totalTileCount(), std::size_t{1});
}

// 2. Copy and paste produces independent cels, even though the tiles are
//    shared until one of them is written to.
void duplicateProducesIndependentCels() {
    TEST("duplicating an image gives independent cels");
    Fixture f;

    const ImageId source = f.doc.insertImage(f.timeline, 0);
    {
        ScopedCommand command(f.doc, "Stroke");
        paint(f.doc, f.timeline, source, f.layer, 5, 5, kRed);
    }

    const ImageId copy = f.doc.duplicateImage(f.timeline, 0);
    CHECK(copy != kNoId);
    CHECK(copy != source);

    const CelId source_cel = f.tl().findImage(source)->celFor(f.layer);
    const CelId copy_cel = f.tl().findImage(copy)->celFor(f.layer);
    CHECK(source_cel != copy_cel);

    // The copy starts out identical...
    CHECK_NEAR(read(f.doc, f.timeline, copy, f.layer, 5, 5).r, 1.0, 1e-3);
    // ...and sharing the tile, so duplication is nearly free.
    CHECK_EQ(f.doc.totalTileCount(), std::size_t{2});

    {
        ScopedCommand command(f.doc, "Stroke on copy");
        paint(f.doc, f.timeline, copy, f.layer, 5, 5, kBlue);
    }

    CHECK_NEAR(read(f.doc, f.timeline, source, f.layer, 5, 5).r, 1.0, 1e-3);
    CHECK_NEAR(read(f.doc, f.timeline, source, f.layer, 5, 5).b, 0.0, 1e-3);
    CHECK_NEAR(read(f.doc, f.timeline, copy, f.layer, 5, 5).b, 1.0, 1e-3);
    CHECK_NEAR(read(f.doc, f.timeline, copy, f.layer, 5, 5).r, 0.0, 1e-3);
}

// 3. Adding an interval allocates no tile. Neither does extending exposure.
void addingIntervalsAllocatesNothing() {
    TEST("adding intervals allocates no tiles");
    Fixture f;

    for (int i = 0; i < 24; ++i) f.doc.insertImage(f.timeline, static_cast<std::size_t>(i));
    f.doc.extendExposure(f.timeline, 0, 5);

    CHECK_EQ(f.tl().frameCount(), std::size_t{29});
    CHECK_EQ(f.tl().images.size(), std::size_t{24});
    CHECK_EQ(f.doc.celCount(), std::size_t{0});
    CHECK_EQ(f.doc.totalTileCount(), std::size_t{0});
}

// 5. Adding a layer to a 500-image timeline is O(1): it must not touch a single
//    Image record. That structural fact is the property, and it is worth
//    asserting directly rather than timing it.
void addingLayerTouchesNoImage() {
    TEST("adding a layer to a 500-image timeline touches no image");
    Fixture f;

    for (int i = 0; i < 500; ++i) f.doc.insertImage(f.timeline, static_cast<std::size_t>(i));
    CHECK_EQ(f.tl().images.size(), std::size_t{500});

    const LayerId clean = f.doc.addLayer(f.timeline, "clean", 0);
    CHECK(clean != kNoId);
    CHECK_EQ(f.tl().layers.size(), std::size_t{2});
    CHECK_EQ(f.tl().layers[0].id, clean);  // index 0 is the top of the stack

    std::size_t touched = 0;
    for (const auto& [id, image] : f.tl().images) {
        if (!image.cels.empty()) ++touched;
    }
    CHECK_EQ(touched, std::size_t{0});
    CHECK_EQ(f.doc.celCount(), std::size_t{0});
}

// Onion skin walks distinct ImageIds, so a held drawing counts once.
void onionSkinCountsDistinctImages() {
    TEST("onion skin counts distinct images, not slots");
    Fixture f;

    const ImageId a = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 4);  // a held for 5 frames
    const ImageId b = f.doc.insertImage(f.timeline, 5);
    const ImageId c = f.doc.insertImage(f.timeline, 6);
    CHECK_EQ(f.tl().frameCount(), std::size_t{7});

    const std::vector<ImageId> back = f.tl().distinctNeighbours(6, 2, -1);
    CHECK_EQ(back.size(), std::size_t{2});
    CHECK_EQ(back[0], b);
    CHECK_EQ(back[1], a);

    const std::vector<ImageId> forward = f.tl().distinctNeighbours(0, 2, +1);
    CHECK_EQ(forward.size(), std::size_t{2});
    CHECK_EQ(forward[0], b);
    CHECK_EQ(forward[1], c);
}

// Removing one slot of an exposed image does not remove the drawing.
void removingOneSlotKeepsTheImage() {
    TEST("removing one slot of an exposed image keeps the drawing");
    Fixture f;

    const ImageId image = f.doc.insertImage(f.timeline, 0);
    f.doc.extendExposure(f.timeline, 0, 2);
    {
        ScopedCommand command(f.doc, "Stroke");
        paint(f.doc, f.timeline, image, f.layer, 3, 3, kRed);
    }

    f.doc.removeSlot(f.timeline, 1);
    CHECK_EQ(f.tl().frameCount(), std::size_t{2});
    CHECK(f.tl().findImage(image) != nullptr);
    CHECK_NEAR(read(f.doc, f.timeline, image, f.layer, 3, 3).r, 1.0, 1e-3);

    f.doc.removeSlot(f.timeline, 0);
    f.doc.removeSlot(f.timeline, 0);
    CHECK_EQ(f.tl().frameCount(), std::size_t{0});
    CHECK(f.tl().findImage(image) == nullptr);
}

// Tile coordinates must floor, not truncate, or the tile left of the origin
// collides with the one right of it.
void tileCoordinatesFloor() {
    TEST("tile coordinates floor towards negative infinity");
    CHECK_EQ(tileCoordFor(0, 0).x, 0);
    CHECK_EQ(tileCoordFor(127, 127).x, 0);
    CHECK_EQ(tileCoordFor(128, 0).x, 1);
    CHECK_EQ(tileCoordFor(-1, 0).x, -1);
    CHECK_EQ(tileCoordFor(-128, 0).x, -1);
    CHECK_EQ(tileCoordFor(-129, 0).x, -2);

    CHECK_EQ(tileLocal(-1), 127);
    CHECK_EQ(tileLocal(-128), 0);
    CHECK_EQ(tileLocal(129), 1);
}

}  // namespace

int main() {
    std::printf("model:\n");
    exposureSharesOneCel();
    duplicateProducesIndependentCels();
    addingIntervalsAllocatesNothing();
    addingLayerTouchesNoImage();
    onionSkinCountsDistinctImages();
    removingOneSlotKeepsTheImage();
    tileCoordinatesFloor();
    return testing::summarise("model");
}
