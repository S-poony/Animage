// SPDX-License-Identifier: GPL-3.0-or-later
//
// Not a test -- a stopwatch, for issue #10: strokes go jagged at some zoom
// levels and not others, and navigating while zoomed out lags. Both had been
// argued about and neither had been timed, so this drives the real
// CanvasWidget offscreen and reads the answers off it.
//
// It is also where issue #11 was settled: holding the cache at one entry per
// screen pixel instead of per image pixel, so that the sampling ratio follows
// zoom continuously and the block under an entry is averaged rather than
// point-sampled. The tables below are what says whether that worked.
//
// Three questions, in order:
//   1. What is cache_step_ actually doing as zoom varies? That is what decides
//      how much of the drawing survives to the screen, and it used to jump by
//      a factor of two at a zoom a memory budget happened to pick.
//   2. What does a full refresh, a cached repaint and a pan each cost at each
//      zoom, and how much of that is the compositor rather than everything
//      round it?
//   3. Which resampling filter is on when, and what does it do to an edge?
//
// Run it by hand:  ./build/tests/bench_zoom -platform offscreen

#include <QGuiApplication>
#include "canvas_view.h"
using CanvasWidget = CanvasView;
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
         {
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
    QGuiApplication::sendEvent(&canvas, &press);

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(moves));
    QElapsedTimer clock;
    for (int i = 1; i <= moves; ++i) {
        const QPointF at = start + QPointF(i * pixels_per_move, i * pixels_per_move * 0.5);
        QMouseEvent move(QEvent::MouseMove, at, at, Qt::NoButton, Qt::MiddleButton,
                         Qt::NoModifier);
        clock.start();
        QGuiApplication::sendEvent(&canvas, &move);
        canvas.grab();  // the repaint the move asked for
        samples.push_back(clock.nsecsElapsed() / 1e6);
    }

    QMouseEvent release(QEvent::MouseButtonRelease, start, start, Qt::MiddleButton, Qt::NoButton,
                        Qt::NoModifier);
    QGuiApplication::sendEvent(&canvas, &release);
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
                          const PixelRect& region, SampleStep step, int repeats) {
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
    std::printf("  zoom   step  entries  margin(scr px)   full refresh   composite   "
                "pan: med / worst / stalled    zoom-drag: med / worst\n");

    for (double zoom : kZooms) {
        CanvasWidget& canvas = fixture.canvas;
        canvas.resetView();
        canvas.setZoom(zoom, QPointF(kCanvasWidth / 2.0, kCanvasHeight / 2.0));
        canvas.grab();  // settle the cache before anything is timed

        const SampleStep step = canvas.cacheStep();
        const PixelRect cached = canvas.cachedRegion();
        // Read here, before the drags below move the view: the whole claim of
        // issue #11 is that this number stops depending on the zoom.
        const long long entries = canvas.cacheEntryCount();

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

        std::printf("  %4.2f  %5.2f  %6.2fM  %9.0f     %8.2f ms  %8.2f ms   "
                    "%6.2f /%7.2f /%5.0f%%        %6.2f /%7.2f\n",
                    zoom, step.ratio(), entries / 1e6, margin_screen,
                    median(full), composite, pan.median_ms, pan.worst_ms, pan.stall_percent,
                    zoom_drag.median_ms, zoom_drag.worst_ms);
    }
}

// The sampling ratio has to be a function of the zoom the status bar is showing
// and of nothing else. It used to also be a function of the window size,
// because a memory budget scaled with the window and the budget was what
// decided when the integer step doubled: the boundary sat at 50% in an 800x500
// canvas and 70.7% on anything large, so the same percentage meant different
// sharpness in different windows. That is exactly why the relationship looked
// non-linear and unexplainable.
//
// Two things are looked for here. The largest jump anywhere in a fine sweep --
// a step of any size shows up as one -- and whether the answer moves with the
// window at all.
void samplingVersusWindowSize() {
    std::printf("\nimage pixels per cache entry, and how that varies with the window\n");
    std::printf("  canvas widget     at 40%%   at 60%%   at 68%%   at 72%%   at 100%%   "
                "largest jump between adjacent zooms\n");

    for (auto size : {std::pair{800, 500}, {1100, 640}, {1400, 700}, {1645, 765},
                      {2200, 1200}, {2560, 1400}}) {
        Document doc;
        const TrackId track = doc.addTrack("main");
        const ImageId image = doc.insertImage(track, 0);
        const LayerId layer = doc.addLayer(track, "layer 1");
        drawCurves(doc, track, image, layer, 3, 1920, 1080, 0x9e37u);

        CanvasWidget canvas;
    canvas.setDocument(&doc);
        canvas.resize(size.first, size.second);
        canvas.setTrack(track);
        canvas.setFrame(0);
        canvas.setActiveLayer(layer);

        const auto ratioAt = [&](double zoom) {
            canvas.resetView();
            canvas.setZoom(zoom, QPointF(size.first / 2.0, size.second / 2.0));
            canvas.grab();
            return canvas.cacheStep().ratio();
        };

        double worst_jump = 1.0;
        double previous = 0.0;
        for (int i = 0; i <= 700; ++i) {
            const double ratio = ratioAt(0.30 + i * 0.001);
            if (previous > 0.0) {
                worst_jump = std::max(worst_jump, std::max(ratio / previous, previous / ratio));
            }
            previous = ratio;
        }

        std::printf("  %5d x %-5d   %6.2f   %6.2f   %6.2f   %6.2f   %7.2f   %31.3fx\n",
                    size.first, size.second, ratioAt(0.40), ratioAt(0.60), ratioAt(0.68),
                    ratioAt(0.72), ratioAt(1.00), worst_jump);
    }
}

// --- what the pipeline does to a curve -----------------------------------

// The drawing the resampling experiment works on, in image pixels.
constexpr int kDrawingSide = 720;

// A circle is the right test shape and a straight line is not: the issue says
// the jaggedness shows on curves and not on orthogonal edges, which is exactly
// what a resampling artefact does. Rendered at whatever size is asked for, so
// the same circle can be produced as a ground truth at the display size and as
// a document to be resampled.
//
// The geometry comes from the drawing scaled by `scale`, not from the size of
// the image it is being put in, and those are not the same thing: the pipeline
// covers a fraction of a pixel less than the whole drawing, so its scale is not
// the round number the image size is. Deriving the truth's geometry from the
// rounded size instead slid it a third of a pixel away from the pipeline by the
// far edge, and charged that to the filter.
QImage circleAt(int side, double scale) {
    const double extent = kDrawingSide * scale;
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 3.0 * scale));
    painter.drawEllipse(QRectF(extent * 0.1, extent * 0.1, extent * 0.8, extent * 0.8));
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

// How much of image pixel `at` -- standing for the `stride` pixels from there
// -- falls inside entry `entry`. The compositor's weighting, on the
// compositor's own grid, so this cannot drift into measuring something else.
double shareOf(const SampleStep& step, long long entry, int at, int stride, int extent) {
    const double lower = std::max<double>(at, static_cast<double>(step.entryTop(entry)) /
                                                  SampleStep::kOne);
    const double upper = std::min<double>(std::min(at + stride, extent),
                                          static_cast<double>(step.entryTop(entry + 1)) /
                                              SampleStep::kOne);
    return std::max(0.0, upper - lower);
}

// The cache as the compositor fills it: each entry the weighted mean of the
// block it covers, read on the same bounded lattice, with a sample that
// straddles an entry boundary split between the two.
QImage reduced(const QImage& source, const SampleStep& step, bool weighted) {
    if (step.isOne()) return source;
    const int w = step.entriesAcross(0, source.width());
    const int h = step.entriesAcross(0, source.height());
    const int stride = boxSampleStride(step);
    QImage out(w, h, QImage::Format_RGB32);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double r = 0.0, g = 0.0, b = 0.0, total = 0.0;
            const int y0 = std::max(0, step.entryBegin(y) - stride);
            const int x0 = std::max(0, step.entryBegin(x) - stride);
            for (int sy = (y0 / stride) * stride; sy < source.height(); sy += stride) {
                const double wy = shareOf(step, y, sy, stride, source.height());
                if (wy <= 0.0) {
                    if (sy > step.entryBegin(y + 1)) break;
                    continue;
                }
                for (int sx = (x0 / stride) * stride; sx < source.width(); sx += stride) {
                    const double wx = shareOf(step, x, sx, stride, source.width());
                    if (wx <= 0.0) {
                        if (sx > step.entryBegin(x + 1)) break;
                        continue;
                    }
                    // Unweighted is the reduction with the entry boundaries
                    // rounded to whole pixels: every sample that touches the
                    // entry counted equally. It is what a box filter looks like
                    // before the split is weighted, and it is worth a column.
                    const double weight = weighted ? wx * wy : 1.0;
                    const QRgb p = source.pixel(sx, sy);
                    r += qRed(p) * weight;
                    g += qGreen(p) * weight;
                    b += qBlue(p) * weight;
                    total += weight;
                }
            }
            if (total <= 0.0) { out.setPixel(x, y, source.pixel(0, 0)); continue; }
            out.setPixel(x, y, qRgb(static_cast<int>(std::lround(r / total)),
                                    static_cast<int>(std::lround(g / total)),
                                    static_cast<int>(std::lround(b / total))));
        }
    }
    return out;
}

// The same grid, but taking one pixel of the block instead of averaging it.
// This is what the compositor did before issue #11, and it is the column the
// filter has to be measured against.
QImage pointSampled(const QImage& source, const SampleStep& step) {
    if (step.isOne()) return source;
    const int w = step.entriesAcross(0, source.width());
    const int h = step.entriesAcross(0, source.height());
    QImage out(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            out.setPixel(x, y, source.pixel(std::min(source.width() - 1, step.entryBegin(x)),
                                            std::min(source.height() - 1, step.entryBegin(y))));
        }
    }
    return out;
}

enum class Reduction { Point, UnweightedBox, WeightedBox };

// One trip through the canvas's pipeline: reduce the drawing into the cache at
// `step`, then blit the cache to the window with the filter the canvas picks.
//
// Screen pixels per image pixel that the pipeline actually applies, which is
// not quite the zoom asked for. A whole number of entries covers entryEdge(n)
// image pixels, and the canvas snaps the view so that those entries land on
// whole screen pixels; both the drawing and the ground truth have to be
// measured against that rather than against the number in the status bar, or
// the comparison slides a black curve a fraction of a pixel across white paper
// and charges the pipeline several RMS for the measurement's own rounding.
double displayScale(const QImage& drawing, const SampleStep& step, double zoom) {
    const double covered = step.entryEdge(step.entriesAcross(0, drawing.width()));
    return std::max(1.0, std::round(covered * zoom)) / covered;
}

// One trip through the canvas's pipeline: reduce the drawing into the cache at
// `step`, then blit the cache to the window with the filter the canvas picks.
// Below 100% zoom that blit is a copy, because an entry is a screen pixel.
QImage throughPipeline(const QImage& drawing, const SampleStep& step, double scale,
                       bool smooth_blit, Reduction how) {
    const QImage cache = (how == Reduction::Point)
                             ? pointSampled(drawing, step)
                             : reduced(drawing, step, how == Reduction::WeightedBox);
    const double covered = step.entryEdge(cache.width()) * scale;
    const int side = static_cast<int>(std::lround(drawing.width() * scale));

    QImage out(side, side, QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth_blit);
    painter.drawImage(QRectF(0.0, 0.0, covered, covered), cache);
    painter.end();
    return out;
}

void filterExperiment(const char* dump_dir) {
    constexpr int kSide = kDrawingSide;
    const QImage drawing = circleAt(kSide, 1.0);

    std::printf("\nwhat the resampling costs, against drawing the same curve at the display"
                " size\n");
    std::printf("(RMS level error, 0-255; 0 would mean the pipeline lost nothing)\n");
    std::printf("  zoom   step  blit filter   as shipped   point-sampled   box, boundaries"
                " rounded\n");

    // Both the step and the filter are asked of the canvas rather than written
    // down here. A table of what the code used to do is worse than no table:
    // it goes stale silently and then argues with the thing it is measuring.
    Fixture probe(1, 3, 1920);
    for (double zoom : {0.40, 0.50, 0.55, 0.60, 0.65, 0.68, 0.70, 0.72, 0.90, 1.00, 1.09,
                        1.20, 1.50, 2.00, 4.00}) {
        probe.canvas.resetView();
        probe.canvas.setZoom(zoom, QPointF(kCanvasWidth / 2.0, kCanvasHeight / 2.0));
        probe.canvas.grab();
        const SampleStep step = probe.canvas.cacheStep();
        const bool shipped_smooth = CanvasWidget::blitInterpolatesAt(step.ratio() * zoom);

        const double scale = displayScale(drawing, step, zoom);
        const int side = static_cast<int>(std::lround(kSide * scale));
        // Ground truth: the curve drawn at the size it is being displayed at.
        const QImage truth = circleAt(side, scale);

        const double shipped = rmsAgainst(
            throughPipeline(drawing, step, scale, shipped_smooth, Reduction::WeightedBox), truth);
        const double sampled = rmsAgainst(
            throughPipeline(drawing, step, scale, shipped_smooth, Reduction::Point), truth);
        const double rounded = rmsAgainst(
            throughPipeline(drawing, step, scale, shipped_smooth, Reduction::UnweightedBox),
            truth);

        std::printf("  %4.2f  %5.2f  %-11s   %8.1f   %13.1f   %24.1f\n", zoom, step.ratio(),
                    shipped_smooth ? "smooth" : "nearest", shipped, sampled, rounded);
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

    const SampleStep one;
    sheet("above-100-percent",
          {{"drawn at 109% (ground truth)",
            crop(circleAt(static_cast<int>(std::lround(kSide * 1.09)), 1.09), 1.09)},
           {"109% before issue #10: nearest-neighbour blit  (RMS 6.0)",
            crop(throughPipeline(drawing, one, 1.09, false, Reduction::Point), 1.09)},
           {"109% as shipped, with a smooth blit  (RMS 3.4)",
            crop(throughPipeline(drawing, one, 1.09, true, Reduction::Point), 1.09)}});

    // 50% is where the point sample and the average part company most clearly:
    // two image pixels an entry, so a box filter is the exact reconstruction of
    // one screen pixel and point sampling throws three quarters of the ink away.
    const SampleStep half = SampleStep::fromRatio(2.0);
    sheet("across-the-step-cliff",
          {{"drawn at 50% (ground truth)",
            crop(circleAt(static_cast<int>(std::lround(kSide * 0.50)), 0.50), 0.50)},
           {"50% before issue #11: every 2nd pixel taken  (RMS 8.6)",
            crop(throughPipeline(drawing, half, 0.50, true, Reduction::Point), 0.50)},
           {"50% as shipped, with the block averaged  (RMS 0.1)",
            crop(throughPipeline(drawing, half, 0.50, true, Reduction::WeightedBox), 0.50)}});

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

        CanvasWidget canvas;
    canvas.setDocument(&doc);
        canvas.resize(kCanvasWidth, kCanvasHeight);
        canvas.setTrack(track);
        canvas.setFrame(0);
        canvas.setActiveLayer(layer);

        std::vector<Strip> strips;
        for (double zoom : {0.50, 0.68, 1.00, 1.09}) {
            canvas.resetView();
            canvas.setZoom(zoom, QPointF(kCanvasWidth / 2.0, kCanvasHeight / 2.0));
            const QImage shot = canvas.grab();

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
            strips.push_back({QString("the real canvas at %1%  (%2 image px an entry, %3 blit)")
                                  .arg(static_cast<int>(std::lround(zoom * 100)))
                                  .arg(canvas.cacheStep().ratio(), 0, 'f', 2)
                                  .arg(CanvasWidget::blitInterpolatesAt(canvas.cacheStep().ratio() * zoom)
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
    QGuiApplication app(argc, argv);

    sweep("a sparse drawing, like the screenshots on the issue", 1, 6, 1920);
    sweep("a full shot: four layers drawn over a wide field", 4, 90, 5000);
    samplingVersusWindowSize();
    filterExperiment(argc > 1 && argv[argc - 1][0] != '-' ? argv[argc - 1] : nullptr);

    std::printf("\nA frame at 60 Hz is 16.7 ms. A pan wants one of these per mouse move.\n");
    return 0;
}
