// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_widget.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <thread>
#include <vector>

#include "color.h"

using namespace animage;

namespace {

constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 32.0;
constexpr int kCheckerSize = 8;
constexpr double kScrubbyZoomPerPixel = 0.006;
constexpr double kSizeDragPerPixel = 0.012;
// Cached beyond the viewport, so a small pan does not recomposite. Measured
// rather than guessed: at 192 the padding more than doubled the cost of every
// full refresh -- every frame change, every opacity tick -- to buy free panning
// nobody had asked for. 64 keeps most of the benefit for a third of the price.
constexpr int kCacheMargin = 64;
// How long after a tablet event a mouse event is still assumed to have been
// promoted from it by the platform rather than produced by a real mouse.
constexpr qint64 kPenMouseWindowMs = 250;

// Linear to sRGB through a lookup table. The conversion is per pixel of the
// viewport on every recomposite, and std::pow in that loop is measurable.
const std::array<quint8, 4096>& srgbTable() {
    static const std::array<quint8, 4096> table = [] {
        std::array<quint8, 4096> values{};
        for (int i = 0; i < 4096; ++i) {
            const float linear = static_cast<float>(i) / 4095.0f;
            const float encoded = std::clamp(linearToSrgb(linear), 0.0f, 1.0f);
            values[static_cast<std::size_t>(i)] =
                static_cast<quint8>(std::lround(encoded * 255.0f));
        }
        return values;
    }();
    return table;
}

quint8 toSrgbByte(float linear) {
    const int index = static_cast<int>(std::lround(std::clamp(linear, 0.0f, 1.0f) * 4095.0f));
    return srgbTable()[static_cast<std::size_t>(index)];
}

bool comesFromAStylus(const QPointingDevice* device) {
    if (!device) return false;
    const QInputDevice::DeviceType type = device->type();
    return type == QInputDevice::DeviceType::Stylus ||
           type == QInputDevice::DeviceType::Airbrush || type == QInputDevice::DeviceType::Puck;
}

// Threads cost about as much to start as a small band costs to convert, so
// only spread work that is worth spreading. The same shape as the
// compositor's own heuristic, and for the same reason.
int chooseWorkerCount(int rows, int columns) {
    const long long work = static_cast<long long>(rows) * columns;
    if (work < 64'000) return 1;
    const unsigned hardware = std::thread::hardware_concurrency();
    const int available = static_cast<int>(hardware ? hardware : 1u);
    return std::clamp(std::min(available, rows / 32), 1, 8);
}

PixelRect intersect(const PixelRect& a, const PixelRect& b) {
    const int x0 = std::max(a.x, b.x);
    const int y0 = std::max(a.y, b.y);
    const int x1 = std::min(a.x + a.width, b.x + b.width);
    const int y1 = std::min(a.y + a.height, b.y + b.height);
    if (x1 <= x0 || y1 <= y0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

bool contains(const PixelRect& outer, const PixelRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

// The view, moved to a whole number of screen pixels.
//
// This is what makes a screen-resolution cache worth having. Entry e of the
// cache begins at image x = e/zoom, which lands on a whole screen pixel exactly
// when pan * zoom is whole -- and then the cache blits one entry to one pixel,
// a copy rather than a resample. Off that alignment Qt filters the cache
// against itself at roughly 1:1, which is pure blur: measured at 4.2 RMS
// against a curve drawn at display size, where the aligned blit gives 1.7.
//
// Nothing is given up for it. A pan is a gesture in screen pixels to begin
// with, and the largest correction this can make is half of one.
QPointF onWholeScreenPixels(const QPointF& pan, double zoom) {
    if (zoom <= 0.0) return pan;
    return {std::round(pan.x() * zoom) / zoom, std::round(pan.y() * zoom) / zoom};
}

PixelRect unite(const PixelRect& a, const PixelRect& b) {
    if (a.isEmpty()) return b;
    if (b.isEmpty()) return a;
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.width, b.x + b.width);
    const int y1 = std::max(a.y + a.height, b.y + b.height);
    return {x0, y0, x1 - x0, y1 - y0};
}

}  // namespace

CanvasWidget::CanvasWidget(Document& document, QWidget* parent)
    : QWidget(parent), doc_(document) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TabletTracking);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);

    brush_settings_.radius = 6.0f;
    brush_settings_.hardness = 0.6f;
    brush_settings_.pressure_affects_opacity = true;
    brush_settings_.r = brush_settings_.g = brush_settings_.b = 0.0f;
    brush_settings_.a = 1.0f;

    eraser_settings_ = brush_settings_;
    eraser_settings_.radius = 18.0f;
    eraser_settings_.hardness = 0.85f;
    eraser_settings_.pressure_affects_opacity = false;
    eraser_settings_.erase = true;

    ctg_poll_ = new QTimer(this);
    ctg_poll_->setInterval(16);
    connect(ctg_poll_, &QTimer::timeout, this, &CanvasWidget::collectColour);

    clock_.start();
}

CanvasWidget::~CanvasWidget() {
    // Before anything else this object owns goes away. A solve holds only its
    // own copy of the drawing, so nothing here is unsafe -- but a max-flow that
    // has not been told the window is closing goes on running for a second or
    // two after it has, and the process stays up with nothing on screen.
    ctg_solver_.cancelAll();
}

void CanvasWidget::setTrack(TrackId track) {
    track_ = track;
    setFrame(slot_);
}

void CanvasWidget::setFrame(std::size_t slot) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;

    slot_ = std::min(slot, track->slots.empty() ? std::size_t{0}
                                                   : track->slots.size() - 1);
    const ImageId next = track->imageAtSlot(slot_);
    const bool changed = next != image_;
    image_ = next;

    // A stroke in progress when the frame changes carries on onto the new
    // drawing. Holding the pen down through playback then leaves a mark on
    // each frame it passed over, which is how you sketch a moving point.
    if (changed && stroking_) rebindStrokeToCurrentImage();

    // Marked, not rebuilt. Holding an arrow key produces frame changes faster
    // than a rebuild takes, and rebuilding on each one let the queue run away:
    // the playhead carried on moving for a while after the key came up.
    onion_dirty_ = true;
    refreshAll();
}

void CanvasWidget::setActiveLayer(LayerId layer) { active_layer_ = layer; }

void CanvasWidget::setEraser(bool erasing) { erasing_ = erasing; }

void CanvasWidget::setBrushColour(float r, float g, float b) {
    brush_settings_.r = r;
    brush_settings_.g = g;
    brush_settings_.b = b;
}

void CanvasWidget::setBackground(Background background) {
    background_ = background;
    refreshAll();
}

void CanvasWidget::setOnion(const OnionSettings& settings) {
    onion_settings_ = settings;
    onion_dirty_ = true;
    refreshAll();
}

void CanvasWidget::setPlaying(bool playing) {
    if (playing_ == playing) return;
    playing_ = playing;
    onion_dirty_ = true;
    refreshAll();
}

// Asks for any CTG fill whose scribbles or line art have moved to be solved
// somewhere else.
//
// Nothing is computed here. What was a max-flow inside a paint -- capped at
// about 512x512 precisely so that it could not stop the program -- is now a
// hash compared against the cached fill and, if they differ, a snapshot handed
// to a worker thread. The old fill stays on screen until the new one lands,
// which is the honest thing to show: it is what the drawing looked like a
// moment ago, and the alternative is the colour blinking out on every stroke.
//
// The bookkeeping in ctg_asked_ is what stops this asking again on the next
// paint for a solve that is already running. A widget repaints many times a
// second, and the solver's rule is that the newest question wins -- so without
// it, every paint would call off the answer the last paint was waiting for and
// no fill would ever be finished.
void CanvasWidget::requestCtgFills() {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || image_ == kNoId) return;

    // Never during a stroke, whichever layer is being drawn on. Every dab bumps
    // the cel's revision, so asking whenever a fill looked stale would start a
    // solve per dab -- and each one would supersede the last, so the pen would
    // be leaving a trail of abandoned max-flows and no fill at all.
    //
    // This used to test for a scribble in progress, which covered drawing on the
    // colour layer and missed drawing on the line art it is cut against. Inking
    // over a filled drawing therefore re-solved on every dab, and it is the same
    // trap either way: the solve belongs at the end of the stroke.
    if (stroking_) return;

    dropStaleColourRequests(/*only_this_drawing=*/true);

    const std::uint64_t generation = doc_.ctgCache().generation();
    const CtgSettings settings;
    for (const Layer& layer : track->layers) {
        if (layer.kind != LayerKind::Ctg || !layer.visible) continue;
        // Nothing to solve for a layer showing its scribbles: the fill would be
        // computed and then not drawn.
        if (layer.show_scribbles) continue;

        const CtgInputs wanted = ctgInputsFor(doc_, track_, image_, layer.id, settings);
        if (!wanted.valid) continue;

        // Coarse first, then as fine as the drawing deserves. The first answer
        // is bounded so that it arrives while the stroke that caused it is
        // still recent -- about a tenth of a second -- and the second is bounded
        // only by what a max-flow costs in memory, which at 1080p is full
        // resolution. Nothing waits for either.
        //
        // A fill is finished when it is as fine as it was asked for, or when it
        // already had the largest allowance there is. Without the second half a
        // drawing too large for even the full budget would be re-solved for
        // ever, arriving at the same coarse answer every time.
        const CtgFill* held = doc_.ctgFillFor(track_, image_, layer.id);
        const bool current = held && held->valid && held->inputs == wanted.hash;
        if (current && (held->step <= std::max(1, settings.downscale) ||
                        held->budget >= kFullSolveBudget)) {
            continue;
        }
        const long long budget = current ? kFullSolveBudget : kInteractiveSolveBudget;

        const ColourAsked asked{image_, layer.id, true};
        const auto already = ctg_asked_.find(asked);
        if (already != ctg_asked_.end() && already->second.inputs == wanted.hash) {
            continue;  // already being worked out
        }

        ctg_asked_[asked] = {wanted.hash, generation};
        ctg_solver_.request({image_, layer.id},
                            ctgJobFor(doc_, track_, image_, layer.id, settings, budget), true);
    }

    noteColourPending();
}

// Asks for every drawing to be judged.
//
// The same solve as a fill and a much cheaper one: coarse, and stopped after
// the labelling, so what it keeps is a few bytes rather than a canvas of tiles.
// It goes behind everything on screen, because it is a whole track's worth of
// work and nobody is waiting for any one of its answers.
//
// This ran on the interface thread until now, once every quarter of a second
// after the edits stopped -- 67 ms for twelve drawings and rather more for
// ninety-six, paid in the middle of whatever you were doing.
void CanvasWidget::requestColourAudit() {
    if (track_ == kNoId) return;

    const std::uint64_t generation = doc_.ctgCache().generation();
    const CtgSettings settings = auditSettings();

    for (const CtgToJudge& todo : ctgAuditWork(doc_, track_, settings)) {
        const ColourAsked asked{todo.key.image, todo.key.layer, false};
        const auto already = ctg_asked_.find(asked);
        if (already != ctg_asked_.end() && already->second.inputs == todo.inputs) continue;

        ctg_asked_[asked] = {todo.inputs, generation};
        ctg_solver_.request(todo.key,
                            ctgJobFor(doc_, track_, todo.key.image, todo.key.layer, settings),
                            false, CtgSolver::Priority::Whenever);
    }

    noteColourPending();
}

// Starts and stops the poll, and says when the answer to "is the colour being
// worked out" has changed.
//
// Both because they are the same event. Nothing is waiting for a solve, but the
// status bar is entitled to say one is happening: it is the whole of the
// visible difference between solving here and solving elsewhere, and without it
// a fill that is a second out of date looks like a fill that is wrong.
void CanvasWidget::noteColourPending() {
    const bool pending = !ctg_asked_.empty();
    if (ctg_poll_) {
        if (pending && !ctg_poll_->isActive()) ctg_poll_->start();
        if (!pending) ctg_poll_->stop();
    }
    if (pending == colour_was_pending_) return;
    colour_was_pending_ = pending;
    Q_EMIT colourChanged();
}

// Requests whose answer nobody is waiting for any more.
//
// A fill for a drawing that has been left, or -- fill or judgement, on screen
// or not -- one about a document that has since been thrown away. Playing a
// coloured shot is twenty-four of the first a second against solves taking a
// tenth of one, and a queue that fills faster than it drains never catches up.
//
// Judgements are not dropped for being about another drawing: being about the
// drawings you are not looking at is the whole of what they are for.
void CanvasWidget::dropStaleColourRequests(bool only_this_drawing) {
    const std::uint64_t generation = doc_.ctgCache().generation();
    for (auto it = ctg_asked_.begin(); it != ctg_asked_.end();) {
        const bool left = only_this_drawing && it->first.tiles && it->first.image != image_;
        if (!left && it->second.generation == generation) {
            ++it;
            continue;
        }
        ctg_solver_.cancel({it->first.image, it->first.layer}, it->first.tiles);
        it = ctg_asked_.erase(it);
    }
}

// Takes what the solver has finished and puts it in the document.
//
// This runs on the interface thread and it is the only place a solved fill or
// verdict enters the document, which is the whole of the threading discipline
// here: the worker is handed a copy and hands back an answer, and every write
// to the document stays on the thread that owns it.
void CanvasWidget::collectColour() {
    bool filled = false;
    bool judged = false;
    dropStaleColourRequests(/*only_this_drawing=*/false);

    for (CtgSolver::Result& result : ctg_solver_.collect()) {
        const ColourAsked key{result.key.image, result.key.layer, result.wanted_tiles};
        const auto asked = ctg_asked_.find(key);
        // An answer to a question that has since been asked again, about a
        // drawing that has since been left, or about a document that has since
        // been replaced. All ordinary, and all dropped: what it would have
        // replaced is at worst as old.
        if (asked == ctg_asked_.end() || asked->second.inputs != result.fill.inputs) continue;

        ctg_asked_.erase(asked);
        if (result.wanted_tiles) {
            doc_.ctgCache().store(result.key, std::move(result.fill));
            filled = true;
        } else {
            doc_.ctgVerdicts()[result.key] = verdictFrom(result.fill);
            judged = true;
        }
    }

    if (!filled && !judged) {
        noteColourPending();
        return;
    }
    colour_was_pending_ = !ctg_asked_.empty();
    if (ctg_poll_ && ctg_asked_.empty()) ctg_poll_->stop();

    // A regenerated fill changes colour across whole regions, nowhere near
    // wherever the pen was, so all of it is redrawn. Marking only the stroke's
    // own rectangle left the new fill beside the stroke and the old one
    // everywhere else, and hiding and showing the layer repainted the lot -- so
    // the same operation appeared to have two behaviours.
    //
    // A verdict changes nothing on the canvas at all -- it is a judgement about
    // pixels and not pixels -- so it costs a panel refresh and no repaint.
    if (filled) {
        dirty_everything_ = true;
        update();
    }
    Q_EMIT colourChanged();
}

bool CanvasWidget::settleColour(int timeout_ms) {
    QElapsedTimer clock;
    clock.start();
    while (true) {
        // Asking again is what carries the ladder up a rung: the coarse answer
        // being installed is exactly what makes the fine one worth asking for,
        // and in use it is the repaint that does this. Waiting without it would
        // settle on the first answer and call that the colour.
        requestCtgFills();
        if (ctg_asked_.empty()) return true;
        if (clock.elapsed() > timeout_ms) return false;
        ctg_solver_.waitUntilIdle();
        collectColour();

        // A solve that came back to nobody -- superseded, or about a drawing
        // that has been left -- leaves its entry behind, and waiting for an
        // answer that will never arrive is a hang. Nothing outstanding at the
        // solver means nothing more is coming.
        if (!ctg_asked_.empty() && ctg_solver_.idle()) {
            ctg_asked_.clear();
            return false;
        }
    }
    return true;
}

// While a scribble is being drawn its layer shows the marks rather than the
// fill, so the pen leaves something visible without a solve behind it. A view
// flag, so it is set directly and never recorded: undo has no business
// restoring what you were looking at.
void CanvasWidget::setScribblePreview(LayerId layer_id, bool previewing) {
    Track* track = doc_.mutableScene().findTrack(track_);
    if (!track) return;
    Layer* layer = track->findLayer(layer_id);
    if (!layer || layer->kind != LayerKind::Ctg) return;

    if (previewing) {
        scribble_preview_layer_ = layer_id;
        scribble_preview_was_showing_ = layer->show_scribbles;
        layer->show_scribbles = true;
    } else {
        layer->show_scribbles = scribble_preview_was_showing_;
        scribble_preview_layer_ = kNoId;
    }
}

// Flattens the neighbouring drawings into one tinted, faded layer. Previous
// drawings go warm and later ones cool, which is the convention every animator
// already reads without being told.
void CanvasWidget::rebuildOnion() {
    onion_.resize(0, 0);
    if (playing_ || track_ == kNoId) return;
    if (onion_settings_.before <= 0 && onion_settings_.after <= 0) return;

    const Track* track = doc_.scene().findTrack(track_);
    if (!track || cached_region_.isEmpty()) return;

    onion_.resize(cache_step_.entriesAcross(cached_region_.x, cached_region_.width),
                  cache_step_.entriesAcross(cached_region_.y, cached_region_.height));

    struct Ghost {
        ImageId image;
        float weight;
        float r, g, b;
    };
    std::vector<Ghost> ghosts;

    const auto collect = [&](int count, int direction, float r, float g, float b) {
        if (count <= 0) return;
        const std::vector<ImageId> neighbours =
            track->distinctNeighbours(slot_, count, direction);
        for (std::size_t i = 0; i < neighbours.size(); ++i) {
            const float falloff =
                static_cast<float>(count - static_cast<int>(i)) / static_cast<float>(count);
            ghosts.push_back({neighbours[i], onion_settings_.opacity * falloff, r, g, b});
        }
    };
    collect(onion_settings_.before, -1, 0.85f, 0.15f, 0.10f);
    collect(onion_settings_.after, +1, 0.10f, 0.40f, 0.85f);

    // Furthest first, so the nearest drawing ends up on top.
    std::stable_sort(ghosts.begin(), ghosts.end(),
                     [](const Ghost& a, const Ghost& b) { return a.weight < b.weight; });

    Framebuffer ghost_frame;
    for (const Ghost& ghost : ghosts) {
        compositor_.composite(doc_, track_, ghost.image, cached_region_, ghost_frame,
                              cache_step_);
        for (int y = 0; y < onion_.height(); ++y) {
            const Rgba* source = ghost_frame.row(y);
            Rgba* destination = onion_.row(y);
            for (int x = 0; x < onion_.width(); ++x) {
                const float alpha = std::clamp(source[x].a, 0.0f, 1.0f) * ghost.weight;
                if (alpha <= 0.0f) continue;
                // Tinted silhouette: the shape is what matters, not the colour
                // the neighbouring drawing happens to be.
                const Rgba tinted{ghost.r * alpha, ghost.g * alpha, ghost.b * alpha, alpha};
                destination[x] = over(tinted, destination[x]);
            }
        }
    }
}

QPointF CanvasWidget::imageFromWidget(const QPointF& widget_point) const {
    return {pan_.x() + widget_point.x() / zoom_, pan_.y() + widget_point.y() / zoom_};
}

QPointF CanvasWidget::widgetFromImage(const QPointF& image_point) const {
    return {(image_point.x() - pan_.x()) * zoom_, (image_point.y() - pan_.y()) * zoom_};
}

PixelRect CanvasWidget::visibleImageRect() const {
    const QPointF top_left = imageFromWidget({0.0, 0.0});
    const QPointF bottom_right = imageFromWidget({static_cast<double>(width()),
                                                  static_cast<double>(height())});
    const int x0 = static_cast<int>(std::floor(top_left.x())) - 1;
    const int y0 = static_cast<int>(std::floor(top_left.y())) - 1;
    const int x1 = static_cast<int>(std::ceil(bottom_right.x())) + 1;
    const int y1 = static_cast<int>(std::ceil(bottom_right.y())) + 1;
    return {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
}

long long CanvasWidget::cacheEntryCount() const {
    return static_cast<long long>(display_.width()) * display_.height();
}

void CanvasWidget::ensureCacheCoversView() {
    const PixelRect wanted = visibleImageRect();

    // One cache entry per screen pixel, never finer than one per image pixel.
    //
    // This is the whole of issue #11, and it replaces a memory budget that was
    // making decisions nobody attributed to it. The cache used to be held at
    // one entry per *image* pixel reduced by an integer step, which forces two
    // things at once: the size grows as viewport/zoom^2, so it must be capped,
    // and an integer step cannot follow a continuous zoom, so there must be a
    // zoom at which it doubles -- with the cap deciding where. A stroke was
    // crisp at 62% and jagged at 60% because a memory limit landed between
    // them, and the boundary moved with the window size, so the same percentage
    // meant different sharpness in different windows.
    //
    // Held at one entry per screen pixel, the size is about the viewport at
    // every zoom, the sampling ratio is 1/zoom and continuous, and reducing a
    // block to its average becomes what the compositor's loop is already for.
    // Everything allocated here -- the cache, the onion buffer, the scratch the
    // compositor writes into -- is now bounded by the window rather than by how
    // much of the image the window can see. At 5% zoom that used to ask for
    // half a gigabyte.
    const SampleStep step = SampleStep::fromRatio(std::max(1.0, 1.0 / zoom_));

    // The margin is a fixed number of *screen* pixels, converted to image
    // pixels here. Held in image pixels instead it was 192 at 100% zoom and
    // 6144 at 3200%, so the cache -- and the rectangle the blit has to scale it
    // into -- grew without bound the further you zoomed in. Nothing spends it
    // any more: it costs the same fraction of a viewport-sized cache whatever
    // the zoom, so there is no longer a band where a pan recomposited on every
    // single mouse move.
    const int margin = std::max(1, static_cast<int>(std::lround(kCacheMargin / zoom_)));

    // Snapped out to entry boundaries, so that a partial refresh lands on the
    // same entries a full one would and no entry is averaged from part of its
    // block.
    const PixelRect padded =
        snapToSampleGrid(step, {wanted.x - margin, wanted.y - margin, wanted.width + 2 * margin,
                                wanted.height + 2 * margin});

    // Panning inside the cached margin, or zooming in, needs no new composite
    // at all -- the cache already holds those pixels and the blit just samples
    // a different part of it. Recompositing on every mouse move was what made
    // navigation feel heavy.
    if (!display_.isNull() && step == cache_step_ && contains(cached_region_, wanted)) {
        const long long cached_area =
            static_cast<long long>(cached_region_.width) * cached_region_.height;
        const long long wanted_area = static_cast<long long>(wanted.width) * wanted.height;
        // Unless the cache has become far larger than the view needs, which
        // happens after zooming a long way in and would waste the memory.
        if (cached_area <= wanted_area * 4) return;
    }

    cache_step_ = step;
    cached_region_ = padded;

    display_ = QImage(step.entriesAcross(padded.x, padded.width),
                      step.entriesAcross(padded.y, padded.height), QImage::Format_RGB32);
    onion_dirty_ = true;
    dirty_everything_ = true;
}

void CanvasWidget::refreshAll() {
    dirty_everything_ = true;
    update();
}

void CanvasWidget::markDirty(const PixelRect& region) {
    if (dirty_everything_) return;
    pending_dirty_ = unite(pending_dirty_, region);
}

// Composites `region` and writes it into the cached sRGB image. This is the
// only place the linear working space becomes display pixels.
void CanvasWidget::refreshRegion(const PixelRect& region) {
    if (display_.isNull() || track_ == kNoId || image_ == kNoId) return;

    PixelRect area = intersect(region, cached_region_);
    if (area.isEmpty()) return;

    // Snap out to the sampling grid, so the entries this produces are exactly
    // the ones a full refresh would have produced. The grid is anchored at the
    // image origin rather than at the cached region, which is what makes that
    // true whatever the region happens to be and wherever the view has panned
    // to; the cached region is snapped too, so intersecting keeps it aligned.
    area = intersect(snapToSampleGrid(cache_step_, area), cached_region_);
    if (area.isEmpty()) return;

    compositor_.composite(doc_, track_, image_, area, scratch_, cache_step_);

    // Where this patch of entries sits in the cache.
    const long long first_column = cache_step_.entryAt(area.x);
    const long long first_row = cache_step_.entryAt(area.y);
    const int column_base =
        static_cast<int>(first_column - cache_step_.entryAt(cached_region_.x));
    const int row_base = static_cast<int>(first_row - cache_step_.entryAt(cached_region_.y));

    const bool checker = background_ == Background::Checker;
    const float flat = 1.0f;

    // Detached once, here, before any worker exists. QImage is copy-on-write and
    // scanLine() is the non-const accessor that triggers the copy, so calling it
    // from several threads is a race over the detach rather than over the
    // pixels. bits() does the same detach once, and the rows are then addressed
    // by hand.
    uchar* const base_line = display_.bits();
    const qsizetype stride = display_.bytesPerLine();

    // One band of rows: paper, onion, drawing, sRGB. Rows are independent --
    // each reads one row of the scratch and writes one scanline of the cache --
    // so this splits exactly the way the compositor already splits.
    //
    // It has to. This loop, not the flattening, is the larger part of a refresh
    // by a wide margin: compositing a full viewport measures 3-8 ms and this
    // measured 37, and it is the same 37 ms whether the drawing has 66 tiles or
    // 2425 of them, because the work is per output pixel rather than per stroke.
    // The compositor was parallelised years before this loop was even timed --
    // `bench_composite` only ever watched the other end. See `bench_zoom`.
    const auto convert_rows = [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            const Rgba* source = scratch_.row(y);
            const int row = row_base + y;
            if (row < 0 || row >= display_.height()) continue;
            auto* destination = reinterpret_cast<QRgb*>(base_line + row * stride);
            // Only the checker needs to know where the entry sits in the
            // drawing, and it is the uncommon background, so the mapping back
            // to image coordinates is paid for only when it is asked for.
            const int image_y = checker ? cache_step_.entryBegin(first_row + y) : 0;

            for (int x = 0; x < scratch_.width(); ++x) {
                const int column = column_base + x;
                if (column < 0 || column >= display_.width()) continue;
                float background = flat;
                if (checker) {
                    const int image_x = cache_step_.entryBegin(first_column + x);
                    const bool light =
                        (((image_x / kCheckerSize) + (image_y / kCheckerSize)) & 1) == 0;
                    background = light ? 1.0f : 0.78f;
                }

                // Paper, then the onion skin over it, then the drawing over that.
                Rgba base{background, background, background, 1.0f};
                if (!onion_.isEmpty() && row < onion_.height() && column < onion_.width()) {
                    base = over(onion_.row(row)[column], base);
                }

                const Rgba& pixel = source[x];
                const float keep = 1.0f - std::clamp(pixel.a, 0.0f, 1.0f);
                const float r = pixel.r + base.r * keep;
                const float g = pixel.g + base.g * keep;
                const float b = pixel.b + base.b * keep;

                destination[column] = qRgb(toSrgbByte(r), toSrgbByte(g), toSrgbByte(b));
            }
        }
    };

    const int rows = scratch_.height();
    const int workers = chooseWorkerCount(rows, scratch_.width());
    if (workers <= 1) {
        convert_rows(0, rows);
        return;
    }

    const int band = (rows + workers - 1) / workers;
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(workers) - 1);
    for (int w = 1; w < workers; ++w) {
        const int y_begin = std::min(rows, w * band);
        const int y_end = std::min(rows, y_begin + band);
        if (y_begin >= y_end) break;
        pool.emplace_back(convert_rows, y_begin, y_end);
    }
    convert_rows(0, std::min(rows, band));  // this thread takes the first band
    for (std::thread& worker : pool) worker.join();
}

void CanvasWidget::repaintImageRect(const PixelRect& region) {
    const QPointF top_left = widgetFromImage({static_cast<double>(region.x),
                                              static_cast<double>(region.y)});
    const QPointF bottom_right =
        widgetFromImage({static_cast<double>(region.x + region.width),
                         static_cast<double>(region.y + region.height)});
    update(QRectF(top_left, bottom_right).normalized().adjusted(-2, -2, 2, 2).toAlignedRect());
}

void CanvasWidget::resizeEvent(QResizeEvent*) {
    ensureCacheCoversView();
    update();
}

void CanvasWidget::paintEvent(QPaintEvent* event) {
    ensureCacheCoversView();

    // All the compositing for this frame happens here, once, however many
    // edits arrived since the last paint.
    //
    // The colour is asked for here and never worked out here. What arrives is
    // whatever was solved by the time this ran; a fill landing later brings the
    // paint on itself.
    requestCtgFills();
    if (onion_dirty_) {
        rebuildOnion();
        onion_dirty_ = false;
        dirty_everything_ = true;
    }
    if (dirty_everything_) {
        refreshRegion(cached_region_);
        dirty_everything_ = false;
        pending_dirty_ = {};
    } else if (!pending_dirty_.isEmpty()) {
        refreshRegion(pending_dirty_);
        pending_dirty_ = {};
    }

    QPainter painter(this);
    painter.setClipRect(event->rect());
    painter.fillRect(event->rect(), QColor(48, 48, 52));

    if (display_.isNull()) return;

    // Nearest-neighbour when magnified far enough that the pixels are the thing
    // being looked at; smooth below that.
    //
    // Two corrections to what this used to be, both measured. The factor the
    // blit applies is `cache_step_ * zoom_`, not `zoom_`: with the cache held
    // at screen resolution that product is 1 at every zoom below 100% and the
    // zoom itself above, which is the number the question was always about.
    //
    // And the threshold was 1.0, which meant nearest-neighbour from 101%
    // upwards. At 109% that duplicates one pixel column in eleven -- a
    // staircase along every curve, invisible on an orthogonal edge, which is
    // exactly the shape of the complaint. Measured against the same curve drawn
    // at display size, it doubled the error a smooth blit gives. Nobody
    // inspecting pixels is doing it at 109%; by 300% they are, and there
    // nearest is the honest thing to show.
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          blitInterpolatesAt(cache_step_.ratio() * zoom_));

    // The rectangle the cached *entries* cover, not the integer rectangle round
    // them. With a fractional step an entry boundary falls between two image
    // pixels, so the cached region is up to a pixel wider than the entries in
    // it actually span -- and blitting into that stretches the cache by a part
    // in a thousand, which over the width of a window slides a curve a pixel
    // off where the drawing says it is. Both corners go through the view
    // transform for the same reason: a size computed from the entry count would
    // accumulate the same error.
    const long long first_column = cache_step_.entryAt(cached_region_.x);
    const long long first_row = cache_step_.entryAt(cached_region_.y);
    const QPointF origin = widgetFromImage(
        {cache_step_.entryEdge(first_column), cache_step_.entryEdge(first_row)});
    const QPointF corner =
        widgetFromImage({cache_step_.entryEdge(first_column + display_.width()),
                         cache_step_.entryEdge(first_row + display_.height())});
    painter.drawImage(QRectF(origin, corner), display_);

    drawCanvasFrame(painter);
}

// The canvas: the rectangle that will be exported, outlined, with everything
// outside it veiled.
//
// Drawing outside stays allowed and is not discouraged -- roughs run off the
// edge, and a surface with no edges is the point of the tile model. But the
// picture has a boundary, and until it was drawn there was no way to know where
// it was: the only rectangle on screen was the region a colour fill happened to
// solve, which looked like a canvas and was not one.
void CanvasWidget::drawCanvasFrame(QPainter& painter) {
    const PixelRect canvas = doc_.scene().canvas();
    if (canvas.isEmpty()) return;

    const QPointF top_left = widgetFromImage({static_cast<double>(canvas.x),
                                              static_cast<double>(canvas.y)});
    const QPointF bottom_right =
        widgetFromImage({static_cast<double>(canvas.x + canvas.width),
                         static_cast<double>(canvas.y + canvas.height)});
    const QRectF frame(top_left, bottom_right);

    // Odd-even filling, so the inner rectangle punches a hole in the outer one
    // and the veil covers everything except the picture.
    QPainterPath outside;
    outside.addRect(QRectF(rect()));
    outside.addRect(frame);

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.fillPath(outside, QColor(28, 28, 32, 150));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(150, 150, 160), 1.0));
    painter.drawRect(frame);
    painter.restore();
}

// --- picking -------------------------------------------------------------

// Samples the flattened drawing under the pointer, in the space it is stored
// in, and hands the colour to whoever is listening.
//
// This is the eyedropper, and it is here rather than on the colour dialog's
// "pick screen colour" for a reason that is not a matter of taste. That picker
// only ever hears about mouse buttons, and on Windows a pen produces none it can
// hear: Qt routes pen input as a tablet event and then discards the legacy mouse
// messages Windows promotes from it, so the click never arrives however the
// canvas behaves. Hence colours could be picked with a mouse and not with a pen.
//
// Sampling the document is the better answer regardless. The screen picker reads
// back what the monitor was showing -- after sRGB encoding to eight bits, after
// the zoom filter, with the onion skin and the paper mixed into it -- and calls
// the result the colour you drew with. This reads the colour that was stored.
//
// The paper is not part of the drawing, so an empty pixel has nothing to pick
// and the brush is left alone rather than being set to white.
bool CanvasWidget::pickColourAt(const QPointF& image_point) {
    if (track_ == kNoId || image_ == kNoId) return false;

    const PixelRect one{static_cast<int>(std::floor(image_point.x())),
                        static_cast<int>(std::floor(image_point.y())), 1, 1};
    Framebuffer sample;
    compositor_.composite(doc_, track_, image_, one, sample);
    if (sample.isEmpty()) return false;

    // A CTG layer contributes the fill it last generated, which is what is on
    // screen and so what picking from it should mean.
    const Rgba pixel = sample.pixel(0, 0);
    if (pixel.a < 0.01f) return false;

    Q_EMIT colourPicked(pixel.r / pixel.a, pixel.g / pixel.a, pixel.b / pixel.a);
    return true;
}

// --- strokes -------------------------------------------------------------

void CanvasWidget::beginStroke(const QPointF& image_point, float pressure) {
    if (track_ == kNoId || image_ == kNoId || active_layer_ == kNoId) return;

    const Track* track = doc_.scene().findTrack(track_);
    const Layer* layer = track ? track->findLayer(active_layer_) : nullptr;
    if (!layer || layer->locked || !layer->visible) return;

    BrushSettings settings = (stylus_eraser_ || erasing_) ? eraser_settings_ : brush_settings_;
    settings.erase = stylus_eraser_ || erasing_;

    // On a CTG layer the stroke is a label, not paint. Pressure must not thin
    // the mark into a half-vote for a colour, and a soft rim would be read as
    // scribbled or not depending on a threshold, which is no way to decide --
    // so the rim is not written at all. See BrushSettings::label.
    if (layer->kind == LayerKind::Ctg) {
        settings.pressure_affects_opacity = false;
        settings.hardness = 1.0f;
        settings.opacity = 1.0f;
        settings.label = true;
    } else if (!settings.erase &&
               isTransparentScribble(Rgba{settings.r, settings.g, settings.b, 1.0f})) {
        // The transparent label is a scribble, not paint: on a raster layer it
        // would be a stroke of negative light. The interface puts the colour
        // back when you leave a colour layer, so this cannot happen -- it is
        // here because "cannot happen" is worth being wrong about cheaply, and
        // the alternative is pixels no filter will ever make sense of.
        return;
    }
    brush_.settings() = settings;

    doc_.beginCommand(settings.erase ? "Erase" : "Stroke");
    stroking_ = true;

    brush_.begin(doc_, track_, image_, active_layer_,
                 {static_cast<float>(image_point.x()), static_cast<float>(image_point.y()),
                  pressure});

    last_image_point_ = image_point;
    last_pressure_ = pressure;
    scribbling_ = layer->kind == LayerKind::Ctg;
    if (scribbling_) setScribblePreview(active_layer_, true);

    // A scribble changes the fill across the whole region, not just where the
    // pen went, so there is nothing useful to mark dirty. Repainting only the
    // stroke's own rectangle left the rest of the region showing the previous
    // colour until some unrelated event forced a repaint -- which looked for
    // all the world like paint left on the brush.
    if (scribbling_) {
        refreshAll();
        return;
    }

    const int radius = static_cast<int>(std::ceil(settings.radius)) + 2;
    markDirty({static_cast<int>(std::floor(image_point.x())) - radius,
               static_cast<int>(std::floor(image_point.y())) - radius, 2 * radius, 2 * radius});
    repaintImageRect({static_cast<int>(std::floor(image_point.x())) - radius,
                      static_cast<int>(std::floor(image_point.y())) - radius, 2 * radius,
                      2 * radius});
}

void CanvasWidget::extendStroke(const QPointF& image_point, float pressure) {
    if (!stroking_) return;

    brush_.extend({static_cast<float>(image_point.x()), static_cast<float>(image_point.y()),
                   pressure});

    if (scribbling_) {
        last_image_point_ = image_point;
        last_pressure_ = pressure;
        refreshAll();
        return;
    }

    const int radius = static_cast<int>(std::ceil(brush_.settings().radius)) + 2;
    const QRectF segment = QRectF(last_image_point_, image_point).normalized();
    const PixelRect dirty{static_cast<int>(std::floor(segment.left())) - radius,
                          static_cast<int>(std::floor(segment.top())) - radius,
                          static_cast<int>(std::ceil(segment.width())) + 2 * radius,
                          static_cast<int>(std::ceil(segment.height())) + 2 * radius};
    markDirty(dirty);
    repaintImageRect(dirty);

    last_image_point_ = image_point;
    last_pressure_ = pressure;
}

// The frame moved under an active stroke. Close the brush on the old drawing
// and reopen it on the new one at the same place, inside the same command, so
// the whole gesture is still a single undo step.
void CanvasWidget::rebindStrokeToCurrentImage() {
    brush_.end();
    if (image_ == kNoId || active_layer_ == kNoId) {
        stroking_ = false;
        doc_.endCommand();
        return;
    }
    brush_.begin(doc_, track_, image_, active_layer_,
                 {static_cast<float>(last_image_point_.x()),
                  static_cast<float>(last_image_point_.y()), last_pressure_});
}

void CanvasWidget::endStroke() {
    if (!stroking_) return;
    brush_.end();
    doc_.endCommand();
    stroking_ = false;
    stylus_eraser_ = false;

    if (scribbling_) {
        scribbling_ = false;
        setScribblePreview(scribble_preview_layer_, false);
    }

    // The pen lifting is what triggers the solve, so the fill has to be rebuilt
    // and redrawn even though nothing else has changed. That holds whichever
    // layer was drawn on: a stroke on the line art moves the boundary a colour
    // is cut against just as surely as a scribble does.
    refreshAll();
    Q_EMIT documentChanged();
}

// --- navigation ----------------------------------------------------------

bool CanvasWidget::beginNavigation(const QPointF& widget_point, Qt::MouseButton button) {
    // Alt and the right button, dragged sideways, resizes the brush without
    // leaving the drawing -- the gesture Photoshop and Krita already taught
    // everyone's hands.
    if (button == Qt::RightButton &&
        (QGuiApplication::keyboardModifiers() & Qt::AltModifier)) {
        sizing_ = true;
        size_anchor_widget_ = widget_point;
        radius_at_press_ = brushSettings().radius;
        setCursor(Qt::SizeHorCursor);
        return true;
    }
    if (zoom_key_held_) {
        zooming_ = true;
        zoom_anchor_widget_ = widget_point;
        zoom_at_press_ = zoom_;
        setCursor(Qt::SizeHorCursor);
        return true;
    }
    if (button == Qt::MiddleButton || (space_held_ && button == Qt::LeftButton)) {
        panning_ = true;
        pan_anchor_widget_ = widget_point;
        pan_anchor_image_ = pan_;
        setCursor(Qt::ClosedHandCursor);
        return true;
    }
    return false;
}

bool CanvasWidget::continueNavigation(const QPointF& widget_point) {
    if (sizing_) {
        const double dx = widget_point.x() - size_anchor_widget_.x();
        const float radius = static_cast<float>(
            std::clamp(radius_at_press_ * std::exp(dx * kSizeDragPerPixel), 0.5, 400.0));
        brushSettings().radius = radius;
        Q_EMIT brushSizeChanged(radius);
        return true;
    }
    if (zooming_) {
        // Scrubby zoom: drag right to come in, left to go out, about the point
        // the drag started from.
        const double dx = widget_point.x() - zoom_anchor_widget_.x();
        setZoom(zoom_at_press_ * std::exp(dx * kScrubbyZoomPerPixel), zoom_anchor_widget_);
        return true;
    }
    if (panning_) {
        const QPointF moved = widget_point - pan_anchor_widget_;
        pan_ = onWholeScreenPixels({pan_anchor_image_.x() - moved.x() / zoom_,
                                    pan_anchor_image_.y() - moved.y() / zoom_},
                                   zoom_);
        ensureCacheCoversView();
        update();
        Q_EMIT viewChanged();
        return true;
    }
    return false;
}

void CanvasWidget::endNavigation() {
    if (!panning_ && !zooming_ && !sizing_) return;
    panning_ = false;
    zooming_ = false;
    sizing_ = false;
    setCursor(zoom_key_held_  ? Qt::SizeHorCursor
              : space_held_   ? Qt::OpenHandCursor
                              : Qt::CrossCursor);
}

// --- input ---------------------------------------------------------------

void CanvasWidget::tabletEvent(QTabletEvent* event) {
    // Qt drops mouse events aimed at a window a modal dialog has blocked. It
    // does not do the same for tablet events, which are delivered by hit test
    // and arrive here regardless -- so the pen drew on the canvas behind an
    // open dialog, and took the keyboard focus off it on the way in. Leave the
    // event alone: whatever is modal has a better claim on the pen than we do.
    if (QApplication::activeModalWidget()) {
        event->ignore();
        return;
    }

    event->accept();
    last_tablet_ms_ = clock_.elapsed();

    // Qt gives click-focus for a mouse press but not for a tablet press, so an
    // artist who has touched a spin box in the toolbar never gets the keyboard
    // back by drawing -- and the spin box's line edit then eats B and E as text
    // rather than letting them switch tool. Taken explicitly, for both devices.
    if (event->type() == QEvent::TabletPress) setFocus(Qt::MouseFocusReason);

    const QPointF widget_point = event->position();

    if (event->type() == QEvent::TabletPress && beginNavigation(widget_point, Qt::LeftButton)) {
        return;
    }
    if ((panning_ || zooming_ || sizing_) && event->type() == QEvent::TabletMove) {
        continueNavigation(widget_point);
        return;
    }
    if ((panning_ || zooming_ || sizing_) && event->type() == QEvent::TabletRelease) {
        endNavigation();
        return;
    }

    // Alt and the tip picks the colour under it, the gesture every drawing
    // program already taught everyone's hands. Nothing is drawn and no command
    // is opened: picking a colour is not an edit.
    //
    // Taken when the pen lifts rather than when it lands, so the pen can be slid
    // onto the exact pixel while it is down. Landing a nib precisely is the hard
    // part; sliding it once it is on the tablet is not.
    //
    // Which is only worth having if you can see what you are sliding onto, so
    // the colour follows the pen the whole way down and the release simply stops
    // it moving. Sampling is a one-pixel composite, far below what a pen move
    // already costs, and nothing is being drawn meanwhile -- so the live value
    // can be the real brush colour rather than a preview of one, and there is
    // one path instead of two that have to agree.
    //
    // An empty pixel leaves the colour alone, here as everywhere: dragging out
    // over bare paper holds the last colour rather than snatching it away, so
    // the release commits what is shown even when it lands on nothing.
    if (event->type() == QEvent::TabletPress && (event->modifiers() & Qt::AltModifier)) {
        picking_ = true;
        pickColourAt(imageFromWidget(widget_point));
        return;
    }
    if (picking_) {
        if (event->type() == QEvent::TabletMove) {
            pickColourAt(imageFromWidget(widget_point));
        } else if (event->type() == QEvent::TabletRelease) {
            picking_ = false;
            pickColourAt(imageFromWidget(widget_point));
        }
        return;  // no stroke, whatever happened to Alt in the meantime
    }

    const QPointingDevice* device = event->pointingDevice();
    if (event->type() == QEvent::TabletPress && device) {
        stylus_eraser_ = device->pointerType() == QPointingDevice::PointerType::Eraser;
    }

    const QPointF image_point = imageFromWidget(widget_point);
    const float pressure = static_cast<float>(event->pressure());

    switch (event->type()) {
        case QEvent::TabletPress: beginStroke(image_point, pressure); break;
        case QEvent::TabletMove:
            if (stroking_) extendStroke(image_point, pressure);
            break;
        case QEvent::TabletRelease: endStroke(); break;
        default: break;
    }
}

// Windows Ink promotes every pen event to a mouse event as well. Acting on
// those would draw the stroke a second time at an invented pressure.
//
// The question is "did a pen produce this event", so the device is asked first.
// It does not always say -- a promoted event arrives claiming to be the mouse --
// so the fallback is how long ago the pen was last heard from. That used to be
// "has this canvas ever seen a tablet event", which is true forever afterwards:
// touching the tablet once left the mouse unable to draw for the rest of the
// session, with nothing on screen to say why.
//
// The window only has to outlast the promotion, which follows its pen event
// immediately. No hand puts down a pen and clicks a mouse inside a quarter of a
// second, so nothing real is caught by it.
bool CanvasWidget::eventIsSynthesisedFromPen(QMouseEvent* event) const {
    if (comesFromAStylus(event->pointingDevice())) return true;
    if (last_tablet_ms_ < 0) return false;
    return clock_.elapsed() - last_tablet_ms_ < kPenMouseWindowMs;
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    if (beginNavigation(event->position(), event->button())) return;
    if (eventIsSynthesisedFromPen(event)) return;
    if (event->button() != Qt::LeftButton) return;
    if (event->modifiers() & Qt::AltModifier) {
        // Shown from the moment the button goes down and followed until it comes
        // up; see tabletEvent for why the live value is the colour itself.
        picking_ = true;
        pickColourAt(imageFromWidget(event->position()));
        return;
    }
    beginStroke(imageFromWidget(event->position()), 1.0f);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (continueNavigation(event->position())) return;
    if (eventIsSynthesisedFromPen(event)) return;
    if (picking_) {
        pickColourAt(imageFromWidget(event->position()));
        return;
    }
    if (stroking_) extendStroke(imageFromWidget(event->position()), 1.0f);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_ || zooming_ || sizing_) {
        endNavigation();
        return;
    }
    if (eventIsSynthesisedFromPen(event)) return;
    if (picking_) {
        picking_ = false;
        pickColourAt(imageFromWidget(event->position()));
        return;
    }
    endStroke();
}

void CanvasWidget::setZoom(double zoom, const QPointF& widget_anchor) {
    const double clamped = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (std::abs(clamped - zoom_) < 1e-9) return;

    // Keep the image point under the anchor where it is.
    const QPointF before = imageFromWidget(widget_anchor);
    zoom_ = clamped;
    const QPointF after = imageFromWidget(widget_anchor);
    pan_ = onWholeScreenPixels(pan_ + (before - after), zoom_);

    ensureCacheCoversView();
    update();
    Q_EMIT viewChanged();
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) return;
    setZoom(zoom_ * std::pow(1.2, steps), event->position());
    event->accept();
}

void CanvasWidget::resetView() {
    zoom_ = 1.0;
    pan_ = {0.0, 0.0};
    ensureCacheCoversView();
    update();
    Q_EMIT viewChanged();
}

void CanvasWidget::fitToCanvas() { fitTo(doc_.scene().canvas()); }

void CanvasWidget::fitToDrawing() { fitTo(imageBounds(doc_, track_, image_)); }

void CanvasWidget::fitTo(const PixelRect& bounds) {
    if (bounds.isEmpty() || width() <= 0 || height() <= 0) {
        resetView();
        return;
    }

    const double scale_x = static_cast<double>(width()) / bounds.width;
    const double scale_y = static_cast<double>(height()) / bounds.height;
    zoom_ = std::clamp(std::min(scale_x, scale_y) * 0.9, kMinZoom, kMaxZoom);
    pan_ = onWholeScreenPixels({bounds.x + bounds.width / 2.0 - width() / (2.0 * zoom_),
                                bounds.y + bounds.height / 2.0 - height() / (2.0 * zoom_)},
                               zoom_);

    ensureCacheCoversView();
    update();
    Q_EMIT viewChanged();
}

// Space and Z are accepted here whether or not they are auto-repeats. That
// looks like a detail and is not: an ignored key propagates to the parent
// widget, where the application-wide filter sees it again and forwards it back
// here, which propagates again. Holding either key past the auto-repeat delay
// used to recurse until the stack ran out -- a crash a few seconds into every
// pan and every zoom.
void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Space:
            if (!event->isAutoRepeat()) {
                space_held_ = true;
                if (!panning_ && !zooming_ && !sizing_) setCursor(Qt::OpenHandCursor);
            }
            event->accept();
            return;
        case Qt::Key_Z:
            // Held, not toggled: a zoom you have to switch back out of costs
            // more attention than the zoom is worth.
            if (!event->isAutoRepeat()) {
                zoom_key_held_ = true;
                if (!panning_ && !zooming_ && !sizing_) setCursor(Qt::SizeHorCursor);
            }
            event->accept();
            return;
        default: break;
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Space:
            if (!event->isAutoRepeat()) {
                space_held_ = false;
                if (!panning_ && !zooming_ && !sizing_) {
                    setCursor(zoom_key_held_ ? Qt::SizeHorCursor : Qt::CrossCursor);
                }
            }
            event->accept();
            return;
        case Qt::Key_Z:
            if (!event->isAutoRepeat()) {
                zoom_key_held_ = false;
                if (!panning_ && !zooming_ && !sizing_) {
                    setCursor(space_held_ ? Qt::OpenHandCursor : Qt::CrossCursor);
                }
            }
            event->accept();
            return;
        default: break;
    }
    QWidget::keyReleaseEvent(event);
}
