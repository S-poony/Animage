// SPDX-License-Identifier: GPL-3.0-or-later
//
// The CTG layer: scribbles in, fill out, and the fill is never what is stored.

#include "brush.h"
#include "compositor.h"
#include "ctg.h"
#include "testing.h"

using namespace animage;

namespace {

struct Fixture {
    Document doc;
    TimelineId timeline;
    LayerId ink;
    LayerId colour;
    ImageId image;

    Fixture() {
        timeline = doc.addTimeline("main");
        colour = doc.addLayer(timeline, "colour", 0, LayerKind::Ctg);
        ink = doc.addLayer(timeline, "ink", 1);
        image = doc.insertImage(timeline, 0);

        Layer ctg = *doc.scene().findTimeline(timeline)->findLayer(colour);
        ctg.ctg_sources = {ink};
        doc.updateLayer(timeline, colour, ctg);
    }

    void stroke(LayerId layer, float x0, float y0, float x1, float y1, float radius,
                float r, float g, float b) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings settings;
        settings.radius = radius;
        settings.hardness = 0.95f;
        settings.pressure_affects_opacity = false;
        settings.r = r;
        settings.g = g;
        settings.b = b;
        settings.a = 1.0f;
        Brush brush(settings);
        brush.begin(doc, timeline, image, layer, {x0, y0, 1.0f});
        brush.extend({x1, y1, 1.0f});
        brush.end();
    }

    // A box with a hole in the bottom wall, the case a paint bucket cannot do.
    void drawGappedBox(LayerId layer, int left, int top, int right, int bottom, int gap_from,
                       int gap_to) {
        const float w = 2.5f;
        stroke(layer, left, top, right, top, w, 0, 0, 0);
        stroke(layer, left, top, left, bottom, w, 0, 0, 0);
        stroke(layer, right, top, right, bottom, w, 0, 0, 0);
        stroke(layer, left, bottom, gap_from, bottom, w, 0, 0, 0);
        stroke(layer, gap_to, bottom, right, bottom, w, 0, 0, 0);
    }
};

Rgba fillAt(const CtgFill& fill, int x, int y) { return fill.tiles.pixel(x, y); }

void aScribbleFillsItsRegion() {
    TEST("a scribble fills the region the line art encloses");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);

    // One scrawl inside, one outside. Nothing precise about either.
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);

    const CtgFill& fill = ctgFill(f.doc, f.timeline, f.image, f.colour);
    CHECK(fill.valid);
    CHECK_EQ(fill.colours, 2);

    // Inside is red, well away from where the scribble was actually drawn.
    const Rgba inside = fillAt(fill, 130, 160);
    CHECK_NEAR(inside.r, 1.0, 0.02);
    CHECK_NEAR(inside.b, 0.0, 0.02);

    // Outside is blue, including just below the gap.
    const Rgba outside = fillAt(fill, 130, 210);
    CHECK_NEAR(outside.b, 1.0, 0.02);
    CHECK_NEAR(outside.r, 0.0, 0.02);

    // And the colour did not pour through the gap.
    const Rgba above_gap = fillAt(fill, 130, 172);
    CHECK_NEAR(above_gap.r, 1.0, 0.02);
}

// The layer stores what you drew, not what it computed. That is the whole
// point: the fill can always be thrown away and rebuilt.
void theLayerStoresScribblesNotTheFill() {
    TEST("the layer stores scribbles, not the fill");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);

    const CtgFill& fill = ctgFill(f.doc, f.timeline, f.image, f.colour);
    const std::size_t filled_tiles = fill.tiles.tileCount();

    const Cel* scribbles = f.doc.celAt(f.timeline, f.image, f.colour);
    CHECK(scribbles != nullptr);

    // The cel holds only the marks, which cover far less than the fill does.
    CHECK(scribbles->tiles().tileCount() < filled_tiles);
    CHECK(filled_tiles > 0);

    // Nothing at all where the scribble was not drawn but the fill reaches.
    CHECK_NEAR(scribbles->pixel(130, 160).a, 0.0, 1e-3);
    CHECK_NEAR(fillAt(fill, 130, 160).r, 1.0, 0.02);
}

void editingAScribbleRecoloursTheWholeRegion() {
    TEST("moving a scribble recolours the whole region");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);

    CHECK_NEAR(fillAt(ctgFill(f.doc, f.timeline, f.image, f.colour), 130, 160).r, 1.0, 0.02);

    // Scribble green over the red one. The region follows.
    f.stroke(f.colour, 100, 110, 150, 110, 8.0f, 0.0f, 1.0f, 0.0f);

    const CtgFill& after = ctgFill(f.doc, f.timeline, f.image, f.colour);
    const Rgba inside = fillAt(after, 130, 160);
    CHECK_NEAR(inside.g, 1.0, 0.02);
    CHECK_NEAR(inside.r, 0.0, 0.02);
}

// Regenerating is not free, so it must not happen when nothing has moved.
void theFillIsCachedUntilSomethingChanges() {
    TEST("the fill is reused until an input changes");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);

    const std::uint64_t first = ctgFill(f.doc, f.timeline, f.image, f.colour).inputs;
    CHECK(first != 0);
    CHECK_EQ(ctgFill(f.doc, f.timeline, f.image, f.colour).inputs, first);

    // Drawing on the line art must invalidate it, not only drawing a scribble.
    f.stroke(f.ink, 130, 178, 138, 178, 2.5f, 0, 0, 0);
    const std::uint64_t after_ink = ctgFill(f.doc, f.timeline, f.image, f.colour).inputs;
    CHECK(after_ink != first);

    f.stroke(f.colour, 100, 110, 120, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    CHECK(ctgFill(f.doc, f.timeline, f.image, f.colour).inputs != after_ink);
}

// Closing the gap on a second layer should stop the leak without touching the
// first. This is the improvement over TVPaint the design notes ask for by name.
void twoBarrierLayersClosseEachOthersGaps() {
    TEST("a second barrier layer closes the first one's gaps");
    Fixture f;
    const LayerId rough = f.doc.addLayer(f.timeline, "rough", 2);

    // The clean line has a wide hole; the rough happens to cross it.
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 110, 150);

    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);

    // With only the clean line, a gap that wide lets the boundary through: the
    // pixels right at the hole are contested.
    const Rgba single = fillAt(ctgFill(f.doc, f.timeline, f.image, f.colour), 130, 176);

    // Now add the rough as a second barrier, drawn across the hole.
    f.stroke(rough, 105, 180, 155, 180, 2.5f, 0, 0, 0);
    Layer ctg = *f.doc.scene().findTimeline(f.timeline)->findLayer(f.colour);
    ctg.ctg_sources = {f.ink, rough};
    f.doc.updateLayer(f.timeline, f.colour, ctg);

    const CtgFill& both = ctgFill(f.doc, f.timeline, f.image, f.colour);
    const Rgba doubled = fillAt(both, 130, 176);

    // Whatever the single-source result was, with the gap bridged the pixel
    // just inside the wall belongs to the inside.
    CHECK_NEAR(doubled.r, 1.0, 0.02);
    CHECK_NEAR(doubled.b, 0.0, 0.02);
    (void)single;

    // And below the bridged wall is still outside.
    CHECK_NEAR(fillAt(both, 130, 210).b, 1.0, 0.02);
}

// A CTG layer with nothing scribbled on it produces nothing, rather than
// filling the world with whatever colour it finds first.
void noScribblesMeansNoFill() {
    TEST("a CTG layer with no scribbles fills nothing");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);

    // No cel at all on the colour layer yet.
    const CtgFill& nothing = ctgFill(f.doc, f.timeline, f.image, f.colour);
    CHECK(!nothing.valid);
    CHECK_EQ(nothing.tiles.tileCount(), std::size_t{0});
}

// Solving coarse while the pen moves is the plan's answer to interactivity. The
// result should be blocky, not wrong.
void aCoarseSolveAgreesWithTheFineOne() {
    TEST("a downscaled solve agrees with the full one");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);

    CtgSettings coarse;
    coarse.downscale = 4;
    const CtgFill& quick = ctgFill(f.doc, f.timeline, f.image, f.colour, coarse);
    CHECK(quick.valid);

    // Well inside and well outside must still be right; only the boundary is
    // allowed to be rough.
    CHECK_NEAR(fillAt(quick, 130, 150).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(quick, 130, 220).b, 1.0, 0.02);
}

// What the compositor puts on screen for a CTG layer is the fill, never the
// scribbles -- so the scrawl you made disappears the moment it takes effect.
void theCompositorShowsTheFillNotTheScribbles() {
    TEST("the compositor draws the fill, not the scribbles");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);

    Compositor compositor;
    Framebuffer frame;
    const PixelRect region{0, 0, 260, 240};

    // Before anything is solved the layer draws nothing at all: compositing is
    // not allowed to start a max-flow behind the caller's back.
    compositor.composite(f.doc, f.timeline, f.image, region, frame);
    CHECK_NEAR(frame.pixel(130, 160).a, 0.0, 1e-3);

    ctgFill(f.doc, f.timeline, f.image, f.colour);
    compositor.composite(f.doc, f.timeline, f.image, region, frame);

    // Now the region is filled, at a spot no scribble ever touched.
    const Rgba inside = frame.pixel(130, 160);
    CHECK_NEAR(inside.a, 1.0, 0.02);
    CHECK_NEAR(inside.r, 1.0, 0.02);

    // And the line art still sits on top of its own colour.
    CHECK_NEAR(frame.pixel(130, 60).a, 1.0, 0.05);
}

// One scribble should fill one shape. Without an implicit background the solver
// has nothing to cut against, labels the whole canvas, and filling a shape means
// scribbling twice -- once for the shape and once for the world outside it.
void oneScribbleFillsOneShape() {
    TEST("one scribble fills one shape and leaves the rest alone");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);

    // Only the inside is scribbled. Nothing marks the background.
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    const CtgFill& fill = ctgFill(f.doc, f.timeline, f.image, f.colour);
    CHECK(fill.valid);
    CHECK_EQ(fill.colours, 1);

    // The shape is filled, well away from the scribble itself.
    CHECK_NEAR(fillAt(fill, 130, 160).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(fill, 80, 90).r, 1.0, 0.02);

    // And the world outside it is untouched, including just past the gap.
    CHECK_NEAR(fillAt(fill, 130, 215).a, 0.0, 1e-3);
    CHECK_NEAR(fillAt(fill, 20, 20).a, 0.0, 1e-3);
    CHECK_NEAR(fillAt(fill, 240, 220).a, 0.0, 1e-3);
}

}  // namespace

int main() {
    std::printf("ctg:\n");
    oneScribbleFillsOneShape();
    aScribbleFillsItsRegion();
    theLayerStoresScribblesNotTheFill();
    editingAScribbleRecoloursTheWholeRegion();
    theFillIsCachedUntilSomethingChanges();
    twoBarrierLayersClosseEachOthersGaps();
    noScribblesMeansNoFill();
    aCoarseSolveAgreesWithTheFineOne();
    theCompositorShowsTheFillNotTheScribbles();
    return testing::summarise("ctg");
}
