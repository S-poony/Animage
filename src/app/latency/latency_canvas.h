// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QWidget>
#include <deque>

// M0. One window, QTabletEvent, a stroke on screen. The point is not to be a
// drawing program; it is to find out whether pen-to-pixel latency on this
// platform is under 25 ms before fifty thousand lines of code assume the
// current rendering architecture.
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
    Stats latencyStats() const;
    double eventRateHz() const;

    QImage canvas_;
    QElapsedTimer clock_;

    bool drawing_ = false;
    bool erasing_ = false;
    QPointF last_position_;
    qreal last_pressure_ = 0.0;

    QPointF cursor_;
    bool cursor_valid_ = false;

    // Set when a tablet event arrives, cleared when the frame that carries it
    // is painted. Measures the application's own share of the latency only --
    // it cannot see the driver, the compositor or the panel, which is exactly
    // why the real number has to come from a camera.
    qint64 pending_event_ns_ = -1;
    qint64 last_event_ns_ = -1;

    std::deque<double> latencies_ms_;
    std::deque<double> intervals_ms_;

    QString device_name_ = QStringLiteral("none yet");
    QString pointer_kind_ = QStringLiteral("-");
    qreal pressure_ = 0.0;
    qreal tilt_x_ = 0.0;
    qreal tilt_y_ = 0.0;
    qint64 tablet_events_ = 0;
    qint64 mouse_events_ = 0;
    qint64 frames_ = 0;

    bool show_hud_ = true;
    bool show_crosshair_ = true;
};
