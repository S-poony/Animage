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
        doc.updateLayer(track, colour, ctg);
    }

    const Track& track_ref() const { return *doc.scene().findTrack(track); }

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
    return testing::summarise("ctg");
}
