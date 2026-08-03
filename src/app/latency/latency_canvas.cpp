// SPDX-License-Identifier: GPL-3.0-or-later
#include "latency_canvas.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QResizeEvent>
#include <QScreen>
#include <QTabletEvent>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kSampleWindow = 240;
constexpr qreal kMinWidth = 1.5;
constexpr qreal kMaxWidth = 26.0;

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
}

void LatencyCanvas::clear() {
    canvas_.fill(Qt::black);
    latencies_ms_.clear();
    intervals_ms_.clear();
    tablet_events_ = 0;
    mouse_events_ = 0;
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

void LatencyCanvas::noteEventArrival() {
    const qint64 now = clock_.nsecsElapsed();
    if (last_event_ns_ >= 0) {
        intervals_ms_.push_back(static_cast<double>(now - last_event_ns_) / 1e6);
        if (intervals_ms_.size() > kSampleWindow) intervals_ms_.pop_front();
    }
    last_event_ns_ = now;

    // Keep the oldest unpainted event: latency is measured from the first event
    // a frame carries, not the last.
    if (pending_event_ns_ < 0) pending_event_ns_ = now;
}

void LatencyCanvas::beginStroke(const QPointF& position, qreal pressure) {
    drawing_ = true;
    last_position_ = position;
    last_pressure_ = pressure;
}

void LatencyCanvas::extendStroke(const QPointF& position, qreal pressure) {
    if (!drawing_ || canvas_.isNull()) return;

    QPainter painter(&canvas_);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal width = kMinWidth + (kMaxWidth - kMinWidth) * std::max(pressure, qreal(0.02));
    QPen pen(erasing_ ? QColor(0, 0, 0) : QColor(255, 255, 255));
    pen.setWidthF(width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawLine(last_position_, position);

    last_position_ = position;
    last_pressure_ = pressure;
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
    cursor_ = event->position();
    cursor_valid_ = true;

    switch (event->type()) {
        case QEvent::TabletPress: beginStroke(event->position(), event->pressure()); break;
        case QEvent::TabletMove:
            if (drawing_) extendStroke(event->position(), event->pressure());
            break;
        case QEvent::TabletRelease: endStroke(); break;
        default: break;
    }

    update();
}

// Mouse handling exists only so that a missing or misconfigured tablet is
// obvious rather than silent: the HUD shows mouse events arriving and no tablet
// events, which is the failure everyone hits first.
void LatencyCanvas::mousePressEvent(QMouseEvent* event) {
    ++mouse_events_;
    noteEventArrival();
    erasing_ = false;
    cursor_ = event->position();
    cursor_valid_ = true;
    beginStroke(event->position(), 0.5);
    update();
}

void LatencyCanvas::mouseMoveEvent(QMouseEvent* event) {
    ++mouse_events_;
    noteEventArrival();
    cursor_ = event->position();
    cursor_valid_ = true;
    if (drawing_) extendStroke(event->position(), 0.5);
    update();
}

void LatencyCanvas::mouseReleaseEvent(QMouseEvent*) {
    endStroke();
    update();
}

LatencyCanvas::Stats LatencyCanvas::latencyStats() const {
    const std::vector<double> samples(latencies_ms_.begin(), latencies_ms_.end());
    if (samples.empty()) return {};
    Stats stats;
    stats.median = percentile(samples, 0.5);
    stats.p95 = percentile(samples, 0.95);
    stats.worst = *std::max_element(samples.begin(), samples.end());
    return stats;
}

double LatencyCanvas::eventRateHz() const {
    if (intervals_ms_.empty()) return 0.0;
    double total = 0.0;
    for (double interval : intervals_ms_) total += interval;
    const double mean = total / static_cast<double>(intervals_ms_.size());
    return (mean > 0.0) ? 1000.0 / mean : 0.0;
}

void LatencyCanvas::paintEvent(QPaintEvent*) {
    if (pending_event_ns_ >= 0) {
        latencies_ms_.push_back(static_cast<double>(clock_.nsecsElapsed() - pending_event_ns_) /
                                1e6);
        if (latencies_ms_.size() > kSampleWindow) latencies_ms_.pop_front();
        pending_event_ns_ = -1;
    }
    ++frames_;

    QPainter painter(this);
    if (!canvas_.isNull()) painter.drawImage(0, 0, canvas_);

    // The gap between the physical pen tip and this crosshair while the pen is
    // moving steadily is the latency, made visible. It is the cheapest estimate
    // available without a camera.
    if (show_crosshair_ && cursor_valid_) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(255, 64, 64));
        pen.setWidthF(1.5);
        painter.setPen(pen);
        painter.drawLine(QPointF(cursor_.x() - 22, cursor_.y()),
                         QPointF(cursor_.x() + 22, cursor_.y()));
        painter.drawLine(QPointF(cursor_.x(), cursor_.y() - 22),
                         QPointF(cursor_.x(), cursor_.y() + 22));
    }

    if (!show_hud_) return;

    const Stats stats = latencyStats();
    const qreal refresh = screen() ? screen()->refreshRate() : 0.0;

    QStringList lines;
    lines << QStringLiteral("device      %1  (%2)").arg(device_name_, pointer_kind_);
    lines << QStringLiteral("tablet ev   %1     mouse ev  %2").arg(tablet_events_).arg(mouse_events_);
    lines << QStringLiteral("pressure    %1").arg(pressure_, 0, 'f', 3);
    lines << QStringLiteral("tilt        %1 / %2").arg(tilt_x_, 0, 'f', 1).arg(tilt_y_, 0, 'f', 1);
    lines << QStringLiteral("event rate  %1 Hz").arg(eventRateHz(), 0, 'f', 0);
    lines << QStringLiteral("screen      %1 Hz     frames %2").arg(refresh, 0, 'f', 0).arg(frames_);
    lines << QString();
    lines << QStringLiteral("event to paint, application only:");
    lines << QStringLiteral("  median %1 ms   p95 %2 ms   worst %3 ms")
                 .arg(stats.median, 0, 'f', 2)
                 .arg(stats.p95, 0, 'f', 2)
                 .arg(stats.worst, 0, 'f', 2);
    lines << QString();
    lines << QStringLiteral("This is not the number that matters. Film the pen tip and");
    lines << QStringLiteral("the screen at 120 fps or more and count frames. Pass: < 25 ms.");
    lines << QString();
    lines << QStringLiteral("C clear    H hud    X crosshair    S report    F fullscreen    Q quit");

    QFont font(QStringLiteral("Consolas"));
    font.setPointSizeF(10.0);
    font.setStyleHint(QFont::Monospace);
    painter.setFont(font);

    const QFontMetricsF metrics(font);
    qreal widest = 0.0;
    for (const QString& line : lines) widest = std::max(widest, metrics.horizontalAdvance(line));
    const qreal line_height = metrics.height();
    const QRectF panel(12, 12, widest + 24, line_height * lines.size() + 20);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 190));
    painter.drawRoundedRect(panel, 6, 6);

    painter.setPen(QColor(220, 220, 220));
    qreal y = panel.top() + 10 + metrics.ascent();
    for (const QString& line : lines) {
        painter.drawText(QPointF(panel.left() + 12, y), line);
        y += line_height;
    }
}

void LatencyCanvas::printReport() const {
    const Stats stats = latencyStats();
    QTextStream out(stdout);
    out << "\n--- M0 latency report ---\n"
        << "device        " << device_name_ << " (" << pointer_kind_ << ")\n"
        << "tablet events " << tablet_events_ << "\n"
        << "mouse events  " << mouse_events_ << "\n"
        << "event rate    " << Qt::fixed << qSetRealNumberPrecision(0) << eventRateHz() << " Hz\n"
        << "event->paint  median " << qSetRealNumberPrecision(2) << stats.median << " ms, p95 "
        << stats.p95 << " ms, worst " << stats.worst << " ms\n"
        << "Application share only. The pass criterion of 25 ms is pen tip to\n"
        << "photons and must be measured with a camera.\n";
    out.flush();
}

void LatencyCanvas::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_C: clear(); break;
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
