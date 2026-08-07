// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_view.h"

#include <QGuiApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QThread>
#include <QTimer>
#include <QTouchEvent>
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
// full refresh to buy free panning nobody had asked for. 64 keeps most of the
// benefit for a third of the price.
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

PixelRect rectIntersect(const PixelRect& a, const PixelRect& b) {
    const int x0 = std::max(a.x, b.x);
    const int y0 = std::max(a.y, b.y);
    const int x1 = std::min(a.x + a.width, b.x + b.width);
    const int y1 = std::min(a.y + a.height, b.y + b.height);
    if (x1 <= x0 || y1 <= y0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

bool rectContains(const PixelRect& outer, const PixelRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

// The view, moved to a whole number of screen pixels. See the widget this was
// ported from for the measured reason: it is what lets the cache blit one
// entry to one pixel, a copy rather than a resample.
QPointF onWholeScreenPixels(const QPointF& pan, double zoom) {
    if (zoom <= 0.0) return pan;
    return {std::round(pan.x() * zoom) / zoom, std::round(pan.y() * zoom) / zoom};
}

PixelRect rectUnite(const PixelRect& a, const PixelRect& b) {
    if (a.isEmpty()) return b;
    if (b.isEmpty()) return a;
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.width, b.x + b.width);
    const int y1 = std::max(a.y + a.height, b.y + b.height);
    return {x0, y0, x1 - x0, y1 - y0};
}

}  // namespace

CanvasView::CanvasView(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    // Render through a QImage on the GUI thread, not an FBO on the render
    // thread: the paint() below touches the document, and the document is not
    // for the render thread to poke.
    setRenderTarget(QQuickPaintedItem::Image);
    setOpaquePainting(true);
    setAntialiasing(false);
    setActiveFocusOnTab(true);

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
    connect(ctg_poll_, &QTimer::timeout, this, &CanvasView::collectColour);

    // Space and Z are held modifiers, and they have to keep working when
    // something else has focus -- clicking a checkbox was enough to steal them
    // from the widget this replaced. An application-wide filter forwards them
    // here, leaving text editors alone.
    QGuiApplication::instance()->installEventFilter(this);

    clock_.start();
}

CanvasView::~CanvasView() {
    // The filter is installed on the application, which outlives this item.
    // Removing it here rather than letting ~QObject do it keeps it from seeing
    // events while the children it forwards to are already being destroyed.
    QGuiApplication::instance()->removeEventFilter(this);
    // Before anything else this object owns goes away. A solve holds only its
    // own copy of the drawing, so nothing here is unsafe -- but a max-flow that
    // has not been told the window is closing goes on running for a second or
    // two after it has, and the process stays up with nothing on screen.
    ctg_solver_.cancelAll();
}

void CanvasView::setDocument(Document* document) {
    doc_ = document;
    if (doc_) refreshAll();
}

void CanvasView::setTrack(TrackId track) {
    track_ = track;
    setFrame(slot_);
}

void CanvasView::setFrame(std::size_t slot) {
    const Track* track = doc_->scene().findTrack(track_);
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

    onion_dirty_ = true;
    refreshAll();
}

void CanvasView::setActiveLayer(LayerId layer) { active_layer_ = layer; }

void CanvasView::setEraser(bool erasing) { erasing_ = erasing; }

void CanvasView::setBrushColour(float r, float g, float b) {
    brush_settings_.r = r;
    brush_settings_.g = g;
    brush_settings_.b = b;
}

void CanvasView::setBackground(Background background) {
    background_ = background;
    refreshAll();
}

void CanvasView::setOnion(const OnionSettings& settings) {
    onion_settings_ = settings;
    onion_dirty_ = true;
    refreshAll();
}

void CanvasView::setPlaying(bool playing) {
    if (playing_ == playing) return;
    playing_ = playing;
    onion_dirty_ = true;
    refreshAll();
}

// Asks for any CTG fill whose scribbles or line art have moved to be solved
// somewhere else. Nothing is computed here: the solve happens on a worker
// thread, and what stays on screen is the last answer until the new one lands.
// See the widget this was ported from for the full reasoning.
bool CanvasView::isShownNow(ImageId image) const {
    if (image == kNoId || !doc_) return false;
    for (const Track& track : doc_->scene().tracks) {
        if (track.imageShownAt(slot_) == image) return true;
    }
    return false;
}

void CanvasView::requestCtgFills() {
    if (!doc_) return;
    if (stroking_) return;

    dropStaleColourRequests(/*only_this_frame=*/true);

    const std::uint64_t generation = doc_->ctgCache().generation();
    const CtgSettings settings;
    for (const Track& track_here : doc_->scene().tracks) {
        const TrackId track_id = track_here.id;
        const ImageId image = track_here.imageShownAt(slot_);
        if (image == kNoId) continue;

        for (const Layer& layer : track_here.layers) {
            if (layer.kind != LayerKind::Ctg || !layer.visible) continue;
            if (layer.show_scribbles) continue;

            const CtgInputs wanted = ctgInputsFor(*doc_, track_id, image, layer.id, settings);
            if (!wanted.valid) continue;

            const CtgFill* held = doc_->ctgFillFor(track_id, image, layer.id);
            const bool current = held && held->valid && held->inputs == wanted.hash;
            if (current && (held->step <= std::max(1, settings.downscale) ||
                            held->budget >= kFullSolveBudget)) {
                continue;
            }
            const long long budget = current ? kFullSolveBudget : kInteractiveSolveBudget;

            const ColourAsked asked{image, layer.id, true};
            const auto already = ctg_asked_.find(asked);
            if (already != ctg_asked_.end() && already->second.inputs == wanted.hash) {
                continue;  // already being worked out
            }

            ctg_asked_[asked] = {wanted.hash, generation};
            ctg_solver_.request({image, layer.id},
                                ctgJobFor(*doc_, track_id, image, layer.id, settings, budget), true);
        }
    }

    noteColourPending();
}

void CanvasView::noteColourPending() {
    const bool pending = !ctg_asked_.empty();
    if (ctg_poll_) {
        // requestCtgFills() is called from paint(). With QQuickPaintedItem the
        // scene graph may invoke paint() on the render thread while the item
        // and its QTimers live on the GUI thread. Starting or stopping a
        // QTimer from the wrong thread triggers
        // "QObject::startTimer: Timers cannot be started from another thread"
        // and the timer never fires.
        if (QThread::currentThread() != ctg_poll_->thread()) {
            QMetaObject::invokeMethod(
                ctg_poll_,
                [poll = ctg_poll_, pending] {
                    if (!poll) return;
                    if (pending && !poll->isActive())
                        poll->start();
                    else if (!pending)
                        poll->stop();
                },
                Qt::QueuedConnection);
        } else {
            if (pending && !ctg_poll_->isActive()) ctg_poll_->start();
            if (!pending) ctg_poll_->stop();
        }
    }
    if (pending == colour_was_pending_) return;
    colour_was_pending_ = pending;
    Q_EMIT colourChanged();
}

void CanvasView::dropStaleColourRequests(bool only_this_frame) {
    const std::uint64_t generation = doc_->ctgCache().generation();
    for (auto it = ctg_asked_.begin(); it != ctg_asked_.end();) {
        const bool left = only_this_frame && it->first.tiles && !isShownNow(it->first.image);
        if (!left && it->second.generation == generation) {
            ++it;
            continue;
        }
        ctg_solver_.cancel({it->first.image, it->first.layer}, it->first.tiles);
        it = ctg_asked_.erase(it);
    }
}

void CanvasView::collectColour() {
    bool filled = false;
    dropStaleColourRequests(/*only_this_drawing=*/false);

    for (CtgSolver::Result& result : ctg_solver_.collect()) {
        const ColourAsked key{result.key.image, result.key.layer, result.wanted_tiles};
        const auto asked = ctg_asked_.find(key);
        if (asked == ctg_asked_.end() || asked->second.inputs != result.fill.inputs) continue;

        ctg_asked_.erase(asked);
        doc_->ctgShifts()[result.key] = result.fill.carried_by;
        doc_->ctgCache().store(result.key, std::move(result.fill));
        filled = true;
    }

    if (!filled) {
        noteColourPending();
        return;
    }
    colour_was_pending_ = !ctg_asked_.empty();
    if (ctg_poll_ && ctg_asked_.empty()) {
        if (QThread::currentThread() != ctg_poll_->thread()) {
            QMetaObject::invokeMethod(
                ctg_poll_, [poll = ctg_poll_] { poll->stop(); }, Qt::QueuedConnection);
        } else {
            ctg_poll_->stop();
        }
    }

    dirty_everything_ = true;
    update();
    Q_EMIT colourChanged();
}

bool CanvasView::settleColour(int timeout_ms) {
    QElapsedTimer clock;
    clock.start();
    while (true) {
        requestCtgFills();
        if (ctg_asked_.empty()) return true;
        if (clock.elapsed() > timeout_ms) return false;
        ctg_solver_.waitUntilIdle();
        collectColour();

        if (!ctg_asked_.empty() && ctg_solver_.idle()) {
            ctg_asked_.clear();
            return false;
        }
    }
    return true;
}

void CanvasView::setScribblePreview(LayerId layer_id, bool previewing) {
    Track* track = doc_->mutableScene().findTrack(track_);
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

void CanvasView::rebuildOnion() {
    onion_.resize(0, 0);
    if (playing_ || track_ == kNoId) return;
    if (onion_settings_.before <= 0 && onion_settings_.after <= 0) return;

    const Track* track = doc_->scene().findTrack(track_);
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

    std::stable_sort(ghosts.begin(), ghosts.end(),
                     [](const Ghost& a, const Ghost& b) { return a.weight < b.weight; });

    Framebuffer ghost_frame;
    for (const Ghost& ghost : ghosts) {
        compositor_.composite(*doc_, track_, ghost.image, cached_region_, ghost_frame,
                              cache_step_);
        for (int y = 0; y < onion_.height(); ++y) {
            const Rgba* source = ghost_frame.row(y);
            Rgba* destination = onion_.row(y);
            for (int x = 0; x < onion_.width(); ++x) {
                const float alpha = std::clamp(source[x].a, 0.0f, 1.0f) * ghost.weight;
                if (alpha <= 0.0f) continue;
                const Rgba tinted{ghost.r * alpha, ghost.g * alpha, ghost.b * alpha, alpha};
                destination[x] = over(tinted, destination[x]);
            }
        }
    }
}

QPointF CanvasView::imageFromWidget(const QPointF& widget_point) const {
    return {pan_.x() + widget_point.x() / zoom_, pan_.y() + widget_point.y() / zoom_};
}

QPointF CanvasView::widgetFromImage(const QPointF& image_point) const {
    return {(image_point.x() - pan_.x()) * zoom_, (image_point.y() - pan_.y()) * zoom_};
}

PixelRect CanvasView::visibleImageRect() const {
    const QPointF top_left = imageFromWidget({0.0, 0.0});
    const QPointF bottom_right =
        imageFromWidget({static_cast<double>(width()), static_cast<double>(height())});
    const int x0 = static_cast<int>(std::floor(top_left.x())) - 1;
    const int y0 = static_cast<int>(std::floor(top_left.y())) - 1;
    const int x1 = static_cast<int>(std::ceil(bottom_right.x())) + 1;
    const int y1 = static_cast<int>(std::ceil(bottom_right.y())) + 1;
    return {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
}

long long CanvasView::cacheEntryCount() const {
    return static_cast<long long>(display_.width()) * display_.height();
}

void CanvasView::ensureCacheCoversView() {
    if (!doc_) return;
    const PixelRect wanted = visibleImageRect();

    // One cache entry per screen pixel, never finer than one per image pixel.
    // See the widget for the measured argument; the property that matters is
    // that everything allocated here is bounded by the window, whatever the
    // zoom.
    const SampleStep step = SampleStep::fromRatio(std::max(1.0, 1.0 / zoom_));
    const int margin = std::max(1, static_cast<int>(std::lround(kCacheMargin / zoom_)));
    const PixelRect padded =
        snapToSampleGrid(step, {wanted.x - margin, wanted.y - margin, wanted.width + 2 * margin,
                                wanted.height + 2 * margin});

    if (!display_.isNull() && step == cache_step_ && rectContains(cached_region_, wanted)) {
        const long long cached_area =
            static_cast<long long>(cached_region_.width) * cached_region_.height;
        const long long wanted_area = static_cast<long long>(wanted.width) * wanted.height;
        if (cached_area <= wanted_area * 4) return;
    }

    cache_step_ = step;
    cached_region_ = padded;

    display_ = QImage(step.entriesAcross(padded.x, padded.width),
                      step.entriesAcross(padded.y, padded.height), QImage::Format_RGB32);
    onion_dirty_ = true;
    dirty_everything_ = true;
}

void CanvasView::refreshAll() {
    dirty_everything_ = true;
    update();
}

void CanvasView::markDirty(const PixelRect& region) {
    if (dirty_everything_) return;
    pending_dirty_ = rectUnite(pending_dirty_, region);
}

void CanvasView::refreshRegion(const PixelRect& region) {
    if (display_.isNull() || !doc_) return;

    PixelRect area = rectIntersect(region, cached_region_);
    if (area.isEmpty()) return;

    area = rectIntersect(snapToSampleGrid(cache_step_, area), cached_region_);
    if (area.isEmpty()) return;

    compositor_.compositeScene(*doc_, slot_, area, scratch_, cache_step_);

    const long long first_column = cache_step_.entryAt(area.x);
    const long long first_row = cache_step_.entryAt(area.y);
    const int column_base =
        static_cast<int>(first_column - cache_step_.entryAt(cached_region_.x));
    const int row_base = static_cast<int>(first_row - cache_step_.entryAt(cached_region_.y));

    const bool checker = background_ == Background::Checker;
    const float flat = 1.0f;

    uchar* const base_line = display_.bits();
    const qsizetype stride = display_.bytesPerLine();

    const auto convert_rows = [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            const Rgba* source = scratch_.row(y);
            const int row = row_base + y;
            if (row < 0 || row >= display_.height()) continue;
            auto* destination = reinterpret_cast<QRgb*>(base_line + row * stride);
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
    convert_rows(0, std::min(rows, band));
    for (std::thread& worker : pool) worker.join();
}

void CanvasView::repaintImageRect(const PixelRect& region) {
    const QPointF top_left = widgetFromImage({static_cast<double>(region.x),
                                              static_cast<double>(region.y)});
    const QPointF bottom_right =
        widgetFromImage({static_cast<double>(region.x + region.width),
                         static_cast<double>(region.y + region.height)});
    (void)top_left; (void)bottom_right;
    update();
}

void CanvasView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        ensureCacheCoversView();
        update();
    }
}

void CanvasView::paint(QPainter* painter) {
    if (!doc_) {
        painter->fillRect(boundingRect(), QColor(48, 48, 52));
        return;
    }
    ensureCacheCoversView();

    // All the compositing for this frame happens here, once, however many
    // edits arrived since the last paint. The colour is asked for here and
    // never worked out here; a fill landing later brings the paint on itself.
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

    painter->setClipRect(boundingRect());
    painter->fillRect(boundingRect(), QColor(48, 48, 52));

    if (display_.isNull()) return;

    painter->setRenderHint(QPainter::SmoothPixmapTransform,
                           blitInterpolatesAt(cache_step_.ratio() * zoom_));

    const long long first_column = cache_step_.entryAt(cached_region_.x);
    const long long first_row = cache_step_.entryAt(cached_region_.y);
    const QPointF origin = widgetFromImage(
        {cache_step_.entryEdge(first_column), cache_step_.entryEdge(first_row)});
    const QPointF corner =
        widgetFromImage({cache_step_.entryEdge(first_column + display_.width()),
                         cache_step_.entryEdge(first_row + display_.height())});
    painter->drawImage(QRectF(origin, corner), display_);

    drawCanvasFrame(*painter);
}

// The canvas: the rectangle that will be exported, outlined, with everything
// outside it veiled.
void CanvasView::drawCanvasFrame(QPainter& painter) const {
    const PixelRect canvas = doc_->scene().canvas();
    if (canvas.isEmpty()) return;

    const QPointF top_left = widgetFromImage({static_cast<double>(canvas.x),
                                              static_cast<double>(canvas.y)});
    const QPointF bottom_right =
        widgetFromImage({static_cast<double>(canvas.x + canvas.width),
                         static_cast<double>(canvas.y + canvas.height)});
    const QRectF frame(top_left, bottom_right);

    QPainterPath outside;
    outside.addRect(boundingRect());
    outside.addRect(frame);

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.fillPath(outside, QColor(28, 28, 32, 150));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(150, 150, 160), 1.0));
    painter.drawRect(frame);
    painter.restore();
}

QImage CanvasView::grab() const {
    // Const only so the interface is honest; the deferred composite runs here,
    // the same way it runs inside paint().
    if (!doc_) return QImage();
    CanvasView* self = const_cast<CanvasView*>(this);
    self->ensureCacheCoversView();
    self->requestCtgFills();
    if (self->onion_dirty_) {
        self->rebuildOnion();
        self->onion_dirty_ = false;
        self->dirty_everything_ = true;
    }
    if (self->dirty_everything_) {
        self->refreshRegion(cached_region_);
        self->dirty_everything_ = false;
        self->pending_dirty_ = {};
    } else if (!self->pending_dirty_.isEmpty()) {
        self->refreshRegion(pending_dirty_);
        self->pending_dirty_ = {};
    }

    const int w = std::max(1, static_cast<int>(std::ceil(width())));
    const int h = std::max(1, static_cast<int>(std::ceil(height())));
    QImage out(w, h, QImage::Format_RGB32);
    out.fill(QColor(48, 48, 52));

    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          blitInterpolatesAt(cache_step_.ratio() * zoom_));
    if (!display_.isNull()) {
        const long long first_column = cache_step_.entryAt(cached_region_.x);
        const long long first_row = cache_step_.entryAt(cached_region_.y);
        const QPointF origin = widgetFromImage(
            {cache_step_.entryEdge(first_column), cache_step_.entryEdge(first_row)});
        const QPointF corner =
            widgetFromImage({cache_step_.entryEdge(first_column + display_.width()),
                             cache_step_.entryEdge(first_row + display_.height())});
        painter.drawImage(QRectF(origin, corner), display_);
    }
    drawCanvasFrame(painter);
    painter.end();
    return out;
}

// --- picking -------------------------------------------------------------

bool CanvasView::pickColourAt(const QPointF& image_point) {
    if (!doc_) return false;

    const PixelRect one{static_cast<int>(std::floor(image_point.x())),
                        static_cast<int>(std::floor(image_point.y())), 1, 1};
    Framebuffer sample;
    compositor_.compositeScene(*doc_, slot_, one, sample);
    if (sample.isEmpty()) return false;

    const Rgba pixel = sample.pixel(0, 0);
    if (pixel.a < 0.01f) return false;

    Q_EMIT colourPicked(pixel.r / pixel.a, pixel.g / pixel.a, pixel.b / pixel.a);
    return true;
}

// --- strokes -------------------------------------------------------------

void CanvasView::beginStroke(const QPointF& image_point, float pressure) {
    if (track_ == kNoId || image_ == kNoId || active_layer_ == kNoId) return;

    const Track* track = doc_->scene().findTrack(track_);
    const Layer* layer = track ? track->findLayer(active_layer_) : nullptr;
    if (!layer || layer->locked || !layer->visible) return;

    BrushSettings settings = (stylus_eraser_ || erasing_) ? eraser_settings_ : brush_settings_;
    settings.erase = stylus_eraser_ || erasing_;

    if (layer->kind == LayerKind::Ctg) {
        settings.pressure_affects_opacity = false;
        settings.hardness = 1.0f;
        settings.opacity = 1.0f;
        settings.label = true;
    } else if (!settings.erase &&
               isTransparentScribble(Rgba{settings.r, settings.g, settings.b, 1.0f})) {
        return;
    }
    brush_.settings() = settings;

    doc_->beginCommand(settings.erase ? "Erase" : "Stroke");
    stroking_ = true;

    brush_.begin(*doc_, track_, image_, active_layer_,
                 {static_cast<float>(image_point.x()), static_cast<float>(image_point.y()),
                  pressure});

    last_image_point_ = image_point;
    last_pressure_ = pressure;
    scribbling_ = layer->kind == LayerKind::Ctg;
    if (scribbling_) setScribblePreview(active_layer_, true);

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

void CanvasView::extendStroke(const QPointF& image_point, float pressure) {
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

void CanvasView::rebindStrokeToCurrentImage() {
    brush_.end();
    if (image_ == kNoId || active_layer_ == kNoId) {
        stroking_ = false;
        doc_->endCommand();
        return;
    }
    brush_.begin(*doc_, track_, image_, active_layer_,
                 {static_cast<float>(last_image_point_.x()),
                  static_cast<float>(last_image_point_.y()), last_pressure_});
}

void CanvasView::endStroke() {
    if (!stroking_) return;
    brush_.end();
    doc_->endCommand();
    stroking_ = false;
    stylus_eraser_ = false;

    if (scribbling_) {
        scribbling_ = false;
        setScribblePreview(scribble_preview_layer_, false);
    }

    refreshAll();
    Q_EMIT documentChanged();
}

// --- navigation ----------------------------------------------------------

bool CanvasView::beginNavigation(const QPointF& widget_point, Qt::MouseButton button) {
    if (button == Qt::RightButton &&
        (QGuiApplication::keyboardModifiers() & Qt::AltModifier)) {
        sizing_ = true;
        size_anchor_widget_ = widget_point;
        radius_at_press_ = brushSettings().radius;
        setCursor(QCursor(Qt::SizeHorCursor));
        return true;
    }
    if (zoom_key_held_) {
        zooming_ = true;
        zoom_anchor_widget_ = widget_point;
        zoom_at_press_ = zoom_;
        setCursor(QCursor(Qt::SizeHorCursor));
        return true;
    }
    if (button == Qt::MiddleButton || (space_held_ && button == Qt::LeftButton)) {
        panning_ = true;
        pan_anchor_widget_ = widget_point;
        pan_anchor_image_ = pan_;
        setCursor(QCursor(Qt::ClosedHandCursor));
        return true;
    }
    return false;
}

bool CanvasView::continueNavigation(const QPointF& widget_point) {
    if (sizing_) {
        const double dx = widget_point.x() - size_anchor_widget_.x();
        const float radius = static_cast<float>(
            std::clamp(radius_at_press_ * std::exp(dx * kSizeDragPerPixel), 0.5, 400.0));
        brushSettings().radius = radius;
        Q_EMIT brushSizeChanged(radius);
        return true;
    }
    if (zooming_) {
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

void CanvasView::endNavigation() {
    if (!panning_ && !zooming_ && !sizing_) return;
    panning_ = false;
    zooming_ = false;
    sizing_ = false;
    setCursor(zoom_key_held_  ? QCursor(Qt::SizeHorCursor)
              : space_held_   ? QCursor(Qt::OpenHandCursor)
                              : QCursor(Qt::CrossCursor));
}

// --- input ---------------------------------------------------------------

// Qt Quick delivers the pen as a touch event, one point, with its pressure.
// A finger is the same shape, which is right: the canvas has nothing that
// distinguishes them beyond how hard they pressed.
void CanvasView::touchEvent(QTouchEvent* event) {
    if (event->points().isEmpty()) {
        event->ignore();
        return;
    }
    const QTouchEvent::TouchPoint& point = event->points().first();
    const QPointF local = point.position();  // localized by the delivery agent
    const float pressure = static_cast<float>(std::clamp(point.pressure(), 0.0, 1.0));

    switch (point.state()) {
        case Qt::TouchPointPressed: {
            last_tablet_ms_ = clock_.elapsed();
            if (capture_focus_) forceActiveFocus();
            const QPointingDevice* device = event->pointingDevice();
            if (device &&
                device->pointerType() == QPointingDevice::PointerType::Eraser) {
                stylus_eraser_ = true;
            }

            if (QGuiApplication::keyboardModifiers() & Qt::AltModifier) {
                picking_ = true;
                pickColourAt(imageFromWidget(local));
            } else if (!beginNavigation(local, Qt::LeftButton)) {
                beginStroke(imageFromWidget(local), pressure);
            }
            event->accept();
            break;
        }
        case Qt::TouchPointMoved: {
            if (picking_) {
                pickColourAt(imageFromWidget(local));
            } else if (panning_ || zooming_ || sizing_) {
                continueNavigation(local);
            } else if (stroking_) {
                extendStroke(imageFromWidget(local), pressure);
            }
            event->accept();
            break;
        }
        case Qt::TouchPointReleased: {
            if (picking_) {
                picking_ = false;
                pickColourAt(imageFromWidget(local));
            } else if (panning_ || zooming_ || sizing_) {
                endNavigation();
            } else {
                endStroke();
            }
            event->accept();
            break;
        }
        default:
            event->ignore();
            break;
    }
}

// The tablet fallback. Qt Quick turns a real pen into touch events, so this
// path exists for events sent straight at the item -- tests above all -- and
// for platforms whose stylus events the delivery agent passes through. The
// positions are taken as item-local, which is what a directly delivered event
// carries.
bool CanvasView::handleTabletEvent(QTabletEvent* event) {
    const QPointF local = event->position();
    const float pressure = static_cast<float>(event->pressure());
    last_tablet_ms_ = clock_.elapsed();

    switch (event->type()) {
        case QEvent::TabletPress: {
            if (capture_focus_) forceActiveFocus();
            if (event->pointingDevice()->pointerType() ==
                QPointingDevice::PointerType::Eraser) {
                stylus_eraser_ = true;
            }
            if (event->modifiers() & Qt::AltModifier) {
                picking_ = true;
                pickColourAt(imageFromWidget(local));
            } else if (!beginNavigation(local, Qt::LeftButton)) {
                beginStroke(imageFromWidget(local), pressure);
            }
            event->accept();
            return true;
        }
        case QEvent::TabletMove: {
            if (picking_) {
                pickColourAt(imageFromWidget(local));
            } else if (panning_ || zooming_ || sizing_) {
                continueNavigation(local);
            } else if (stroking_) {
                extendStroke(imageFromWidget(local), pressure);
            }
            event->accept();
            return true;
        }
        case QEvent::TabletRelease: {
            if (picking_) {
                picking_ = false;
                pickColourAt(imageFromWidget(local));
            } else if (panning_ || zooming_ || sizing_) {
                endNavigation();
            } else {
                endStroke();
            }
            event->accept();
            return true;
        }
        default:
            return false;
    }
}

bool CanvasView::eventFilter(QObject* watched, QEvent* event) {
    // The events this filter itself sends come straight back through it, and an
    // unaccepted key that propagates to a parent would too; the canvas's own
    // focus path is the one legitimate route and it must be the only one.
    if (watched == this) return false;

    const bool key = event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease;
    if (!key) return false;
    auto* key_event = static_cast<QKeyEvent*>(event);
    if (key_event->key() != Qt::Key_Space && key_event->key() != Qt::Key_Z) return false;
    // Ctrl+Z and friends are shortcuts and must be left alone.
    if (key_event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    // Typing into a text editor is not a pan gesture.
    if (QObject* focus = QGuiApplication::focusObject()) {
        if (focus->inherits("QQuickTextInput") || focus->inherits("QQuickTextEdit")) {
            return false;
        }
    }
    // With the focus here, the item's own handlers already saw the event.
    if (hasActiveFocus()) return false;

    QCoreApplication::sendEvent(this, key_event);
    return true;
}

void CanvasView::setSpaceHeld(bool held) {
    if (held) {
        if (!space_held_) space_held_ = true;
    } else {
        space_held_ = false;
    }
    if (!panning_ && !zooming_ && !sizing_) {
        setCursor(space_held_ ? QCursor(Qt::OpenHandCursor)
                  : zoom_key_held_ ? QCursor(Qt::SizeHorCursor)
                                   : QCursor(Qt::CrossCursor));
    }
}

void CanvasView::setZoomHeld(bool held) {
    if (held) {
        if (!zoom_key_held_) zoom_key_held_ = true;
    } else {
        zoom_key_held_ = false;
    }
    if (!panning_ && !zooming_ && !sizing_) {
        setCursor(zoom_key_held_ ? QCursor(Qt::SizeHorCursor)
                  : space_held_ ? QCursor(Qt::OpenHandCursor)
                                : QCursor(Qt::CrossCursor));
    }
}

bool CanvasView::event(QEvent* event) {
    if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove ||
        event->type() == QEvent::TabletRelease) {
        return handleTabletEvent(static_cast<QTabletEvent*>(event));
    }
    return QQuickPaintedItem::event(event);
}

bool CanvasView::eventIsSynthesisedFromPen(QMouseEvent* event) const {
    if (comesFromAStylus(event->pointingDevice())) return true;
    if (last_tablet_ms_ < 0) return false;
    return clock_.elapsed() - last_tablet_ms_ < kPenMouseWindowMs;
}

void CanvasView::mousePressEvent(QMouseEvent* event) {
    if (capture_focus_) forceActiveFocus();
    if (beginNavigation(event->position(), event->button())) return;
    if (eventIsSynthesisedFromPen(event)) return;
    if (event->button() != Qt::LeftButton) return;
    if (event->modifiers() & Qt::AltModifier) {
        picking_ = true;
        pickColourAt(imageFromWidget(event->position()));
        return;
    }
    beginStroke(imageFromWidget(event->position()), 1.0f);
}

void CanvasView::mouseMoveEvent(QMouseEvent* event) {
    if (continueNavigation(event->position())) return;
    if (eventIsSynthesisedFromPen(event)) return;
    if (picking_) {
        pickColourAt(imageFromWidget(event->position()));
        return;
    }
    if (stroking_) extendStroke(imageFromWidget(event->position()), 1.0f);
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event) {
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

void CanvasView::setZoom(double zoom, const QPointF& widget_anchor) {
    const double clamped = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (std::abs(clamped - zoom_) < 1e-9) return;

    const QPointF before = imageFromWidget(widget_anchor);
    zoom_ = clamped;
    const QPointF after = imageFromWidget(widget_anchor);
    pan_ = onWholeScreenPixels(pan_ + (before - after), zoom_);

    ensureCacheCoversView();
    update();
    Q_EMIT viewChanged();
}

void CanvasView::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) return;
    setZoom(zoom_ * std::pow(1.2, steps), event->position());
    event->accept();
}

void CanvasView::hoverMoveEvent(QHoverEvent* event) {
    const bool over = boundingRect().contains(event->position());
    setCursor(over && !panning_ && !zooming_ && !sizing_ ? QCursor(Qt::CrossCursor) : QCursor(Qt::ArrowCursor));
    QQuickPaintedItem::hoverMoveEvent(event);
}

void CanvasView::resetView() {
    zoom_ = 1.0;
    pan_ = {0.0, 0.0};
    ensureCacheCoversView();
    update();
    Q_EMIT viewChanged();
}

void CanvasView::fitToCanvas() { fitTo(doc_->scene().canvas()); }

void CanvasView::fitToDrawing() { fitTo(imageBounds(*doc_, track_, image_)); }

void CanvasView::fitTo(const PixelRect& bounds) {
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

// Space and Z are held modifiers for panning and zooming, not shortcuts, so
// they live in the item's key handling and need it to hold the focus. The
// interface gives the canvas the focus when the pointer lands on it, which is
// the only place a hand expects the keys to matter.
void CanvasView::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Space:
            if (!event->isAutoRepeat()) {
                space_held_ = true;
                if (!panning_ && !zooming_ && !sizing_) setCursor(QCursor(Qt::OpenHandCursor));
            }
            event->accept();
            return;
        case Qt::Key_Z:
            if (!event->isAutoRepeat()) {
                zoom_key_held_ = true;
                if (!panning_ && !zooming_ && !sizing_) setCursor(QCursor(Qt::SizeHorCursor));
            }
            event->accept();
            return;
        default:
            break;
    }
    QQuickPaintedItem::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Space:
            if (!event->isAutoRepeat()) {
                space_held_ = false;
                if (!panning_ && !zooming_ && !sizing_) {
                    setCursor(zoom_key_held_ ? QCursor(Qt::SizeHorCursor) : QCursor(Qt::CrossCursor));
                }
            }
            event->accept();
            return;
        case Qt::Key_Z:
            if (!event->isAutoRepeat()) {
                zoom_key_held_ = false;
                if (!panning_ && !zooming_ && !sizing_) {
                    setCursor(space_held_ ? QCursor(Qt::OpenHandCursor) : QCursor(Qt::CrossCursor));
                }
            }
            event->accept();
            return;
        default:
            break;
    }
    QQuickPaintedItem::keyReleaseEvent(event);
}
