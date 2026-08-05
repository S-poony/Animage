// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guards on the display path, written after issue #10, where three separate
// rendering faults had been argued about for a long time and never measured.
//
// These are deliberately *structural* rather than timed. Every one of the
// faults was a decision the code made -- how coarsely to sample, how much to
// cache beyond the viewport, which filter to blit with -- and each of those is
// a property that can be asserted exactly. A wall clock would catch the same
// regressions less reliably and would flake on a loaded build machine.
//
// `bench_zoom` has the numbers. This file has the invariants that keep them
// true, so a change that quietly gives one back is a red build rather than a
// complaint about jagged strokes six months later.

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <vector>

#include "brush.h"
#include "canvas_widget.h"
#include "compositor.h"
#include "document.h"
#include "testing.h"

using namespace animage;

namespace {

// Closed curves across the frame. Curves rather than straight lines because
// that is what a resampling artefact shows on, and spread out because a cache
// covering one dot proves nothing about a cache covering a drawing.
void drawCurves(Document& doc, TrackId track, ImageId image, LayerId layer, int curves,
                int width, int height) {
    ScopedCommand command(doc, "Curves");
    BrushSettings settings;
    settings.radius = 6.0f;
    settings.pressure_affects_opacity = false;
    settings.a = 1.0f;
    Brush brush(settings);

    unsigned state = 0x9e37u;
    const auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>((state >> 8) & 0xffff) / 65535.0;
    };

    for (int c = 0; c < curves; ++c) {
        const double cx = next() * width;
        const double cy = next() * height;
        const double rx = 60.0 + next() * 200.0;
        const double ry = 60.0 + next() * 200.0;
        brush.begin(doc, track, image, layer,
                    {static_cast<float>(cx + rx), static_cast<float>(cy), 1.0f});
        for (int i = 1; i <= 48; ++i) {
            const double t = i * 2.0 * M_PI / 48.0;
            brush.extend({static_cast<float>(cx + rx * std::cos(t)),
                          static_cast<float>(cy + ry * std::sin(t)), 1.0f});
        }
        brush.end();
    }
}

struct Fixture {
    Document doc;
    TrackId track;
    LayerId layer;
    ImageId image;
    CanvasWidget canvas;

    Fixture(int width, int height, int curves = 6) : canvas(doc) {
        track = doc.addTrack("main");
        image = doc.insertImage(track, 0);
        layer = doc.addLayer(track, "layer 1");
        doc.setCanvasSize(1920, 1080);
        drawCurves(doc, track, image, layer, curves, 1920, 1080);
        canvas.resize(width, height);
        canvas.setTrack(track);
        canvas.setFrame(0);
        canvas.setActiveLayer(layer);
    }

    // Builds the cache fresh at `zoom`, rather than letting it inherit one from
    // whatever the last call left behind.
    void settleAt(double zoom) {
        canvas.resetView();
        canvas.setZoom(zoom, QPointF(canvas.width() / 2.0, canvas.height() / 2.0));
        canvas.grab();
    }

    // How far past the viewport the cache reaches, in screen pixels, on the
    // tighter of the two axes. Freshly built the padding is symmetric, so half
    // the excess width is the margin on each side.
    double marginInScreenPixels() const {
        const PixelRect cached = canvas.cachedRegion();
        const double zoom = canvas.zoom();
        const double horizontal = (cached.width * zoom - canvas.width()) / 2.0;
        const double vertical = (cached.height * zoom - canvas.height()) / 2.0;
        return std::min(horizontal, vertical);
    }
};

// The fault: between 60% and 72% zoom the budget loop spent the entire margin,
// so the cache held the viewport and nothing more and every single mouse move
// of a pan left it. Each one cost a full recomposite -- 18 to 65 ms, measured,
// on every move for as long as the hand kept moving.
//
// The margin is what makes a pan free, so it is the thing that must survive.
void everyZoomKeepsAMarginToPanInto() {
    TEST("a margin to pan into survives at every zoom");
    Fixture fixture(1645, 765);

    for (double zoom : {0.05, 0.10, 0.25, 0.40, 0.50, 0.55, 0.60, 0.65, 0.68, 0.70, 0.72,
                        0.80, 0.90, 1.00, 1.09, 1.50, 2.00, 4.00, 8.00, 16.0}) {
        fixture.settleAt(zoom);
        const double margin = fixture.marginInScreenPixels();
        // A pixel of slack for the rounding into and out of image coordinates.
        if (margin < CanvasWidget::kMinCacheMargin - 1.0) {
            testing::fail(__FILE__, __LINE__,
                          "no margin to pan into at zoom " + std::to_string(zoom) +
                              ": margin = " + std::to_string(margin) + " screen px, floor is " +
                              std::to_string(CanvasWidget::kMinCacheMargin));
        }
        CHECK(margin >= CanvasWidget::kMinCacheMargin - 1.0);
    }
}

// The consequence of the same fault, asserted where it was actually felt: a
// drag across the canvas must not rebuild the cache on every move. Counting
// rebuilds rather than timing them, so this says the same thing on any machine.
void panningDoesNotRecompositeOnEveryMove() {
    TEST("a pan does not recomposite on every mouse move");
    Fixture fixture(1645, 765);

    // The band that used to stall on 100% of moves, plus one either side.
    for (double zoom : {0.50, 0.60, 0.65, 0.68, 0.70, 0.72, 0.90}) {
        fixture.settleAt(zoom);

        constexpr int kMoves = 60;
        constexpr double kPixelsPerMove = 12.0;
        const QPointF start(fixture.canvas.width() / 2.0, fixture.canvas.height() / 2.0);

        QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::MiddleButton,
                          Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(&fixture.canvas, &press);

        PixelRect previous = fixture.canvas.cachedRegion();
        int rebuilds = 0;
        for (int i = 1; i <= kMoves; ++i) {
            const QPointF at = start + QPointF(i * kPixelsPerMove, i * kPixelsPerMove * 0.5);
            QMouseEvent move(QEvent::MouseMove, at, at, Qt::NoButton, Qt::MiddleButton,
                             Qt::NoModifier);
            QApplication::sendEvent(&fixture.canvas, &move);
            fixture.canvas.grab();

            const PixelRect now = fixture.canvas.cachedRegion();
            if (now.x != previous.x || now.y != previous.y || now.width != previous.width ||
                now.height != previous.height) {
                ++rebuilds;
                previous = now;
            }
        }

        QMouseEvent release(QEvent::MouseButtonRelease, start, start, Qt::MiddleButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&fixture.canvas, &release);

        // 720 screen pixels of travel against a margin of at least 32 either
        // side is at most about 23 rebuilds; 30 leaves room for the rounding
        // without leaving room for the fault, which scored 60 out of 60.
        if (rebuilds > 30) {
            testing::fail(__FILE__, __LINE__,
                          "pan rebuilt the cache " + std::to_string(rebuilds) + " times in " +
                              std::to_string(kMoves) + " moves at zoom " + std::to_string(zoom));
        }
        CHECK(rebuilds <= 30);
        // And it must actually be panning far enough to matter, or the bound
        // above would pass on a test that never moved.
        CHECK(rebuilds >= 1);
    }
}

// The fault: the cache was held at one entry per *image* pixel reduced by an
// integer step, so it grew as viewport/zoom^2 and had to be capped -- and the
// cap, not any judgement about resolution, decided the zoom at which the step
// doubled. A stroke was crisp at 72% and jagged at 68% because a memory limit
// landed between them, and the boundary moved with the window size, so the same
// percentage meant different sharpness in different windows.
//
// Held at one entry per *screen* pixel there is no step to jump: the ratio is
// 1/zoom, continuously. This is the stronger assertion issue #11 asks for --
// not "full resolution above some zoom", but that sampling density tracks zoom
// with no jump anywhere and no dependence on the window at all.
void samplingDensityTracksZoomWithNoStep() {
    TEST("sampling density tracks zoom continuously, in every window");

    for (auto size : {std::pair{800, 500}, {1100, 640}, {1400, 700}, {1645, 765},
                      {2200, 1200}, {2560, 1400}}) {
        Fixture fixture(size.first, size.second);
        double previous = 0.0;
        double previous_zoom = 0.0;

        // A fine sweep across the band the issue is about, so a step of any
        // size has nowhere to hide between two sampled zooms.
        for (int i = 0; i <= 80; ++i) {
            const double zoom = 0.30 + i * 0.01;
            fixture.settleAt(zoom);
            const double ratio = fixture.canvas.cacheStep().ratio();

            // One entry per screen pixel, to within the grid's own rounding.
            const double wanted = std::max(1.0, 1.0 / zoom);
            if (std::abs(ratio - wanted) > 0.01) {
                testing::fail(__FILE__, __LINE__,
                              "the cache samples " + std::to_string(ratio) +
                                  " image pixels an entry at zoom " + std::to_string(zoom) +
                                  " in a " + std::to_string(size.first) + "x" +
                                  std::to_string(size.second) + " window, wanted " +
                                  std::to_string(wanted));
            }
            CHECK(std::abs(ratio - wanted) <= 0.01);

            // And no jump between adjacent zooms. 1/zoom moves by about 3% per
            // step of this sweep at its steepest; the fault this replaces
            // doubled.
            if (previous > 0.0) {
                const double jump = std::max(ratio / previous, previous / ratio);
                if (jump > 1.06) {
                    testing::fail(__FILE__, __LINE__,
                                  "sampling density jumped " + std::to_string(jump) +
                                      "x between zoom " + std::to_string(previous_zoom) +
                                      " and " + std::to_string(zoom));
                }
                CHECK(jump <= 1.06);
            }
            previous = ratio;
            previous_zoom = zoom;
        }
    }
}

// The other half of the same design: because the cache is addressed in screen
// pixels, its size stops depending on the zoom. It used to be 2.5M entries at
// 72% and 100k at 400% -- a 25x swing -- and since a refresh costs about 25 ns
// an entry, that swing *was* the cost of zooming out. It is also what forced
// the cap that put the step where it was.
void theCacheDoesNotGrowAsTheViewZoomsOut() {
    TEST("the cache stays the size of the window at every zoom");
    Fixture fixture(1645, 765);
    const long long viewport = 1645LL * 765;

    for (double zoom : {0.05, 0.10, 0.25, 0.40, 0.50, 0.60, 0.68, 0.72, 0.90, 1.00, 1.09,
                        2.00, 4.00, 16.0}) {
        fixture.settleAt(zoom);
        const long long entries = fixture.canvas.cacheEntryCount();
        // The viewport plus a 64 screen pixel margin each side is about 1.26x
        // it; twice leaves room for the rounding out to entry boundaries
        // without leaving room for a cache that scales with 1/zoom^2.
        if (entries > viewport * 2) {
            testing::fail(__FILE__, __LINE__,
                          "the cache holds " + std::to_string(entries) +
                              " entries at zoom " + std::to_string(zoom) +
                              " for a viewport of " + std::to_string(viewport));
        }
        CHECK(entries <= viewport * 2);
    }
}

// Holding the cache at screen resolution is only worth it if the blit is then a
// copy. That needs two things: the ratio being exactly 1/zoom, which the sweep
// above pins, and the view sitting on a whole screen pixel, so that entry
// boundaries -- at image x = entry/zoom -- land on pixel boundaries.
//
// Off that alignment Qt resamples the cache against itself at roughly 1:1,
// which is pure blur and buys nothing: measured at 4.2 RMS against a curve
// drawn at display size, where the aligned blit gives 1.7. It is invisible in
// any structural way, which is exactly why it wants an invariant.
void theViewSitsOnWholeScreenPixels() {
    TEST("the view sits on whole screen pixels, so the cache blits one to one");
    Fixture fixture(1645, 765);

    const auto aligned = [&](const char* what, double zoom) {
        const QPointF pan = fixture.canvas.pan();
        for (double along : {pan.x(), pan.y()}) {
            const double screen = along * zoom;
            if (std::abs(screen - std::round(screen)) > 1e-6) {
                testing::fail(__FILE__, __LINE__,
                              std::string(what) + " left the view off a screen pixel at zoom " +
                                  std::to_string(zoom) + ": pan * zoom = " +
                                  std::to_string(screen));
            }
            CHECK(std::abs(screen - std::round(screen)) <= 1e-6);
        }
    };

    for (double zoom : {0.05, 0.17, 0.33, 0.50, 0.61, 0.68, 0.72, 0.83, 1.00, 1.37, 4.00}) {
        fixture.settleAt(zoom);
        aligned("zooming", zoom);

        // And it has to survive a pan, which is where the view actually moves.
        const QPointF start(fixture.canvas.width() / 2.0, fixture.canvas.height() / 2.0);
        QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::MiddleButton,
                          Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(&fixture.canvas, &press);
        for (int i = 1; i <= 8; ++i) {
            // Deliberately fractional: a whole-pixel drag would pass this
            // without the snapping doing anything.
            const QPointF at = start + QPointF(i * 7.3, i * 3.9);
            QMouseEvent move(QEvent::MouseMove, at, at, Qt::NoButton, Qt::MiddleButton,
                             Qt::NoModifier);
            QApplication::sendEvent(&fixture.canvas, &move);
            aligned("panning", zoom);
        }
        QMouseEvent release(QEvent::MouseButtonRelease, start, start, Qt::MiddleButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&fixture.canvas, &release);
    }

    // Fitting sets the view directly rather than moving it, and is the third
    // way in.
    fixture.canvas.fitToCanvas();
    aligned("fitting to the canvas", fixture.canvas.zoom());
    fixture.canvas.fitToDrawing();
    aligned("fitting to the drawing", fixture.canvas.zoom());
}

// The fault: the blit chose its filter on `zoom_ < 1.0`, which is wrong twice
// over. It asked about the zoom when the factor being applied is
// cache_step_ * zoom_, and it switched to nearest-neighbour at 101%, where
// there is nothing to see and a curve gains a staircase.
void theBlitInterpolatesUntilThePixelsAreWorthSeeing() {
    TEST("the blit interpolates until the pixels are worth seeing");

    // Magnifying a little: interpolate. This is the 109% case from the issue.
    CHECK(CanvasWidget::blitInterpolatesAt(1.01));
    CHECK(CanvasWidget::blitInterpolatesAt(1.09));
    CHECK(CanvasWidget::blitInterpolatesAt(2.0));

    // Minifying: interpolate, always.
    CHECK(CanvasWidget::blitInterpolatesAt(0.5));
    CHECK(CanvasWidget::blitInterpolatesAt(0.99));

    // Far enough in that the pixels are the subject: show them as squares.
    CHECK(!CanvasWidget::blitInterpolatesAt(3.0));
    CHECK(!CanvasWidget::blitInterpolatesAt(8.0));

    // And the argument is the factor the blit applies, not the zoom. At step 2
    // and 70% zoom the cache is being magnified by 1.4, so this must be a
    // question about 1.4 rather than about 0.7 -- both interpolate here, but
    // they part company as soon as the step rises.
    CHECK(CanvasWidget::blitInterpolatesAt(2 * 0.70));
    CHECK(!CanvasWidget::blitInterpolatesAt(4 * 0.90));
}

// The fault: the loop that turns the flattened region into display pixels ran
// on one thread. It was never timed, because `bench_composite` watched the
// compositor instead -- which is the smaller half. Measured at 37 ms against
// the compositor's 3-8, and identical for a 66-tile drawing and a 2425-tile
// one, because the work is per output pixel rather than per stroke.
//
// A ratio rather than a deadline, so a slow machine fails this test only if it
// is slow at the writeback specifically. Un-threading it took the ratio from
// about 3 to about 12.
void theWritebackIsNotTheSlowHalfOfARefresh() {
    TEST("the writeback does not dominate a refresh");
    Fixture fixture(1645, 765, 40);
    fixture.settleAt(0.72);  // near the largest the cache is allowed to get

    const auto medianOf = [](std::vector<double> samples) {
        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
    };

    std::vector<double> refreshes;
    QElapsedTimer clock;
    for (int i = 0; i < 7; ++i) {
        fixture.canvas.refreshAll();
        clock.start();
        fixture.canvas.grab();
        refreshes.push_back(clock.nsecsElapsed() / 1e6);
    }

    Compositor compositor;
    Framebuffer frame;
    const PixelRect cached = fixture.canvas.cachedRegion();
    const SampleStep step = fixture.canvas.cacheStep();
    compositor.composite(fixture.doc, fixture.track, fixture.image, cached, frame, step);
    std::vector<double> composites;
    for (int i = 0; i < 7; ++i) {
        clock.start();
        compositor.composite(fixture.doc, fixture.track, fixture.image, cached, frame, step);
        composites.push_back(clock.nsecsElapsed() / 1e6);
    }

    const double refresh = medianOf(refreshes);
    const double composite = medianOf(composites);
    const double ratio = refresh / std::max(0.01, composite);
    if (ratio > 6.0) {
        testing::fail(__FILE__, __LINE__,
                      "a full refresh costs " + std::to_string(ratio) +
                          "x the compositing it contains (" + std::to_string(refresh) +
                          " ms against " + std::to_string(composite) +
                          " ms) -- the writeback is probably back on one thread");
    }
    CHECK(ratio <= 6.0);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::printf("render:\n");
    everyZoomKeepsAMarginToPanInto();
    panningDoesNotRecompositeOnEveryMove();
    samplingDensityTracksZoomWithNoStep();
    theCacheDoesNotGrowAsTheViewZoomsOut();
    theViewSitsOnWholeScreenPixels();
    theBlitInterpolatesUntilThePixelsAreWorthSeeing();
    theWritebackIsNotTheSlowHalfOfARefresh();
    return testing::summarise("render");
}
