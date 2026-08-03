// SPDX-License-Identifier: GPL-3.0-or-later
#include "latency_canvas.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QResizeEvent>
#include <QScreen>
#include <QTabletEvent>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kSampleWindow = 240;
constexpr qreal kMinWidth = 1.5;
constexpr qreal kMaxWidth = 26.0;
constexpr int kCrosshairArm = 22;
constexpr qint64 kHudRebuildIntervalNs = 100'000'000;  // 10 Hz is plenty for text

QString pointerTypeName(QPointingDevice::PointerType type) {
    switch (type) {
        case QPointingDevice::PointerType::Pen: return QStringLiteral("pen");
        case QPointingDevice::PointerType::Eraser: return QStringLiteral("eraser");
        case QPointingDevice::PointerType::Cursor: return QStringLiteral("puck");
        case QPointingDevice::PointerType::Finger: return QStringLiteral("finger");
        case QPointingDevice::PointerType::Generic: return QStringLiteral("generic");
        default: return QStringLiteral("unknown");
    }
}

bool comesFromAStylus(const QPointingDevice* device) {
    if (!device) return false;
    const QInputDevice::DeviceType type = device->type();
    return type == QInputDevice::DeviceType::Stylus ||
           type == QInputDevice::DeviceType::Airbrush || type == QInputDevice::DeviceType::Puck;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = std::min(
        values.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(values.size())));
    return values[index];
}

}  // namespace

LatencyCanvas::LatencyCanvas(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TabletTracking);  // deliver hover events, not just contact
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    clock_.start();

    // The HUD repaints on its own slow schedule rather than riding along with
    // every pen event. Laying out fourteen lines of text per pen event costs
    // more than drawing the stroke does.
    hud_timer_ = new QTimer(this);
    hud_timer_->setInterval(100);
    connect(hud_timer_, &QTimer::timeout, this, [this] {
        if (show_hud_ && !hud_rect_.isEmpty()) update(hud_rect_);
    });
    hud_timer_->start();
}

void LatencyCanvas::clear() {
    canvas_.fill(Qt::black);
    latency_newest_ms_.clear();
    latency_oldest_ms_.clear();
    intervals_ms_.clear();
    tablet_events_ = 0;
    mouse_events_ = 0;
    mouse_from_pen_ = 0;
    frames_ = 0;
    update();
}

void LatencyCanvas::resizeEvent(QResizeEvent* event) {
    const QSize size = event->size() * devicePixelRatioF();
    if (canvas_.size() == size) return;

    QImage resized(size, QImage::Format_RGB32);
    resized.setDevicePixelRatio(devicePixelRatioF());
    resized.fill(Qt::black);
    if (!canvas_.isNull()) {
        QPainter painter(&resized);
        painter.drawImage(0, 0, canvas_);
    }
    canvas_ = resized;
}

QRect LatencyCanvas::crosshairRect(const QPointF& centre) const {
    return QRect(static_cast<int>(centre.x()) - kCrosshairArm - 2,
                 static_cast<int>(centre.y()) - kCrosshairArm - 2, 2 * kCrosshairArm + 5,
                 2 * kCrosshairArm + 5);
}

void LatencyCanvas::noteEventArrival() {
    const qint64 now = clock_.nsecsElapsed();
    if (last_event_ns_ >= 0) {
        intervals_ms_.push_back(static_cast<double>(now - last_event_ns_) / 1e6);
        if (intervals_ms_.size() > kSampleWindow) intervals_ms_.pop_front();
    }
    last_event_ns_ = now;

    if (pending_oldest_ns_ < 0) pending_oldest_ns_ = now;
    pending_newest_ns_ = now;
}

void LatencyCanvas::moveCursorTo(const QPointF& position) {
    if (show_crosshair_ && cursor_valid_) update(crosshairRect(cursor_));
    cursor_ = position;
    cursor_valid_ = true;
    if (show_crosshair_) update(crosshairRect(cursor_));
}

void LatencyCanvas::beginStroke(const QPointF& position, qreal pressure) {
    drawing_ = true;
    last_position_ = position;
    (void)pressure;
}

void LatencyCanvas::extendStroke(const QPointF& position, qreal pressure) {
    if (!drawing_ || canvas_.isNull()) return;

    const qreal width = kMinWidth + (kMaxWidth - kMinWidth) * std::max(pressure, qreal(0.02));

    QPainter painter(&canvas_);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(erasing_ ? QColor(0, 0, 0) : QColor(255, 255, 255));
    pen.setWidthF(width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawLine(last_position_, position);

    // Repaint only what changed. Blitting the whole canvas on every pen event
    // is work proportional to the window, not to the stroke, and at 200 events
    // per second that is most of the frame budget on a large window.
    const qreal margin = width * 0.5 + 2.0;
    const QRectF segment = QRectF(last_position_, position).normalized();
    update(segment.adjusted(-margin, -margin, margin, margin).toAlignedRect());

    last_position_ = position;
}

void LatencyCanvas::endStroke() { drawing_ = false; }

void LatencyCanvas::tabletEvent(QTabletEvent* event) {
    event->accept();
    ++tablet_events_;
    noteEventArrival();

    const QPointingDevice* device = event->pointingDevice();
    if (device) {
        device_name_ = device->name().isEmpty() ? QStringLiteral("unnamed") : device->name();
        pointer_kind_ = pointerTypeName(device->pointerType());
        erasing_ = device->pointerType() == QPointingDevice::PointerType::Eraser;
    }

    pressure_ = event->pressure();
    tilt_x_ = event->xTilt();
    tilt_y_ = event->yTilt();

    switch (event->type()) {
        case QEvent::TabletPress: beginStroke(event->position(), event->pressure()); break;
        case QEvent::TabletMove:
            if (drawing_) extendStroke(event->position(), event->pressure());
            break;
        case QEvent::TabletRelease: endStroke(); break;
        default: break;
    }

    moveCursorTo(event->position());
}

// Windows Ink synthesises a mouse event for every pen event so that
// tablet-unaware applications still work. Acting on those would draw the same
// stroke twice, at a made-up pressure, and double the work in the one code path
// being measured. They are counted and then dropped.
//
// Returns true when the event was a real mouse and should still be drawn with.
bool LatencyCanvas::handleAsMouse(QMouseEvent* event) {
    ++mouse_events_;
    const bool synthesised = comesFromAStylus(event->pointingDevice()) || tablet_events_ > 0;
    if (synthesised) {
        ++mouse_from_pen_;
        return false;
    }
    return true;
}

void LatencyCanvas::mousePressEvent(QMouseEvent* event) {
    if (!handleAsMouse(event)) return;
    noteEventArrival();
    erasing_ = false;
    beginStroke(event->position(), 0.5);
    moveCursorTo(event->position());
}

void LatencyCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!handleAsMouse(event)) return;
    noteEventArrival();
    if (drawing_) extendStroke(event->position(), 0.5);
    moveCursorTo(event->position());
}

void LatencyCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (!handleAsMouse(event)) return;
    endStroke();
}

LatencyCanvas::Stats LatencyCanvas::latencyStats(const std::deque<double>& samples) const {
    const std::vector<double> values(samples.begin(), samples.end());
    if (values.empty()) return {};
    Stats stats;
    stats.median = percentile(values, 0.5);
    stats.p95 = percentile(values, 0.95);
    stats.worst = *std::max_element(values.begin(), values.end());
    return stats;
}

double LatencyCanvas::eventRateHz() const {
    if (intervals_ms_.empty()) return 0.0;
    double total = 0.0;
    for (double interval : intervals_ms_) total += interval;
    const double mean = total / static_cast<double>(intervals_ms_.size());
    return (mean > 0.0) ? 1000.0 / mean : 0.0;
}

void LatencyCanvas::rebuildHud() {
    const Stats newest = latencyStats(latency_newest_ms_);
    const Stats oldest = latencyStats(latency_oldest_ms_);
    const qreal refresh = screen() ? screen()->refreshRate() : 0.0;
    const double frame_ms = (refresh > 0.0) ? 1000.0 / refresh : 0.0;

    QStringList lines;
    lines << QStringLiteral("device      %1  (%2)").arg(device_name_, pointer_kind_);
    lines << QStringLiteral("tablet ev   %1").arg(tablet_events_);
    lines << QStringLiteral("mouse ev    %1   of which synthesised from the pen: %2")
                 .arg(mouse_events_)
                 .arg(mouse_from_pen_);
    lines << QStringLiteral("pressure    %1        tilt  %2 / %3")
                 .arg(pressure_, 0, 'f', 3)
                 .arg(tilt_x_, 0, 'f', 1)
                 .arg(tilt_y_, 0, 'f', 1);
    lines << QStringLiteral("event rate  %1 Hz").arg(eventRateHz(), 0, 'f', 0);
    lines << QStringLiteral("screen      %1 Hz  = %2 ms per frame   painted %3")
                 .arg(refresh, 0, 'f', 0)
                 .arg(frame_ms, 0, 'f', 1)
                 .arg(frames_);
    lines << QString();
    lines << QStringLiteral("ink lag, application only:  median %1  p95 %2  worst %3 ms")
                 .arg(newest.median, 0, 'f', 2)
                 .arg(newest.p95, 0, 'f', 2)
                 .arg(newest.worst, 0, 'f', 2);
    lines << QStringLiteral("queue depth, oldest event:  median %1  p95 %2  worst %3 ms")
                 .arg(oldest.median, 0, 'f', 2)
                 .arg(oldest.p95, 0, 'f', 2)
                 .arg(oldest.worst, 0, 'f', 2);
    lines << QString();
    lines << QStringLiteral("Neither is the answer. Film the pen tip and the screen at 120 fps");
    lines << QStringLiteral("or more and count frames. Pass: under 25 ms.");
    lines << QString();
    lines << QStringLiteral("M camera mode (on-screen clock, for filming)");
    lines << QStringLiteral("C clear   H hud   X crosshair   S report   F fullscreen   Q quit");

    QFont font(QStringLiteral("Consolas"));
    font.setPointSizeF(10.0);
    font.setStyleHint(QFont::Monospace);

    const QFontMetricsF metrics(font);
    qreal widest = 0.0;
    for (const QString& line : lines) widest = std::max(widest, metrics.horizontalAdvance(line));
    const qreal line_height = metrics.height();

    const QSizeF panel(widest + 24, line_height * lines.size() + 20);
    hud_rect_ = QRect(12, 12, static_cast<int>(panel.width()) + 1,
                      static_cast<int>(panel.height()) + 1);

    const qreal dpr = devicePixelRatioF();
    hud_ = QPixmap(hud_rect_.size() * dpr);
    hud_.setDevicePixelRatio(dpr);
    hud_.fill(Qt::transparent);

    QPainter painter(&hud_);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 200));
    painter.drawRoundedRect(QRectF(0, 0, panel.width(), panel.height()), 6, 6);

    painter.setFont(font);
    painter.setPen(QColor(220, 220, 220));
    qreal y = 10 + metrics.ascent();
    for (const QString& line : lines) {
        painter.drawText(QPointF(12, y), line);
        y += line_height;
    }

    hud_built_ns_ = clock_.nsecsElapsed();
}

// The problem with counting frames in a video is that you have to know what the
// camera was doing. Phones relabel their slow-motion rate freely and often do
// not report it at all.
//
// So put the clock on the screen instead. These digits are a real timestamp,
// not a frame counter: read them in the frame where the pen tip reaches a spot,
// read them again in the frame where ink appears there, subtract. The camera's
// frame rate never enters the arithmetic.
//
// Resolution is still one display refresh, because that is how often the digits
// can change. On a 60 Hz panel that is about 17 ms of granularity.
void LatencyCanvas::paintCameraClock(QPainter& painter) {
    const qint64 elapsed_ms = clock_.elapsed();

    QFont digits(QStringLiteral("Consolas"));
    digits.setPointSizeF(64.0);
    digits.setBold(true);
    digits.setStyleHint(QFont::Monospace);
    painter.setFont(digits);

    const QFontMetrics metrics(digits);
    const QString text = QStringLiteral("%1").arg(elapsed_ms % 10000, 4, 10, QLatin1Char('0'));
    const int block = metrics.height();

    if (clock_rect_.isEmpty()) {
        clock_rect_ = QRect(24, 24, metrics.horizontalAdvance(QStringLiteral("0000")) + block + 36,
                            block + 24);
    }

    painter.fillRect(clock_rect_, Qt::black);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(clock_rect_.adjusted(18, 12, 0, 0), Qt::AlignLeft | Qt::AlignTop, text);

    // A square that flips every refresh. Digits smear when the exposure is
    // long; a block that is simply light or dark survives that, and it makes a
    // skipped frame obvious.
    const QRect flip(clock_rect_.right() - block - 12, clock_rect_.top() + 12, block, block);
    painter.fillRect(flip, ((elapsed_ms / 8) % 2) ? QColor(255, 255, 255) : QColor(40, 40, 40));

    // Keep repainting so the clock keeps ticking when the pen is still.
    update(clock_rect_);
}

void LatencyCanvas::paintEvent(QPaintEvent* event) {
    const qint64 now = clock_.nsecsElapsed();
    if (pending_newest_ns_ >= 0) {
        latency_newest_ms_.push_back(static_cast<double>(now - pending_newest_ns_) / 1e6);
        latency_oldest_ms_.push_back(static_cast<double>(now - pending_oldest_ns_) / 1e6);
        if (latency_newest_ms_.size() > kSampleWindow) latency_newest_ms_.pop_front();
        if (latency_oldest_ms_.size() > kSampleWindow) latency_oldest_ms_.pop_front();
        pending_oldest_ns_ = -1;
        pending_newest_ns_ = -1;
    }
    ++frames_;

    const QRect dirty = event->rect();

    QPainter painter(this);
    painter.setClipRect(dirty);
    if (!canvas_.isNull()) painter.drawImage(0, 0, canvas_);

    // The gap between the physical pen tip and this crosshair while the pen is
    // moving steadily is the latency, made visible. It is the cheapest estimate
    // available without a camera.
    if (camera_mode_) {
        paintCameraClock(painter);
        return;  // nothing else on screen: overlays are easy to mistake for ink
    }

    if (show_crosshair_ && cursor_valid_ && dirty.intersects(crosshairRect(cursor_))) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(255, 64, 64));
        pen.setWidthF(1.5);
        painter.setPen(pen);
        painter.drawLine(QPointF(cursor_.x() - kCrosshairArm, cursor_.y()),
                         QPointF(cursor_.x() + kCrosshairArm, cursor_.y()));
        painter.drawLine(QPointF(cursor_.x(), cursor_.y() - kCrosshairArm),
                         QPointF(cursor_.x(), cursor_.y() + kCrosshairArm));
    }

    if (!show_hud_) return;
    if (hud_.isNull() || now - hud_built_ns_ > kHudRebuildIntervalNs) rebuildHud();
    if (dirty.intersects(hud_rect_)) painter.drawPixmap(hud_rect_.topLeft(), hud_);
}

void LatencyCanvas::printReport() const {
    const Stats newest = latencyStats(latency_newest_ms_);
    const Stats oldest = latencyStats(latency_oldest_ms_);
    const qreal refresh = screen() ? screen()->refreshRate() : 0.0;

    QTextStream out(stdout);
    out << Qt::fixed << qSetRealNumberPrecision(2);
    out << "\n--- M0 latency report ---\n"
        << "device        " << device_name_ << " (" << pointer_kind_ << ")\n"
        << "tablet events " << tablet_events_ << "\n"
        << "mouse events  " << mouse_events_ << " (" << mouse_from_pen_
        << " synthesised from the pen and ignored)\n"
        << "event rate    " << eventRateHz() << " Hz\n"
        << "screen        " << refresh << " Hz\n"
        << "ink lag       median " << newest.median << " ms, p95 " << newest.p95 << " ms, worst "
        << newest.worst << " ms\n"
        << "queue depth   median " << oldest.median << " ms, p95 " << oldest.p95 << " ms, worst "
        << oldest.worst << " ms\n"
        << "Application share only. The 25 ms criterion is pen tip to photons\n"
        << "and has to be measured with a camera.\n";
    out.flush();
}

void LatencyCanvas::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_C: clear(); break;
        case Qt::Key_M:
            camera_mode_ = !camera_mode_;
            clock_rect_ = QRect();
            update();
            break;
        case Qt::Key_H: show_hud_ = !show_hud_; update(); break;
        case Qt::Key_X: show_crosshair_ = !show_crosshair_; update(); break;
        case Qt::Key_S: printReport(); break;
        case Qt::Key_F:
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
            break;
        case Qt::Key_Q:
        case Qt::Key_Escape: window()->close(); break;
        default: QWidget::keyPressEvent(event); break;
    }
}
