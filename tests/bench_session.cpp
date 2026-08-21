// SPDX-License-Identifier: GPL-3.0-or-later
//
// Not a test -- a stopwatch, for the report that panning goes heavy after a
// while: a session that starts smooth is laggy to pan an hour later, and
// closing the project and opening the same folder again makes it smooth.
//
// That last sentence is the whole shape of the question. The drawings are
// identical either side of the reopen, so nothing about the *picture* can be
// what got slow -- it has to be something the session accumulates and a load
// throws away. The history is the obvious candidate and it is the one the
// report already suspected, so this measures it rather than arguing about it.
//
// It drives the real CanvasWidget offscreen, the same way bench_zoom does, and
// asks one question repeatedly:
//
//     what does a pan drag cost after N strokes?
//
// with N growing, and with the history's size, the document's size and the
// process's memory read off beside it. Then it does the two things that tell
// retention apart from wear:
//
//   - clearHistory(), which is what opening a project does to the history and
//     nothing else, and re-times the pan. If the cost comes back down, what
//     was holding it was the history being *there*.
//   - the same pan again after that, with the strokes still drawn, which is
//     the reopened project: same pixels, no history.
//
// Whatever it says, it says it in numbers, and the numbers are per pointer
// move rather than averaged over a drag -- a pan that is smooth for five moves
// and stalls on the sixth is felt as the stall.
//
// Run it by hand:
//
//   ./build/tests/bench_session -platform offscreen
//   ./build/tests/bench_session -platform offscreen --project "C:/path/to/the.animage"
//
// Options: --project FOLDER, --blocks N, --strokes N (per block), --zoom Z,
//          --onion N (drawings either side; 0, the default, is off),
//          --transforms N (baked one after another, the pan timed either side
//          of each -- the report says the pan was already heavy after one),
//          --lasso N (a selection of N points left up while the pan is timed).

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointF>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "brush.h"
#include "canvas_widget.h"
#include "compositor.h"
#include "document.h"
#include "project_io.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// After windows.h, which it needs.
#include <psapi.h>
#endif

using namespace animage;

namespace {

// The canvas widget in a maximised 1920x1080 window, measured off the
// screenshots on issue #10: the layers dock and the timeline take the rest.
// The same numbers as bench_zoom, so the two are comparable.
constexpr int kCanvasWidth = 1645;
constexpr int kCanvasHeight = 765;

// What the process is holding, which is the reading the history budget is
// really about -- historyBytes() counts the tiles the history pins and nothing
// else, and the report this bench exists for said 215 MB while the machine was
// clearly carrying more than that.
struct Memory {
    double private_mb = 0.0;
    double working_set_mb = 0.0;
};

Memory memoryNow() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        return {};
    }
    return {static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0),
            static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0)};
#else
    return {};
#endif
}

double median(std::vector<double> samples) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// What a drag costs, per pointer move. A median on its own hides the whole
// effect: most moves land inside the cached margin and cost nothing, and then
// one leaves it and pays for a full recomposite. What is felt is the spike and
// how often it comes, so both are reported. Straight from bench_zoom, for the
// same reason it is written that way there.
struct DragCost {
    double median_ms = 0.0;
    double worst_ms = 0.0;
    double stall_percent = 0.0;  // moves costing more than a 60 Hz frame
};

DragCost summarise(std::vector<double> samples) {
    if (samples.empty()) return {};
    const double worst = *std::max_element(samples.begin(), samples.end());
    const long long stalls =
        std::count_if(samples.begin(), samples.end(), [](double ms) { return ms > 16.7; });
    return {median(samples), worst,
            100.0 * static_cast<double>(stalls) / static_cast<double>(samples.size())};
}

// Middle-drag, through the real event handlers, so whatever
// ensureCacheCoversView decides on the way is included. Far enough to cross
// the 64-pixel margin many times over: 60 moves of 12 px is 720 px of travel,
// which is an ordinary one-second drag.
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

// A lasso of roughly `points` points, drawn through the real handlers.
//
// Because a selection is one of the few things a session keeps that opening the
// project again throws away, and because it is redrawn on every paint -- twice,
// once wide and once dashed -- over the *whole* widget whenever the whole
// widget is repainted, which is what a pan does and what a stroke deliberately
// does not. Its loop takes a point per image pixel travelled, so a lasso drawn
// while zoomed out has several times as many points as the same loop drawn
// close in.
void drawLasso(CanvasWidget& canvas, int points) {
    canvas.setTool(CanvasWidget::Tool::Lasso);

    const QPointF centre(kCanvasWidth / 2.0, kCanvasHeight / 2.0);
    const double radius = std::min(kCanvasWidth, kCanvasHeight) * 0.4;

    QMouseEvent press(QEvent::MouseButtonPress, centre + QPointF(radius, 0.0),
                      centre + QPointF(radius, 0.0), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&canvas, &press);

    for (int i = 1; i <= points; ++i) {
        const double t = i * 2.0 * M_PI / points;
        const QPointF at = centre + QPointF(radius * std::cos(t), radius * std::sin(t));
        QMouseEvent move(QEvent::MouseMove, at, at, Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(&canvas, &move);
    }

    const QPointF last = centre + QPointF(radius, 0.0);
    QMouseEvent release(QEvent::MouseButtonRelease, last, last, Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&canvas, &release);
    canvas.setTool(CanvasWidget::Tool::Brush);
}

// A full recomposite of the whole cached region, which is what a pan that
// leaves the margin pays for. Timed on its own so the pan's spike can be
// attributed rather than guessed at.
double timeFullRefresh(CanvasWidget& canvas, int repeats = 7) {
    std::vector<double> samples;
    QElapsedTimer clock;
    for (int i = 0; i < repeats; ++i) {
        canvas.refreshAll();
        clock.start();
        canvas.grab();
        samples.push_back(clock.nsecsElapsed() / 1e6);
    }
    return median(std::move(samples));
}

// The compositor's own share of that, on the region and step the canvas has
// settled on. Everything the refresh does beyond this is the sRGB conversion
// and Qt.
double timeCompositeAlone(const Document& doc, std::size_t slot, const PixelRect& region,
                          SampleStep step, int repeats = 7) {
    Compositor compositor;
    Framebuffer frame;
    compositor.compositeScene(doc, slot, region, frame, step, {});
    QElapsedTimer clock;
    clock.start();
    for (int i = 0; i < repeats; ++i) {
        compositor.compositeScene(doc, slot, region, frame, step, {});
    }
    return clock.nsecsElapsed() / 1e6 / repeats;
}

// One stroke, as a session records them: its own command, so it is its own
// undo step and pins the tiles it displaced exactly as a real one does.
//
// A short arc rather than a dot or a full-frame sweep. The handover puts a
// stroke at two to six tiles and a quarter to three quarters of a megabyte,
// and that is the shape being reproduced -- a session is thousands of small
// marks, not a few big ones.
void drawStroke(Document& doc, TrackId track, ImageId image, LayerId layer,
                const PixelRect& canvas, unsigned& state) {
    const auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>((state >> 8) & 0xffff) / 65535.0;
    };

    ScopedCommand command(doc, "Stroke");
    BrushSettings settings;
    settings.radius = 6.0f;
    settings.hardness = 0.6f;
    settings.pressure_affects_opacity = false;
    settings.r = settings.g = settings.b = 0.0f;
    settings.a = 1.0f;
    Brush brush(settings);

    const double cx = canvas.x + next() * canvas.width;
    const double cy = canvas.y + next() * canvas.height;
    const double length = 60.0 + next() * 180.0;
    const double angle = next() * 2.0 * M_PI;
    const double bend = (next() - 0.5) * 1.5;

    brush.begin(doc, track, image, layer, {static_cast<float>(cx), static_cast<float>(cy), 1.0f});
    for (int i = 1; i <= 24; ++i) {
        const double t = i / 24.0;
        const double a = angle + bend * t;
        brush.extend({static_cast<float>(cx + length * t * std::cos(a)),
                      static_cast<float>(cy + length * t * std::sin(a)), 1.0f});
    }
    brush.end();
}

struct Options {
    QString project;
    int blocks = 8;
    int strokes = 100;
    double zoom = 1.0;
    int onion = 0;
    int transforms = 0;
    int lasso = 0;
};

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const auto value = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if (std::strcmp(argv[i], "--project") == 0) {
            options.project = QString::fromLocal8Bit(value());
        } else if (std::strcmp(argv[i], "--blocks") == 0) {
            options.blocks = std::atoi(value());
        } else if (std::strcmp(argv[i], "--strokes") == 0) {
            options.strokes = std::atoi(value());
        } else if (std::strcmp(argv[i], "--zoom") == 0) {
            options.zoom = std::atof(value());
        } else if (std::strcmp(argv[i], "--onion") == 0) {
            options.onion = std::atoi(value());
        } else if (std::strcmp(argv[i], "--transforms") == 0) {
            options.transforms = std::atoi(value());
        } else if (std::strcmp(argv[i], "--lasso") == 0) {
            options.lasso = std::atoi(value());
        }
    }
    return options;
}

void printHeader() {
    std::printf(
        "\n  what      strokes   undo   history MB   cels   tiles   private MB   working MB   "
        "pan: med / worst / stalled     full refresh   composite\n");
}

void printRow(const char* what, long long strokes, const Document& doc, const DragCost& pan,
              double full, double composite) {
    const Memory memory = memoryNow();
    std::printf("  %-8s  %7lld  %5zu   %10.0f   %4zu   %5zu   %10.1f   %10.1f   "
                "%7.2f / %7.2f / %5.0f%%   %12.2f   %9.2f\n",
                what, strokes, doc.undoDepth(),
                static_cast<double>(doc.historyBytes()) / (1024.0 * 1024.0), doc.celCount(),
                doc.totalTileCount(), memory.private_mb, memory.working_set_mb, pan.median_ms,
                pan.worst_ms, pan.stall_percent, full, composite);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const Options options = parse(argc, argv);

    Document doc;
    TrackId track = kNoId;
    ImageId image = kNoId;
    LayerId layer = kNoId;

    if (!options.project.isEmpty()) {
        QString error;
        if (!ProjectIO::load(doc, options.project, &error)) {
            std::printf("could not open %s: %s\n", qPrintable(options.project),
                        qPrintable(error));
            return 1;
        }
        if (doc.scene().tracks.empty() || doc.scene().tracks.front().layers.empty()) {
            std::printf("%s has no track to draw on\n", qPrintable(options.project));
            return 1;
        }
        const Track& first = doc.scene().tracks.front();
        track = first.id;
        layer = first.layers.front().id;
        image = first.imageShownAt(0);
        std::printf("\nopened %s\n", qPrintable(options.project));
    } else {
        // Nothing to open: a project of the same shape as the one the report
        // came from -- one track, two layers, a few drawings -- so the bench
        // runs on a machine that has not got that folder.
        doc.setCanvasSize(1920, 1080);
        track = doc.addTrack("main");
        image = doc.insertImage(track, 0);
        layer = doc.addLayer(track, "line");
        doc.addLayer(track, "rough");
        doc.clearHistory();
        std::printf("\nno --project given: a synthetic one-track scene instead\n");
    }

    if (image == kNoId) {
        std::printf("no drawing at frame 0 to draw on\n");
        return 1;
    }

    CanvasWidget canvas(doc);
    canvas.resize(kCanvasWidth, kCanvasHeight);
    canvas.setTrack(track);
    canvas.setFrame(0);
    canvas.setActiveLayer(layer);
    if (options.onion > 0) {
        canvas.setOnion({options.onion, options.onion, 0.45f});
    }
    canvas.resetView();
    canvas.setZoom(options.zoom, QPointF(kCanvasWidth / 2.0, kCanvasHeight / 2.0));
    canvas.grab();  // settle the cache before anything is timed

    const PixelRect canvas_rect = doc.scene().canvas();
    std::printf("canvas %dx%d, %zu track(s), zoom %.0f%%, onion %d, widget %dx%d\n",
                canvas_rect.width, canvas_rect.height, doc.scene().tracks.size(),
                options.zoom * 100.0, options.onion, kCanvasWidth, kCanvasHeight);
    std::printf("%d blocks of %d strokes, a pan drag timed after each\n", options.blocks,
                options.strokes);
    printHeader();

    const auto measure = [&](const char* what, long long strokes) {
        canvas.resetView();
        canvas.setZoom(options.zoom, QPointF(kCanvasWidth / 2.0, kCanvasHeight / 2.0));
        canvas.grab();
        const DragCost pan = timePan(canvas);
        canvas.resetView();
        canvas.setZoom(options.zoom, QPointF(kCanvasWidth / 2.0, kCanvasHeight / 2.0));
        canvas.grab();
        const double full = timeFullRefresh(canvas);
        const double composite =
            timeCompositeAlone(doc, canvas.frame(), canvas.cachedRegion(), canvas.cacheStep());
        printRow(what, strokes, doc, pan, full, composite);
    };

    measure("start", 0);

    // A selection is session state: it is one of the few things a reopen
    // throws away, and the only one of them that is redrawn on every paint.
    if (options.lasso > 0) {
        std::printf("\n  with a lasso of %d points left up:\n", options.lasso);
        printHeader();
        drawLasso(canvas, options.lasso);
        std::printf("  (the loop kept %zu points)\n", canvas.selection().loop.size());
        measure("lasso", 0);
        canvas.clearSelection();
        measure("cleared", 0);
    }

    unsigned state = 0x9e3779b9u;
    long long drawn = 0;
    for (int block = 0; block < options.blocks; ++block) {
        for (int i = 0; i < options.strokes; ++i) {
            drawStroke(doc, track, image, layer, canvas_rect, state);
            // What the pen lifting does: the canvas throws its cache away and
            // repaints in full. Left out, the session would accumulate history
            // without ever exercising the path that is reported to be slow.
            canvas.refreshAll();
            canvas.grab();
            ++drawn;
        }
        measure("drawn", drawn);
    }

    // The report says the pan was already heavy after a transform, so the
    // transform gets its own phase rather than being folded into the strokes:
    // one gesture, baked, and the same pan timed either side of it. A rotation
    // and a scale together, which is the one that resamples -- a whole-pixel
    // nudge takes the exact path and is a different measurement.
    if (options.transforms > 0) {
        std::printf("\n  a transform each row -- rotate 7 degrees and scale to 110%%, baked:\n");
        printHeader();
        for (int i = 0; i < options.transforms; ++i) {
            const CanvasWidget::Refusal why = canvas.beginTransform();
            if (why != CanvasWidget::Refusal::None) {
                std::printf("  transform %d refused: %s\n", i + 1,
                            qPrintable(CanvasWidget::explain(why)));
                break;
            }
            Transform values = canvas.transformValues();
            values.rotation = 7.0;
            values.scale_x = 1.1;
            values.scale_y = 1.1;
            canvas.setTransformValues(values);

            // What panning costs while the float is still up, which is a
            // different composite entirely: the layer stands in for itself and
            // every paint draws the veil and the preview over the top.
            measure("live", drawn);

            if (!canvas.applyTransform()) {
                std::printf("  transform %d would not commit\n", i + 1);
                canvas.cancelTransform();
                break;
            }
            measure("baked", drawn);
        }
    }

    // What opening the project again does to the history, and nothing else it
    // does. The pixels below are the ones that were just drawn, so a pan that
    // comes back down here is a pan that was paying for the history's presence
    // rather than for the drawing.
    std::printf("\n  the same drawing, with the history dropped -- which is the reopen:\n");
    printHeader();
    doc.clearHistory();
    measure("cleared", drawn);

    // And once more, because the first reading after a clear is taken over a
    // heap that has only just been handed hundreds of megabytes back. If the
    // cost falls here and not above, what was slow was the allocator catching
    // up rather than the history.
    measure("again", drawn);

    return 0;
}
