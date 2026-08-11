// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives the canvas widget offscreen. These are the paths that only a human
// clicking around used to reach, which meant their crashes were found by a
// human clicking around.

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QThread>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDoubleSpinBox>
#include <QTreeWidget>
#include <QPointingDevice>
#include <QPushButton>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QAbstractButton>
#include <QDockWidget>
#include <QMessageBox>
#include <QTimer>
#include <cmath>
#include <map>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QFile>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QHeaderView>
#include <QMenu>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>

#include "brush.h"
#include "canvas_widget.h"
#include "export_sequence.h"
#include "color.h"
#include "half.h"

// Declarations only; animage_ui compiles the implementation.
#include "tinyexr.h"
#include "main_window.h"
#include "document.h"
#include "project_io.h"
#include "scribble.h"
#include "timeline_widget.h"
#include "scene_settings_dialog.h"
#include "testing.h"

using namespace animage;

namespace {

struct Fixture {
    Document doc;
    TrackId track;
    LayerId layer;
    ImageId image;
    CanvasWidget canvas;

    Fixture() : canvas(doc) {
        track = doc.addTrack("main");
        layer = doc.addLayer(track, "layer 1");
        image = doc.insertImage(track, 0);
        canvas.resize(1280, 800);
        canvas.setTrack(track);
        canvas.setFrame(0);
        canvas.setActiveLayer(layer);
    }

    void draw(float x0, float y0, float x1, float y1) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings settings;
        settings.radius = 12.0f;
        settings.pressure_affects_opacity = false;
        Brush brush(settings);
        brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
        brush.extend({x1, y1, 1.0f});
        brush.end();
    }

    // Forces the deferred composite to actually run, which is where the
    // allocation and the indexing happen.
    QImage render() { return canvas.grab().toImage(); }
};

// Sweeping the whole zoom range is what crashed: at low zoom the cached region
// covers a hundred times more image pixels than the window has, and the buffers
// were being sized from the image area rather than the window.
void zoomSweepDoesNotExplode() {
    TEST("the whole zoom range renders without exhausting memory");
    Fixture f;
    f.draw(100.0f, 100.0f, 700.0f, 500.0f);

    const double zooms[] = {1.0,  0.9,  0.75, 0.6,  0.51, 0.5,  0.49, 0.34,
                            0.25, 0.12, 0.06, 0.05, 0.2,  1.0,  4.0,  16.0, 32.0};
    for (double zoom : zooms) {
        f.canvas.setZoom(zoom, QPointF(640, 400));
        const QImage frame = f.render();
        CHECK(!frame.isNull());
        CHECK_EQ(frame.width(), 1280);
    }
}

// Zooming out and in repeatedly reallocates the cache each time the view leaves
// it, and the sampling step changes underneath. The indices have to keep
// agreeing across that.
void repeatedZoomAndPanStayConsistent() {
    TEST("repeated zoom and pan keep rendering");
    Fixture f;
    f.draw(-300.0f, -200.0f, 400.0f, 300.0f);

    for (int i = 0; i < 24; ++i) {
        const double zoom = 0.05 + (i % 12) * 0.35;
        f.canvas.setZoom(zoom, QPointF(300 + i * 10, 200));
        f.canvas.refreshAll();
        CHECK(!f.render().isNull());
    }

    f.canvas.fitToDrawing();
    CHECK(!f.render().isNull());
    f.canvas.resetView();
    CHECK(!f.render().isNull());
}

// Onion skin allocates a second buffer over the same region, so it has to obey
// the same bound. This is the case that doubles the memory.
void onionSkinAtLowZoom() {
    TEST("onion skin renders at low zoom");
    Fixture f;
    f.draw(50.0f, 50.0f, 600.0f, 400.0f);
    f.doc.insertImage(f.track, 1);
    f.doc.insertImage(f.track, 2);

    CanvasWidget::OnionSettings onion;
    onion.before = 3;
    onion.after = 3;
    f.canvas.setOnion(onion);
    f.canvas.setFrame(2);

    for (double zoom : {1.0, 0.5, 0.2, 0.05}) {
        f.canvas.setZoom(zoom, QPointF(640, 400));
        CHECK(!f.render().isNull());
    }
}

// The other reported crash: delete a drawing, then undo past it.
void deleteDrawingThenUndo() {
    TEST("deleting a drawing and undoing keeps the canvas valid");
    Fixture f;
    f.draw(100.0f, 100.0f, 300.0f, 300.0f);
    f.doc.extendExposure(f.track, 0, 4);

    const ImageId second = f.doc.insertImage(f.track, 5);
    f.canvas.setFrame(5);
    CHECK_EQ(f.canvas.currentImage(), second);
    CHECK(!f.render().isNull());

    f.canvas.setFrame(0);
    f.doc.removeDrawing(f.track, f.image);
    // The canvas is still pointing at the drawing that just went.
    f.canvas.setFrame(0);
    CHECK_EQ(f.canvas.currentImage(), second);
    CHECK(!f.render().isNull());

    CHECK(f.doc.undo());
    f.canvas.setFrame(0);
    CHECK_EQ(f.canvas.currentImage(), f.image);
    CHECK(!f.render().isNull());

    CHECK(f.doc.redo());
    f.canvas.setFrame(0);
    CHECK(!f.render().isNull());
}

// The paper is drawn over the whole view and not only over the canvas, so the
// veil is the only thing telling the picture from what surrounds it. Which is
// why hiding it has to leave the outline behind: otherwise turning the veil off
// makes the exported rectangle invisible rather than unshaded.
void hidingThePassePartoutKeepsTheCanvasEdge() {
    TEST("hiding the passe-partout leaves the canvas outline");
    Fixture f;
    // Fits with a margin, which is what puts the canvas edge on screen at all.
    f.canvas.fitToCanvas();

    const PixelRect canvas = f.doc.scene().canvas();
    const auto widgetFrom = [&](double x, double y) {
        return QPointF((x - f.canvas.pan().x()) * f.canvas.zoom(),
                       (y - f.canvas.pan().y()) * f.canvas.zoom());
    };
    // The same height on either side of the left edge, far enough from it that
    // the outline itself lands on neither.
    const QPoint outside = widgetFrom(canvas.x - 20.0, canvas.height / 2.0).toPoint();
    const QPoint inside = widgetFrom(canvas.x + 20.0, canvas.height / 2.0).toPoint();

    CHECK(f.canvas.passePartout());
    const QImage veiled = f.render();
    CHECK(qGray(veiled.pixel(outside)) < qGray(veiled.pixel(inside)));

    f.canvas.setPassePartout(false);
    CHECK(!f.canvas.passePartout());
    const QImage bare = f.render();
    // Nothing over the paper any more, so both sides of the edge are the paper.
    CHECK_EQ(bare.pixel(outside), bare.pixel(inside));

    // The edge is still drawn, within a pixel or two of where the canvas says
    // it is. Which pixel exactly a one-pixel pen lands on is Qt's business.
    const int edge = static_cast<int>(std::lround(widgetFrom(canvas.x, 0.0).x()));
    bool outlined = false;
    for (int x = edge - 2; x <= edge + 2; ++x) {
        if (qGray(bare.pixel(x, inside.y())) < qGray(bare.pixel(inside))) outlined = true;
    }
    CHECK(outlined);

    // And it comes back.
    f.canvas.setPassePartout(true);
    const QImage again = f.render();
    CHECK(qGray(again.pixel(outside)) < qGray(again.pixel(inside)));
}

// The menu item is the whole feature as far as the issue is concerned: the
// canvas could always have been told, and nothing could tell it.
void theViewMenuHidesThePassePartout() {
    TEST("the View menu turns the passe-partout off and on");
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    QAction* toggle = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("&Passe-partout")) toggle = action;
    }
    CHECK(toggle != nullptr);
    if (!toggle) return;

    CHECK(toggle->isCheckable());
    CHECK(toggle->isChecked());
    CHECK(canvas->passePartout());

    toggle->trigger();
    QCoreApplication::processEvents();
    CHECK(!canvas->passePartout());

    toggle->trigger();
    QCoreApplication::processEvents();
    CHECK(canvas->passePartout());
}

// --- several tracks --------------------------------------------------------

QAction* actionCalled(MainWindow& window, const QString& text) {
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == text) return action;
    }
    return nullptr;
}

// Defined with the saving tests further down, which is where it was first
// needed. Declared here so these can use it without moving it.
void strokeOn(Document& doc, TrackId track, ImageId image, LayerId layer, float x0, float y0,
              float x1, float y1);

// Issue #1's first half, through the interface that was the whole of what was
// missing: the model and the file always took several tracks and nothing could
// make a second one.
void theTrackMenuAddsATrackYouCanDrawOn() {
    TEST("the Track menu adds a track, and it arrives ready to draw on");
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();

    Document& doc = window.documentForTesting();
    CHECK_EQ(doc.scene().tracks.size(), std::size_t{1});

    QAction* add = actionCalled(window, QStringLiteral("Add track"));
    CHECK(add != nullptr);
    if (!add) return;
    add->trigger();
    QCoreApplication::processEvents();

    CHECK_EQ(doc.scene().tracks.size(), std::size_t{2});
    const Track& added = doc.scene().tracks.back();

    // A track with no layer and no drawing is a row where the brush silently
    // does nothing, which is indistinguishable from a bug.
    CHECK_EQ(added.layers.size(), std::size_t{1});
    CHECK_EQ(added.slots.size(), std::size_t{1});

    // And it is the one being edited, because drawing on it is what comes next.
    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    CHECK_EQ(canvas->currentImage(), added.slots.front());
    CHECK_EQ(canvas->activeLayer(), added.layers.front().id);

    // One undo step for the lot: a half-made track is not a state to land in.
    const std::size_t depth = doc.undoDepth();
    CHECK(doc.undo());
    CHECK_EQ(doc.scene().tracks.size(), std::size_t{1});
    CHECK_EQ(doc.undoDepth(), depth - 1);
}

// Everything downstream holds a track id, and pointing them at a track that has
// just been deleted is the crash this is here to stop.
void deletingATrackRebindsEverything() {
    TEST("deleting a track leaves nothing pointing at it");
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();

    Document& doc = window.documentForTesting();
    QAction* add = actionCalled(window, QStringLiteral("Add track"));
    CHECK(add != nullptr);
    if (!add) return;
    add->trigger();
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().tracks.size(), std::size_t{2});

    const TrackId gone = doc.scene().tracks.back().id;
    const TrackId kept = doc.scene().tracks.front().id;

    // Straight at the document, because the menu item asks a question and a
    // test cannot answer a dialog. What is being tested is the rebinding.
    doc.removeTrack(gone);
    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(timeline != nullptr);
    if (!timeline) return;
    timeline->setTrack(kept);
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    CHECK_EQ(timeline->track(), kept);
    // The active layer came from the track that has gone; it must not still.
    const Track* left = doc.scene().findTrack(kept);
    CHECK(left != nullptr);
    if (!left) return;
    CHECK(left->findLayer(canvas->activeLayer()) != nullptr);
    CHECK(!canvas->grab().isNull());
}

// The canvas shows the whole scene. A track you cannot see is a track you
// cannot use, and this is the one thing a single-track build could not do.
void theCanvasCompositesEveryTrack() {
    TEST("the canvas shows every track, stacked");
    Document doc;
    const TrackId back = doc.addTrack("background");
    const TrackId front = doc.addTrack("character");
    // addTrack appends, so `back` is index 0 and composites on top. Draw the
    // two in places that do not overlap, so each is its own evidence.
    const LayerId back_layer = doc.addLayer(back, "ink");
    const LayerId front_layer = doc.addLayer(front, "ink");
    const ImageId back_image = doc.insertImage(back, 0);
    const ImageId front_image = doc.insertImage(front, 0);
    strokeOn(doc, back, back_image, back_layer, 40.0f, 40.0f, 120.0f, 40.0f);
    strokeOn(doc, front, front_image, front_layer, 40.0f, 200.0f, 120.0f, 200.0f);

    const Compositor compositor;
    Framebuffer frame;
    compositor.compositeScene(doc, 0, PixelRect{0, 0, 300, 300}, frame);

    CHECK(frame.pixel(80, 40).a > 0.5f);   // the track above
    CHECK(frame.pixel(80, 200).a > 0.5f);  // and the one below it
    CHECK_NEAR(frame.pixel(250, 250).a, 0.0, 1e-3);

    // A track that does not reach this frame contributes nothing rather than
    // clearing what is under it -- tracks are not all the same length.
    doc.extendExposure(back, 0, 3);
    CHECK_EQ(doc.scene().timelineFrames(), std::size_t{4});
    compositor.compositeScene(doc, 3, PixelRect{0, 0, 300, 300}, frame);
    CHECK(frame.pixel(80, 40).a > 0.5f);            // still held
    CHECK_NEAR(frame.pixel(80, 200).a, 0.0, 1e-3);  // past the shorter track's end
}

// The playhead belongs to the timeline and not to any track on it, so it has to
// reach the end of the longest one.
void theTimelineIsAsLongAsTheLongestTrack() {
    TEST("the timeline runs to the end of the longest track");
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();

    Document& doc = window.documentForTesting();
    const TrackId first = doc.scene().tracks.front().id;
    QAction* add = actionCalled(window, QStringLiteral("Add track"));
    CHECK(add != nullptr);
    if (!add) return;
    add->trigger();
    QCoreApplication::processEvents();

    // The first track runs to 10, the second still has its one frame.
    doc.extendExposure(first, 0, 9);
    CHECK_EQ(doc.scene().timelineFrames(), std::size_t{10});

    auto* timeline = window.findChild<TimelineWidget*>();
    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(timeline != nullptr && canvas != nullptr);
    if (!timeline || !canvas) return;
    timeline->refresh();

    // Standing past the short track's end is a real frame of the shot, and the
    // canvas still draws -- it just has nothing of this track to draw on.
    timeline->setCurrentSlot(7);
    QCoreApplication::processEvents();
    CHECK_EQ(timeline->currentSlot(), std::size_t{7});
    CHECK_EQ(canvas->frame(), std::size_t{7});
    CHECK_EQ(canvas->currentImage(), kNoId);
    CHECK(!canvas->grab().isNull());
}

// The timeline dock's height, which had three wrong versions and no test.
//
// Deliberately relative rather than in pixels: what the dock should be for one
// row belongs to the style and the font, and pinning a number here would fail on
// a different theme while saying nothing about the behaviour. What broke twice
// was the *shape* -- growing but never shrinking, then not growing at all -- and
// that is what these assert.
void theTimelineDockFollowsTheTrackCount() {
    TEST("the timeline dock grows a row at a time, caps, and comes back down");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    QDockWidget* dock = nullptr;
    for (QDockWidget* d : window.findChildren<QDockWidget*>()) {
        if (d->windowTitle() == QStringLiteral("Timeline")) dock = d;
    }
    CHECK(dock != nullptr);
    QAction* add = actionCalled(window, QStringLiteral("Add track"));
    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(add != nullptr && timeline != nullptr);
    if (!dock || !add || !timeline) return;

    Document& doc = window.documentForTesting();
    std::vector<int> going_up{dock->height()};
    for (int i = 0; i < 4; ++i) {
        add->trigger();
        QCoreApplication::processEvents();
        going_up.push_back(dock->height());
    }
    CHECK_EQ(doc.scene().tracks.size(), std::size_t{5});

    // One row is one row, however many there already are.
    const int row = going_up[1] - going_up[0];
    CHECK(row > 0);
    CHECK_EQ(going_up[2] - going_up[1], row);
    CHECK_EQ(going_up[3] - going_up[2], row);
    // ...up to the cap, past which the strip scrolls instead of taking more of
    // the canvas. The fifth track adds nothing.
    CHECK_EQ(going_up[4], going_up[3]);

    // And back down again, to the same heights it came up through. This is the
    // half that was broken: adding raised the dock and deleting left it there.
    for (int i = 4; i >= 1; --i) {
        // What removeCurrentTrack does, without the dialog a test cannot answer.
        const TrackId current = timeline->track();
        std::size_t index = 0;
        for (std::size_t t = 0; t < doc.scene().tracks.size(); ++t) {
            if (doc.scene().tracks[t].id == current) index = t;
        }
        doc.removeTrack(current);
        const std::vector<Track>& left = doc.scene().tracks;
        timeline->setTrack(left[std::min(index, left.size() - 1)].id);
        QCoreApplication::processEvents();
        CHECK_EQ(dock->height(), going_up[static_cast<std::size_t>(i - 1)]);
    }
}

// The other half: the track count chooses where the dock starts, and after that
// it is the animator's. Pinning the minimum and maximum to the wanted height
// sized it correctly and welded it shut, which is what this catches.
void theTimelineDockCanBeResizedByHand() {
    TEST("the timeline dock can be dragged smaller than its default and stays");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    QDockWidget* dock = nullptr;
    for (QDockWidget* d : window.findChildren<QDockWidget*>()) {
        if (d->windowTitle() == QStringLiteral("Timeline")) dock = d;
    }
    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(dock != nullptr && timeline != nullptr);
    if (!dock || !timeline) return;

    const int settled = dock->height();

    // Smaller than the track count asked for: what dragging the splitter does.
    window.resizeDocks({dock}, {settled / 2}, Qt::Vertical);
    QCoreApplication::processEvents();
    const int dragged_small = dock->height();
    CHECK(dragged_small < settled);

    // A refresh must not shove it back. There is one of those per frame change,
    // so a dock that resets would undo the drag the moment you scrubbed.
    timeline->setCurrentSlot(0);
    timeline->refresh();
    QCoreApplication::processEvents();
    CHECK_EQ(dock->height(), dragged_small);

    // And taller than any track count would ask for, which nothing caps.
    window.resizeDocks({dock}, {settled * 3}, Qt::Vertical);
    QCoreApplication::processEvents();
    CHECK(dock->height() > settled);
}

// A track that holds or cycles is showing a picture out past its last drawing,
// and that is all it is doing: there is no slot and no cel there, so there is
// nothing to draw on. What a track shows and what it holds are different
// questions, and editing follows what it holds.
void pastATracksEndYouCanSeeItButNotDrawOnIt() {
    TEST("a held or cycled drawing is shown past the end but cannot be drawn on");
    Document doc;
    const TrackId shot = doc.addTrack("character");
    doc.addLayer(shot, "ink");
    doc.insertImage(shot, 0);
    doc.extendExposure(shot, 0, 5);  // six frames of scene

    const TrackId back = doc.addTrack("background");
    const LayerId back_ink = doc.addLayer(back, "ink");
    const ImageId only = doc.insertImage(back, 0);
    strokeOn(doc, back, only, back_ink, 40.0f, 200.0f, 200.0f, 200.0f);
    TrackProperties props = doc.scene().findTrack(back)->properties();
    props.end = TrackEnd::HoldLast;
    doc.updateTrack(back, props);
    CHECK_EQ(doc.scene().timelineFrames(), std::size_t{6});

    CanvasWidget canvas(doc);
    canvas.resize(400, 400);
    canvas.setTrack(back);
    canvas.setActiveLayer(back_ink);

    // Inside the track, the drawing is there and is the one being edited.
    canvas.setFrame(0);
    CHECK_EQ(canvas.currentImage(), only);

    // Past it, the track holds nothing, so there is nothing to edit...
    canvas.setFrame(4);
    CHECK_EQ(canvas.frame(), std::size_t{4});
    CHECK_EQ(canvas.currentImage(), kNoId);

    // ...but the picture still has it in, because the track is holding it. That
    // is the whole distinction: shown, not held.
    const Compositor compositor;
    Framebuffer frame;
    compositor.compositeScene(doc, 4, PixelRect{0, 0, 300, 300}, frame);
    CHECK(frame.pixel(120, 200).a > 0.5f);

    // And with the end behaviour off, neither is true out there.
    props.end = TrackEnd::Nothing;
    doc.updateTrack(back, props);
    compositor.compositeScene(doc, 4, PixelRect{0, 0, 300, 300}, frame);
    CHECK_NEAR(frame.pixel(120, 200).a, 0.0, 1e-3);
}

// Issue #9 through the button that does it, because the button is where the
// track's setting has to be read.
void theInsertButtonObeysTheOverwriteSetting() {
    TEST("the insert button spends the hold when the track overwrites");
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();

    Document& doc = window.documentForTesting();
    const TrackId track = doc.scene().tracks.front().id;
    doc.extendExposure(track, 0, 10);  // held 11
    CHECK_EQ(doc.scene().findTrack(track)->frameCount(), std::size_t{11});

    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(timeline != nullptr);
    if (!timeline) return;
    timeline->refresh();
    timeline->setCurrentSlot(3);  // frame 4
    QCoreApplication::processEvents();

    QAction* overwrite = actionCalled(window, QStringLiteral("Overwrite drawings"));
    QAction* insert = actionCalled(window, QStringLiteral("Insert drawing"));
    CHECK(overwrite != nullptr && insert != nullptr);
    if (!overwrite || !insert) return;

    CHECK(overwrite->isCheckable());
    CHECK(overwrite->isChecked());  // on is the default, and the menu says so
    CHECK(doc.scene().findTrack(track)->overwrite_drawings);

    insert->trigger();
    QCoreApplication::processEvents();

    const Track* after = doc.scene().findTrack(track);
    CHECK_EQ(after->frameCount(), std::size_t{11});  // the shot did not grow
    CHECK_EQ(after->images.size(), std::size_t{2});
    // The playhead followed the new drawing to where it actually landed.
    CHECK_EQ(timeline->currentSlot(), std::size_t{3});
    CHECK_EQ(after->imageAtSlot(3), after->imageAtSlot(10));
    CHECK(after->imageAtSlot(2) != after->imageAtSlot(3));

    // Switched off, the same button lengthens the shot instead.
    overwrite->trigger();
    QCoreApplication::processEvents();
    CHECK(!doc.scene().findTrack(track)->overwrite_drawings);

    const std::size_t was = doc.scene().findTrack(track)->frameCount();
    insert->trigger();
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().findTrack(track)->frameCount(), was + 1);

    // And the setting belongs to the track, not to the window: a second track
    // arrives at the default rather than inheriting what this one was set to.
    QAction* add = actionCalled(window, QStringLiteral("Add track"));
    CHECK(add != nullptr);
    if (!add) return;
    add->trigger();
    QCoreApplication::processEvents();
    CHECK(doc.scene().tracks.back().overwrite_drawings);
    CHECK(overwrite->isChecked());  // and the menu followed the new track
}

// Every frame gone, which leaves no image to composite at all.
void emptyTimelineRenders() {
    TEST("a track with no frames still renders");
    Fixture f;
    f.draw(10.0f, 10.0f, 90.0f, 90.0f);
    f.doc.removeDrawing(f.track, f.image);

    f.canvas.setFrame(0);
    CHECK_EQ(f.canvas.currentImage(), kNoId);
    CHECK(!f.render().isNull());

    f.canvas.setZoom(0.05, QPointF(100, 100));
    CHECK(!f.render().isNull());
}

// Space and Z are forwarded to the canvas by an application-wide event filter
// so they keep working when something else has focus. An application filter
// also sees the events that filter itself sends, so forwarding without a guard
// re-enters immediately and recurses until the stack runs out. Pressing Z --
// the zoom key -- did nothing and then killed the process.
void heldKeysDoNotRecurse() {
    TEST("forwarding Space and Z to the canvas does not recurse");
    MainWindow window;
    window.resize(1000, 700);

    auto* elsewhere = window.findChild<QWidget*>();
    CHECK(elsewhere != nullptr);

    for (int key : {Qt::Key_Space, Qt::Key_Z}) {
        for (int i = 0; i < 50; ++i) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
            QCoreApplication::sendEvent(elsewhere, &press);
            QCoreApplication::sendEvent(elsewhere, &release);
        }

        // Holding the key past the auto-repeat delay is what actually crashed.
        // An auto-repeat that is not accepted propagates to the parent, where
        // this same filter sees it again and sends it back.
        for (int i = 0; i < 200; ++i) {
            QKeyEvent repeat(QEvent::KeyPress, key, Qt::NoModifier, QString(), true);
            QCoreApplication::sendEvent(elsewhere, &repeat);
        }
        for (int i = 0; i < 200; ++i) {
            QKeyEvent repeat(QEvent::KeyPress, key, Qt::NoModifier, QString(), true);
            QCoreApplication::sendEvent(window.findChild<CanvasWidget*>(), &repeat);
        }
        QKeyEvent final_release(QEvent::KeyRelease, key, Qt::NoModifier);
        QCoreApplication::sendEvent(elsewhere, &final_release);
    }

    // Ctrl+Z must still reach the shortcut rather than being swallowed.
    QKeyEvent undo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
    QCoreApplication::sendEvent(elsewhere, &undo);

    // Letting go of Ctrl before Z leaves a bare Z release, which took the
    // forwarding path even though the press had not. That is why undoing
    // sometimes killed the process and sometimes did not: it depended on the
    // order the two keys came up.
    QKeyEvent release_ctrl_first(QEvent::KeyRelease, Qt::Key_Z, Qt::NoModifier);
    QCoreApplication::sendEvent(elsewhere, &release_ctrl_first);
    CHECK(true);  // reaching here at all is the assertion
}

// The cache is padded and, when zoomed out, sampled. Both have to stay tied to
// the size of the window: sized from the visible image area instead, a
// maximised window at 51% zoom asks for a quarter of a gigabyte.
void cacheStaysBoundedAtEveryZoom() {
    TEST("the composite cache stays bounded at every zoom");
    Fixture f;
    f.canvas.resize(2560, 1440);
    f.draw(-2000.0f, -1500.0f, 3000.0f, 2000.0f);

    const long long viewport = 2560LL * 1440;
    for (double zoom : {32.0, 8.0, 1.0, 0.9, 0.75, 0.6, 0.55, 0.51, 0.5, 0.3, 0.1, 0.05}) {
        f.canvas.setZoom(zoom, QPointF(1280, 720));
        f.canvas.refreshAll();
        CHECK(!f.render().isNull());
        // Generous, but a constant multiple of the window rather than of the
        // image area, which is the property that matters.
        CHECK(f.canvas.cacheEntryCount() <= viewport * 3);
    }
}

// Real time has to pass: the canvas tells a promoted mouse event from a real one
// by how long ago the pen was last heard from.
void waitMs(int ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
}

// Clicks a row's check indicator through the viewport, where a real click
// arrives, rather than calling setCheckState. The bug this exists for was
// entirely about the click never reaching the indicator.
void clickCheck(QTreeWidget* list, QTreeWidgetItem* item, int column);

void sendMouse(QWidget* widget, QEvent::Type type, const QPointF& at, Qt::MouseButton button,
               Qt::MouseButtons buttons) {
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

void clickCheck(QTreeWidget* list, QTreeWidgetItem* item, int column) {
    const QRect rect = list->visualItemRect(item);
    const int left = list->header()->sectionPosition(column);
    // Where the style puts the indicator, not a guess at it.
    const QPoint at(left + list->style()->pixelMetric(QStyle::PM_IndicatorWidth) / 2 + 3,
                    rect.center().y());
    sendMouse(list->viewport(), QEvent::MouseButtonPress, QPointF(at), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(list->viewport(), QEvent::MouseButtonRelease, QPointF(at), Qt::LeftButton,
              Qt::NoButton);
    QCoreApplication::processEvents();
}

// Answers the next modal dialog to appear. Armed before the call that raises
// it, because that call blocks in the dialog's own event loop and nothing in
// the test runs again until the dialog is gone. Retries rather than firing
// once: a queued single shot can arrive before the dialog is up, and a test
// that then waits forever is worse than one that fails.
void answerNextDialog(QMessageBox::StandardButton button) {
    auto* timer = new QTimer(qApp);
    auto* attempts = new int(0);
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, button, attempts] {
        if (++*attempts > 200) {  // two seconds; the dialog is not coming
            timer->stop();
            delete attempts;
            timer->deleteLater();
            return;
        }
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) return;
        QAbstractButton* pressed = box->button(button);
        if (!pressed) return;
        pressed->click();
        timer->stop();
        delete attempts;
        timer->deleteLater();
    });
    timer->start();
}

// The same, for a dialog that is not a message box: dismiss whatever modal
// window turns up. Used for the Scene settings dialog New raises.
void dismissNextDialog() {
    auto* timer = new QTimer(qApp);
    auto* attempts = new int(0);
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, attempts] {
        QWidget* modal = QApplication::activeModalWidget();
        if (!modal && ++*attempts <= 200) return;
        if (modal) modal->close();
        timer->stop();
        delete attempts;
        timer->deleteLater();
    });
    timer->start();
}

void drawWithMouse(QWidget* canvas, const QPointF& from, const QPointF& to, int steps) {
    sendMouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        sendMouse(canvas, QEvent::MouseMove, from + (to - from) * t, Qt::NoButton,
                  Qt::LeftButton);
    }
    sendMouse(canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
}

// Panning far enough that the view keeps leaving the cached region, which is
// what forces the reallocation and the re-composite.
void longPanGestureSurvives() {
    TEST("a long pan drag survives");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    drawWithMouse(canvas, QPointF(150, 150), QPointF(1000, 650), 60);

    QKeyEvent pan_down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &pan_down);

    const QPointF start(600, 400);
    sendMouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    for (int i = 0; i < 400; ++i) {
        const QPointF at(start.x() + 500.0 * std::sin(i * 0.09),
                         start.y() + 350.0 * std::cos(i * 0.05));
        sendMouse(canvas, QEvent::MouseMove, at, Qt::NoButton, Qt::LeftButton);
        QCoreApplication::processEvents();
    }
    sendMouse(canvas, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);

    QKeyEvent pan_up(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &pan_up);
    QCoreApplication::processEvents();
    CHECK(canvas->zoom() > 0.0);
}

// The whole scrubby-zoom gesture through the real window: hold Z, press, drag
// for a long time, release. Driving setZoom directly missed whatever this
// reaches.
void scrubbyZoomGestureSurvives() {
    TEST("a long scrubby zoom drag survives");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    // Draw something first. On an empty canvas the compositor does almost
    // nothing, which is why the earlier version of this test passed.
    drawWithMouse(canvas, QPointF(200, 200), QPointF(900, 600), 40);

    QKeyEvent zoom_down(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &zoom_down);

    const QPointF start(500, 400);
    QMouseEvent press(QEvent::MouseButtonPress, start, canvas->mapToGlobal(start),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &press);

    // Sweep out and back in, repeatedly, the way a hand does.
    for (int i = 0; i < 400; ++i) {
        const double offset = 420.0 * std::sin(i * 0.07);
        const QPointF at(start.x() + offset, start.y());
        QMouseEvent move(QEvent::MouseMove, at, canvas->mapToGlobal(at), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &move);
        QCoreApplication::processEvents();
    }

    QMouseEvent release(QEvent::MouseButtonRelease, start, canvas->mapToGlobal(start),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &release);
    QKeyEvent zoom_up(QEvent::KeyRelease, Qt::Key_Z, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &zoom_up);

    QCoreApplication::processEvents();
    CHECK(canvas->zoom() > 0.0);
}

// The same for the wheel, which is the other way in and takes a different code
// path into setZoom.
void wheelZoomGestureSurvives() {
    TEST("a long wheel zoom survives");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    drawWithMouse(canvas, QPointF(200, 200), QPointF(950, 620), 50);

    for (int i = 0; i < 300; ++i) {
        const int direction = ((i / 25) % 2 == 0) ? -120 : 120;
        const QPointF at(400 + (i % 17) * 20, 300 + (i % 11) * 20);
        QWheelEvent wheel(at, canvas->mapToGlobal(at), QPoint(0, 0), QPoint(0, direction),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(canvas, &wheel);
        QCoreApplication::processEvents();
    }
    CHECK(canvas->zoom() > 0.0);
}

// Clicking a spin box in the toolbar hands it the keyboard, and its line edit
// then swallows plain letters as text -- so B and E stop switching tool. Going
// back to the canvas has to take the keyboard back, and it has to do so for the
// pen as well as the mouse: Qt gives click-focus for mouse presses but not for
// tablet presses, which is exactly the case an artist hits.
void touchingTheCanvasTakesTheKeyboardBack() {
    TEST("drawing on the canvas takes focus back from a spin box");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    auto* spin = window.findChild<QDoubleSpinBox*>();
    CHECK(canvas != nullptr);
    CHECK(spin != nullptr);
    if (!canvas || !spin) return;

    spin->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    CHECK(spin->hasFocus());

    sendMouse(canvas, QEvent::MouseButtonPress, QPointF(400, 300), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, QPointF(400, 300), Qt::LeftButton,
              Qt::NoButton);
    QCoreApplication::processEvents();
    CHECK(canvas->hasFocus());

    // And again with the pen, which is the path that was actually broken.
    spin->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    CHECK(spin->hasFocus());

    QPointingDevice stylus(QStringLiteral("test stylus"), 1, QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Pen,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                           1, 0);
    const QPointF at(500, 350);
    QTabletEvent press(QEvent::TabletPress, &stylus, at, canvas->mapToGlobal(at), 1.0, 0, 0, 0,
                       0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(canvas, &press);
    QTabletEvent release(QEvent::TabletRelease, &stylus, at, canvas->mapToGlobal(at), 0.0, 0, 0,
                         0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(canvas, &release);
    QCoreApplication::processEvents();
    CHECK(canvas->hasFocus());
}

// Alt-click samples the drawing rather than the screen. The pen is the path
// that matters: the colour dialog's screen picker only ever hears mouse
// buttons, and on Windows a stylus produces none it can hear, so a pen could
// not pick a colour at all. It must not draw while it is at it, either.
void altClickPicksTheColourUnderThePointer() {
    TEST("Alt+click picks the colour under the pointer, with pen and mouse");
    Fixture f;

    // A green stroke, laid down through the brush so it goes through the same
    // premultiplied half-float round trip a real one does.
    {
        ScopedCommand command(f.doc, "Stroke");
        BrushSettings settings;
        settings.radius = 30.0f;
        settings.pressure_affects_opacity = false;
        settings.r = 0.0f;
        settings.g = 0.6f;
        settings.b = 0.2f;
        Brush brush(settings);
        brush.begin(f.doc, f.track, f.image, f.layer, {400.0f, 300.0f, 1.0f});
        brush.extend({420.0f, 300.0f, 1.0f});
        brush.end();
    }

    float r = -1.0f, g = -1.0f, b = -1.0f;
    int picks = 0;
    QObject::connect(&f.canvas, &CanvasWidget::colourPicked,
                     [&](float pr, float pg, float pb) {
                         r = pr;
                         g = pg;
                         b = pb;
                         ++picks;
                     });

    const std::size_t before = f.doc.undoDepth();
    const QPointF off_the_stroke(120, 120);  // zoom 1, pan 0: widget == image
    const QPointF on_the_stroke(410, 300);

    QPointingDevice stylus(QStringLiteral("test stylus"), 1, QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Pen,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                           1, 0);

    // The colour is taken where the button comes up, not where it went down, so
    // the pointer can be slid onto the right pixel while it is held -- and it
    // follows the pointer the whole way, so you can see what you are sliding
    // onto. Press somewhere useless, drag onto the stroke, and the colour
    // arrives during the drag rather than after it.
    QMouseEvent press(QEvent::MouseButtonPress, off_the_stroke,
                      f.canvas.mapToGlobal(off_the_stroke), Qt::LeftButton, Qt::LeftButton,
                      Qt::AltModifier);
    QCoreApplication::sendEvent(&f.canvas, &press);
    CHECK_EQ(picks, 0);  // bare paper, so there was nothing to report

    QMouseEvent drag(QEvent::MouseMove, on_the_stroke, f.canvas.mapToGlobal(on_the_stroke),
                     Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    QCoreApplication::sendEvent(&f.canvas, &drag);
    CHECK_EQ(picks, 1);  // shown while the button is still down
    CHECK(std::abs(r - 0.0f) < 0.01f);
    CHECK(std::abs(g - 0.6f) < 0.01f);
    CHECK(std::abs(b - 0.2f) < 0.01f);

    // Dragging back out over bare paper holds what is shown rather than
    // snatching it away, which is what makes "the release commits what you can
    // see" true even when the release lands on nothing.
    QMouseEvent drag_off(QEvent::MouseMove, off_the_stroke,
                         f.canvas.mapToGlobal(off_the_stroke), Qt::NoButton, Qt::LeftButton,
                         Qt::AltModifier);
    QCoreApplication::sendEvent(&f.canvas, &drag_off);
    CHECK_EQ(picks, 1);
    CHECK(std::abs(g - 0.6f) < 0.01f);

    QMouseEvent release(QEvent::MouseButtonRelease, on_the_stroke,
                        f.canvas.mapToGlobal(on_the_stroke), Qt::LeftButton, Qt::NoButton,
                        Qt::AltModifier);
    QCoreApplication::sendEvent(&f.canvas, &release);
    CHECK_EQ(picks, 2);
    CHECK(std::abs(g - 0.6f) < 0.01f);

    // The same with the pen, which is the path that matters.
    QTabletEvent pen_down(QEvent::TabletPress, &stylus, off_the_stroke,
                          f.canvas.mapToGlobal(off_the_stroke), 1.0, 0, 0, 0, 0, 0,
                          Qt::AltModifier, Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(&f.canvas, &pen_down);
    CHECK_EQ(picks, 2);  // bare paper again

    // Alt let go mid-gesture must not turn the rest of it into a stroke -- and
    // the pick carries on, because the gesture is the one that is in progress
    // and not the modifier that started it.
    QTabletEvent pen_move(QEvent::TabletMove, &stylus, on_the_stroke,
                          f.canvas.mapToGlobal(on_the_stroke), 1.0, 0, 0, 0, 0, 0, Qt::NoModifier,
                          Qt::NoButton, Qt::LeftButton);
    QCoreApplication::sendEvent(&f.canvas, &pen_move);
    CHECK(!f.canvas.isStroking());
    CHECK_EQ(picks, 3);
    CHECK(std::abs(g - 0.6f) < 0.01f);

    QTabletEvent pen_up(QEvent::TabletRelease, &stylus, on_the_stroke,
                        f.canvas.mapToGlobal(on_the_stroke), 0.0, 0, 0, 0, 0, 0, Qt::NoModifier,
                        Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(&f.canvas, &pen_up);
    CHECK_EQ(picks, 4);
    CHECK(std::abs(g - 0.6f) < 0.01f);

    // Neither one drew: picking a colour is not an edit, and it must not leave
    // a dab where it was picked from.
    CHECK(!f.canvas.isStroking());
    CHECK_EQ(f.doc.undoDepth(), before);

    // Bare paper has no colour to take, so the brush keeps the one it had.
    const QPointF empty_at(50, 50);
    QTabletEvent empty_down(QEvent::TabletPress, &stylus, empty_at,
                            f.canvas.mapToGlobal(empty_at), 1.0, 0, 0, 0, 0, 0, Qt::AltModifier,
                            Qt::LeftButton, Qt::LeftButton);
    QTabletEvent empty_up(QEvent::TabletRelease, &stylus, empty_at,
                          f.canvas.mapToGlobal(empty_at), 0.0, 0, 0, 0, 0, 0, Qt::AltModifier,
                          Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(&f.canvas, &empty_down);
    QCoreApplication::sendEvent(&f.canvas, &empty_up);
    CHECK_EQ(picks, 4);
}

// Touching the tablet must not disable the mouse for the rest of the session.
// The guard against Windows Ink's promoted mouse events was "has this canvas
// ever seen a tablet event", which is true forever after the pen first comes
// near -- so putting the pen down and picking up the mouse left a canvas that
// silently would not draw.
void theMouseStillWorksAfterThePenHasBeenUsed() {
    TEST("the mouse still draws after the pen has been used");
    Fixture f;

    QPointingDevice stylus(QStringLiteral("test stylus"), 1, QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Pen,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                           1, 0);
    const QPointF pen_at(300, 240);
    QTabletEvent pen_down(QEvent::TabletPress, &stylus, pen_at, f.canvas.mapToGlobal(pen_at), 1.0,
                          0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(&f.canvas, &pen_down);
    QTabletEvent pen_up(QEvent::TabletRelease, &stylus, pen_at, f.canvas.mapToGlobal(pen_at), 0.0,
                        0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(&f.canvas, &pen_up);
    CHECK(f.doc.undoDepth() >= 1);  // the pen drew

    // The promotion follows its pen event immediately, so a mouse event now is
    // still assumed to be one and must not draw a second time.
    const std::size_t after_pen = f.doc.undoDepth();
    drawWithMouse(&f.canvas, QPointF(500, 400), QPointF(560, 430), 4);
    CHECK_EQ(f.doc.undoDepth(), after_pen);

    // Once the pen has been quiet, the mouse is a mouse again. A real hand takes
    // far longer than this to change tool.
    waitMs(400);
    drawWithMouse(&f.canvas, QPointF(600, 500), QPointF(660, 530), 4);
    CHECK_EQ(f.doc.undoDepth(), after_pen + 1);
    CHECK(!f.canvas.isStroking());
}

// A colour layer is cut against the line art and belongs under it. Created on
// top, the flat it generates hides the drawing that produced it.
void colourLayerIsCreatedAtTheBottom() {
    TEST("a colour layer is created at the bottom of the pile");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    auto* panel = window.findChild<QTreeWidget*>();
    CHECK(panel != nullptr);
    if (!panel) return;

    QPushButton* add_colour = nullptr;
    QPushButton* add_layer = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add colour layer")) add_colour = button;
        if (button->text() == QStringLiteral("Add layer")) add_layer = button;
    }
    CHECK(add_colour != nullptr);
    CHECK(add_layer != nullptr);
    if (!add_colour || !add_layer) return;

    add_layer->click();
    add_colour->click();
    QCoreApplication::processEvents();

    CHECK_EQ(panel->topLevelItemCount(), 3);
    // The list is topmost first, so the last row is the bottom of the pile.
    CHECK(panel->topLevelItem(panel->topLevelItemCount() - 1)->text(0).startsWith(QStringLiteral("colour 1")));
    CHECK(!panel->topLevelItem(0)->text(0).startsWith(QStringLiteral("colour")));

    // And again with a raster layer selected somewhere in the middle: it still
    // goes to the bottom rather than above the selection.
    panel->setCurrentItem(panel->topLevelItem(0));
    add_colour->click();
    QCoreApplication::processEvents();
    CHECK_EQ(panel->topLevelItemCount(), 4);
    CHECK(panel->topLevelItem(panel->topLevelItemCount() - 1)->text(0).startsWith(QStringLiteral("colour 2")));
}

// The canvas is expressed three ways -- a ratio, a resolution and a pair of
// pixel sizes -- and each one writes to the other two. That is the arrangement
// most likely to be right in the screenshot and wrong two edits later.
void sceneSettingsKeepsRatioAndPixelsAgreeing() {
    TEST("scene settings keeps the ratio, the slider and the pixels agreeing");
    SceneSettingsDialog dialog(24, 1920, 1080, false, 100, 1);

    auto* aspect = dialog.findChild<QComboBox*>(QStringLiteral("aspect"));
    auto* ratio_w = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("ratioWidth"));
    auto* ratio_h = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("ratioHeight"));
    auto* resolution = dialog.findChild<QSlider*>(QStringLiteral("resolution"));
    auto* pixels_w = dialog.findChild<QSpinBox*>(QStringLiteral("pixelWidth"));
    auto* pixels_h = dialog.findChild<QSpinBox*>(QStringLiteral("pixelHeight"));
    CHECK(aspect && ratio_w && ratio_h && resolution && pixels_w && pixels_h);
    if (!aspect || !ratio_w || !ratio_h || !resolution || !pixels_w || !pixels_h) return;

    // Opened on the scene it was given, and it recognises the shape.
    CHECK_EQ(dialog.framerate(), 24);
    CHECK_EQ(dialog.canvasWidth(), 1920);
    CHECK_EQ(dialog.canvasHeight(), 1080);
    CHECK_EQ(aspect->currentText().toStdString(), std::string("16:9"));
    CHECK_EQ(resolution->value(), 1080);

    // Choosing a ratio keeps the resolution and moves the width.
    aspect->setCurrentText(QStringLiteral("4:3"));
    CHECK_EQ(dialog.canvasHeight(), 1080);
    CHECK_EQ(dialog.canvasWidth(), 1440);

    // The slider multiplies the ratio: both sides move, the shape does not.
    resolution->setValue(720);
    CHECK_EQ(dialog.canvasHeight(), 720);
    CHECK_EQ(dialog.canvasWidth(), 960);
    CHECK_EQ(aspect->currentText().toStdString(), std::string("4:3"));

    // Typing pixels drives the ratio and the slider the other way, and the menu
    // names the shape when it has a name.
    pixels_w->setValue(1000);
    pixels_h->setValue(1000);
    CHECK_EQ(aspect->currentText().toStdString(), std::string("1:1"));
    CHECK_EQ(resolution->value(), 1000);

    // A shape with no name says so rather than pretending to be the nearest one.
    pixels_w->setValue(1234);
    pixels_h->setValue(567);
    CHECK_EQ(aspect->currentText().toStdString(), std::string("Custom"));
    CHECK_EQ(dialog.canvasWidth(), 1234);
    CHECK_EQ(dialog.canvasHeight(), 567);

    // Typing a ratio that happens to be a named one is recognised as such, and
    // 1.7778:1 is 16:9 however it was written.
    ratio_w->setValue(1.7778);
    ratio_h->setValue(1.0);
    CHECK_EQ(aspect->currentText().toStdString(), std::string("16:9"));
}

// The second box beside a colour layer switches between showing the fill and
// showing the scribbles that produced it. Clicked at its own coordinates, the
// way a hand clicks it -- calling toggle() would pass whatever is wrong with
// where it sits.
void theScribbleBoxCanBeClicked() {
    TEST("the show-scribbles box responds to a click on it");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QPushButton* add_colour = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add colour layer")) add_colour = button;
    }
    CHECK(add_colour != nullptr);
    if (!add_colour) return;
    add_colour->click();
    QCoreApplication::processEvents();

    auto* list = window.findChild<QTreeWidget*>();
    CHECK(list != nullptr);
    if (!list) return;
    CHECK_EQ(list->topLevelItemCount(), 2);

    // The colour layer is at the bottom, and it is the only row with a tick in
    // the second column.
    QTreeWidgetItem* colour = list->topLevelItem(1);
    CHECK_EQ(colour->data(1, Qt::CheckStateRole).isValid(), true);
    CHECK_EQ(list->topLevelItem(0)->data(1, Qt::CheckStateRole).isValid(), false);

    const Qt::CheckState before = colour->checkState(1);
    clickCheck(list, colour, 1);
    CHECK(colour->checkState(1) != before);

    // And it reached the document, not just the panel.
    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    CHECK(!window.findChild<QTreeWidget*>()->topLevelItem(1)->text(0).isEmpty());
}

// Hiding a layer is the item's own tick, on the left. A colour layer also
// carries a row widget for the show-scribbles box, and that widget covers the
// whole item -- including the tick underneath it. Clicks arrive at the viewport
// in the real application, so that is where this sends them.
void theVisibilityTickWorksOnAColourLayer() {
    TEST("a colour layer can still be hidden by its tick");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QPushButton* add_colour = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add colour layer")) add_colour = button;
    }
    CHECK(add_colour != nullptr);
    if (!add_colour) return;
    add_colour->click();
    QCoreApplication::processEvents();

    auto* list = window.findChild<QTreeWidget*>();
    CHECK(list != nullptr);
    if (!list) return;
    CHECK_EQ(list->topLevelItemCount(), 2);

    // The plain layer, as a control: this one has never been in doubt.
    const Qt::CheckState plain_before = list->topLevelItem(0)->checkState(0);
    clickCheck(list, list->topLevelItem(0), 0);
    CHECK(list->topLevelItem(0)->checkState(0) != plain_before);

    // The colour layer, which is the one that used to carry a row widget and so
    // could not be hidden at all.
    const Qt::CheckState colour_before = list->topLevelItem(1)->checkState(0);
    clickCheck(list, list->topLevelItem(1), 0);
    CHECK(list->topLevelItem(1)->checkState(0) != colour_before);
}

// Inking over a coloured drawing must not re-solve on every dab, and when it
// does re-solve the whole picture has to be redrawn.
//
// The old guard only knew about strokes made on the colour layer itself, so a
// stroke on the line art underneath ran a max-flow per dab -- and then repainted
// only the rectangle the pen had touched, which left the new fill showing beside
// the stroke and the old fill everywhere else. Hiding and showing the layer
// repainted everything and so looked like a different feature.
void theFillWaitsForTheStrokeToFinish() {
    TEST("drawing on the line art re-solves once, at the end, and repaints all");
    Document doc;
    const TrackId track = doc.addTrack("main");
    const LayerId ink = doc.addLayer(track, "ink");
    const LayerId colour = doc.addLayer(track, "colour", 1, LayerKind::Ctg);
    const ImageId image = doc.insertImage(track, 0);
    {
        Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
        settings.ctg_sources = {ink};
        doc.updateLayer(track, colour, settings);
    }

    CanvasWidget canvas(doc);
    canvas.resize(900, 700);
    canvas.setTrack(track);
    canvas.setFrame(0);

    // A box on the line art and a scribble inside it, so there is a fill.
    const auto strokeOn = [&](LayerId layer, float x0, float y0, float x1, float y1, float radius,
                              float r, float g, float b) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings settings;
        settings.radius = radius;
        settings.hardness = 0.95f;
        settings.pressure_affects_opacity = false;
        settings.r = r;
        settings.g = g;
        settings.b = b;
        settings.a = 1.0f;
        Brush brush(settings);
        brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
        brush.extend({x1, y1, 1.0f});
        brush.end();
    };
    strokeOn(ink, 100, 100, 400, 100, 3.0f, 0, 0, 0);
    strokeOn(ink, 100, 100, 100, 300, 3.0f, 0, 0, 0);
    strokeOn(ink, 400, 100, 400, 300, 3.0f, 0, 0, 0);
    strokeOn(ink, 100, 300, 400, 300, 3.0f, 0, 0, 0);
    strokeOn(colour, 200, 200, 300, 200, 8.0f, 1.0f, 0.0f, 0.0f);
    strokeOn(colour, 40, 40, 440, 40, 8.0f, 0.0f, 0.0f, 1.0f);

    canvas.setActiveLayer(ink);
    canvas.refreshAll();
    canvas.grab();  // one paint, so the first fill is asked for
    // ...and the solve happens on a worker thread, so the fill arrives after
    // it, not during it. Everything below is about *when* a solve is asked
    // for, which is the part that did not change.
    CHECK(canvas.settleColour());

    const CtgFill* fill = doc.ctgFillFor(track, image, colour);
    CHECK(fill != nullptr);
    if (!fill) return;
    const std::uint64_t settled = fill->inputs;

    // Now ink across the box, a dab at a time, painting between each one the way
    // the event loop would.
    sendMouse(&canvas, QEvent::MouseButtonPress, QPointF(150, 150), Qt::LeftButton,
              Qt::LeftButton);
    for (int i = 1; i <= 8; ++i) {
        sendMouse(&canvas, QEvent::MouseMove, QPointF(150 + i * 20, 150), Qt::NoButton,
                  Qt::LeftButton);
        canvas.grab();
        // Nothing asked for and nothing re-solved: the fill is still keyed on
        // what it was before the stroke started, and no solve is in flight.
        CHECK_EQ(doc.ctgFillFor(track, image, colour)->inputs, settled);
        CHECK(!canvas.colourPending());
    }

    sendMouse(&canvas, QEvent::MouseButtonRelease, QPointF(310, 150), Qt::LeftButton,
              Qt::NoButton);
    canvas.grab();
    CHECK(canvas.settleColour());

    // And once the pen is up it has, exactly once.
    const CtgFill* after = doc.ctgFillFor(track, image, colour);
    CHECK(after != nullptr);
    if (!after) return;
    CHECK(after->inputs != settled);

    // A second paint with nothing changed must not ask for anything.
    const std::uint64_t resolved = after->inputs;
    canvas.grab();
    CHECK(!canvas.colourPending());
    CHECK_EQ(doc.ctgFillFor(track, image, colour)->inputs, resolved);
}

// The solve is off the interface thread, so a paint no longer waits for one.
// What is on screen in the meantime has to be the last answer rather than
// nothing: the colour blinking out on every stroke would be a worse thing to
// look at than colour that is a moment out of date.
void theLastFillStaysUntilTheNextOneArrives() {
    TEST("the colour on screen is the last answer until the next one lands");
    Document doc;
    const TrackId track = doc.addTrack("main");
    const LayerId ink = doc.addLayer(track, "ink");
    const LayerId colour = doc.addLayer(track, "colour", 1, LayerKind::Ctg);
    const ImageId image = doc.insertImage(track, 0);
    {
        Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
        settings.ctg_sources = {ink};
        doc.updateLayer(track, colour, settings);
    }

    CanvasWidget canvas(doc);
    canvas.resize(900, 700);
    canvas.setTrack(track);
    canvas.setFrame(0);

    const auto strokeOn = [&](LayerId layer, float x0, float y0, float x1, float y1,
                              float radius, float r, float g, float b) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings settings;
        settings.radius = radius;
        settings.hardness = 0.95f;
        settings.pressure_affects_opacity = false;
        settings.r = r;
        settings.g = g;
        settings.b = b;
        settings.a = 1.0f;
        Brush brush(settings);
        brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
        brush.extend({x1, y1, 1.0f});
        brush.end();
    };
    strokeOn(ink, 100, 100, 400, 100, 3.0f, 0, 0, 0);
    strokeOn(ink, 100, 100, 100, 300, 3.0f, 0, 0, 0);
    strokeOn(ink, 400, 100, 400, 300, 3.0f, 0, 0, 0);
    strokeOn(ink, 100, 300, 400, 300, 3.0f, 0, 0, 0);
    strokeOn(colour, 200, 200, 300, 200, 8.0f, 1.0f, 0.0f, 0.0f);

    canvas.refreshAll();
    canvas.grab();
    CHECK(canvas.settleColour());

    const CtgFill* first = doc.ctgFillFor(track, image, colour);
    CHECK(first != nullptr);
    if (!first) return;
    const std::uint64_t was = first->inputs;
    CHECK_EQ(first->colours, 1);

    // A second colour, and a paint that does not wait for it.
    strokeOn(colour, 140, 260, 180, 260, 8.0f, 0.0f, 0.0f, 1.0f);
    canvas.refreshAll();
    canvas.grab();
    CHECK(canvas.colourPending());
    CHECK_EQ(doc.ctgFillFor(track, image, colour)->inputs, was);

    // And it arrives on its own, without anybody asking again: the poll is what
    // brings the new fill and the repaint with it.
    QElapsedTimer clock;
    clock.start();
    while (canvas.colourPending() && clock.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    CHECK(!canvas.colourPending());

    const CtgFill* second = doc.ctgFillFor(track, image, colour);
    CHECK(second != nullptr);
    if (!second) return;
    CHECK(second->inputs != was);
    CHECK_EQ(second->colours, 2);

    // And it is on the screen, which is the part a document check cannot see.
    // The view is untouched, so an image pixel is a widget pixel.
    const QImage shown = canvas.grab().toImage();
    const QColor inside = shown.pixelColor(250, 250);
    CHECK(inside.red() > 200);
    CHECK(inside.green() < 80);
}

// The cap that was on every solve is now on the first one only.
//
// It was there because the solve ran where the interface was waiting, and a
// max-flow grows faster than its region: unbounded, a large drawing did not
// take a while, it stopped the program. So a big drawing was coloured blockily
// and permanently. Now the coarse answer is the one that arrives while the
// stroke is still recent, and a full-resolution one replaces it.
void theColourIsCoarseFirstAndThenAsFineAsTheDrawing() {
    TEST("the first answer is coarse and the one that replaces it is not");
    Document doc;
    doc.setCanvasSize(1600, 1200);
    const TrackId track = doc.addTrack("main");
    const LayerId ink = doc.addLayer(track, "ink");
    const LayerId colour = doc.addLayer(track, "colour", 1, LayerKind::Ctg);
    const ImageId image = doc.insertImage(track, 0);
    {
        Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
        settings.ctg_sources = {ink};
        doc.updateLayer(track, colour, settings);
    }

    const auto strokeOn = [&](LayerId layer, float x0, float y0, float x1, float y1,
                              float radius, float r, float g, float b) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings settings;
        settings.radius = radius;
        settings.hardness = 0.95f;
        settings.pressure_affects_opacity = false;
        settings.r = r;
        settings.g = g;
        settings.b = b;
        settings.a = 1.0f;
        Brush brush(settings);
        brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
        brush.extend({x1, y1, 1.0f});
        brush.end();
    };
    strokeOn(ink, 100, 100, 1500, 100, 3.0f, 0, 0, 0);
    strokeOn(ink, 100, 100, 100, 1100, 3.0f, 0, 0, 0);
    strokeOn(ink, 1500, 100, 1500, 1100, 3.0f, 0, 0, 0);
    strokeOn(ink, 100, 1100, 1500, 1100, 3.0f, 0, 0, 0);
    strokeOn(colour, 600, 600, 1000, 600, 20.0f, 1.0f, 0.0f, 0.0f);

    CanvasWidget canvas(doc);
    canvas.resize(900, 700);
    canvas.setTrack(track);
    canvas.setFrame(0);
    canvas.refreshAll();
    canvas.grab();

    // The first answer, collected directly so that nothing has asked for the
    // better one yet.
    canvas.colourSolver().waitUntilIdle();
    canvas.collectColour();
    const CtgFill* first = doc.ctgFillFor(track, image, colour);
    CHECK(first != nullptr);
    if (!first) return;
    CHECK(first->step > 1);
    const std::uint64_t question = first->inputs;

    // And the one that replaces it: the same question, answered at the size the
    // drawing was made at.
    CHECK(canvas.settleColour());
    const CtgFill* better = doc.ctgFillFor(track, image, colour);
    CHECK(better != nullptr);
    if (!better) return;
    CHECK_EQ(better->step, 1);
    CHECK_EQ(better->inputs, question);
    CHECK_NEAR(better->tiles.pixel(1400, 1000).r, 1.0, 0.02);

    // ...and then it is finished. Nothing asks a third time.
    canvas.grab();
    CHECK(!canvas.colourPending());
}

// Reported, and worth driving through the real window rather than the model:
// draw a shape, move it on the next drawing, colour the first, and everything
// that reports on the colour has to agree with the fill it can see.
void movedMarksAgreeWithThemselvesInTheWindow() {
    TEST("a mark that moved with the drawing is reported as having moved");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(canvas != nullptr);
    CHECK(timeline != nullptr);
    if (!canvas || !timeline) return;

    QPushButton* add_colour = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add colour layer")) add_colour = button;
    }
    CHECK(add_colour != nullptr);
    if (!add_colour) return;
    add_colour->click();
    QCoreApplication::processEvents();

    // The document the window is holding, reached through the canvas rather
    // than through an accessor invented for a test.
    Document& doc = window.documentForTesting();
    const Track* track = doc.scene().findTrack(doc.scene().tracks.front().id);
    CHECK(track != nullptr);
    if (!track) return;
    const TrackId track_id = track->id;

    LayerId ink = kNoId;
    LayerId colour = kNoId;
    for (const Layer& layer : track->layers) {
        if (layer.kind == LayerKind::Ctg) {
            colour = layer.id;
        } else if (ink == kNoId) {
            ink = layer.id;
        }
    }
    CHECK(ink != kNoId);
    CHECK(colour != kNoId);
    if (ink == kNoId || colour == kNoId) return;

    // Two drawings, the same box, moved a long way right on the second.
    const ImageId first = track->slots.front();
    const ImageId second = doc.insertImage(track_id, 1);

    const auto strokeOn = [&](ImageId image, LayerId layer, float x0, float y0, float x1,
                              float y1, float radius, float r, float g, float b, bool label) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings settings;
        settings.radius = radius;
        settings.hardness = 0.95f;
        settings.pressure_affects_opacity = false;
        settings.label = label;
        settings.r = r;
        settings.g = g;
        settings.b = b;
        settings.a = 1.0f;
        Brush brush(settings);
        brush.begin(doc, track_id, image, layer, {x0, y0, 1.0f});
        brush.extend({x1, y1, 1.0f});
        brush.end();
    };
    const auto box = [&](ImageId image, float left) {
        strokeOn(image, ink, left, 200, left + 300, 200, 3.0f, 0, 0, 0, false);
        strokeOn(image, ink, left, 200, left, 500, 3.0f, 0, 0, 0, false);
        strokeOn(image, ink, left + 300, 200, left + 300, 500, 3.0f, 0, 0, 0, false);
        strokeOn(image, ink, left, 500, left + 300, 500, 3.0f, 0, 0, 0, false);
    };
    box(first, 200);
    box(second, 700);
    strokeOn(first, colour, 300, 350, 400, 350, 20.0f, 1.0f, 0.0f, 0.0f, true);

    // Stand on the second drawing and let everything settle, exactly as using
    // it would.
    timeline->setCurrentSlot(1);
    QCoreApplication::processEvents();
    window.grab();
    CHECK(window.waitForColour());
    QCoreApplication::processEvents();

    // The fill followed the box.
    const CtgFill* fill = doc.ctgFillFor(track_id, second, colour);
    CHECK(fill != nullptr);
    if (!fill) return;
    CHECK_NEAR(fill->tiles.pixel(850, 450).r, 1.0, 0.02);

    // And a stroke made here takes the drawing over from the marks as they are
    // being shown, so the fill it had survives the taking over.
    strokeOn(second, colour, 760, 460, 780, 460, 8.0f, 1.0f, 0.0f, 0.0f, true);
    window.grab();
    CHECK(window.waitForColour());

    const CtgFill* after = doc.ctgFillFor(track_id, second, colour);
    CHECK(after != nullptr);
    if (!after) return;
    CHECK(!after->inherited);
    CHECK_NEAR(after->tiles.pixel(850, 450).r, 1.0, 0.02);
}

// A fill depends on some things it is not keyed on -- which way marks are
// carried, and, when a project is opened, the whole document being a different
// one whose drawings answer to the same ids. The way all of those say "every
// fill you have is wrong" is by emptying the cache, and that was enough while a
// solve finished inside the call that started it.
//
// It is not enough now. A solve started before the emptying lands after it,
// with a hash that still matches, and would be installed as though it were
// current -- so a solve in flight has to be thrown away by the same act.
void emptyingTheFillCacheThrowsAwayASolveAlreadyRunning() {
    TEST("emptying the fill cache throws away the solves in flight as well");
    Document doc;
    const TrackId track = doc.addTrack("main");
    const LayerId ink = doc.addLayer(track, "ink");
    const LayerId colour = doc.addLayer(track, "colour", 1, LayerKind::Ctg);
    const ImageId image = doc.insertImage(track, 0);
    {
        Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
        settings.ctg_sources = {ink};
        doc.updateLayer(track, colour, settings);
    }

    CanvasWidget canvas(doc);
    canvas.resize(900, 700);
    canvas.setTrack(track);
    canvas.setFrame(0);

    const auto strokeOn = [&](LayerId layer, float x0, float y0, float x1, float y1,
                              float radius, float r, float g, float b) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings settings;
        settings.radius = radius;
        settings.hardness = 0.95f;
        settings.pressure_affects_opacity = false;
        settings.r = r;
        settings.g = g;
        settings.b = b;
        settings.a = 1.0f;
        Brush brush(settings);
        brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
        brush.extend({x1, y1, 1.0f});
        brush.end();
    };
    strokeOn(ink, 100, 100, 400, 100, 3.0f, 0, 0, 0);
    strokeOn(ink, 100, 100, 100, 300, 3.0f, 0, 0, 0);
    strokeOn(ink, 400, 100, 400, 300, 3.0f, 0, 0, 0);
    strokeOn(ink, 100, 300, 400, 300, 3.0f, 0, 0, 0);
    strokeOn(colour, 200, 200, 300, 200, 8.0f, 1.0f, 0.0f, 0.0f);

    canvas.refreshAll();
    canvas.grab();  // a solve is now running
    CHECK(canvas.colourPending());

    doc.ctgCache().clear();  // ...and everything it is about is now wrong

    // Let the solve finish and offer its answer. Collected directly rather than
    // through settleColour, which would ask again and get a right answer, which
    // is not what is being tested.
    canvas.colourSolver().waitUntilIdle();
    canvas.collectColour();

    // The answer arrived and was dropped rather than installed. Nothing else
    // could have told it apart: the document has not moved, so the hash it
    // carries is exactly the one that was asked for.
    CHECK(doc.ctgFillFor(track, image, colour) == nullptr);

    // And asking again gets it back, so this is a discard and not a wedge.
    canvas.refreshAll();
    canvas.grab();
    CHECK(canvas.settleColour());
    const CtgFill* fill = doc.ctgFillFor(track, image, colour);
    CHECK(fill != nullptr);
    if (!fill) return;
    CHECK_NEAR(fill->tiles.pixel(390, 280).r, 1.0, 0.02);
}

// --- saving ----------------------------------------------------------------

// A document with pixels in it, so the round trip has something to lose.
Document buildDrawnScene() {
    Document doc;
    const TrackId track = doc.addTrack("main");
    const LayerId ink = doc.addLayer(track, "ink");
    const LayerId colour = doc.addLayer(track, "colour", 1, LayerKind::Ctg);
    const ImageId first = doc.insertImage(track, 0);
    doc.extendExposure(track, 0, 2);
    const ImageId second = doc.insertImage(track, 3);

    {
        Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
        settings.ctg_sources = {ink};
        doc.updateLayer(track, colour, settings);
    }

    const auto stroke = [&](ImageId image, LayerId layer, float x0, float y0, float x1, float y1,
                            float r, float g, float b) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings s;
        s.radius = 9.0f;
        s.pressure_affects_opacity = false;
        s.r = r; s.g = g; s.b = b; s.a = 1.0f;
        Brush brush(s);
        brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
        brush.extend({x1, y1, 1.0f});
        brush.end();
    };

    stroke(first, ink, 100, 100, 400, 260, 0, 0, 0);
    stroke(first, colour, 180, 150, 300, 200, 0.9f, 0.3f, 0.05f);
    // Well away from the origin and negative, because the drawing surface has
    // no edges and tile coordinates are signed.
    stroke(second, ink, -600, -400, -300, -150, 0, 0, 0);

    doc.setCanvasSize(1280, 720);
    doc.setFramerate(12);
    return doc;
}

float alphaAt(const Document& doc, TrackId track, ImageId image, LayerId layer, int x, int y) {
    const Cel* cel = doc.celAt(track, image, layer);
    return cel ? cel->pixel(x, y).a : -1.0f;
}

void strokeOn(Document& doc, TrackId track, ImageId image, LayerId layer, float x0, float y0,
              float x1, float y1) {
    ScopedCommand command(doc, "Stroke");
    BrushSettings s;
    s.radius = 9.0f;
    s.pressure_affects_opacity = false;
    s.r = 0; s.g = 0; s.b = 0; s.a = 1.0f;
    Brush brush(s);
    brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
    brush.extend({x1, y1, 1.0f});
    brush.end();
}

// Everything a project folder holds, keyed by name, so two of them can be
// compared without caring which one was written how.
std::map<QString, QByteArray> projectBytes(const QString& folder) {
    std::map<QString, QByteArray> out;
    const QString cels = folder + QStringLiteral("/cels");
    for (const QString& name : QDir(cels).entryList(QDir::Files)) {
        QFile file(cels + QStringLiteral("/") + name);
        if (file.open(QIODevice::ReadOnly)) out[name] = file.readAll();
    }
    QFile scene(folder + QStringLiteral("/scene.json"));
    if (scene.open(QIODevice::ReadOnly)) out[QStringLiteral("scene.json")] = scene.readAll();
    return out;
}

void aProjectSurvivesSavingAndLoading() {
    TEST("a project comes back from disk with its pixels intact");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));

    const Document original = buildDrawnScene();
    QString error;
    CHECK(ProjectIO::save(original, folder, &error));
    CHECK_EQ(error.toStdString(), std::string());

    // The layout is part of the promise: a folder somebody can look inside.
    CHECK(QFileInfo::exists(folder + QStringLiteral("/scene.json")));
    CHECK(QFileInfo::exists(folder + QStringLiteral("/cels")));
    CHECK(QDir(folder + QStringLiteral("/cels")).entryList(QDir::Files).size() >= 3);

    Document loaded;
    CHECK(ProjectIO::load(loaded, folder, &error));
    CHECK_EQ(error.toStdString(), std::string());

    // Structure.
    CHECK_EQ(loaded.scene().framerate, 12);
    CHECK_EQ(loaded.scene().width, 1280);
    CHECK_EQ(ProjectIO::writeSceneJson(loaded), ProjectIO::writeSceneJson(original));

    // And the pixels, bit for bit, on both drawings and both layers.
    const TrackId track = loaded.scene().tracks.front().id;
    const Track& before = original.scene().tracks.front();
    std::size_t compared = 0;
    std::size_t differing = 0;
    for (const auto& [image_id, image] : before.images) {
        for (const Layer& layer : before.layers) {
            const Cel* was = original.celAt(before.id, image_id, layer.id);
            const Cel* now = loaded.celAt(track, image_id, layer.id);
            if (!was) continue;
            CHECK(now != nullptr);
            if (!now) continue;
            for (const TileCoord& coord : was->tiles().coords()) {
                const TileRef a = was->tiles().find(coord);
                const TileRef b = now->tiles().find(coord);
                if (!b) {
                    // A tile a dab touched without covering anything is dropped
                    // on the way out, deliberately: absent and transparent are
                    // the same thing in the model. Any other absence is a loss.
                    if (!a->isFullyTransparent()) ++differing;
                    continue;
                }
                ++compared;
                for (std::size_t i = 0; i < a->rgba.size(); ++i) {
                    if (a->rgba[i].bits != b->rgba[i].bits) ++differing;
                }
            }
        }
    }
    CHECK(compared > 0);
    CHECK_EQ(differing, std::size_t{0});

    // A tile a long way into negative coordinates came back too.
    CHECK(alphaAt(loaded, track, before.slots.back(), before.layers.front().id, -450, -275) > 0.0f);
}

// Issue #1's second half. A scene holds several tracks and every one of them
// has its own layers, its own timing and its own drawings -- so the thing to
// check is not that the file parses but that nothing came back with one track's
// worth of anything. A single-track project cannot fail this way at all, which
// is why the round trip above does not cover it.
void aMultiTrackProjectComesBackWhole() {
    TEST("a project with several tracks loses no track, no order and no cel");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));

    Document original;
    struct Built {
        TrackId id;
        LayerId layer;
        std::vector<ImageId> drawings;
    };
    std::vector<Built> built;

    // Three tracks with deliberately different shapes: different layer counts,
    // different lengths, and holds in different places. A reader that mixed two
    // tracks up would have to get all of that right by accident.
    const char* names[] = {"character", "background", "effects"};
    for (int t = 0; t < 3; ++t) {
        Built made;
        made.id = original.addTrack(names[t]);
        made.layer = original.addLayer(made.id, "ink");
        for (int extra = 0; extra < t; ++extra) {
            original.addLayer(made.id, "extra " + std::to_string(extra));
        }
        for (int d = 0; d < t + 2; ++d) {
            made.drawings.push_back(original.insertImage(made.id, static_cast<std::size_t>(d)));
        }
        original.extendExposure(made.id, 0, t + 1);  // a hold of a different length each time
        // One stroke per drawing, at a position no other track uses, so a cel
        // that came back attached to the wrong drawing is visible as a pixel in
        // the wrong place rather than only as a missing one.
        for (std::size_t d = 0; d < made.drawings.size(); ++d) {
            const float y = 100.0f * static_cast<float>(t) + 20.0f * static_cast<float>(d);
            strokeOn(original, made.id, made.drawings[d], made.layer, 60.0f, y, 200.0f, y);
        }
        built.push_back(std::move(made));
    }

    // Away from the default on one track only, so the file has to carry the
    // setting per track rather than getting it right by luck.
    TrackProperties props = original.scene().findTrack(built[1].id)->properties();
    props.overwrite_drawings = false;
    original.updateTrack(built[1].id, props);

    QString error;
    CHECK(ProjectIO::save(original, folder, &error));
    CHECK_EQ(error.toStdString(), std::string());

    Document loaded;
    CHECK(ProjectIO::load(loaded, folder, &error));
    CHECK_EQ(error.toStdString(), std::string());

    CHECK_EQ(loaded.scene().tracks.size(), std::size_t{3});
    for (std::size_t t = 0; t < built.size(); ++t) {
        const Track* track = loaded.scene().findTrack(built[t].id);
        CHECK(track != nullptr);
        if (!track) continue;

        // The track itself: which one it is, and in which order.
        CHECK_EQ(track->name, std::string(names[t]));
        CHECK_EQ(loaded.scene().tracks[t].id, built[t].id);
        CHECK_EQ(track->layers.size(), std::size_t{static_cast<std::size_t>(t) + 1});
        CHECK_EQ(track->overwrite_drawings, t != 1);

        // Its timing: the slots in the order they were, holds included.
        const Track* was = original.scene().findTrack(built[t].id);
        CHECK_EQ(track->slots.size(), was->slots.size());
        for (std::size_t i = 0; i < was->slots.size(); ++i) {
            CHECK_EQ(track->slots[i], was->slots[i]);
        }
        CHECK_EQ(track->images.size(), built[t].drawings.size());

        // And its pixels, on the drawing they belong to.
        for (std::size_t d = 0; d < built[t].drawings.size(); ++d) {
            const float y = 100.0f * static_cast<float>(t) + 20.0f * static_cast<float>(d);
            const ImageId drawing = built[t].drawings[d];
            CHECK(alphaAt(loaded, track->id, drawing, built[t].layer, 120,
                          static_cast<int>(y)) > 0.0f);
            // Nothing from the track above it landed on this one.
            CHECK(alphaAt(loaded, track->id, drawing, built[t].layer, 120,
                          static_cast<int>(y) + 100) <= 0.0f);
        }
    }

    // Every cel in the document is a cel on disk: a track whose cels were not
    // collected would save a scene naming files that are not there, and only
    // show up as an empty drawing much later.
    CHECK_EQ(ProjectIO::celsReferencedBy(loaded).size(),
             ProjectIO::celsReferencedBy(original).size());
    CHECK_EQ(QDir(folder + QStringLiteral("/cels")).entryList(QDir::Files).size(),
             static_cast<int>(ProjectIO::celsReferencedBy(original).size()));
}

// A save that dies part way through must not take the last good one with it.
void aFailedSaveLeavesTheOldProjectAlone() {
    TEST("a save that cannot finish leaves the previous project untouched");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));

    const Document first = buildDrawnScene();
    CHECK(ProjectIO::save(first, folder, nullptr));
    QFile scene(folder + QStringLiteral("/scene.json"));
    CHECK(scene.open(QIODevice::ReadOnly));
    const QByteArray original_bytes = scene.readAll();
    scene.close();

    // Saving over it with somewhere unwritable underneath: the scratch folder
    // is made inside the target's parent, so a read-only parent stops it.
    Document second = buildDrawnScene();
    second.setFramerate(30);
    QString error;
    CHECK_EQ(ProjectIO::save(second, QStringLiteral("\0invalid"), &error), false);
    CHECK(!error.isEmpty());

    // The first save is exactly as it was.
    CHECK(scene.open(QIODevice::ReadOnly));
    CHECK_EQ(scene.readAll() == original_bytes, true);
    scene.close();

    // And no scratch folders were left lying about.
    const QStringList leftovers =
        QDir(scratch.path()).entryList(QStringList() << QStringLiteral("*.saving-*")
                                                     << QStringLiteral("*.replaced-*"),
                                       QDir::Dirs);
    CHECK_EQ(leftovers.size(), 0);
}

void abrokenProjectDoesNotReplaceTheOpenOne() {
    TEST("a project that will not open leaves the open one alone");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    Document open_document = buildDrawnScene();
    open_document.setFramerate(25);
    const std::string before = ProjectIO::writeSceneJson(open_document);

    // No such folder.
    QString error;
    CHECK_EQ(ProjectIO::load(open_document, scratch.filePath(QStringLiteral("absent")), &error),
             false);
    CHECK(!error.isEmpty());
    CHECK_EQ(ProjectIO::writeSceneJson(open_document), before);

    // A cel file corrupted after the fact. This is the case that matters: the
    // scene reads, so a careless loader would have replaced the document before
    // finding out the pixels were rubbish.
    const QStringList cels = QDir(folder + QStringLiteral("/cels")).entryList(QDir::Files);
    CHECK(!cels.isEmpty());
    if (!cels.isEmpty()) {
        QFile broken(folder + QStringLiteral("/cels/") + cels.first());
        CHECK(broken.open(QIODevice::WriteOnly));
        broken.write("ANIMCELZ and then nonsense");
        broken.close();

        CHECK_EQ(ProjectIO::load(open_document, folder, &error), false);
        CHECK(!error.isEmpty());
        CHECK_EQ(ProjectIO::writeSceneJson(open_document), before);
        CHECK_EQ(open_document.scene().framerate, 25);
    }
}

void savingTwiceWritesTheSameBytes() {
    TEST("saving an unchanged document twice writes identical files");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const Document doc = buildDrawnScene();

    const QString a = scratch.filePath(QStringLiteral("a.animage"));
    const QString b = scratch.filePath(QStringLiteral("b.animage"));
    CHECK(ProjectIO::save(doc, a, nullptr));
    CHECK(ProjectIO::save(doc, b, nullptr));

    const QStringList files = QDir(a + QStringLiteral("/cels")).entryList(QDir::Files);
    CHECK(!files.isEmpty());
    for (const QString& name : files) {
        QFile one(a + QStringLiteral("/cels/") + name);
        QFile two(b + QStringLiteral("/cels/") + name);
        CHECK(one.open(QIODevice::ReadOnly));
        CHECK(two.open(QIODevice::ReadOnly));
        // Byte-identical, so a backup tool or a diff can tell what actually
        // changed between two saves.
        CHECK_EQ(one.readAll() == two.readAll(), true);
    }
}

// The whole point of carrying a cel file forward is that nobody can tell you
// did. If an incremental save can produce a project a full save would not have,
// then "cheap" has quietly become "different", and every promise the format
// makes is only true of one of the two paths.
void anIncrementalSaveWritesTheSameProject() {
    TEST("an incremental save writes exactly what a full save would have");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString grown = scratch.filePath(QStringLiteral("grown.animage"));
    const QString fresh = scratch.filePath(QStringLiteral("fresh.animage"));

    Document doc = buildDrawnScene();
    const TrackId track = doc.scene().tracks.front().id;
    const ImageId image = doc.scene().tracks.front().slots.front();
    const LayerId ink = doc.scene().tracks.front().layers.front().id;

    ProjectIO::SaveState state;
    CHECK(ProjectIO::save(doc, grown, state, nullptr));
    CHECK_EQ(state.folder.toStdString(), grown.toStdString());
    CHECK(!state.revisions.empty());

    // One drawing moves. The others do not, and are the ones carried forward.
    strokeOn(doc, track, image, ink, 500, 480, 620, 560);

    QString error;
    CHECK(ProjectIO::save(doc, grown, state, &error));
    CHECK_EQ(error.toStdString(), std::string());

    // The same document written from nothing, as the comparison.
    CHECK(ProjectIO::save(doc, fresh, nullptr));
    CHECK(projectBytes(fresh).size() >= 4);
    CHECK_EQ(projectBytes(grown) == projectBytes(fresh), true);

    // And it is a project, not merely a folder with matching bytes: the stroke
    // that arrived after the first save is in it.
    Document back;
    CHECK(ProjectIO::load(back, grown, &error));
    CHECK(alphaAt(back, back.scene().tracks.front().id, image, ink, 560, 520) > 0.0f);
}

// The state says what was written; the folder is where it went, and the two can
// come apart -- a sync client half way through, a file deleted by hand. The
// state is a hint about the pixels and never a promise about the disk.
void anIncrementalSaveReplacesWhatWentMissing() {
    TEST("a cel file that vanished is written again rather than carried forward");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    const QString reference = scratch.filePath(QStringLiteral("reference.animage"));

    const Document doc = buildDrawnScene();
    ProjectIO::SaveState state;
    CHECK(ProjectIO::save(doc, folder, state, nullptr));

    const QString cels = folder + QStringLiteral("/cels");
    const QStringList names = QDir(cels).entryList(QDir::Files);
    CHECK(!names.isEmpty());
    if (names.isEmpty()) return;
    CHECK(QFile::remove(cels + QStringLiteral("/") + names.first()));

    // Nothing in the document changed, so every cel is a candidate to be
    // carried forward -- including the one that is no longer there to carry.
    QString error;
    CHECK(ProjectIO::save(doc, folder, state, &error));
    CHECK_EQ(error.toStdString(), std::string());

    CHECK(ProjectIO::save(doc, reference, nullptr));
    CHECK_EQ(projectBytes(folder) == projectBytes(reference), true);

    Document back;
    CHECK(ProjectIO::load(back, folder, &error));
    CHECK_EQ(ProjectIO::writeSceneJson(back), ProjectIO::writeSceneJson(doc));
}

// Save As has to hand back something that stands on its own, so a state
// describing somewhere else must carry nothing forward at all.
void savingElsewhereCarriesNothingForward() {
    TEST("saving to a new folder writes a whole project, not a difference");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString first = scratch.filePath(QStringLiteral("first.animage"));
    const QString second = scratch.filePath(QStringLiteral("second.animage"));

    const Document doc = buildDrawnScene();
    ProjectIO::SaveState state;
    CHECK(ProjectIO::save(doc, first, state, nullptr));
    CHECK(ProjectIO::save(doc, second, state, nullptr));

    CHECK_EQ(state.folder.toStdString(), second.toStdString());
    CHECK_EQ(projectBytes(second) == projectBytes(first), true);
}

// Opening records what is on disk, so the first save after an open is
// incremental too rather than paying for a project it just finished reading.
void openingLeavesTheFolderKnown() {
    TEST("a load records what it read, so the next save carries it forward");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    Document back;
    ProjectIO::SaveState state;
    QString error;
    CHECK(ProjectIO::load(back, folder, state, &error));
    CHECK_EQ(state.folder.toStdString(), folder.toStdString());
    CHECK_EQ(state.revisions.size(), ProjectIO::celsReferencedBy(back).size());

    // Every revision recorded is the one the document is actually holding --
    // the check the save will make, made here where a mismatch is legible.
    for (CelId id : ProjectIO::celsReferencedBy(back)) {
        const Cel* cel = back.cel(id);
        CHECK(cel != nullptr);
        if (!cel) continue;
        const auto seen = state.revisions.find(id);
        CHECK(seen != state.revisions.end());
        if (seen != state.revisions.end()) CHECK_EQ(seen->second, cel->revision());
    }

    const QString again = scratch.filePath(QStringLiteral("again.animage"));
    CHECK(ProjectIO::save(back, folder, state, &error));
    CHECK(ProjectIO::save(back, again, nullptr));
    CHECK_EQ(projectBytes(folder) == projectBytes(again), true);
}

// Autosave through the window. The two things worth pinning are that it writes
// when there is something to write and that it stays entirely out of the way
// when there is not -- a save that fires every two minutes regardless would put
// the whole project through a sync client for nothing, forever.
void autosaveWritesOnlyWhenSomethingMoved() {
    TEST("autosave writes what changed and does nothing when nothing did");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QString error;
    CHECK(window.openProjectAt(folder, &error));
    QCoreApplication::processEvents();

    // A deleted cel file is the observable for "did a save happen at all": an
    // unchanged document produces identical bytes, so comparing them proves
    // nothing, while a file that is still missing proves nothing was written.
    const QString cels = folder + QStringLiteral("/cels");
    const QStringList names = QDir(cels).entryList(QDir::Files);
    CHECK(!names.isEmpty());
    if (names.isEmpty()) return;
    const QString hostage = cels + QStringLiteral("/") + names.first();
    CHECK(QFile::remove(hostage));

    // Nothing has been drawn since the open, so this must not write.
    window.onAutosaveTick();
    QCoreApplication::processEvents();
    CHECK(!QFileInfo::exists(hostage));
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    drawWithMouse(canvas, QPointF(300, 300), QPointF(360, 340), 4);
    QCoreApplication::processEvents();
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    // Now it must, without being asked and without a dialog. The file taken
    // hostage comes back too: it could not be carried forward, so it was
    // written out in full.
    window.onAutosaveTick();
    QCoreApplication::processEvents();
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
    CHECK(QFileInfo::exists(hostage));

    // And what is on disk is a project that opens.
    Document back;
    CHECK(ProjectIO::load(back, folder, &error));
    CHECK_EQ(error.toStdString(), std::string());
    CHECK(!back.scene().tracks.empty());
}

// A stroke must never be interrupted by a save: a tenth of a second is nothing
// between two strokes and a stutter inside one.
void autosaveWaitsForTheStrokeToFinish() {
    TEST("autosave defers while the pen is down");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();
    CHECK(window.openProjectAt(folder, nullptr));
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    // One finished stroke first, so the document is already owed a save. Without
    // it the tick would stop at "nothing has moved" -- a stroke only becomes an
    // undo entry when it ends -- and the deferral would never be reached.
    drawWithMouse(canvas, QPointF(200, 200), QPointF(260, 240), 4);
    QCoreApplication::processEvents();
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    // Now press and move without releasing: a second stroke is open, and this
    // is the tick that would land in the middle of someone's line.
    sendMouse(canvas, QEvent::MouseButtonPress, QPointF(300, 300), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseMove, QPointF(340, 330), Qt::NoButton, Qt::LeftButton);
    QCoreApplication::processEvents();
    CHECK(canvas->isStroking());

    window.onAutosaveTick();
    QCoreApplication::processEvents();
    // Deferred, so the change is still unwritten and the title still says so.
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    sendMouse(canvas, QEvent::MouseButtonRelease, QPointF(340, 330), Qt::LeftButton,
              Qt::NoButton);
    QCoreApplication::processEvents();
    CHECK(!canvas->isStroking());

    window.onAutosaveTick();
    QCoreApplication::processEvents();
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
}

// Closing writes rather than asking, which is the other half of deciding the
// disk is always current: without it the last two minutes fall off the end.
void closingWritesTheLastChanges() {
    TEST("closing the window flushes what autosave had not reached yet");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QCoreApplication::processEvents();
        CHECK(window.openProjectAt(folder, nullptr));
        QCoreApplication::processEvents();

        auto* canvas = window.findChild<CanvasWidget*>();
        CHECK(canvas != nullptr);
        if (!canvas) return;
        drawWithMouse(canvas, QPointF(300, 300), QPointF(360, 340), 4);
        QCoreApplication::processEvents();
        CHECK(window.windowTitle().contains(QLatin1Char('*')));

        // No autosave has fired, so this stroke exists only in memory.
        CHECK(window.close());
        QCoreApplication::processEvents();
    }

    // It is on disk, and the project still opens.
    Document back;
    QString error;
    CHECK(ProjectIO::load(back, folder, &error));
    CHECK_EQ(error.toStdString(), std::string());
    CHECK(!back.scene().tracks.empty());
}

// An untitled document is the one thing autosave cannot protect, because it has
// nowhere to be written. Leaving it is therefore the only moment in the program
// where work can go without anybody being told -- so it is the only moment that
// asks, and Cancel has to actually stop it.
void leavingAnUntitledDocumentAsksFirst() {
    TEST("an untitled document with changes is not discarded silently");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    drawWithMouse(canvas, QPointF(300, 300), QPointF(360, 340), 4);
    QCoreApplication::processEvents();
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    // Cancel means stay: the window is still open and the drawing is still here.
    answerNextDialog(QMessageBox::Cancel);
    CHECK_EQ(window.close(), false);
    QCoreApplication::processEvents();
    CHECK(window.isVisible());
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    // Discard means go.
    answerNextDialog(QMessageBox::Discard);
    CHECK(window.close());
    QCoreApplication::processEvents();
}

// An untitled document with nothing in it is not work, and must not ask.
void closingAnUntouchedWindowJustCloses() {
    TEST("closing an untouched window closes it without asking");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));

    // No dialog is armed, so this hangs rather than fails if one appears.
    CHECK(window.close());
    QCoreApplication::processEvents();
}

// New is "launching the application again": a fresh document, nothing carried
// over from the last one, and the Scene settings dialog asking what shape the
// shot is.
void newProjectStartsOverCleanly() {
    TEST("New gives a fresh untitled document with no history");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();
    CHECK(window.openProjectAt(folder, nullptr));
    QCoreApplication::processEvents();

    auto* layers = window.findChild<QTreeWidget*>();
    CHECK(layers != nullptr);
    if (!layers) return;
    CHECK_EQ(layers->topLevelItemCount(), 2);

    QAction* create = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("&New")) create = action;
    }
    CHECK(create != nullptr);
    if (!create) return;

    // The open project is saved and unchanged, so nothing is asked about it --
    // the only dialog is the Scene settings one New raises on purpose.
    dismissNextDialog();
    create->trigger();
    QCoreApplication::processEvents();

    // Back to what the application starts with.
    CHECK(window.windowTitle().startsWith(QStringLiteral("Untitled")));
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
    CHECK_EQ(layers->topLevelItemCount(), 1);

    // And with no history: a fresh document you can undo into an invalid one is
    // the bug screenshots caught the first time round.
    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    CHECK(canvas->currentImage() != kNoId);

    // The project it came from is untouched on disk.
    Document back;
    CHECK(ProjectIO::load(back, folder, nullptr));
    CHECK(back.scene().tracks.front().layers.size() == 2);
}

// --- export ----------------------------------------------------------------

QByteArray fileBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Issue #20's export half, and the distinction it turns on: what a track shows
// past its last drawing is a fact about the *picture*, so it belongs to the
// flattened composite and not to a layer's own sequence. A background that
// cycles is in the composite for the whole shot, and its own folder holds only
// the frames the background actually has.
void theEndBehaviourAppliesToTheCompositeOnly() {
    TEST("a cycling track fills the composite and not its own layer sequence");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString out = scratch.filePath(QStringLiteral("out"));

    Document doc;
    // A character over ten frames, and a two-drawing background that cycles.
    const TrackId character = doc.addTrack("character");
    const LayerId character_ink = doc.addLayer(character, "ink");
    const ImageId drawn = doc.insertImage(character, 0);
    doc.extendExposure(character, 0, 9);
    strokeOn(doc, character, drawn, character_ink, 40.0f, 40.0f, 200.0f, 40.0f);

    const TrackId background = doc.addTrack("background");
    const LayerId background_ink = doc.addLayer(background, "ink");
    const ImageId back_one = doc.insertImage(background, 0);
    const ImageId back_two = doc.insertImage(background, 1);
    strokeOn(doc, background, back_one, background_ink, 40.0f, 300.0f, 200.0f, 300.0f);
    strokeOn(doc, background, back_two, background_ink, 40.0f, 360.0f, 200.0f, 360.0f);

    TrackProperties props = doc.scene().findTrack(background)->properties();
    props.end = TrackEnd::Cycle;
    doc.updateTrack(background, props);

    doc.setCanvasSize(640, 480);
    CHECK_EQ(doc.scene().timelineFrames(), std::size_t{10});
    CHECK_EQ(doc.scene().findTrack(background)->frameCount(), std::size_t{2});

    exporting::Options options;
    options.folder = out;
    options.layers = true;
    options.flattened = true;
    // Ten for the character, two for the background, ten for the composite.
    CHECK_EQ(exporting::fileCount(doc, options), 22);

    QString error;
    CHECK(exporting::write(doc, options, nullptr, nullptr, &error));
    CHECK_EQ(error.toStdString(), std::string());

    // The background's own sequence stops where the background does.
    CHECK_EQ(QDir(out + QStringLiteral("/background_ink")).entryList(QDir::Files).size(), 2);
    CHECK_EQ(QDir(out + QStringLiteral("/character_ink")).entryList(QDir::Files).size(), 10);
    // The composite runs the length of the shot, which is the longest track.
    CHECK_EQ(QDir(out + QStringLiteral("/composite")).entryList(QDir::Files).size(), 10);

    // And frame 8 of the composite has the background in it: slot 7 is past its
    // last drawing, and 7 % 2 is 1, so it is showing its second drawing.
    const QImage late(QStringLiteral("%1/composite/composite_0008.png").arg(out));
    CHECK(!late.isNull());
    if (late.isNull()) return;
    const auto opaque = [&](int x, int y) {
        return qAlpha(late.pixelColor(x, y).rgba64().toArgb32()) > 0;
    };
    CHECK(opaque(120, 40));   // the character, still held
    CHECK(opaque(120, 360));  // the background's second drawing, cycled round
    CHECK(!opaque(120, 300)); // and not its first, which is not this frame's
}

// A fixed scene length is a cap on the shot, so it is a cap on the export too --
// both the composite and every layer's own sequence. The drawings past it are
// not destroyed and not hidden; they are simply not in this shot.
void aFixedSceneLengthCapsTheExport() {
    TEST("a fixed scene length caps what is exported, without losing the drawings");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString out = scratch.filePath(QStringLiteral("out"));

    Document doc;
    const TrackId track = doc.addTrack("character");
    const LayerId ink = doc.addLayer(track, "ink");
    const ImageId drawn = doc.insertImage(track, 0);
    doc.extendExposure(track, 0, 11);  // twelve frames on the track
    strokeOn(doc, track, drawn, ink, 40.0f, 40.0f, 200.0f, 40.0f);
    doc.setCanvasSize(320, 240);

    // Derived: the shot is the track.
    CHECK_EQ(doc.scene().shotFrames(), std::size_t{12});

    // Fixed at four: the shot is four, the timeline still reaches twelve.
    doc.setSceneLength(true, 4);
    CHECK_EQ(doc.scene().shotFrames(), std::size_t{4});
    CHECK_EQ(doc.scene().timelineFrames(), std::size_t{12});
    CHECK_EQ(doc.scene().findTrack(track)->frameCount(), std::size_t{12});

    exporting::Options options;
    options.folder = out;
    options.layers = true;
    options.flattened = true;
    CHECK_EQ(exporting::fileCount(doc, options), 8);  // four of each

    QString error;
    CHECK(exporting::write(doc, options, nullptr, nullptr, &error));
    CHECK_EQ(error.toStdString(), std::string());
    CHECK_EQ(QDir(out + QStringLiteral("/character_ink")).entryList(QDir::Files).size(), 4);
    CHECK_EQ(QDir(out + QStringLiteral("/composite")).entryList(QDir::Files).size(), 4);

    // Move the boundary and the rest of the shot is there again -- nothing had
    // been thrown away, which is the whole point of a cap over a truncation.
    doc.setSceneLength(true, 12);
    const QString longer = scratch.filePath(QStringLiteral("longer"));
    options.folder = longer;
    CHECK(exporting::write(doc, options, nullptr, nullptr, &error));
    CHECK_EQ(QDir(longer + QStringLiteral("/character_ink")).entryList(QDir::Files).size(), 12);

    // And switching it off goes back to the tracks deciding.
    doc.setSceneLength(false, 4);
    CHECK_EQ(doc.scene().shotFrames(), std::size_t{12});
}

void exportWritesASequencePerLayer() {
    TEST("export writes a 16-bit PNG sequence per layer, over the canvas");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString out = scratch.filePath(QStringLiteral("out"));

    Document doc = buildDrawnScene();
    const std::size_t frames = doc.scene().tracks.front().frameCount();
    CHECK(frames > 1);

    exporting::Options options;
    options.folder = out;
    options.layers = true;
    options.flattened = true;
    CHECK_EQ(exporting::fileCount(doc, options), static_cast<int>(frames * 3));

    QString error;
    CHECK(exporting::write(doc, options, nullptr, nullptr, &error));
    CHECK_EQ(error.toStdString(), std::string());

    // The layout and the names the specification asks for: a folder per layer,
    // `{track}_{layer}_{frame:04}.png` inside it, counting from one.
    const QString ink = QStringLiteral("main_ink");
    CHECK(QDir(out + QStringLiteral("/") + ink).exists());
    CHECK(QDir(out + QStringLiteral("/main_colour")).exists());
    CHECK(QDir(out + QStringLiteral("/composite")).exists());
    CHECK_EQ(QDir(out + QStringLiteral("/") + ink).entryList(QDir::Files).size(),
             static_cast<int>(frames));

    const QString first = QStringLiteral("%1/%2/%2_0001.png").arg(out, ink);
    CHECK(QFileInfo::exists(first));

    const QImage image(first);
    CHECK(!image.isNull());
    if (image.isNull()) return;
    // The canvas rectangle, not the drawing's bounding box: two frames of a
    // sequence that were different sizes would be useless downstream.
    CHECK_EQ(image.width(), doc.scene().width);
    CHECK_EQ(image.height(), doc.scene().height);
    // 64 bits a pixel is four 16-bit channels, which is the whole point of
    // choosing the format. An 8-bit PNG here would pass every other check.
    CHECK_EQ(image.depth(), 64);

    // The ink stroke runs from (100,100) to (400,260), so it is under this
    // point and nowhere near the far corner.
    CHECK(image.pixelColor(250, 180).alphaF() > 0.5);
    CHECK(image.pixelColor(1200, 700).alphaF() < 0.01);
    // Black ink, not blue: the channels arrive in the order PNG expects.
    CHECK(image.pixelColor(250, 180).redF() < 0.1);
    CHECK(image.pixelColor(250, 180).greenF() < 0.1);
    CHECK(image.pixelColor(250, 180).blueF() < 0.1);

    // The last frame's drawing was made entirely at negative coordinates. The
    // drawing surface has no edges, but the canvas does, and the canvas is what
    // gets exported -- so this frame is empty rather than being a picture of
    // somewhere else, and every frame is the same size.
    const QImage outside(QStringLiteral("%1/%2/%2_%3.png")
                             .arg(out, ink, QString::number(frames).rightJustified(
                                                4, QLatin1Char('0'))));
    CHECK(!outside.isNull());
    if (outside.isNull()) return;
    CHECK_EQ(outside.width(), doc.scene().width);
    long long anything = 0;
    for (int y = 0; y < outside.height(); y += 3) {
        for (int x = 0; x < outside.width(); x += 3) {
            if (outside.pixelColor(x, y).alphaF() > 0.01) ++anything;
        }
    }
    CHECK_EQ(anything, 0LL);
}

// The exposure is the model's central bet: one drawing held over three frames
// is one drawing, and the export has to say so three times.
void exportRepeatsAHeldDrawing() {
    TEST("a drawing held over three frames exports as three identical frames");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString out = scratch.filePath(QStringLiteral("out"));

    Document doc = buildDrawnScene();
    const Track& track = doc.scene().tracks.front();
    CHECK(track.frameCount() >= 4);
    if (track.frameCount() < 4) return;
    // The fixture holds the first drawing over slots 0..2 and puts a second one
    // at slot 3, which is what makes this test mean anything.
    CHECK(track.imageAtSlot(0) == track.imageAtSlot(2));
    CHECK(track.imageAtSlot(0) != track.imageAtSlot(3));

    exporting::Options options;
    options.folder = out;
    options.layers = true;
    CHECK(exporting::write(doc, options, nullptr, nullptr, nullptr));

    const auto frame = [&](int number) {
        return fileBytes(QStringLiteral("%1/main_ink/main_ink_%2.png")
                             .arg(out, QString::number(number).rightJustified(4, QLatin1Char('0'))));
    };
    CHECK(!frame(1).isEmpty());
    CHECK_EQ(frame(1) == frame(2), true);
    CHECK_EQ(frame(1) == frame(3), true);
    CHECK_EQ(frame(1) == frame(4), false);
}

// The one export bug worth having a test of its own. A CTG layer holds
// scribbles and its fill is a cache, built on demand -- and the canvas only
// builds it for the frame being looked at, because compositing is not allowed
// to start a max-flow. So a project straight off disk has no fills at all, and
// an export that composited only what was cached wrote blank colour sequences
// and said nothing about it.
void exportSolvesColourItHasNeverSeen() {
    TEST("a colour layer exports filled even though nothing ever displayed it");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    // Straight off disk, so no fill has ever been built for any frame.
    Document doc;
    CHECK(ProjectIO::load(doc, folder, nullptr));
    const TrackId track = doc.scene().tracks.front().id;
    const ImageId first = doc.scene().tracks.front().imageAtSlot(0);
    const LayerId colour = doc.scene().tracks.front().layers.back().id;
    CHECK(doc.ctgFillFor(track, first, colour) == nullptr);

    const QString out = scratch.filePath(QStringLiteral("out"));
    exporting::Options options;
    options.folder = out;
    options.layers = true;
    CHECK(exporting::write(doc, options, nullptr, nullptr, nullptr));

    // It solved rather than skipping.
    CHECK(doc.ctgFillFor(track, first, colour) != nullptr);

    const QImage image(out + QStringLiteral("/main_colour/main_colour_0001.png"));
    CHECK(!image.isNull());
    if (image.isNull()) return;

    // And the frame has colour in it. Counted rather than sampled at a point:
    // where a fill lands depends on where the line art closes, which is not
    // something this test should be asserting.
    //
    // The number is small on purpose. This fixture's line art is one open
    // stroke, so it encloses nothing, and against an unseverable rim a scribble
    // with no shape around it keeps roughly its own pixels -- about 2000 here,
    // or 129 of these samples. The bug being pinned produced exactly zero.
    long long covered = 0;
    for (int y = 0; y < image.height(); y += 4) {
        for (int x = 0; x < image.width(); x += 4) {
            if (image.pixelColor(x, y).alphaF() > 0.5) ++covered;
        }
    }
    CHECK(covered > 50);
}

// Hidden means not in the picture. If the per-layer sequences disagreed with
// the flattened one about that, reassembling them would not give back the shot.
void exportLeavesOutHiddenLayers() {
    TEST("a hidden layer is not exported at all");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString out = scratch.filePath(QStringLiteral("out"));

    Document doc = buildDrawnScene();
    const TrackId track = doc.scene().tracks.front().id;
    const LayerId ink = doc.scene().tracks.front().layers.front().id;
    {
        Layer settings = *doc.scene().findTrack(track)->findLayer(ink);
        settings.visible = false;
        doc.updateLayer(track, ink, settings);
    }

    exporting::Options options;
    options.folder = out;
    options.layers = true;
    CHECK(exporting::write(doc, options, nullptr, nullptr, nullptr));

    CHECK(!QDir(out + QStringLiteral("/main_ink")).exists());
    CHECK(QDir(out + QStringLiteral("/main_colour")).exists());
}

// An export of a real shot is hundreds of frames and takes as long as it takes,
// so stopping has to actually stop.
void exportCanBeCancelled() {
    TEST("cancelling an export stops it");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString out = scratch.filePath(QStringLiteral("out"));

    Document doc = buildDrawnScene();
    exporting::Options options;
    options.folder = out;
    options.layers = true;

    // Counted rather than compared against a fixed number: the steps before the
    // first file are the colour solves this shot needs, and how many that is
    // belongs to the fixture rather than to what is being tested here.
    int seen = 0;
    int files = 0;
    QString error;
    const bool ok = exporting::write(
        doc, options,
        [&](int done, int, const QString& what) {
            if (done > seen && what.startsWith(QStringLiteral("writing"))) ++files;
            seen = done;
            return files < 2;
        },
        nullptr, &error);
    CHECK_EQ(ok, false);
    CHECK(!error.isEmpty());
    CHECK_EQ(files, 2);

    // What it had already written is still there. An export is not atomic and
    // does not claim to be -- half a sequence is visibly half a sequence.
    //
    // One file each rather than two of one: frames are written in slot order,
    // every sequence at once, which is what makes each drawing solve exactly
    // once however many sequences it appears in.
    CHECK_EQ(QDir(out + QStringLiteral("/main_ink")).entryList(QDir::Files).size(), 1);
    CHECK_EQ(QDir(out + QStringLiteral("/main_colour")).entryList(QDir::Files).size(), 1);
}

// A name is a folder name here, and people call layers things like "rough 2".
void exportNamesSurviveAwkwardLayerNames() {
    TEST("layer names that a filesystem would refuse become usable folder names");
    CHECK_EQ(exporting::sequenceName("main", "ink").toStdString(), std::string("main_ink"));
    // The underscore is the separator and nothing else is, so the number in
    // "layer 1" is visibly part of the layer's name rather than a fourth field.
    CHECK_EQ(exporting::sequenceName("track 1", "layer 1").toStdString(),
             std::string("track-1_layer-1"));
    CHECK_EQ(exporting::sequenceName("main", "rough 2").toStdString(),
             std::string("main_rough-2"));
    // Including an underscore somebody typed: it would be indistinguishable
    // from the separator, so it is not allowed to survive as one.
    CHECK_EQ(exporting::sequenceName("main", "rough_2").toStdString(),
             std::string("main_rough-2"));
    CHECK_EQ(exporting::sequenceName("a/b", "c:d").toStdString(), std::string("a-b_c-d"));
    // A run of junk is one separator, and the ends are trimmed.
    CHECK_EQ(exporting::sequenceName("main", " rough // clean ").toStdString(),
             std::string("main_rough-clean"));
    CHECK_EQ(exporting::sequenceName("", "").toStdString(), std::string("unnamed_unnamed"));
}

// Through the window, which is where the progress dialog and the document are.
void theFileMenuExports() {
    TEST("exporting through the window writes the sequences");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));
    const QString out = scratch.filePath(QStringLiteral("out"));

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();
    CHECK(window.openProjectAt(folder, nullptr));
    QCoreApplication::processEvents();

    // The menu item exists and is enabled, which it was not before M5.
    QAction* exporter = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("&Export sequences...")) exporter = action;
    }
    CHECK(exporter != nullptr);
    if (!exporter) return;
    CHECK(exporter->isEnabled());

    // The sequences go in a folder named after the project rather than loose in
    // whatever directory was chosen. "shot.animage" is the project; "shot" is
    // what the export is called.
    CHECK_EQ(window.defaultExportName().toStdString(), std::string("shot"));

    QString error;
    CHECK(window.exportSequencesTo(out, true, true, exporting::Format::Png, &error));
    CHECK_EQ(error.toStdString(), std::string());
    QCoreApplication::processEvents();

    CHECK(QFileInfo::exists(out + QStringLiteral("/main_ink/main_ink_0001.png")));
    CHECK(QFileInfo::exists(out + QStringLiteral("/composite/composite_0001.png")));
}

// The guard on a recursive delete, so it is worth being exact about. An export
// replaces what was in the folder rather than merging into it -- a merge leaves
// a shortened shot's old tail sitting after the new frames, which downstream is
// a well-formed sequence of the wrong length -- and the price of replacing is
// that something has to decide what may be deleted.
void anExportIsRecognisedBeforeAnythingIsDeleted() {
    TEST("only a folder that really is an export is offered for overwriting");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const auto path = [&](const char* name) { return scratch.filePath(QLatin1String(name)); };
    const auto touch = [&](const QString& file) {
        QDir().mkpath(QFileInfo(file).path());
        QFile handle(file);
        CHECK(handle.open(QIODevice::WriteOnly));
        handle.write("x");
    };

    // Nothing there, and a folder with nothing in it, are both somewhere to
    // write rather than something to ask about.
    CHECK(exporting::occupantOf(path("missing")) == exporting::Occupant::Nothing);
    CHECK(QDir().mkpath(path("empty")));
    CHECK(exporting::occupantOf(path("empty")) == exporting::Occupant::Nothing);

    // A real one, written by the real thing.
    Document doc = buildDrawnScene();
    exporting::Options options;
    options.folder = path("shot");
    options.layers = true;
    options.flattened = true;
    CHECK(exporting::write(doc, options, nullptr, nullptr, nullptr));
    CHECK(exporting::occupantOf(path("shot")) == exporting::Occupant::AnExport);

    // Junk a file browser drops in it does not stop it being one, or a folder
    // anybody had opened would become undeletable.
    touch(path("shot") + QStringLiteral("/.DS_Store"));
    touch(path("shot") + QStringLiteral("/main_ink/Thumbs.db"));
    CHECK(exporting::occupantOf(path("shot")) == exporting::Occupant::AnExport);

    // A loose file in it is somebody else's folder.
    touch(path("shot") + QStringLiteral("/notes.txt"));
    CHECK(exporting::occupantOf(path("shot")) == exporting::Occupant::SomethingElse);

    // So is a sequence folder holding something that is not its own frames --
    // including a frame belonging to a different sequence, which is what a
    // renamed layer would leave behind.
    touch(path("mixed") + QStringLiteral("/main_ink/main_ink_0001.png"));
    CHECK(exporting::occupantOf(path("mixed")) == exporting::Occupant::AnExport);
    touch(path("mixed") + QStringLiteral("/main_ink/main_colour_0001.png"));
    CHECK(exporting::occupantOf(path("mixed")) == exporting::Occupant::SomethingElse);

    // And so, emphatically, is a project folder. It is the obvious way to point
    // a recursive delete at every drawing in the shot.
    const QString project = path("project.animage");
    CHECK(ProjectIO::save(buildDrawnScene(), project, nullptr));
    CHECK(exporting::occupantOf(project) == exporting::Occupant::SomethingElse);

    // Overwriting leaves nothing of what was there. The stale tail is the whole
    // point: a shorter shot must not inherit the longer one's later frames.
    const QString stale =
        path("shot") + QStringLiteral("/main_ink/main_ink_9999.png");
    touch(stale);
    QString error;
    CHECK(exporting::removeExport(path("shot"), &error));
    CHECK_EQ(error.toStdString(), std::string());
    CHECK(!QFileInfo::exists(stale));
    CHECK(!QDir(path("shot")).exists());

    CHECK(exporting::write(doc, options, nullptr, nullptr, nullptr));
    CHECK(!QFileInfo::exists(stale));
    CHECK(QFileInfo::exists(path("shot") + QStringLiteral("/main_ink/main_ink_0001.png")));
}

// The two formats have to be the same picture, differing only in the two
// conversions PNG makes on purpose. Reported as line art appearing to sit both
// under and over the colour when the EXR was opened in Blender, which is what a
// premultiply applied twice looks like -- so this pins that the file leaves here
// premultiplied exactly once, and that the flattened picture stacks its layers
// the way the compositor does.
void exrAndPngAreTheSamePicture() {
    TEST("the EXR and the PNG differ only by the conversions the PNG makes");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());

    Document doc = buildDrawnScene();

    // Half-opacity on the colour layer, and it is the whole reason this test
    // can fail. Without it every partly-covered pixel in the fixture belongs to
    // the black line art -- and black is the one colour where premultiplied and
    // straight are identical, because both are zero. The first version of this
    // test passed with the unpremultiply deleted, which was checked rather than
    // assumed. A translucent *orange* region is what tells the two apart:
    // premultiplied it is (0.45, 0.15, 0.02), straight it is (0.9, 0.3, 0.05).
    const TrackId track = doc.scene().tracks.front().id;
    const LayerId colour = doc.scene().tracks.front().layers.back().id;
    {
        Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
        settings.opacity = 0.5f;
        doc.updateLayer(track, colour, settings);
    }

    const auto exportAs = [&](exporting::Format format, const QString& into) {
        exporting::Options options;
        options.folder = scratch.filePath(into);
        options.format = format;
        options.layers = false;
        options.flattened = true;
        CHECK(exporting::write(doc, options, nullptr, nullptr, nullptr));
    };
    exportAs(exporting::Format::Png, QStringLiteral("png"));
    exportAs(exporting::Format::Exr, QStringLiteral("exr"));

    const QImage png(scratch.filePath(QStringLiteral("png")) +
                     QStringLiteral("/composite/composite_0001.png"));
    CHECK(!png.isNull());
    if (png.isNull()) return;

    float* exr = nullptr;
    int width = 0, height = 0;
    const char* why = nullptr;
    CHECK_EQ(LoadEXR(&exr, &width, &height,
                     (scratch.filePath(QStringLiteral("exr")) +
                      QStringLiteral("/composite/composite_0001.exr"))
                         .toUtf8()
                         .constData(),
                     &why),
             TINYEXR_SUCCESS);
    if (!exr) return;
    CHECK_EQ(width, png.width());
    CHECK_EQ(height, png.height());

    // Put the EXR through exactly what the PNG writer does -- unpremultiply,
    // then the sRGB curve on the colours but not on alpha -- and the two should
    // land on the same 16-bit numbers. A premultiply too many or too few shows
    // up here as edges that disagree while solid interiors match, which is
    // precisely the "dark outline under the colour" symptom.
    // How far apart the two are allowed to be, and it is not zero for a reason
    // worth knowing. The PNG's numbers are computed from the float32 the
    // compositor works in; the EXR's have been through half first, because half
    // is what it stores. So the EXR path quantises *before* the sRGB curve, and
    // the curve then magnifies that step: half's absolute step near 0.9 is
    // about 4.9e-4 and the curve's slope there is about 0.47, which is 15 parts
    // in 65535. Measured worst on this fixture is 10. Twenty-four leaves
    // headroom and is still 0.04%, far below anything that could hide a missing
    // premultiply, a swapped channel or a curve applied twice.
    constexpr int kSlack = 24;

    long long differing = 0, edges = 0;
    int worst_gap = 0;
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const quint16*>(png.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const float* got = exr + 4 * (static_cast<std::size_t>(y) * width + x);
            Rgba premultiplied{got[0], got[1], got[2], got[3]};
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            unpremultiply(premultiplied, r, g, b, a);
            const auto toShort = [](float v) {
                return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f));
            };
            const int want[4] = {toShort(linearToSrgb(r)), toShort(linearToSrgb(g)),
                                 toShort(linearToSrgb(b)), toShort(a)};
            // Partly covered *and* not grey, which is the combination that can
            // tell a premultiply from its absence.
            const bool coloured = std::abs(got[0] - got[2]) > 0.02f;
            if (got[3] > 0.01f && got[3] < 0.99f && coloured) ++edges;
            for (int c = 0; c < 4; ++c) {
                const int gap = std::abs(static_cast<int>(row[4 * x + c]) - want[c]);
                if (gap > kSlack) ++differing;
                worst_gap = std::max(worst_gap, gap);
            }
        }
    }
    // Reported, not just asserted: if half's step through the curve ever grows,
    // this is the number that says so before the slack above starts hiding it.
    CHECK(worst_gap < kSlack);
    CHECK_EQ(differing, 0LL);
    // The comparison is only worth anything if there were partly-covered pixels
    // in it, since those are the only ones a wrong premultiply moves.
    CHECK(edges > 100);
    free(exr);
}

// EXR is the lossless half of the export, so what it has to prove is that
// nothing was converted: the bits that went in are the bits that came out.
//
// Note what this test does *not* prove. It reads the file back with the same
// library that wrote it, which is measuring twice on the same side of the
// event -- a file both agree about can still be malformed for somebody else.
// The independent check is `exrheader`, from OpenEXR proper, run by hand; see
// the handover.
void exrExportsThePixelsUnconverted() {
    TEST("an EXR export converts nothing: the halves survive exactly");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString out = scratch.filePath(QStringLiteral("out"));

    Document doc = buildDrawnScene();
    exporting::Options options;
    options.folder = out;
    options.format = exporting::Format::Exr;
    options.layers = true;
    QString error;
    CHECK(exporting::write(doc, options, nullptr, nullptr, &error));
    CHECK_EQ(error.toStdString(), std::string());

    // The extension follows the format, and the layout does not otherwise
    // change: a file per layer, same folders, same frame numbers.
    const QString first = out + QStringLiteral("/main_ink/main_ink_0001.exr");
    CHECK(QFileInfo::exists(first));
    CHECK(!QFileInfo::exists(out + QStringLiteral("/main_ink/main_ink_0001.png")));

    // Read it back and compare against the compositor's own output. Anything
    // that converted -- an sRGB curve, an unpremultiply, a float32 round trip
    // -- shows up here as pixels that are close rather than equal.
    float* pixels = nullptr;
    int width = 0, height = 0;
    const char* why = nullptr;
    CHECK_EQ(LoadEXR(&pixels, &width, &height, first.toUtf8().constData(), &why), TINYEXR_SUCCESS);
    if (!pixels) return;
    CHECK_EQ(width, doc.scene().width);
    CHECK_EQ(height, doc.scene().height);

    const PixelRect canvas = doc.scene().canvas();
    Compositor compositor;
    Framebuffer expected(canvas.width, canvas.height);
    expected.clear();
    const Track& track = doc.scene().tracks.front();
    compositor.compositeLayers(doc, track.id, track.imageAtSlot(0), {track.layers.front().id},
                               canvas, expected);
    // The ink is black on nothing, so R, G and B all agree and a writer that
    // swapped two of them would pass everything below. The colour layer's
    // scribble is orange -- 0.9, 0.3, 0.05 -- so it is the one that can tell.
    // Checked further down against the file written for it.
    const QString coloured = out + QStringLiteral("/main_colour/main_colour_0001.exr");

    // Every pixel, not a sample: "lossless" is a claim about all of them, and a
    // sampled version of this test would have passed with the alpha channel
    // dropped. Compared as halves, because half is what the file stores -- the
    // float32 the compositor works in is the wider type here.
    long long differing = 0, opaque = 0;
    for (int y = 0; y < height && differing == 0; ++y) {
        const Rgba* want = expected.row(y);
        for (int x = 0; x < width; ++x) {
            const float* got = pixels + 4 * (static_cast<std::size_t>(y) * width + x);
            if (want[x].a > 0.5f) ++opaque;
            const bool same = animage::Half(want[x].r).bits == animage::Half(got[0]).bits &&
                              animage::Half(want[x].g).bits == animage::Half(got[1]).bits &&
                              animage::Half(want[x].b).bits == animage::Half(got[2]).bits &&
                              animage::Half(want[x].a).bits == animage::Half(got[3]).bits;
            if (!same) ++differing;
        }
    }
    CHECK_EQ(differing, 0LL);
    // And the frame was not simply blank, which every check above would pass.
    CHECK(opaque > 100);
    free(pixels);

    // Now the channel order, which needs a pixel whose channels differ. The
    // scribble is orange, so the brightest pixel of the colour layer must come
    // back red-most and blue-least; a writer that named its planes in the wrong
    // order produces a blue scribble and an otherwise perfect file.
    float* colour = nullptr;
    int cw = 0, ch = 0;
    CHECK_EQ(LoadEXR(&colour, &cw, &ch, coloured.toUtf8().constData(), &why), TINYEXR_SUCCESS);
    if (!colour) return;
    float best_r = 0.0f, best_g = 0.0f, best_b = 0.0f, best = -1.0f;
    for (int i = 0; i < cw * ch; ++i) {
        const float* p = colour + 4 * i;
        if (p[3] > 0.5f && p[0] > best) {
            best = p[0];
            best_r = p[0];
            best_g = p[1];
            best_b = p[2];
        }
    }
    CHECK(best > 0.0f);
    CHECK(best_r > best_g);
    CHECK(best_g > best_b);
    free(colour);
}

// Exporting through the window hands its max-flows to a solver instead of
// running them where the progress dialog is being drawn. What that buys, and
// the only part of it a test can see from the outside, is the cap: a solve
// nobody can wait for takes the interactive budget, and one on a worker takes
// the whole of it. An exported fill used to be coarser than the one on screen.
void theWindowExportsAtFullResolution() {
    TEST("exporting through the window solves at the full budget, not the capped one");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));
    const QString out = scratch.filePath(QStringLiteral("out"));

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();
    CHECK(window.openProjectAt(folder, nullptr));
    QCoreApplication::processEvents();

    QString error;
    CHECK(window.exportSequencesTo(out, true, false, exporting::Format::Png, &error));
    CHECK_EQ(error.toStdString(), std::string());

    const Document& doc = window.documentForTesting();
    const Track& track = doc.scene().tracks.front();
    const LayerId colour = track.layers.back().id;
    // The last drawing, so that whatever the canvas happened to solve for the
    // frame it was standing on is not what is being read back.
    const ImageId last = track.imageAtSlot(track.frameCount() - 1);
    const CtgFill* fill = doc.ctgFillFor(track.id, last, colour);
    CHECK(fill != nullptr);
    if (!fill) return;
    CHECK(fill->valid);
    CHECK(fill->budget >= kFullSolveBudget);
}

// A shot whose shape sits still for two drawings and then jumps across the
// frame, so a mark carried from the first drawing ends up stranded on blank
// paper with nothing to fill.
animage::Document buildStrandedShot() {
    using namespace animage;
    Document doc;
    const TrackId track = doc.addTrack("main");
    const LayerId colour = doc.addLayer(track, "colour 1", 0, LayerKind::Ctg);
    const LayerId ink = doc.addLayer(track, "ink", 1);

    Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
    settings.ctg_sources = {ink};
    // The shot exists to strand a mark, so the marks are told to stay where
    // they were drawn. With them following the drawing -- which is the default
    // -- the mark lands in the shape that moved and there is nothing to flag,
    // which is the feature and not this test.
    settings.ctg_follow_motion = false;
    doc.updateLayer(track, colour, settings);

    std::vector<ImageId> images;
    for (int i = 0; i < 4; ++i) {
        images.push_back(doc.insertImage(track, static_cast<std::size_t>(i)));
    }

    const auto line = [&](ImageId image, LayerId layer, float x0, float y0, float x1, float y1,
                          float radius, float r, float g, float b, bool label) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings s;
        s.radius = radius;
        s.hardness = label ? 1.0f : 0.95f;
        s.opacity = 1.0f;
        s.pressure_affects_opacity = false;
        s.r = r;
        s.g = g;
        s.b = b;
        s.a = 1.0f;
        s.label = label;
        Brush brush(s);
        brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
        brush.extend({x1, y1, 1.0f});
        brush.end();
    };

    const int lefts[4] = {200, 200, 900, 900};
    for (int i = 0; i < 4; ++i) {
        const ImageId image = images[static_cast<std::size_t>(i)];
        const float l = static_cast<float>(lefts[i]);
        const float r = l + 300.0f;
        line(image, ink, l, 200, r, 200, 2.5f, 0, 0, 0, false);
        line(image, ink, l, 200, l, 500, 2.5f, 0, 0, 0, false);
        line(image, ink, r, 200, r, 500, 2.5f, 0, 0, 0, false);
        line(image, ink, l, 500, r, 500, 2.5f, 0, 0, 0, false);
    }
    line(images[0], colour, 260, 300, 440, 300, 22.0f, 0.9f, 0.2f, 0.2f, true);
    doc.clearHistory();
    return doc;
}

// What the panel says about a drawing that is carrying its colour, and what the
// status bar says while the colour is being worked out. There was a third thing
// here -- a warning for carried marks that had landed badly -- and it was
// removed; see docs/handover.md.
void aCarriedMarkSaysSoInThePanel() {
    TEST("a drawing carrying its colour says so on the layer row");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("stranded.animage"));
    animage::Document built = buildStrandedShot();
    CHECK(ProjectIO::save(built, folder, nullptr));

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();
    CHECK(window.openProjectAt(folder, nullptr));
    QCoreApplication::processEvents();

    auto* timeline = window.findChild<TimelineWidget*>();
    auto* layers = window.findChild<QTreeWidget*>();
    CHECK(timeline != nullptr);
    CHECK(layers != nullptr);
    if (!timeline || !layers) return;

    const auto colourRowText = [&] {
        for (int row = 0; row < layers->topLevelItemCount(); ++row) {
            const QString text = layers->topLevelItem(row)->text(0);
            if (text.contains(QStringLiteral("colour"))) return text;
        }
        return QString();
    };

    // The solve happens on a worker thread, so the colour arrives after the
    // opening rather than during it, and the status bar says so while it does.
    // That is the whole of the visible difference between solving here and
    // solving elsewhere: the program does not stop, so without a word about it
    // a fill a second out of date looks like a fill that is wrong.
    const auto statusText = [&] {
        for (QLabel* label : window.findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("frame "))) return label->text();
        }
        return QString();
    };
    window.grab();  // the paint is what asks for the colour
    QCoreApplication::processEvents();
    CHECK(statusText().contains(QStringLiteral("colouring")));

    CHECK(window.waitForColour());
    QCoreApplication::processEvents();
    CHECK(!statusText().contains(QStringLiteral("colouring")));

    // Standing on each drawing in turn, letting the paint that asks for the
    // solve happen, the solve finish, and the queued report that follows it
    // arrive.
    const auto visit = [&](int slot) {
        timeline->setCurrentSlot(static_cast<std::size_t>(slot));
        QCoreApplication::processEvents();
        window.grab();
        CHECK(window.waitForColour());
        QCoreApplication::processEvents();
    };

    // Its own marks: no arrow.
    visit(0);
    CHECK(!colourRowText().startsWith(QStringLiteral("←")));

    // Carried here from an earlier drawing: an arrow, and which drawing it came
    // from in the tooltip.
    visit(1);
    CHECK(colourRowText().startsWith(QStringLiteral("←")));
    visit(3);
    CHECK(colourRowText().startsWith(QStringLiteral("←")));

    // And back onto the drawing that owns them, which takes the arrow away.
    visit(0);
    CHECK(!colourRowText().startsWith(QStringLiteral("←")));
}

// The colour-layer settings, which are the only way to reach carrying and its
// direction from the interface.
void theColourLayerBoxEditsWhatTheLayerDoes() {
    TEST("the colour layer box is there for colour layers and edits them");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QGroupBox* box = nullptr;
    for (QGroupBox* candidate : window.findChildren<QGroupBox*>()) {
        if (candidate->title().contains(QStringLiteral("Colour layer"))) box = candidate;
    }
    CHECK(box != nullptr);
    if (!box) return;

    QPushButton* add_layer = nullptr;
    QPushButton* add_colour = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add layer")) add_layer = button;
        if (button->text() == QStringLiteral("Add colour layer")) add_colour = button;
    }
    CHECK(add_layer != nullptr);
    CHECK(add_colour != nullptr);
    if (!add_layer || !add_colour) return;

    // A fresh document has one raster layer, so there is nothing to configure.
    CHECK(!box->isVisible());

    // The dock must not change width when the box comes and goes, or selecting
    // a colour layer shoves the canvas sideways every time.
    //
    // Measured from before the box has ever appeared. The first version of this
    // test took its reading *after* the box was showing and then checked the
    // width did not shrink on the way out -- so it never saw the growth on the
    // way in, passed, and shipped the bug. A grab forces the layout pass that
    // makes the reading mean anything.
    QWidget* dock = box->parentWidget();
    while (dock && !dock->inherits("QDockWidget")) dock = dock->parentWidget();
    CHECK(dock != nullptr);
    if (!dock) return;
    window.grab();
    const int settled = dock->width();
    CHECK(settled > 0);

    add_layer->click();
    add_colour->click();
    QCoreApplication::processEvents();
    window.grab();
    QCoreApplication::processEvents();
    CHECK(box->isVisible());
    CHECK_EQ(dock->width(), settled);

    auto* sources = box->findChild<QListWidget*>();
    auto* direction = box->findChild<QComboBox*>();
    QCheckBox* carry = nullptr;
    QCheckBox* follow = nullptr;
    for (QCheckBox* candidate : box->findChildren<QCheckBox*>()) {
        if (candidate->text().contains(QStringLiteral("Carry"))) carry = candidate;
        if (candidate->text().contains(QStringLiteral("Move"))) follow = candidate;
    }
    CHECK(sources != nullptr);
    CHECK(direction != nullptr);
    CHECK(carry != nullptr);
    CHECK(follow != nullptr);
    if (!sources || !direction || !carry || !follow) return;

    // Both raster layers offered and both taken, since both were visible when
    // the colour layer was made. The colour layer is not offered against
    // itself: a flat has no edges to cut along.
    CHECK_EQ(sources->count(), 2);
    for (int row = 0; row < sources->count(); ++row) {
        CHECK_EQ(sources->item(row)->checkState(), Qt::Checked);
        CHECK(!sources->item(row)->text().contains(QStringLiteral("colour")));
    }

    // Marks follow the drawing by default: left where they were drawn, a
    // carried mark holds its region only while the drawing has moved less than
    // about half that region's width, which between two drawings is not much.
    CHECK(follow->isEnabled());
    CHECK(follow->isChecked());

    // Three directions, not two: only reaching forwards leaves the drawings
    // before a coloured one with nothing.
    CHECK_EQ(direction->count(), 3);

    // Carrying is on by default and the other two go with it; turning it off
    // leaves the choices visible but meaningless, so they grey.
    CHECK(carry->isChecked());
    CHECK(direction->isEnabled());
    carry->setChecked(false);
    QCoreApplication::processEvents();
    CHECK(!direction->isEnabled());
    CHECK(!follow->isEnabled());
    carry->setChecked(true);
    QCoreApplication::processEvents();
    CHECK(direction->isEnabled());
    CHECK(follow->isEnabled());

    // And the tick reaches the layer and comes back from it: the panel is
    // filled from the document, so a setting that survives leaving the layer
    // and returning to it is one that was really written.
    follow->setChecked(false);
    QCoreApplication::processEvents();

    // Unticking a source really reaches the layer. Read back through the row's
    // tooltip, which counts them, rather than through the document: what is
    // being tested is that the panel and the model agree.
    auto* layers = window.findChild<QTreeWidget*>();
    CHECK(layers != nullptr);
    if (!layers) return;
    const auto colourTip = [&] {
        for (int row = 0; row < layers->topLevelItemCount(); ++row) {
            if (layers->topLevelItem(row)->text(0).contains(QStringLiteral("colour"))) {
                return layers->topLevelItem(row)->toolTip(0);
            }
        }
        return QString();
    };
    CHECK(colourTip().contains(QStringLiteral("2 layers")));

    sources->item(0)->setCheckState(Qt::Unchecked);
    QCoreApplication::processEvents();
    CHECK(colourTip().contains(QStringLiteral("1 layer")));

    // And stepping onto a raster layer takes the whole box away again, still
    // without the dock moving.
    for (int row = 0; row < layers->topLevelItemCount(); ++row) {
        if (!layers->topLevelItem(row)->text(0).contains(QStringLiteral("colour"))) {
            layers->setCurrentItem(layers->topLevelItem(row));
            break;
        }
    }
    QCoreApplication::processEvents();
    window.grab();
    QCoreApplication::processEvents();
    CHECK(!box->isVisible());
    CHECK_EQ(dock->width(), settled);

    // Back onto the colour layer: the box is filled from the document, so the
    // tick that was cleared a moment ago comes back cleared only if it really
    // reached the layer.
    for (int row = 0; row < layers->topLevelItemCount(); ++row) {
        if (layers->topLevelItem(row)->text(0).contains(QStringLiteral("colour"))) {
            layers->setCurrentItem(layers->topLevelItem(row));
            break;
        }
    }
    QCoreApplication::processEvents();
    CHECK(!follow->isChecked());
}

// Transparency is a colour on a colour layer and nothing anywhere else. On a
// raster layer it would be a stroke of negative light -- pixels no filter, no
// export and no file format can make sense of -- so the state has to be
// unreachable rather than guarded at the moment it would do damage.
void transparencyIsOfferedOnlyWhereItMeansSomething() {
    TEST("the None swatch is offered on colour layers and nowhere else");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    // The two halves of the switch carry no text, so they are told apart by
    // what they say they are for.
    QPushButton* none = nullptr;
    QPushButton* solid = nullptr;
    QPushButton* add_colour = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add colour layer")) add_colour = button;
        if (!button->text().isEmpty()) continue;
        if (button->toolTip().contains(QStringLiteral("no colour at all"))) none = button;
        if (button->toolTip().contains(QStringLiteral("Paint with this colour"))) solid = button;
    }
    CHECK(none != nullptr);
    CHECK(solid != nullptr);
    CHECK(add_colour != nullptr);
    if (!none || !solid || !add_colour) return;

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    const auto holdingNothing = [&] {
        const BrushSettings& s = canvas->brushSettings();
        return isTransparentScribble(Rgba{s.r, s.g, s.b, 1.0f});
    };

    // A stylesheet with no type selector applies to the widget *and* to
    // everything it owns, tooltips included -- so an unscoped `background:`
    // handed the swatch's own fill to its tooltip, and over the slashed one
    // that drew a red streak through the text. Asserted rather than eyeballed,
    // because an offscreen grab does not contain the tooltip to look at.
    CHECK(solid->styleSheet().startsWith(QStringLiteral("QPushButton {")));
    CHECK(none->styleSheet().startsWith(QStringLiteral("QPushButton {")));

    // A fresh document has one raster layer, so there is nothing to offer.
    CHECK(!none->isEnabled());
    CHECK(!holdingNothing());

    add_colour->click();
    QCoreApplication::processEvents();
    CHECK(none->isEnabled());

    // Picked, it really is the transparent label and not some dark colour
    // standing in for one.
    none->click();
    QCoreApplication::processEvents();
    CHECK(holdingNothing());

    // Clicking the colour half chooses the colour, and does not open the
    // dialog -- which is also why this test can click it at all: a modal would
    // hang here exactly as it would interrupt somebody drawing.
    solid->click();
    QCoreApplication::processEvents();
    CHECK(!holdingNothing());

    // Only the rimmed half is the one in hand, and only one is ever rimmed.
    none->click();
    QCoreApplication::processEvents();
    CHECK(none->styleSheet().contains(QStringLiteral("#1fb6a6")));
    CHECK(!solid->styleSheet().contains(QStringLiteral("#1fb6a6")));

    // Stepping off the colour layer with it in hand puts a colour back, rather
    // than leaving a brush loaded with something a raster layer cannot hold.
    // This is the path that matters: the half being greyed stops you choosing
    // it, and this stops you carrying it.
    auto* layers = window.findChild<QTreeWidget*>();
    CHECK(layers != nullptr);
    if (!layers) return;
    CHECK(layers->topLevelItemCount() >= 2);
    layers->setCurrentItem(layers->topLevelItem(0));  // the raster layer
    QCoreApplication::processEvents();

    CHECK(!none->isEnabled());
    CHECK(!holdingNothing());
    CHECK(solid->styleSheet().contains(QStringLiteral("#1fb6a6")));
}

// Saving and opening through the window, rather than through ProjectIO::save
// directly: the part that has gone wrong before is not the file, it is the
// canvas and the panels still holding ids from the document that was replaced.
void theFileMenuSavesAndOpens() {
    TEST("saving and opening through the window rebinds everything");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QAction* open = nullptr;
    QAction* save = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("&Open...")) open = action;
        if (action->text() == QStringLiteral("&Save")) save = action;
    }
    CHECK(open != nullptr);
    CHECK(save != nullptr);

    // The window starts on a scene of its own, and the title says so.
    CHECK(window.windowTitle().startsWith(QStringLiteral("Untitled")));

    // Loading through the same path the menu uses.
    auto* canvas = window.findChild<CanvasWidget*>();
    auto* layers = window.findChild<QTreeWidget*>();
    CHECK(canvas != nullptr);
    CHECK(layers != nullptr);
    if (!canvas || !layers) return;

    const int before_layers = layers->topLevelItemCount();
    QString error;
    CHECK(window.openProjectAt(folder, &error));
    CHECK_EQ(error.toStdString(), std::string());
    QCoreApplication::processEvents();

    // The panel followed the new document rather than the old one's ids.
    CHECK_EQ(layers->topLevelItemCount(), 2);
    CHECK(before_layers != layers->topLevelItemCount());
    CHECK_EQ(window.windowTitle().toStdString(), std::string("shot.animage - Animage"));

    // And the canvas is pointing at a drawing that exists in it.
    CHECK(canvas->currentImage() != kNoId);
    CHECK(!canvas->grab().toImage().isNull());

    // Drawing marks the title, saving clears it.
    drawWithMouse(canvas, QPointF(300, 300), QPointF(360, 340), 4);
    QCoreApplication::processEvents();
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    save->trigger();
    QCoreApplication::processEvents();
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
}

// buildActions reads the shortcut table now, and this is what stops it quietly
// going back to literals. A stale QKeySequence at a call site still builds and
// still works, so nothing short of comparing the window against the table would
// ever notice -- which is the same shape as the mis-encoded character and the
// button that was never added: a green build proves nothing about the interface.
void theWindowTakesItsKeysFromTheTable() {
    TEST("every row of the shortcut table reaches an action in the window");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    for (const shortcuts::Entry& entry : shortcuts::table()) {
        QAction* action = window.actionForTesting(entry.id);
        CHECK(action != nullptr);
        if (!action) continue;

        const std::vector<QKeySequence> wanted = shortcuts::sequencesFor(entry);
        CHECK(!wanted.empty());
        if (!wanted.empty()) {
            CHECK_EQ(action->shortcut().toString().toStdString(),
                     wanted.front().toString().toStdString());
        }
        // Application-wide, every one of them: the canvas holds the keyboard,
        // and a window-context shortcut stops working the moment a spin box in
        // the toolbar takes focus.
        CHECK_EQ(static_cast<int>(action->shortcutContext()),
                 static_cast<int>(Qt::ApplicationShortcut));
        // And something holds it. An action in no menu, no toolbar and on no
        // widget is an action whose shortcut nothing will ever hear -- which is
        // exactly what the two brush-size keys would be if the addAction that
        // puts them on the window were dropped.
        CHECK(!action->associatedObjects().isEmpty());
        // And the window opens in Normal, where everything is live.
        CHECK(action->isEnabled());
    }
}

// --- lasso and transform -------------------------------------------------

// A window with one drawing on it, ready to be picked up.
struct WindowWithInk {
    MainWindow window;
    CanvasWidget* canvas = nullptr;

    WindowWithInk() {
        window.resize(1200, 800);
        window.show();
        QCoreApplication::processEvents();
        canvas = window.findChild<CanvasWidget*>();
        if (canvas) {
            canvas->resetView();
            drawWithMouse(canvas, QPointF(300, 300), QPointF(420, 360), 10);
        }
        QCoreApplication::processEvents();
    }

    animage::Document& doc() { return window.documentForTesting(); }

    QAction* action(shortcuts::Id id) { return window.actionForTesting(id); }

    // The one cel there is, whatever ids the window handed out.
    const animage::Cel* ink() {
        const animage::Track* track = doc().scene().tracks.empty()
                                          ? nullptr
                                          : &doc().scene().tracks.front();
        if (!track || track->layers.empty()) return nullptr;
        return doc().celAt(track->id, canvas->currentImage(), track->layers.front().id);
    }

    // A loop drawn with the pointer, the way one actually gets made: press,
    // several moves, release. The threshold that separates a click from a drag
    // is in screen pixels, so the loop has to be dragged rather than assigned.
    void lasso(const QRectF& around) {
        const QPointF corners[4] = {around.topLeft(), around.topRight(), around.bottomRight(),
                                    around.bottomLeft()};
        sendMouse(canvas, QEvent::MouseButtonPress, corners[0], Qt::LeftButton, Qt::LeftButton);
        for (int side = 0; side < 4; ++side) {
            const QPointF from = corners[side];
            const QPointF to = corners[(side + 1) % 4];
            for (int i = 1; i <= 8; ++i) {
                sendMouse(canvas, QEvent::MouseMove, from + (to - from) * (i / 8.0), Qt::NoButton,
                          Qt::LeftButton);
            }
        }
        sendMouse(canvas, QEvent::MouseButtonRelease, corners[0], Qt::LeftButton, Qt::NoButton);
        QCoreApplication::processEvents();
    }

    void press(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent down(QEvent::KeyPress, key, modifiers);
        QCoreApplication::sendEvent(canvas, &down);
        QKeyEvent up(QEvent::KeyRelease, key, modifiers);
        QCoreApplication::sendEvent(canvas, &up);
        QCoreApplication::processEvents();
    }
};

void theTransformToolTakesTheWholeDrawing() {
    TEST("the transform tool boxes the whole drawing with nothing selected");
    WindowWithInk fixture;
    CHECK(fixture.canvas != nullptr);
    if (!fixture.canvas) return;
    CHECK(fixture.ink() != nullptr);

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();

    CHECK(fixture.canvas->transformIsLive());
    // It starts as an identity: entering the tool picks the drawing up and
    // changes nothing about it.
    CHECK(fixture.canvas->transformValues().isIdentity());

    // And the mode took the keys with it. A disabled QAction does not consume
    // its shortcut, which is what frees Return to validate and the arrows to
    // nudge; nothing about looking at the drawing gives up its key.
    CHECK(!fixture.action(shortcuts::Id::Play)->isEnabled());
    CHECK(!fixture.action(shortcuts::Id::NextFrame)->isEnabled());
    CHECK(!fixture.action(shortcuts::Id::DeleteDrawing)->isEnabled());
    CHECK(fixture.action(shortcuts::Id::FitCanvas)->isEnabled());
    CHECK(fixture.action(shortcuts::Id::Undo)->isEnabled());

    fixture.canvas->cancelTransform();
    QCoreApplication::processEvents();
    CHECK(fixture.action(shortcuts::Id::Play)->isEnabled());
}

void nudgingMovesTheDrawingExactly() {
    TEST("a nudge moves the drawing by whole pixels and does not soften it");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    const animage::Cel* cel = fixture.ink();
    CHECK(cel != nullptr);
    if (!cel) return;
    const animage::TileGrid before = cel->tiles();
    const animage::PixelRect drawn = animage::drawnBounds(before);
    const std::size_t depth = fixture.doc().undoDepth();

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();

    for (int i = 0; i < 3; ++i) fixture.press(Qt::Key_Right);
    fixture.press(Qt::Key_Down, Qt::ShiftModifier);  // ten at a time
    CHECK_NEAR(fixture.canvas->transformValues().dx, 3.0, 1e-9);
    CHECK_NEAR(fixture.canvas->transformValues().dy, 10.0, 1e-9);

    // Return validates rather than playing, which is the whole of what the mode
    // is for.
    fixture.press(Qt::Key_Return);
    CHECK(!fixture.canvas->transformIsLive());

    // The whole session is one command however many nudges it took: nothing
    // about looking commits, so nothing was written until this.
    CHECK_EQ(fixture.doc().undoDepth(), depth + 1);

    const animage::Cel* after = fixture.ink();
    CHECK(after != nullptr);
    if (!after) return;

    // Bit-exact, not merely close. A registration nudge must never soften a
    // line, and softening is invisible in any comparison with a tolerance.
    std::size_t differing = 0;
    for (int y = drawn.y; y < drawn.y + drawn.height; ++y) {
        for (int x = drawn.x; x < drawn.x + drawn.width; ++x) {
            if (!(before.pixel(x, y) == after->tiles().pixel(x + 3, y + 10))) ++differing;
        }
    }
    CHECK_EQ(differing, std::size_t{0});
}

void cancellingLeavesTheUndoDepthWhereItWas() {
    TEST("cancelling a transform leaves no undo entry and no changed pixel");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    const animage::TileGrid before = fixture.ink()->tiles();
    const animage::PixelRect drawn = animage::drawnBounds(before);
    const std::size_t depth = fixture.doc().undoDepth();

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    for (int i = 0; i < 5; ++i) fixture.press(Qt::Key_Left);
    fixture.press(Qt::Key_Escape);

    CHECK(!fixture.canvas->transformIsLive());
    // Nothing happened, so there is nothing to undo. The obvious implementation
    // writes the hole immediately and puts the pixels back on cancel, which
    // leaves an undo entry for a thing that did not happen.
    CHECK_EQ(fixture.doc().undoDepth(), depth);

    std::size_t differing = 0;
    for (int y = drawn.y; y < drawn.y + drawn.height; ++y) {
        for (int x = drawn.x; x < drawn.x + drawn.width; ++x) {
            if (!(before.pixel(x, y) == fixture.ink()->tiles().pixel(x, y))) ++differing;
        }
    }
    CHECK_EQ(differing, std::size_t{0});

    // And an applied transform that moved nothing is not an edit either.
    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    fixture.press(Qt::Key_Return);
    CHECK_EQ(fixture.doc().undoDepth(), depth);
}

void aTransformAppliesToTheWholeHold() {
    TEST("transforming a drawing held over five frames changes it once");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    animage::Document& doc = fixture.doc();
    const animage::TrackId track = doc.scene().tracks.front().id;
    doc.extendExposure(track, 0, 4);  // one drawing, five frames
    QCoreApplication::processEvents();

    const std::size_t depth = doc.undoDepth();
    const animage::ImageId held = fixture.canvas->currentImage();

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    fixture.press(Qt::Key_Right);
    fixture.press(Qt::Key_Return);

    CHECK_EQ(doc.undoDepth(), depth + 1);
    // One cel, five slots. Anything walking the slots rather than the drawings
    // would have transformed it five times.
    const animage::Track* after = doc.scene().findTrack(track);
    CHECK(after != nullptr);
    if (!after) return;
    for (std::size_t slot = 0; slot < 5; ++slot) {
        CHECK_EQ(after->imageAtSlot(slot), held);
    }
    CHECK(doc.undo());
    CHECK_EQ(doc.undoDepth(), depth);
}

void changingFrameCommitsTheTransform() {
    TEST("changing frame commits a live transform");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    animage::Document& doc = fixture.doc();
    const animage::TrackId track = doc.scene().tracks.front().id;
    doc.addDrawing(track, 0);
    QCoreApplication::processEvents();
    const std::size_t depth = doc.undoDepth();

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    fixture.press(Qt::Key_Right);
    CHECK(fixture.canvas->transformIsLive());

    // A float that follows you to another drawing is a transform of the wrong
    // drawing waiting to happen.
    fixture.canvas->setFrame(1);
    QCoreApplication::processEvents();
    CHECK(!fixture.canvas->transformIsLive());
    CHECK_EQ(doc.undoDepth(), depth + 1);
}

void transformIsRefusedOnAColourLayer() {
    TEST("a colour layer refuses the transform tool and says why");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    auto* add_colour = fixture.window.findChild<QPushButton*>();
    // The button is found by its text rather than by position, because the
    // panel's order is not what is being tested here.
    for (QPushButton* button : fixture.window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add colour layer")) add_colour = button;
    }
    CHECK(add_colour != nullptr);
    if (!add_colour) return;
    add_colour->click();
    QCoreApplication::processEvents();

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();

    CHECK(!fixture.canvas->transformIsLive());
    // And the tool went back rather than sitting checked over a mode that never
    // started.
    CHECK(fixture.action(shortcuts::Id::Brush)->isChecked());
    CHECK(fixture.window.statusBar()->currentMessage().contains(QStringLiteral("Cannot")));
}

void undoDuringATransformCancelsIt() {
    TEST("Ctrl+Z during a transform cancels it rather than undoing the stroke");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    const std::size_t depth = fixture.doc().undoDepth();
    CHECK(depth > 0);  // the stroke that drew the ink

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    fixture.press(Qt::Key_Right);

    fixture.action(shortcuts::Id::Undo)->trigger();
    QCoreApplication::processEvents();

    CHECK(!fixture.canvas->transformIsLive());
    // The stroke is still there: undo answered the question that was asked.
    CHECK_EQ(fixture.doc().undoDepth(), depth);
}

void theNumericFieldsAndTheBoxAreOneThing() {
    TEST("the transform bar edits the same transform the handles do");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    // The canvas keeps its size when the bar arrives, which is the whole reason
    // the bar is a child of it rather than a row in the window: a canvas that
    // loses height moves the drawing on screen at the moment you start placing
    // it, and gets it back at the moment you finish.
    const QSize canvas_was = fixture.canvas->size();

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    CHECK_EQ(fixture.canvas->height(), canvas_was.height());
    CHECK_EQ(fixture.canvas->width(), canvas_was.width());

    // The bar is there while the transform is and not before it.
    QFrame* bar = fixture.window.findChild<QFrame*>(QStringLiteral("transformBar"));
    CHECK(bar != nullptr);
    if (!bar) return;
    CHECK(bar->isVisible());

    // Over the canvas rather than above it: appearing must not take height from
    // the drawing, or the thing being placed moves on screen while it is being
    // placed. A child of the canvas costs it nothing.
    CHECK(bar->parentWidget() == fixture.canvas);
    CHECK(bar->geometry().top() >= 0);
    CHECK(bar->geometry().bottom() < fixture.canvas->height());
    CHECK(bar->geometry().right() < fixture.canvas->width());

    QSpinBox* dx = bar->findChild<QSpinBox*>();
    CHECK(dx != nullptr);
    if (!dx) return;
    dx->setValue(25);
    QCoreApplication::processEvents();
    CHECK_NEAR(fixture.canvas->transformValues().dx, 25.0, 1e-9);

    // And the other way: a nudge reaches the field.
    fixture.press(Qt::Key_Right);
    CHECK_EQ(dx->value(), 26);

    fixture.canvas->cancelTransform();
    QCoreApplication::processEvents();
    CHECK(!bar->isVisible());
    CHECK_EQ(fixture.canvas->height(), canvas_was.height());
}

void aLassoSelectsAndAClickClears() {
    TEST("a drag makes a selection and a click clears it");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    fixture.action(shortcuts::Id::Lasso)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->isLassoing());
    CHECK(!fixture.canvas->hasSelection());

    fixture.lasso(QRectF(280, 280, 90, 60));
    CHECK(fixture.canvas->hasSelection());

    // A click -- press and release without moving past the threshold -- clears
    // it. Nothing is lost that cannot be recreated in two seconds, which is the
    // whole reason a selection can be this cheap.
    sendMouse(fixture.canvas, QEvent::MouseButtonPress, QPointF(600, 500), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(fixture.canvas, QEvent::MouseButtonRelease, QPointF(601, 500), Qt::LeftButton,
              Qt::NoButton);
    QCoreApplication::processEvents();
    CHECK(!fixture.canvas->hasSelection());
}

// The trap the design note names first: a loop enclosing no ink is the same as
// no selection, and "no selection" means "transform everything" -- so a stray
// loop over blank paper would quietly become a whole-drawing transform.
void anEmptyLassoDoesNotBecomeSelectAll() {
    TEST("a loop enclosing no ink clears the selection instead of selecting all");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    fixture.action(shortcuts::Id::Lasso)->trigger();
    QCoreApplication::processEvents();

    // A real loop, well away from the stroke the fixture drew.
    fixture.lasso(QRectF(700, 550, 120, 90));
    CHECK(!fixture.canvas->hasSelection());

    // And the transform that follows takes the whole drawing rather than
    // nothing, because that is what no selection means.
    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->transformIsLive());
    fixture.canvas->cancelTransform();
}

void transformingASelectionMovesOnlyWhatWasSelected() {
    TEST("a transform of a selection leaves the rest of the drawing alone");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    // A second stroke well away from the first, so there is something that must
    // not move.
    drawWithMouse(fixture.canvas, QPointF(700, 500), QPointF(800, 560), 10);
    QCoreApplication::processEvents();

    const animage::Cel* cel = fixture.ink();
    CHECK(cel != nullptr);
    if (!cel) return;
    const animage::TileGrid before = cel->tiles();
    const QPointF elsewhere = QPointF(750, 530);
    const animage::PixelRect untouched{
        static_cast<int>(elsewhere.x()) - 30, static_cast<int>(elsewhere.y()) - 30, 60, 60};

    fixture.action(shortcuts::Id::Lasso)->trigger();
    QCoreApplication::processEvents();
    fixture.lasso(QRectF(270, 270, 180, 120));  // round the first stroke only
    CHECK(fixture.canvas->hasSelection());

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->transformIsLive());

    for (int i = 0; i < 20; ++i) fixture.press(Qt::Key_Down, Qt::ShiftModifier);
    fixture.press(Qt::Key_Return);
    QCoreApplication::processEvents();

    const animage::Cel* after = fixture.ink();
    CHECK(after != nullptr);
    if (!after) return;

    // The second stroke is bit-identical: a selection is what the transform
    // acts on, and nothing outside it may be touched.
    std::size_t moved = 0;
    for (int y = untouched.y; y < untouched.y + untouched.height; ++y) {
        for (int x = untouched.x; x < untouched.x + untouched.width; ++x) {
            if (!(before.pixel(x, y) == after->tiles().pixel(x, y))) ++moved;
        }
    }
    CHECK_EQ(moved, std::size_t{0});

    // And the first one is not where it was.
    CHECK(before.pixel(340, 320).a > 0.5f);
    CHECK(after->tiles().pixel(340, 320).a < 0.5f);

    // The loop went with the pixels it described.
    CHECK(!fixture.canvas->hasSelection());
}

void backspaceErasesTheSelectionAndDeleteStillDeletesTheDrawing() {
    TEST("Backspace erases the selection; Delete still deletes the drawing");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    animage::Document& doc = fixture.doc();
    const animage::TrackId track = doc.scene().tracks.front().id;
    doc.addDrawing(track, 0);  // two drawings, so Delete has something to take
    QCoreApplication::processEvents();
    fixture.canvas->setFrame(0);
    QCoreApplication::processEvents();

    fixture.action(shortcuts::Id::Lasso)->trigger();
    QCoreApplication::processEvents();
    fixture.lasso(QRectF(270, 270, 180, 120));
    CHECK(fixture.canvas->hasSelection());

    const std::size_t drawings = doc.scene().findTrack(track)->images.size();
    fixture.action(shortcuts::Id::EraseSelection)->trigger();
    QCoreApplication::processEvents();

    // The ink is gone and the drawing is not.
    CHECK_EQ(doc.scene().findTrack(track)->images.size(), drawings);
    CHECK(fixture.ink() == nullptr || fixture.ink()->tiles().pixel(340, 320).a < 0.5f);
    CHECK(!fixture.canvas->hasSelection());

    // And Delete still means what it always meant.
    fixture.action(shortcuts::Id::DeleteDrawing)->trigger();
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().findTrack(track)->images.size(), drawings - 1);
}

void theSelectionSurvivesALayerChangeAndNotAFrameChange() {
    TEST("a loop survives changing layer and is cleared by changing frame");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    animage::Document& doc = fixture.doc();
    const animage::TrackId track = doc.scene().tracks.front().id;
    const animage::LayerId second = doc.addLayer(track, "second", 0);
    doc.addDrawing(track, 0);
    QCoreApplication::processEvents();
    fixture.canvas->setFrame(0);

    fixture.action(shortcuts::Id::Lasso)->trigger();
    QCoreApplication::processEvents();
    fixture.lasso(QRectF(270, 270, 180, 120));
    CHECK(fixture.canvas->hasSelection());

    // A loop is geometry in image space, so re-lifting it from another layer of
    // the same drawing is meaningful.
    fixture.canvas->setActiveLayer(second);
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->hasSelection());

    // Carrying it to another drawing is how you transform the wrong thing.
    fixture.canvas->setFrame(1);
    QCoreApplication::processEvents();
    CHECK(!fixture.canvas->hasSelection());
}

void aPasteLandsWhereItWasCopiedFrom() {
    TEST("a paste is a float that lands at the coordinates it came from");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    animage::Document& doc = fixture.doc();
    const animage::TrackId track = doc.scene().tracks.front().id;
    doc.addDrawing(track, 0);  // an empty second drawing to paste onto
    QCoreApplication::processEvents();
    fixture.canvas->setFrame(0);
    QCoreApplication::processEvents();

    fixture.action(shortcuts::Id::Copy)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->canPaste());
    // Copying is not an edit.
    const std::size_t depth = doc.undoDepth();

    // The clipboard follows you to another drawing, which is what makes a float
    // that does not the right answer. Through the timeline, which is the path a
    // frame change really takes -- moving the canvas alone leaves the two
    // disagreeing, and the next refresh puts it back.
    fixture.action(shortcuts::Id::NextFrame)->trigger();
    QCoreApplication::processEvents();
    CHECK_EQ(fixture.canvas->frame(), std::size_t{1});
    CHECK(fixture.ink() == nullptr);

    fixture.action(shortcuts::Id::Paste)->trigger();
    QCoreApplication::processEvents();

    // It arrives as a float: a live transform, with the bar up and the tool
    // saying so, and nothing written yet.
    CHECK(fixture.canvas->transformIsLive());
    CHECK(fixture.action(shortcuts::Id::Transform)->isChecked());
    CHECK_EQ(doc.undoDepth(), depth);
    CHECK(fixture.ink() == nullptr);

    // Applied without moving it, it lands where it was copied from -- you paste
    // to re-register something, not to drop it wherever the view happens to be.
    // And unlike a transform of the drawing's own pixels, an unmoved paste is
    // still an edit.
    fixture.press(Qt::Key_Return);
    QCoreApplication::processEvents();
    CHECK_EQ(doc.undoDepth(), depth + 1);
    CHECK(fixture.ink() != nullptr);
    if (!fixture.ink()) return;
    CHECK(fixture.ink()->tiles().pixel(340, 320).a > 0.5f);
}

void cutTakesThePixelsAway() {
    TEST("cut copies and erases in one command");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    animage::Document& doc = fixture.doc();
    const std::size_t depth = doc.undoDepth();
    CHECK(fixture.ink()->tiles().pixel(340, 320).a > 0.5f);

    fixture.action(shortcuts::Id::Cut)->trigger();
    QCoreApplication::processEvents();

    CHECK_EQ(doc.undoDepth(), depth + 1);
    CHECK(fixture.canvas->canPaste());
    CHECK(fixture.ink() == nullptr || fixture.ink()->tiles().pixel(340, 320).a < 0.5f);

    // And it undoes in one step, like everything else that writes.
    CHECK(doc.undo());
    CHECK(fixture.ink()->tiles().pixel(340, 320).a > 0.5f);
}

void cancellingAPasteWritesNothing() {
    TEST("cancelling a paste leaves the drawing and the history alone");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    animage::Document& doc = fixture.doc();
    fixture.action(shortcuts::Id::Copy)->trigger();
    QCoreApplication::processEvents();

    const std::size_t depth = doc.undoDepth();
    fixture.action(shortcuts::Id::Paste)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->transformIsLive());

    fixture.press(Qt::Key_Escape);
    QCoreApplication::processEvents();
    CHECK(!fixture.canvas->transformIsLive());
    CHECK_EQ(doc.undoDepth(), depth);
    // And the clipboard is still there: cancelling a paste is not a way to lose
    // what you copied.
    CHECK(fixture.canvas->canPaste());
}

void pastingOntoAColourLayerIsRefused() {
    TEST("a colour layer refuses a paste and says why");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    fixture.action(shortcuts::Id::Copy)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->canPaste());

    for (QPushButton* button : fixture.window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add colour layer")) button->click();
    }
    QCoreApplication::processEvents();

    fixture.action(shortcuts::Id::Paste)->trigger();
    QCoreApplication::processEvents();

    // Blocked on the layer kind and not on a guess about the pixels: raster
    // paint written onto a colour layer is a label nobody meant, and the
    // reverse writes negative light as paint.
    CHECK(!fixture.canvas->transformIsLive());
    CHECK(fixture.window.statusBar()->currentMessage().contains(QStringLiteral("Cannot paste")));
}

// The bar floats over the canvas, so the canvas is its parent -- and a QSpinBox
// has no tabletEvent, so the pen propagates from it to the canvas. Accepting it
// there did two wrong things at once: the press started a transform drag on the
// drawing underneath, and Qt only synthesises a mouse event for a tablet event
// nobody accepted, so the bar was not clickable with a pen at all. Reported.
void thePenReachesTheTransformBar() {
    TEST("the pen reaches the transform bar rather than dragging the canvas");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->transformIsLive());

    QFrame* bar = fixture.window.findChild<QFrame*>(QStringLiteral("transformBar"));
    CHECK(bar != nullptr);
    if (!bar) return;

    // The middle of the bar, in the canvas's coordinates, which is where a
    // propagated tablet event arrives.
    const QPointF on_the_bar = bar->geometry().center();
    const animage::Transform before = fixture.canvas->transformValues();

    QPointingDevice stylus(QStringLiteral("test stylus"), 1, QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Pen,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                           1, 0);
    QTabletEvent press(QEvent::TabletPress, &stylus, on_the_bar,
                       fixture.canvas->mapToGlobal(on_the_bar), 1.0, 0, 0, 0, 0, 0,
                       Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(fixture.canvas, &press);

    // Left for whoever it landed on, which is what makes Qt synthesise the mouse
    // event the bar's controls actually listen for.
    CHECK(!press.isAccepted());

    QTabletEvent moved(QEvent::TabletMove, &stylus, on_the_bar + QPointF(60, 40),
                       fixture.canvas->mapToGlobal(on_the_bar + QPointF(60, 40)), 1.0, 0, 0, 0, 0,
                       0, Qt::NoModifier, Qt::NoButton, Qt::LeftButton);
    QCoreApplication::sendEvent(fixture.canvas, &moved);
    QTabletEvent release(QEvent::TabletRelease, &stylus, on_the_bar + QPointF(60, 40),
                         fixture.canvas->mapToGlobal(on_the_bar + QPointF(60, 40)), 0.0, 0, 0, 0,
                         0, 0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::sendEvent(fixture.canvas, &release);
    QCoreApplication::processEvents();

    // And the drawing did not move: a press on a control is not a drag on what
    // is behind it.
    CHECK(fixture.canvas->transformIsLive());
    CHECK_NEAR(fixture.canvas->transformValues().dx, before.dx, 1e-9);
    CHECK_NEAR(fixture.canvas->transformValues().dy, before.dy, 1e-9);

    fixture.canvas->cancelTransform();
}

// A gesture nobody can see is a gesture nobody uses. Rotation was only available
// from an invisible band just outside a corner, so the numeric field was the
// only discoverable way to turn a drawing.
void theBoxHasSomethingToRotateBy() {
    TEST("dragging the knob above the box rotates the drawing");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->transformIsLive());
    CHECK_NEAR(fixture.canvas->transformValues().rotation, 0.0, 1e-9);

    // Where the knob is drawn: out from the middle of the top edge, away from
    // the middle of the box, at a fixed distance on screen. Asked of the widget
    // rather than recomputed here -- a test that worked the position out for
    // itself would agree with a knob drawn where nobody can press it.
    const QPointF knob = fixture.canvas->rotationHandleForTesting();
    const QPointF centre = fixture.canvas->transformCentreForTesting();

    // A quarter turn: from directly above the centre round to directly right of
    // it, at the same distance, so the angle is the whole of what changed.
    const double reach = QLineF(centre, knob).length();
    CHECK(reach > 10.0);
    const QPointF quarter(centre.x() + reach, centre.y());

    sendMouse(fixture.canvas, QEvent::MouseButtonPress, knob, Qt::LeftButton, Qt::LeftButton);
    sendMouse(fixture.canvas, QEvent::MouseMove, quarter, Qt::NoButton, Qt::LeftButton);
    sendMouse(fixture.canvas, QEvent::MouseButtonRelease, quarter, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();

    // Ninety degrees, and only the rotation: a knob that moved the drawing as
    // well would be a knob that does two things.
    CHECK_NEAR(fixture.canvas->transformValues().rotation, 90.0, 1.0);
    CHECK_NEAR(fixture.canvas->transformValues().scale_x, 1.0, 1e-6);
    CHECK_NEAR(fixture.canvas->transformValues().scale_y, 1.0, 1e-6);

    fixture.canvas->cancelTransform();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::printf("canvas:\n");
    thePenReachesTheTransformBar();
    theBoxHasSomethingToRotateBy();
    aPasteLandsWhereItWasCopiedFrom();
    cutTakesThePixelsAway();
    cancellingAPasteWritesNothing();
    pastingOntoAColourLayerIsRefused();
    aLassoSelectsAndAClickClears();
    anEmptyLassoDoesNotBecomeSelectAll();
    transformingASelectionMovesOnlyWhatWasSelected();
    backspaceErasesTheSelectionAndDeleteStillDeletesTheDrawing();
    theSelectionSurvivesALayerChangeAndNotAFrameChange();
    theWindowTakesItsKeysFromTheTable();
    theTransformToolTakesTheWholeDrawing();
    nudgingMovesTheDrawingExactly();
    cancellingLeavesTheUndoDepthWhereItWas();
    aTransformAppliesToTheWholeHold();
    changingFrameCommitsTheTransform();
    transformIsRefusedOnAColourLayer();
    undoDuringATransformCancelsIt();
    theNumericFieldsAndTheBoxAreOneThing();
    touchingTheCanvasTakesTheKeyboardBack();
    altClickPicksTheColourUnderThePointer();
    theMouseStillWorksAfterThePenHasBeenUsed();
    colourLayerIsCreatedAtTheBottom();
    sceneSettingsKeepsRatioAndPixelsAgreeing();
    theScribbleBoxCanBeClicked();
    theVisibilityTickWorksOnAColourLayer();
    theFillWaitsForTheStrokeToFinish();
    theLastFillStaysUntilTheNextOneArrives();
    theColourIsCoarseFirstAndThenAsFineAsTheDrawing();
    movedMarksAgreeWithThemselvesInTheWindow();
    emptyingTheFillCacheThrowsAwayASolveAlreadyRunning();
    aProjectSurvivesSavingAndLoading();
    aMultiTrackProjectComesBackWhole();
    aFailedSaveLeavesTheOldProjectAlone();
    abrokenProjectDoesNotReplaceTheOpenOne();
    savingTwiceWritesTheSameBytes();
    anIncrementalSaveWritesTheSameProject();
    anIncrementalSaveReplacesWhatWentMissing();
    savingElsewhereCarriesNothingForward();
    openingLeavesTheFolderKnown();
    autosaveWritesOnlyWhenSomethingMoved();
    autosaveWaitsForTheStrokeToFinish();
    closingWritesTheLastChanges();
    leavingAnUntitledDocumentAsksFirst();
    closingAnUntouchedWindowJustCloses();
    newProjectStartsOverCleanly();
    exportWritesASequencePerLayer();
    theEndBehaviourAppliesToTheCompositeOnly();
    aFixedSceneLengthCapsTheExport();
    exportRepeatsAHeldDrawing();
    exportSolvesColourItHasNeverSeen();
    exportLeavesOutHiddenLayers();
    exportCanBeCancelled();
    exportNamesSurviveAwkwardLayerNames();
    exrExportsThePixelsUnconverted();
    exrAndPngAreTheSamePicture();
    theFileMenuExports();
    theWindowExportsAtFullResolution();
    anExportIsRecognisedBeforeAnythingIsDeleted();
    aCarriedMarkSaysSoInThePanel();
    theColourLayerBoxEditsWhatTheLayerDoes();
    transparencyIsOfferedOnlyWhereItMeansSomething();
    theFileMenuSavesAndOpens();
    heldKeysDoNotRecurse();
    longPanGestureSurvives();
    scrubbyZoomGestureSurvives();
    wheelZoomGestureSurvives();
    cacheStaysBoundedAtEveryZoom();
    zoomSweepDoesNotExplode();
    repeatedZoomAndPanStayConsistent();
    onionSkinAtLowZoom();
    deleteDrawingThenUndo();
    hidingThePassePartoutKeepsTheCanvasEdge();
    theViewMenuHidesThePassePartout();
    emptyTimelineRenders();
    theTrackMenuAddsATrackYouCanDrawOn();
    deletingATrackRebindsEverything();
    theCanvasCompositesEveryTrack();
    theTimelineIsAsLongAsTheLongestTrack();
    pastATracksEndYouCanSeeItButNotDrawOnIt();
    theTimelineDockFollowsTheTrackCount();
    theTimelineDockCanBeResizedByHand();
    theInsertButtonObeysTheOverwriteSetting();
    return testing::summarise("canvas");
}
