// SPDX-License-Identifier: GPL-3.0-or-later
//
// Not a test -- a stopwatch. What a transform costs, and where.
//
// Written before anything was optimised, which is this repository's most
// repeated lesson: a benchmark decides where you will look next, and it also
// decides where the next problem will be. Compositing was optimised twice while
// the loop after it -- ten times larger -- went unmeasured, because
// bench_composite only ever watched one end.
//
// There are three separate costs here and they are not the same shape:
//
//   the lift     rasterising a loop and splitting the drawing along it. Paid
//                once, when the gesture starts.
//   the preview  one layer composited into an image, paid once, and then a
//                QTransform blit per frame that this cannot see -- it is Qt's.
//   the commit   the resample. Paid once, when the gesture ends, and the only
//                one of the three that the user is standing still waiting for.
//
// Run it by hand:  ./build/tests/bench_transform

#include <chrono>
#include <cstdio>
#include <vector>

#include "brush.h"
#include "document.h"
#include "selection.h"
#include "transform.h"

using namespace animage;
using Clock = std::chrono::steady_clock;

namespace {

double milliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Something like a real drawing: strokes over the whole frame, so the tiles
// exist and have to be walked. The same shape bench_composite uses, so the two
// are describing the same sort of picture.
TileGrid drawing(int width, int height, int strokes, unsigned seed) {
    Document doc;
    const TrackId track = doc.addTrack("main");
    const LayerId layer = doc.addLayer(track, "ink");
    const ImageId image = doc.insertImage(track, 0);

    BrushSettings settings;
    settings.radius = 4.0f;
    settings.pressure_affects_opacity = false;
    Brush brush(settings);

    unsigned state = seed;
    const auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>((state >> 8) & 0xffff) / 65535.0;
    };

    {
        ScopedCommand command(doc, "Scribble");
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
    return doc.celAt(track, image, layer)->tiles();
}

double timeTransform(const TileGrid& source, const Transform& values, int repeats) {
    TileGrid out = transformTiles(source, values);  // one outside the timing
    const auto start = Clock::now();
    for (int i = 0; i < repeats; ++i) out = transformTiles(source, values);
    return milliseconds(start, Clock::now()) / repeats;
}

// A rough circle, which is what a lasso round something actually looks like --
// not the four-point rectangle a test uses.
Selection loopAround(const PixelRect& area, int points) {
    Selection loop;
    const double centre_x = area.x + area.width / 2.0;
    const double centre_y = area.y + area.height / 2.0;
    const double radius_x = area.width / 2.0;
    const double radius_y = area.height / 2.0;
    for (int i = 0; i < points; ++i) {
        const double angle = 2.0 * 3.14159265358979323846 * i / points;
        loop.loop.push_back({centre_x + radius_x * std::cos(angle),
                             centre_y + radius_y * std::sin(angle)});
    }
    return loop;
}

}  // namespace

int main() {
    const struct {
        const char* name;
        int width;
        int height;
        int strokes;
    } sizes[] = {
        {"PAL      720x576 ", 720, 576, 40},
        {"HD      1920x1080", 1920, 1080, 110},
        {"4K      3840x2160", 3840, 2160, 400},
    };

    std::printf("what a transform costs\n\n");

    for (const auto& size : sizes) {
        const TileGrid ink = drawing(size.width, size.height, size.strokes, 0x9e37u);
        const PixelRect drawn = paintedBounds(ink);

        std::printf("%s  %zu tiles, ink over %dx%d\n", size.name, ink.tileCount(), drawn.width,
                    drawn.height);

        Transform nudge;
        nudge.dx = 3.0;
        nudge.dy = -2.0;
        std::printf("    nudge, whole pixels  %8.2f ms   %s\n", timeTransform(ink, nudge, 5),
                    nudge.isWholePixelTranslation() ? "(exact path)" : "(RESAMPLED!)");

        // The other exact path (#24). It is here beside the nudge rather than
        // among the resampled cases because that is the claim being made about
        // it: a mirror is a permutation of pixels, so it should cost what moving
        // them costs and not what filtering them costs.
        Transform mirror;
        mirror.flip_x = true;
        mirror.pivot_x = drawn.x + drawn.width / 2.0;
        mirror.pivot_y = drawn.y + drawn.height / 2.0;
        std::printf("    flip X               %8.2f ms   %s\n", timeTransform(ink, mirror, 5),
                    mirror.isAxisMirror() ? "(exact path)" : "(RESAMPLED!)");

        // What each of these says about the filter is worth reading twice. The
        // labels here used to name the path taken, and they were wrong for
        // years about the one that mattered: "rotate 7 degrees (bilinear)" was
        // printed by a case that had never once been bilinear, because the
        // branch treated every rotation as a reduction. A label is a claim, and
        // an unchecked one hides exactly the bug it describes -- so these now
        // say the kernel's width, which is a number the code computes rather
        // than a name somebody wrote down.
        Transform half;
        half.dx = 3.5;
        half.dy = -2.0;
        std::printf("    nudge, half a pixel  %8.2f ms   (kernel 1 px)\n",
                    timeTransform(ink, half, 5));

        Transform turn;
        turn.rotation = 7.0;
        turn.pivot_x = drawn.x + drawn.width / 2.0;
        turn.pivot_y = drawn.y + drawn.height / 2.0;
        std::printf("    rotate 7 degrees     %8.2f ms   (kernel 1 px: a turn reduces nothing)\n",
                    timeTransform(ink, turn, 5));

        Transform grow = turn;
        grow.rotation = 0.0;
        grow.scale_x = grow.scale_y = 2.0;
        std::printf("    scale 200%%           %8.2f ms   (kernel 1 px, 4x the pixels)\n",
                    timeTransform(ink, grow, 5));

        Transform shrink = turn;
        shrink.rotation = 0.0;
        shrink.scale_x = shrink.scale_y = 0.25;
        std::printf("    scale 25%%            %8.2f ms   (kernel 4 px)\n",
                    timeTransform(ink, shrink, 5));

        // The lift: rasterising the loop and splitting the drawing along it,
        // paid once when the gesture starts. A loop round a quarter of the
        // frame, which is a normal thing to lasso.
        const PixelRect quarter{drawn.x + drawn.width / 4, drawn.y + drawn.height / 4,
                                drawn.width / 2, drawn.height / 2};
        const Selection loop = loopAround(quarter, 64);

        auto start = Clock::now();
        CoverageMask mask;
        for (int i = 0; i < 5; ++i) mask = rasterise(loop, drawn);
        const double rasterising = milliseconds(start, Clock::now()) / 5;

        start = Clock::now();
        Lift split;
        for (int i = 0; i < 5; ++i) split = liftThrough(ink, mask);
        const double lifting = milliseconds(start, Clock::now()) / 5;

        std::printf("    lasso: rasterise     %8.2f ms\n", rasterising);
        std::printf("    lasso: lift          %8.2f ms   (%zu tiles taken, %zu left)\n", lifting,
                    split.lifted.tileCount(), split.remaining.tileCount());

        // And the commit of a lassoed transform, which is the resample plus
        // putting the two halves back together.
        Transform moved;
        moved.dx = 40.0;
        moved.dy = 25.0;
        moved.rotation = 5.0;
        moved.pivot_x = quarter.x + quarter.width / 2.0;
        moved.pivot_y = quarter.y + quarter.height / 2.0;

        start = Clock::now();
        for (int i = 0; i < 5; ++i) {
            TileGrid landed = mergeOver(transformTiles(split.lifted, moved), split.remaining);
            (void)landed;
        }
        std::printf("    lasso: commit        %8.2f ms   (resample + merge)\n",
                    milliseconds(start, Clock::now()) / 5);

        // What paintedBounds costs, since it is what the box is made of and it
        // reads every pixel of every occupied tile. Paid once per gesture, and
        // worth knowing before anybody puts it in a loop.
        start = Clock::now();
        long long total = 0;
        for (int i = 0; i < 5; ++i) total += paintedBounds(ink).width;
        std::printf("    paintedBounds        %8.2f ms   (%lld)\n",
                    milliseconds(start, Clock::now()) / 5, total);

        // And what one costs the history, which is the number the undo budget
        // is chosen against rather than guessed at. A transform replaces every
        // tile the cel has and journals both sides, so one command retains a
        // whole drawing -- against a stroke's two to six tiles.
        {
            Document doc;
            const TrackId track = doc.addTrack("main");
            const LayerId layer = doc.addLayer(track, "ink");
            const ImageId image = doc.insertImage(track, 0);
            {
                ScopedCommand command(doc, "Draw");
                doc.celForWriting(track, image, layer)->replaceTiles(ink, doc.journal());
            }
            const std::size_t before = doc.historyBytes();
            {
                ScopedCommand command(doc, "Transform");
                doc.celForWriting(track, image, layer)
                    ->replaceTiles(transformTiles(ink, turn), doc.journal());
            }
            const std::size_t retained = doc.historyBytes() - before;
            std::printf("    one in the history   %8.2f MB   (%zu of them in the %zu MB budget)\n\n",
                        static_cast<double>(retained) / (1024.0 * 1024.0),
                        retained == 0 ? 0 : Document::kDefaultHistoryBudget / retained,
                        Document::kDefaultHistoryBudget / (1024 * 1024));
        }
    }

    return 0;
}
