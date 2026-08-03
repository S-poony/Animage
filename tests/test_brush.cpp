// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>

#include "brush.h"
#include "compositor.h"
#include "testing.h"

using namespace animage;

namespace {

struct Fixture {
    Document doc;
    TimelineId timeline;
    LayerId layer;
    ImageId image;

    Fixture() {
        timeline = doc.addTimeline("main");
        layer = doc.addLayer(timeline, "rough");
        image = doc.insertImage(timeline, 0);
    }
    const Timeline& tl() const { return *doc.scene().findTimeline(timeline); }
};

BrushSettings opaqueBlack() {
    BrushSettings settings;
    settings.radius = 10.0f;
    settings.hardness = 0.9f;
    settings.pressure_affects_opacity = false;
    settings.r = settings.g = settings.b = 0.0f;
    settings.a = 1.0f;
    return settings;
}

float alphaAt(const Document& doc, TimelineId timeline, ImageId image, LayerId layer, int x,
              int y) {
    const Cel* cel = doc.celAt(timeline, image, layer);
    return cel ? cel->pixel(x, y).a : 0.0f;
}

void strokeLaysDownInk() {
    TEST("a stroke lays down ink along its path");
    Fixture f;
    Brush brush(opaqueBlack());

    {
        ScopedCommand command(f.doc, "Stroke");
        brush.begin(f.doc, f.timeline, f.image, f.layer, {100.0f, 100.0f, 1.0f});
        for (int i = 1; i <= 40; ++i) {
            brush.extend({100.0f + static_cast<float>(i) * 2.0f, 100.0f, 1.0f});
        }
        brush.end();
    }

    CHECK(brush.dabCount() > 1);
    CHECK_NEAR(alphaAt(f.doc, f.timeline, f.image, f.layer, 100, 100), 1.0, 1e-2);
    CHECK_NEAR(alphaAt(f.doc, f.timeline, f.image, f.layer, 140, 100), 1.0, 1e-2);
    CHECK_NEAR(alphaAt(f.doc, f.timeline, f.image, f.layer, 180, 100), 1.0, 1e-2);

    // Well clear of the stroke, nothing.
    CHECK_NEAR(alphaAt(f.doc, f.timeline, f.image, f.layer, 100, 400), 0.0, 1e-3);
}

// A fast stroke and a slow one over the same path must lay down the same ink.
// Spacing has to carry its remainder across events, or the dab rate follows the
// event rate instead of the distance.
void spacingIsIndependentOfEventRate() {
    TEST("dab spacing follows distance, not event rate");

    auto strokeWith = [](int steps) {
        Fixture f;
        Brush brush(opaqueBlack());
        ScopedCommand command(f.doc, "Stroke");
        brush.begin(f.doc, f.timeline, f.image, f.layer, {50.0f, 50.0f, 1.0f});
        const float total = 200.0f;
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            brush.extend({50.0f + total * t, 50.0f, 1.0f});
        }
        brush.end();
        return brush.dabCount();
    };

    const int few = strokeWith(4);     // fast pen, sparse events
    const int many = strokeWith(100);  // slow pen, dense events
    CHECK(few > 0);
    CHECK(std::abs(few - many) <= 2);
}

void pressureChangesWidth() {
    TEST("pressure changes the width of the stroke");

    auto widthAtPressure = [](float pressure) {
        Fixture f;
        BrushSettings settings = opaqueBlack();
        settings.min_radius_ratio = 0.1f;
        Brush brush(settings);
        {
            ScopedCommand command(f.doc, "Stroke");
            brush.begin(f.doc, f.timeline, f.image, f.layer, {200.0f, 200.0f, pressure});
            brush.extend({260.0f, 200.0f, pressure});
            brush.end();
        }
        int width = 0;
        for (int y = 150; y < 250; ++y) {
            if (alphaAt(f.doc, f.timeline, f.image, f.layer, 230, y) > 0.5f) ++width;
        }
        return width;
    };

    const int light = widthAtPressure(0.2f);
    const int heavy = widthAtPressure(1.0f);
    CHECK(light > 0);
    CHECK(heavy > light * 2);
}

void eraserRemovesInk() {
    TEST("the eraser removes ink");
    Fixture f;

    {
        ScopedCommand command(f.doc, "Stroke");
        Brush brush(opaqueBlack());
        brush.begin(f.doc, f.timeline, f.image, f.layer, {300.0f, 300.0f, 1.0f});
        brush.extend({360.0f, 300.0f, 1.0f});
        brush.end();
    }
    CHECK_NEAR(alphaAt(f.doc, f.timeline, f.image, f.layer, 330, 300), 1.0, 1e-2);

    {
        ScopedCommand command(f.doc, "Erase");
        BrushSettings settings = opaqueBlack();
        settings.erase = true;
        settings.radius = 14.0f;
        Brush eraser(settings);
        eraser.begin(f.doc, f.timeline, f.image, f.layer, {300.0f, 300.0f, 1.0f});
        eraser.extend({360.0f, 300.0f, 1.0f});
        eraser.end();
    }
    CHECK_NEAR(alphaAt(f.doc, f.timeline, f.image, f.layer, 330, 300), 0.0, 1e-2);

    // And erasing is undoable like anything else.
    CHECK(f.doc.undo());
    CHECK_NEAR(alphaAt(f.doc, f.timeline, f.image, f.layer, 330, 300), 1.0, 1e-2);
}

void strokeIsOneUndoStep() {
    TEST("a whole stroke is a single undo step");
    Fixture f;
    const std::size_t before = f.doc.undoDepth();

    {
        ScopedCommand command(f.doc, "Stroke");
        Brush brush(opaqueBlack());
        brush.begin(f.doc, f.timeline, f.image, f.layer, {10.0f, 10.0f, 1.0f});
        for (int i = 1; i <= 200; ++i) {
            brush.extend({10.0f + static_cast<float>(i), 10.0f + static_cast<float>(i), 1.0f});
        }
        brush.end();
    }

    CHECK_EQ(f.doc.undoDepth(), before + 1);
    CHECK(f.doc.totalTileCount() > 1);  // the stroke crossed several tiles

    CHECK(f.doc.undo());
    CHECK_NEAR(alphaAt(f.doc, f.timeline, f.image, f.layer, 100, 100), 0.0, 1e-3);
    CHECK(f.doc.redo());
    CHECK_NEAR(alphaAt(f.doc, f.timeline, f.image, f.layer, 100, 100), 1.0, 1e-2);
}

// A dab must not allocate tiles it does not reach. This is the property that
// keeps a mostly-empty layer cheap.
void strokeAllocatesOnlyTilesItTouches() {
    TEST("a dab allocates only the tiles it touches");
    Fixture f;
    BrushSettings settings = opaqueBlack();
    settings.radius = 4.0f;
    Brush brush(settings);

    {
        ScopedCommand command(f.doc, "Dot");
        // Well inside one tile: 64,64 with radius 4 cannot reach a neighbour.
        brush.begin(f.doc, f.timeline, f.image, f.layer, {64.0f, 64.0f, 1.0f});
        brush.end();
    }
    CHECK_EQ(f.doc.totalTileCount(), std::size_t{1});
}

void compositorRespectsOrderAndOpacity() {
    TEST("the compositor respects layer order, opacity and visibility");
    Fixture f;
    const LayerId top = f.doc.addLayer(f.timeline, "top", 0);

    // Bottom layer red, top layer green, both opaque over the same spot.
    {
        ScopedCommand command(f.doc, "Red");
        BrushSettings settings = opaqueBlack();
        settings.r = 1.0f;
        Brush brush(settings);
        brush.begin(f.doc, f.timeline, f.image, f.layer, {500.0f, 500.0f, 1.0f});
        brush.end();
    }
    {
        ScopedCommand command(f.doc, "Green");
        BrushSettings settings = opaqueBlack();
        settings.g = 1.0f;
        Brush brush(settings);
        brush.begin(f.doc, f.timeline, f.image, top, {500.0f, 500.0f, 1.0f});
        brush.end();
    }

    Compositor compositor;
    Framebuffer frame;
    const PixelRect region{495, 495, 10, 10};

    compositor.composite(f.doc, f.timeline, f.image, region, frame);
    Rgba centre = frame.pixel(5, 5);
    CHECK_NEAR(centre.a, 1.0, 1e-2);
    CHECK_NEAR(centre.g, 1.0, 1e-2);  // green is on top
    CHECK_NEAR(centre.r, 0.0, 1e-2);

    // Hide the top layer and the red beneath shows through.
    Layer hidden = *f.tl().findLayer(top);
    hidden.visible = false;
    f.doc.updateLayer(f.timeline, top, hidden);

    compositor.composite(f.doc, f.timeline, f.image, region, frame);
    centre = frame.pixel(5, 5);
    CHECK_NEAR(centre.r, 1.0, 1e-2);
    CHECK_NEAR(centre.g, 0.0, 1e-2);

    // Half opacity on the only visible layer halves its contribution.
    Layer faded = *f.tl().findLayer(f.layer);
    faded.opacity = 0.5f;
    f.doc.updateLayer(f.timeline, f.layer, faded);

    compositor.composite(f.doc, f.timeline, f.image, region, frame);
    centre = frame.pixel(5, 5);
    CHECK_NEAR(centre.a, 0.5, 1e-2);
    CHECK_NEAR(centre.r, 0.5, 1e-2);
}

void compositorHandlesEmptyAndBounds() {
    TEST("compositing an empty image gives transparency");
    Fixture f;
    Compositor compositor;
    Framebuffer frame;

    compositor.composite(f.doc, f.timeline, f.image, {0, 0, 16, 16}, frame);
    CHECK_EQ(frame.width(), 16);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) CHECK_NEAR(frame.pixel(x, y).a, 0.0, 1e-6);
    }

    const PixelRect empty = imageBounds(f.doc, f.timeline, f.image);
    CHECK(empty.isEmpty());

    {
        ScopedCommand command(f.doc, "Dot");
        Brush brush(opaqueBlack());
        brush.begin(f.doc, f.timeline, f.image, f.layer, {200.0f, 300.0f, 1.0f});
        brush.end();
    }
    const PixelRect bounds = imageBounds(f.doc, f.timeline, f.image);
    CHECK(!bounds.isEmpty());
    CHECK(bounds.x <= 200 && bounds.x + bounds.width > 200);
    CHECK(bounds.y <= 300 && bounds.y + bounds.height > 300);
}

// Negative coordinates are where tile indexing goes wrong quietly, so the
// compositor is checked there specifically.
void compositorWorksLeftOfTheOrigin() {
    TEST("compositing works at negative coordinates");
    Fixture f;

    {
        ScopedCommand command(f.doc, "Dot");
        BrushSettings settings = opaqueBlack();
        settings.radius = 6.0f;
        settings.r = 1.0f;
        Brush brush(settings);
        brush.begin(f.doc, f.timeline, f.image, f.layer, {-200.0f, -150.0f, 1.0f});
        brush.end();
    }

    Compositor compositor;
    Framebuffer frame;
    compositor.composite(f.doc, f.timeline, f.image, {-205, -155, 10, 10}, frame);
    CHECK_NEAR(frame.pixel(5, 5).a, 1.0, 1e-2);
    CHECK_NEAR(frame.pixel(5, 5).r, 1.0, 1e-2);
}

}  // namespace

int main() {
    std::printf("brush:\n");
    strokeLaysDownInk();
    spacingIsIndependentOfEventRate();
    pressureChangesWidth();
    eraserRemovesInk();
    strokeIsOneUndoStep();
    strokeAllocatesOnlyTilesItTouches();
    compositorRespectsOrderAndOpacity();
    compositorHandlesEmptyAndBounds();
    compositorWorksLeftOfTheOrigin();
    return testing::summarise("brush");
}
