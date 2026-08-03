// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

#include "brush.h"
#include "compositor.h"
#include "document.h"

// The drawing surface. Shows one image of one timeline, composited, and turns
// tablet events into brush strokes.
//
// The composite is cached for the visible region and only the rectangle a
// stroke actually dirties is recomposited. Flattening the whole viewport on
// every dab would put the layer count and the window size into the latency
// budget, and M0 showed there is no room in it to spare.
class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    enum class Background { White, Checker, Black };

    explicit CanvasWidget(animage::Document& document, QWidget* parent = nullptr);

    void setTarget(animage::TimelineId timeline, animage::ImageId image);
    void setActiveLayer(animage::LayerId layer);
    animage::LayerId activeLayer() const { return active_layer_; }

    animage::BrushSettings& brushSettings() { return brush_.settings(); }
    void setEraser(bool erasing);
    bool isErasing() const { return brush_.settings().erase; }

    void setBackground(Background background);
    Background background() const { return background_; }

    double zoom() const { return zoom_; }
    void setZoom(double zoom, const QPointF& widget_anchor);
    void resetView();
    void fitToDrawing();

    // Everything drawn changed underneath us: undo, layer visibility, opacity.
    void refreshAll();

Q_SIGNALS:
    void viewChanged();
    void documentChanged();

protected:
    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    QPointF imageFromWidget(const QPointF& widget_point) const;
    QPointF widgetFromImage(const QPointF& image_point) const;
    animage::PixelRect visibleImageRect() const;

    void ensureCacheCoversView();
    void refreshRegion(const animage::PixelRect& region);
    void repaintImageRect(const animage::PixelRect& region);

    void beginStroke(const QPointF& image_point, float pressure);
    void extendStroke(const QPointF& image_point, float pressure);
    void endStroke();
    bool eventIsSynthesisedFromPen(QMouseEvent* event) const;

    animage::Document& doc_;
    animage::Compositor compositor_;
    animage::Brush brush_;
    animage::Framebuffer scratch_;

    animage::TimelineId timeline_ = animage::kNoId;
    animage::ImageId image_ = animage::kNoId;
    animage::LayerId active_layer_ = animage::kNoId;

    // The cached composite, in sRGB, covering `cached_region_` in image
    // coordinates at one image pixel per entry.
    QImage display_;
    animage::PixelRect cached_region_;

    QPointF pan_;  // image coordinate shown at the widget's top-left corner
    double zoom_ = 1.0;

    bool stroking_ = false;
    QPointF last_image_point_;
    float last_radius_ = 0.0f;

    bool panning_ = false;
    bool space_held_ = false;
    QPointF pan_anchor_widget_;
    QPointF pan_anchor_image_;

    Background background_ = Background::White;
    qint64 tablet_events_seen_ = 0;
};
