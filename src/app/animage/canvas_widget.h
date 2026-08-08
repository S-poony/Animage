// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QWidget>

#include <map>
#include <utility>

class QPainter;
class QTimer;

#include "brush.h"
#include "compositor.h"
#include "ctg.h"
#include "ctg_solver.h"
#include "document.h"

// The drawing surface. Shows the whole scene at one frame -- every track,
// stacked, index 0 on top -- and turns tablet events into brush strokes on the
// one track that is current.
//
// Looking at everything and editing one thing is the whole of the difference a
// second track makes here. What is composited, what a colour is picked from and
// what colour layers are solved all follow the picture; the brush, the onion
// skin and "fit to drawing" all follow the track you are working on, because
// they are about the drawing in your hand and not about the shot.
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
    ~CanvasWidget() override;

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

    // The veil over everything outside the exported rectangle. Worth being able
    // to lift: it darkens the roughs that run off the edge, and judging a
    // drawing whose action carries past the frame means seeing all of it at the
    // strength it was drawn at. The outline stays either way -- the paper does
    // not stop at the canvas, so with neither veil nor outline there would be
    // nothing on screen saying where the picture ends.
    void setPassePartout(bool shown);
    bool passePartout() const { return passe_partout_; }

    void setOnion(const OnionSettings& settings);
    OnionSettings onion() const { return onion_settings_; }

    // Onion skin is suppressed during playback: it triples the compositing
    // cost per frame and nobody reads it at twenty-four frames a second.
    void setPlaying(bool playing);
    bool isStroking() const { return stroking_; }

    double zoom() const { return zoom_; }
    // The image coordinate at the widget's top-left corner. Always on a whole
    // screen pixel -- see onWholeScreenPixels -- which is what lets the cache
    // blit one entry to one pixel instead of being resampled against itself.
    //
    // The alignment is applied here, on the way out, rather than being stored.
    // `pan_` behind it is exact and is never snapped. That distinction is the
    // whole of the fix for the wandering scrubby zoom: rounding to a screen
    // pixel is a property of *showing* the view, and writing it back into the
    // view made every gesture start from the last rounding error and add to it.
    QPointF pan() const;
    void setZoom(double zoom, const QPointF& widget_anchor);
    void resetView();
    void fitToDrawing();
    void fitToCanvas();

    // Everything drawn changed underneath us: undo, layer visibility, opacity.
    void refreshAll();

    // Where the colour of a CTG layer is worked out: a worker thread, so that a
    // max-flow taking a second happens beside the interface rather than inside
    // it. Lives here because the canvas is what knows which drawing is on
    // screen, when a stroke has ended and what has to be repainted -- and it is
    // lent out to anything else that needs one solved.
    animage::CtgSolver& colourSolver() { return ctg_solver_; }

    // Installs anything the solver has finished. Called on a timer while
    // solves are outstanding; public so a test can drive it directly.
    void collectColour();

    // Waits for every outstanding solve and installs it.
    //
    // For tests, and for anything that must not run ahead of the colour. Never
    // during ordinary drawing: waiting for a max-flow on the interface thread
    // is the thing all of this exists to stop.
    bool settleColour(int timeout_ms = 30000);

    // Whether any colour is being worked out. The interface has no business
    // blocking on one, but it is entitled to say so.
    bool colourPending() const { return !ctg_asked_.empty(); }

    // Entries in the composite cache. Exposed so a test can assert this tracks
    // the size of the window rather than the size of the visible image area.
    long long cacheEntryCount() const;

    // The other two numbers ensureCacheCoversView settles on. Exposed for the
    // same reason: how coarsely the cache samples the drawing, and over what
    // area, is what decides both how much of a stroke survives to the screen
    // and how much a pan costs. Neither could be read from outside, so both
    // were argued about instead of measured. See bench_zoom.
    //
    // Image pixels per cache entry, and fractional on purpose -- an integer
    // could not follow a continuous zoom, so there was always a zoom at which
    // it doubled.
    animage::SampleStep cacheStep() const { return cache_step_; }
    animage::PixelRect cachedRegion() const { return cached_region_; }

    // The margin the cache will not go below, in screen pixels. A pan costs a
    // full recomposite the moment it runs past the cached region, so when the
    // margin was allowed to reach zero every mouse move paid for one. Nothing
    // spends it now that the cache no longer grows as the view zooms out, but
    // it is what the invariant is asserted against.
    static constexpr int kMinCacheMargin = 32;

    // Magnification at which the blit stops interpolating and starts showing
    // pixels as squares. Above this an animator is looking *at* the pixels and
    // a guess between them is a lie; below it, nearest-neighbour is just a
    // staircase along every curve.
    //
    // A rule rather than a rendered result on purpose: testing it through the
    // pixels Qt produces would be testing Qt's resampler, not this decision.
    static constexpr double kNearestNeighbourAbove = 3.0;
    static bool blitInterpolatesAt(double blit_scale) {
        return blit_scale < kNearestNeighbourAbove;
    }

Q_SIGNALS:
    void viewChanged();
    void documentChanged();
    void brushSizeChanged(double radius);
    // Linear light, straight rather than premultiplied.
    void colourPicked(float r, float g, float b);

    // A fill landed, so anything reporting on the colour is out of date. Emitted when a solve
    // is installed, which happens on a timer and no longer inside a paint; the
    // queued connection it is on can stay either way, and a fill arriving while
    // the canvas is painting itself would be a way to delete a widget from
    // inside its own paint.
    void colourChanged();

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
    void requestCtgFills();
    void dropStaleColourRequests(bool only_this_frame);
    // Whether any track shows this drawing at the frame the playhead is on.
    bool isShownNow(animage::ImageId image) const;
    void noteColourPending();
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

    // The solves asked for and not yet installed, and what each one was asked
    // about. Without this a paint would ask again for a solve already running,
    // and the rule that the newest question wins would call off the answer it
    // was waiting for -- for ever, at the rate a widget repaints.
    //
    // The generation is the other half of "is this answer still about the
    // question I asked". A fill depends on things it is not keyed on -- which
    // way marks are carried, and a document being replaced by another whose
    // drawings answer to the same ids -- and the way those say so is by
    // emptying the fill cache. See CtgFillCache::generation.
    struct ColourAsked {
        animage::ImageId image = animage::kNoId;
        animage::LayerId layer = animage::kNoId;
        bool tiles = true;  // a picture, rather than a judgement about one

        friend auto operator<=>(const ColourAsked&, const ColourAsked&) = default;
    };
    struct ColourWanted {
        std::uint64_t inputs = 0;
        std::uint64_t generation = 0;
    };
    animage::CtgSolver ctg_solver_;
    std::map<ColourAsked, ColourWanted> ctg_asked_;

    // Runs only while something is outstanding.
    //
    // A poll rather than the solver's own wake-up, deliberately: that callback
    // arrives on a worker thread, and a worker thread that touches a widget --
    // or outlives one by a few microseconds -- is a crash that happens to
    // somebody else on a machine with a different number of cores. Sixteen
    // milliseconds is nothing beside a solve, and the timer stops when there is
    // nothing to wait for.
    QTimer* ctg_poll_ = nullptr;
    bool colour_was_pending_ = false;

    animage::TrackId track_ = animage::kNoId;
    animage::ImageId image_ = animage::kNoId;
    animage::LayerId active_layer_ = animage::kNoId;

    animage::BrushSettings brush_settings_;
    animage::BrushSettings eraser_settings_;
    bool erasing_ = false;
    bool stylus_eraser_ = false;  // the pen was turned over for this stroke

    // The cached composite, in sRGB, covering `cached_region_` in image
    // coordinates. `cached_region_` is snapped to the sampling grid, so the
    // image size is exactly the entries the region spans.
    QImage display_;
    animage::PixelRect cached_region_;
    // Image pixels per cached entry: one per *screen* pixel, so 1 while zoomed
    // in and 1/zoom when zoomed out. That makes the cache the size of the
    // window whatever the zoom, rather than the size of the visible image area.
    animage::SampleStep cache_step_;

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

    // Image coordinate shown at the widget's top-left corner, exact. Read it
    // through pan(), which is where it gets aligned to a screen pixel; nothing
    // should ever store that aligned value back here.
    QPointF pan_;
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
    bool passe_partout_ = true;

    // When the pen was last heard from, used to recognise the mouse events
    // Windows Ink promotes from it. A count of tablet events was used for this
    // and could only ever go up, so the first time the pen came near the tablet
    // the mouse stopped working for the rest of the session.
    QElapsedTimer clock_;
    qint64 last_tablet_ms_ = -1;
};
