// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "brush.h"
#include "half.h"
#include "compositor.h"
#include "testing.h"

using namespace animage;

namespace {

struct Fixture {
    Document doc;
    TrackId track;
    LayerId layer;
    ImageId image;

    Fixture() {
        track = doc.addTrack("main");
        layer = doc.addLayer(track, "rough");
        image = doc.insertImage(track, 0);
    }
    const Track& tl() const { return *doc.scene().findTrack(track); }
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

float alphaAt(const Document& doc, TrackId track, ImageId image, LayerId layer, int x,
              int y) {
    const Cel* cel = doc.celAt(track, image, layer);
    return cel ? cel->pixel(x, y).a : 0.0f;
}

void strokeLaysDownInk() {
    TEST("a stroke lays down ink along its path");
    Fixture f;
    Brush brush(opaqueBlack());

    {
        ScopedCommand command(f.doc, "Stroke");
        brush.begin(f.doc, f.track, f.image, f.layer, {100.0f, 100.0f, 1.0f});
        for (int i = 1; i <= 40; ++i) {
            brush.extend({100.0f + static_cast<float>(i) * 2.0f, 100.0f, 1.0f});
        }
        brush.end();
    }

    CHECK(brush.dabCount() > 1);
    CHECK_NEAR(alphaAt(f.doc, f.track, f.image, f.layer, 100, 100), 1.0, 1e-2);
    CHECK_NEAR(alphaAt(f.doc, f.track, f.image, f.layer, 140, 100), 1.0, 1e-2);
    CHECK_NEAR(alphaAt(f.doc, f.track, f.image, f.layer, 180, 100), 1.0, 1e-2);

    // Well clear of the stroke, nothing.
    CHECK_NEAR(alphaAt(f.doc, f.track, f.image, f.layer, 100, 400), 0.0, 1e-3);
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
        brush.begin(f.doc, f.track, f.image, f.layer, {50.0f, 50.0f, 1.0f});
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
            brush.begin(f.doc, f.track, f.image, f.layer, {200.0f, 200.0f, pressure});
            brush.extend({260.0f, 200.0f, pressure});
            brush.end();
        }
        int width = 0;
        for (int y = 150; y < 250; ++y) {
            if (alphaAt(f.doc, f.track, f.image, f.layer, 230, y) > 0.5f) ++width;
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
        brush.begin(f.doc, f.track, f.image, f.layer, {300.0f, 300.0f, 1.0f});
        brush.extend({360.0f, 300.0f, 1.0f});
        brush.end();
    }
    CHECK_NEAR(alphaAt(f.doc, f.track, f.image, f.layer, 330, 300), 1.0, 1e-2);

    {
        ScopedCommand command(f.doc, "Erase");
        BrushSettings settings = opaqueBlack();
        settings.erase = true;
        settings.radius = 14.0f;
        Brush eraser(settings);
        eraser.begin(f.doc, f.track, f.image, f.layer, {300.0f, 300.0f, 1.0f});
        eraser.extend({360.0f, 300.0f, 1.0f});
        eraser.end();
    }
    CHECK_NEAR(alphaAt(f.doc, f.track, f.image, f.layer, 330, 300), 0.0, 1e-2);

    // And erasing is undoable like anything else.
    CHECK(f.doc.undo());
    CHECK_NEAR(alphaAt(f.doc, f.track, f.image, f.layer, 330, 300), 1.0, 1e-2);
}

void strokeIsOneUndoStep() {
    TEST("a whole stroke is a single undo step");
    Fixture f;
    const std::size_t before = f.doc.undoDepth();

    {
        ScopedCommand command(f.doc, "Stroke");
        Brush brush(opaqueBlack());
        brush.begin(f.doc, f.track, f.image, f.layer, {10.0f, 10.0f, 1.0f});
        for (int i = 1; i <= 200; ++i) {
            brush.extend({10.0f + static_cast<float>(i), 10.0f + static_cast<float>(i), 1.0f});
        }
        brush.end();
    }

    CHECK_EQ(f.doc.undoDepth(), before + 1);
    CHECK(f.doc.totalTileCount() > 1);  // the stroke crossed several tiles

    CHECK(f.doc.undo());
    CHECK_NEAR(alphaAt(f.doc, f.track, f.image, f.layer, 100, 100), 0.0, 1e-3);
    CHECK(f.doc.redo());
    CHECK_NEAR(alphaAt(f.doc, f.track, f.image, f.layer, 100, 100), 1.0, 1e-2);
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
        brush.begin(f.doc, f.track, f.image, f.layer, {64.0f, 64.0f, 1.0f});
        brush.end();
    }
    CHECK_EQ(f.doc.totalTileCount(), std::size_t{1});
}

void compositorRespectsOrderAndOpacity() {
    TEST("the compositor respects layer order, opacity and visibility");
    Fixture f;
    const LayerId top = f.doc.addLayer(f.track, "top", 0);

    // Bottom layer red, top layer green, both opaque over the same spot.
    {
        ScopedCommand command(f.doc, "Red");
        BrushSettings settings = opaqueBlack();
        settings.r = 1.0f;
        Brush brush(settings);
        brush.begin(f.doc, f.track, f.image, f.layer, {500.0f, 500.0f, 1.0f});
        brush.end();
    }
    {
        ScopedCommand command(f.doc, "Green");
        BrushSettings settings = opaqueBlack();
        settings.g = 1.0f;
        Brush brush(settings);
        brush.begin(f.doc, f.track, f.image, top, {500.0f, 500.0f, 1.0f});
        brush.end();
    }

    Compositor compositor;
    Framebuffer frame;
    const PixelRect region{495, 495, 10, 10};

    compositor.composite(f.doc, f.track, f.image, region, frame);
    Rgba centre = frame.pixel(5, 5);
    CHECK_NEAR(centre.a, 1.0, 1e-2);
    CHECK_NEAR(centre.g, 1.0, 1e-2);  // green is on top
    CHECK_NEAR(centre.r, 0.0, 1e-2);

    // Hide the top layer and the red beneath shows through.
    Layer hidden = *f.tl().findLayer(top);
    hidden.visible = false;
    f.doc.updateLayer(f.track, top, hidden);

    compositor.composite(f.doc, f.track, f.image, region, frame);
    centre = frame.pixel(5, 5);
    CHECK_NEAR(centre.r, 1.0, 1e-2);
    CHECK_NEAR(centre.g, 0.0, 1e-2);

    // Half opacity on the only visible layer halves its contribution.
    Layer faded = *f.tl().findLayer(f.layer);
    faded.opacity = 0.5f;
    f.doc.updateLayer(f.track, f.layer, faded);

    compositor.composite(f.doc, f.track, f.image, region, frame);
    centre = frame.pixel(5, 5);
    CHECK_NEAR(centre.a, 0.5, 1e-2);
    CHECK_NEAR(centre.r, 0.5, 1e-2);
}

void compositorHandlesEmptyAndBounds() {
    TEST("compositing an empty image gives transparency");
    Fixture f;
    Compositor compositor;
    Framebuffer frame;

    compositor.composite(f.doc, f.track, f.image, {0, 0, 16, 16}, frame);
    CHECK_EQ(frame.width(), 16);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) CHECK_NEAR(frame.pixel(x, y).a, 0.0, 1e-6);
    }

    const PixelRect empty = imageBounds(f.doc, f.track, f.image);
    CHECK(empty.isEmpty());

    {
        ScopedCommand command(f.doc, "Dot");
        Brush brush(opaqueBlack());
        brush.begin(f.doc, f.track, f.image, f.layer, {200.0f, 300.0f, 1.0f});
        brush.end();
    }
    const PixelRect bounds = imageBounds(f.doc, f.track, f.image);
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
        brush.begin(f.doc, f.track, f.image, f.layer, {-200.0f, -150.0f, 1.0f});
        brush.end();
    }

    Compositor compositor;
    Framebuffer frame;
    compositor.composite(f.doc, f.track, f.image, {-205, -155, 10, 10}, frame);
    CHECK_NEAR(frame.pixel(5, 5).a, 1.0, 1e-2);
    CHECK_NEAR(frame.pixel(5, 5).r, 1.0, 1e-2);

    // And the reducing path, which indexes a grid of its own. Every division in
    // it has to round towards negative infinity or the entry left of the origin
    // is named by the one to its right, and the picture tears along x = 0 --
    // quietly, since nothing else in a test document is over there.
    const SampleStep half = SampleStep::fromRatio(2.0);
    const PixelRect around = snapToSampleGrid(half, {-220, -170, 40, 40});
    Framebuffer reduced;
    compositor.composite(f.doc, f.track, f.image, around, reduced, half);
    CHECK_EQ(reduced.width(), 20);

    // The dot's centre, found through the grid rather than counted off by hand.
    const int column = static_cast<int>(half.entryAt(-200) - half.entryAt(around.x));
    const int row = static_cast<int>(half.entryAt(-150) - half.entryAt(around.y));
    CHECK_NEAR(reduced.pixel(column, row).a, 1.0, 1e-2);
    CHECK_NEAR(reduced.pixel(column, row).r, 1.0, 1e-2);
    // And nothing has leaked into the far corner.
    CHECK_NEAR(reduced.pixel(0, 0).a, 0.0, 1e-3);
}

// Zoomed out, the compositor reduces a block of image pixels to each entry so
// the buffer tracks the size of the window rather than the visible image area.
// The reducing path is separate code from the run-length one, and what it must
// produce is the *average* of the block: taking one pixel of it and discarding
// the rest is what made thin lines shimmer when zoomed out (issue #11).
void sampledCompositingAveragesTheBlockItStandsFor() {
    TEST("sampled compositing averages the block it stands for");
    Fixture f;
    BrushSettings settings = opaqueBlack();
    settings.radius = 30.0f;
    settings.r = 1.0f;
    Brush brush(settings);

    {
        ScopedCommand command(f.doc, "Stroke");
        brush.begin(f.doc, f.track, f.image, f.layer, {100.0f, 100.0f, 1.0f});
        brush.extend({300.0f, 260.0f, 1.0f});
        brush.end();
    }

    const PixelRect region{0, 0, 400, 400};
    Compositor compositor;
    Framebuffer full;
    Framebuffer sampled;
    compositor.composite(f.doc, f.track, f.image, region, full);
    // Two image pixels an entry -- 50% zoom, which is where the issue measured
    // the largest quality loss and where the block filter is exact: the entry
    // boundaries fall on pixel boundaries, so there is nothing to weight and
    // the answer is the plain mean of the four.
    compositor.composite(f.doc, f.track, f.image, region, sampled, SampleStep::fromRatio(2.0));

    CHECK_EQ(full.width(), 400);
    CHECK_EQ(sampled.width(), 200);
    CHECK_EQ(sampled.height(), 200);

    // A point sample would pass an equality against full.pixel(2x, 2y), and
    // that is exactly what this is here to rule out.
    double worst = 0.0;
    for (int y = 0; y < sampled.height(); ++y) {
        for (int x = 0; x < sampled.width(); ++x) {
            Rgba mean{};
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const Rgba p = full.pixel(x * 2 + dx, y * 2 + dy);
                    mean.r += p.r / 4.0f;
                    mean.g += p.g / 4.0f;
                    mean.b += p.b / 4.0f;
                    mean.a += p.a / 4.0f;
                }
            }
            const Rgba got = sampled.pixel(x, y);
            CHECK_NEAR(got.a, mean.a, 1e-4);
            CHECK_NEAR(got.r, mean.r, 1e-4);
            worst = std::max<double>(worst, std::abs(got.a - full.pixel(x * 2, y * 2).a));
        }
    }

    // And it must actually differ from the point sample somewhere, or the
    // stroke was too smooth for the test to be measuring anything.
    CHECK(worst > 0.05);
}

// An entry is the same entry whatever rectangle it was asked for. The reduction
// used to widen its first sample block back to the edge of the region and clip
// its last one to the far edge, and normalise by the weight that produced -- so
// the entries around the boundary of a rectangle came out different from the
// same entries inside a larger one.
//
// That is not an academic difference, and it is issue #64. The canvas composites
// one dirty rectangle per dab while the pen is down and the whole cache when it
// lifts, so every dab laid a line of wrong entries around itself: dragging
// the eraser past a stroke without touching it chewed the stroke until you let
// go, worst just under 100% zoom and gone at 100% where nothing is reduced. A
// pan left the same line along every strip it exposed and nothing wiped that.
//
// Asserted as exact agreement rather than a tolerance, because that is what the
// property is. The strokes run diagonally so that the bands cut ink at every
// angle, and the steps are the awkward ones: an integer ratio has nothing to
// split and would pass whatever the weighting did.
void aReducedEntryDoesNotDependOnTheRegionAskedFor() {
    TEST("a reduced entry does not depend on the region it was asked for");
    Fixture f;
    BrushSettings settings = opaqueBlack();
    settings.radius = 5.0f;
    settings.r = 1.0f;
    Brush brush(settings);

    // Twice over, and the second one is left of and above the origin. Every
    // division in this path has to round towards negative infinity, and a
    // rectangle that never crosses zero would not notice one that does not --
    // which is the same reason `compositorWorksLeftOfTheOrigin` exists.
    for (const PixelRect& at : {PixelRect{40, 40, 0, 0}, PixelRect{-300, -280, 0, 0}}) {
        ScopedCommand command(f.doc, "Strokes");
        for (int line = 0; line < 3; ++line) {
            const float x0 = static_cast<float>(at.x);
            const float y0 = static_cast<float>(at.y) + static_cast<float>(line) * 90.0f;
            brush.begin(f.doc, f.track, f.image, f.layer, {x0, y0, 1.0f});
            for (int i = 1; i <= 260; ++i) {
                brush.extend({x0 + static_cast<float>(i),
                              y0 + static_cast<float>(i) * 0.55f, 1.0f});
            }
            brush.end();
        }
    }

    Compositor compositor;
    for (double ratio : {1.005, 1.43, 2.5, 3.03, 5.0, 20.0}) {
        const SampleStep step = SampleStep::fromRatio(ratio);
        for (const PixelRect& asked :
             {PixelRect{0, 0, 360, 340}, PixelRect{-340, -320, 360, 340}}) {
            const PixelRect whole = snapToSampleGrid(step, asked);
            Framebuffer full;
            compositor.composite(f.doc, f.track, f.image, whole, full, step);

            const long long whole_x = step.entryAt(whole.x);
            const long long whole_y = step.entryAt(whole.y);

            // Bands the size and shape of a dab's dirty rectangle, walked across
            // the ink at a stride that is not a multiple of anything.
            for (int y = whole.y; y < whole.y + whole.height; y += 37) {
                for (int x = whole.x; x < whole.x + whole.width; x += 29) {
                    const PixelRect band =
                        intersect(snapToSampleGrid(step, PixelRect{x, y, 40, 40}), whole);
                    if (band.isEmpty()) continue;
                    Framebuffer part;
                    compositor.composite(f.doc, f.track, f.image, band, part, step);

                    const int column_base = static_cast<int>(step.entryAt(band.x) - whole_x);
                    const int row_base = static_cast<int>(step.entryAt(band.y) - whole_y);
                    for (int j = 0; j < part.height(); ++j) {
                        for (int i = 0; i < part.width(); ++i) {
                            const Rgba got = part.pixel(i, j);
                            const Rgba want = full.pixel(column_base + i, row_base + j);
                            CHECK_NEAR(got.a, want.a, 1e-5);
                            CHECK_NEAR(got.r, want.r, 1e-5);
                        }
                    }
                }
            }
        }
    }
}

// Between one and two image pixels an entry -- the band from 100% zoom down to
// 50% -- an entry boundary lands inside a pixel rather than between two, and
// what the pixel contributes has to be split in proportion. Rounding the
// boundary to the nearest pixel instead gives some entries one pixel and some
// two, and the alternation between them measured worse than the point sampling
// it replaced: RMS 10.7 against a curve drawn at display size, where the split
// gives 2.4. See bench_zoom.
void aFractionalStepSplitsThePixelsItLandsInside() {
    TEST("a fractional step splits the pixel an entry boundary lands inside");
    Fixture f;
    BrushSettings settings = opaqueBlack();
    settings.radius = 24.0f;
    settings.r = 1.0f;
    Brush brush(settings);

    {
        ScopedCommand command(f.doc, "Stroke");
        brush.begin(f.doc, f.track, f.image, f.layer, {40.0f, 40.0f, 1.0f});
        brush.extend({220.0f, 190.0f, 1.0f});
        brush.end();
    }

    // Three image pixels for every two entries, so every second boundary falls
    // half way through a pixel.
    const PixelRect region{0, 0, 240, 240};
    Compositor compositor;
    Framebuffer full;
    Framebuffer reduced;
    compositor.composite(f.doc, f.track, f.image, region, full);
    compositor.composite(f.doc, f.track, f.image, region, reduced, SampleStep::fromRatio(1.5));

    CHECK_EQ(reduced.width(), 160);
    CHECK_EQ(reduced.height(), 160);

    // Entry 1 covers image x in [1.5, 3.0): half of pixel 1 and all of pixel 2.
    // Same in y, so it is a weighted mean of a 2x2 corner of the drawing.
    const auto weightedMean = [&](int x, int y) {
        const float weights[2] = {0.5f, 1.0f};
        Rgba mean{};
        float total = 0.0f;
        for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
                const float weight = weights[dx] * weights[dy];
                const Rgba p = full.pixel(x + dx, y + dy);
                mean.r += p.r * weight;
                mean.a += p.a * weight;
                total += weight;
            }
        }
        mean.r /= total;
        mean.a /= total;
        return mean;
    };

    // Every entry with an odd index sits at the same phase, so this is the
    // whole diagonal of them rather than one lucky pixel.
    for (int i = 1; i < 100; i += 2) {
        const Rgba mean = weightedMean(i + i / 2, i + i / 2);
        const Rgba got = reduced.pixel(i, i);
        CHECK_NEAR(got.a, mean.a, 1e-4);
        CHECK_NEAR(got.r, mean.r, 1e-4);
    }
}

// The half-float decode became a lookup table. It has to still be the same
// function it replaced, for every one of the 65536 possible values.
void halfLookupMatchesTheComputation() {
    TEST("the half-float table agrees with the computation");
    for (std::uint32_t bits = 0; bits < 65536; ++bits) {
        const auto h = static_cast<std::uint16_t>(bits);
        const float table = halfBitsToFloat(h);
        const float computed = halfBitsToFloatComputed(h);
        if (std::isnan(table)) {
            CHECK(std::isnan(computed));
            continue;
        }
        CHECK_EQ(table, computed);
    }
}

}  // namespace

int main() {
    std::printf("brush:\n");
    sampledCompositingAveragesTheBlockItStandsFor();
    aReducedEntryDoesNotDependOnTheRegionAskedFor();
    aFractionalStepSplitsThePixelsItLandsInside();
    halfLookupMatchesTheComputation();
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
