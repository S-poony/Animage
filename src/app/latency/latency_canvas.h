// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QWidget>
#include <deque>

class QTimer;

// M0. One window, QTabletEvent, a stroke on screen. The point is not to be a
// drawing program; it is to find out whether pen-to-pixel latency on this
// platform is under 25 ms before fifty thousand lines of code assume the
// current rendering architecture.
//
// Everything here is arranged around one rule: do as little as possible per
// pen event. Whole-window repaints and per-frame text layout are the two ways
// an application quietly adds a frame of its own.
class LatencyCanvas : public QWidget {
    Q_OBJECT

public:
    explicit LatencyCanvas(QWidget* parent = nullptr);

    void clear();
    void printReport() const;

protected:
    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Stats {
        double median = 0.0;
        double p95 = 0.0;
        double worst = 0.0;
    };

    void beginStroke(const QPointF& position, qreal pressure);
    void extendStroke(const QPointF& position, qreal pressure);
    void endStroke();
    void noteEventArrival();
    void moveCursorTo(const QPointF& position);
    bool handleAsMouse(QMouseEvent* event);
    void rebuildHud();
    Stats latencyStats(const std::deque<double>& samples) const;
    double eventRateHz() const;
    QRect crosshairRect(const QPointF& centre) const;

    QImage canvas_;
    QElapsedTimer clock_;

    bool drawing_ = false;
    bool erasing_ = false;
    QPointF last_position_;

    QPointF cursor_;
    bool cursor_valid_ = false;

    // Latency from the newest event a frame carries is the lag you can see in
    // the ink. From the oldest, it is how far behind the event queue has
    // fallen. They are different questions and both are worth knowing.
    qint64 pending_oldest_ns_ = -1;
    qint64 pending_newest_ns_ = -1;
    qint64 last_event_ns_ = -1;

    std::deque<double> latency_newest_ms_;
    std::deque<double> latency_oldest_ms_;
    std::deque<double> intervals_ms_;

    QPixmap hud_;
    QRect hud_rect_;
    qint64 hud_built_ns_ = -1;
    QTimer* hud_timer_ = nullptr;

    QString device_name_ = QStringLiteral("none yet");
    QString pointer_kind_ = QStringLiteral("-");
    qreal pressure_ = 0.0;
    qreal tilt_x_ = 0.0;
    qreal tilt_y_ = 0.0;
    qint64 tablet_events_ = 0;
    qint64 mouse_events_ = 0;
    qint64 mouse_from_pen_ = 0;
    qint64 frames_ = 0;

    bool show_hud_ = true;
    bool show_crosshair_ = true;
};
