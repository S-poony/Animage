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
#include "compositor.h"
#include "document.h"

using namespace animage;
using Clock = std::chrono::steady_clock;

namespace {

double milliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Something like a real drawing: strokes spread over the whole viewport rather
// than one dot, so the tiles actually exist and have to be walked.
void scribble(Document& doc, TimelineId timeline, ImageId image, LayerId layer, int strokes,
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
        brush.begin(doc, timeline, image, layer, {x0, y0, 1.0f});
        for (int i = 0; i < 18; ++i) {
            brush.extend({static_cast<float>(x0 + (next() - 0.5) * 260),
                          static_cast<float>(y0 + (next() - 0.5) * 260), 1.0f});
        }
        brush.end();
    }
}

double timeComposites(const Document& doc, TimelineId timeline, ImageId image,
                      const PixelRect& region, int step, int repeats) {
    Compositor compositor;
    Framebuffer frame;
    // One outside the timing, so the buffer is already allocated.
    compositor.composite(doc, timeline, image, region, frame, step);

    const auto start = Clock::now();
    for (int i = 0; i < repeats; ++i) {
        compositor.composite(doc, timeline, image, region, frame, step);
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
        const TimelineId timeline = doc.addTimeline("main");
        const ImageId image = doc.insertImage(timeline, 0);
        for (int i = 0; i < layers; ++i) {
            const LayerId layer = doc.addLayer(timeline, "layer " + std::to_string(i + 1));
            scribble(doc, timeline, image, layer, 90, kViewportWidth, kViewportHeight,
                     0x9e37u + i * 7919u);
        }

        const PixelRect viewport{0, 0, kViewportWidth, kViewportHeight};
        const double bare = timeComposites(doc, timeline, image, viewport, 1, 20);

        std::printf("%d layer%s  %6zu tiles\n", layers, layers == 1 ? " " : "s",
                    doc.totalTileCount());
        std::printf("    no margin        %7.2f ms\n", bare);

        // The margin buys free panning and is charged on every full refresh --
        // every frame change, every opacity tick. Worth seeing the price.
        for (int margin : {kMargin, 192}) {
            const PixelRect padded{-margin, -margin, kViewportWidth + 2 * margin,
                                   kViewportHeight + 2 * margin};
            const double timed = timeComposites(doc, timeline, image, padded, 1, 20);
            std::printf("    margin %3d px    %7.2f ms   (%.2fx)\n", margin, timed,
                        timed / bare);
        }
    }

    // Zooming out samples every nth pixel over a much wider region. It was
    // never measured before, and it was the slowest path in the program.
    std::printf("\nzoomed out, 4 layers over a wide drawing\n");
    {
        Document doc;
        const TimelineId timeline = doc.addTimeline("main");
        const ImageId image = doc.insertImage(timeline, 0);
        for (int i = 0; i < 4; ++i) {
            const LayerId layer = doc.addLayer(timeline, "layer " + std::to_string(i + 1));
            scribble(doc, timeline, image, layer, 240, kViewportWidth * 6, kViewportHeight * 6,
                     0x51edu + i * 7919u);
        }
        for (int step : {1, 2, 5, 10, 20}) {
            const PixelRect region{0, 0, kViewportWidth * step, kViewportHeight * step};
            const double timed = timeComposites(doc, timeline, image, region, step, 10);
            std::printf("    step %2d (zoom %5.2f)  %7.2f ms\n", step, 1.0 / step, timed);
        }
    }

    std::printf("\nA frame at 60 Hz is 16.7 ms. Scrubbing wants one of these per frame.\n");
    return 0;
}
