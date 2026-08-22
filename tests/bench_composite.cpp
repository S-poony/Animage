// SPDX-License-Identifier: GPL-3.0-or-later
//
// Not a test -- a stopwatch. Scrubbing the timeline, dragging the opacity
// slider and changing frame all end in exactly one operation: flatten the
// visible region. If that operation costs more than a frame, every one of them
// feels heavy, and no amount of deferring or coalescing will fix it.
//
// Run it by hand:  ./build/tests/bench_composite

#include <chrono>
#include <cstdio>
#include <vector>

#include "brush.h"
#include "lazybrush.h"
#include "compositor.h"
#include "ctg.h"
#include "document.h"

using namespace animage;
using Clock = std::chrono::steady_clock;

namespace {

double milliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Something like a real drawing: strokes spread over the whole viewport rather
// than one dot, so the tiles actually exist and have to be walked.
void scribble(Document& doc, TrackId track, ImageId image, LayerId layer, int strokes,
              int width, int height, unsigned seed) {
    ScopedCommand command(doc, "Scribble");
    BrushSettings settings;
    settings.radius = 9.0f;
    settings.pressure_affects_opacity = false;
    Brush brush(settings);

    unsigned state = seed;
    const auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>((state >> 8) & 0xffff) / 65535.0;
    };

    for (int s = 0; s < strokes; ++s) {
        const float x0 = static_cast<float>(next() * width);
        const float y0 = static_cast<float>(next() * height);
        brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
        for (int i = 0; i < 18; ++i) {
            brush.extend({static_cast<float>(x0 + (next() - 0.5) * 260),
                          static_cast<float>(y0 + (next() - 0.5) * 260), 1.0f});
        }
        brush.end();
    }
}

double timeComposites(const Document& doc, TrackId track, ImageId image,
                      const PixelRect& region, SampleStep step, int repeats) {
    Compositor compositor;
    Framebuffer frame;
    // One outside the timing, so the buffer is already allocated.
    compositor.composite(doc, track, image, region, frame, step);

    const auto start = Clock::now();
    for (int i = 0; i < repeats; ++i) {
        compositor.composite(doc, track, image, region, frame, step);
    }
    return milliseconds(start, Clock::now()) / repeats;
}

}  // namespace

int main() {
    constexpr int kViewportWidth = 1150;
    constexpr int kViewportHeight = 640;
    constexpr int kMargin = 64;  // what CanvasWidget actually uses

    std::printf("compositing a %dx%d viewport\n\n", kViewportWidth, kViewportHeight);

    for (int layers = 1; layers <= 4; ++layers) {
        Document doc;
        const TrackId track = doc.addTrack("main");
        const ImageId image = doc.insertImage(track, 0);
        for (int i = 0; i < layers; ++i) {
            const LayerId layer = doc.addLayer(track, "layer " + std::to_string(i + 1));
            scribble(doc, track, image, layer, 90, kViewportWidth, kViewportHeight,
                     0x9e37u + i * 7919u);
        }

        const PixelRect viewport{0, 0, kViewportWidth, kViewportHeight};
        const double bare = timeComposites(doc, track, image, viewport, {}, 20);

        std::printf("%d layer%s  %6zu tiles\n", layers, layers == 1 ? " " : "s",
                    doc.totalTileCount());
        std::printf("    no margin        %7.2f ms\n", bare);

        // The margin buys free panning and is charged on every full refresh --
        // every frame change, every opacity tick. Worth seeing the price.
        for (int margin : {kMargin, 192}) {
            const PixelRect padded{-margin, -margin, kViewportWidth + 2 * margin,
                                   kViewportHeight + 2 * margin};
            const double timed = timeComposites(doc, track, image, padded, {}, 20);
            std::printf("    margin %3d px    %7.2f ms   (%.2fx)\n", margin, timed,
                        timed / bare);
        }
    }

    // Zooming out reduces a block of image pixels to each entry over a much
    // wider region. It was never measured before, and it was the slowest path
    // in the program. The block is averaged rather than point-sampled since
    // issue #11, so the reading is bounded per entry rather than free -- which
    // is the price of not shimmering, and worth watching here.
    std::printf("\nzoomed out, 4 layers over a wide drawing\n");
    {
        Document doc;
        const TrackId track = doc.addTrack("main");
        const ImageId image = doc.insertImage(track, 0);
        for (int i = 0; i < 4; ++i) {
            const LayerId layer = doc.addLayer(track, "layer " + std::to_string(i + 1));
            scribble(doc, track, image, layer, 240, kViewportWidth * 6, kViewportHeight * 6,
                     0x51edu + i * 7919u);
        }
        for (double zoom : {1.0, 0.7, 0.5, 0.2, 0.1, 0.05}) {
            const SampleStep step = SampleStep::fromRatio(1.0 / zoom);
            const PixelRect region{0, 0, static_cast<int>(kViewportWidth / zoom),
                                   static_cast<int>(kViewportHeight / zoom)};
            const double timed = timeComposites(doc, track, image, region, step, 10);
            std::printf("    zoom %5.2f (%5.2f image px an entry, read every %d)  %7.2f ms\n",
                        zoom, step.ratio(), boxSampleStride(step), timed);
        }
    }

    // LazyBrush. The plan's answer to interactivity is to solve at half or
    // quarter resolution while the scribble is being drawn and at full size
    // once the pen lifts, so what matters is where resolution stops being
    // affordable.
    std::printf("\nLazyBrush: three boxed regions on a background, each wall gapped\n");
    for (int side : {128, 256, 512, 1024}) {
        LazyBrushProblem problem;
        problem.width = side;
        problem.height = side;
        problem.intensity.assign(static_cast<std::size_t>(side) * side, 1.0f);
        problem.seeds.assign(static_cast<std::size_t>(side) * side, -1);
        problem.colour_count = 4;
        problem.hard.assign(4, 0);

        const auto line = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= side || y >= side) return;
            problem.intensity[static_cast<std::size_t>(y) * side + x] = 0.0f;
        };
        for (int box = 0; box < 3; ++box) {
            const int left = side / 8 + box * side / 4;
            const int top = side / 4;
            const int w = side / 6;
            const int h = side / 2;
            for (int x = 0; x <= w; ++x) {
                line(left + x, top);
                if (x < w / 2 || x > w / 2 + 3) line(left + x, top + h);  // a gap
            }
            for (int y = 0; y <= h; ++y) {
                line(left, top + y);
                line(left + w, top + y);
            }
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    problem.seeds[static_cast<std::size_t>(top + h / 2 + y) * side + left +
                                  w / 2 + x] = box + 1;
                }
            }
        }
        for (int x = 0; x < side; ++x) problem.seeds[static_cast<std::size_t>(2) * side + x] = 0;

        const auto start = Clock::now();
        const LazyBrushResult solved = solveLazyBrush(problem);
        const double timed = milliseconds(start, Clock::now());
        std::printf("    %4dx%-4d  %9.1f ms   (%d cuts)\n", side, side, timed, solved.cuts);
    }

    // A whole CTG solve on a real drawing, at both of the sizes one is asked
    // for. The first answer is bounded so it arrives while the stroke that
    // caused it is still recent; the second is bounded only by memory. Neither
    // is on the interface thread any more, which is what makes the second one
    // possible at all -- so what these numbers say is "how long until the
    // colour is right", not "how long the program is stopped for".
    std::printf("\nA CTG solve on a 1920x1080 drawing, coarse then full:\n");
    {
        Document doc;
        doc.setCanvasSize(1920, 1080);
        const TrackId track = doc.addTrack("main");
        const LayerId colour = doc.addLayer(track, "colour", 0, LayerKind::Ctg);
        const LayerId ink = doc.addLayer(track, "ink", 1);
        const ImageId image = doc.insertImage(track, 0);
        {
            Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
            settings.ctg_sources = {ink};
            doc.updateLayer(track, colour, settings);
        }

        const auto stroke = [&](LayerId layer, float x0, float y0, float x1, float y1,
                                float radius, float r, float g, float b) {
            ScopedCommand command(doc, "Stroke");
            BrushSettings settings;
            settings.radius = radius;
            settings.hardness = 0.95f;
            settings.pressure_affects_opacity = false;
            // A mark on a colour layer is a label, as the application writes
            // one: a blended rim would quantise to labels nobody drew, and the
            // number of colours is what the solver's cost is counted in.
            settings.label = layer == colour;
            settings.r = r;
            settings.g = g;
            settings.b = b;
            settings.a = 1.0f;
            Brush brush(settings);
            brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
            brush.extend({x1, y1, 1.0f});
            brush.end();
        };

        // Three shapes with gapped walls, and a mark in each -- the arrangement
        // the LazyBrush timings above use, drawn at the size an animator works
        // at.
        for (int box = 0; box < 3; ++box) {
            const float left = 160.0f + static_cast<float>(box) * 560.0f;
            const float right = left + 420.0f;
            stroke(ink, left, 180, right, 180, 3.0f, 0, 0, 0);
            stroke(ink, left, 180, left, 900, 3.0f, 0, 0, 0);
            stroke(ink, right, 180, right, 900, 3.0f, 0, 0, 0);
            stroke(ink, left, 900, left + 180.0f, 900, 3.0f, 0, 0, 0);  // gapped wall
            stroke(ink, left + 240.0f, 900, right, 900, 3.0f, 0, 0, 0);
            stroke(colour, left + 100.0f, 540, right - 100.0f, 540, 22.0f,
                   0.2f + 0.3f * static_cast<float>(box), 0.3f, 0.8f);
        }

        // What a carried mark costs before the solve starts: one estimate of
        // how far the drawing has moved, on every solve of a drawing whose
        // marks came from another one.
        {
            const CtgJob job = ctgJobFor(doc, track, image, colour);
            const auto start = Clock::now();
            const CtgShift shift = estimateCtgShift(job.sources, job.sources,
                                                    doc.scene().canvas());
            const double timed = milliseconds(start, Clock::now());
            std::printf("    motion estimate, paid once per solve   %9.1f ms  (%d, %d)\n",
                        timed, shift.x, shift.y);
        }

        // And what asking it per region costs instead, which is the same
        // question plus a coarse cut of the source drawing and one search for
        // each piece a mark owns. Read as a multiple of the line above and of
        // the solve below it -- what rung three costs is what it adds to a
        // solve, and the solve is the expensive thing here.
        {
            const CtgJob job = ctgJobFor(doc, track, image, colour);
            const auto start = Clock::now();
            const CtgWarp warp =
                estimateCtgWarp(job.sources, job.sources, job.scribbles, CtgSettings{});
            const double timed = milliseconds(start, Clock::now());
            std::printf("    the same per region                    %9.1f ms  (%s)\n", timed,
                        warp.isUniform() ? "every region agreed"
                                         : "regions disagreed, a field was built");
        }

        // What flattening the ink costs, over the drawing and over four times
        // as much paper as the drawing needs.
        //
        // Two numbers rather than one, because the interesting quantity is the
        // ratio between them. The barrier used to cost the area of the region
        // whatever was in it, so the second was four times the first; it now
        // costs the area of the tiles in it, so the two should be about equal.
        // That is what makes a region nobody has bounded affordable, and it is
        // why it is measured where the region can be made large rather than
        // where it happens to fit the drawing.
        {
            const CtgJob job = ctgJobFor(doc, track, image, colour);
            const PixelRect drawn = doc.scene().canvas();
            const PixelRect wide{drawn.x - drawn.width / 2, drawn.y - drawn.height / 2,
                                 drawn.width * 2, drawn.height * 2};

            for (const auto& [name, area] :
                 {std::pair<const char*, PixelRect>{"over the drawing         ", drawn},
                  std::pair<const char*, PixelRect>{"over four times the paper", wide}}) {
                const auto start = Clock::now();
                const std::vector<float> barrier = ctgBarrier(job.sources, area, 1);
                const double timed = milliseconds(start, Clock::now());
                std::printf("    barrier %s  %9.1f ms  (%.1f Mpx)\n", name, timed,
                            static_cast<double>(area.width) * area.height / 1.0e6);
            }
        }

        for (const auto& [name, budget] :
             {std::pair<const char*, long long>{"first  ", kInteractiveSolveBudget},
              std::pair<const char*, long long>{"refined", kFullSolveBudget}}) {
            const CtgJob job = ctgJobFor(doc, track, image, colour, CtgSettings{}, budget);
            const auto start = Clock::now();
            const CtgFill fill = solveCtgJob(job, true);
            const double timed = milliseconds(start, Clock::now());
            std::printf("    %s  budget %8lld  step %d  %9.1f ms  (%d colours, %zu KB)\n",
                        name, budget, fill.step, timed, fill.colours,
                        (fill.labels.size() * sizeof(std::int16_t) +
                         fill.palette.size() * sizeof(std::uint32_t)) /
                            1024);
        }
    }

    std::printf("\nA frame at 60 Hz is 16.7 ms. Scrubbing wants one of these per frame.\n");
    return 0;
}
