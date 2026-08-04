// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QWidget>

class QPainter;

#include "brush.h"
#include "compositor.h"
#include "ctg.h"
#include "document.h"

// The drawing surface. Shows one image of one track, composited, and turns
// tablet events into brush strokes.
//
// Compositing is deferred: edits mark a region dirty and the flattening happens
// once in paintEvent. Doing it eagerly meant a slider that emits fifty changes
// a second asked for fifty full-viewport composites, and the interface stopped
// responding long before the drawing did.
class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    enum class Background { White, Checker };

    // Onion skin counts distinct drawings, not slots: a drawing held for five
    // frames is one neighbour, not five. That falls out of the model rather
    // than being special-cased.
    struct OnionSettings {
        int before = 0;
        int after = 0;
        float opacity = 0.45f;
    };

    explicit CanvasWidget(animage::Document& document, QWidget* parent = nullptr);

    void setTrack(animage::TrackId track);
    void setFrame(std::size_t slot);
    std::size_t frame() const { return slot_; }
    animage::ImageId currentImage() const { return image_; }

    void setActiveLayer(animage::LayerId layer);
    animage::LayerId activeLayer() const { return active_layer_; }

    // Brush and eraser keep their own settings, as separate tools do
    // everywhere else: a size and a pressure response that suit inking do not
    // suit rubbing out.
    animage::BrushSettings& brushSettings() { return erasing_ ? eraser_settings_ : brush_settings_; }
    void setEraser(bool erasing);
    bool isErasing() const { return erasing_; }

    // Always the brush's colour, never the eraser's. brushSettings() hands back
    // whichever tool is selected, so setting a colour through it while the
    // eraser was up wrote it somewhere it means nothing and lost it on the way
    // back to the brush.
    void setBrushColour(float r, float g, float b);

    void setBackground(Background background);
    Background background() const { return background_; }

    void setOnion(const OnionSettings& settings);
    OnionSettings onion() const { return onion_settings_; }

    // Onion skin is suppressed during playback: it triples the compositing
    // cost per frame and nobody reads it at twenty-four frames a second.
    void setPlaying(bool playing);
    bool isStroking() const { return stroking_; }

    double zoom() const { return zoom_; }
    void setZoom(double zoom, const QPointF& widget_anchor);
    void resetView();
    void fitToDrawing();
    void fitToCanvas();

    // Everything drawn changed underneath us: undo, layer visibility, opacity.
    void refreshAll();

    // Entries in the composite cache. Exposed so a test can assert this tracks
    // the size of the window rather than the size of the visible image area.
    long long cacheEntryCount() const;

Q_SIGNALS:
    void viewChanged();
    void documentChanged();
    void brushSizeChanged(double radius);
    // Linear light, straight rather than premultiplied.
    void colourPicked(float r, float g, float b);

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
    void markDirty(const animage::PixelRect& region);
    void repaintImageRect(const animage::PixelRect& region);
    void rebuildOnion();
    void fitTo(const animage::PixelRect& bounds);
    void drawCanvasFrame(QPainter& painter);
    bool refreshCtgFills();
    void setScribblePreview(animage::LayerId layer, bool previewing);

    bool pickColourAt(const QPointF& image_point);

    void beginStroke(const QPointF& image_point, float pressure);
    void extendStroke(const QPointF& image_point, float pressure);
    void endStroke();
    void rebindStrokeToCurrentImage();
    bool eventIsSynthesisedFromPen(QMouseEvent* event) const;

    bool beginNavigation(const QPointF& widget_point, Qt::MouseButton button);
    bool continueNavigation(const QPointF& widget_point);
    void endNavigation();

    animage::Document& doc_;
    animage::Compositor compositor_;
    animage::Brush brush_;
    animage::Framebuffer scratch_;

    animage::TrackId track_ = animage::kNoId;
    animage::ImageId image_ = animage::kNoId;
    animage::LayerId active_layer_ = animage::kNoId;

    animage::BrushSettings brush_settings_;
    animage::BrushSettings eraser_settings_;
    bool erasing_ = false;
    bool stylus_eraser_ = false;  // the pen was turned over for this stroke

    // The cached composite, in sRGB, covering `cached_region_` in image
    // coordinates at one image pixel per entry.
    QImage display_;
    animage::PixelRect cached_region_;
    // Image pixels per cached entry. 1 while zoomed in; larger when zoomed out,
    // so the cache tracks the size of the window rather than the size of the
    // visible image area.
    int cache_step_ = 1;

    // Accumulated between paints. Empty width means nothing is pending.
    animage::PixelRect pending_dirty_;
    bool dirty_everything_ = false;

    // The onion skin flattened once, covering the same region. It only changes
    // when the frame, the view or the settings do, so a stroke does not pay to
    // recomposite the neighbouring drawings on every dab.
    animage::Framebuffer onion_;
    bool onion_dirty_ = false;
    OnionSettings onion_settings_;
    std::size_t slot_ = 0;
    bool playing_ = false;

    QPointF pan_;  // image coordinate shown at the widget's top-left corner
    double zoom_ = 1.0;

    bool stroking_ = false;
    // An eyedropper gesture is in progress. The colour is taken when the button
    // or the pen comes up, so the pointer can be slid onto the right pixel.
    bool picking_ = false;
    bool scribbling_ = false;  // the stroke is on a CTG layer
    animage::LayerId scribble_preview_layer_ = animage::kNoId;
    bool scribble_preview_was_showing_ = false;
    QPointF last_image_point_;
    float last_pressure_ = 1.0f;

    bool panning_ = false;
    bool space_held_ = false;
    QPointF pan_anchor_widget_;
    QPointF pan_anchor_image_;

    bool sizing_ = false;
    QPointF size_anchor_widget_;
    float radius_at_press_ = 1.0f;

    bool zooming_ = false;
    bool zoom_key_held_ = false;
    QPointF zoom_anchor_widget_;
    double zoom_at_press_ = 1.0;

    Background background_ = Background::White;

    // When the pen was last heard from, used to recognise the mouse events
    // Windows Ink promotes from it. A count of tablet events was used for this
    // and could only ever go up, so the first time the pen came near the tablet
    // the mouse stopped working for the rest of the session.
    QElapsedTimer clock_;
    qint64 last_tablet_ms_ = -1;
};
