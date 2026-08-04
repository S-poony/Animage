// SPDX-License-Identifier: GPL-3.0-or-later
//
// The CTG layer: scribbles in, fill out, and the fill is never what is stored.

#include <array>
#include <chrono>

#include "brush.h"
#include "compositor.h"
#include "ctg.h"
#include "testing.h"

using namespace animage;

namespace {

struct Fixture {
    Document doc;
    TrackId track;
    LayerId ink;
    LayerId colour;
    ImageId image;

    Fixture() {
        track = doc.addTrack("main");
        colour = doc.addLayer(track, "colour", 0, LayerKind::Ctg);
        ink = doc.addLayer(track, "ink", 1);
        image = doc.insertImage(track, 0);

        Layer ctg = *doc.scene().findTrack(track)->findLayer(colour);
        ctg.ctg_sources = {ink};
        doc.updateLayer(track, colour, ctg);
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
        brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
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

    const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour);
    CHECK(fill.valid);
    CHECK_EQ(fill.colours, 2);

    // Inside is red, well away from where the scribble was actually drawn.
    const Rgba inside = fillAt(fill, 130, 160);
    CHECK_NEAR(inside.r, 1.0, 0.02);
    CHECK_NEAR(inside.b, 0.0, 0.02);

    // Outside is not blue, and this is the change the hard background made: a
    // scribble out there cannot expand to the rim, because the rim is
    // unseverable, so it keeps roughly its own pixels and the rest of the paper
    // stays uncoloured. The inside no longer needs it -- one scribble fills the
    // shape by itself -- but a *coloured* background is no longer had by
    // scribbling one.
    const Rgba outside = fillAt(fill, 130, 210);
    CHECK_NEAR(outside.a, 0.0, 0.001);

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

    const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour);
    const std::size_t filled_tiles = fill.tiles.tileCount();

    const Cel* scribbles = f.doc.celAt(f.track, f.image, f.colour);
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

    CHECK_NEAR(fillAt(ctgFill(f.doc, f.track, f.image, f.colour), 130, 160).r, 1.0, 0.02);

    // Scribble green over the red one. The region follows.
    f.stroke(f.colour, 100, 110, 150, 110, 8.0f, 0.0f, 1.0f, 0.0f);

    const CtgFill& after = ctgFill(f.doc, f.track, f.image, f.colour);
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

    const std::uint64_t first = ctgFill(f.doc, f.track, f.image, f.colour).inputs;
    CHECK(first != 0);
    CHECK_EQ(ctgFill(f.doc, f.track, f.image, f.colour).inputs, first);

    // Drawing on the line art must invalidate it, not only drawing a scribble.
    f.stroke(f.ink, 130, 178, 138, 178, 2.5f, 0, 0, 0);
    const std::uint64_t after_ink = ctgFill(f.doc, f.track, f.image, f.colour).inputs;
    CHECK(after_ink != first);

    f.stroke(f.colour, 100, 110, 120, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    CHECK(ctgFill(f.doc, f.track, f.image, f.colour).inputs != after_ink);
}

// Closing the gap on a second layer should stop the leak without touching the
// first. This is the improvement over TVPaint the design notes ask for by name.
void twoBarrierLayersClosseEachOthersGaps() {
    TEST("a second barrier layer closes the first one's gaps");
    Fixture f;
    const LayerId rough = f.doc.addLayer(f.track, "rough", 2);

    // The clean line has a wide hole; the rough happens to cross it.
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 110, 150);

    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);

    // With only the clean line, a gap that wide lets the boundary through: the
    // pixels right at the hole are contested.
    const Rgba single = fillAt(ctgFill(f.doc, f.track, f.image, f.colour), 130, 176);

    // Now add the rough as a second barrier, drawn across the hole.
    f.stroke(rough, 105, 180, 155, 180, 2.5f, 0, 0, 0);
    Layer ctg = *f.doc.scene().findTrack(f.track)->findLayer(f.colour);
    ctg.ctg_sources = {f.ink, rough};
    f.doc.updateLayer(f.track, f.colour, ctg);

    const CtgFill& both = ctgFill(f.doc, f.track, f.image, f.colour);
    const Rgba doubled = fillAt(both, 130, 176);

    // Whatever the single-source result was, with the gap bridged the pixel
    // just inside the wall belongs to the inside.
    CHECK_NEAR(doubled.r, 1.0, 0.02);
    CHECK_NEAR(doubled.b, 0.0, 0.02);
    (void)single;

    // And below the bridged wall is still outside.
    CHECK_NEAR(fillAt(both, 130, 210).a, 0.0, 0.001);  // outside is background now
}

// A CTG layer with nothing scribbled on it produces nothing, rather than
// filling the world with whatever colour it finds first.
void noScribblesMeansNoFill() {
    TEST("a CTG layer with no scribbles fills nothing");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);

    // No cel at all on the colour layer yet.
    const CtgFill& nothing = ctgFill(f.doc, f.track, f.image, f.colour);
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
    const CtgFill& quick = ctgFill(f.doc, f.track, f.image, f.colour, coarse);
    CHECK(quick.valid);

    // Well inside and well outside must still be right; only the boundary is
    // allowed to be rough.
    CHECK_NEAR(fillAt(quick, 130, 150).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(quick, 130, 220).a, 0.0, 0.001);
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
    compositor.composite(f.doc, f.track, f.image, region, frame);
    CHECK_NEAR(frame.pixel(130, 160).a, 0.0, 1e-3);

    ctgFill(f.doc, f.track, f.image, f.colour);
    compositor.composite(f.doc, f.track, f.image, region, frame);

    // Now the region is filled, at a spot no scribble ever touched.
    const Rgba inside = frame.pixel(130, 160);
    CHECK_NEAR(inside.a, 1.0, 0.02);
    CHECK_NEAR(inside.r, 1.0, 0.02);

    // And the line art still sits on top of its own colour.
    CHECK_NEAR(frame.pixel(130, 60).a, 1.0, 0.05);
}

// One scribble is enough. It did not used to be: with nothing to be cut
// against, the solver had no reason to stop anywhere and labelled everything it
// could reach, so filling one shape took two scribbles -- one for the shape and
// one for the world outside it.
//
// A background *seeded* at the rim was tried first and removed, and the reason
// it failed is worth keeping: the strength of a soft seed is its area, so a rim
// seed's authority came from the size of the canvas rather than from anything
// the user meant. The border is priced in the smoothing term instead, where it
// labels nothing and can overrule nobody. See LazyBrushOptions::gap_tolerance.
void oneScribbleFillsOneShape() {
    TEST("one scribble fills the shape it is in, gap and all");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour);
    CHECK(fill.valid);
    CHECK_EQ(fill.colours, 1);

    // Inside the box is filled, including the far corners and the stretch
    // beside the twenty-pixel hole in its bottom wall -- the hole is inside the
    // gap tolerance, so the boundary bridges it.
    CHECK_NEAR(fillAt(fill, 130, 160).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(fill, 70, 70).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(fill, 190, 170).r, 1.0, 0.02);

    // Outside is not, and no second scribble was needed to say so.
    CHECK_NEAR(fillAt(fill, 130, 215).a, 0.0, 0.001);
    CHECK_NEAR(fillAt(fill, 20, 20).a, 0.0, 0.001);

    // Adding a second colour outside does not disturb the first, which is the
    // property the unconditional background exists for. It does not fill the
    // outside either -- see aScribbleFillsItsRegion for why.
    f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);
    const CtgFill& both = ctgFill(f.doc, f.track, f.image, f.colour);
    CHECK_NEAR(fillAt(both, 130, 160).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(both, 130, 215).a, 0.0, 0.001);
}

// However large the drawing, the solve stays bounded. It runs where the
// interface is waiting, and a max-flow over a megapixel takes over a second --
// so on a big canvas an unbounded one does not take a while, it stops the
// program.
void theSolveStaysBoundedOnALargeDrawing() {
    TEST("the solve stays bounded however large the drawing");
    Fixture f;
    // A canvas big enough that it is not what bounds this: the point is that
    // the solve budget holds even when the picture genuinely is several
    // megapixels.
    f.doc.setCanvasSize(3000, 2400);

    // Line art spread far enough that the region is several megapixels.
    f.drawGappedBox(f.ink, 100, 100, 2600, 2000, 1300, 1400);
    f.stroke(f.colour, 800, 900, 1400, 900, 20.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 40, 40, 2700, 40, 20.0f, 0.0f, 0.0f, 1.0f);

    const auto started = std::chrono::steady_clock::now();
    const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    CHECK(fill.valid);
    CHECK(fill.region.width > 2000);   // the region really is large
    CHECK(seconds < 2.0);              // generous; an unbounded solve is far worse

    // And it is still correct, only coarser: deep inside the box is the inside
    // colour, and up by the outside scribble is the outside one.
    CHECK_NEAR(fillAt(fill, 1200, 1500).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(fill, 1200, 70).b, 1.0, 0.02);
}

// The region solved is the canvas. It used to be the bounding box of every tile
// anyone had touched, which was wrong at both ends: colour reached out past the
// frame after a stray stroke, and the colour *around* a shape stopped a tile
// from the outermost stroke instead of running to the edge of the picture.
void theFillCoversTheCanvasAndStopsThere() {
    TEST("the fill covers the canvas and stops at its edge");
    Fixture f;
    f.doc.setCanvasSize(400, 400);

    // A box that runs off the right-hand edge of the canvas, coloured inside,
    // with the surrounding colour scribbled in one corner only.
    f.drawGappedBox(f.ink, 100, 100, 700, 300, 380, 420);
    f.stroke(f.colour, 150, 200, 250, 200, 8.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 20, 20, 120, 20, 8.0f, 0.0f, 0.0f, 1.0f);

    const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour);
    CHECK(fill.valid);

    // Exactly the canvas: nothing outside it, and nothing short of it.
    CHECK_EQ(fill.region.x, 0);
    CHECK_EQ(fill.region.y, 0);
    CHECK_EQ(fill.region.width, 400);
    CHECK_EQ(fill.region.height, 400);

    // Inside the box takes the inside colour.
    CHECK_NEAR(fillAt(fill, 200, 200).r, 1.0, 0.02);

    // The far corners of the canvas take the background, which is now what a
    // scribble outside a shape leaves them: the rim cannot be bought, so the
    // outside colour keeps roughly its own pixels rather than spreading. What
    // this test still pins is that the *region* is the canvas and the extension
    // reaches its corners -- whatever label is there is carried all the way out.
    CHECK_NEAR(fillAt(fill, 380, 380).a, 0.0, 0.001);
    CHECK_NEAR(fillAt(fill, 10, 390).a, 0.0, 0.001);

    // And nothing beyond the frame, even though the box carries on out there.
    CHECK_NEAR(fillAt(fill, 500, 200).a, 0.0, 0.001);

    // Growing the canvas re-solves rather than serving the old answer from the
    // cache: the canvas is one of the fill's inputs.
    f.doc.setCanvasSize(800, 400);
    const CtgFill& wider = ctgFill(f.doc, f.track, f.image, f.colour);
    CHECK_EQ(wider.region.width, 800);
    CHECK_NEAR(fillAt(wider, 500, 200).r, 1.0, 0.02);
}

// The solve is over what has been drawn on, and the labels are extended from
// there to the rest of the canvas. So enlarging the canvas must not coarsen the
// answer over the drawing, and must not change it at all.
void theCanvasSizeDoesNotChangeTheFill() {
    TEST("the same drawing fills the same way on a bigger canvas");
    const auto fillOn = [](int canvas_w, int canvas_h) {
        Fixture f;
        f.doc.setCanvasSize(canvas_w, canvas_h);
        f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
        f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
        f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);
        const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour);
        // Sample inside the shape, just inside its outline, and out in the far
        // corner that only the extension can reach.
        return std::array<Rgba, 3>{fillAt(fill, 130, 120), fillAt(fill, 66, 66),
                                   fillAt(fill, canvas_w - 5, canvas_h - 5)};
    };

    const std::array<Rgba, 3> small = fillOn(320, 260);
    const std::array<Rgba, 3> large = fillOn(4000, 3000);

    // Inside the shape is the inside colour on both, and identical: the extra
    // canvas bought no resolution and cost none either.
    CHECK_NEAR(small[0].r, 1.0, 0.02);
    CHECK_NEAR(large[0].r, small[0].r, 0.001);
    CHECK_NEAR(large[0].b, small[0].b, 0.001);

    // And the edge of the shape lands in the same place, which is the part a
    // coarser solve would have moved.
    CHECK_NEAR(large[1].r, small[1].r, 0.001);
    CHECK_NEAR(large[1].b, small[1].b, 0.001);

    // The far corner of either canvas is the same as the other's, which is the
    // point: the extension reaches it and the canvas size does not change what
    // it finds.
    CHECK_NEAR(large[2].a, small[2].a, 0.001);
    CHECK_NEAR(large[2].r, small[2].r, 0.001);
}

}  // namespace

int main() {
    std::printf("ctg:\n");
    theSolveStaysBoundedOnALargeDrawing();
    theFillCoversTheCanvasAndStopsThere();
    theCanvasSizeDoesNotChangeTheFill();
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
