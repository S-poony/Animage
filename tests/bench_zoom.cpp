// SPDX-License-Identifier: GPL-3.0-or-later
//
// Not a test -- a stopwatch, for issue #10: strokes go jagged at some zoom
// levels and not others, and navigating while zoomed out lags. Both had been
// argued about and neither had been timed, so this drives the real
// CanvasWidget offscreen and reads the answers off it.
//
// Three questions, in order:
//   1. What is cache_step_ actually doing as zoom varies, and where does it
//      change? That is what decides how much of the drawing survives to the
//      screen.
//   2. What does a full refresh, a cached repaint and a pan each cost at each
//      zoom, and how much of that is the compositor rather than everything
//      round it?
//   3. Which resampling filter is on when, and what does it do to an edge?
//
// Run it by hand:  ./build/tests/bench_zoom -platform offscreen

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "brush.h"
#include "canvas_widget.h"
#include "compositor.h"
#include "document.h"

using namespace animage;

namespace {

// The canvas widget in a maximised 1920x1080 window, measured off the
// screenshots on the issue: the layers dock and the timeline take the rest.
constexpr int kCanvasWidth = 1645;
constexpr int kCanvasHeight = 765;

// The zoom levels the issue names, plus enough either side to see the shape of
// whatever is happening between them.
const std::vector<double> kZooms = {0.10, 0.25, 0.40, 0.50, 0.60, 0.65, 0.68,
                                    0.69, 0.70, 0.71, 0.72, 0.80, 0.90, 0.99,
                                    1.00, 1.01, 1.09, 1.20, 1.50, 2.00, 4.00};

double median(std::vector<double> samples) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// A drawing rather than a dot: closed curves across the frame, which is what
// the jaggedness shows up on and what a pan has to recomposite.
void drawCurves(Document& doc, TrackId track, ImageId image, LayerId layer, int curves,
                int width, int height, unsigned seed) {
    ScopedCommand command(doc, "Curves");
    BrushSettings settings;
    settings.radius = 6.0f;
    settings.hardness = 0.6f;
    settings.pressure_affects_opacity = false;
    settings.r = settings.g = settings.b = 0.0f;
    settings.a = 1.0f;
    Brush brush(settings);

    unsigned state = seed;
    const auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>((state >> 8) & 0xffff) / 65535.0;
    };

    for (int c = 0; c < curves; ++c) {
        const double cx = next() * width;
        const double cy = next() * height;
        const double rx = 60.0 + next() * 220.0;
        const double ry = 60.0 + next() * 220.0;
        brush.begin(doc, track, image, layer,
                    {static_cast<float>(cx + rx), static_cast<float>(cy), 1.0f});
        for (int i = 1; i <= 72; ++i) {
            const double t = i * 2.0 * M_PI / 72.0;
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

    explicit Fixture(int layers, int curves_per_layer, int spread)
        : canvas(doc) {
        track = doc.addTrack("main");
        image = doc.insertImage(track, 0);
        doc.setCanvasSize(1920, 1080);
        for (int i = 0; i < layers; ++i) {
            layer = doc.addLayer(track, "layer " + std::to_string(i + 1));
            drawCurves(doc, track, image, layer, curves_per_layer, spread, spread * 9 / 16,
                       0x9e37u + i * 7919u);
        }
        canvas.resize(kCanvasWidth, kCanvasHeight);
        canvas.setTrack(track);
        canvas.setFrame(0);
        canvas.setActiveLayer(layer);
    }
};

// What a drag costs, per mouse move. A median on its own is the wrong number
// here and hides the whole effect: most moves land inside the cached margin
// and cost nothing, and then one leaves it and pays for a full recomposite.
// What is felt is the spike and how often it comes, so both are reported.
struct DragCost {
    double median_ms;
    double worst_ms;
    double stall_percent;  // moves costing more than a 60 Hz frame
};

DragCost summarise(std::vector<double> samples) {
    if (samples.empty()) return {0.0, 0.0, 0.0};
    const double worst = *std::max_element(samples.begin(), samples.end());
    const long long stalls =
        std::count_if(samples.begin(), samples.end(), [](double ms) { return ms > 16.7; });
    return {median(samples), worst,
            100.0 * static_cast<double>(stalls) / static_cast<double>(samples.size())};
}

// Middle-drag, through the real event handlers, so whatever
// ensureCacheCoversView decides on the way is included. Far enough to cross
// the margin many times over: 60 moves of 12 px is 720 px of travel, which is
// a normal one-second drag and leaves any margin behind whatever the zoom.
DragCost timePan(CanvasWidget& canvas, int moves = 60, double pixels_per_move = 12.0) {
    const QPointF start(kCanvasWidth / 2.0, kCanvasHeight / 2.0);
    QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::MiddleButton, Qt::MiddleButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&canvas, &press);

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(moves));
    QElapsedTimer clock;
    for (int i = 1; i <= moves; ++i) {
        const QPointF at = start + QPointF(i * pixels_per_move, i * pixels_per_move * 0.5);
        QMouseEvent move(QEvent::MouseMove, at, at, Qt::NoButton, Qt::MiddleButton,
                         Qt::NoModifier);
        clock.start();
        QApplication::sendEvent(&canvas, &move);
        canvas.grab();  // the repaint the move asked for
        samples.push_back(clock.nsecsElapsed() / 1e6);
    }

    QMouseEvent release(QEvent::MouseButtonRelease, start, start, Qt::MiddleButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&canvas, &release);
    return summarise(std::move(samples));
}

// Scrubby zoom: Z held, dragged sideways. Every move changes the zoom, so
// nothing about the cache survives from one to the next.
DragCost timeZoomDrag(CanvasWidget& canvas, double from, int moves = 40) {
    const QPointF anchor(kCanvasWidth / 2.0, kCanvasHeight / 2.0);
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(moves));
    QElapsedTimer clock;
    for (int i = 1; i <= moves; ++i) {
        // kScrubbyZoomPerPixel is 0.006 and a drag moves several pixels a move.
        const double zoom = from * std::exp(i * 4.0 * 0.006);
        clock.start();
        canvas.setZoom(zoom, anchor);
        canvas.grab();
        samples.push_back(clock.nsecsElapsed() / 1e6);
    }
    return summarise(std::move(samples));
}

// The compositor's own share, on the region and step the canvas settled on.
double timeCompositeAlone(const Document& doc, TrackId track, ImageId image,
                          const PixelRect& region, int step, int repeats) {
    Compositor compositor;
    Framebuffer frame;
    compositor.composite(doc, track, image, region, frame, step);
    QElapsedTimer clock;
    clock.start();
    for (int i = 0; i < repeats; ++i) {
        compositor.composite(doc, track, image, region, frame, step);
    }
    return clock.nsecsElapsed() / 1e6 / repeats;
}

void sweep(const char* title, int layers, int curves, int spread) {
    Fixture fixture(layers, curves, spread);
    std::printf("\n%s -- %zu tiles, %d layer%s, widget %dx%d\n", title,
                fixture.doc.totalTileCount(), layers, layers == 1 ? "" : "s", kCanvasWidth,
                kCanvasHeight);
    std::printf("  zoom  step  margin(scr px)   full refresh   composite   "
                "pan: med / worst / stalled    zoom-drag: med / worst\n");

    for (double zoom : kZooms) {
        CanvasWidget& canvas = fixture.canvas;
        canvas.resetView();
        canvas.setZoom(zoom, QPointF(kCanvasWidth / 2.0, kCanvasHeight / 2.0));
        canvas.grab();  // settle the cache before anything is timed

        const int step = canvas.cacheStep();
        const PixelRect cached = canvas.cachedRegion();

        std::vector<double> full;
        QElapsedTimer clock;
        for (int i = 0; i < 7; ++i) {
            canvas.refreshAll();
            clock.start();
            canvas.grab();
            full.push_back(clock.nsecsElapsed() / 1e6);
        }

        const double composite = timeCompositeAlone(fixture.doc, fixture.track, fixture.image,
                                                    cached, step, 5);

        // How far the view can move before the cache runs out, in the units
        // that matter to a hand on a mouse: screen pixels. The cached region is
        // the visible one plus a margin each side, so what is spare either side
        // is half of however much wider it is than the window.
        const double margin_screen = (cached.width * zoom - kCanvasWidth) / 2.0;

        const DragCost pan = timePan(canvas);
        canvas.setZoom(zoom, QPointF(kCanvasWidth / 2.0, kCanvasHeight / 2.0));
        canvas.grab();
        const DragCost zoom_drag = timeZoomDrag(canvas, zoom);

        std::printf("  %4.2f  %4d  %9.0f     %8.2f ms  %8.2f ms   %6.2f /%7.2f /%5.0f%%"
                    "        %6.2f /%7.2f\n",
                    zoom, step, margin_screen, median(full), composite, pan.median_ms,
                    pan.worst_ms, pan.stall_percent, zoom_drag.median_ms, zoom_drag.worst_ms);
    }
}

// Where the cache stops sampling every pixel, for windows of different sizes.
// If that zoom moves with the window, then the sharpness of a stroke is not a
// function of the zoom the status bar is showing, and no amount of staring at
// the zoom percentage will explain it.
void cliffVersusWindowSize() {
    std::printf("\nthe zoom at which step goes 1 -> 2, for each window size\n");
    std::printf("  canvas widget     budget      step becomes 2 below\n");

    for (auto size : {std::pair{800, 500}, {1100, 640}, {1400, 700}, {1645, 765},
                      {2200, 1200}, {2560, 1400}}) {
        Document doc;
        const TrackId track = doc.addTrack("main");
        const ImageId image = doc.insertImage(track, 0);
        const LayerId layer = doc.addLayer(track, "layer 1");
        drawCurves(doc, track, image, layer, 3, 1920, 1080, 0x9e37u);

        CanvasWidget canvas(doc);
        canvas.resize(size.first, size.second);
        canvas.setTrack(track);
        canvas.setFrame(0);
        canvas.setActiveLayer(layer);

        double cliff = 0.0;
        for (int i = 0; i <= 600; ++i) {
            const double zoom = 0.40 + i * 0.001;
            canvas.resetView();
            canvas.setZoom(zoom, QPointF(size.first / 2.0, size.second / 2.0));
            canvas.grab();
            if (canvas.cacheStep() == 1) { cliff = zoom; break; }
        }
        std::printf("  %5d x %-5d   %9lld   %20.3f\n", size.first, size.second,
                    std::max<long long>(2'000'000, 2LL * size.first * size.second), cliff);
    }
}

// --- what the pipeline does to a curve -----------------------------------

// A circle is the right test shape and a straight line is not: the issue says
// the jaggedness shows on curves and not on orthogonal edges, which is exactly
// what a resampling artefact does. Rendered at whatever size is asked for, so
// the same circle can be produced as a ground truth at the display size and as
// a document to be resampled.
QImage circleAt(int side, double scale) {
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 3.0 * scale));
    painter.drawEllipse(QRectF(side * 0.1, side * 0.1, side * 0.8, side * 0.8));
    return image;
}

// Root-mean-square difference in levels, 0-255. Zero means the pipeline
// produced exactly what drawing the curve at that size would have produced;
// anything else is what the resampling cost.
double rmsAgainst(const QImage& a, const QImage& b) {
    const QImage x = a.convertToFormat(QImage::Format_Grayscale8);
    const QImage y = b.convertToFormat(QImage::Format_Grayscale8);
    const int w = std::min(x.width(), y.width());
    const int h = std::min(x.height(), y.height());
    double sum = 0.0;
    for (int j = 0; j < h; ++j) {
        const uchar* rx = x.constScanLine(j);
        const uchar* ry = y.constScanLine(j);
        for (int i = 0; i < w; ++i) {
            const double d = static_cast<double>(rx[i]) - static_cast<double>(ry[i]);
            sum += d * d;
        }
    }
    return std::sqrt(sum / std::max(1, w * h));
}

// The cache, as the compositor fills it: every `step`th pixel taken, nothing
// averaged. blendLayerRows does exactly this -- `local_x + i * step` -- so a
// step of 2 keeps one pixel in four and discards the rest.
QImage pointSampled(const QImage& source, int step) {
    if (step <= 1) return source;
    const int w = (source.width() + step - 1) / step;
    const int h = (source.height() + step - 1) / step;
    QImage out(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            out.setPixel(x, y, source.pixel(x * step, y * step));
        }
    }
    return out;
}

// The same reduction, but averaging the block instead of picking one of it.
// This is the box filter the compositor does not have.
QImage boxFiltered(const QImage& source, int step) {
    if (step <= 1) return source;
    const int w = (source.width() + step - 1) / step;
    const int h = (source.height() + step - 1) / step;
    QImage out(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            long long r = 0, g = 0, b = 0, n = 0;
            for (int dy = 0; dy < step; ++dy) {
                for (int dx = 0; dx < step; ++dx) {
                    const int sx = std::min(source.width() - 1, x * step + dx);
                    const int sy = std::min(source.height() - 1, y * step + dy);
                    const QRgb p = source.pixel(sx, sy);
                    r += qRed(p); g += qGreen(p); b += qBlue(p); ++n;
                }
            }
            out.setPixel(x, y, qRgb(static_cast<int>(r / n), static_cast<int>(g / n),
                                    static_cast<int>(b / n)));
        }
    }
    return out;
}

// One trip through the canvas's pipeline: sample the drawing into the cache at
// `step`, then blit the cache to the window with the filter the canvas picks.
QImage throughPipeline(const QImage& drawing, int step, double zoom, bool smooth_blit,
                       bool box) {
    const QImage cache = box ? boxFiltered(drawing, step) : pointSampled(drawing, step);
    const int side = static_cast<int>(std::lround(drawing.width() * zoom));
    return cache.scaled(side, side, Qt::IgnoreAspectRatio,
                        smooth_blit ? Qt::SmoothTransformation : Qt::FastTransformation);
}

void filterExperiment(const char* dump_dir) {
    constexpr int kSide = 720;
    const QImage drawing = circleAt(kSide, 1.0);

    std::printf("\nwhat the resampling costs, against drawing the same curve at the display"
                " size\n");
    std::printf("(RMS level error, 0-255; 0 would mean the pipeline lost nothing)\n");
    std::printf("  zoom  step  blit filter   as shipped   with a smooth blit   with a box"
                " filter\n");

    // Both the step and the filter are asked of the canvas rather than written
    // down here. A table of what the code used to do is worse than no table:
    // it goes stale silently and then argues with the thing it is measuring.
    Fixture probe(1, 3, 1920);
    for (double zoom : {0.40, 0.50, 0.60, 0.65, 0.68, 0.70, 0.72, 0.90, 1.00, 1.09, 1.20,
                        1.50, 2.00, 4.00}) {
        probe.canvas.resetView();
        probe.canvas.setZoom(zoom, QPointF(kCanvasWidth / 2.0, kCanvasHeight / 2.0));
        probe.canvas.grab();
        const int step = probe.canvas.cacheStep();
        const bool shipped_smooth = CanvasWidget::blitInterpolatesAt(step * zoom);

        const int side = static_cast<int>(std::lround(kSide * zoom));
        // Ground truth: the curve drawn at the size it is being displayed at.
        const QImage truth = circleAt(side, zoom);

        const double shipped =
            rmsAgainst(throughPipeline(drawing, step, zoom, shipped_smooth, false), truth);
        const double smooth_blit =
            rmsAgainst(throughPipeline(drawing, step, zoom, true, false), truth);
        const double boxed =
            rmsAgainst(throughPipeline(drawing, step, zoom, true, true), truth);

        std::printf("  %4.2f  %4d  %-11s   %8.1f   %17.1f   %16.1f\n", zoom, step,
                    shipped_smooth ? "smooth" : "nearest", shipped, smooth_blit, boxed);
    }

    if (!dump_dir) return;

    // Something to look at, since a number is not an edge. A number cannot be
    // argued with either, but a jagged edge is the complaint and the complaint
    // is visual. Each strip is a crop of the arc where it runs at roughly 45
    // degrees -- the worst case for a resampler and the place the issue says
    // the artefact shows -- magnified so the pixels are readable, and stacked
    // with a caption so the comparison is one image rather than six.
    const auto crop = [&](const QImage& image, double zoom) {
        // The same piece of the circle whatever size the image came out.
        const QRect box(static_cast<int>(image.width() * 0.72),
                        static_cast<int>(image.height() * 0.16),
                        static_cast<int>(image.width() * 0.16),
                        static_cast<int>(image.height() * 0.11));
        // Back to a common size first, so strips at different zooms are
        // comparable, then magnified with nearest so a pixel is a square.
        const QImage piece = image.copy(box);
        const int w = static_cast<int>(std::lround(piece.width() / zoom));
        const int h = static_cast<int>(std::lround(piece.height() / zoom));
        return piece.scaled(w * 5, h * 5, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    };

    // Owning, not a `const char*`: some of these captions are built from what
    // the canvas actually chose, and a QByteArray temporary would be gone
    // before the sheet was drawn.
    struct Strip { QString caption; QImage image; };
    const auto sheet = [&](const char* name, std::vector<Strip> strips) {
        if (strips.empty()) return;
        const int w = strips[0].image.width();
        const int h = strips[0].image.height();
        const int caption_h = 26;
        QImage out(w, (h + caption_h) * static_cast<int>(strips.size()), QImage::Format_RGB32);
        out.fill(QColor(245, 245, 247));
        QPainter painter(&out);
        painter.setFont(QFont("Segoe UI", 11));
        for (std::size_t i = 0; i < strips.size(); ++i) {
            const int top = static_cast<int>(i) * (h + caption_h);
            painter.setPen(QColor(30, 30, 34));
            painter.drawText(QRect(8, top, w - 8, caption_h), Qt::AlignVCenter,
                             strips[i].caption);
            painter.drawImage(0, top + caption_h, strips[i].image);
        }
        painter.end();
        out.save(QString("%1/%2.png").arg(dump_dir, name));
    };

    sheet("above-100-percent",
          {{"drawn at 109% (ground truth)",
            crop(circleAt(static_cast<int>(std::lround(kSide * 1.09)), 1.09), 1.09)},
           {"109% as shipped: nearest-neighbour blit  (RMS 6.0)",
            crop(throughPipeline(drawing, 1, 1.09, false, false), 1.09)},
           {"109% with a smooth blit  (RMS 3.4)",
            crop(throughPipeline(drawing, 1, 1.09, true, false), 1.09)}});

    sheet("across-the-step-cliff",
          {{"72%, step 1 as shipped  (RMS 2.2)",
            crop(throughPipeline(drawing, 1, 0.72, true, false), 0.72)},
           {"70%, step 2 as shipped: every 2nd pixel taken  (RMS 8.9)",
            crop(throughPipeline(drawing, 2, 0.70, true, false), 0.70)},
           {"70%, step 2 with the block averaged instead  (RMS 6.1)",
            crop(throughPipeline(drawing, 2, 0.70, true, true), 0.70)}});

    // And the same three zooms through the real widget rather than through a
    // simulation of it. A green build proves nothing about what is on screen;
    // this is the only strip here that has been near an actual CanvasWidget.
    {
        Document doc;
        const TrackId track = doc.addTrack("main");
        const ImageId image = doc.insertImage(track, 0);
        const LayerId layer = doc.addLayer(track, "layer 1");
        doc.setCanvasSize(1920, 1080);
        {
            // One big ellipse, like the drawing in the screenshots on the issue.
            ScopedCommand command(doc, "Ellipse");
            BrushSettings settings;
            settings.radius = 6.0f;
            settings.hardness = 0.6f;
            settings.pressure_affects_opacity = false;
            settings.a = 1.0f;
            Brush brush(settings);
            brush.begin(doc, track, image, layer, {1260.0f, 540.0f, 1.0f});
            for (int i = 1; i <= 240; ++i) {
                const double t = i * 2.0 * M_PI / 240.0;
                brush.extend({static_cast<float>(960 + 300 * std::cos(t)),
                              static_cast<float>(540 + 260 * std::sin(t)), 1.0f});
            }
            brush.end();
        }

        CanvasWidget canvas(doc);
        canvas.resize(kCanvasWidth, kCanvasHeight);
        canvas.setTrack(track);
        canvas.setFrame(0);
        canvas.setActiveLayer(layer);

        std::vector<Strip> strips;
        for (double zoom : {0.68, 1.00, 1.09}) {
            canvas.resetView();
            canvas.setZoom(zoom, QPointF(kCanvasWidth / 2.0, kCanvasHeight / 2.0));
            const QImage shot = canvas.grab().toImage();

            // The upper-right of the ellipse, where it runs at about 45 degrees
            // and a resampler has the most to get wrong. Found through the view
            // transform rather than by guessing at a fraction of the window:
            // resetView leaves the widget centre on image (W/2, H/2), and
            // setZoom keeps that point still, so the pan is what follows.
            const double pan_x = kCanvasWidth / 2.0 - kCanvasWidth / (2.0 * zoom);
            const double pan_y = kCanvasHeight / 2.0 - kCanvasHeight / (2.0 * zoom);
            const double at_x = (960 + 300 * std::cos(-M_PI / 4) - pan_x) * zoom;
            const double at_y = (540 + 260 * std::sin(-M_PI / 4) - pan_y) * zoom;
            const QRect box(static_cast<int>(at_x) - 75, static_cast<int>(at_y) - 45, 150, 90);
            strips.push_back({QString("the real canvas at %1%  (step %2, %3 blit)")
                                  .arg(static_cast<int>(std::lround(zoom * 100)))
                                  .arg(canvas.cacheStep())
                                  .arg(CanvasWidget::blitInterpolatesAt(canvas.cacheStep() * zoom)
                                           ? "smooth"
                                           : "nearest"),
                              shot.copy(box).scaled(150 * 4, 90 * 4, Qt::IgnoreAspectRatio,
                                                    Qt::FastTransformation)});
        }
        sheet("real-canvas", strips);
    }

    std::printf("\n  wrote comparison sheets to %s\n", dump_dir);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    sweep("a sparse drawing, like the screenshots on the issue", 1, 6, 1920);
    sweep("a full shot: four layers drawn over a wide field", 4, 90, 5000);
    cliffVersusWindowSize();
    filterExperiment(argc > 1 && argv[argc - 1][0] != '-' ? argv[argc - 1] : nullptr);

    std::printf("\nA frame at 60 Hz is 16.7 ms. A pan wants one of these per mouse move.\n");
    return 0;
}
