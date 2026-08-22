// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QWidget>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class QPainter;
class QTimer;

#include "brush.h"
#include "compositor.h"
#include "ctg.h"
#include "ctg_solver.h"
#include "document.h"
#include "selection.h"
#include "transform.h"

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

    // Which tool has the pen. One value and not a flag each, because they
    // compete for it: any two of them being true at once is a state the dispatch
    // below has no answer for, and it used to be reachable -- entering a
    // transform cleared the lasso and left the eraser up, so the size box, the
    // brush ring and Alt+right-drag all went on answering for a tool that was no
    // longer selected. Transform is one of them rather than a flag beside them
    // for the same reason; transform_ is its payload, and the two are kept in
    // step wherever that payload is set or reset.
    enum class Tool { Brush, Eraser, Lasso, Transform };
    void setTool(Tool tool);
    Tool tool() const { return tool_; }

    // Brush and eraser keep their own settings, as separate tools do
    // everywhere else: a size and a pressure response that suit inking do not
    // suit rubbing out.
    animage::BrushSettings& brushSettings() {
        return isErasing() ? eraser_settings_ : brush_settings_;
    }
    bool isErasing() const { return tool_ == Tool::Eraser; }

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

    // Shift was down when the pen landed, so this stroke is a straight line
    // being aimed rather than a mark being made. Nothing is written until the
    // pen lifts; what is on screen meanwhile is the band drawn by
    // drawLinePreview and no pixel of the drawing has moved.
    bool isAimingALine() const { return drawing_line_; }

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

    // How many times this widget has actually painted.
    //
    // The honest way to ask whether a frame was *shown*. Playback sets a slot
    // and the paint happens afterwards, so counting slot changes counts frames
    // asked for -- which is the same number only as long as no two slot changes
    // collapse into one paint, and that is precisely what happens when playback
    // starts overrunning. Counting here cannot be wrong about it.
    //
    // Read by the status bar's playback rate, and by bench_playback, which had
    // to infer the difference from two columns disagreeing before this existed.
    std::uint64_t paintCount() const { return paint_count_; }

    // Why an edit could not happen. Transform, cut, copy, paste and erase all
    // refuse where the brush refuses -- a locked layer, a hidden layer, past the
    // end of a track where there is no slot and no cel -- and it is easy to
    // forget precisely because none of them is the brush.
    //
    // Every one of them reports through `explain()` rather than returning a
    // bare `false`. An operation that quietly does nothing is a bug as far as
    // anybody holding the pen is concerned, and Backspace was exactly that.
    enum class Refusal {
        None,
        NoDrawing,     // past the end of a track: no slot, no cel, nothing to edit
        NoLayer,
        LockedLayer,
        HiddenLayer,
        ColourLayer,   // a mark there is a label; interpolating one invents colours
        NothingDrawn,
        NothingSelected,  // erase takes a loop as its argument and had none
        NothingCopied,
        DifferentLayerKind,
        TooLargeToCommit,  // the scale asks for more tiles than a commit may hold
    };
    static QString explain(Refusal refusal);

    // Why the brush would refuse where the playhead and the active layer stand,
    // for the status bar to say so out loud. `None` when it would draw.
    // The brush itself has nowhere to report -- see beginStroke -- so this is
    // how the answer reaches anybody.
    Refusal whyTheBrushWillNotDraw() const { return refuseToEditHere(); }

    // --- selection -------------------------------------------------------

    // The lasso is a tool because it competes with the brush for the pen. What
    // it produces is not a mode and has no status of its own: a selection here
    // cannot clip the brush -- you can draw anywhere whether or not something is
    // selected -- so it is an argument to transform, copy and erase and nothing
    // else, and an ordinary click clears it.
    bool isLassoing() const { return tool_ == Tool::Lasso; }

    bool hasSelection() const { return !selection_.isEmpty(); }
    const animage::Selection& selection() const { return selection_; }
    void clearSelection();
    // Everything drawn on the active layer, as a loop. Symmetrical with what
    // the Transform tool does with no selection, and the reason it exists at
    // all is that "select all" and "nothing selected" should not be two
    // different pictures of the same thing.
    void selectEverything();

    // Erases what is inside the loop, in one command, and clears it. Backspace.
    // Delete still deletes the drawing: the natural expectation on Delete is
    // "erase what I selected" and the existing binding is "delete this
    // drawing", which is a bad surprise in the dangerous direction -- and making
    // it depend on whether a selection exists is a bad surprise in the other.
    //
    // The fifth operation on the shared refusal list, and the one place that
    // list is checked *without* the layer kind. The kind matters to the four
    // that carry marks about, because scribbles are not lines; erasing takes a
    // mark away and carries nothing, and the eraser already rubs scribbles out
    // on a colour layer -- so refusing here would make Backspace stricter than
    // the tool beside it.
    Refusal eraseSelection();

    // --- clipboard -------------------------------------------------------

    // Internal, and it has to be. The system clipboard cannot carry half-float
    // precision or the CTG label encoding, and an image handed to another
    // program is a different feature with a different argument behind it.
    //
    // Copy takes the selection, or the whole cel if there is none --
    // symmetrical with what the Transform tool does. Cut is the same in one
    // command with the hole written.
    Refusal copySelection();
    Refusal cutSelection();
    bool canPaste() const { return !clipboard_.empty(); }

    // A paste is a float that came from the clipboard instead of from the cel,
    // which is the whole reason this comes third: the lifting, the hole, the box
    // and the commit are all already built. It lands at the coordinates it was
    // copied from -- you paste to re-register something, not to drop it wherever
    // the view happens to be -- and it touches no pixel until it is validated.
    Refusal paste();

    // --- transform -------------------------------------------------------

    // Takes the whole cel of the active layer. Entering the tool is what starts
    // a transform -- there is no "transform selection" button, because the tool
    // is the button, and that single rule is what makes "press it with nothing
    // selected and it boxes the whole drawing" fall out of the design instead of
    // being a special case in it.
    Refusal beginTransform();
    bool transformIsLive() const { return transform_.has_value(); }

    // The five numbers on the bar. Setting them puts the pivot back to the
    // middle of what was picked up, so that a typed rotation always means the
    // same thing whatever the last handle drag did.
    animage::Transform transformValues() const;
    void setTransformValues(const animage::Transform& values);
    void nudgeTransform(int dx, int dy);

    // Where the rotation knob, the eight handles and the middle of the box are,
    // in widget coordinates. For tests that press them rather than recomputing
    // where they ought to be -- a test that worked the position out for itself
    // would agree happily with a knob drawn where nobody can press it.
    QPointF rotationHandleForTesting() const;
    QPointF transformCentreForTesting() const;
    std::array<QPointF, 8> transformHandlesForTesting() const { return transformHandles(); }

    // Bakes it. One resample, one command, and nothing at all if the transform
    // is an identity -- looking at a drawing and putting it back is not an edit.
    //
    // False when it would not fit in `kCommitTileBudget`, in which case nothing
    // is written and the transform is left live to be scaled down or cancelled.
    // Whoever asked decides what to do about that, which is the whole reason
    // this returns something: pressing Enter can leave it up, but the six
    // callers that commit because you are *leaving* cannot.
    bool applyTransform();
    // Commit it, and if it will not commit, put it back where it was.
    //
    // What "you are leaving, so commit" means once a commit can refuse.
    // Changing frame, track or layer, picking another tool, pasting and closing
    // the document all go through here: none of them can carry a live transform
    // out with them, and none of them can be left half done either.
    void settleTransform();
    // Leaves no undo entry, because nothing was ever written.
    void cancelTransform();

    // Whether the live transform could be committed, for the box and the bar to
    // say so before Enter is ever pressed. True when there is no transform.
    bool transformFitsInMemory() const { return transform_fits_; }

    // --- the pointer -----------------------------------------------------

    // What a press would do where the pointer is.
    //
    // One enum and one function that answers it from everything true at once --
    // which tool, which modifier is held, whether a gesture is under way, what
    // is under the pointer -- because the cursor used to be set from nine places
    // and three of them repeated the same held-key chain by hand. State spread
    // across call sites is how a cursor ends up stuck as a closed hand after
    // some path nobody tested; the shortcut table is the same fix for keys.
    //
    // These are decisions and not appearances. Two of them come out as the same
    // shape -- a zoom and a brush resize are both a horizontal drag -- and one
    // of them has no shape in Qt at all, which is what a drawn cursor is for.
    enum class Pointing {
        Draw,
        Erase,     // a rubber, drawn: the tool nothing else announces
        Pick,      // Alt: the eyedropper, drawn
        PanReady,  // Space held, nothing dragged yet
        Panning,
        Zoom,       // Z held, or a scrubby zoom under way
        SizeBrush,  // Alt and the right button, dragged sideways
        Lasso,
        Move,    // inside the transform box
        Rotate,  // the knob, or the band just outside a corner. Drawn.
        // The eight handles, by the direction they stretch the drawing *on
        // screen*, which follows the box round as it turns.
        ScaleHorizontal,
        ScaleVertical,
        ScaleFalling,  // "\", top left to bottom right
        ScaleRising,   // "/", bottom left to top right
        // A press does nothing here. Unreachable during a transform, which
        // claims the whole canvas -- the case it is reachable in is a layer
        // that will take no mark: locked, hidden, or past the end of a track.
        Nothing,
    };

    // The decision the pointer is showing, and what it would be somewhere else.
    //
    // Worth asserting alongside cursor().shape() rather than instead of it: a
    // drawn cursor's shape is Qt::BitmapCursor whatever was drawn on it, so the
    // shape alone cannot tell the rotate cursor from the eyedropper.
    Pointing pointing() const { return pointing_.value_or(Pointing::Draw); }
    Pointing pointingAt(const QPointF& widget_point) const;

    // The circle at the tool's radius, while a drag is setting that radius, at
    // the point the drag started from. Nothing otherwise.
    //
    // Drawn by the canvas because no cursor could be: a brush here goes up to
    // 400 pixels across and a cursor is a 32-pixel bitmap. The price of drawing
    // it is that it arrives a frame after the pointer does, which is why this
    // one is anchored and why the eraser is a cursor instead -- feedback that
    // has to sit under a *moving* pointer cannot be painted by us.
    //
    // It is also the one piece of pointer feedback a screenshot can catch:
    // QWidget::grab() renders the widget and never the pointer.
    struct ToolRing {
        QPointF at;
        double radius = 0.0;  // screen pixels
    };
    std::optional<ToolRing> toolRing() const;

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

    // A transform started, its numbers moved, or it ended -- committed or not.
    // The bar with the numeric fields on it is what listens: it exists only
    // while a transform does, because the scope of a transform belongs to that
    // transform and not to the program.
    void transformBegan();
    void transformNumbersChanged();
    void transformEnded();

    // A commit was asked for and would not fit. The canvas has nowhere to say
    // so itself -- the same reason whyTheBrushWillNotDraw is asked from outside
    // -- and the six callers that commit on the way out are inside this class,
    // so a return value would not reach the status bar from any of them.
    void transformRefused(Refusal why);

    // A loop was made or cleared. Nothing but the status bar listens: a
    // selection has no panel and no state of its own to keep in step.
    void selectionChanged();

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
    // The two ways a gesture stops being ours to finish: the keyboard goes
    // somewhere else in this window, or the window itself stops being active.
    // Both mean the release will be delivered elsewhere or not at all. See
    // abandonGesture for what that costs when nothing catches it.
    void focusOutEvent(QFocusEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    // What a press, a move and a release mean, asked by both devices.
    //
    // Five branches resolving in one order -- eyedropper, transform, lasso,
    // stroke, with navigation already handled by the caller. They were written
    // out twice, once for the pen and once for the mouse, and had begun to
    // drift; the order is load-bearing, so two copies of it is two chances to
    // get it wrong. What stays with each device is only what is genuinely its
    // own: button filtering and pen-synthesis rejection for the mouse, modal and
    // child-widget deferral for the pen, and where the pressure comes from.
    void pressAt(const QPointF& widget_point, Qt::KeyboardModifiers modifiers, float pressure);
    void moveTo(const QPointF& widget_point, float pressure);
    void releaseAt(const QPointF& widget_point);

    // End whatever is under way as if the release had arrived. Idempotent, and
    // safe to call when nothing is happening.
    void abandonGesture();

    QPointF imageFromWidget(const QPointF& widget_point) const;
    QPointF widgetFromImage(const QPointF& image_point) const;
    animage::PixelRect visibleImageRect() const;

    void ensureCacheCoversView();
    void refreshRegion(const animage::PixelRect& region);
    void markDirty(const animage::PixelRect& region);
    void repaintImageRect(const animage::PixelRect& region);
    // One neighbouring drawing, as the onion skin shows it.
    struct Ghost {
        animage::ImageId image = animage::kNoId;
        float weight = 0.0f;
        float r = 0.0f, g = 0.0f, b = 0.0f;

        friend bool operator==(const Ghost&, const Ghost&) = default;
    };
    std::vector<Ghost> collectGhosts() const;

    // Everything the onion buffer was built from.
    //
    // The ghosts are composited through the same layer flags as the drawing in
    // front of them and out of the same cels, so switching a layer off or
    // undoing a stroke made on a neighbouring drawing changes what they should
    // look like -- and nothing was telling them. `refreshAll` marks the display
    // cache dirty, and the onion buffer is not the display cache, so the
    // ghosts went on showing a layer that was no longer there until a frame
    // change happened to rebuild them.
    //
    // Compared rather than signalled, because there are twenty-six calls to
    // `refreshAll` and a rule that every future one has to remember is a rule
    // that will be forgotten. The layer list is held whole and compared with a
    // defaulted `operator==` for the same reason: a field added to `Layer` is
    // then part of the comparison without anybody doing anything.
    struct OnionState {
        std::vector<Ghost> ghosts;
        std::vector<animage::Layer> layers;
        // One per ghost per layer, in that order: what the ghosts are drawn
        // from, so an undo that moves a neighbouring drawing's pixels counts.
        std::vector<std::uint64_t> revisions;

        friend bool operator==(const OnionState&, const OnionState&) = default;
    };
    OnionState onionState() const;

    void rebuildOnion();
    void paintOnion(const animage::PixelRect& region);
    void fitTo(const animage::PixelRect& bounds);
    void drawCanvasFrame(QPainter& painter);
    void requestCtgFills();
    void dropStaleColourRequests(bool only_this_frame);
    // Whether any track shows this drawing at the frame the playhead is on.
    bool isShownNow(animage::ImageId image) const;
    void noteColourPending();
    void setScribblePreview(animage::LayerId layer, bool previewing);

    bool pickColourAt(const QPointF& image_point);

    // `straight` is Shift, read off the press and held for the whole gesture.
    // Deliberately no default: there are two call sites, one per device, and a
    // device that quietly forgot to ask is exactly the bug that would ship.
    void beginStroke(const QPointF& image_point, float pressure, bool straight);
    void extendStroke(const QPointF& image_point, float pressure);
    void endStroke();
    void rebindStrokeToCurrentImage();
    bool eventIsSynthesisedFromPen(QMouseEvent* event) const;

    bool beginNavigation(const QPointF& widget_point, Qt::MouseButton button,
                         Qt::KeyboardModifiers modifiers);
    bool continueNavigation(const QPointF& widget_point);
    void endNavigation();

    // The eight handles, in widget coordinates, clockwise from the top left.
    // Drawn at a fixed screen size, so a box narrower than about three of them
    // has them sitting outside its edge rather than on it -- there is nowhere on
    // it left to put them.
    std::array<QPointF, 8> transformHandles() const;
    void drawTransformPreview(QPainter& painter);
    void buildTransformPicture();
    // The middle of what was picked up, in image coordinates. Two things want
    // it: the pivot between gestures, and the pivot a symmetrical handle drag
    // scales about.
    animage::Vec2 transformBoxCentre() const;
    // Puts the pivot back in the middle of what was picked up, at the end of
    // every gesture. See repivot.
    void centreTransformPivot();

    bool beginTransformDrag(const QPointF& widget_point);
    bool continueTransformDrag(const QPointF& widget_point);
    void endTransformDrag();

    // The pointer moved. Everything else that changes what a press would do
    // calls refreshPointer, which asks the same question from where the pointer
    // was last seen -- a tool being picked, a key going down, a gesture ending.
    void updatePointerAt(const QPointF& widget_point);
    void refreshPointer();
    // Takes the last ring off the screen and puts the new one on, and nothing
    // at all when they are the same. Every mouse move goes through here.
    void updateToolRing();
    void drawToolRing(QPainter& painter);
    QRect toolRingRect() const;

    // The band showing where a straight line will land, on the same take-it-off
    // and put-it-on-again plan as the ring: the far end moves on every pen move,
    // and repainting the whole widget for each one would recomposite the
    // viewport at the rate the hand is travelling.
    void updateLinePreview();
    void drawLinePreview(QPainter& painter);
    QRect linePreviewRect() const;

    // Everything the brush checks before it draws: no drawing under the
    // playhead, no layer, a locked one, a hidden one. `beginStroke` and
    // `eraseSelection` ask this one, because both of them write where the brush
    // writes -- the eraser included, which is what a Backspace through a loop
    // is.
    Refusal refuseToEditHere() const;

    // That list plus the layer kind. Shared by the transform tool and by all
    // three clipboard operations, because "refuse where the brush refuses" is
    // one list and not four -- and the kind is on it because all four carry
    // marks from one place to another, and what a colour layer holds is
    // scribbles rather than lines. Erasing carries nothing anywhere, which is
    // the one place the two lists differ.
    Refusal refuseHere() const;

    // **An empty selection cannot exist**, and this is the one place that rule
    // is applied. A loop catching no ink is thrown away, because "no selection"
    // already means "act on the whole drawing" for the transform tool and for
    // copy -- so a loop over nothing would quietly become a loop over
    // everything, and there would be nothing for erase to take out of it.
    //
    // Asked wherever the loop or what is under it changes: when a loop is
    // finished, and when another layer is chosen with one still up. A frame
    // change drops the selection outright and does not come here. True on
    // arrival at every one of those, which is why nothing downstream has to ask
    // again. Answers whether it dropped one, since only then is there anything
    // to repaint or announce.
    bool dropSelectionIfItCatchesNothing();

    void beginLasso(const QPointF& widget_point);
    void extendLasso(const QPointF& widget_point);
    void endLasso();
    void drawSelection(QPainter& painter) const;
    // The part of the active layer's cel the selection covers, and the rest.
    // With no selection everything is lifted and nothing stays behind, which is
    // what makes "press Transform with nothing selected and it boxes the whole
    // drawing" one code path rather than two.
    animage::Lift liftForTransform() const;

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
    Tool tool_ = Tool::Brush;
    bool stylus_eraser_ = false;  // the pen was turned over for this stroke
    // And is turned over *now*, which is a different question and the one the
    // pointer answers. Read from hover rather than from the press, because the
    // whole of #4 is that the eraser coming up has to be visible before the
    // stroke that would otherwise be how you found out. Any real mouse event
    // clears it: the pen has been put down and its last attitude means nothing.
    bool hover_eraser_ = false;

    // Where the pointer was last seen. A decision made from what is under the
    // pointer needs somewhere to read that from when what changed is the state
    // and not the pointer -- a tool being picked, a key going down.
    QPointF pointer_at_;
    // What the cursor on screen is saying. Empty before the first answer, which
    // is how one function can be both "decide" and "decide for the first time".
    std::optional<Pointing> pointing_;
    // Held from the events themselves rather than read off the keyboard when
    // asked. QGuiApplication::keyboardModifiers() answers for the machine, so a
    // test driving this widget with synthetic events would be answered about
    // whichever keys the person running it happened to be leaning on.
    bool alt_held_ = false;
    // What the last paint put on screen for the ring, so the next one can take
    // it off again. Empty means there is nothing there.
    QRect ring_drawn_;

    // The cached composite, in sRGB, covering `cached_region_` in image
    // coordinates. `cached_region_` is snapped to the sampling grid, so the
    // image size is exactly the entries the region spans.
    QImage display_;
    animage::PixelRect cached_region_;
    // Image pixels per cached entry: one per *screen* pixel, so 1 while zoomed
    // in and 1/zoom when zoomed out. That makes the cache the size of the
    // window whatever the zoom, rather than the size of the visible image area.
    animage::SampleStep cache_step_;

    // Accumulated between paints, and a short list rather than one rectangle.
    //
    // It was one, united as regions arrived. A pan exposes an L of newly
    // visible cache -- a strip down one side and a strip along one edge -- and
    // the union of an L is very nearly the whole viewport, which is precisely
    // the cost the scrolling in ensureCacheCoversView exists to avoid. Past a
    // handful they are merged after all: the point is to keep two or three
    // disjoint strips apart, not to track a hundred separate dabs.
    std::vector<animage::PixelRect> pending_dirty_;
    static constexpr std::size_t kMaxDirtyRegions = 6;

    // Where the onion buffer has to be painted before those regions are
    // converted, which is only ever the strips a scroll exposed: everything
    // else in the buffer is still right about the pixels it holds, and an edit
    // to the drawing does not move a neighbouring one.
    std::vector<animage::PixelRect> onion_pending_;
    bool dirty_everything_ = false;
    std::uint64_t paint_count_ = 0;


    // The onion skin flattened once, covering the same region. It only changes
    // when the frame, the view or the settings do, so a stroke does not pay to
    // recomposite the neighbouring drawings on every dab.
    animage::Framebuffer onion_;
    bool onion_dirty_ = false;
    // What onion_ holds, for the comparison OnionState describes.
    OnionState onion_state_;
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

    // A straight line, while one is being aimed. Shift decides it when the pen
    // lands and the whole gesture keeps that answer -- the constraint cannot be
    // taken up part way through, because by then there are dabs on the drawing
    // and the brush has no way to lift one off again.
    //
    // Both ends are in image coordinates so that zooming mid-gesture moves the
    // band with the drawing rather than with the window, and both pressures are
    // kept because the release carries none: a pen lifting reports zero, and the
    // weight of the far end is what the last move said.
    bool drawing_line_ = false;
    QPointF line_from_;
    QPointF line_to_;
    float line_from_pressure_ = 1.0f;
    float line_to_pressure_ = 1.0f;
    // What the last paint put on screen for the band, so the next one can take
    // it off. Empty means there is nothing there. See ring_drawn_.
    QRect line_drawn_;

    // What the pen is doing to the view rather than to the drawing. One value
    // and not a flag each: beginNavigation sets exactly one of them through a
    // strict chain and endNavigation clears them together, so they were already
    // one value -- the type just did not say so, and "is one of them running"
    // was a three-term disjunction written out at four call sites, two of which
    // were re-deriving by hand what continueNavigation already returns.
    enum class Navigating { None, Panning, Zooming, Sizing };
    Navigating navigating_ = Navigating::None;
    bool isNavigating() const { return navigating_ != Navigating::None; }

    // Each gesture's anchor. Only the live one means anything; the others hold
    // whatever the last gesture of that kind left behind.
    bool space_held_ = false;
    QPointF pan_anchor_widget_;
    QPointF pan_anchor_image_;

    QPointF size_anchor_widget_;
    float radius_at_press_ = 1.0f;

    bool zoom_key_held_ = false;
    QPointF zoom_anchor_widget_;
    double zoom_at_press_ = 1.0;

    // A live transform, and the whole of it. The document is not touched until
    // it is committed: the obvious version writes the hole immediately and puts
    // the pixels back if you cancel, which means Escape has to unwind a command
    // and there is an undo entry for a thing that did not happen.
    struct LiveTransform {
        animage::TrackId track = animage::kNoId;
        animage::ImageId image = animage::kNoId;
        animage::LayerId layer = animage::kNoId;
        // What was picked up, in image pixels. The box refers to this rectangle
        // and so does the pivot.
        animage::PixelRect bounds;
        animage::Transform values;

        // The two halves of the layer: what is moving and what is not. The
        // second stands in for the layer where it was, in the layer's own place
        // in the stack, so the drawing has a hole in it exactly the shape of
        // what was picked up.
        animage::TileGrid lifted;
        animage::TileGrid remaining;

        // A paste writes even when it has not been moved: landing something on
        // the drawing at the coordinates it came from is the operation, not the
        // absence of one. A transform of the drawing's own pixels that has not
        // moved them is the absence of one, and must cost neither a resample nor
        // an undo step.
        bool pasted = false;

        // The float: the layer alone, ready to be blitted through the matrix.
        //
        // The preview and the commit will not agree exactly, and that is
        // deliberate. Re-resampling half-float tiles on every mouse move will
        // not hold a frame, so this is drawn through a QTransform and the real
        // resample is paid once, on commit -- the same class of honesty as the
        // PNG and the EXR not containing the same numbers.
        QImage picture;
        animage::PixelRect covers;
        animage::SampleStep step;
    };
    std::optional<LiveTransform> transform_;

    // Whether what is on screen could be committed, kept beside the numbers
    // rather than asked for when it is wanted.
    //
    // The box is painted from it and so is the bar, and both are asked on every
    // repaint -- while the answer only changes when the five numbers do. So it
    // is worked out where they are set, which is also the only place that can
    // afford to: see commitFitsInBudget on what asking costs and when.
    bool transform_fits_ = true;
    void refreshTransformFit();

    // What a press on the box grabbed.
    enum class Grab { None, Move, Rotate, Handle };

    // And what a press *would* grab, which is the same question asked before the
    // press. One function answers both, because a pointer that promises one
    // thing and a press that does another is worse than no pointer at all.
    struct BoxTarget {
        Grab grab = Grab::None;
        int handle = -1;
    };
    BoxTarget boxTargetAt(const QPointF& widget_point) const;

    Grab grab_ = Grab::None;
    int grabbed_handle_ = -1;
    QPointF grab_image_;
    animage::Transform grab_values_;

    // The loop, in image coordinates, and the gesture that is drawing one.
    //
    // Cleared by changing frame and kept through changing layer: a loop is
    // geometry in image space, so re-lifting it from another layer of the same
    // drawing is meaningful, and carrying it to another drawing is how you
    // transform the wrong thing. It is not in the document and not saved.
    // The clipboard, and what kind of layer it came off. The kind is what a
    // paste is blocked on: a CTG cel pasted onto a raster layer writes negative
    // light as paint, and that has to be refused on the layer kind rather than
    // on a guess about the pixels.
    animage::TileGrid clipboard_;
    animage::LayerKind clipboard_kind_ = animage::LayerKind::Raster;

    animage::Selection selection_;
    bool drawing_lasso_ = false; // the lasso tool is up and the pen is down
    QPointF lasso_press_widget_;
    bool lasso_passed_threshold_ = false;

    Background background_ = Background::White;
    bool passe_partout_ = true;

    // When the pen was last heard from, used to recognise the mouse events
    // Windows Ink promotes from it. A count of tablet events was used for this
    // and could only ever go up, so the first time the pen came near the tablet
    // the mouse stopped working for the rest of the session.
    QElapsedTimer clock_;
    qint64 last_tablet_ms_ = -1;
};
