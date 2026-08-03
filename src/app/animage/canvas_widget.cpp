// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_widget.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>

#include "color.h"

using namespace animage;

namespace {

constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 32.0;
constexpr int kCheckerSize = 8;
constexpr double kScrubbyZoomPerPixel = 0.006;
constexpr double kSizeDragPerPixel = 0.012;
// Cached beyond the viewport, so a pan of a few pixels does not recomposite.
constexpr int kCacheMargin = 192;

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
}

void CanvasWidget::setTimeline(TimelineId timeline) {
    timeline_ = timeline;
    setFrame(slot_);
}

void CanvasWidget::setFrame(std::size_t slot) {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline) return;

    slot_ = std::min(slot, timeline->slots.empty() ? std::size_t{0}
                                                   : timeline->slots.size() - 1);
    const ImageId next = timeline->imageAtSlot(slot_);
    const bool changed = next != image_;
    image_ = next;

    // A stroke in progress when the frame changes carries on onto the new
    // drawing. Holding the pen down through playback then leaves a mark on
    // each frame it passed over, which is how you sketch a moving point.
    if (changed && stroking_) rebindStrokeToCurrentImage();

    rebuildOnion();
    refreshAll();
}

void CanvasWidget::setActiveLayer(LayerId layer) { active_layer_ = layer; }

void CanvasWidget::setEraser(bool erasing) { erasing_ = erasing; }

void CanvasWidget::setBackground(Background background) {
    background_ = background;
    refreshAll();
}

void CanvasWidget::setOnion(const OnionSettings& settings) {
    onion_settings_ = settings;
    rebuildOnion();
    refreshAll();
}

void CanvasWidget::setPlaying(bool playing) {
    if (playing_ == playing) return;
    playing_ = playing;
    rebuildOnion();
    refreshAll();
}

// Flattens the neighbouring drawings into one tinted, faded layer. Previous
// drawings go warm and later ones cool, which is the convention every animator
// already reads without being told.
void CanvasWidget::rebuildOnion() {
    onion_.resize(0, 0);
    if (playing_ || timeline_ == kNoId) return;
    if (onion_settings_.before <= 0 && onion_settings_.after <= 0) return;

    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline || cached_region_.isEmpty()) return;

    onion_.resize((cached_region_.width + cache_step_ - 1) / cache_step_,
                  (cached_region_.height + cache_step_ - 1) / cache_step_);

    struct Ghost {
        ImageId image;
        float weight;
        float r, g, b;
    };
    std::vector<Ghost> ghosts;

    const auto collect = [&](int count, int direction, float r, float g, float b) {
        if (count <= 0) return;
        const std::vector<ImageId> neighbours =
            timeline->distinctNeighbours(slot_, count, direction);
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
        compositor_.composite(doc_, timeline_, ghost.image, cached_region_, ghost_frame,
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

void CanvasWidget::ensureCacheCoversView() {
    const PixelRect wanted = visibleImageRect();
    const int step = std::max(1, static_cast<int>(std::floor(1.0 / zoom_)));

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
    cached_region_ = {wanted.x - kCacheMargin * step, wanted.y - kCacheMargin * step,
                      wanted.width + 2 * kCacheMargin * step,
                      wanted.height + 2 * kCacheMargin * step};

    display_ = QImage((cached_region_.width + step - 1) / step,
                      (cached_region_.height + step - 1) / step, QImage::Format_RGB32);
    rebuildOnion();
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
    if (display_.isNull() || timeline_ == kNoId || image_ == kNoId) return;

    PixelRect area = intersect(region, cached_region_);
    if (area.isEmpty()) return;

    // Snap to the sampling grid so cache entries line up with image pixels.
    const int step = cache_step_;
    if (step > 1) {
        const int dx = (area.x - cached_region_.x) % step;
        const int dy = (area.y - cached_region_.y) % step;
        area = {area.x - dx, area.y - dy, area.width + dx + step, area.height + dy + step};
        area = intersect(area, cached_region_);
        if (area.isEmpty()) return;
    }

    compositor_.composite(doc_, timeline_, image_, area, scratch_, step);

    const bool checker = background_ == Background::Checker;
    const float flat = 1.0f;

    for (int y = 0; y < scratch_.height(); ++y) {
        const Rgba* source = scratch_.row(y);
        const int image_y = area.y + y * step;
        const int row = (image_y - cached_region_.y) / step;
        if (row < 0 || row >= display_.height()) continue;
        auto* destination = reinterpret_cast<QRgb*>(display_.scanLine(row));

        for (int x = 0; x < scratch_.width(); ++x) {
            const int image_x = area.x + x * step;
            const int column = (image_x - cached_region_.x) / step;
            if (column < 0 || column >= display_.width()) continue;
            float background = flat;
            if (checker) {
                const bool light = (((image_x / kCheckerSize) + (image_y / kCheckerSize)) & 1) == 0;
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

    // Nearest-neighbour when magnified: an animator zooming in wants to see the
    // pixels, not a guess at what is between them.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1.0);

    const QPointF origin = widgetFromImage({static_cast<double>(cached_region_.x),
                                            static_cast<double>(cached_region_.y)});
    const QRectF target(origin, QSizeF(display_.width() * cache_step_ * zoom_,
                                       display_.height() * cache_step_ * zoom_));
    painter.drawImage(target, display_);
}

// --- strokes -------------------------------------------------------------

void CanvasWidget::beginStroke(const QPointF& image_point, float pressure) {
    if (timeline_ == kNoId || image_ == kNoId || active_layer_ == kNoId) return;

    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    const Layer* layer = timeline ? timeline->findLayer(active_layer_) : nullptr;
    if (!layer || layer->locked || !layer->visible) return;

    BrushSettings settings = (stylus_eraser_ || erasing_) ? eraser_settings_ : brush_settings_;
    settings.erase = stylus_eraser_ || erasing_;
    brush_.settings() = settings;

    doc_.beginCommand(settings.erase ? "Erase" : "Stroke");
    stroking_ = true;

    brush_.begin(doc_, timeline_, image_, active_layer_,
                 {static_cast<float>(image_point.x()), static_cast<float>(image_point.y()),
                  pressure});

    last_image_point_ = image_point;
    last_pressure_ = pressure;

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
    brush_.begin(doc_, timeline_, image_, active_layer_,
                 {static_cast<float>(last_image_point_.x()),
                  static_cast<float>(last_image_point_.y()), last_pressure_});
}

void CanvasWidget::endStroke() {
    if (!stroking_) return;
    brush_.end();
    doc_.endCommand();
    stroking_ = false;
    stylus_eraser_ = false;
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
        pan_ = {pan_anchor_image_.x() - moved.x() / zoom_,
                pan_anchor_image_.y() - moved.y() / zoom_};
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
    event->accept();
    ++tablet_events_seen_;

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

// Windows Ink synthesises a mouse event for every pen event. Acting on those
// would draw the stroke a second time at an invented pressure.
bool CanvasWidget::eventIsSynthesisedFromPen(QMouseEvent* event) const {
    return comesFromAStylus(event->pointingDevice()) || tablet_events_seen_ > 0;
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    if (beginNavigation(event->position(), event->button())) return;
    if (eventIsSynthesisedFromPen(event)) return;
    if (event->button() != Qt::LeftButton) return;
    beginStroke(imageFromWidget(event->position()), 1.0f);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (continueNavigation(event->position())) return;
    if (eventIsSynthesisedFromPen(event)) return;
    if (stroking_) extendStroke(imageFromWidget(event->position()), 1.0f);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_ || zooming_ || sizing_) {
        endNavigation();
        return;
    }
    if (eventIsSynthesisedFromPen(event)) return;
    endStroke();
}

void CanvasWidget::setZoom(double zoom, const QPointF& widget_anchor) {
    const double clamped = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (std::abs(clamped - zoom_) < 1e-9) return;

    // Keep the image point under the anchor where it is.
    const QPointF before = imageFromWidget(widget_anchor);
    zoom_ = clamped;
    const QPointF after = imageFromWidget(widget_anchor);
    pan_ += before - after;

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

void CanvasWidget::fitToDrawing() {
    const PixelRect bounds = imageBounds(doc_, timeline_, image_);
    if (bounds.isEmpty() || width() <= 0 || height() <= 0) {
        resetView();
        return;
    }

    const double scale_x = static_cast<double>(width()) / bounds.width;
    const double scale_y = static_cast<double>(height()) / bounds.height;
    zoom_ = std::clamp(std::min(scale_x, scale_y) * 0.9, kMinZoom, kMaxZoom);
    pan_ = {bounds.x + bounds.width / 2.0 - width() / (2.0 * zoom_),
            bounds.y + bounds.height / 2.0 - height() / (2.0 * zoom_)};

    ensureCacheCoversView();
    update();
    Q_EMIT viewChanged();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        QWidget::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
        case Qt::Key_Space:
            space_held_ = true;
            if (!panning_ && !zooming_ && !sizing_) setCursor(Qt::OpenHandCursor);
            return;
        case Qt::Key_Z:
            // Held, not toggled: a zoom you have to switch back out of costs
            // more attention than the zoom is worth.
            zoom_key_held_ = true;
            if (!panning_ && !zooming_ && !sizing_) setCursor(Qt::SizeHorCursor);
            return;
        default: break;
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        QWidget::keyReleaseEvent(event);
        return;
    }
    switch (event->key()) {
        case Qt::Key_Space:
            space_held_ = false;
            if (!panning_ && !zooming_ && !sizing_) {
                setCursor(zoom_key_held_ ? Qt::SizeHorCursor : Qt::CrossCursor);
            }
            return;
        case Qt::Key_Z:
            zoom_key_held_ = false;
            if (!panning_ && !zooming_ && !sizing_) {
                setCursor(space_held_ ? Qt::OpenHandCursor : Qt::CrossCursor);
            }
            return;
        default: break;
    }
    QWidget::keyReleaseEvent(event);
}
