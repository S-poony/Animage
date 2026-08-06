// SPDX-License-Identifier: GPL-3.0-or-later
//
// The CTG layer: scribbles in, fill out, and the fill is never what is stored.

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

#include "brush.h"
#include "compositor.h"
#include "ctg.h"
#include "testing.h"

using namespace animage;

namespace {

void strokeOn(Document& doc, TrackId track, ImageId image, LayerId layer, float x0, float y0,
              float x1, float y1, float radius, float r, float g, float b) {
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
void gappedBoxOn(Document& doc, TrackId track, ImageId image, LayerId layer, int left, int top,
                 int right, int bottom, int gap_from, int gap_to) {
    const float w = 2.5f;
    strokeOn(doc, track, image, layer, left, top, right, top, w, 0, 0, 0);
    strokeOn(doc, track, image, layer, left, top, left, bottom, w, 0, 0, 0);
    strokeOn(doc, track, image, layer, right, top, right, bottom, w, 0, 0, 0);
    strokeOn(doc, track, image, layer, left, bottom, gap_from, bottom, w, 0, 0, 0);
    strokeOn(doc, track, image, layer, gap_to, bottom, right, bottom, w, 0, 0, 0);
}

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
        // Marks left where they were drawn, which is what most of these are
        // about. Moving them to follow the line art is on by default in the
        // application and has tests of its own further down; a fixture that did
        // it silently would quietly repair every case here that is *supposed*
        // to land wrong.
        ctg.ctg_follow_motion = false;
        doc.updateLayer(track, colour, ctg);
    }

    void stroke(LayerId layer, float x0, float y0, float x1, float y1, float radius,
                float r, float g, float b) {
        strokeOn(doc, track, image, layer, x0, y0, x1, y1, radius, r, g, b);
    }

    void drawGappedBox(LayerId layer, int left, int top, int right, int bottom, int gap_from,
                       int gap_to) {
        gappedBoxOn(doc, track, image, layer, left, top, right, bottom, gap_from, gap_to);
    }
};

// Several drawings in a row, which is what inheritance needs in order to have
// anything to inherit from.
struct Sequence {
    Document doc;
    TrackId track;
    LayerId ink;
    LayerId colour;
    std::vector<ImageId> images;

    explicit Sequence(int count, int hold = 1) {
        track = doc.addTrack("main");
        colour = doc.addLayer(track, "colour", 0, LayerKind::Ctg);
        ink = doc.addLayer(track, "ink", 1);

        for (int i = 0; i < count; ++i) {
            const std::size_t at = doc.scene().findTrack(track)->slots.size();
            images.push_back(doc.insertImage(track, at));
            if (hold > 1) doc.extendExposure(track, at, hold - 1);
        }

        Layer ctg = *doc.scene().findTrack(track)->findLayer(colour);
        ctg.ctg_sources = {ink};
        ctg.ctg_follow_motion = false;  // see Fixture
        doc.updateLayer(track, colour, ctg);
    }

    const Track& track_ref() const { return *doc.scene().findTrack(track); }

    // For the tests that are about the marks moving.
    void followTheMotion() {
        Layer ctg = *doc.scene().findTrack(track)->findLayer(colour);
        ctg.ctg_follow_motion = true;
        doc.updateLayer(track, colour, ctg);
    }

    void stroke(int drawing, LayerId layer, float x0, float y0, float x1, float y1,
                float radius, float r, float g, float b) {
        strokeOn(doc, track, images[static_cast<std::size_t>(drawing)], layer, x0, y0, x1, y1,
                 radius, r, g, b);
    }

    void box(int drawing, int left, int top, int right, int bottom, int gap_from, int gap_to) {
        gappedBoxOn(doc, track, images[static_cast<std::size_t>(drawing)], ink, left, top,
                    right, bottom, gap_from, gap_to);
    }

    const CtgFill& fillOf(int drawing) {
        return ctgFill(doc, track, images[static_cast<std::size_t>(drawing)], colour);
    }

    ImageId at(int drawing) const { return images[static_cast<std::size_t>(drawing)]; }
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

// The solver decides the pixels nobody said anything about. Where somebody did
// say something, that is the answer -- so a mark is a touch-up for whatever the
// min-cut missed, and it costs the solver nothing.
// How many scribbled pixels the fill disagrees with. Zero is the rule; the
// number is returned rather than asserted so a test can also show that there
// was a disagreement there to lose, which is the part that would otherwise pass
// for free.
std::size_t scribblePixelsNotHonoured(const Cel& scribbles, const CtgFill& fill,
                                      std::size_t* counted) {
    std::size_t wrong = 0;
    std::size_t total = 0;
    for (const TileCoord& coord : scribbles.tiles().coords()) {
        for (int y = 0; y < kTileSize; ++y) {
            for (int x = 0; x < kTileSize; ++x) {
                const int px = coord.x * kTileSize + x;
                const int py = coord.y * kTileSize + y;
                if (px < fill.region.x || px >= fill.region.x + fill.region.width) continue;
                if (py < fill.region.y || py >= fill.region.y + fill.region.height) continue;

                const Rgba mark = scribbles.pixel(px, py);
                if (mark.a < 0.5f) continue;  // the same threshold the seeding uses
                ++total;

                // Both premultiplied; the mark is opaque, so this is its colour.
                const Rgba got = fill.tiles.pixel(px, py);
                if (std::fabs(got.r - mark.r / mark.a) > 0.02f ||
                    std::fabs(got.g - mark.g / mark.a) > 0.02f ||
                    std::fabs(got.b - mark.b / mark.a) > 0.02f) {
                    ++wrong;
                }
            }
        }
    }
    if (counted) *counted = total;
    return wrong;
}

// The solver decides the pixels nobody said anything about. Where somebody did
// say something, that is the answer -- so a mark is a touch-up for whatever the
// min-cut missed, and it costs the solver nothing.
//
// Checked as the rule over every mark rather than at a point chosen to show it,
// because a point chosen to show it is a point the solver may happen to agree
// about. It usually does: a scribble sitting in open paper keeps its own region
// easily, since cutting across blank paper is dear. The disagreement is where a
// mark lands on the wrong side of a line, which is also the failure this exists
// to let somebody fix.
void aScribbleWinsInThePixelsItCovers() {
    TEST("a scribble wins in its own pixels, whatever the solver decided");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour);
    const Cel* scribbles = f.doc.celAt(f.track, f.image, f.colour);
    CHECK(scribbles != nullptr);

    std::size_t counted = 0;
    CHECK_EQ(scribblePixelsNotHonoured(*scribbles, fill, &counted), std::size_t{0});
    CHECK(counted > 500);  // there really were marks to honour

    // The mark is invisible, because it agrees with what it produced. That is
    // what stops this looking like scrawl over the artwork: you see the
    // disagreement and nothing else.
    CHECK_NEAR(fillAt(fill, 130, 110).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(fill, 130, 170).r, 1.0, 0.02);
}

// The disagreement that matters, and the one this exists for.
//
// The solve is capped -- it runs where the interface is waiting -- so on a
// large drawing it is coarse, and a mark finer than one cell of the solve grid
// is never sampled as a seed at all. The solver does not overrule it; it never
// hears about it. That is exactly when somebody is dabbing at a spot the fill
// got wrong, so it is exactly when the mark has to appear anyway.
//
// Constructed rather than tuned: the sample lattice starts at the region's
// origin, which is tile-aligned, so a step of eight samples every eighth pixel
// from a multiple of eight. A four-pixel dot centred at 125 falls between two
// of them and cannot be sampled, whatever the solver then decides.
void aMarkFinerThanTheSolveGridStillShows() {
    TEST("a mark finer than the solve grid still shows");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 125, 125, 125, 125, 2.0f, 0.0f, 1.0f, 0.0f);

    CtgSettings coarse;
    coarse.downscale = 8;
    const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour, coarse);

    // The solver never saw the green dot: one colour, not two.
    CHECK(fill.valid);
    CHECK_EQ(fill.colours, 1);

    // The region is red, as the solver decided.
    CHECK_NEAR(fillAt(fill, 90, 160).r, 1.0, 0.02);
    // And the dot is green anyway, because somebody drew it there.
    CHECK_NEAR(fillAt(fill, 125, 125).g, 1.0, 0.02);
    CHECK_NEAR(fillAt(fill, 125, 125).r, 0.0, 0.02);

    // Every mark honoured, dot included.
    const Cel* scribbles = f.doc.celAt(f.track, f.image, f.colour);
    CHECK_EQ(scribblePixelsNotHonoured(*scribbles, fill, nullptr), std::size_t{0});
}

// --- scribbles through time, part 1 --------------------------------------
//
// Absence of a cel on a CTG layer means inherited, not empty. Colour the first
// drawing of a run and the run is coloured; draw on one and it detaches from
// there onwards. Everything below is that one sentence, taken apart.

// The scribbles come from the earlier drawing and the barrier does not. That
// distinction is the feature: the colour carries forward, the line art it is
// cut against is always the drawing in front of you.
void aDrawingWithNoScribblesInheritsTheEarlierOnes() {
    TEST("a drawing with no scribbles of its own inherits the previous ones");
    Sequence s(3);

    // The same shape, moved right on the last drawing. Any point inside one box
    // and outside the other says which drawing's line art was used.
    s.box(0, 60, 60, 200, 180, 120, 140);
    s.box(1, 60, 60, 200, 180, 120, 140);
    s.box(2, 100, 60, 240, 180, 160, 180);

    // Scribbled once, on the first drawing only.
    s.stroke(0, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    CHECK(s.doc.celAt(s.track, s.at(1), s.colour) == nullptr);
    CHECK(s.doc.celAt(s.track, s.at(2), s.colour) == nullptr);

    // All three fill, and none of the later two owns a scribble.
    CHECK_NEAR(fillAt(s.fillOf(0), 130, 160).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).r, 1.0, 0.02);

    const CtgFill& third = s.fillOf(2);
    CHECK(third.valid);
    CHECK_EQ(third.colours, 1);

    // Inside the third drawing's box, out beyond where the first drawing's box
    // ended: filled, so the barrier is this drawing's own.
    CHECK_NEAR(fillAt(third, 225, 160).r, 1.0, 0.02);
    // And inside the *first* drawing's box, outside the third's: not filled.
    CHECK_NEAR(fillAt(third, 70, 160).a, 0.0, 0.001);

    // Still nothing allocated on the drawings that inherited.
    CHECK(s.doc.celAt(s.track, s.at(1), s.colour) == nullptr);
    CHECK(s.doc.celAt(s.track, s.at(2), s.colour) == nullptr);
}

// Drawing on an inheriting drawing copies what it was showing and edits the
// copy, so the marks that were already there survive the first stroke. Getting
// this wrong would make one stroke wipe the colour off the drawing you were
// adding to, which is the worst possible failure and the quietest.
void editingADrawingDetachesItAndTheOnesAfterFollow() {
    TEST("drawing on an inherited scribble copies it rather than starting empty");
    Sequence s(4);
    for (int i = 0; i < 4; ++i) s.box(i, 60, 60, 200, 180, 120, 140);
    s.stroke(0, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    CHECK_NEAR(fillAt(s.fillOf(3), 130, 160).r, 1.0, 0.02);

    // A green mark on the third drawing, nowhere near the inherited red one.
    s.stroke(2, s.colour, 80, 165, 95, 165, 5.0f, 0.0f, 1.0f, 0.0f);

    // It has its own cel now, and that cel still carries the inherited red --
    // this is the copy, and it is what stops a first stroke from erasing.
    const Cel* own = s.doc.celAt(s.track, s.at(2), s.colour);
    CHECK(own != nullptr);
    CHECK_NEAR(own->pixel(135, 110).r, 1.0, 0.02);
    CHECK_NEAR(own->pixel(88, 165).g, 1.0, 0.02);

    // Two colours from here on, one before.
    CHECK_EQ(s.fillOf(1).colours, 1);
    CHECK_EQ(s.fillOf(2).colours, 2);
    CHECK_EQ(s.fillOf(3).colours, 2);

    // The earlier drawings are untouched, and still own nothing.
    CHECK(s.doc.celAt(s.track, s.at(0), s.colour) != nullptr);
    CHECK(s.doc.celAt(s.track, s.at(1), s.colour) == nullptr);
    CHECK(s.doc.celAt(s.track, s.at(3), s.colour) == nullptr);
}

// Reverting is clearCel and nothing else, because absence is what inheriting
// means. No "delete inherited scribble" concept exists and none is needed.
void clearingAnOverrideReturnsItToInheriting() {
    TEST("clearing a drawing's own scribbles returns it to inheriting");
    Sequence s(3);
    for (int i = 0; i < 3; ++i) s.box(i, 60, 60, 200, 180, 120, 140);
    s.stroke(0, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    s.stroke(1, s.colour, 120, 110, 150, 110, 7.0f, 0.0f, 0.0f, 1.0f);

    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).b, 1.0, 0.02);
    CHECK_NEAR(fillAt(s.fillOf(2), 130, 160).b, 1.0, 0.02);

    s.doc.clearCel(s.track, s.at(1), s.colour);
    CHECK(s.doc.celAt(s.track, s.at(1), s.colour) == nullptr);

    // Both are back to the first drawing's red, without anything having had to
    // know that blue was an override.
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(s.fillOf(2), 130, 160).r, 1.0, 0.02);
}

// Resolving at read time is what makes this true, and it is why the design says
// never to store a parent pointer: one would be stale the instant a drawing was
// deleted, and stale intermittently.
void deletingTheSourceLeavesLaterDrawingsInheritingFromWhatPrecedes() {
    TEST("deleting the drawing a scribble came from re-inherits from earlier");
    Sequence s(3);
    for (int i = 0; i < 3; ++i) s.box(i, 60, 60, 200, 180, 120, 140);
    s.stroke(0, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    s.stroke(1, s.colour, 120, 110, 150, 110, 7.0f, 0.0f, 0.0f, 1.0f);

    CHECK_NEAR(fillAt(s.fillOf(2), 130, 160).b, 1.0, 0.02);

    s.doc.removeDrawing(s.track, s.at(1));
    CHECK_NEAR(fillAt(s.fillOf(2), 130, 160).r, 1.0, 0.02);
}

void reorderingChangesInheritanceWithoutTouchingACel() {
    TEST("reordering drawings changes who inherits from whom, and costs nothing");
    Sequence s(3);
    for (int i = 0; i < 3; ++i) s.box(i, 60, 60, 200, 180, 120, 140);
    s.stroke(0, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    s.stroke(2, s.colour, 120, 110, 150, 110, 7.0f, 0.0f, 0.0f, 1.0f);

    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).r, 1.0, 0.02);

    // Bring the last drawing to the front: 2, 0, 1.
    s.doc.moveDrawing(s.track, s.at(2), 0);

    // The middle drawing now follows the blue one, and still owns no cel.
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).r, 1.0, 0.02);  // 0 still precedes 1
    CHECK(s.doc.celAt(s.track, s.at(1), s.colour) == nullptr);

    // And the drawing that used to be first now inherits blue from the one in
    // front of it -- except it has red of its own, so it keeps red.
    CHECK_NEAR(fillAt(s.fillOf(0), 130, 160).r, 1.0, 0.02);

    // Move the red one to the end instead: 2, 1, 0. Now the middle drawing,
    // which owns nothing, follows blue.
    s.doc.moveDrawing(s.track, s.at(0), 2);
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).b, 1.0, 0.02);
    CHECK(s.doc.celAt(s.track, s.at(1), s.colour) == nullptr);
}

// Forward only. A drawing before the one that was scribbled has nothing to
// inherit and stays uncoloured; it does not reach forwards for a scribble it
// was never given. Backwards is a separate, explicit thing if it is ever
// wanted -- it is sometimes what you want and never what you expect.
void inheritanceRunsForwardOnly() {
    TEST("inheritance runs forward only");
    Sequence s(3);
    for (int i = 0; i < 3; ++i) s.box(i, 60, 60, 200, 180, 120, 140);
    s.stroke(1, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    CHECK(!s.fillOf(0).valid);  // nothing before it, so nothing at all
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(s.fillOf(2), 130, 160).r, 1.0, 0.02);
}

// A drawing held for five frames is one step back, not five, and a hold is not
// a break in the chain either.
void inheritanceCrossesAHold() {
    TEST("a held drawing is one step back, not one per frame");
    Sequence s(3, 5);  // each drawing exposed over five frames
    CHECK_EQ(s.track_ref().frameCount(), std::size_t{15});
    for (int i = 0; i < 3; ++i) s.box(i, 60, 60, 200, 180, 120, 140);
    s.stroke(0, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    CHECK_EQ(s.track_ref().celSourceFor(s.at(2), s.colour), s.at(0));
    CHECK_NEAR(fillAt(s.fillOf(2), 130, 160).r, 1.0, 0.02);
}

// Inheritance belongs to the CTG layer and to nothing else. On a raster layer
// an absent cel still means the layer is empty here, and it has to keep meaning
// that: it is what makes adding a layer and holding a drawing cost nothing.
void onlyAColourLayerInherits() {
    TEST("a raster layer with no cel is still empty, not inherited");
    Sequence s(2);
    s.box(0, 60, 60, 200, 180, 120, 140);

    CHECK(s.doc.celAt(s.track, s.at(1), s.ink) == nullptr);
    CHECK(s.doc.ctgScribblesAt(s.track, s.at(1), s.ink) == nullptr);
    CHECK_EQ(s.track_ref().celSourceFor(s.at(1), s.ink), s.at(0));  // the walk is neutral

    // The compositor draws nothing for the second drawing's ink.
    Compositor compositor;
    Framebuffer frame;
    compositor.composite(s.doc, s.track, s.at(1), {0, 0, 260, 240}, frame);
    CHECK_NEAR(frame.pixel(130, 60).a, 0.0, 1e-3);
}

// The invariant inheritance was most likely to break by materialising
// something. It does not, because it resolves at read time.
void addingAColourLayerToALongTrackAllocatesNothing() {
    TEST("adding a colour layer to a long track still allocates nothing");
    Sequence s(1);
    for (int i = 0; i < 499; ++i) {
        const std::size_t at = s.doc.scene().findTrack(s.track)->slots.size();
        s.doc.insertImage(s.track, at);
    }
    const std::size_t before = s.doc.celCount();
    s.doc.addLayer(s.track, "colour 2", 0, LayerKind::Ctg);
    CHECK_EQ(s.doc.celCount(), before);
}

// Reordering moves no revision anywhere -- it only changes which cel a drawing
// reads its scribbles from -- so a cache key made of revisions alone goes on
// serving the colour from whichever drawing used to precede this one. The two
// scribbles here are drawn identically on purpose, so their cels sit at exactly
// the same revision and only their identity tells them apart. That is not a
// contrived case: every cel in a project straight off disk is at revision 1.
void aReorderInvalidatesTheFillEvenWhenNoRevisionMoves() {
    TEST("a reorder invalidates the fill even though no revision moved");
    Sequence s(3);
    for (int i = 0; i < 3; ++i) s.box(i, 60, 60, 200, 180, 120, 140);

    // The same stroke twice, in two colours: same dabs, same revision.
    s.stroke(0, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    s.stroke(2, s.colour, 120, 110, 150, 110, 6.0f, 0.0f, 0.0f, 1.0f);
    CHECK_EQ(s.doc.celAt(s.track, s.at(0), s.colour)->revision(),
             s.doc.celAt(s.track, s.at(2), s.colour)->revision());

    // The middle drawing follows the red one, and that answer gets cached.
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).r, 1.0, 0.02);

    // Slide the blue drawing in between, so the middle one now follows blue:
    // the order becomes 0, 2, 1. Nothing has been written to any cel.
    s.doc.moveDrawing(s.track, s.at(2), 1);
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).b, 1.0, 0.02);
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).r, 0.0, 0.02);
}

// The change most likely to be missed, because nothing fails when it is: the
// fill is right either way and only the cost moves. Under the old key -- the
// cel holding the scribbles -- a run of drawings inheriting one cel shared one
// cache slot and fought over it, re-solving on every frame change.
void solvingTwoDrawingsAndComingBackDoesNotResolve() {
    TEST("coming back to a drawing does not re-solve it");
    Sequence s(3);
    for (int i = 0; i < 3; ++i) s.box(i, 60, 60, 200, 180, 120, 140);
    s.stroke(0, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    // All three inherit one cel, and each is its own entry.
    s.fillOf(0);
    s.fillOf(1);
    s.fillOf(2);
    const std::uint64_t solves = s.doc.ctgCache().storeCount();
    CHECK_EQ(solves, std::uint64_t{3});

    // Scrubbing back over them costs nothing at all.
    s.fillOf(1);
    s.fillOf(0);
    s.fillOf(2);
    s.fillOf(1);
    CHECK_EQ(s.doc.ctgCache().storeCount(), solves);

    // And each drawing still has its own fill to serve, rather than one of them
    // having overwritten the others.
    CHECK(s.doc.ctgFillFor(s.track, s.at(0), s.colour) != nullptr);
    CHECK(s.doc.ctgFillFor(s.track, s.at(1), s.colour) != nullptr);
    CHECK(s.doc.ctgFillFor(s.track, s.at(2), s.colour) != nullptr);
}

// --- erasing puts things back ---------------------------------------------

// A stroke on a CTG layer exactly as the canvas makes one: no pressure on
// opacity, hardness 1, opacity 1, because a scribble is a label and not paint.
// See CanvasWidget::beginStroke.
void ctgStroke(Document& doc, TrackId track, ImageId image, LayerId layer, float x0, float y0,
               float x1, float y1, float radius, float r, float g, float b, bool erase) {
    ScopedCommand command(doc, erase ? "Erase" : "Stroke");
    BrushSettings settings;
    settings.radius = radius;
    settings.hardness = 1.0f;
    settings.opacity = 1.0f;
    settings.pressure_affects_opacity = false;
    settings.r = r;
    settings.g = g;
    settings.b = b;
    settings.a = 1.0f;
    settings.erase = erase;
    settings.label = true;
    Brush brush(settings);
    brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
    brush.extend({x1, y1, 1.0f});
    brush.end();
}

std::size_t differingPixels(const CtgFill& a, const CtgFill& b, const PixelRect& over) {
    std::size_t differing = 0;
    for (int y = over.y; y < over.y + over.height; ++y) {
        for (int x = over.x; x < over.x + over.width; ++x) {
            const Rgba p = a.tiles.pixel(x, y);
            const Rgba q = b.tiles.pixel(x, y);
            if (std::fabs(p.r - q.r) > 0.01f || std::fabs(p.g - q.g) > 0.01f ||
                std::fabs(p.b - q.b) > 0.01f || std::fabs(p.a - q.a) > 0.01f) {
                ++differing;
            }
        }
    }
    return differing;
}

bool sameRect(const PixelRect& a, const PixelRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// Erasing a mark has to put the fill back where it was. Adding a scribble
// changes how the ones already there behave -- that is the whole point of a
// global min-cut, and it is expected -- but taking it away again has to undo
// exactly that and nothing more.
void erasingAScribbleUndoesWhatItDid() {
    TEST("erasing a scribble puts the fill back as it was");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);

    ctgStroke(f.doc, f.track, f.image, f.colour, 100, 110, 150, 110, 6.0f, 1, 0, 0, false);
    const CtgFill before = ctgFill(f.doc, f.track, f.image, f.colour);

    // A second one changes how the first behaves. That much is expected.
    ctgStroke(f.doc, f.track, f.image, f.colour, 90, 150, 160, 150, 5.0f, 0, 0, 1, false);
    const CtgFill with_second = ctgFill(f.doc, f.track, f.image, f.colour);
    CHECK_EQ(with_second.colours, 2);
    CHECK(differingPixels(before, with_second, before.region) > 100);

    // Erased by retracing it with the same nib, which is how anybody rubs a
    // mark out.
    ctgStroke(f.doc, f.track, f.image, f.colour, 90, 150, 160, 150, 5.0f, 0, 0, 0, true);
    const CtgFill after = ctgFill(f.doc, f.track, f.image, f.colour);

    CHECK_EQ(after.colours, before.colours);
    CHECK(sameRect(after.solved, before.solved));
    CHECK_EQ(after.step, before.step);
    CHECK_EQ(differingPixels(before, after, before.region), std::size_t{0});
}

// The same question with the erased mark somewhere the line art is not, which
// is the case that was broken.
//
// celBounds was built from tile coordinates alone, and erasing empties a tile
// without releasing it, so the solve region kept the shape of a mark that was
// no longer there. The region chooses the solve resolution, so a stray scribble
// out in a corner -- made and then rubbed out -- left every later solve coarser
// than it had been before the scribble was ever drawn. Permanently, invisibly,
// and with nothing on screen to attribute it to.
//
// The invariant is asserted on the solve and not only on the pixels, because a
// coarser solve does not have to move a pixel to be wrong: this very drawing,
// at step 2 rather than 1, comes out identical. Waiting for the pixels to
// disagree would have been waiting for a geometry that happened to expose it.
void erasingAStrayScribbleUndoesWhatItDid() {
    TEST("erasing a scribble drawn out on its own puts the solve back too");
    Fixture f;
    f.doc.setCanvasSize(600, 600);
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);

    ctgStroke(f.doc, f.track, f.image, f.colour, 100, 110, 150, 110, 6.0f, 1, 0, 0, false);
    const CtgFill before = ctgFill(f.doc, f.track, f.image, f.colour);
    const Cel* cel = f.doc.celAt(f.track, f.image, f.colour);
    const std::size_t tiles_before = cel->tiles().tileCount();

    // A stray mark out in the corner, well away from anything drawn. It really
    // does coarsen the solve -- that is not the bug, it is the cost of the
    // drawing being bigger.
    ctgStroke(f.doc, f.track, f.image, f.colour, 480, 500, 520, 500, 6.0f, 0, 0, 1, false);
    const CtgFill stray = ctgFill(f.doc, f.track, f.image, f.colour);
    CHECK(stray.step > before.step);
    CHECK(cel->tiles().tileCount() > tiles_before);

    // Erased completely, with a wider nib.
    ctgStroke(f.doc, f.track, f.image, f.colour, 470, 500, 530, 500, 12.0f, 0, 0, 0, true);
    const CtgFill after = ctgFill(f.doc, f.track, f.image, f.colour);

    // The tiles are still allocated -- that is a separate matter, and the point
    // is that the solve no longer cares.
    CHECK(cel->tiles().tileCount() > tiles_before);
    CHECK_EQ(after.colours, before.colours);
    CHECK(sameRect(after.solved, before.solved));
    CHECK_EQ(after.step, before.step);
    CHECK_EQ(differingPixels(before, after, before.region), std::size_t{0});
}

// A mark made outside a shape, near its wall. Reported as colour appearing
// where nothing was scribbled, and suspected of the colour layer being used as
// its own barrier -- so this pins both halves at once.
void closedBox(Fixture& f, int left, int top, int right, int bottom) {
    const float w = 2.5f;
    f.stroke(f.ink, left, top, right, top, w, 0, 0, 0);
    f.stroke(f.ink, left, bottom, right, bottom, w, 0, 0, 0);
    f.stroke(f.ink, left, top, left, bottom, w, 0, 0, 0);
    f.stroke(f.ink, right, top, right, bottom, w, 0, 0, 0);
}

void aMarkOutsideAShapeNearItsWall() {
    TEST("what a mark just outside a shape does");

    // A colour layer is never its own barrier: it holds labels, not edges, and
    // a flat has nothing to cut along. Its cel is raster tiles like any other --
    // there is no scribble tool, you draw with the ordinary brush -- but the
    // *layer* is LayerKind::Ctg, and that is what the barrier is chosen by.
    {
        Fixture f;
        const Layer* self = f.doc.scene().findTrack(f.track)->findLayer(f.colour);
        CHECK(std::find(self->ctg_sources.begin(), self->ctg_sources.end(), f.colour) ==
              self->ctg_sources.end());
    }

    // A shape that is genuinely closed, with the mark outside its left wall and
    // close to it. It does not reach in: the shape stays uncoloured, the paper
    // around it stays uncoloured, and the mark keeps its own pixels and nothing
    // else. That last part is what the rim being unseverable buys.
    {
        Fixture f;
        closedBox(f, 100, 100, 300, 300);
        ctgStroke(f.doc, f.track, f.image, f.colour, 60, 200, 88, 200, 8.0f, 1, 0, 0, false);
        const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour);

        CHECK_NEAR(fillAt(fill, 200, 200).a, 0.0, 0.001);  // inside the shape
        CHECK_NEAR(fillAt(fill, 20, 20).a, 0.0, 0.001);    // far out on the paper
        CHECK_NEAR(fillAt(fill, 74, 200).r, 1.0, 0.02);    // on the mark itself
        CHECK(fill.spread < 1.5f);  // it filled nothing, which is the honest report
    }

    // The same, with a real hole in the wall the mark is sitting next to, which
    // is what line art actually looks like. Still does not reach in: separating
    // the mark from the rim where it is costs less than pouring through the gap.
    {
        Fixture f;
        const float w = 2.5f;
        f.stroke(f.ink, 100, 100, 300, 100, w, 0, 0, 0);
        f.stroke(f.ink, 100, 300, 300, 300, w, 0, 0, 0);
        f.stroke(f.ink, 300, 100, 300, 300, w, 0, 0, 0);
        f.stroke(f.ink, 100, 100, 100, 180, w, 0, 0, 0);  // left wall, gap 180..220
        f.stroke(f.ink, 100, 220, 100, 300, w, 0, 0, 0);
        ctgStroke(f.doc, f.track, f.image, f.colour, 60, 200, 88, 200, 8.0f, 1, 0, 0, false);
        const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour);

        CHECK_NEAR(fillAt(fill, 200, 200).a, 0.0, 0.001);
        CHECK_NEAR(fillAt(fill, 74, 200).r, 1.0, 0.02);
        CHECK(fill.spread < 1.5f);
    }
}

// --- what a colour layer is allowed to do with time -----------------------

void setCtg(Sequence& s, bool inherit, CtgDirection direction) {
    Layer layer = *s.doc.scene().findTrack(s.track)->findLayer(s.colour);
    layer.ctg_inherit = inherit;
    layer.ctg_direction = direction;
    s.doc.updateLayer(s.track, s.colour, layer);
}

// Carrying marks forward is the default and is worth having, and it is not the
// only way to work: a shot whose design changes every drawing gets nothing from
// it and has to go looking for the marks it carried. Switched off, absence
// means what it means on a raster layer.
void carryingScribblesCanBeSwitchedOff() {
    TEST("a colour layer can be told not to carry marks at all");
    Sequence s(3);
    for (int i = 0; i < 3; ++i) s.box(i, 60, 60, 200, 180, 120, 140);
    s.stroke(0, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    CHECK_NEAR(fillAt(s.fillOf(2), 130, 160).r, 1.0, 0.02);

    setCtg(s, false, CtgDirection::Forward);
    CHECK(!s.fillOf(2).valid);   // empty here means empty
    CHECK(!s.fillOf(1).valid);
    CHECK(s.fillOf(0).valid);    // its own marks are still its own

    // And back, without anything having been stored or thrown away: the marks
    // were never copied anywhere, so there is nothing to undo.
    setCtg(s, true, CtgDirection::Forward);
    CHECK_NEAR(fillAt(s.fillOf(2), 130, 160).r, 1.0, 0.02);
    CHECK(s.doc.celAt(s.track, s.at(2), s.colour) == nullptr);
}

// Backward is for colouring the drawing in front of you -- often the last of a
// run, because it is the one you were working on -- and having it apply to
// everything before it.
void carryingCanRunBackwards() {
    TEST("marks can be carried backwards instead");
    Sequence s(3);
    for (int i = 0; i < 3; ++i) s.box(i, 60, 60, 200, 180, 120, 140);
    s.stroke(2, s.colour, 120, 110, 150, 110, 6.0f, 0.0f, 0.0f, 1.0f);

    // Forwards, nothing reaches the drawings before it.
    CHECK(!s.fillOf(0).valid);
    CHECK(!s.fillOf(1).valid);

    setCtg(s, true, CtgDirection::Backward);
    CHECK_NEAR(fillAt(s.fillOf(0), 130, 160).b, 1.0, 0.02);
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).b, 1.0, 0.02);
    CHECK_EQ(s.track_ref().celSourceFor(s.at(0), s.colour, +1), s.at(2));

    // Marks of its own still win over anything carried to it, whichever way the
    // carrying runs.
    s.stroke(1, s.colour, 120, 110, 150, 110, 7.0f, 1.0f, 0.0f, 0.0f);
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(s.fillOf(0), 130, 160).r, 1.0, 0.02);  // now the nearest later one
}

// Carrying both ways fills the gaps between the drawings you have coloured,
// rather than only what follows them. Whichever coloured drawing is fewer
// drawings off wins; a tie goes to the earlier one.
void carryingCanRunBothWays() {
    TEST("marks can be carried from whichever side is nearer");
    Sequence s(5);
    for (int i = 0; i < 5; ++i) s.box(i, 60, 60, 200, 180, 120, 140);

    // Coloured at each end, nothing in between.
    s.stroke(0, s.colour, 120, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);  // red
    s.stroke(4, s.colour, 120, 110, 150, 110, 6.0f, 0.0f, 0.0f, 1.0f);  // blue

    // Forwards, the whole middle follows the red one.
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(s.fillOf(3), 130, 160).r, 1.0, 0.02);

    setCtg(s, true, CtgDirection::Nearest);
    CHECK_NEAR(fillAt(s.fillOf(1), 130, 160).r, 1.0, 0.02);  // one back, three on
    CHECK_NEAR(fillAt(s.fillOf(3), 130, 160).b, 1.0, 0.02);  // three back, one on

    // Dead centre: two either way, and the earlier one takes it.
    CHECK_NEAR(fillAt(s.fillOf(2), 130, 160).r, 1.0, 0.02);
    CHECK_EQ(s.track_ref().celSourceFor(s.at(2), s.colour, 0), s.at(0));

    // With only one side coloured it reaches the other way rather than giving
    // up, which is the whole difference from picking a direction.
    Sequence t(3);
    for (int i = 0; i < 3; ++i) t.box(i, 60, 60, 200, 180, 120, 140);
    t.stroke(2, t.colour, 120, 110, 150, 110, 6.0f, 0.0f, 1.0f, 0.0f);
    setCtg(t, true, CtgDirection::Nearest);
    CHECK_NEAR(fillAt(t.fillOf(0), 130, 160).g, 1.0, 0.02);
    CHECK_NEAR(fillAt(t.fillOf(1), 130, 160).g, 1.0, 0.02);
}

// What `spread` measures, which is all that is left of the flag that was built
// on it: how much region a mark won for each pixel of itself. The flag came out
// -- see docs/handover.md -- and the number stayed, because it is the honest
// measurement the next attempt has to beat and bench_carry reports it.
void aCarriedMarkThatLandsWrongWinsNothingButItself() {
    TEST("a carried mark that lands on nothing wins nothing but itself");
    Sequence s(2);

    // The same shape on both drawings, but moved a long way across. A mark
    // scribbled inside it on the first drawing is outside it on the second.
    s.box(0, 60, 60, 200, 180, 120, 140);
    s.box(1, 320, 60, 460, 180, 380, 400);
    s.stroke(0, s.colour, 90, 100, 170, 100, 10.0f, 1.0f, 0.0f, 0.0f);

    // On the drawing it was made for, it lands where it meant to and is not
    // flagged -- and would not be flagged even if it did not, because you can
    // see that one happening.
    const CtgFill& own = s.fillOf(0);
    CHECK(own.valid);
    CHECK(!own.inherited);
    // It filled a shape many times its own size: 8.3 here, which is the box's
    // area over the mark's.
    CHECK(own.spread > 5.0f);

    // Carried to the second, it is out in the open with the shape elsewhere, so
    // it fills nothing but itself: with no line art to follow, the cut hugs the
    // seed.
    const CtgFill& carried = s.fillOf(1);
    CHECK(carried.valid);
    CHECK(carried.inherited);
    CHECK(carried.spread < 1.05f);  // exactly 1.00: the cut hugs the seed

    // And the signal the design notes propose says nothing at all here, which
    // is the whole reason it was not what the flag rested on.
    CHECK(carried.confidence > 0.99f);
}

// And the ordinary case, which is what the number could never be told from --
// a mark filling a small region snugly measures 1.96, against 1.00 here.
void aCarriedMarkThatLandsRightWinsARegion() {
    TEST("a carried mark that lands where it should wins a whole region");
    Sequence s(3);
    for (int i = 0; i < 3; ++i) s.box(i, 60, 60, 200, 180, 120, 140);
    s.stroke(0, s.colour, 90, 100, 170, 100, 10.0f, 1.0f, 0.0f, 0.0f);

    for (int i = 0; i < 3; ++i) {
        const CtgFill& fill = s.fillOf(i);
        CHECK(fill.valid);
        CHECK(fill.spread > 5.0f);
    }
    CHECK(s.fillOf(1).inherited);
    CHECK(!s.fillOf(0).inherited);
}

// The trap this measurement has: a mark wins its own pixels in the finished
// fill whatever the solver decided, so reading confidence back off the fill
// would report every mark as perfectly placed, always.
void confidenceIsMeasuredAgainstTheSolveAndNotTheFill() {
    TEST("confidence reads the solver's answer, not the one the mark overrode");
    Sequence s(2);
    s.box(0, 60, 60, 200, 180, 120, 140);
    s.box(1, 320, 60, 460, 180, 380, 400);
    s.stroke(0, s.colour, 90, 100, 170, 100, 10.0f, 1.0f, 0.0f, 0.0f);

    const CtgFill& carried = s.fillOf(1);
    CHECK(carried.spread < 1.05f);

    // Every one of those pixels is nonetheless its own colour in the fill,
    // which is what makes the fill useless for judging this.
    const Cel* marks = s.doc.celAt(s.track, s.at(0), s.colour);
    CHECK(marks != nullptr);
    CHECK_EQ(scribblePixelsNotHonoured(*marks, carried, nullptr), std::size_t{0});
}

// --- a mark is a label, and one of the labels is "nothing" ----------------

// Every distinct label on the cel, and the alpha values it uses.
std::vector<std::uint32_t> labelsOn(const Cel& cel, std::vector<float>* alphas) {
    std::vector<std::uint32_t> labels;
    for (const TileCoord& coord : cel.tiles().coords()) {
        for (int y = 0; y < kTileSize; ++y) {
            for (int x = 0; x < kTileSize; ++x) {
                const Rgba p = cel.pixel(coord.x * kTileSize + x, coord.y * kTileSize + y);
                if (alphas && std::find(alphas->begin(), alphas->end(), p.a) == alphas->end()) {
                    alphas->push_back(p.a);
                }
                if (!isScribbled(p)) continue;
                const std::uint32_t key = scribbleLabel(p);
                if (std::find(labels.begin(), labels.end(), key) == labels.end()) {
                    labels.push_back(key);
                }
            }
        }
    }
    return labels;
}

// The brush blended, and the solver has thresholded from the start, so the two
// only ever agreed in the middle of a stroke. At its rim, and anywhere one
// colour crossed another, it left pixels quantising to a third colour -- a
// label nobody drew. Invisible while the fill was all you could see, and not
// invisible now that a mark draws over its own fill.
void aCtgStrokeWritesLabelsAndNotPaint() {
    TEST("a mark on a colour layer is written hard, so it makes no third colour");
    Fixture f;
    ctgStroke(f.doc, f.track, f.image, f.colour, 80, 100, 180, 100, 8.0f, 1, 0, 0, false);
    ctgStroke(f.doc, f.track, f.image, f.colour, 130, 50, 130, 150, 8.0f, 0, 0, 1, false);

    const Cel* cel = f.doc.celAt(f.track, f.image, f.colour);
    std::vector<float> alphas;
    const std::vector<std::uint32_t> labels = labelsOn(*cel, &alphas);

    // Two strokes crossing, two labels. Not one more for the seam between them,
    // and not one more for either rim.
    CHECK_EQ(labels.size(), std::size_t{2});

    // And a pixel is scribbled or it is not: nothing in between was written.
    for (float a : alphas) CHECK(a == 0.0f || a == 1.0f);

    // Erasing writes exact zeros, so a rubbed-out mark leaves nothing behind
    // rather than a rim too faint to see and strong enough to seed.
    ctgStroke(f.doc, f.track, f.image, f.colour, 130, 50, 130, 150, 12.0f, 0, 0, 0, true);
    std::vector<float> after_alphas;
    const std::vector<std::uint32_t> after = labelsOn(*cel, &after_alphas);
    CHECK_EQ(after.size(), std::size_t{1});
    CHECK_EQ(after[0], std::uint32_t{0xff0000});
    for (float a : after_alphas) CHECK(a == 0.0f || a == 1.0f);
}

// Transparency has to be a label rather than an absence, because an absence is
// already how the layer says nothing was scribbled at all. Stored as a pixel
// that is unmistakably present and unmistakably not a colour.
void theTransparentScribbleSurvivesTheHalfFloat() {
    TEST("the transparent label survives being stored");
    Tile tile;
    tile.setPixel(3, 4, kTransparentScribble);
    const Rgba back = tile.pixel(3, 4);

    CHECK_EQ(back.r, -1.0f);
    CHECK_EQ(back.a, 1.0f);
    CHECK(isScribbled(back));
    CHECK(isTransparentScribble(back));
    CHECK_EQ(scribbleLabel(back), kTransparentScribbleLabel);
    CHECK_EQ(scribbleColour(kTransparentScribbleLabel).a, 0.0f);

    // And no colour can be mistaken for it, including the extremes.
    CHECK(!isTransparentScribble(Rgba{0.0f, 0.0f, 0.0f, 1.0f}));
    CHECK(!isTransparentScribble(Rgba{1.0f, 1.0f, 1.0f, 1.0f}));
    CHECK(scribbleLabel(Rgba{0.0f, 0.0f, 0.0f, 1.0f}) != kTransparentScribbleLabel);
}

// It competes for regions like any other colour -- that is the point of it
// being a label -- and where it wins, the picture has a hole rather than a
// colour. And a transparent mark keeps its own pixels, so it can be used to
// take colour back off a spot the fill got wrong.
void aTransparentScribbleTakesColourAway() {
    TEST("a transparent scribble is a colour that leaves nothing behind");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);

    // Red fills the box.
    ctgStroke(f.doc, f.track, f.image, f.colour, 90, 100, 170, 100, 6.0f, 1, 0, 0, false);
    CHECK_NEAR(fillAt(ctgFill(f.doc, f.track, f.image, f.colour), 130, 160).r, 1.0, 0.02);

    // A transparent scribble lower down. Two soft marks inside one shape do not
    // put it to a vote -- the cut settles between them, at the cheapest
    // boundary it can find -- so each takes the part of the shape nearer it.
    ctgStroke(f.doc, f.track, f.image, f.colour, 85, 150, 175, 150, 12.0f,
              kTransparentScribble.r, kTransparentScribble.g, kTransparentScribble.b, false);

    const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour);

    // Two labels, and transparency is one of them: it is a colour, not the
    // absence of a scribble.
    CHECK_EQ(fill.colours, 2);

    // Its half of the shape has nothing in it -- no colour at all, rather than
    // some colour standing in for none.
    CHECK_NEAR(fillAt(fill, 130, 172).a, 0.0, 0.001);
    CHECK_NEAR(fillAt(fill, 90, 168).a, 0.0, 0.001);

    // Red keeps its own half, and its own pixels within it.
    CHECK_NEAR(fillAt(fill, 70, 70).r, 1.0, 0.02);
    CHECK_NEAR(fillAt(fill, 130, 100).r, 1.0, 0.02);

    // And on the transparent mark itself there is nothing, which is what makes
    // it usable to take colour back off a spot the solver got wrong: over the
    // fill, never under it.
    CHECK_NEAR(fillAt(fill, 130, 150).a, 0.0, 0.001);
}

// The case the whole encoding exists for: scribbling "nothing" over a region
// that has already been filled has to leave a hole, not be hidden by the fill.
void aTransparentScribbleShowsThroughAFillThatDisagrees() {
    TEST("a transparent mark punches through a fill that says otherwise");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    ctgStroke(f.doc, f.track, f.image, f.colour, 90, 100, 170, 100, 10.0f, 1, 0, 0, false);

    // Small enough that the solver keeps the region red around it.
    ctgStroke(f.doc, f.track, f.image, f.colour, 125, 125, 125, 125, 2.0f,
              kTransparentScribble.r, kTransparentScribble.g, kTransparentScribble.b, false);

    CtgSettings coarse;
    coarse.downscale = 8;  // the mark is finer than the grid, so it is not a seed
    const CtgFill& fill = ctgFill(f.doc, f.track, f.image, f.colour, coarse);

    CHECK_EQ(fill.colours, 1);                            // the solver never saw it
    CHECK_NEAR(fillAt(fill, 90, 160).r, 1.0, 0.02);       // the region is red
    CHECK_NEAR(fillAt(fill, 125, 125).a, 0.0, 0.001);     // and there is a hole in it
}

// --- marks that move ----------------------------------------------------
//
// Part 2 of docs/scribbles-through-time.md: one translation for the whole
// drawing, measured from the line art rather than stored anywhere, applied when
// a mark is read on a drawing it was not made on.

void aCarriedMarkFollowsTheDrawing() {
    TEST("a carried mark moves with the drawing it was cut against");
    Sequence s(2);
    s.followTheMotion();

    // The same shape, a long way across on the second drawing. Carried
    // unchanged, the mark is out on bare paper there -- that is the case above.
    s.box(0, 60, 60, 200, 180, 120, 140);
    s.box(1, 320, 60, 460, 180, 380, 400);
    s.stroke(0, s.colour, 90, 100, 170, 100, 10.0f, 1.0f, 0.0f, 0.0f);

    const CtgFill& own = s.fillOf(0);
    CHECK(own.valid);
    CHECK(!own.inherited);
    CHECK(own.carried_by.isZero());  // its own marks are never moved

    const CtgFill& carried = s.fillOf(1);
    CHECK(carried.valid);
    CHECK(carried.inherited);

    // Found the shift, near enough. It is measured on a coarse grid because a
    // mark does not need to be placed accurately -- what it needs is most of
    // its pixels in the right region.
    CHECK(std::abs(carried.carried_by.x - 260) <= 12);
    CHECK(std::abs(carried.carried_by.y) <= 12);

    // And the shape is filled, where carrying it unchanged fills nothing.
    CHECK_NEAR(fillAt(carried, 390, 160).r, 1.0, 0.02);
    CHECK(carried.spread > 5.0f);
}

// The mark's own pixels have to move with it. They are painted over the fill --
// a mark wins the pixels it covers, whatever the solver decided -- so a seed
// read in one place and an override painted in another would leave a stripe of
// colour across a region with every reason to be a different one.
void theMarkItselfMovesWithItsSeed() {
    TEST("a mark that has moved is drawn where it moved to, not where it was");
    Sequence s(2);
    s.followTheMotion();

    s.box(0, 60, 60, 200, 180, 120, 140);
    s.box(1, 320, 60, 460, 180, 380, 400);
    s.stroke(0, s.colour, 90, 100, 170, 100, 10.0f, 1.0f, 0.0f, 0.0f);

    const CtgFill& carried = s.fillOf(1);
    CHECK(carried.valid);

    // Where the mark was drawn there is now nothing: that part of the picture
    // is outside every shape on this drawing, and the mark is not there any
    // more to claim it.
    CHECK_NEAR(fillAt(carried, 130, 100).a, 0.0, 0.001);
}

void aMarkOnItsOwnDrawingIsNeverMoved() {
    TEST("a mark is only ever moved on a drawing it was not made on");
    Sequence s(2);
    s.followTheMotion();

    s.box(0, 60, 60, 200, 180, 120, 140);
    s.box(1, 320, 60, 460, 180, 380, 400);
    s.stroke(0, s.colour, 90, 100, 170, 100, 10.0f, 1.0f, 0.0f, 0.0f);
    // Its own mark on the second drawing, in the shape where it belongs.
    s.stroke(1, s.colour, 350, 100, 430, 100, 10.0f, 0.0f, 0.0f, 1.0f);

    const CtgFill& second = s.fillOf(1);
    CHECK(second.valid);
    CHECK(!second.inherited);
    CHECK(second.carried_by.isZero());
    CHECK_NEAR(fillAt(second, 390, 160).b, 1.0, 0.02);
}

void movingMarksCanBeTurnedOff() {
    TEST("leaving marks where they were drawn is still a choice");
    Sequence s(2);  // the fixture leaves them, which is what is being checked

    s.box(0, 60, 60, 200, 180, 120, 140);
    s.box(1, 320, 60, 460, 180, 380, 400);
    s.stroke(0, s.colour, 90, 100, 170, 100, 10.0f, 1.0f, 0.0f, 0.0f);

    const CtgFill& carried = s.fillOf(1);
    CHECK(carried.valid);
    CHECK(carried.carried_by.isZero());
    CHECK(carried.spread < 1.05f);  // and it lands on nothing, which is the point
}

// Redrawing the line art of the drawing a mark came from changes how far the
// mark has to move. Nothing else in the fill's key mentions that drawing, so
// without this the fill would go on showing a shift measured against ink that
// is no longer there.
void redrawingTheOriginMovesTheMarkAgain() {
    TEST("redrawing the drawing a mark came from re-measures the shift");
    Sequence s(2);
    s.followTheMotion();

    s.box(0, 60, 60, 200, 180, 120, 140);
    s.box(1, 320, 60, 460, 180, 380, 400);
    s.stroke(0, s.colour, 90, 100, 170, 100, 10.0f, 1.0f, 0.0f, 0.0f);

    const std::uint64_t before = s.fillOf(1).inputs;
    s.stroke(0, s.ink, 62, 62, 198, 62, 2.5f, 0.0f, 0.0f, 0.0f);
    CHECK(s.fillOf(1).inputs != before);
}

void theShiftIsMeasuredFromTheInkAlone() {
    TEST("the shift is what the ink moved, found without any marks at all");
    Sequence s(2);
    s.box(0, 60, 60, 200, 180, 120, 140);
    s.box(1, 200, 130, 340, 250, 260, 280);

    const Cel* first = s.doc.celAt(s.track, s.at(0), s.ink);
    const Cel* second = s.doc.celAt(s.track, s.at(1), s.ink);
    CHECK(first != nullptr);
    CHECK(second != nullptr);
    if (!first || !second) return;

    const CtgShift shift = estimateCtgShift({first->tiles()}, {second->tiles()},
                                            {0, 0, 640, 360});
    CHECK(std::abs(shift.x - 140) <= 12);
    CHECK(std::abs(shift.y - 70) <= 12);

    // Nothing to match against is not an error and not a guess: it is zero,
    // which is the answer that carries the mark unchanged.
    CHECK(estimateCtgShift({}, {second->tiles()}, {0, 0, 640, 360}).isZero());
    CHECK(estimateCtgShift({TileGrid{}}, {second->tiles()}, {0, 0, 640, 360}).isZero());
}

// Reported: the fill follows the drawing, and everything that reports on the
// marks says they did not. Three symptoms, and the test asks each of them
// separately because they need not have had one cause.
// Reported, with a project: five circles drawn freehand in the same place, and
// the marks moved hundreds of pixels. Two drawings of the same thing never
// coincide exactly, and the score has to survive that.
//
// The first version scored a shift by how much the two drawings *differed*,
// which on line art has a fatal minimum: a drawing is nearly all bare paper, so
// a wrong alignment is charged twice -- for the ink it puts where there is none
// and for the ink it leaves uncovered -- while sliding the drawing clean off the
// edge is charged once. "Disappear entirely" beat "line them up", and the search
// answered with the far corner of its own search window.
void aShapeRedrawnInPlaceHasNotMoved() {
    TEST("a shape redrawn in the same place is not reported as having moved");
    Sequence s(2);
    s.followTheMotion();

    // The same box twice, the second a little smaller and a little off, as a
    // hand draws it. Nothing has moved.
    s.box(0, 100, 100, 400, 400, 240, 260);
    s.box(1, 108, 112, 388, 384, 240, 260);
    s.stroke(0, s.colour, 200, 250, 300, 250, 12.0f, 1.0f, 0.0f, 0.0f);

    const CtgFill& carried = s.fillOf(1);
    CHECK(carried.valid);
    CHECK(carried.inherited);

    // Within a small part of the shape. It is not zero and should not be -- the
    // drawing did shift a little -- but a mark in the middle stays in the
    // middle.
    CHECK(std::abs(carried.carried_by.x) < 60);
    CHECK(std::abs(carried.carried_by.y) < 60);
    CHECK_NEAR(fillAt(carried, 350, 350).r, 1.0, 0.02);
    CHECK(carried.spread > 5.0f);
}

void whatIsShownAgreesWithWhatWasSolved() {
    TEST("everything that reports on a moved mark agrees with the fill");
    Sequence s(2);
    s.followTheMotion();

    s.box(0, 60, 60, 200, 180, 120, 140);
    s.box(1, 320, 60, 460, 180, 380, 400);
    s.stroke(0, s.colour, 90, 100, 170, 100, 10.0f, 1.0f, 0.0f, 0.0f);

    // The fill is right: this is the part that works.
    const CtgFill& carried = s.fillOf(1);
    CHECK(carried.valid);
    CHECK_NEAR(fillAt(carried, 390, 160).r, 1.0, 0.02);

    // 1. The Marks column, which shows the scribbles instead of the fill. It
    //    has to show them where they are being used, or it says the fill is
    //    built from marks that are not the ones it was built from.
    const CtgShift moved = s.doc.ctgShiftAt(s.at(1), s.colour);
    CHECK(std::abs(moved.x - 260) <= 12);

    Compositor compositor;
    Framebuffer frame;
    {
        Layer marks = *s.doc.scene().findTrack(s.track)->findLayer(s.colour);
        marks.show_scribbles = true;
        s.doc.updateLayer(s.track, s.colour, marks);
    }
    compositor.compositeLayers(s.doc, s.track, s.at(1), {s.colour}, {0, 0, 500, 250}, frame);
    // Where the mark is now, and not where it was drawn.
    CHECK(frame.pixel(390, 100).a > 0.5f);
    CHECK_NEAR(frame.pixel(130, 100).a, 0.0, 0.001);

    // 2. Taking the drawing over. The first mark made on a drawing that is
    //    carrying copies what it was showing and edits the copy -- and what it
    //    was showing is the moved marks, not the ones as drawn.
    {
        Layer marks = *s.doc.scene().findTrack(s.track)->findLayer(s.colour);
        marks.show_scribbles = false;
        s.doc.updateLayer(s.track, s.colour, marks);
    }
    s.stroke(1, s.colour, 340, 170, 350, 170, 4.0f, 1.0f, 0.0f, 0.0f);

    const Cel* own = s.doc.celAt(s.track, s.at(1), s.colour);
    CHECK(own != nullptr);
    if (own) {
        CHECK(own->pixel(390, 100).a > 0.5f);
        CHECK_NEAR(own->pixel(130, 100).a, 0.0, 0.001);
    }

    // And the fill it produces is the fill it had a moment ago.
    const CtgFill& after = s.fillOf(1);
    CHECK(after.valid);
    CHECK(!after.inherited);
    CHECK_NEAR(fillAt(after, 390, 160).r, 1.0, 0.02);

    // 3. And now that the drawing owns its marks, nothing may move them again.
    //    A stroke in progress is shown through the same path that draws the
    //    Marks column, so a shift left over from before the takeover put the
    //    pen's own line as far from the pen as the drawing had moved.
    CHECK(s.doc.ctgShiftAt(s.at(1), s.colour).isZero());
    {
        Layer marks = *s.doc.scene().findTrack(s.track)->findLayer(s.colour);
        marks.show_scribbles = true;
        s.doc.updateLayer(s.track, s.colour, marks);
    }
    Framebuffer own_marks;
    compositor.compositeLayers(s.doc, s.track, s.at(1), {s.colour}, {0, 0, 700, 250},
                               own_marks);
    // The dab made at 340..350, where it was made.
    CHECK(own_marks.pixel(345, 170).a > 0.5f);
    CHECK_NEAR(own_marks.pixel(605, 170).a, 0.0, 0.001);

    // Taking the drawing over forgets the shift, and a solve that was already
    // running for the state before it can still land afterwards and put one
    // back. Own marks are where they are whatever the store says.
    s.doc.ctgShifts()[CtgKey{s.at(1), s.colour}] = CtgShift{260, 0};
    Framebuffer late;
    compositor.compositeLayers(s.doc, s.track, s.at(1), {s.colour}, {0, 0, 700, 250}, late);
    CHECK(late.pixel(345, 170).a > 0.5f);
    CHECK_NEAR(late.pixel(605, 170).a, 0.0, 0.001);
}

// --- the solve, lifted off the document ---------------------------------
//
// A max-flow is the expensive thing this program does, and it may not run on
// the thread the interface is on. What makes that possible is that a solve
// reads the document exactly once, into a job -- so these tests are about the
// job being a complete and independent description of one solve, which is the
// property the whole of the background solving rests on.

void aLiftedSolveAgreesWithTheDocument() {
    TEST("a solve lifted off the document gives the document's answer");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);
    f.stroke(f.colour, 20, 20, 240, 20, 6.0f, 0.0f, 0.0f, 1.0f);

    const CtgFill lifted = solveCtgJob(ctgJobFor(f.doc, f.track, f.image, f.colour), true);
    const CtgFill& direct = ctgFill(f.doc, f.track, f.image, f.colour);

    CHECK(lifted.valid);
    CHECK_EQ(lifted.colours, direct.colours);
    CHECK_EQ(lifted.step, direct.step);
    CHECK_EQ(lifted.inputs, direct.inputs);
    CHECK_NEAR(fillAt(lifted, 130, 160).r, fillAt(direct, 130, 160).r, 0.001);
    CHECK_NEAR(fillAt(lifted, 30, 200).b, fillAt(direct, 30, 200).b, 0.001);
}

void takingAJobCopiesNoPixels() {
    TEST("lifting a solve off the document copies handles and not pixels");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    const CtgJob job = ctgJobFor(f.doc, f.track, f.image, f.colour);
    const Cel* marks = f.doc.celAt(f.track, f.image, f.colour);
    CHECK(marks != nullptr);

    // The same tiles, not copies of them. This is what makes taking a job cheap
    // enough to do on every stroke: a tile is immutable once shared, so the
    // copy is the shared_ptr and the pixels stay where they are.
    int shared = 0;
    for (const TileCoord& coord : marks->tiles().coords()) {
        if (job.scribbles.find(coord).get() == marks->tiles().find(coord).get()) ++shared;
    }
    CHECK_EQ(shared, static_cast<int>(marks->tiles().tileCount()));
    CHECK(shared > 0);
}

void aLiftedSolveIsNotChangedByLaterEdits() {
    TEST("editing the document does not change a solve already lifted off it");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    const CtgJob job = ctgJobFor(f.doc, f.track, f.image, f.colour);

    // Everything the job read, changed underneath it.
    f.stroke(f.colour, 110, 160, 160, 160, 6.0f, 0.0f, 0.0f, 1.0f);
    f.stroke(f.ink, 60, 120, 200, 120, 2.5f, 0.0f, 0.0f, 0.0f);

    const CtgFill lifted = solveCtgJob(job, true);
    const CtgFill& now = ctgFill(f.doc, f.track, f.image, f.colour);

    // The job still describes the drawing it was taken from: one colour, and
    // red where the document now says blue.
    CHECK_EQ(lifted.colours, 1);
    CHECK_NEAR(fillAt(lifted, 130, 160).r, 1.0, 0.02);
    CHECK_EQ(now.colours, 2);
    CHECK_NEAR(fillAt(now, 130, 160).b, 1.0, 0.02);
}

void aLiftedSolveRunsWhileTheDocumentIsDrawnOn() {
    TEST("a lifted solve runs on another thread while the document is drawn on");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    const CtgJob job = ctgJobFor(f.doc, f.track, f.image, f.colour);

    std::atomic<bool> done{false};
    CtgFill solved;
    std::thread worker([&] {
        solved = solveCtgJob(job, true);
        done.store(true);
    });

    // Kept drawing on the very cels the job is reading. Copy-on-write is what
    // makes this safe: the writer sees a tile it does not own alone and clones
    // it, so the solve goes on reading the tiles it was handed.
    for (int i = 0; i < 400 && !done.load(); ++i) {
        f.stroke(f.ink, 62.0f, 62.0f + static_cast<float>(i % 100), 198.0f,
                 62.0f + static_cast<float>(i % 100), 1.5f, 0.0f, 0.0f, 0.0f);
    }
    worker.join();

    CHECK(solved.valid);
    CHECK_EQ(solved.colours, 1);
    CHECK_NEAR(fillAt(solved, 130, 160).r, 1.0, 0.02);
}

void anAbandonedSolveReturnsNothing() {
    TEST("a solve told to give up returns nothing rather than half an answer");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    std::atomic<bool> abandon{true};
    const CtgFill given_up =
        solveCtgJob(ctgJobFor(f.doc, f.track, f.image, f.colour), true, &abandon);

    // Not a partial fill, because a partial fill is indistinguishable from a
    // finished one and would be cached, drawn and believed.
    CHECK(!given_up.valid);
    CHECK_EQ(given_up.tiles.tileCount(), std::size_t{0});
}

void anUnboundedSolveIsFinerThanABoundedOne() {
    TEST("a solve with no budget is solved at full resolution");
    Fixture f;
    // Big enough that the interactive budget has to coarsen it.
    f.doc.setCanvasSize(1600, 1200);
    f.drawGappedBox(f.ink, 100, 100, 1500, 1100, 700, 900);
    f.stroke(f.colour, 400, 600, 1200, 600, 20.0f, 1.0f, 0.0f, 0.0f);

    const CtgFill capped =
        solveCtgJob(ctgJobFor(f.doc, f.track, f.image, f.colour), false);
    const CtgFill whole = solveCtgJob(
        ctgJobFor(f.doc, f.track, f.image, f.colour, CtgSettings{}, /*budget=*/0), false);

    CHECK(capped.step > 1);   // where the interface waits, quality is what pays
    CHECK_EQ(whole.step, 1);  // and where it does not, nothing pays
    CHECK(whole.spread > 5.0f);
}

// A source layer is a source because it is listed as one, not because it is
// being looked at. Hiding the line art to see the colours underneath used to
// take the barrier away with it -- and only at the next re-solve, since a
// layer's visibility is not part of what a fill is keyed on, so the fill went
// wrong at some later moment with nothing connecting the two.
void ahiddenSourceIsStillABarrier() {
    TEST("hiding a source layer does not take the barrier away with it");
    Fixture f;
    f.drawGappedBox(f.ink, 60, 60, 200, 180, 120, 140);
    f.stroke(f.colour, 100, 110, 150, 110, 6.0f, 1.0f, 0.0f, 0.0f);

    const CtgFill seen = solveCtgJob(ctgJobFor(f.doc, f.track, f.image, f.colour), true);

    Layer ink = *f.doc.scene().findTrack(f.track)->findLayer(f.ink);
    ink.visible = false;
    f.doc.updateLayer(f.track, f.ink, ink);

    const CtgFill hidden = solveCtgJob(ctgJobFor(f.doc, f.track, f.image, f.colour), true);
    CHECK_NEAR(hidden.spread, seen.spread, 0.001);
    CHECK_NEAR(fillAt(hidden, 130, 160).r, fillAt(seen, 130, 160).r, 0.001);
    CHECK_NEAR(fillAt(hidden, 30, 200).a, fillAt(seen, 30, 200).a, 0.001);
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
    aScribbleWinsInThePixelsItCovers();
    aMarkFinerThanTheSolveGridStillShows();

    aDrawingWithNoScribblesInheritsTheEarlierOnes();
    editingADrawingDetachesItAndTheOnesAfterFollow();
    clearingAnOverrideReturnsItToInheriting();
    deletingTheSourceLeavesLaterDrawingsInheritingFromWhatPrecedes();
    reorderingChangesInheritanceWithoutTouchingACel();
    aReorderInvalidatesTheFillEvenWhenNoRevisionMoves();
    inheritanceRunsForwardOnly();
    inheritanceCrossesAHold();
    onlyAColourLayerInherits();
    addingAColourLayerToALongTrackAllocatesNothing();
    solvingTwoDrawingsAndComingBackDoesNotResolve();

    erasingAScribbleUndoesWhatItDid();
    erasingAStrayScribbleUndoesWhatItDid();

    aMarkOutsideAShapeNearItsWall();
    carryingScribblesCanBeSwitchedOff();
    carryingCanRunBackwards();
    carryingCanRunBothWays();
    aCarriedMarkThatLandsWrongWinsNothingButItself();
    aCarriedMarkThatLandsRightWinsARegion();
    confidenceIsMeasuredAgainstTheSolveAndNotTheFill();

    aCtgStrokeWritesLabelsAndNotPaint();
    theTransparentScribbleSurvivesTheHalfFloat();
    aTransparentScribbleTakesColourAway();
    aTransparentScribbleShowsThroughAFillThatDisagrees();

    aCarriedMarkFollowsTheDrawing();
    theMarkItselfMovesWithItsSeed();
    aMarkOnItsOwnDrawingIsNeverMoved();
    movingMarksCanBeTurnedOff();
    redrawingTheOriginMovesTheMarkAgain();
    theShiftIsMeasuredFromTheInkAlone();
    aShapeRedrawnInPlaceHasNotMoved();
    whatIsShownAgreesWithWhatWasSolved();

    aLiftedSolveAgreesWithTheDocument();
    takingAJobCopiesNoPixels();
    aLiftedSolveIsNotChangedByLaterEdits();
    aLiftedSolveRunsWhileTheDocumentIsDrawnOn();
    anAbandonedSolveReturnsNothing();
    anUnboundedSolveIsFinerThanABoundedOne();
    ahiddenSourceIsStillABarrier();
    return testing::summarise("ctg");
}
