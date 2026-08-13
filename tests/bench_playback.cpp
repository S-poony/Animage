// SPDX-License-Identifier: GPL-3.0-or-later
//
// What playback costs, and -- the number that matters -- what it drops.
//
// Playback is driven by a clock and not by counting ticks: MainWindow::
// onPlaybackTick works the slot out from elapsed time, so a paint that overruns
// does not make the take run slow, it makes the frames underneath it never
// appear. That is the right design and it is why this benchmark exists. A pan
// that stutters reports itself -- you feel it in your hand and you complain. A
// playback that drops every third frame looks like the *drawing* is wrong, and
// the animator goes and fixes a breakdown that was fine. Judging timing is the
// whole purpose of playback, so the one thing that can quietly corrupt that
// judgement should not be the one thing with no instrument on it.
//
// Two decisions about how it measures, both of which are this file's own answer
// to lessons the handover already records.
//
// **It drives MainWindow and not CanvasWidget.** bench_zoom drives the canvas,
// which is the right scope for a zoom and the wrong one for a frame change: a
// playback frame is a slot change through the timeline, refreshLayerFlags and
// syncStatus in onSlotChanged, a full-cache canvas repaint -- setFrame calls
// refreshAll, so a playback frame is never a partial refresh -- and the timeline
// repainting its playhead. Timing only the canvas would be the bench_composite
// mistake one function further out, which is exactly how the 37 ms conversion
// loop went unnoticed for years. The three are reported apart so the answer says
// *where*, not only *how much*.
//
// **It reports frames shown, not milliseconds.** A median hides this completely:
// twenty-three good frames and one that takes three budgets is a visible hitch
// and a fine average. And a frame over budget does not drop one frame, it eats
// the ones underneath it -- so the drops are modelled by walking the clock the
// way onPlaybackTick does rather than by counting frames over 41 ms.
//
// Two passes, and they have to agree.
//
//   1. Deterministic: setCurrentSlot then grab, per frame, timed synchronously.
//      Reproducible, comparable between runs, and what you optimise against.
//   2. The real thing: trigger Play, spin the event loop, count how many slots
//      actually changed against how many the clock passed through.
//
// The second is a cross-check on the first and is labelled as one. If it reports
// no drops where the first predicts many, the paints are not reaching the
// backing store offscreen and the real pass is timing the timer alone -- so
// believe the first and say so, rather than believing the flattering number.
// The disagreement is the signal; there is no separate way to count paints.
//
// The fixture is drawn as closed shapes with a gap in one wall. That is
// realistic line art and it is the case the solver exists for, but the reason it
// is load-bearing here is the coloured pass: what playback composites is the
// *fill*, and a scribble that has no region to win gets cut close around itself
// by the hard rim and fills almost nothing. A fixture like that would report
// that colour costs nothing. So the fill coverage is printed with the fixture --
// if it reads a few per cent, or nearly everything, the shapes have stopped
// being the shapes they were meant to be and the coloured row means nothing.
//
// Run it by hand:  ./build/tests/bench_playback -platform offscreen

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointF>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "brush.h"
#include "canvas_widget.h"
#include "document.h"
#include "main_window.h"
#include "shortcuts.h"
#include "timeline_widget.h"

using namespace animage;

namespace {

// --- the shot to play ------------------------------------------------------

struct Case {
    const char* name;
    int canvas_width;
    int canvas_height;
    int window_width;
    int window_height;
    int tracks;
    int drawings;  // per track
    int hold;      // frames each drawing is held for
    int shapes;    // per drawing
};

// The display half of a frame is per *output* pixel, so it follows the window;
// the compositing half reads the drawing, so it follows the canvas. A 4K
// document on an HD monitor is therefore a different and cheaper case than the
// one below, which is somebody working at 4K on a 4K screen.
const std::vector<Case> kCases = {
    {"a shot you would review", 1920, 1080, 1920, 1080, 2, 24, 2, 5},
    {"the same at 4K", 3840, 2160, 3840, 2160, 2, 24, 2, 5},
    {"four tracks, 96 frames", 1920, 1080, 1920, 1080, 4, 48, 2, 5},
};

constexpr int kFps = 24;

double median(std::vector<double> samples) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

double percentile(std::vector<double> samples, double fraction) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const std::size_t at = std::min(samples.size() - 1,
                                    static_cast<std::size_t>(fraction * samples.size()));
    return samples[at];
}

// How many of these frames would actually be shown at `fps`.
//
// Modelled the way onPlaybackTick behaves rather than by counting frames over
// budget, because those are different numbers. The slot to show is worked out
// from the clock, so a frame costing three budgets is not one drop -- the two
// underneath it are never asked for at all. And a frame that comes in under
// budget does not buy time back: the tick returns early until the next boundary,
// which is what makes playback run at the speed it says it does.
// The slot is carried and never recovered from the clock by dividing, which is
// what the first version did and it reported 53 frames shown out of 48. Landing
// the clock exactly on a boundary and asking which boundary that was is a
// floating-point question: (slot + 1) * budget / budget comes back a hair under
// slot + 1, floor takes it to slot, and the frame is counted a second time. The
// cross-check against the real timer is what caught it -- it said 47 of 47 where
// this said 53 of 48, and the flattering column was this one.
int shownAt(const std::vector<double>& costs_ms, int fps) {
    const double budget = 1000.0 / static_cast<double>(fps);
    double clock = 0.0;
    std::size_t slot = 0;
    int shown = 0;
    while (slot < costs_ms.size()) {
        clock += costs_ms[slot];
        ++shown;
        // Where the clock got to, and never backwards: a frame that came in
        // under budget does not buy time back, because onPlaybackTick returns
        // early until it crosses the next boundary.
        const auto reached = static_cast<std::size_t>(std::floor(clock / budget));
        slot = std::max(slot + 1, reached);
    }
    return shown;
}

// --- drawing the fixture ---------------------------------------------------

void strokeOn(Document& doc, TrackId track, ImageId image, LayerId layer, float x0, float y0,
              float x1, float y1, float radius, float r, float g, float b, bool label) {
    ScopedCommand command(doc, "Stroke");
    BrushSettings settings;
    settings.radius = radius;
    settings.hardness = 0.9f;
    settings.pressure_affects_opacity = false;
    settings.label = label;
    settings.r = r;
    settings.g = g;
    settings.b = b;
    settings.a = 1.0f;
    Brush brush(settings);
    brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
    brush.extend({x1, y1, 1.0f});
    brush.end();
}

// One shape: a box with a gap in the bottom wall, which is line art rather than
// geometry -- a closed region is what a paint bucket already does and the gap is
// what LazyBrush is for. Kept modest on purpose. Too wide and the fill escapes
// and covers the canvas, which overstates the coloured cost exactly as an
// unfillable shape understates it.
struct Box {
    float left, top, width, height;

    void draw(Document& doc, TrackId track, ImageId image, LayerId ink) const {
        const float r = left + width;
        const float b = top + height;
        const float gap = std::min(40.0f, width * 0.12f);
        const float gap_from = left + width * 0.5f - gap * 0.5f;
        const float gap_to = left + width * 0.5f + gap * 0.5f;
        strokeOn(doc, track, image, ink, left, top, r, top, 3.0f, 0, 0, 0, false);
        strokeOn(doc, track, image, ink, left, top, left, b, 3.0f, 0, 0, 0, false);
        strokeOn(doc, track, image, ink, r, top, r, b, 3.0f, 0, 0, 0, false);
        strokeOn(doc, track, image, ink, left, b, gap_from, b, 3.0f, 0, 0, 0, false);
        strokeOn(doc, track, image, ink, gap_to, b, r, b, 3.0f, 0, 0, 0, false);
    }

    // Inside, well clear of the walls: where a scribble goes, and the region it
    // is meant to win.
    QPointF centre() const { return {left + width * 0.5, top + height * 0.5}; }
};

// The shapes on one drawing of one track. They move down the shot, because
// consecutive drawings that are identical are not what a shot looks like and
// would let every cache in the program off the hook.
std::vector<Box> boxesFor(const Case& shot, int track_index, int drawing) {
    std::vector<Box> boxes;
    const auto w = static_cast<float>(shot.canvas_width);
    const auto h = static_cast<float>(shot.canvas_height);
    const float side = h * 0.22f;
    const float drift = static_cast<float>(drawing) * w * 0.004f;
    const float row = h * (0.14f + 0.30f * static_cast<float>(track_index % 2));

    for (int i = 0; i < shot.shapes; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(std::max(1, shot.shapes));
        boxes.push_back({w * 0.06f + t * w * 0.80f + drift,
                         row + std::sin(t * 6.0f + static_cast<float>(drawing) * 0.4f) * h * 0.06f,
                         side, side});
    }
    return boxes;
}

// Lays `drawings` drawings down the track, each held `hold` frames.
//
// The first one may already be there: the document a window starts with has a
// track with a drawing in it, and using that rather than adding a second beside
// it is what keeps MainWindow's current track valid without reaching for a
// setter.
std::vector<ImageId> layDrawings(Document& doc, TrackId track, int drawings, int hold) {
    std::vector<ImageId> images;
    for (int d = 0; d < drawings; ++d) {
        const auto slot = static_cast<std::size_t>(d) * static_cast<std::size_t>(hold);
        const Track* before = doc.scene().findTrack(track);
        ImageId image =
            (before && slot < before->slots.size()) ? before->slots[slot] : kNoId;
        if (image == kNoId) image = doc.insertImage(track, slot);
        images.push_back(image);

        const Track* after = doc.scene().findTrack(track);
        const std::size_t have = after ? after->slots.size() : 0;
        const std::size_t want = slot + static_cast<std::size_t>(hold);
        if (have < want) doc.extendExposure(track, slot, static_cast<int>(want - have));
    }
    return images;
}

LayerId inkLayerOf(Document& doc, TrackId track) {
    const Track* t = doc.scene().findTrack(track);
    if (t && !t->layers.empty() && t->layers.front().kind == LayerKind::Raster) {
        return t->layers.front().id;
    }
    return doc.addLayer(track, "ink");
}

const Layer* findLayer(const Document& doc, TrackId track, LayerId layer) {
    const Track* t = doc.scene().findTrack(track);
    if (!t) return nullptr;
    for (const Layer& l : t->layers) {
        if (l.id == layer) return &l;
    }
    return nullptr;
}

struct Built {
    std::vector<TrackId> tracks;
    std::vector<LayerId> ink;
    std::vector<LayerId> colour;  // empty when the shot is line art only
};

// Builds the shot into the window's own document.
//
// Colour is one CTG layer per track, cut against that track's ink, scribbled on
// the *first* drawing only -- carrying is what colours the rest, which is both
// how a shot is actually coloured and what ctg_inherit is for.
Built buildShot(Document& doc, const Case& shot, bool coloured) {
    Built built;
    doc.setCanvasSize(shot.canvas_width, shot.canvas_height);
    doc.setFramerate(kFps);

    for (int t = 0; t < shot.tracks; ++t) {
        TrackId track = kNoId;
        if (t == 0 && !doc.scene().tracks.empty()) {
            track = doc.scene().tracks.front().id;
        } else {
            track = doc.addTrack("track " + std::to_string(t + 1));
        }
        built.tracks.push_back(track);

        const LayerId ink = inkLayerOf(doc, track);
        built.ink.push_back(ink);

        const std::vector<ImageId> images = layDrawings(doc, track, shot.drawings, shot.hold);
        for (int d = 0; d < static_cast<int>(images.size()); ++d) {
            for (const Box& box : boxesFor(shot, t, d)) {
                box.draw(doc, track, images[static_cast<std::size_t>(d)], ink);
            }
        }

        if (!coloured) continue;

        // The bottom of the stack, which is where a colour layer goes.
        const Track* resolved = doc.scene().findTrack(track);
        const std::size_t bottom = resolved ? resolved->layers.size() : 0;
        const LayerId ctg = doc.addLayer(track, "colour", bottom, LayerKind::Ctg);
        built.colour.push_back(ctg);
        if (const Layer* properties = findLayer(doc, track, ctg)) {
            Layer edited = *properties;
            edited.ctg_sources = {ink};
            doc.updateLayer(track, ctg, edited);
        }

        // Scribbled on the first drawing only. Inside each box, clear of the
        // walls, and short -- a mark is a seed and not a fill.
        const ImageId first = images.front();
        int index = 0;
        for (const Box& box : boxesFor(shot, t, 0)) {
            const QPointF at = box.centre();
            const float r = (index % 3 == 0) ? 0.85f : 0.15f;
            const float g = (index % 3 == 1) ? 0.75f : 0.20f;
            const float b = (index % 3 == 2) ? 0.80f : 0.25f;
            strokeOn(doc, track, first, ctg, static_cast<float>(at.x()) - box.width * 0.12f,
                     static_cast<float>(at.y()), static_cast<float>(at.x()) + box.width * 0.12f,
                     static_cast<float>(at.y()), 9.0f, r, g, b, true);
            ++index;
        }
    }
    return built;
}

// --- what the fixture actually produced ------------------------------------

// How much of the canvas the fills cover at one frame.
//
// The fixture's own check, and it has to be read with that drawing's fill freshly
// solved -- see coverageWhenSolved. Read off the cache after a whole shot has
// been walked and it answers a different question, because the cache is bounded
// and cannot hold one: the first version of this reported 0% at 4K and read as
// "the shapes are not closing" when what it had measured was eviction.
double fillCoverage(const Document& doc, const Built& built, std::size_t slot) {
    if (built.colour.empty()) return 0.0;
    const PixelRect canvas = doc.scene().canvas();
    long long covered = 0;
    long long total = 0;

    for (int y = canvas.y; y < canvas.y + canvas.height; y += 8) {
        for (int x = canvas.x; x < canvas.x + canvas.width; x += 8) {
            ++total;
            bool any = false;
            for (std::size_t t = 0; t < built.tracks.size() && !any; ++t) {
                const Track* track = doc.scene().findTrack(built.tracks[t]);
                if (!track) continue;
                const ImageId image = track->imageShownAt(slot);
                if (image == kNoId) continue;
                const CtgFill* fill =
                    doc.ctgFillFor(built.tracks[t], image, built.colour[t]);
                if (!fill) continue;
                any = fill->tiles.pixel(x, y).a > 0.5f;
            }
            covered += any ? 1 : 0;
        }
    }
    return total > 0 ? static_cast<double>(covered) / static_cast<double>(total) : 0.0;
}

// --- driving it ------------------------------------------------------------

// Solves every drawing's fill and installs it, one frame at a time.
//
// One frame at a time and not a sweep followed by one wait: the solver's rule is
// that the newest question wins, so walking the shot and waiting at the end
// would leave every solve but the last one called off. This is setup and is
// allowed to be slow -- it is a max-flow per drawing per colour layer.
void presolveColour(MainWindow& window, TimelineWidget& timeline, CanvasWidget& canvas) {
    const std::size_t frames = window.documentForTesting().scene().shotFrames();
    for (std::size_t slot = 0; slot < frames; ++slot) {
        timeline.setCurrentSlot(slot);
        canvas.grab();
        window.waitForColour();
    }
}

// Coverage with that one drawing's fill certainly present: stand on the frame,
// paint it, wait for the answer, then look. This is the question "do the shapes
// close", and it is deliberately asked apart from "does the fill survive to
// playback", which is the cache's question and is counted in solves instead.
double coverageWhenSolved(MainWindow& window, TimelineWidget& timeline, CanvasWidget& canvas,
                          const Built& built, std::size_t slot) {
    timeline.setCurrentSlot(slot);
    canvas.grab();
    window.waitForColour();
    canvas.grab();
    return fillCoverage(window.documentForTesting(), built, slot);
}

struct Deterministic {
    std::vector<double> frame;  // per slot, the representative pass
    double slot_ms = 0.0;
    double canvas_ms = 0.0;
    double timeline_ms = 0.0;
};

// One pass with nothing timed. The first frame off a cold cache pays for
// building it, and that is not what playback costs.
void warmPass(TimelineWidget& timeline, CanvasWidget& canvas, std::size_t frames) {
    for (std::size_t slot = 0; slot < frames; ++slot) {
        timeline.setCurrentSlot(slot);
        canvas.grab();
        timeline.grab();
    }
}

// One pass over the shot, timed synchronously. The three components are timed
// apart because the answer has to say where the frame went: the slot change is
// the bookkeeping onSlotChanged does, the canvas is the composite and the sRGB
// conversion, and the timeline is the playhead moving.
Deterministic timeDeterministic(TimelineWidget& timeline, CanvasWidget& canvas,
                                std::size_t frames, int passes) {
    std::vector<std::vector<double>> per_pass;
    std::vector<double> slot_all;
    std::vector<double> canvas_all;
    std::vector<double> timeline_all;

    QElapsedTimer clock;
    for (int pass = 0; pass < passes; ++pass) {
        std::vector<double> frame;
        frame.reserve(frames);
        for (std::size_t slot = 0; slot < frames; ++slot) {
            clock.start();
            timeline.setCurrentSlot(slot);
            const double slot_ms = clock.nsecsElapsed() / 1e6;

            clock.start();
            canvas.grab();
            const double canvas_ms = clock.nsecsElapsed() / 1e6;

            clock.start();
            timeline.grab();
            const double timeline_ms = clock.nsecsElapsed() / 1e6;

            slot_all.push_back(slot_ms);
            canvas_all.push_back(canvas_ms);
            timeline_all.push_back(timeline_ms);
            frame.push_back(slot_ms + canvas_ms + timeline_ms);
        }
        per_pass.push_back(std::move(frame));
    }

    // A representative shot rather than the best or the worst one: the drop
    // model needs a single pass in slot order, and the per-slot median across
    // passes is the one least likely to be somebody else's scheduler.
    Deterministic out;
    out.frame.reserve(frames);
    for (std::size_t slot = 0; slot < frames; ++slot) {
        std::vector<double> across;
        across.reserve(per_pass.size());
        for (const std::vector<double>& pass : per_pass) across.push_back(pass[slot]);
        out.frame.push_back(median(std::move(across)));
    }
    out.slot_ms = median(std::move(slot_all));
    out.canvas_ms = median(std::move(canvas_all));
    out.timeline_ms = median(std::move(timeline_all));
    return out;
}

struct Played {
    int shown = 0;    // slot changes: frames the program decided to show
    int painted = 0;  // canvas paints: frames it actually put up
    int expected = 0;
    bool ran = false;
};

// The real timer, the real event loop, and the slots that actually changed.
//
// Two counts, because they answer different questions and only one of them is
// about the program.
//
// `shown` is slot changes. onPlaybackTick returns early when the slot it works
// out is already current, so a frame skipped because the paint before it
// overran never becomes current and never emits -- that count is what playback
// *decided* to show, and it is the one the deterministic model predicts.
//
// `painted` is CanvasWidget::paintCount, which is what reached the screen. The
// two agree here, including offscreen, and that is worth knowing rather than
// assuming: a `shots` situation photographing this same readout showed 20 of 24
// and looked like the offscreen platform capping paints. It was the harness --
// it spun on `while (elapsed < ms) processEvents()`, which never lets the loop
// idle, and repaints are flushed on the idle pass. The nested loop below has
// always been right, which is how the difference was found.
//
// So a shortfall between the two is a real one. What it means is that two slot
// changes collapsed into one paint, which is what happens once playback starts
// overrunning.
Played playForReal(MainWindow& window, TimelineWidget& timeline, CanvasWidget& canvas, int fps,
                   int for_ms) {
    Played played;
    QAction* play = window.actionForTesting(shortcuts::Id::Play);
    if (!play) return played;

    int shown = 0;
    const auto counter = QObject::connect(&timeline, &TimelineWidget::currentSlotChanged,
                                          &timeline, [&shown](std::size_t) { ++shown; });

    const std::uint64_t painted_before = canvas.paintCount();
    QElapsedTimer clock;
    clock.start();
    play->trigger();

    QEventLoop loop;
    QTimer::singleShot(for_ms, &loop, &QEventLoop::quit);
    loop.exec();

    const qint64 elapsed = clock.elapsed();
    play->trigger();  // stop
    QObject::disconnect(counter);

    played.shown = shown;
    played.painted = static_cast<int>(canvas.paintCount() - painted_before);
    played.expected = static_cast<int>(elapsed * fps / 1000);
    played.ran = true;
    return played;
}

void run(const Case& shot, bool coloured, bool print_header) {
    MainWindow window;
    window.resize(shot.window_width, shot.window_height);
    window.show();
    QCoreApplication::processEvents();

    auto* timeline = window.findChild<TimelineWidget*>();
    auto* canvas = window.findChild<CanvasWidget*>();
    if (!timeline || !canvas) {
        std::printf("  could not find the timeline or the canvas in the window\n");
        return;
    }

    Document& doc = window.documentForTesting();
    const Built built = buildShot(doc, shot, coloured);
    timeline->refresh();
    QCoreApplication::processEvents();

    // Fit, which is where somebody reviewing a shot sits. Stated rather than
    // inherited, so the row says what zoom it was measured at.
    const double fit = std::min(static_cast<double>(canvas->width()) / shot.canvas_width,
                                static_cast<double>(canvas->height()) / shot.canvas_height);
    canvas->resetView();
    canvas->setZoom(fit, QPointF(canvas->width() / 2.0, canvas->height() / 2.0));
    canvas->grab();

    const std::size_t frames = doc.scene().shotFrames();

    if (coloured) presolveColour(window, *timeline, *canvas);

    if (print_header) {
        std::printf("\n%s\n", shot.name);
        std::printf("  %dx%d canvas, %d tracks, %d drawings on %ss = %zu frames, "
                    "canvas widget %dx%d at %.0f%%\n",
                    shot.canvas_width, shot.canvas_height, shot.tracks, shot.drawings,
                    std::to_string(shot.hold).c_str(), frames, canvas->width(),
                    canvas->height(), fit * 100.0);
        std::printf("                  %zu tiles\n", doc.totalTileCount());
        std::printf("                per frame: slot / canvas / timeline      "
                    "frame: med / p95 / worst      shown at %d fps    real timer\n",
                    kFps);
    }

    // Measured before the warm pass, because that is the fixture's question and
    // a whole walk of the shot is what evicts the answer.
    const double coverage =
        coloured ? coverageWhenSolved(window, *timeline, *canvas, built, frames / 2) : 0.0;

    warmPass(*timeline, *canvas, frames);

    const Deterministic timed = timeDeterministic(*timeline, *canvas, frames, 3);
    const int shown = shownAt(timed.frame, kFps);

    // Two seconds of it, which is long enough for a stall to show and short
    // enough that six of these do not become a coffee break.
    //
    // The solves are counted around *this* pass and not around the timed one,
    // and that is not a preference. A solve is asked for by a paint and
    // installed later, by a 16 ms poll -- so it needs an event loop, and the
    // deterministic pass has none: grab() paints without spinning one, nothing
    // is ever collected, and storeCount sits at zero however much solving the
    // shot deserves. Counted there it read 0.0 on a 4K shot holding twenty
    // fills out of forty-eight, which is the third instrument in this file to
    // report zero for a reason that was not the one it was measuring.
    const std::uint64_t solves_before = doc.ctgCache().storeCount();
    const Played played = playForReal(window, *timeline, *canvas, kFps, 2000);
    const std::uint64_t solves_after = doc.ctgCache().storeCount();

    char real[64];
    if (played.ran && played.expected > 0) {
        std::snprintf(real, sizeof(real), "%d of %d (%d painted)", played.shown,
                      played.expected, played.painted);
    } else {
        std::snprintf(real, sizeof(real), "did not run");
    }

    std::printf("  %-12s  %5.2f / %6.2f / %5.2f ms          %6.2f / %6.2f / %6.2f      "
                "%3d of %-3zu         %s\n",
                coloured ? "coloured" : "line art", timed.slot_ms, timed.canvas_ms,
                timed.timeline_ms, median(timed.frame), percentile(timed.frame, 0.95),
                *std::max_element(timed.frame.begin(), timed.frame.end()), shown, frames,
                real);

    if (coloured) {
        std::printf("  %-12s  fill covers %.0f%% of the canvas when solved%s\n", "",
                    coverage * 100.0,
                    (coverage < 0.05 || coverage > 0.90)
                        ? "  <-- the shapes are not filling as intended; this row means nothing"
                        : "");
        std::printf("  %-12s  %llu drawings, %zu fills held (%zu tiles), "
                    "%llu solves during 2 s of playback\n",
                    "", static_cast<unsigned long long>(shot.drawings * shot.tracks),
                    doc.ctgCache().size(), doc.ctgCache().tileCount(),
                    static_cast<unsigned long long>(solves_after - solves_before));
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    std::printf("What playback costs, and what it drops. A frame at %d fps is %.1f ms.\n", kFps,
                1000.0 / kFps);

    for (const Case& shot : kCases) {
        run(shot, false, true);
        run(shot, true, false);
    }

    std::printf("\nThe two right-hand columns have to agree on slots. The deterministic one is\n"
                "what to optimise against; the real timer is the cross-check, and it is the\n"
                "one that caught this file's own drop model counting 53 frames out of 48.\n"
                "\n"
                "The painted count is what reached the screen, and it agrees with the slot\n"
                "count here. A shortfall between them would mean two slot changes collapsed\n"
                "into one paint, which is what overrunning looks like from the inside.\n");
    return 0;
}
