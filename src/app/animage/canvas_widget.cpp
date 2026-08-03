// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_widget.h"

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

}  // namespace

CanvasWidget::CanvasWidget(Document& document, QWidget* parent)
    : QWidget(parent), doc_(document) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TabletTracking);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);

    BrushSettings settings;
    settings.radius = 6.0f;
    settings.hardness = 0.6f;
    settings.pressure_affects_opacity = true;
    settings.r = settings.g = settings.b = 0.0f;
    settings.a = 1.0f;
    brush_.settings() = settings;
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
    image_ = timeline->imageAtSlot(slot_);
    rebuildOnion();
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

    onion_.resize(cached_region_.width, cached_region_.height);

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
        compositor_.composite(doc_, timeline_, ghost.image, cached_region_, ghost_frame);
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

void CanvasWidget::setActiveLayer(LayerId layer) { active_layer_ = layer; }

void CanvasWidget::setEraser(bool erasing) { brush_.settings().erase = erasing; }

void CanvasWidget::setBackground(Background background) {
    background_ = background;
    refreshAll();
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
    if (wanted.x == cached_region_.x && wanted.y == cached_region_.y &&
        wanted.width == cached_region_.width && wanted.height == cached_region_.height &&
        !display_.isNull()) {
        return;
    }

    cached_region_ = wanted;
    display_ = QImage(wanted.width, wanted.height, QImage::Format_RGB32);
    rebuildOnion();
    refreshRegion(wanted);
}

void CanvasWidget::refreshAll() {
    if (!display_.isNull()) refreshRegion(cached_region_);
    update();
}

// Composites `region` and writes it into the cached sRGB image. This is the
// only place the linear working space becomes display pixels.
void CanvasWidget::refreshRegion(const PixelRect& region) {
    if (display_.isNull() || timeline_ == kNoId || image_ == kNoId) return;

    const PixelRect area = intersect(region, cached_region_);
    if (area.isEmpty()) return;

    compositor_.composite(doc_, timeline_, image_, area, scratch_);

    const bool checker = background_ == Background::Checker;
    const float flat = (background_ == Background::Black) ? 0.0f : 1.0f;

    for (int y = 0; y < area.height; ++y) {
        const Rgba* source = scratch_.row(y);
        const int image_y = area.y + y;
        auto* destination =
            reinterpret_cast<QRgb*>(display_.scanLine(image_y - cached_region_.y));

        for (int x = 0; x < area.width; ++x) {
            const int image_x = area.x + x;
            float background = flat;
            if (checker) {
                const bool light = (((image_x / kCheckerSize) + (image_y / kCheckerSize)) & 1) == 0;
                background = light ? 1.0f : 0.78f;
            }

            // Paper, then the onion skin over it, then the drawing over that.
            Rgba base{background, background, background, 1.0f};
            if (!onion_.isEmpty()) {
                const Rgba ghost = onion_.row(image_y - cached_region_.y)[image_x -
                                                                          cached_region_.x];
                base = over(ghost, base);
            }

            const Rgba& pixel = source[x];
            const float keep = 1.0f - std::clamp(pixel.a, 0.0f, 1.0f);
            const float r = pixel.r + base.r * keep;
            const float g = pixel.g + base.g * keep;
            const float b = pixel.b + base.b * keep;

            destination[image_x - cached_region_.x] =
                qRgb(toSrgbByte(r), toSrgbByte(g), toSrgbByte(b));
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

void CanvasWidget::resizeEvent(QResizeEvent*) { ensureCacheCoversView(); }

void CanvasWidget::paintEvent(QPaintEvent* event) {
    ensureCacheCoversView();

    QPainter painter(this);
    painter.setClipRect(event->rect());
    painter.fillRect(event->rect(), QColor(48, 48, 52));

    if (display_.isNull()) return;

    // Nearest-neighbour when magnified: an animator zooming in wants to see the
    // pixels, not a guess at what is between them.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1.0);

    const QPointF origin = widgetFromImage({static_cast<double>(cached_region_.x),
                                            static_cast<double>(cached_region_.y)});
    const QRectF target(origin, QSizeF(cached_region_.width * zoom_, cached_region_.height * zoom_));
    painter.drawImage(target, display_);
}

// --- input ---------------------------------------------------------------

void CanvasWidget::beginStroke(const QPointF& image_point, float pressure) {
    if (timeline_ == kNoId || image_ == kNoId || active_layer_ == kNoId) return;

    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    const Layer* layer = timeline ? timeline->findLayer(active_layer_) : nullptr;
    if (!layer || layer->locked || !layer->visible) return;

    doc_.beginCommand(brush_.settings().erase ? "Erase" : "Stroke");
    stroking_ = true;

    const StrokePoint point{static_cast<float>(image_point.x()),
                            static_cast<float>(image_point.y()), pressure};
    brush_.begin(doc_, timeline_, image_, active_layer_, point);

    last_image_point_ = image_point;
    last_radius_ = brush_.settings().radius;

    const int radius = static_cast<int>(std::ceil(last_radius_)) + 2;
    const PixelRect dirty{static_cast<int>(std::floor(image_point.x())) - radius,
                          static_cast<int>(std::floor(image_point.y())) - radius, 2 * radius,
                          2 * radius};
    refreshRegion(dirty);
    repaintImageRect(dirty);
}

void CanvasWidget::extendStroke(const QPointF& image_point, float pressure) {
    if (!stroking_) return;

    brush_.extend({static_cast<float>(image_point.x()), static_cast<float>(image_point.y()),
                   pressure});

    // Recomposite only what this segment could have touched.
    const int radius = static_cast<int>(std::ceil(brush_.settings().radius)) + 2;
    const QRectF segment = QRectF(last_image_point_, image_point).normalized();
    const PixelRect dirty{static_cast<int>(std::floor(segment.left())) - radius,
                          static_cast<int>(std::floor(segment.top())) - radius,
                          static_cast<int>(std::ceil(segment.width())) + 2 * radius,
                          static_cast<int>(std::ceil(segment.height())) + 2 * radius};
    refreshRegion(dirty);
    repaintImageRect(dirty);

    last_image_point_ = image_point;
}

void CanvasWidget::endStroke() {
    if (!stroking_) return;
    brush_.end();
    doc_.endCommand();
    stroking_ = false;
    Q_EMIT documentChanged();
}

void CanvasWidget::tabletEvent(QTabletEvent* event) {
    event->accept();
    ++tablet_events_seen_;

    if (space_held_) return;  // panning takes precedence

    const QPointingDevice* device = event->pointingDevice();
    const bool eraser_end =
        device && device->pointerType() == QPointingDevice::PointerType::Eraser;
    const bool was_erasing = brush_.settings().erase;
    if (eraser_end) brush_.settings().erase = true;

    const QPointF image_point = imageFromWidget(event->position());
    const float pressure = static_cast<float>(event->pressure());

    switch (event->type()) {
        case QEvent::TabletPress: beginStroke(image_point, pressure); break;
        case QEvent::TabletMove:
            if (stroking_) extendStroke(image_point, pressure);
            break;
        case QEvent::TabletRelease:
            endStroke();
            if (eraser_end) brush_.settings().erase = was_erasing;
            break;
        default: break;
    }
}

// Windows Ink synthesises a mouse event for every pen event. Acting on those
// would draw the stroke a second time at an invented pressure.
bool CanvasWidget::eventIsSynthesisedFromPen(QMouseEvent* event) const {
    return comesFromAStylus(event->pointingDevice()) || tablet_events_seen_ > 0;
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    const bool wants_pan = event->button() == Qt::MiddleButton ||
                           (space_held_ && event->button() == Qt::LeftButton);
    if (wants_pan) {
        panning_ = true;
        pan_anchor_widget_ = event->position();
        pan_anchor_image_ = pan_;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (eventIsSynthesisedFromPen(event)) return;
    if (event->button() != Qt::LeftButton) return;
    beginStroke(imageFromWidget(event->position()), 1.0f);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QPointF moved = event->position() - pan_anchor_widget_;
        pan_ = {pan_anchor_image_.x() - moved.x() / zoom_,
                pan_anchor_image_.y() - moved.y() / zoom_};
        ensureCacheCoversView();
        update();
        Q_EMIT viewChanged();
        return;
    }

    if (eventIsSynthesisedFromPen(event)) return;
    if (stroking_) extendStroke(imageFromWidget(event->position()), 1.0f);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_) {
        panning_ = false;
        setCursor(space_held_ ? Qt::OpenHandCursor : Qt::CrossCursor);
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
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        space_held_ = true;
        if (!panning_) setCursor(Qt::OpenHandCursor);
        return;
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        space_held_ = false;
        if (!panning_) setCursor(Qt::CrossCursor);
        return;
    }
    QWidget::keyReleaseEvent(event);
}
