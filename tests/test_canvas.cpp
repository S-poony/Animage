// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives the canvas widget offscreen. These are the paths that only a human
// clicking around used to reach, which meant their crashes were found by a
// human clicking around.

#include <QAbstractSpinBox>
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QScrollArea>
#include <QScrollBar>
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
#include <functional>
#include <map>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QFile>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QHeaderView>
#include <QMenu>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QDialogButtonBox>
#include <QKeySequenceEdit>

#include "brush.h"
#include "canvas_widget.h"
#include "layer_list.h"
#include "name_limits.h"
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
#include "shortcuts_dialog.h"
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

// The pan slider goes under the track row, not over it.
//
// Issue #26, and the cause is one line of Qt: `QScrollArea::sizeHint` adds the
// horizontal scrollbar's height only when the policy is `ScrollBarAlwaysOn`, and
// the timeline's is `ScrollBarAsNeeded`. So the dock is sized as though the pan
// slider does not exist, and when a shot grows wider than the window the slider
// appears and takes its height out of the viewport -- ten pixels of the bottom of
// the row, on the desk this was measured on.
//
// **Asserted once, with the slider showing, as "does the row still fit".** Not
// as a before-and-after: what the dock was before the shot got long is a reading
// taken on the far side of the thing that goes wrong, and two readings the same
// side of an event agree with each other perfectly and say nothing. That mistake
// shipped a dock fix twice; see the handover.
//
// In viewport pixels and not in dock pixels, because how much taller the dock
// has to be is the style's business -- a scrollbar is 14 px here and something
// else on another theme -- while "the strip is inside the space it is shown in"
// is the same sentence everywhere.
void aLongShotsPanSliderLeavesTheTrackRowWhole() {
    TEST("a shot too long to fit leaves the track row whole under its pan slider");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(timeline != nullptr);
    if (!timeline) return;
    QScrollArea* scroll = nullptr;
    for (QWidget* w = timeline; w; w = w->parentWidget()) {
        if (auto* area = qobject_cast<QScrollArea*>(w)) { scroll = area; break; }
    }
    CHECK(scroll != nullptr);
    if (!scroll) return;

    // Nothing to see until the strip is wider than what it is shown in: the
    // slider is what the fault arrives with, so a short shot tests nothing. The
    // count is worked out rather than written down, so this still bites on a
    // window or a cell of another size.
    Document& doc = window.documentForTesting();
    const TrackId track = timeline->track();
    while (timeline->minimumWidth() <= scroll->viewport()->width()) {
        doc.insertImage(track, doc.scene().timelineFrames());
        timeline->refresh();
        QCoreApplication::processEvents();
    }
    CHECK(scroll->horizontalScrollBar()->isVisible());

    // The whole of it: every row the strip draws is inside the viewport, with
    // the slider taking its height from somewhere else.
    CHECK(scroll->viewport()->height() >= timeline->minimumHeight());
    // And no vertical scrollbar, which is the same shortfall seen from the side:
    // a strip that does not fit its viewport asks for one, and a timeline of one
    // track scrolling up and down is nonsense.
    CHECK(!scroll->verticalScrollBar()->isVisible());
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

// A held key is remembered by the canvas and cleared by its release, so any
// route that delivers the press and drops the release leaves the pen panning or
// zooming for ever instead of drawing. Alt is not at risk -- it is a modifier,
// so every pointer event carries it and re-derives the flag -- but Space and Z
// appear in no event's modifiers() and nothing re-derives them.
void aHeldKeyDoesNotSurviveItsRelease() {
    TEST("Space and Z do not stay held when their release goes astray");
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    // The timeline and not just any child: the keys have to travel through the
    // window's filter to reach the canvas, and an event sent to the canvas
    // itself is handled directly and never tests the filter at all. The timeline
    // takes click focus, so this is the everyday way the keyboard ends up off
    // the canvas while the pointer is still over it.
    auto* elsewhere = window.findChild<TimelineWidget*>();
    CHECK(elsewhere != nullptr);
    if (!elsewhere) return;
    elsewhere->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    CHECK(!canvas->hasFocus());

    // What a press would do. A held key is answered before anything under the
    // pointer, so this reads the flag without the pointer having to move.
    const auto pointing = [&] { return canvas->pointing(); };

    // Route one: the release carries a modifier the press did not. Alt+Tab is
    // Alt going down, so the Space release on the way back arrives with Alt on
    // it -- and the filter's modifier guard, which exists for Ctrl+Z, dropped it
    // while its press had already gone through. Reachable without leaving the
    // application at all: hold Space, then press Alt, then let Space go.
    QKeyEvent space_down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(elsewhere, &space_down);
    QCoreApplication::processEvents();
    CHECK(pointing() == CanvasWidget::Pointing::PanReady);

    QKeyEvent space_up_with_alt(QEvent::KeyRelease, Qt::Key_Space, Qt::AltModifier);
    QCoreApplication::sendEvent(elsewhere, &space_up_with_alt);
    QCoreApplication::processEvents();
    const bool space_let_go_through_the_filter = pointing() != CanvasWidget::Pointing::PanReady;
    CHECK(space_let_go_through_the_filter);

    // The same for Z, whose guard is the one Ctrl+Z is actually about.
    QKeyEvent zoom_down(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier);
    QCoreApplication::sendEvent(elsewhere, &zoom_down);
    QCoreApplication::processEvents();
    CHECK(pointing() == CanvasWidget::Pointing::Zoom);

    QKeyEvent zoom_up_with_ctrl(QEvent::KeyRelease, Qt::Key_Z, Qt::ControlModifier);
    QCoreApplication::sendEvent(elsewhere, &zoom_up_with_ctrl);
    QCoreApplication::processEvents();
    CHECK(pointing() != CanvasWidget::Pointing::Zoom);

    // Route two: the window stops being the one the release will reach at all.
    // Nothing re-derives the flag, so the canvas has to let go of it itself.
    QKeyEvent held_again(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(elsewhere, &held_again);
    QCoreApplication::processEvents();
    CHECK(pointing() == CanvasWidget::Pointing::PanReady);

    canvas->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    canvas->clearFocus();
    QCoreApplication::processEvents();
    const bool space_let_go_on_focus_loss = pointing() != CanvasWidget::Pointing::PanReady;
    CHECK(space_let_go_on_focus_loss);
}

// The other half of that forwarding, and it was wrong from the day it was
// written: a space is a character wherever one is being typed, and the filter
// was taking every one of them to pan with. Nothing noticed while the only
// places to type were dialogs nobody put a space in; renaming a layer or a
// track in place is typing into the main window, and "rough pass" came out as
// "roughpass".
void spaceReachesWhateverIsBeingTypedInto() {
    TEST("Space is a character in a field and pan everywhere else");
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();

    auto* field = new QLineEdit(&window);
    field->show();
    field->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    CHECK(QApplication::focusWidget() == field);
    if (QApplication::focusWidget() != field) return;

    // Sent to the field the way the platform sends it, and the filter has to
    // leave it there rather than handing it to the canvas.
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    QCoreApplication::sendEvent(field, &space);
    QCoreApplication::processEvents();
    CHECK_EQ(field->text().toStdString(), std::string(" "));

    // And with the keyboard back on the canvas it is pan again, which is the
    // half that must not be lost to the fix.
    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    canvas->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    auto* panel = window.findChild<QTreeWidget*>();
    CHECK(panel != nullptr);
    if (!panel) return;
    QKeyEvent elsewhere(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(panel, &elsewhere);
    QCoreApplication::processEvents();
    // Held Space is an open hand: the canvas says what a press would do now,
    // which is the one thing visible from outside that the key reached it.
    CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);
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

// A tap: press and release, at a stated moment.
//
// The moment is the point of it. What a pen produces is two ordinary presses
// with nothing marking them as a pair, so the only thing separating a double tap
// from two clicks is how far apart in time they are -- and a QMouseEvent built by
// hand carries timestamp 0, which would make every press in a test the second of
// a double tap at the same place.
void sendTap(QWidget* widget, const QPointF& at, quint64 when) {
    QMouseEvent press(QEvent::MouseButtonPress, at, widget->mapToGlobal(at), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    press.setTimestamp(when);
    QCoreApplication::sendEvent(widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, widget->mapToGlobal(at), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    release.setTimestamp(when + 10);
    QCoreApplication::sendEvent(widget, &release);
    QCoreApplication::processEvents();
}

// An item view releases its editor with deleteLater, so a closed one is still a
// child of the viewport until the deferred deletes run -- and findChild would
// hand back the dead one instead of the editor that is actually open. That is
// not a hypothetical: an earlier version of the rename test typed into a closed
// editor and passed, because a rename that goes nowhere leaves the name alone
// exactly as a refused rename does.
void settleEditors() {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
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


// --- issue #42: the swap at the end of a save ------------------------------
//
// The old project is moved aside, the new one is moved into place, and only
// then is the old one deleted. What #42 is about is the middle rename failing,
// which on Windows it can: the first rename moved `folder` away, so the second
// fails if anything recreated or locked the path in the instant between them --
// a cloud-sync client watching the folder, an antivirus handle. The restore
// then fails for exactly the same reason.
//
// There is no way to arrange that instant from outside, so `swapIntoPlace`
// takes the rename as an argument and these tests hand it one that refuses.
// Everything else is the real filesystem: the folders are real, the projects in
// them are real, and what is checked afterwards is what is actually on disk.
//
// The calls, in the order the swap makes them:
//   1  the previous project moved aside, to `.replaced-<ms>`
//   2  the new project moved into place
//   3  the previous project put back
//   4  the new project moved out of the scratch name and kept

// A whole project at `path`, with a framerate that says which one it is.
void projectAt(const QString& path, int framerate) {
    Document doc = buildDrawnScene();
    doc.setFramerate(framerate);
    CHECK(ProjectIO::save(doc, path, nullptr));
}

QStringList leftoversIn(const QString& parent, const QString& pattern) {
    return QDir(parent).entryList(QStringList() << pattern, QDir::Dirs);
}

// The line the issue is actually about. The restore was attempted and its
// answer thrown away; when it works, this is what has to be true afterwards.
void aFailedSwapPutsThePreviousProjectBack() {
    TEST("a swap that cannot finish puts the previous project back");
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString folder = dir.filePath(QStringLiteral("shot.animage"));
    const QString scratch = dir.filePath(QStringLiteral("shot.animage.saving-abc"));

    projectAt(folder, 12);
    const std::map<QString, QByteArray> was = projectBytes(folder);
    projectAt(scratch, 30);

    int calls = 0;
    QString error;
    CHECK_EQ(ProjectIO::swapIntoPlace(scratch, folder,
                                      [&calls](const QString& from, const QString& to) {
                                          ++calls;
                                          return calls == 2 ? false : QDir().rename(from, to);
                                      },
                                      &error),
             false);
    CHECK(error.contains(QStringLiteral("cannot move the new project into place")));

    // Back where it lives, byte for byte, with nothing left lying beside it.
    CHECK(projectBytes(folder) == was);
    CHECK(leftoversIn(dir.path(), QStringLiteral("*.replaced-*")).isEmpty());
    CHECK(leftoversIn(dir.path(), QStringLiteral("*.rescued-*")).isEmpty());
    CHECK(!QFileInfo::exists(scratch));
}

// And when the restore fails too, which is the case that used to leave a
// project orphaned under a name nothing mentioned. Both copies are kept and
// both are named, because a path is the difference between a recoverable scare
// and a lost shot.
void aSwapThatCannotBePutBackKeepsBothCopiesAndNamesThem() {
    TEST("a swap that cannot be undone keeps both copies and names them");
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString folder = dir.filePath(QStringLiteral("shot.animage"));
    const QString scratch = dir.filePath(QStringLiteral("shot.animage.saving-abc"));

    projectAt(folder, 12);
    projectAt(scratch, 30);

    // Calls 2 and 3 refused. Call 4 -- moving the new project out of the
    // scratch name -- is allowed, which is the ordinary shape of this failure:
    // what the filesystem is holding is `folder`, not either of the two copies.
    int calls = 0;
    QString error;
    CHECK_EQ(ProjectIO::swapIntoPlace(
                 scratch, folder,
                 [&calls](const QString& from, const QString& to) {
                     ++calls;
                     return (calls == 2 || calls == 3) ? false : QDir().rename(from, to);
                 },
                 &error),
             false);

    CHECK(!QFileInfo::exists(folder));

    const QStringList replaced = leftoversIn(dir.path(), QStringLiteral("*.replaced-*"));
    const QStringList rescued = leftoversIn(dir.path(), QStringLiteral("*.rescued-*"));
    CHECK_EQ(replaced.size(), 1);
    CHECK_EQ(rescued.size(), 1);
    if (replaced.size() != 1 || rescued.size() != 1) return;

    // Named in the message, both of them, and said to be intact.
    CHECK(error.contains(replaced.first()));
    CHECK(error.contains(rescued.first()));
    CHECK(error.contains(QStringLiteral("Nothing has been deleted")));

    // And they really are whole projects rather than the wreckage of one.
    Document old_one;
    Document new_one;
    CHECK(ProjectIO::load(old_one, dir.filePath(replaced.first()), nullptr));
    CHECK(ProjectIO::load(new_one, dir.filePath(rescued.first()), nullptr));
    CHECK_EQ(old_one.scene().framerate, 12);
    CHECK_EQ(new_one.scene().framerate, 30);

    // Nothing is left at the scratch name. That path is named after the process
    // and so is the same one every save, and the next save's first act is to
    // clear it -- a rescue copy left there would be gone two minutes later
    // without a word.
    CHECK(!QFileInfo::exists(scratch));
}

// The fallback, when even moving the new project out of the scratch name is
// refused. There is nothing further to try, so the message has to name where it
// actually is rather than where it was meant to go.
void aRescueThatCannotBeMovedIsNamedWhereItIs() {
    TEST("a rescue copy that cannot be moved is named at the path it is on");
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString folder = dir.filePath(QStringLiteral("shot.animage"));
    const QString scratch = dir.filePath(QStringLiteral("shot.animage.saving-abc"));

    projectAt(folder, 12);
    projectAt(scratch, 30);

    int calls = 0;
    QString error;
    CHECK_EQ(ProjectIO::swapIntoPlace(scratch, folder,
                                      [&calls](const QString& from, const QString& to) {
                                          ++calls;
                                          return calls == 1 ? QDir().rename(from, to) : false;
                                      },
                                      &error),
             false);

    CHECK(error.contains(QStringLiteral("shot.animage.saving-abc")));
    CHECK(QFileInfo::exists(scratch));
    Document still_there;
    CHECK(ProjectIO::load(still_there, scratch, nullptr));
    CHECK_EQ(still_there.scene().framerate, 30);
}

// **The regression that would matter.** The failure above leaves `folder` with
// nothing in it and the caller's `SaveState` still describing what used to be
// there. That state is what decides which cel files are carried forward as hard
// links instead of re-encoded, so the question is whether the *next* save can
// build a project out of files that are no longer on disk -- which would be a
// corrupt project written silently, two minutes later, by autosave.
//
// It cannot. `carryForward` asks whether the file is there and writes the bytes
// when it is not, which is what "the state is a hint and never a promise" means.
// Pinned here rather than reasoned about, because the reasoning is what would
// quietly stop being true.
void aSaveAfterTheFolderVanishedIsStillWhole() {
    TEST("a save into a folder that vanished writes a whole project, not a difference");
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString folder = dir.filePath(QStringLiteral("shot.animage"));
    const QString reference = dir.filePath(QStringLiteral("reference.animage"));

    const Document doc = buildDrawnScene();
    ProjectIO::SaveState state;
    CHECK(ProjectIO::save(doc, folder, state, nullptr));
    CHECK_EQ(state.folder.toStdString(), folder.toStdString());
    CHECK(!state.revisions.empty());

    // Exactly what a swap that could not put anything back leaves behind.
    CHECK(QDir(folder).removeRecursively());
    CHECK(!QFileInfo::exists(folder));

    // Nothing in the document has changed, so every revision still matches and
    // every cel is a candidate to be carried forward -- with nothing left to
    // carry it from.
    QString error;
    CHECK(ProjectIO::save(doc, folder, state, &error));
    CHECK_EQ(error.toStdString(), std::string());

    CHECK(ProjectIO::save(doc, reference, nullptr));
    CHECK_EQ(projectBytes(folder) == projectBytes(reference), true);

    Document back;
    CHECK(ProjectIO::load(back, folder, &error));
    CHECK_EQ(ProjectIO::writeSceneJson(back), ProjectIO::writeSceneJson(doc));
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

// Undoing a stroke and drawing a different one leaves the history at the depth
// it was saved at, over a document that is not the one on disk. It read as
// saved, silently, and the answer is that the marker is a position in the
// history and not a count of it -- which is also what stops a history that
// drops its oldest steps from making every long session read as saved.
void adifferentEditFromTheSameDepthIsUnsaved() {
    TEST("undoing and drawing something else still counts as unsaved");
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

    drawWithMouse(canvas, QPointF(300, 300), QPointF(360, 340), 4);
    QCoreApplication::processEvents();
    window.onAutosaveTick();
    QCoreApplication::processEvents();
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));

    // Back one step and forward a different one. Same number of steps, another
    // drawing.
    QAction* undo = actionCalled(window, QStringLiteral("&Undo"));
    CHECK(undo != nullptr);
    if (!undo) return;
    const std::size_t depth = window.documentForTesting().undoDepth();
    undo->trigger();
    QCoreApplication::processEvents();
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    drawWithMouse(canvas, QPointF(500, 300), QPointF(560, 340), 4);
    QCoreApplication::processEvents();
    CHECK_EQ(window.documentForTesting().undoDepth(), depth);
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    // And it is not merely the title: the tick that decides whether to write
    // has to agree, or the change is never saved at all.
    const QString cels = folder + QStringLiteral("/cels");
    const QStringList names = QDir(cels).entryList(QDir::Files);
    CHECK(!names.isEmpty());
    if (names.isEmpty()) return;
    const QString hostage = cels + QStringLiteral("/") + names.first();
    CHECK(QFile::remove(hostage));

    window.onAutosaveTick();
    QCoreApplication::processEvents();
    CHECK(QFileInfo::exists(hostage));
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
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

    // Characters, not bytes. A std::string here is UTF-8, and walking it a byte
    // at a time hands isLetterOrNumber half of an accented letter -- the first
    // byte of "é" is a letter on its own and the second is not, so "décor" came
    // out "dÃ-cor" and nothing failed. Reported by somebody who names layers in
    // French, which is the specification's own language.
    CHECK_EQ(exporting::sequenceName("décor", "arrière").toStdString(),
             std::string("décor_arrière"));
    // Non-Latin too, where the byte-at-a-time version produced nothing readable
    // at all rather than merely the wrong letter.
    CHECK_EQ(exporting::sequenceName("фон", "b").toStdString(), std::string("фон_b"));
    // And the punctuation rule is unchanged by any of it.
    CHECK_EQ(exporting::sequenceName("décor / lointain", "a").toStdString(),
             std::string("décor-lointain_a"));

    // Two names, one folder. Sanitising is many-to-one, so this is what the
    // export has to refuse rather than what it can assume away.
    CHECK_EQ(exporting::sequenceName("t", "rough 1").toStdString(),
             exporting::sequenceName("t", "rough-1").toStdString());
    // But never across the two fields: the separator moves, so the names differ.
    CHECK(exporting::sequenceName("a b", "c") != exporting::sequenceName("a", "b c"));
    // And nothing can collide with the flattened pass, which has no underscore.
    CHECK(exporting::sequenceName("composite", "composite") != QStringLiteral("composite"));
}

// What happens when two layers of one track want the same folder.
//
// The failure this prevents is the quiet kind: both wrote the same filenames
// into the same folder, so the export succeeded, looked complete, and held one
// layer where two were asked for.
void anExportRefusesTwoLayersInOneFolder() {
    TEST("two layers that would share a folder stop the export before it starts");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString out = scratch.filePath(QStringLiteral("out"));

    animage::Document doc = buildDrawnScene();
    Track& track = doc.mutableScene().tracks.front();
    CHECK(track.layers.size() >= 2);
    if (track.layers.size() < 2) return;
    // Different names, one folder: the space and the hyphen both sanitise to a
    // hyphen. Identical names would do it too, and nothing prevents those.
    track.layers[0].name = "rough 1";
    track.layers[1].name = "rough-1";

    exporting::Options options;
    options.folder = out;
    options.layers = true;
    options.flattened = false;

    QString error;
    CHECK(!exporting::write(doc, options, nullptr, nullptr, &error));
    // Both of them by name, because "rough 1" clashing with "rough-1" is not
    // something anybody works out from a folder listing afterwards.
    CHECK(error.contains(QStringLiteral("rough 1")));
    CHECK(error.contains(QStringLiteral("rough-1")));

    // And nothing was written at all -- not even the folder. Refusing halfway
    // through would leave an export that is a mixture of two runs.
    CHECK(!QDir(out).exists());

    // The same answer without the export, which is what the window asks before
    // it empties the folder. A refusal that happened only inside write() would
    // come after the clearing and would have thrown the previous export away to
    // produce nothing.
    QString clash;
    CHECK(exporting::namesCollide(doc, &clash));
    CHECK_EQ(clash.toStdString(), error.toStdString());

    // Renaming one of them is all it takes.
    track.layers[1].name = "clean";
    error.clear();
    CHECK(!exporting::namesCollide(doc, nullptr));
    CHECK(exporting::write(doc, options, nullptr, nullptr, &error));
    CHECK(error.isEmpty());
    CHECK(QDir(out).exists());

    // A hidden layer is not written, so it cannot collide with anything. This
    // is the case a check over every layer rather than every *written* layer
    // would refuse for no reason at all.
    track.layers[1].name = "rough 1";
    track.layers[1].visible = false;
    CHECK(!exporting::namesCollide(doc, nullptr));
}

// A name too long to be a folder, which fails *partway* if nobody looks.
//
// Measured rather than assumed, and the measurement is the whole point of the
// number: up to 246 characters an export works; from 247 to 255 the folder is
// created and no frame in it can be written; from 256 the folder cannot be
// created either. That middle band is a partial export -- and the export folder
// is emptied first, so the previous one has gone as well.
void anExportRefusesANameTooLongToWrite() {
    TEST("a name too long to be a folder stops the export instead of half-writing it");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString out = scratch.filePath(QStringLiteral("out"));

    animage::Document doc = buildDrawnScene();
    Track& track = doc.mutableScene().tracks.front();
    track.name = "t";
    track.layers[1].visible = false;  // one sequence, so the arithmetic is plain

    exporting::Options options;
    options.folder = out;
    options.layers = true;
    options.flattened = false;

    // "t_" and the layer's name, so the sequence is two characters longer.
    const auto nameOfLength = [&](std::size_t total) {
        track.layers[0].name = std::string(total - 2, 'a');
        CHECK_EQ(static_cast<std::size_t>(
                     exporting::sequenceName(track.name, track.layers[0].name).size()),
                 total);
    };

    // Exactly at the limit still exports, which is what stops the number being
    // quietly lowered to something safe-looking and never noticed.
    nameOfLength(names::kExported);
    QString error;
    CHECK(!exporting::namesCollide(doc, &error));
    CHECK(exporting::write(doc, options, nullptr, nullptr, &error));
    CHECK_EQ(QDir(out).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);

    // One past it is refused, before anything is created. This is the first
    // length in the band that used to make the folder and then fail on every
    // frame in it.
    QDir(out).removeRecursively();
    nameOfLength(names::kExported + 1);
    error.clear();
    CHECK(exporting::namesCollide(doc, &error));
    CHECK(error.contains(QStringLiteral("too long")));
    CHECK(!exporting::write(doc, options, nullptr, nullptr, &error));
    CHECK(!QDir(out).exists());

    // And it says the length rather than repeating the name, which by definition
    // will not fit in a sentence: the name is elided, so the message stays a
    // sentence and still says which layer.
    CHECK(error.contains(QString::number(names::kExported + 1)));
    CHECK(error.contains(QStringLiteral("…")));
    CHECK(!error.contains(QString(40, QLatin1Char('a'))));
    CHECK(error.size() < 250);
}

// Nothing typed into the interface can reach that limit, because the fields stop
// far short of it. A hard cap and not a complaint afterwards -- which is worth
// doing only because it is far past any name anybody would want.
void aNameFieldStopsLongBeforeTheExportWould() {
    TEST("every field that names a track or a layer caps its length");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* layers = static_cast<LayerList*>(window.findChild<QTreeWidget*>());
    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(layers != nullptr);
    CHECK(timeline != nullptr);
    if (!layers || !timeline) return;

    layers->renameRowForTesting(0);
    QCoreApplication::processEvents();
    auto* layer_editor = layers->findChild<QLineEdit*>();
    CHECK(layer_editor != nullptr);
    if (!layer_editor) return;
    CHECK_EQ(layer_editor->maxLength(), names::kTyped);

    // Typed rather than assigned: setText is capped by the same rule, and what
    // is being pinned is that a hand cannot get past it.
    layer_editor->setText(QString(names::kTyped + 20, QLatin1Char('x')));
    CHECK_EQ(layer_editor->text().size(), names::kTyped);
    layers->finishRenameForTesting(false);
    settleEditors();

    timeline->renameTrackForTesting(0);
    QCoreApplication::processEvents();
    QLineEdit* track_editor = timeline->renameEditorForTesting();
    CHECK(track_editor != nullptr);
    if (!track_editor) return;
    CHECK_EQ(track_editor->maxLength(), names::kTyped);

    // Two of them and a separator have to fit in what the export can write, or
    // the cap would be decoration. The header asserts it too; this is the same
    // sum from the other side.
    CHECK(2 * static_cast<std::size_t>(names::kTyped) + 1 < names::kExported);
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
        // Only the rows Qt is meant to deliver. The keys a live transform
        // borrows are read off the event by the canvas, and the held keys are
        // held rather than pressed -- an action for either would consume the key
        // and be exactly the bug they are in the table to prevent.
        if (entry.kind != shortcuts::Kind::Action) {
            CHECK(action == nullptr);
            continue;
        }
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

// The other half of issue #14: a control that names a key has to go on naming
// the right one.
//
// Every one of these tooltips used to end in a typed-out "(Ctrl+D)" or
// "(Enter)". That was already only as true as the last person to move a binding,
// and it is false the first time anybody rebinds anything -- which is now a
// thing they can do. So the sentence says what the control does and the key is
// appended from the bindings.
//
// Nothing about the spelling is asserted, because it is the platform's: Ctrl+D
// is "⌘D" on macOS and the test would be about which machine it ran on. What is
// asserted is that the tooltip says what the bindings say, before and after.
void theTooltipsFollowTheKeys() {
    TEST("a control's tooltip names the key it is on, and follows a rebinding");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    const auto native = [](shortcuts::Id id) {
        return shortcuts::current().sequenceFor(id).toString(QKeySequence::NativeText);
    };
    const auto inBrackets = [](const QString& key) { return QStringLiteral("(%1)").arg(key); };

    QPushButton* duplicate = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Duplicate")) duplicate = button;
    }
    CHECK(duplicate != nullptr);
    if (!duplicate) return;

    const QString was = native(shortcuts::Id::DuplicateDrawing);
    CHECK(!was.isEmpty());
    CHECK(duplicate->toolTip().contains(inBrackets(was)));

    // Including the two tools that used to say nothing at all: a toolbar button
    // with no tooltip is one whose key you find out about by reading the source.
    for (const shortcuts::Id id : {shortcuts::Id::Brush, shortcuts::Id::Eraser,
                                   shortcuts::Id::Lasso, shortcuts::Id::Transform}) {
        QAction* tool = window.actionForTesting(id);
        CHECK(tool != nullptr);
        if (tool) CHECK(tool->toolTip().contains(inBrackets(native(id))));
    }
    // And a sentence that names *other* keys has them substituted rather than
    // typed, which is the only way "Return applies" can survive Return moving.
    QAction* transform = window.actionForTesting(shortcuts::Id::Transform);
    if (transform) {
        CHECK(transform->toolTip().contains(
            QStringLiteral("%1 applies").arg(native(shortcuts::Id::TransformApply))));
        CHECK(transform->toolTip().contains(
            QStringLiteral("%1 cancels").arg(native(shortcuts::Id::TransformCancel))));
    }

    shortcuts::Bindings moved;
    moved.set(shortcuts::Id::DuplicateDrawing,
              QKeySequence(QStringLiteral("Ctrl+Shift+K"), QKeySequence::PortableText));
    window.adoptShortcuts(moved);

    const QString now = native(shortcuts::Id::DuplicateDrawing);
    CHECK(duplicate->toolTip().contains(inBrackets(now)));
    CHECK(!duplicate->toolTip().contains(inBrackets(was)));
    // The action moved too, and not only the words about it.
    CHECK_EQ(window.actionForTesting(shortcuts::Id::DuplicateDrawing)
                 ->shortcut()
                 .toString(QKeySequence::PortableText)
                 .toStdString(),
             std::string("Ctrl+Shift+K"));

    // Put the keyboard back. The bindings are process-wide, so a test that left
    // one changed would be a test that changed the ones after it.
    window.adoptShortcuts(shortcuts::Bindings());
    CHECK(duplicate->toolTip().contains(inBrackets(was)));
}

// What the panel is for, asserted through the panel rather than through the rule
// underneath it: Apply is the gate, and a gate that is open is worth nothing.
//
// The situation is issue #14's own -- Fit drawing back on Shift+0 beside Fit
// canvas on 0 -- because it is the collision that looks like two different keys.
void theShortcutsPanelWillNotApplyACollision() {
    TEST("the shortcuts panel refuses a collision, and takes it back once it is gone");
    shortcuts::Bindings clashing;
    clashing.set(shortcuts::Id::FitDrawing,
                 QKeySequence(QStringLiteral("Shift+0"), QKeySequence::PortableText));

    ShortcutsDialog dialog(clashing);
    dialog.show();
    QCoreApplication::processEvents();

    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    CHECK(buttons != nullptr);
    if (!buttons) return;
    QPushButton* apply = buttons->button(QDialogButtonBox::Apply);
    CHECK(apply != nullptr);
    if (!apply) return;
    CHECK(!apply->isEnabled());

    // And it says which two, in words. "Apply is greyed out" with nothing to
    // read is a dialog people close.
    bool named = false;
    for (QLabel* said : dialog.findChildren<QLabel*>()) {
        if (said->text().contains(QStringLiteral("Fit canvas")) &&
            said->text().contains(QStringLiteral("Fit drawing"))) {
            named = true;
        }
    }
    CHECK(named);

    // Typing a free key into the row that caused it. Found by what it holds
    // rather than by its position, so adding a row above it does not move this
    // test onto another one.
    QKeySequenceEdit* offending = nullptr;
    for (QKeySequenceEdit* edit : dialog.findChildren<QKeySequenceEdit*>()) {
        if (edit->keySequence() ==
            QKeySequence(QStringLiteral("Shift+0"), QKeySequence::PortableText)) {
            offending = edit;
        }
    }
    CHECK(offending != nullptr);
    if (!offending) return;
    offending->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Shift+F"),
                                           QKeySequence::PortableText));
    QCoreApplication::processEvents();

    CHECK(apply->isEnabled());
    // And what it would hand back is the edit, not the set it was opened on.
    CHECK_EQ(dialog.bindings()
                 .sequenceFor(shortcuts::Id::FitDrawing)
                 .toString(QKeySequence::PortableText)
                 .toStdString(),
             std::string("Ctrl+Shift+F"));
    // Nothing was installed by any of that: the panel edits a copy, and only
    // MainWindow's Apply handler adopts it.
    CHECK(shortcuts::current().isDefault(shortcuts::Id::FitDrawing));

    // And a chord *typed* into a row lands in the bindings, which is the one
    // part of this panel that nothing but a real key event exercises -- the
    // editors are told to take one chord and no more, and a field that quietly
    // waited for a second one would look identical until you pressed Apply.
    QKeySequenceEdit* brush = nullptr;
    for (QKeySequenceEdit* edit : dialog.findChildren<QKeySequenceEdit*>()) {
        if (edit->keySequence() ==
            QKeySequence(QStringLiteral("B"), QKeySequence::PortableText)) {
            brush = edit;
        }
    }
    CHECK(brush != nullptr);
    if (!brush) return;
    // Press *and* release. QKeySequenceEdit clears the field on the press and
    // only settles on the release -- with one chord asked for it finishes there
    // rather than waiting out its one-second timer -- so a test that pressed and
    // walked away would be reading a field mid-edit and would say the row had
    // been unbound.
    QKeyEvent down(QEvent::KeyPress, Qt::Key_J, Qt::ControlModifier);
    QCoreApplication::sendEvent(brush, &down);
    QKeyEvent up(QEvent::KeyRelease, Qt::Key_J, Qt::ControlModifier);
    QCoreApplication::sendEvent(brush, &up);
    QCoreApplication::processEvents();
    CHECK_EQ(dialog.bindings()
                 .sequenceFor(shortcuts::Id::Brush)
                 .toString(QKeySequence::PortableText)
                 .toStdString(),
             std::string("Ctrl+J"));
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

// The canvas asks the bindings which key applies rather than naming
// Qt::Key_Return, and this is the check that goes red if anything puts the
// literal back.
//
// It has to be asserted here and cannot be inferred from the table: a live
// transform borrows keys from actions that have been *disabled*, so there is no
// QAction anywhere holding these and nothing else in the program would ever
// notice them being wrong.
void theTransformKeysFollowTheirBindings() {
    TEST("a rebound Apply applies the transform, and the key it left does not");
    WindowWithInk fixture;
    CHECK(fixture.canvas != nullptr);
    if (!fixture.canvas) return;

    shortcuts::Bindings moved;
    moved.set(shortcuts::Id::TransformApply,
              QKeySequence(QStringLiteral("Ctrl+K"), QKeySequence::PortableText));
    moved.set(shortcuts::Id::NudgeRight,
              QKeySequence(QStringLiteral("Alt+Right"), QKeySequence::PortableText));
    CHECK(moved.clashes().empty());
    fixture.window.adoptShortcuts(moved);

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->transformIsLive());

    // The key it used to be on does nothing now -- and it is worth checking that
    // it does *nothing* rather than something else: Play owns Return in the
    // other mode, and a transform that started the animation would be a worse
    // bug than one that ignored a key.
    fixture.press(Qt::Key_Right);
    CHECK_NEAR(fixture.canvas->transformValues().dx, 0.0, 1e-9);
    fixture.press(Qt::Key_Return);
    CHECK(fixture.canvas->transformIsLive());

    fixture.press(Qt::Key_Right, Qt::AltModifier);
    CHECK_NEAR(fixture.canvas->transformValues().dx, 1.0, 1e-9);
    // Ten at a time is still "the binding, with a Shift it has not got", which
    // is what keeps it working when the nudge keys move.
    fixture.press(Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
    CHECK_NEAR(fixture.canvas->transformValues().dx, 11.0, 1e-9);

    fixture.press(Qt::Key_K, Qt::ControlModifier);
    CHECK(!fixture.canvas->transformIsLive());

    // Put the keyboard back: the bindings are process-wide.
    fixture.window.adoptShortcuts(shortcuts::Bindings());
}

// --- flipping (#24) -------------------------------------------------------

QPushButton* buttonCalled(const MainWindow& window, const QString& text) {
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == text) return button;
    }
    return nullptr;
}

void theFlipButtonsMirrorTheDrawing() {
    TEST("Flip X mirrors the drawing exactly, and flipping back writes nothing");
    WindowWithInk fixture;
    CHECK(fixture.canvas != nullptr);
    if (!fixture.canvas) return;

    QPushButton* flip_x = buttonCalled(fixture.window, QStringLiteral("Flip X"));
    CHECK(flip_x != nullptr);
    CHECK(buttonCalled(fixture.window, QStringLiteral("Flip Y")) != nullptr);
    if (!flip_x) return;
    // The bar exists only while a transform does, and so do these.
    CHECK(!flip_x->isVisible());

    const animage::Cel* cel = fixture.ink();
    CHECK(cel != nullptr);
    if (!cel) return;
    const animage::TileGrid before = cel->tiles();
    const animage::PixelRect box = animage::paintedBounds(before);
    CHECK(!box.isEmpty());
    const std::size_t depth = fixture.doc().undoDepth();

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    CHECK(flip_x->isVisible());
    CHECK(!flip_x->isChecked());

    flip_x->click();
    QCoreApplication::processEvents();
    // The button says which way round the drawing is, so it stays down.
    CHECK(flip_x->isChecked());
    CHECK(fixture.canvas->transformValues().flip_x);

    fixture.canvas->applyTransform();
    QCoreApplication::processEvents();
    CHECK_EQ(fixture.doc().undoDepth(), depth + 1);

    const animage::Cel* after = fixture.ink();
    CHECK(after != nullptr);
    if (!after) return;

    // Bit-exact and mirrored, which is the whole of issue #24: a flip built as a
    // scale of -1 through the resampler would come back soft and half a pixel
    // out, and nothing on screen would say so.
    const int mirror_x = 2 * box.x + box.width - 1;
    std::size_t differing = 0;
    for (int y = box.y; y < box.y + box.height; ++y) {
        for (int x = box.x; x < box.x + box.width; ++x) {
            if (!(before.pixel(x, y) == after->tiles().pixel(mirror_x - x, y))) ++differing;
        }
    }
    CHECK_EQ(differing, static_cast<std::size_t>(0));

    // And a flip and a flip back is not an edit. It is the same rule that stops
    // picking a drawing up and putting it down costing a resample -- which
    // matters more here, because a flip is one click away from being undone by
    // another one.
    const std::size_t settled = fixture.doc().undoDepth();
    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    flip_x->click();
    flip_x->click();
    QCoreApplication::processEvents();
    CHECK(!fixture.canvas->transformValues().flip_x);
    CHECK(!flip_x->isChecked());
    fixture.canvas->applyTransform();
    QCoreApplication::processEvents();
    CHECK_EQ(fixture.doc().undoDepth(), settled);
}

// The handle maths has to carry the flip's sign, and this is what says so.
//
// A mirrored box has its top-left handle over on the right, so a drag measured
// against the unmirrored arm asks for a negative factor -- and the clamp that
// keeps scale positive turns that into a collapse to one per cent. It looks like
// the box imploding the moment you touch it after flipping.
void aFlippedBoxStillScalesTheRightWay() {
    TEST("a corner drag on a flipped box grows it rather than collapsing it");
    WindowWithInk fixture;
    CHECK(fixture.canvas != nullptr);
    if (!fixture.canvas) return;

    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();

    animage::Transform flipped = fixture.canvas->transformValues();
    flipped.flip_x = true;
    fixture.canvas->setTransformValues(flipped);
    QCoreApplication::processEvents();

    const QPointF centre = fixture.canvas->transformCentreForTesting();
    const QPointF corner = fixture.canvas->transformHandlesForTesting()[0];
    // Straight out along the arm it is already on: whatever the flip did to
    // where that handle is drawn, dragging away from the middle is bigger.
    drawWithMouse(fixture.canvas, corner, centre + (corner - centre) * 1.5, 6);

    const animage::Transform grown = fixture.canvas->transformValues();
    CHECK(grown.scale_x > 1.2);
    CHECK(grown.scale_y > 1.2);
    // And the mirror is still a mirror: a drag scales, it does not unflip.
    CHECK(grown.flip_x);

    fixture.canvas->cancelTransform();
}

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

void aLostReleaseDoesNotEndTheUndoHistory() {
    TEST("a stroke whose release never arrives does not end the undo history");
    WindowWithInk fixture;
    CHECK(fixture.canvas != nullptr);
    if (!fixture.canvas) return;
    Document& doc = fixture.window.documentForTesting();

    QPointingDevice stylus(QStringLiteral("test stylus"), 1, QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Pen,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                           1, 0);
    const auto press = [&](const QPointF& at) {
        QTabletEvent e(QEvent::TabletPress, &stylus, at, fixture.canvas->mapToGlobal(at), 1.0, 0, 0,
                       0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
        QCoreApplication::sendEvent(fixture.canvas, &e);
    };
    const auto release = [&](const QPointF& at) {
        QTabletEvent e(QEvent::TabletRelease, &stylus, at, fixture.canvas->mapToGlobal(at), 0.0, 0,
                       0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
        QCoreApplication::sendEvent(fixture.canvas, &e);
    };

    // The pen goes down and never comes up: the keyboard leaves the canvas,
    // which is what a modal dialog opening over it does, and what the window
    // switcher does. Before this was caught, the command opened by the stroke
    // stayed open and Document::beginCommand counts depth -- so every later
    // stroke went one, two, one and never reached zero again. Nothing was ever
    // pushed onto the undo stack for the rest of the session.
    press(QPointF(400.0, 300.0));
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->isStroking());

    fixture.canvas->clearFocus();
    QCoreApplication::processEvents();
    CHECK(!fixture.canvas->isStroking());

    // The real damage was never the flag: it was that the history stopped
    // recording. So the test is that a later stroke is still undoable.
    const std::size_t before = doc.undoDepth();
    press(QPointF(500.0, 320.0));
    release(QPointF(540.0, 360.0));
    QCoreApplication::processEvents();
    CHECK(doc.undoDepth() > before);

    // And once more, because the failure compounded: the depth never came back
    // down, so it was the second and third strokes that proved it was stuck.
    const std::size_t after_one = doc.undoDepth();
    press(QPointF(560.0, 380.0));
    release(QPointF(600.0, 420.0));
    QCoreApplication::processEvents();
    CHECK(doc.undoDepth() > after_one);
}

void enteringATransformPutsTheOtherToolsDown() {
    TEST("entering a transform puts the eraser down rather than leaving it up");
    WindowWithInk fixture;
    CHECK(fixture.canvas != nullptr);
    if (!fixture.canvas) return;

    fixture.action(shortcuts::Id::Eraser)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.canvas->isErasing());

    // Which tool has the pen is one value, so entering a transform cannot leave
    // a second one up behind it. It used to: the handler cleared the lasso and
    // said nothing about the eraser, so Alt+right-drag went on resizing the
    // eraser, the ring drew at its radius, and the toolbar's size box showed its
    // number under a checked Transform button until the transform ended.
    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();

    CHECK(fixture.canvas->transformIsLive());
    CHECK(fixture.canvas->tool() == CanvasWidget::Tool::Transform);
    CHECK(!fixture.canvas->isErasing());
    CHECK(!fixture.canvas->isLassoing());

    // And leaving it comes back to the brush, not to the tool that was up when
    // it started.
    fixture.canvas->cancelTransform();
    QCoreApplication::processEvents();
    CHECK(!fixture.canvas->transformIsLive());
    CHECK(fixture.canvas->tool() == CanvasWidget::Tool::Brush);
    CHECK(!fixture.canvas->isErasing());
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

// --- issue #43: the fifth operation that removes what is under the loop -----
//
// `eraseSelection` is a fifth operation that takes away what the loop covers,
// and it was never added to the list the other four share. It re-implemented
// the check with its own copy, and answered `bool` to an action that threw the
// answer away.
//
// The layer kind is deliberately *not* added to it. Cut, copy, paste and
// transform refuse on a colour layer because what is on one is scribbles and
// not lines: they carry marks from one place to another, and the two kinds are
// not the same thing to carry. Erasing carries nothing anywhere -- it takes a
// mark away, which is what the eraser already does on that layer -- so
// Backspace being stricter than the eraser beside it would be the surprise,
// not the consistency. The two are asserted side by side here because that
// difference is the whole decision.
void backspaceErasesOnAColourLayerWhereCutRefuses() {
    TEST("Backspace erases scribbles on a colour layer; Cut still refuses there");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    for (QPushButton* button : fixture.window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add colour layer")) button->click();
    }
    QCoreApplication::processEvents();

    const LayerId colour = fixture.canvas->activeLayer();
    const TrackId track = fixture.doc().scene().tracks.front().id;
    const Layer* settings = fixture.doc().scene().findTrack(track)->findLayer(colour);
    CHECK(settings != nullptr);
    if (!settings) return;
    CHECK_EQ(static_cast<int>(settings->kind), static_cast<int>(LayerKind::Ctg));

    // A real colour rather than whatever the panel was holding: the transparent
    // scribble is a label too, and "did the mark land" must not depend on which
    // one it was.
    fixture.canvas->setBrushColour(1.0f, 0.0f, 0.0f);
    drawWithMouse(fixture.canvas, QPointF(300, 300), QPointF(420, 360), 10);

    const auto scribbles = [&]() -> const Cel* {
        return fixture.doc().celAt(track, fixture.canvas->currentImage(), colour);
    };
    CHECK(scribbles() != nullptr);
    if (!scribbles()) return;
    CHECK(!animage::paintedBounds(scribbles()->tiles()).isEmpty());

    fixture.action(shortcuts::Id::Lasso)->trigger();
    QCoreApplication::processEvents();
    fixture.lasso(QRectF(270, 270, 180, 120));
    CHECK(fixture.canvas->hasSelection());

    // Cut refuses, and says so.
    fixture.window.statusBar()->clearMessage();
    fixture.action(shortcuts::Id::Cut)->trigger();
    QCoreApplication::processEvents();
    CHECK(fixture.window.statusBar()->currentMessage().contains(QStringLiteral("Cannot cut")));
    CHECK(!animage::paintedBounds(scribbles()->tiles()).isEmpty());
    CHECK(fixture.canvas->hasSelection());

    // Backspace does not. Cleared first, because the refusal above sits in the
    // bar for six seconds and would otherwise be read as this one's answer.
    fixture.window.statusBar()->clearMessage();
    fixture.action(shortcuts::Id::EraseSelection)->trigger();
    QCoreApplication::processEvents();
    CHECK(!fixture.window.statusBar()->currentMessage().contains(QStringLiteral("Cannot")));
    CHECK(scribbles() == nullptr || animage::paintedBounds(scribbles()->tiles()).isEmpty());
    CHECK(!fixture.canvas->hasSelection());
}

// The other half of it. `eraseSelection` returned `bool` and the action
// discarded it, so on a locked layer Ctrl+X named the reason and Backspace did
// nothing at all with nothing said. Its four siblings all report through
// `explain()`.
void backspaceSaysWhyWhenItRefuses() {
    TEST("Backspace names the reason it will not erase");

    // No loop at all. Saying so beats a key that looks broken.
    {
        WindowWithInk fixture;
        if (!fixture.canvas) return;
        CHECK(!fixture.canvas->hasSelection());
        fixture.window.statusBar()->clearMessage();
        fixture.action(shortcuts::Id::EraseSelection)->trigger();
        QCoreApplication::processEvents();
        const QString said = fixture.window.statusBar()->currentMessage();
        CHECK(said.contains(QStringLiteral("Cannot erase")));
        CHECK(said.contains(QStringLiteral("selected")));
    }

    // A locked layer and a hidden one: the two the brush refuses, and the two
    // Backspace used to refuse in silence.
    const std::pair<const char*, bool> cases[] = {{"locked", true}, {"hidden", false}};
    for (const auto& [expected, lock] : cases) {
        WindowWithInk fixture;
        if (!fixture.canvas) return;

        fixture.action(shortcuts::Id::Lasso)->trigger();
        QCoreApplication::processEvents();
        fixture.lasso(QRectF(270, 270, 180, 120));
        CHECK(fixture.canvas->hasSelection());

        Document& doc = fixture.doc();
        const TrackId track = doc.scene().tracks.front().id;
        const LayerId ink = doc.scene().findTrack(track)->layers.front().id;
        Layer settings = *doc.scene().findTrack(track)->findLayer(ink);
        if (lock) {
            settings.locked = true;
        } else {
            settings.visible = false;
        }
        doc.updateLayer(track, ink, settings);
        QCoreApplication::processEvents();

        fixture.window.statusBar()->clearMessage();
        fixture.action(shortcuts::Id::EraseSelection)->trigger();
        QCoreApplication::processEvents();

        const QString said = fixture.window.statusBar()->currentMessage();
        CHECK(said.contains(QStringLiteral("Cannot erase")));
        CHECK(said.contains(QString::fromLatin1(expected)));
        // And the drawing is exactly where it was.
        CHECK(fixture.ink() != nullptr && fixture.ink()->tiles().pixel(340, 320).a > 0.5f);
    }
}

// `beginStroke` held a third copy of the same list, and splitting the shared
// one is only safe if the brush still refuses precisely where it refused
// before -- and still draws on a colour layer, where the mark is a label
// rather than paint.
void theBrushRefusesWhereItAlwaysDid() {
    TEST("the brush still refuses a locked or hidden layer, and still labels a colour one");

    const std::pair<const char*, bool> blocked[] = {{"locked", true}, {"hidden", false}};
    for (const auto& [what, lock] : blocked) {
        (void)what;
        WindowWithInk fixture;
        if (!fixture.canvas) return;

        Document& doc = fixture.doc();
        const TrackId track = doc.scene().tracks.front().id;
        const LayerId ink = doc.scene().findTrack(track)->layers.front().id;
        Layer settings = *doc.scene().findTrack(track)->findLayer(ink);
        if (lock) {
            settings.locked = true;
        } else {
            settings.visible = false;
        }
        doc.updateLayer(track, ink, settings);
        QCoreApplication::processEvents();

        // Somewhere the fixture's own stroke never reached, so "nothing landed"
        // is not being read off a pixel that was already clear of it.
        const std::size_t depth = doc.undoDepth();
        drawWithMouse(fixture.canvas, QPointF(200, 200), QPointF(260, 240), 8);
        CHECK_EQ(doc.undoDepth(), depth);  // no command was even opened
        CHECK(fixture.ink() == nullptr || fixture.ink()->tiles().pixel(230, 220).a < 0.5f);
        CHECK(!fixture.canvas->isStroking());
    }

    // And a colour layer is not on that list: the brush draws there, as a label.
    {
        WindowWithInk fixture;
        if (!fixture.canvas) return;
        for (QPushButton* button : fixture.window.findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("Add colour layer")) button->click();
        }
        QCoreApplication::processEvents();

        const LayerId colour = fixture.canvas->activeLayer();
        const TrackId track = fixture.doc().scene().tracks.front().id;
        fixture.canvas->setBrushColour(1.0f, 0.0f, 0.0f);
        drawWithMouse(fixture.canvas, QPointF(300, 300), QPointF(420, 360), 10);

        const Cel* cel = fixture.doc().celAt(track, fixture.canvas->currentImage(), colour);
        CHECK(cel != nullptr);
        if (cel) CHECK(!animage::paintedBounds(cel->tiles()).isEmpty());
    }
}


// The rule about what *not* to say. Past the end of a track the status line
// already reads "you cannot draw past the end of a track", permanently, from
// the moment the playhead gets there. A six-second banner saying the same thing
// in other words is not more information -- it is the same information covering
// itself up, and it covers the line that was saying it. So `NoDrawing` is the
// one refusal nobody announces, and this holds all five of them to it.
void pastTheEndOfATrackTheRefusalIsNotSaidTwice() {
    TEST("past the end of a track a refusal is left to the status line");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    auto* timeline = fixture.window.findChild<TimelineWidget*>();
    CHECK(timeline != nullptr);
    if (!timeline) return;

    Document& doc = fixture.doc();
    const TrackId track = doc.scene().tracks.front().id;
    const std::size_t frames = doc.scene().findTrack(track)->frameCount();
    doc.setSceneLength(true, 24);  // somewhere past the track's end to stand
    QCoreApplication::processEvents();
    timeline->setCurrentSlot(frames + 3);
    QCoreApplication::processEvents();
    CHECK_EQ(fixture.canvas->currentImage(), kNoId);

    // The thing being left unsaid is genuinely being said elsewhere. Found by
    // its text rather than by which widget holds it, which is not the point.
    bool the_line_says_it = false;
    for (QLabel* label : fixture.window.findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("past the end of a track"))) {
            the_line_says_it = true;
        }
    }
    CHECK(the_line_says_it);

    const shortcuts::Id quiet[] = {shortcuts::Id::EraseSelection, shortcuts::Id::Transform,
                                   shortcuts::Id::Cut, shortcuts::Id::Copy};
    for (const shortcuts::Id id : quiet) {
        fixture.window.statusBar()->clearMessage();
        fixture.action(id)->trigger();
        QCoreApplication::processEvents();
        CHECK_EQ(fixture.window.statusBar()->currentMessage().toStdString(), std::string());
    }
}

// An empty selection cannot exist, and changing layer is the second way one
// could have come about. `endLasso` had always thrown away a loop that caught
// no ink where it was drawn; carrying that loop to a layer it covers nothing on
// went the other way, and left one standing over nothing.
//
// One rule applied in one place is what removes the whole question, and with it
// the refusal that would otherwise have to explain the state to somebody.
void aLoopIsDroppedWhenItCatchesNothingOnTheNewLayer() {
    TEST("a loop carried onto a layer it catches nothing on is dropped");
    WindowWithInk fixture;
    if (!fixture.canvas) return;

    Document& doc = fixture.doc();
    const TrackId track = doc.scene().tracks.front().id;
    const LayerId inked = doc.scene().findTrack(track)->layers.front().id;

    // A second raster layer with ink of its own, well clear of where the loop
    // will go -- so the layer is not empty, and `NothingDrawn` is not what is
    // being tested here. Drawn before the lasso tool is picked up, because
    // while it is held a drag makes a loop rather than a mark.
    const LayerId elsewhere = doc.addLayer(track, "elsewhere");
    fixture.canvas->setActiveLayer(elsewhere);
    QCoreApplication::processEvents();
    fixture.canvas->setBrushColour(0.0f, 0.0f, 0.0f);
    drawWithMouse(fixture.canvas, QPointF(150, 150), QPointF(200, 190), 8);
    const Cel* cel = doc.celAt(track, fixture.canvas->currentImage(), elsewhere);
    CHECK(cel != nullptr);
    if (cel) CHECK(!animage::paintedBounds(cel->tiles()).isEmpty());

    // The loop is made over the *inked* layer, which is the only way to make
    // one at all: a lasso catching nothing is thrown away where it is drawn.
    fixture.canvas->setActiveLayer(inked);
    fixture.action(shortcuts::Id::Lasso)->trigger();
    QCoreApplication::processEvents();
    fixture.lasso(QRectF(270, 270, 180, 120));
    CHECK(fixture.canvas->hasSelection());

    // And now carried to the other layer, where it covers none of its ink. It
    // does not survive the trip.
    fixture.canvas->setActiveLayer(elsewhere);
    QCoreApplication::processEvents();
    CHECK(!fixture.canvas->hasSelection());

    // So Backspace here is the ordinary "nothing is selected" and not a state
    // of its own needing words of its own.
    fixture.window.statusBar()->clearMessage();
    fixture.action(shortcuts::Id::EraseSelection)->trigger();
    QCoreApplication::processEvents();

    const QString said = fixture.window.statusBar()->currentMessage();
    CHECK(said.contains(QStringLiteral("Cannot erase")));
    CHECK(said.contains(QStringLiteral("nothing is selected")));
    // And nothing was taken off either layer. The inked one is asked for by id
    // rather than through the fixture: `addLayer` defaults to the top of the
    // stack, so `layers.front()` is no longer the layer that was drawn on.
    if (cel) CHECK(!animage::paintedBounds(cel->tiles()).isEmpty());
    const Cel* untouched = doc.celAt(track, fixture.canvas->currentImage(), inked);
    CHECK(untouched != nullptr && untouched->tiles().pixel(340, 320).a > 0.5f);
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

    // The second layer is given ink under where the loop will go. A loop
    // catching nothing on the layer it arrives at is dropped -- an empty
    // selection cannot exist -- so a layer with nothing on it would test that
    // rule rather than this one.
    fixture.canvas->setActiveLayer(second);
    QCoreApplication::processEvents();
    drawWithMouse(fixture.canvas, QPointF(300, 300), QPointF(420, 360), 10);
    fixture.canvas->setActiveLayer(
        doc.scene().findTrack(track)->layers.back().id);
    QCoreApplication::processEvents();

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

// --- what the pointer says -----------------------------------------------
//
// A screenshot cannot check any of this. QWidget::grab() renders the widget and
// not the screen, so no pointer appears in it -- which is why these assert the
// decision and the cursor shape after moving the pointer somewhere, and it is a
// better test than a picture would have been: a decision can be asserted
// exactly, an appearance can only be compared.
//
// Driven through an unshown CanvasWidget rather than a window on purpose. A
// widget nobody can see receives no real mouse events, so nothing the person
// running the tests happens to be doing with their own pointer can reach it.

int shapeOf(const QWidget* widget) { return static_cast<int>(widget->cursor().shape()); }
int pointingOf(const CanvasWidget& canvas) { return static_cast<int>(canvas.pointing()); }
int pointing(CanvasWidget::Pointing value) { return static_cast<int>(value); }
int shape(Qt::CursorShape value) { return static_cast<int>(value); }

void hover(QWidget* canvas, const QPointF& at) {
    sendMouse(canvas, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
    QCoreApplication::processEvents();
}

void sendMouseWith(QWidget* widget, QEvent::Type type, const QPointF& at, Qt::MouseButton button,
                   Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, modifiers);
    QCoreApplication::sendEvent(widget, &event);
    QCoreApplication::processEvents();
}

// A live transform on a widget with nothing else attached to it. The tool is
// the button, so entering it is the whole of starting one.
struct BoxFixture {
    Fixture f;

    BoxFixture() {
        f.draw(300.0f, 300.0f, 420.0f, 360.0f);
        f.canvas.beginTransform();
    }
};

void thePointerSaysWhatAPressOnTheBoxWillDo() {
    TEST("the pointer says which of the four things a press on the box will do");
    BoxFixture box;
    CanvasWidget& canvas = box.f.canvas;
    CHECK(canvas.transformIsLive());

    const std::array<QPointF, 8> handles = canvas.transformHandlesForTesting();
    const QPointF centre = canvas.transformCentreForTesting();

    // Inside: the drawing moves.
    hover(&canvas, centre);
    CHECK_EQ(pointingOf(canvas), pointing(CanvasWidget::Pointing::Move));
    CHECK_EQ(shapeOf(&canvas), shape(Qt::SizeAllCursor));

    // The corners scale both axes, and which diagonal is the one the hand has
    // to drag along.
    hover(&canvas, handles[0]);
    CHECK_EQ(shapeOf(&canvas), shape(Qt::SizeFDiagCursor));  // top left, "\"
    hover(&canvas, handles[2]);
    CHECK_EQ(shapeOf(&canvas), shape(Qt::SizeBDiagCursor));  // top right, "/"

    // The edge middles scale one, along their own normal.
    hover(&canvas, handles[1]);
    CHECK_EQ(shapeOf(&canvas), shape(Qt::SizeVerCursor));
    hover(&canvas, handles[3]);
    CHECK_EQ(shapeOf(&canvas), shape(Qt::SizeHorCursor));

    // The knob turns it. Nothing in Qt says "rotate", so this one is drawn --
    // which is why the decision is asserted as well as the shape: every drawn
    // cursor answers Qt::BitmapCursor whatever was drawn on it.
    hover(&canvas, canvas.rotationHandleForTesting());
    CHECK_EQ(pointingOf(canvas), pointing(CanvasWidget::Pointing::Rotate));
    CHECK_EQ(shapeOf(&canvas), shape(Qt::BitmapCursor));
    // What it looks like was checked by saving cursor().pixmap() and looking at
    // it, which is the one way a drawn cursor can be seen -- grab() renders the
    // widget and the pointer is not in the widget. The first eyedropper was a
    // thumb and the first bulb was a magnifying glass; neither is a thing any
    // assertion here would have noticed.

    // And so does the band just outside a corner, which was the whole of the
    // complaint: dragging at a corner and dragging just outside one did two
    // different things and looked identical. The band stays -- it is where a
    // hand reaches without being told -- and now it says so.
    const QPointF outward = (handles[0] - centre) / QLineF(centre, handles[0]).length();
    hover(&canvas, handles[0] + outward * 16.0);
    CHECK_EQ(pointingOf(canvas), pointing(CanvasWidget::Pointing::Rotate));

    // And well outside it a press moves the drawing, exactly as a press in the
    // middle does. There used to be a fourth outcome here -- a press that did
    // nothing, shown as an arrow -- and it went when the box stopped being the
    // only place the drawing could be picked up from. The pointer says so,
    // which is the rule this whole function exists for: it answers the same
    // question as the press underneath it.
    hover(&canvas, QPointF(60.0, 60.0));
    CHECK_EQ(pointingOf(canvas), pointing(CanvasWidget::Pointing::Move));
    CHECK_EQ(shapeOf(&canvas), shape(Qt::SizeAllCursor));

    // Leaving the tool puts the pointer back without the pointer moving. This is
    // the shape of bug the one decision point exists to make impossible: a
    // cursor left over from a state that has ended.
    hover(&canvas, centre);
    CHECK_EQ(shapeOf(&canvas), shape(Qt::SizeAllCursor));
    canvas.cancelTransform();
    CHECK_EQ(shapeOf(&canvas), shape(Qt::CrossCursor));
}

// Reported: a drag that started outside the box did nothing, so the only way to
// move the drawing was to press inside a rectangle drawn round it -- which is
// the one place you cannot press when what you are moving is a thin line, or
// when the middle of the box is the drawing underneath that you are lining it
// up against.
void aDragOutsideTheBoxMovesIt() {
    TEST("a transform is moved by a drag that starts nowhere near it");
    BoxFixture box;
    CanvasWidget& canvas = box.f.canvas;
    CHECK(canvas.transformIsLive());
    CHECK_NEAR(canvas.transformValues().dx, 0.0, 1e-9);

    // A corner of the widget, as far from the box as it goes. The handles and
    // the rotate band round them are all tested before the fallback, so a point
    // this far out can only be the fallback's own answer.
    const QPointF outside(8.0, 8.0);
    const QPointF moved = outside + QPointF(40.0, 25.0);
    CHECK(QLineF(outside, canvas.transformCentreForTesting()).length() > 100.0);

    sendMouse(&canvas, QEvent::MouseButtonPress, outside, Qt::LeftButton, Qt::LeftButton);
    sendMouse(&canvas, QEvent::MouseMove, moved, Qt::NoButton, Qt::LeftButton);
    sendMouse(&canvas, QEvent::MouseButtonRelease, moved, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();

    // The fixture is at one to one, so what the pointer moved on the widget is
    // the translation. And only the translation: a fallback that had landed on
    // a handle instead would have scaled from the far corner and still moved
    // the box, which is the way this could pass while being wrong.
    CHECK_NEAR(canvas.transformValues().dx, 40.0, 1.0);
    CHECK_NEAR(canvas.transformValues().dy, 25.0, 1.0);
    CHECK_NEAR(canvas.transformValues().scale_x, 1.0, 1e-6);
    CHECK_NEAR(canvas.transformValues().scale_y, 1.0, 1e-6);
    CHECK_NEAR(canvas.transformValues().rotation, 0.0, 1e-9);

    canvas.cancelTransform();
}

void theBoxCursorsTurnWithTheBox() {
    TEST("a rotated box stretches sideways where it used to stretch upwards");
    BoxFixture box;
    CanvasWidget& canvas = box.f.canvas;
    CHECK(canvas.transformIsLive());

    animage::Transform turned = canvas.transformValues();
    turned.rotation = 90.0;
    canvas.setTransformValues(turned);

    // The top edge's handle, wherever a quarter turn has put it. What it names
    // is the direction the hand drags in, and that followed the box round.
    const std::array<QPointF, 8> handles = canvas.transformHandlesForTesting();
    hover(&canvas, handles[1]);
    CHECK_EQ(shapeOf(&canvas), shape(Qt::SizeHorCursor));
    hover(&canvas, handles[3]);
    CHECK_EQ(shapeOf(&canvas), shape(Qt::SizeVerCursor));
    // A corner's diagonal swaps with it.
    hover(&canvas, handles[0]);
    CHECK_EQ(shapeOf(&canvas), shape(Qt::SizeBDiagCursor));

    canvas.cancelTransform();
}

// Issue #4. The canvas was a crosshair whichever tool was up, so nothing on
// screen said the eraser was -- including when it came up because the pen had
// been turned over.
void theEraserSaysSoBeforeYouDraw() {
    TEST("the eraser puts its own glyph where the crosshair was");
    Fixture f;
    f.draw(200.0f, 200.0f, 600.0f, 400.0f);

    hover(&f.canvas, QPointF(400.0, 300.0));
    CHECK_EQ(pointingOf(f.canvas), pointing(CanvasWidget::Pointing::Draw));
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::CrossCursor));

    // Picking the tool is enough: the pointer does not have to move for the
    // canvas to say which tool is now under it.
    f.canvas.setTool(CanvasWidget::Tool::Eraser);
    CHECK_EQ(pointingOf(f.canvas), pointing(CanvasWidget::Pointing::Erase));
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::BitmapCursor));

    // In place of the crosshair and not beside it, and nothing is drawn on the
    // canvas for it. A ring at the tool's radius was the first version: it
    // trailed the pointer, because a widget paints a frame after the hardware
    // has moved, and two marks under one hand read as two pointers.
    CHECK(!f.canvas.toolRing().has_value());

    f.canvas.setTool(CanvasWidget::Tool::Brush);
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::CrossCursor));
}

void turningThePenOverShowsTheEraser() {
    TEST("turning the pen over shows the eraser before it touches the tablet");
    Fixture f;
    QPointingDevice rubber(QStringLiteral("test eraser"), 2, QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Eraser,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                           1, 0);

    // Hovering, with nothing pressed: the whole point is that this is visible
    // before the stroke that would otherwise be how you found out.
    const QPointF at(500.0, 300.0);
    QTabletEvent hovering(QEvent::TabletMove, &rubber, at, f.canvas.mapToGlobal(at), 0.0, 0, 0, 0,
                          0, 0, Qt::NoModifier, Qt::NoButton, Qt::NoButton);
    QCoreApplication::sendEvent(&f.canvas, &hovering);
    QCoreApplication::processEvents();

    CHECK(!f.canvas.isErasing());  // the tool button was never touched
    CHECK_EQ(pointingOf(f.canvas), pointing(CanvasWidget::Pointing::Erase));
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::BitmapCursor));

    // And a real mouse says the pen has been put down, whichever way up it was.
    // The window that tells a promoted mouse event from a real one has to pass
    // first, which is what makes this a real mouse and not the pen's own echo.
    waitMs(300);
    hover(&f.canvas, QPointF(480.0, 300.0));
    CHECK_EQ(pointingOf(f.canvas), pointing(CanvasWidget::Pointing::Draw));
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::CrossCursor));
}

// Issue #5. The gesture already worked and the only feedback was a number in
// the toolbar, which is not where the hand is looking.
void theResizeGestureShowsWhatItIsSetting() {
    TEST("Alt and the right button dragged sideways shows the size it is setting");
    Fixture f;
    const QPointF anchor(500.0, 300.0);
    const double was = f.canvas.brushSettings().radius;

    sendMouseWith(&f.canvas, QEvent::MouseButtonPress, anchor, Qt::RightButton, Qt::RightButton,
                  Qt::AltModifier);
    CHECK_EQ(pointingOf(f.canvas), pointing(CanvasWidget::Pointing::SizeBrush));
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::SizeHorCursor));
    CHECK(f.canvas.toolRing().has_value());

    // And it is on the screen. The ring is the one piece of pointer feedback a
    // picture can catch, because the canvas draws it rather than the system --
    // which is also why it is anchored: what we draw arrives a frame late, and
    // only something holding still can afford that.
    const QImage while_sizing = f.render();

    sendMouseWith(&f.canvas, QEvent::MouseMove, anchor + QPointF(120.0, 40.0), Qt::NoButton,
                  Qt::RightButton, Qt::AltModifier);
    CHECK(f.canvas.brushSettings().radius > was);
    CHECK(f.render() != while_sizing);
    CHECK_NEAR(f.canvas.toolRing()->radius, f.canvas.brushSettings().radius * f.canvas.zoom(),
               1e-6);
    // Still where the drag started, not under the pointer. The pointer is
    // measuring a distance out from that point and the circle is what the
    // distance means; a ring that travelled with it would be the one thing on
    // screen not holding still to be compared against.
    CHECK_NEAR(f.canvas.toolRing()->at.x(), anchor.x(), 1e-6);
    CHECK_NEAR(f.canvas.toolRing()->at.y(), anchor.y(), 1e-6);

    sendMouseWith(&f.canvas, QEvent::MouseButtonRelease, anchor + QPointF(120.0, 40.0),
                  Qt::RightButton, Qt::NoButton, Qt::AltModifier);
    // Alt is still down, so what is offered now is the eyedropper -- which is
    // the other press with nothing on screen to announce it.
    CHECK_EQ(pointingOf(f.canvas), pointing(CanvasWidget::Pointing::Pick));
    CHECK(!f.canvas.toolRing().has_value());

    hover(&f.canvas, anchor);
    CHECK_EQ(pointingOf(f.canvas), pointing(CanvasWidget::Pointing::Draw));
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::CrossCursor));
}

// An old bug, reported while the canvas's pointer was being built: the timeline
// turned into a hand the moment the pointer entered it. The ruler is the first
// thing crossed coming down from the canvas and it claimed a pointing hand, so
// the hand that means "this drawing can be picked up" was the same hand as the
// one that meant nothing in particular.
void theTimelineIsAHandOnlyWhereADrawingCanBePickedUp() {
    TEST("the timeline is a hand only over a drawing that can be moved");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(timeline != nullptr);
    if (!timeline) return;

    // One drawing held over four frames, so there is a card, a held frame and
    // empty track beyond it -- three different answers in one row.
    Document& doc = window.documentForTesting();
    const TrackId track = doc.scene().tracks.front().id;
    doc.extendExposure(track, 0, 3);
    timeline->refresh();
    QCoreApplication::processEvents();

    // Coming in from the canvas, the ruler is what the pointer crosses first.
    // It scrubs; scrubbing is not picking anything up.
    hover(timeline, timeline->rulerPointForTesting(1));
    CHECK_EQ(shapeOf(timeline), shape(Qt::ArrowCursor));

    // The numbered card is the one thing here that can be moved.
    hover(timeline, timeline->cellCentreForTesting(0, 0));
    CHECK_EQ(shapeOf(timeline), shape(Qt::OpenHandCursor));

    // A held frame is the same drawing still showing. There is no second object
    // there to drag, and pressing it selects the frame and nothing more -- the
    // same test mousePressEvent makes before it allows a drag.
    hover(timeline, timeline->cellCentreForTesting(0, 2));
    CHECK_EQ(shapeOf(timeline), shape(Qt::ArrowCursor));

    // Past the end of the track there is nothing to pick up.
    hover(timeline, timeline->cellCentreForTesting(0, 9));
    CHECK_EQ(shapeOf(timeline), shape(Qt::ArrowCursor));

    // The strip of track names is the second thing that can be picked up, and
    // it is why the hand still means one thing rather than two: a drawing moves
    // along its track, a track moves up the stack, and both are "this can be
    // carried somewhere else".
    hover(timeline, QPointF(20.0, timeline->cellCentreForTesting(0, 0).y()));
    CHECK_EQ(shapeOf(timeline), shape(Qt::OpenHandCursor));

    // The end of the run still stretches the exposure, which is a different
    // gesture and says so.
    const QPoint last = timeline->cellCentreForTesting(0, 3);
    hover(timeline, QPointF(last.x() + 12.0, last.y()));
    CHECK_EQ(shapeOf(timeline), shape(Qt::SplitHCursor));
}

// The case the first version of that fix missed, reported: a track of drawings
// with no holds. slotAt clamps to the last slot, so past the end of the strip
// every question about "what is under the pointer" was answered with the last
// drawing -- and with anything held, the clamp lands mid-run and nothing shows.
void pastTheEndOfATrackThereIsNoCardToPickUp() {
    TEST("past the end of a track of single-frame drawings there is no card");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(timeline != nullptr);
    if (!timeline) return;

    // Two drawings, one frame each, and no holds anywhere.
    Document& doc = window.documentForTesting();
    const TrackId track = doc.scene().tracks.front().id;
    doc.insertImage(track, 1);
    timeline->refresh();
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().tracks.front().slots.size(), std::size_t{2});

    // Both cards can be picked up.
    hover(timeline, timeline->cellCentreForTesting(0, 0));
    CHECK_EQ(shapeOf(timeline), shape(Qt::OpenHandCursor));
    hover(timeline, timeline->cellCentreForTesting(0, 1));
    CHECK_EQ(shapeOf(timeline), shape(Qt::OpenHandCursor));

    // And the empty width past them cannot.
    const QPoint beyond = timeline->cellCentreForTesting(0, 6);
    hover(timeline, beyond);
    CHECK_EQ(shapeOf(timeline), shape(Qt::ArrowCursor));

    // Which is a claim about the press and not only about the picture: a drag
    // out here was picking up the last drawing and moving it. Dragging from
    // past the end to the front of the track must do nothing at all.
    const std::vector<ImageId> before = doc.scene().tracks.front().slots;
    sendMouse(timeline, QEvent::MouseButtonPress, QPointF(beyond), Qt::LeftButton,
              Qt::LeftButton);
    const QPoint front = timeline->cellCentreForTesting(0, 0);
    for (int i = 1; i <= 6; ++i) {
        const QPointF at = QPointF(beyond) + (QPointF(front) - QPointF(beyond)) * (i / 6.0);
        sendMouse(timeline, QEvent::MouseMove, at, Qt::NoButton, Qt::LeftButton);
    }
    sendMouse(timeline, QEvent::MouseButtonRelease, QPointF(front), Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    CHECK(doc.scene().tracks.front().slots == before);
}

// Restacking a track by dragging its name, which is the whole of the interface
// for it: there is no menu item and there are no buttons.
//
// Two things are worth driving through the real events rather than calling
// Document::moveTrack. The gesture has to survive a press that also *selects*
// the row it lands on -- selecting is what a press in the gutter did before
// this, and it still does -- and the caret is a boundary between rows while the
// model wants a position with the row taken out, which is exactly the
// arithmetic that is off by one in one direction only.
void draggingATracksNameRestacksIt() {
    TEST("dragging a track's name restacks it, and dropping it back does not");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(timeline != nullptr);
    if (!timeline) return;

    Document& doc = window.documentForTesting();
    const TrackId first = doc.scene().tracks.front().id;
    doc.addTrack("second");
    doc.addTrack("third");
    timeline->refresh();
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().tracks.size(), std::size_t{3});
    const TrackId second = doc.scene().tracks[1].id;
    const TrackId third = doc.scene().tracks[2].id;

    // Somewhere in the name strip of a row, which is the only place a track can
    // be picked up.
    const auto nameOfRow = [&](std::size_t row) {
        return QPointF(20.0, timeline->cellCentreForTesting(row, 0).y());
    };
    const auto dragRow = [&](std::size_t from, QPointF to) {
        const QPointF at = nameOfRow(from);
        sendMouse(timeline, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 6; ++i) {
            sendMouse(timeline, QEvent::MouseMove, at + (to - at) * (i / 6.0), Qt::NoButton,
                      Qt::LeftButton);
        }
        sendMouse(timeline, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        QCoreApplication::processEvents();
    };
    const auto order = [&] {
        std::vector<TrackId> ids;
        for (const Track& track : doc.scene().tracks) ids.push_back(track.id);
        return ids;
    };

    // The bottom row to the top, which is what putting a background behind a
    // character is.
    dragRow(2, nameOfRow(0) - QPointF(0.0, 20.0));
    CHECK((order() == std::vector<TrackId>{third, first, second}));
    // And the row you dragged is the one you are now editing, because pressing
    // it selected it.
    CHECK_EQ(timeline->track(), third);

    // Back down again, past the last row: the far end is the far end, not a
    // refusal, and this is the direction whose arithmetic differs.
    dragRow(0, nameOfRow(2) + QPointF(0.0, 20.0));
    CHECK((order() == std::vector<TrackId>{first, second, third}));

    // Dropped where it already was -- a drag that wandered and came back --
    // changes nothing and leaves nothing on the undo stack to be spent.
    const std::size_t depth = doc.undoDepth();
    dragRow(1, nameOfRow(1) + QPointF(0.0, 4.0));
    CHECK((order() == std::vector<TrackId>{first, second, third}));
    CHECK_EQ(doc.undoDepth(), depth);

    // A press with no drag in it still only selects, which is what the gutter
    // has always been for.
    const QPointF at = nameOfRow(2);
    sendMouse(timeline, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    sendMouse(timeline, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    CHECK((order() == std::vector<TrackId>{first, second, third}));
    CHECK_EQ(timeline->track(), third);
}

// The one piece of a layer drop that is ours: a drop indicator sits *between*
// two rows, and moveLayer counts the destination in the list with the dragged
// row already taken out of it. The two agree going up and differ by one going
// down, which is the shape of mistake that leaves a feature half working.
void whereADroppedRowLands() {
    TEST("a row dropped between two others lands where moveLayer counts it");
    // Four rows, so the boundaries between them are 0 to 4.
    CHECK_EQ(LayerList::destinationFor(0, 0, 4), 0);  // above itself: no move
    CHECK_EQ(LayerList::destinationFor(0, 1, 4), 0);  // below itself: no move
    CHECK_EQ(LayerList::destinationFor(0, 2, 4), 1);
    CHECK_EQ(LayerList::destinationFor(0, 4, 4), 3);  // to the very bottom

    CHECK_EQ(LayerList::destinationFor(3, 0, 4), 0);  // to the very top
    CHECK_EQ(LayerList::destinationFor(3, 3, 4), 3);  // above itself: no move
    CHECK_EQ(LayerList::destinationFor(3, 4, 4), 3);  // below itself: no move
    CHECK_EQ(LayerList::destinationFor(3, 2, 4), 2);

    // Off either end, and a list too short to reorder at all.
    CHECK_EQ(LayerList::destinationFor(1, -3, 4), 0);
    CHECK_EQ(LayerList::destinationFor(1, 40, 4), 3);
    CHECK_EQ(LayerList::destinationFor(0, 1, 1), 0);
}

// Restacking a layer by dragging its row, which replaced Move up and Move down.
//
// The gesture is driven at the seam rather than end to end, and that limitation
// is worth stating: Qt's drag and drop cannot be driven by synthetic events --
// a QDropEvent sent to the view or to its viewport reaches neither handler,
// because only the platform's drag manager delivers these -- so this calls what
// the drop reports and tests everything downstream of it. What the drop works
// out on the way in is pinned above.
//
// What it is really pinning is that the list changes nothing by itself: Qt's
// InternalMove would reorder the *items* and leave the document alone, so the
// panel would show a stack nothing composites until the next rebuild silently
// put it back.
void aLayerDroppedOnAnotherRowRestacksTheStack() {
    TEST("a layer dropped on another row restacks the stack, and the document says so");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    // No metaobject of its own -- see LayerList -- so it is found as what it
    // derives from and known to be what it is. The window has one tree.
    auto* layers = static_cast<LayerList*>(window.findChild<QTreeWidget*>());
    CHECK(layers != nullptr);
    if (!layers) return;
    CHECK(static_cast<bool>(layers->reordered));

    Document& doc = window.documentForTesting();
    const TrackId track = doc.scene().tracks.front().id;
    doc.addLayer(track, "clean", 0);
    doc.addLayer(track, "rough", 1);
    // The setup Qt's own half of the gesture needs, since that half cannot be
    // driven from here: the view drags and drops onto itself, and a row is
    // draggable without being somewhere to drop *onto* -- which is what keeps
    // the indicator to above and below.
    CHECK(layers->dragDropMode() == QAbstractItemView::InternalMove);
    CHECK(layers->dragEnabled());
    CHECK(layers->acceptDrops());
    // Through the button, so the panel is rebuilt the way it is in use.
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add layer")) button->click();
    }
    QCoreApplication::processEvents();

    const auto names = [&] {
        std::vector<std::string> out;
        for (const Layer& layer : doc.scene().findTrack(track)->layers) out.push_back(layer.name);
        return out;
    };
    const std::vector<std::string> before = names();
    CHECK_EQ(before.size(), std::size_t{4});
    if (before.size() != 4) return;
    CHECK_EQ(layers->topLevelItemCount(), 4);
    CHECK((layers->topLevelItem(0)->flags() & Qt::ItemIsDragEnabled) != 0);
    CHECK((layers->topLevelItem(0)->flags() & Qt::ItemIsDropEnabled) == 0);

    // A press on a row and a small move: Qt decides here that this press is
    // becoming a drag, and it is the last moment before the platform takes over.
    // Kept under QApplication::startDragDistance on purpose -- one pixel further
    // and startDrag runs, which blocks in a drag loop that offscreen has no way
    // to finish.
    const QRect row = layers->visualItemRect(layers->topLevelItem(0));
    sendMouse(layers->viewport(), QEvent::MouseButtonPress, QPointF(row.center()),
              Qt::LeftButton, Qt::LeftButton);
    sendMouse(layers->viewport(), QEvent::MouseMove, QPointF(row.center() + QPoint(0, 3)),
              Qt::NoButton, Qt::LeftButton);
    CHECK(layers->dragHasBegunForTesting());
    sendMouse(layers->viewport(), QEvent::MouseButtonRelease,
              QPointF(row.center() + QPoint(0, 3)), Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();

    // The top row to the bottom of the stack.
    layers->setCurrentItem(layers->topLevelItem(0));
    QCoreApplication::processEvents();
    layers->reordered(0, 3);
    std::vector<std::string> wanted(before.begin() + 1, before.end());
    wanted.push_back(before[0]);
    CHECK((names() == wanted));

    // The panel is a picture of the document and not a second copy of it, and
    // the row you were holding is the row you are still on -- picked by layer
    // rather than put back by index, which is what the buttons used to do.
    CHECK_EQ(layers->topLevelItem(3)->text(0).toStdString(), before[0]);
    CHECK_EQ(layers->indexOfTopLevelItem(layers->currentItem()), 3);

    // And back to the top, which is the other direction.
    layers->reordered(3, 0);
    CHECK((names() == before));

    // A drop that asks for where it already is, or for a row that is not there,
    // does nothing -- and leaves nothing on the undo stack to be spent by a
    // Ctrl+Z meant for the stroke before it.
    const std::size_t depth = doc.undoDepth();
    layers->reordered(1, 1);
    layers->reordered(0, 9);
    layers->reordered(-1, 2);
    CHECK((names() == before));
    CHECK_EQ(doc.undoDepth(), depth);
}

// Maximising the window frames the canvas in it.
//
// The canvas keeps its zoom and its pan across a resize, and the pan is the
// image point at the *top left* of the widget -- so a window made much bigger
// used to show the same drawing at the same size in the same corner, with new
// emptiness beside it.
void maximisingFramesTheCanvas() {
    TEST("maximising the window frames the canvas, and restoring frames it again");
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QCoreApplication::processEvents();
    // The fit the window opens with is queued behind the layout, so it has to be
    // let happen before anything here can be a change from it.
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    // What the state change is asked to produce, checked against a fit taken at
    // the size the canvas ends up with -- and not against the fit it had before,
    // which is a different window and so a different number.
    const auto framedNow = [&] {
        const double was = canvas->zoom();
        canvas->fitToCanvas();
        const double fitted = canvas->zoom();
        canvas->setZoom(was, QPointF(0.0, 0.0));
        return fitted;
    };

    CHECK(std::abs(canvas->zoom() - 1.0) > 1e-6);  // opened framed, not at one to one

    canvas->resetView();
    CHECK_NEAR(canvas->zoom(), 1.0, 1e-9);
    window.setWindowState(Qt::WindowMaximized);
    QCoreApplication::processEvents();  // the fit is queued, for the reason the
    QCoreApplication::processEvents();  // opening one is: the resize comes first
    CHECK(std::abs(canvas->zoom() - 1.0) > 1e-6);
    CHECK_NEAR(canvas->zoom(), framedNow(), 1e-9);

    // A resize that arrives *after* the state change reframes too, and this is
    // the half that a passing test used to say nothing about. The platform is
    // free to deliver the two in either order; offscreen it happens to send the
    // resize first, so a version that only asked `isMaximized()` on the resize
    // passed here and did nothing at all in the real window, where the widget is
    // resized before the window state is updated.
    canvas->resetView();
    CHECK_NEAR(canvas->zoom(), 1.0, 1e-9);
    QResizeEvent later(canvas->size(), canvas->size());
    QCoreApplication::sendEvent(canvas, &later);
    QCoreApplication::processEvents();
    CHECK_NEAR(canvas->zoom(), framedNow(), 1e-9);

    // Restoring frames it again rather than leaving the canvas fitted to a
    // window that has gone.
    canvas->resetView();
    CHECK_NEAR(canvas->zoom(), 1.0, 1e-9);
    window.setWindowState(Qt::WindowNoState);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    CHECK(std::abs(canvas->zoom() - 1.0) > 1e-6);
    CHECK_NEAR(canvas->zoom(), framedNow(), 1e-9);

    // And a resize long after one is an ordinary resize: dragging a dock about
    // must not put the view back on the canvas you had left.
    canvas->resetView();
    waitMs(600);  // past the moment a state change keeps the canvas following
    QResizeEvent unrelated(canvas->size(), canvas->size());
    QCoreApplication::sendEvent(canvas, &unrelated);
    QCoreApplication::processEvents();
    CHECK_NEAR(canvas->zoom(), 1.0, 1e-9);
}

// Renaming a layer by double-clicking its name.
//
// The gesture is opened by hand rather than by a double click, for the reason
// the drop above is driven at the seam: Qt's own double click is not the part
// that would be wrong. What would be wrong is what the editor opens on and what
// it does with what is typed, and both are here.
void doubleClickingALayerRenamesIt() {
    TEST("a layer is renamed in place, and the panel never edits itself");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* layers = static_cast<LayerList*>(window.findChild<QTreeWidget*>());
    CHECK(layers != nullptr);
    if (!layers) return;
    // A double click and nothing else. SelectedClicked -- which is in Qt's own
    // default set -- would start a rename on a plain click on the row you are
    // already drawing on.
    CHECK_EQ(static_cast<int>(layers->editTriggers()),
             static_cast<int>(QAbstractItemView::DoubleClicked));

    Document& doc = window.documentForTesting();
    const TrackId track = doc.scene().tracks.front().id;
    const LayerId layer = doc.scene().findTrack(track)->layers.front().id;
    const std::size_t depth = doc.undoDepth();

    // The gesture, through the viewport where a real click arrives.
    const QPointF row(layers->visualItemRect(layers->topLevelItem(0)).center());
    sendMouse(layers->viewport(), QEvent::MouseButtonPress, row, Qt::LeftButton, Qt::LeftButton);
    sendMouse(layers->viewport(), QEvent::MouseButtonRelease, row, Qt::LeftButton, Qt::NoButton);
    sendMouse(layers->viewport(), QEvent::MouseButtonDblClick, row, Qt::LeftButton,
              Qt::LeftButton);
    QCoreApplication::processEvents();

    auto* editor = layers->findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    if (!editor) return;
    CHECK_EQ(editor->text().toStdString(), std::string("layer 1"));

    // Return is Play. While a name is being typed into, every shortcut lets go
    // of the keyboard -- a disabled QAction does not consume its key, which is
    // the whole mechanism a live transform borrows Return with. Without this,
    // pressing Enter to finish a rename started playback instead, which is what
    // was reported.
    QAction* play = window.actionForTesting(shortcuts::Id::Play);
    CHECK(play != nullptr);
    if (!play) return;
    CHECK(!play->isEnabled());

    editor->setText(QStringLiteral("  rough  "));
    layers->finishRenameForTesting(true);
    settleEditors();

    // And it has them back afterwards. An editor closed by anything at all --
    // Return, Escape, a click elsewhere, the panel being rebuilt -- goes through
    // Qt's own closeEditor, so there is no path that leaves them switched off.
    CHECK(play->isEnabled());
    CHECK(layers->findChild<QLineEdit*>() == nullptr);

    // Trimmed, in the document, on the row, and undoable like anything else.
    CHECK_EQ(doc.scene().findTrack(track)->findLayer(layer)->name, std::string("rough"));
    CHECK_EQ(layers->topLevelItem(0)->text(0).toStdString(), std::string("rough"));
    CHECK(doc.undoDepth() > depth);
    doc.undo();
    CHECK_EQ(doc.scene().findTrack(track)->findLayer(layer)->name, std::string("layer 1"));

    // Escape leaves the name alone. This one is Qt's own -- the delegate emits
    // it straight out of its event filter rather than posting it -- so it is
    // driven with the key a hand would press.
    layers->renameRowForTesting(0);
    QCoreApplication::processEvents();
    editor = layers->findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    if (!editor) return;
    editor->setText(QStringLiteral("nonsense"));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &escape);
    settleEditors();
    CHECK_EQ(doc.scene().findTrack(track)->findLayer(layer)->name, std::string("layer 1"));
    CHECK(play->isEnabled());
    CHECK(layers->findChild<QLineEdit*>() == nullptr);

    // And the pen's way in: two taps close together, with no
    // MouseButtonDblClick anywhere, which is what a tablet event nobody accepts
    // is promoted to. Qt's DoubleClicked trigger never fires for these, so
    // double-tapping a name with a pen renamed nothing at all.
    //
    // Two taps a second apart first, because the property that matters is not
    // "two presses rename" -- that would make every second click on a layer a
    // rename -- but "two presses in quick succession" do.
    sendTap(layers->viewport(), row, 1000);
    sendTap(layers->viewport(), row, 2000);
    settleEditors();
    CHECK(layers->findChild<QLineEdit*>() == nullptr);

    sendTap(layers->viewport(), row, 3000);
    sendTap(layers->viewport(), row, 3080);
    CHECK(layers->findChild<QLineEdit*>() != nullptr);
    CHECK(!play->isEnabled());  // and it is a rename, with the keyboard to match
    layers->finishRenameForTesting(false);
    settleEditors();
    CHECK(play->isEnabled());

    // But two taps on the visibility tick are two taps on the visibility tick.
    // Flicking a layer off and on to compare is the most ordinary gesture in
    // this panel, and with the whole row live it would open a rename instead.
    const QRect item = layers->visualItemRect(layers->topLevelItem(0));
    const QPointF tick(layers->style()->pixelMetric(QStyle::PM_IndicatorWidth) / 2 + 3,
                       item.center().y());
    const bool visible_before = doc.scene().findTrack(track)->layers.front().visible;
    sendTap(layers->viewport(), tick, 5000);
    sendTap(layers->viewport(), tick, 5080);
    settleEditors();
    CHECK(layers->findChild<QLineEdit*>() == nullptr);
    CHECK(play->isEnabled());
    // Two toggles, so it is back where it started -- and it moved, which is what
    // says the taps reached the tick rather than being swallowed.
    CHECK_EQ(doc.scene().findTrack(track)->layers.front().visible, visible_before);
    CHECK(doc.undoDepth() > depth);
}

// The same editor, on a colour layer that is showing somebody else's marks.
//
// Its row does not say what the layer is called: it says "<- name", because the
// marks were made on another drawing. An editor seeded from the row would put
// that arrow into the name, and the next rename would put a second one in front
// of the first.
void renamingACarriedColourLayerLeavesTheArrowOutOfIt() {
    TEST("renaming a carried colour layer does not rename it after the arrow");
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
    auto* layers = static_cast<LayerList*>(window.findChild<QTreeWidget*>());
    CHECK(timeline != nullptr);
    CHECK(layers != nullptr);
    if (!timeline || !layers) return;

    // Slot 1 carries its colour from the drawing before it, which is what puts
    // the arrow on the row. The solve is on a worker, so the paint that asks for
    // it and the report that follows both have to happen first.
    timeline->setCurrentSlot(1);
    QCoreApplication::processEvents();
    window.grab();
    CHECK(window.waitForColour());
    QCoreApplication::processEvents();

    int row = -1;
    for (int i = 0; i < layers->topLevelItemCount(); ++i) {
        if (layers->topLevelItem(i)->text(0).contains(QStringLiteral("colour"))) row = i;
    }
    CHECK(row >= 0);
    if (row < 0) return;
    CHECK(layers->topLevelItem(row)->text(0).startsWith(QStringLiteral("←")));

    layers->renameRowForTesting(row);
    QCoreApplication::processEvents();
    auto* editor = layers->findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    if (!editor) return;
    // What the layer is called, and not what the row says.
    CHECK(!editor->text().startsWith(QStringLiteral("←")));

    editor->setText(QStringLiteral("skin"));
    layers->finishRenameForTesting(true);
    QCoreApplication::processEvents();

    Document& doc = window.documentForTesting();
    const Track* track = &doc.scene().tracks.front();
    bool found = false;
    for (const Layer& l : track->layers) {
        if (l.name == "skin") found = true;
    }
    CHECK(found);
    // And the arrow is still on the row, because it is not part of the name and
    // never was: the marks are still carried.
    CHECK_EQ(layers->topLevelItem(row)->text(0).toStdString(), std::string("← skin"));

    // An empty name is refused rather than accepted: a nameless layer has no row
    // label and no folder of its own in an export.
    layers->renameRowForTesting(row);
    QCoreApplication::processEvents();
    editor = layers->findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    if (!editor) return;
    editor->setText(QStringLiteral("   "));
    layers->finishRenameForTesting(true);
    QCoreApplication::processEvents();
    CHECK_EQ(layers->topLevelItem(row)->text(0).toStdString(), std::string("← skin"));
}

// Renaming a track by double-clicking its name in the gutter.
void doubleClickingATrackRenamesIt() {
    TEST("a track is renamed in place, and Escape leaves the name alone");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(timeline != nullptr);
    if (!timeline) return;

    Document& doc = window.documentForTesting();
    const TrackId track = doc.scene().tracks.front().id;
    CHECK_EQ(doc.scene().findTrack(track)->name, std::string("track 1"));

    // The gesture itself, on the name strip: press, release, double click, which
    // is the order the platform sends them in. Nothing here is Qt's own -- the
    // widget is painted and hit-tested by hand -- so this half is testable and
    // is what a hand actually does.
    const QPointF name(timeline->gutterPointForTesting(0));
    sendMouse(timeline, QEvent::MouseButtonPress, name, Qt::LeftButton, Qt::LeftButton);
    sendMouse(timeline, QEvent::MouseButtonRelease, name, Qt::LeftButton, Qt::NoButton);
    sendMouse(timeline, QEvent::MouseButtonDblClick, name, Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::processEvents();

    QLineEdit* editor = timeline->renameEditorForTesting();
    CHECK(editor != nullptr);
    if (!editor) return;
    CHECK(editor->isVisible());
    CHECK_EQ(editor->text().toStdString(), std::string("track 1"));

    // Return is Play, so every shortcut lets go of the keyboard while a name is
    // being typed into. Enter otherwise starts playback instead of finishing
    // the rename, which is what was reported.
    QAction* play = window.actionForTesting(shortcuts::Id::Play);
    CHECK(play != nullptr);
    if (!play) return;
    CHECK(!play->isEnabled());


    // editingFinished, which is what Enter and losing the editor both produce.
    editor->setText(QStringLiteral("  background  "));
    Q_EMIT editor->editingFinished();
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().findTrack(track)->name, std::string("background"));
    CHECK(!editor->isVisible());

    // Escape puts the editor away and leaves the name where it was. It arrives
    // as a key and not as a signal, which is the whole reason the widget filters
    // the editor at all.
    timeline->renameTrackForTesting(0);
    QCoreApplication::processEvents();
    editor->setText(QStringLiteral("nonsense"));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &escape);
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().findTrack(track)->name, std::string("background"));
    CHECK(!editor->isVisible());

    // And an empty name is refused, the same way the Rename track dialog
    // refuses one.
    timeline->renameTrackForTesting(0);
    QCoreApplication::processEvents();
    editor->setText(QStringLiteral("   "));
    Q_EMIT editor->editingFinished();
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().findTrack(track)->name, std::string("background"));
    CHECK(!editor->isVisible());
    CHECK(play->isEnabled());

    // And a double click out on the frames is not a rename. Out there a click
    // means "stand on this frame", and two of them mean it twice.
    sendMouse(timeline, QEvent::MouseButtonDblClick,
              QPointF(timeline->cellCentreForTesting(0, 0)), Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::processEvents();
    CHECK(!editor->isVisible());

    // The pen: two taps on the name, arriving as two ordinary presses with no
    // double click between them. Spaced out first, because two clicks a second
    // apart are two clicks.
    sendTap(timeline, name, 1000);
    sendTap(timeline, name, 2000);
    CHECK(!editor->isVisible());

    sendTap(timeline, name, 3000);
    sendTap(timeline, name, 3080);
    CHECK(editor->isVisible());
    CHECK_EQ(editor->text().toStdString(), std::string("background"));
    CHECK(!play->isEnabled());

    // A tap out on the frames is not one either, however quickly it follows.
    QKeyEvent leave(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &leave);
    QCoreApplication::processEvents();
    const QPointF frame(timeline->cellCentreForTesting(0, 0));
    sendTap(timeline, frame, 4000);
    sendTap(timeline, frame, 4080);
    CHECK(!editor->isVisible());
    CHECK(play->isEnabled());
}

// Return is Play, and Return is also how a number is entered into a field. The
// mechanism that settles that was built for renaming and wired to renaming, so
// every other field in the window still lost the argument: typing 42 into the
// brush size box and pressing Return played the animation and left the brush
// where it was. Reported from use.
//
// The window follows the focus now rather than being told by two editors, which
// is why this test names no editor.
void theKeyboardBelongsToAnyFieldItIsIn() {
    TEST("the keyboard belongs to any field it is in, not only to a rename");
    // With ink on it, because the transform half of this needs something to box.
    WindowWithInk fixture;
    CHECK(fixture.canvas != nullptr);
    if (!fixture.canvas) return;
    CanvasWidget* canvas = fixture.canvas;

    // Two actions name the mode between them, because no single one does: Play
    // is live in Normal alone, Actual size is live in Normal and Transform
    // (kAlways), and in Typing nothing is live at all.
    //
    //   Normal     Play on,  Actual size on
    //   Transform  Play off, Actual size on
    //   Typing     Play off, Actual size off
    QAction* play = fixture.action(shortcuts::Id::Play);
    QAction* looking = fixture.action(shortcuts::Id::ActualSize);
    CHECK(play != nullptr);
    CHECK(looking != nullptr);
    if (!play || !looking) return;

    QWidget* bar = fixture.window.findChild<QWidget*>(QStringLiteral("transformBar"));
    CHECK(bar != nullptr);
    if (!bar) return;

    // The toolbar's fields: the ones outside the transform bar, which is hidden
    // until a transform is live. Brush size and the onion count, at least.
    QList<QAbstractSpinBox*> toolbar_fields;
    for (QAbstractSpinBox* box : fixture.window.findChildren<QAbstractSpinBox*>()) {
        if (box->isVisible() && !bar->isAncestorOf(box)) toolbar_fields.append(box);
    }
    CHECK(toolbar_fields.size() >= 2);

    // The canvas has the keyboard, so Return is Play.
    canvas->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    CHECK(play->isEnabled());
    CHECK(looking->isEnabled());

    for (QAbstractSpinBox* field : toolbar_fields) {
        field->setFocus(Qt::MouseFocusReason);
        QCoreApplication::processEvents();
        // Typing: everything lets go, so Return falls through to the field. A
        // disabled QAction does not consume its shortcut, which is the whole
        // mechanism -- and before this, none of these fields ever got it: typing
        // a brush size and pressing Return played the animation instead.
        CHECK(!play->isEnabled());
        CHECK(!looking->isEnabled());

        canvas->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        CHECK(play->isEnabled());
        CHECK(looking->isEnabled());
    }

    // And coming back is asked for rather than assumed. With a transform live,
    // leaving one of its own number fields has to land in Transform and not in
    // Normal, or the nudge keys would go back to stepping frames.
    fixture.action(shortcuts::Id::Transform)->trigger();
    QCoreApplication::processEvents();
    CHECK(canvas->transformIsLive());
    CHECK(!play->isEnabled());  // Transform
    CHECK(looking->isEnabled());

    const QList<QAbstractSpinBox*> transform_fields = bar->findChildren<QAbstractSpinBox*>();
    CHECK(!transform_fields.isEmpty());
    if (transform_fields.isEmpty()) return;

    transform_fields.first()->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    CHECK(!play->isEnabled());  // Typing
    CHECK(!looking->isEnabled());

    canvas->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    CHECK(canvas->transformIsLive());
    CHECK(!play->isEnabled());  // Transform again, and not Normal
    CHECK(looking->isEnabled());
}

// The other half of "the keyboard belongs to the field it is in": getting it
// back out again.
//
// The canvas and the timeline are the only two widgets in the window that take
// focus on a click; everything else is deliberately Qt::NoFocus so that Space
// keeps panning and a button cannot take the pen. The cost of that was the same
// fact from the other side -- a widget that will not take the keyboard cannot
// take it away either -- so a number being typed into kept it through a click on
// the opacity slider or on the empty part of the layer panel, and a rename kept
// it through a click on another row. Both reported from use.
void aClickElsewhereTakesTheKeyboardBackFromAField() {
    TEST("a click on anything but the field takes the keyboard off it");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* layers = static_cast<LayerList*>(window.findChild<QTreeWidget*>());
    auto* canvas = window.findChild<CanvasWidget*>();
    auto* opacity = window.findChild<QSlider*>();
    QAction* play = window.actionForTesting(shortcuts::Id::Play);
    CHECK(layers != nullptr);
    CHECK(canvas != nullptr);
    CHECK(opacity != nullptr);
    CHECK(play != nullptr);
    if (!layers || !canvas || !opacity || !play) return;

    // Two number fields, so that clicking from one to the other can be checked
    // as well: that one has to land in the second field and not on the canvas.
    QWidget* bar = window.findChild<QWidget*>(QStringLiteral("transformBar"));
    QList<QAbstractSpinBox*> fields;
    for (QAbstractSpinBox* box : window.findChildren<QAbstractSpinBox*>()) {
        if (box->isVisible() && !(bar && bar->isAncestorOf(box))) fields.append(box);
    }
    CHECK(fields.size() >= 2);
    if (fields.size() < 2) return;

    const auto clickOn = [](QWidget* target, QPointF at) {
        sendMouse(target, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        sendMouse(target, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
        QCoreApplication::processEvents();
    };

    // The opacity slider takes no focus, and used to leave the field holding the
    // keyboard -- with every shortcut still switched off, which is how a stuck
    // field became a window where nothing responded.
    fields.first()->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    CHECK(QApplication::focusWidget() == fields.first());
    CHECK(!play->isEnabled());

    clickOn(opacity, QPointF(opacity->rect().center()));
    CHECK(QApplication::focusWidget() == canvas);
    CHECK(play->isEnabled());

    // And the empty space below the rows of the layer panel, which selects
    // nothing and so had nothing to hand the keyboard back by accident.
    fields.first()->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    CHECK(!play->isEnabled());
    clickOn(layers->viewport(),
            QPointF(layers->viewport()->rect().center().x(), layers->viewport()->height() - 4));
    CHECK(QApplication::focusWidget() == canvas);
    CHECK(play->isEnabled());

    // A click on another field must not be answered by handing the keyboard to
    // the canvas. Qt is what moves it into the second field, and Qt's own
    // focus-on-click lives in the window's event handling, which a synthetic
    // press does not reach -- so what a test here can assert is the half this
    // code is responsible for: that it kept its hands off.
    //
    // On the line edit inside the box, which is where a real click lands. The
    // box holds the focus for it through a proxy, so the line edit itself
    // reports NoFocus -- and asking only about the widget under the pointer is
    // exactly how this rule went wrong first.
    auto* inside = fields.at(1)->findChild<QLineEdit*>();
    CHECK(inside != nullptr);
    if (!inside) return;
    fields.first()->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    clickOn(inside, QPointF(inside->rect().center()));
    CHECK(QApplication::focusWidget() != canvas);
    CHECK(!play->isEnabled());  // still in a field, wherever Qt put it

    // And a click inside the field it is already in leaves it alone.
    auto* own = fields.first()->findChild<QLineEdit*>();
    CHECK(own != nullptr);
    if (!own) return;
    fields.first()->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    clickOn(own, QPointF(own->rect().center()));
    CHECK(QApplication::focusWidget() == fields.first());
    CHECK(!play->isEnabled());

    // And the pen, which is never assumed here. See issue #53: a widget with no
    // tabletEvent gets the pen only as a promoted mouse event, and promotion
    // happens in the platform layer where no test can reach it. So this drives
    // the tablet press *directly*, which is the half that is ours: whichever of
    // the two arrives, the rule answers it.
    fields.first()->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    CHECK(!play->isEnabled());
    {
        QPointingDevice stylus(QStringLiteral("test stylus"), 1, QInputDevice::DeviceType::Stylus,
                               QPointingDevice::PointerType::Pen,
                               QInputDevice::Capability::Position |
                                   QInputDevice::Capability::Pressure,
                               1, 0);
        const QPointF at(layers->viewport()->rect().center());
        QTabletEvent tap(QEvent::TabletPress, &stylus, at, layers->viewport()->mapToGlobal(at),
                         1.0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
        QCoreApplication::sendEvent(layers->viewport(), &tap);
        QCoreApplication::processEvents();
    }
    CHECK(QApplication::focusWidget() == canvas);
    CHECK(play->isEnabled());

    // The rename half of the same report: an editor open on one row, a click on
    // another. The editor used to stay open on the first row while the second
    // became the selected layer.
    canvas->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    Document& doc = window.documentForTesting();
    const TrackId track = doc.scene().tracks.front().id;
    doc.addLayer(track, "layer 2");
    // Through the window, so the panel is rebuilt from the document.
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add colour layer")) button->click();
    }
    QCoreApplication::processEvents();
    CHECK(layers->topLevelItemCount() >= 2);
    if (layers->topLevelItemCount() < 2) return;

    layers->renameRowForTesting(0);
    QCoreApplication::processEvents();
    QLineEdit* editor = layers->findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    if (!editor) return;
    editor->setText(QStringLiteral("inked"));
    CHECK(!play->isEnabled());

    clickOn(layers->viewport(),
            QPointF(layers->visualItemRect(layers->topLevelItem(1)).center()));
    settleEditors();

    // Gone, and the name is what it said: losing the editor to a click somewhere
    // else means the same as Return, which is what it has always meant. Asked of
    // the track rather than of one layer id, because which layer row 0 shows is
    // the panel's business and not this test's.
    CHECK(layers->findChild<QLineEdit*>() == nullptr);
    bool renamed = false;
    for (const Layer& l : doc.scene().findTrack(track)->layers) {
        if (l.name == "inked") renamed = true;
    }
    CHECK(renamed);
    CHECK(play->isEnabled());
}

// Giving a rename up rather than finishing it. Escape already does this on both
// panels; this is the same act asked for by name, because a window on its way
// out has to be able to do it to an editor that is still open. See issue #51.
void aRenameGivenUpKeepsTheNameAndTheKeyboard() {
    TEST("a rename given up keeps the name and hands the keyboard back");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* layers = static_cast<LayerList*>(window.findChild<QTreeWidget*>());
    auto* timeline = window.findChild<TimelineWidget*>();
    QAction* play = window.actionForTesting(shortcuts::Id::Play);
    CHECK(layers != nullptr);
    CHECK(timeline != nullptr);
    CHECK(play != nullptr);
    if (!layers || !timeline || !play) return;

    Document& doc = window.documentForTesting();
    const TrackId track = doc.scene().tracks.front().id;
    const LayerId layer = doc.scene().findTrack(track)->layers.front().id;
    const std::size_t depth = doc.undoDepth();

    layers->renameRowForTesting(0);
    QCoreApplication::processEvents();
    QLineEdit* editor = layers->findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    if (!editor) return;
    CHECK(!play->isEnabled());
    editor->setText(QStringLiteral("nonsense"));
    layers->abandonRename();
    settleEditors();

    // The name is where it was, the editor is gone, the keyboard is back, and
    // nothing reached the undo stack -- which is the whole difference between
    // giving a rename up and finishing it.
    CHECK_EQ(doc.scene().findTrack(track)->findLayer(layer)->name, std::string("layer 1"));
    CHECK(layers->findChild<QLineEdit*>() == nullptr);
    CHECK(play->isEnabled());
    CHECK_EQ(doc.undoDepth(), depth);

    // And the other editor, on the other panel, which is a separate mechanism
    // and needs the same answer.
    timeline->renameTrackForTesting(0);
    QCoreApplication::processEvents();
    QLineEdit* gutter = timeline->renameEditorForTesting();
    CHECK(gutter != nullptr);
    if (!gutter) return;
    CHECK(!play->isEnabled());
    gutter->setText(QStringLiteral("nonsense"));
    timeline->abandonRename();
    QCoreApplication::processEvents();

    CHECK_EQ(doc.scene().findTrack(track)->name, std::string("track 1"));
    CHECK(!gutter->isVisible());
    CHECK(play->isEnabled());
    CHECK_EQ(doc.undoDepth(), depth);
}

// Issue #51, which was red on three of the five CI jobs for eight runs.
//
// A window is destroyed from the top down: ~MainWindow's body runs, then every
// member of MainWindow is destroyed -- the document among them -- and only then
// does ~QWidget destroy the children. Destroying a rename editor is what
// *finishes* a rename, so an editor still open reported itself from down there:
// renameLayer on a document that had already been destroyed, setTyping on a
// window that was no longer a window.
//
// **What this asserts is the sanitizers'.** Once the window is gone there is
// nothing left to look at, so the value of the test is that the path runs at
// all: on an unfixed tree it is a UBSan report under the `sanitizers` job and a
// segfault on Windows and macOS. The checks before each window dies are there
// so that a test which has stopped opening an editor cannot pass quietly.
//
// One window each, because the two editors cannot be open at once -- opening
// one takes the focus off the other, which is what finishes it -- and because
// they are two different routes home: the layer panel calls a std::function
// that captured the window, the timeline emits a signal connected to it. CI
// reported one of each, on different runs.
void aWindowDestroyedMidRenameGivesItUp() {
    TEST("a window destroyed with a rename still open does not report it");
    {
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QCoreApplication::processEvents();

        auto* layers = static_cast<LayerList*>(window.findChild<QTreeWidget*>());
        CHECK(layers != nullptr);
        if (!layers) return;
        layers->renameRowForTesting(0);
        QCoreApplication::processEvents();
        QLineEdit* editor = layers->findChild<QLineEdit*>();
        CHECK(editor != nullptr);
        if (!editor) return;
        // Typed into, so there is something to commit if anything still tries.
        editor->setText(QStringLiteral("nonsense"));
    }
    QCoreApplication::processEvents();

    {
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QCoreApplication::processEvents();

        auto* timeline = window.findChild<TimelineWidget*>();
        CHECK(timeline != nullptr);
        if (!timeline) return;
        timeline->renameTrackForTesting(0);
        QCoreApplication::processEvents();
        QLineEdit* editor = timeline->renameEditorForTesting();
        CHECK(editor != nullptr);
        if (!editor) return;
        CHECK(editor->isVisible());
        editor->setText(QStringLiteral("nonsense"));
    }
    QCoreApplication::processEvents();
}

// The same rule as the test above, swept across every state that has something
// to say when it is interrupted -- because #51 was not really about renaming.
// It was about a window being destroyed from the top down while its children
// were still able to talk to it, and a rename editor was only the first child
// found doing it. A focused spin box was the second, and nothing had been
// looking for a third.
//
// **These assert almost nothing themselves, and that is the point.** The checks
// here are that each state was really reached, so a case that has quietly
// stopped setting anything up cannot pass by doing nothing; what happens after
// each window dies is the sanitizers' to judge, and the `sanitizers` job is what
// runs them. Adding a case is three lines, which is the only reason anybody will
// add one.
void aWindowIsDestroyedSafelyFromAnyState() {
    TEST("a window destroyed in the middle of something reports none of it");

    // The visible number fields, which is how the toolbar's are told apart from
    // the transform bar's: the transform bar is hidden until a transform is
    // live. `transformBar` is the one object name in the window, and it is here
    // rather than a guess at creation order.
    const auto visibleBoxesOutsideTheTransformBar = [](MainWindow& window) {
        QList<QWidget*> found;
        QWidget* bar = window.findChild<QWidget*>(QStringLiteral("transformBar"));
        for (QWidget* box : window.findChildren<QAbstractSpinBox*>()) {
            if (!box->isVisible()) continue;
            if (bar && bar->isAncestorOf(box)) continue;
            found.append(box);
        }
        return found;
    };

    struct Interruption {
        const char* what;
        // True if the state was really reached. False fails the test by name.
        std::function<bool(WindowWithInk&)> reach;
    };

    const std::vector<Interruption> cases = {
        {"the keyboard in a number field on the toolbar",
         [&](WindowWithInk& fixture) {
             const QList<QWidget*> boxes = visibleBoxesOutsideTheTransformBar(fixture.window);
             if (boxes.isEmpty()) return false;
             boxes.first()->setFocus(Qt::MouseFocusReason);
             QCoreApplication::processEvents();
             return QApplication::focusWidget() == boxes.first();
         }},

        {"a live transform, with the keyboard in one of its fields",
         [](WindowWithInk& fixture) {
             QAction* transform = fixture.action(shortcuts::Id::Transform);
             if (!transform) return false;
             transform->trigger();
             QCoreApplication::processEvents();
             QWidget* bar = fixture.window.findChild<QWidget*>(QStringLiteral("transformBar"));
             if (!bar || !fixture.canvas->transformIsLive()) return false;
             const QList<QAbstractSpinBox*> boxes = bar->findChildren<QAbstractSpinBox*>();
             if (boxes.isEmpty()) return false;
             boxes.first()->setFocus(Qt::MouseFocusReason);
             QCoreApplication::processEvents();
             return QApplication::focusWidget() == boxes.first();
         }},

        {"a stroke the pen has not been lifted from",
         [](WindowWithInk& fixture) {
             QPointingDevice stylus(QStringLiteral("test stylus"), 1,
                                    QInputDevice::DeviceType::Stylus,
                                    QPointingDevice::PointerType::Pen,
                                    QInputDevice::Capability::Position |
                                        QInputDevice::Capability::Pressure,
                                    1, 0);
             const QPointF at(400.0, 300.0);
             QTabletEvent press(QEvent::TabletPress, &stylus, at,
                                fixture.canvas->mapToGlobal(at), 1.0, 0, 0, 0, 0, 0,
                                Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
             QCoreApplication::sendEvent(fixture.canvas, &press);
             QCoreApplication::processEvents();
             // And no release: the window goes with the command still open.
             return fixture.canvas->isStroking();
         }},

        {"a colour solve still running on its worker",
         [](WindowWithInk& fixture) {
             Document& doc = fixture.doc();
             const TrackId track = doc.scene().tracks.front().id;
             const LayerId ink = doc.scene().findTrack(track)->layers.front().id;
             const LayerId colour = doc.addLayer(track, "colour", 1, LayerKind::Ctg);
             Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
             settings.ctg_sources = {ink};
             doc.updateLayer(track, colour, settings);
             // A scribble for the solver to spread, on the drawing the canvas is
             // standing on.
             {
                 ScopedCommand command(doc, "Scribble");
                 BrushSettings brush_settings;
                 brush_settings.radius = 8.0f;
                 brush_settings.hardness = 0.95f;
                 brush_settings.pressure_affects_opacity = false;
                 brush_settings.r = 1.0f;
                 brush_settings.a = 1.0f;
                 Brush brush(brush_settings);
                 brush.begin(doc, track, fixture.canvas->currentImage(), colour,
                             {340.0f, 320.0f, 1.0f});
                 brush.extend({360.0f, 330.0f, 1.0f});
                 brush.end();
             }
             fixture.canvas->refreshAll();
             // The paint is what asks for the solve; nothing waits for it.
             fixture.canvas->grab();
             return fixture.canvas->colourPending();
         }},
    };

    for (const Interruption& one : cases) {
        WindowWithInk fixture;
        if (!fixture.canvas) {
            testing::fail(__FILE__, __LINE__, std::string("no canvas for: ") + one.what);
            continue;
        }
        const bool reached = one.reach(fixture);
        ++testing::g_checks;
        if (!reached) {
            testing::fail(__FILE__, __LINE__,
                          std::string("never reached the state: ") + one.what);
        }
        // And here the fixture goes out of scope, taking the window with it.
    }
    QCoreApplication::processEvents();
}

// The layer panel says which track's layers it is showing.
//
// It said "Layer" under a dock titled "Layers" -- the same word twice and no
// information -- while the fact worth having is that another track's layers can
// be called exactly the same thing. The status bar had it all along, eight
// readings along and the width of the window away from the rows it qualifies.
void theLayerPanelSaysWhichTrackItIsShowing() {
    TEST("the layer panel is headed with the track whose layers it is showing");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* layers = static_cast<LayerList*>(window.findChild<QTreeWidget*>());
    auto* timeline = window.findChild<TimelineWidget*>();
    CHECK(layers != nullptr);
    CHECK(timeline != nullptr);
    if (!layers || !timeline) return;

    const auto heading = [&] { return layers->headerItem()->text(0).toStdString(); };

    Document& doc = window.documentForTesting();
    const TrackId first = doc.scene().tracks.front().id;
    CHECK_EQ(heading(), std::string("track 1"));
    // And it says what it is, for a name too long for the column and for a name
    // that could be read as one of the rows beneath it.
    CHECK_EQ(layers->headerItem()->toolTip(0).toStdString(),
             std::string("Layers of \"track 1\""));

    // A second track, which is the case the panel could not distinguish: both
    // tracks start with a layer called "layer 1".
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("Add track")) action->trigger();
    }
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().tracks.size(), std::size_t(2));
    if (doc.scene().tracks.size() != 2) return;
    const TrackId second = doc.scene().tracks.back().id;
    CHECK_EQ(heading(), std::string("track 2"));
    CHECK_EQ(layers->topLevelItem(0)->text(0).toStdString(), std::string("layer 1"));

    // Back to the first, the way clicking its row does it.
    timeline->setTrack(first);
    QCoreApplication::processEvents();
    CHECK_EQ(heading(), std::string("track 1"));
    // Same rows, different track: which is the whole reason the heading matters.
    CHECK_EQ(layers->topLevelItem(0)->text(0).toStdString(), std::string("layer 1"));

    // Renaming the track follows, by the route the gutter editor takes.
    timeline->renameTrackForTesting(0);
    QCoreApplication::processEvents();
    QLineEdit* editor = timeline->renameEditorForTesting();
    CHECK(editor != nullptr);
    if (!editor) return;
    editor->setText(QStringLiteral("rough"));
    Q_EMIT editor->editingFinished();
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().findTrack(first)->name, std::string("rough"));
    CHECK_EQ(heading(), std::string("rough"));
    CHECK_EQ(layers->headerItem()->toolTip(0).toStdString(), std::string("Layers of \"rough\""));

    // And by the route the Track menu's dialog takes, which reaches the panel
    // through refreshEverything rather than through documentChanged.
    TrackProperties props = doc.scene().findTrack(second)->properties();
    props.name = "colour pass";
    doc.updateTrack(second, props);
    timeline->setTrack(second);
    QCoreApplication::processEvents();
    CHECK_EQ(heading(), std::string("colour pass"));

    // Undo puts the name back, and the heading with it. Through the action, so
    // the refresh is the one the program actually does.
    QAction* undo = window.actionForTesting(shortcuts::Id::Undo);
    CHECK(undo != nullptr);
    if (!undo) return;
    undo->trigger();
    QCoreApplication::processEvents();
    CHECK_EQ(doc.scene().findTrack(second)->name, std::string("track 2"));
    CHECK_EQ(heading(), std::string("track 2"));
}

// Issue #12. A colour layer goes to the bottom of the stack, so in a track with
// enough layers to need a scrollbar the layer you have just made is off the end
// of the panel -- and the list scrolled itself against the panel as it stood
// before the Colour layer box appeared underneath and took half of its height.
void aNewColourLayerIsInViewWhenItArrives() {
    TEST("a new colour layer is in view the moment it is made");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* layers = window.findChild<QTreeWidget*>();
    CHECK(layers != nullptr);
    if (!layers) return;

    Document& doc = window.documentForTesting();
    const TrackId track = doc.scene().tracks.front().id;
    for (int i = 0; i < 60; ++i) doc.addLayer(track, "layer " + std::to_string(i + 2), 0);

    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Add colour layer")) button->click();
    }
    QCoreApplication::processEvents();

    // The list is worth scrolling at all, or this would pass on a panel that
    // shows every layer whatever it does.
    CHECK(layers->verticalScrollBar()->maximum() > 0);

    QTreeWidgetItem* current = layers->currentItem();
    CHECK(current != nullptr);
    if (!current) return;
    CHECK_EQ(layers->indexOfTopLevelItem(current), layers->topLevelItemCount() - 1);

    // In view now, in one step, and not somewhere a later scroll would have to
    // reach: the rect the view gives for the row has to be inside the viewport.
    const QRect row = layers->visualItemRect(current);
    CHECK(layers->viewport()->rect().contains(row.center()));
}

void theHandDoesNotGetStuckClosed() {
    TEST("the hand opens again after a pan and goes away with the key");
    Fixture f;
    hover(&f.canvas, QPointF(400.0, 300.0));
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::CrossCursor));

    QKeyEvent space_down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.canvas, &space_down);
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::OpenHandCursor));

    sendMouse(&f.canvas, QEvent::MouseButtonPress, QPointF(400.0, 300.0), Qt::LeftButton,
              Qt::LeftButton);
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::ClosedHandCursor));
    sendMouse(&f.canvas, QEvent::MouseMove, QPointF(340.0, 260.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(&f.canvas, QEvent::MouseButtonRelease, QPointF(340.0, 260.0), Qt::LeftButton,
              Qt::NoButton);
    QCoreApplication::processEvents();
    // Open again, because Space is still down. This is the chain that used to
    // be written out by hand at three call sites.
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::OpenHandCursor));

    QKeyEvent space_up(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.canvas, &space_up);
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::CrossCursor));

    // And a held zoom key underneath it comes back rather than being forgotten.
    QKeyEvent zoom_down(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.canvas, &zoom_down);
    QCoreApplication::sendEvent(&f.canvas, &space_down);
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::OpenHandCursor));
    QCoreApplication::sendEvent(&f.canvas, &space_up);
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::SizeHorCursor));
    QKeyEvent zoom_up(QEvent::KeyRelease, Qt::Key_Z, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.canvas, &zoom_up);
    CHECK_EQ(shapeOf(&f.canvas), shape(Qt::CrossCursor));
}

// --- straight lines -------------------------------------------------------
//
// Shift held when the pen lands makes the stroke a straight line from there to
// wherever it lifts, at whatever angle -- which is the whole point, and is why
// every one of these drags at an oblique angle rather than along an axis.
//
// The canvas is at zoom 1 and pan 0, so a widget point is an image point.

// The detour is the assertion. A gesture that goes a long way somewhere the line
// does not, and comes back, is the only shape that can tell "straight" from
// "the hand happened to be steady".
void shiftDrawsAStraightLineAtAnyAngle() {
    TEST("Shift draws a straight line at whatever angle, ignoring the path taken");
    Fixture f;

    const QPointF anchor(200.0, 200.0);
    const QPointF detour(200.0, 600.0);
    const QPointF end(600.0, 500.0);  // slope 3/4: neither an axis nor a diagonal
    const std::size_t before = f.doc.undoDepth();  // the fixture's own setup steps

    sendMouseWith(&f.canvas, QEvent::MouseButtonPress, anchor, Qt::LeftButton, Qt::LeftButton,
                  Qt::ShiftModifier);
    CHECK(f.canvas.isStroking());
    CHECK(f.canvas.isAimingALine());

    sendMouseWith(&f.canvas, QEvent::MouseMove, detour, Qt::NoButton, Qt::LeftButton,
                  Qt::ShiftModifier);
    // Nothing at all is written while the line is being aimed -- not even the
    // dab under the anchor, which is what stops a frame change mid-gesture
    // leaving a dot on the drawing the line was aimed from.
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 200, 200) <= 0.0f);
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 200, 600) <= 0.0f);

    sendMouseWith(&f.canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton,
                  Qt::ShiftModifier);
    sendMouseWith(&f.canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton,
                  Qt::ShiftModifier);
    CHECK(!f.canvas.isStroking());
    CHECK(!f.canvas.isAimingALine());

    // On the line, at both ends and across the middle.
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 200, 200) > 0.0f);
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 400, 350) > 0.0f);
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 560, 470) > 0.0f);

    // And nowhere the hand actually went. 160 and 320 pixels off the line
    // respectively, which no brush radius could account for.
    CHECK_EQ(alphaAt(f.doc, f.track, f.image, f.layer, 200, 400), 0.0f);
    CHECK_EQ(alphaAt(f.doc, f.track, f.image, f.layer, 200, 600), 0.0f);

    // One gesture, one undo step, and undoing takes the whole line off.
    CHECK_EQ(f.doc.undoDepth(), before + 1);
    CHECK(f.doc.undo());
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 400, 350) <= 0.0f);
}

// The control, and it is worth having explicitly: without Shift the same three
// points leave the path the hand took, which is what the constraint is being
// measured against.
void withoutShiftTheSameGestureFollowsTheHand() {
    TEST("without Shift the same gesture leaves the path the hand took");
    Fixture f;

    sendMouse(&f.canvas, QEvent::MouseButtonPress, QPointF(200.0, 200.0), Qt::LeftButton,
              Qt::LeftButton);
    CHECK(!f.canvas.isAimingALine());
    sendMouse(&f.canvas, QEvent::MouseMove, QPointF(200.0, 600.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(&f.canvas, QEvent::MouseMove, QPointF(600.0, 500.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(&f.canvas, QEvent::MouseButtonRelease, QPointF(600.0, 500.0), Qt::LeftButton,
              Qt::NoButton);
    QCoreApplication::processEvents();

    // Down the detour, which the constrained version leaves bare...
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 200, 400) > 0.0f);
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 200, 600) > 0.0f);
    // ...and not across the middle, which is where the line would have been.
    CHECK_EQ(alphaAt(f.doc, f.track, f.image, f.layer, 400, 350), 0.0f);
}

// Shift is a property of the gesture from the moment the pen lands, and it has
// to be: taking the constraint up half way would mean unstamping dabs that are
// already on the drawing, and the brush cannot lift one off.
void shiftIsDecidedWhenThePenLands() {
    TEST("Shift decides at the press and the gesture keeps that answer");
    Fixture f;

    // Let go of Shift half way: still a line, because the press said so.
    sendMouseWith(&f.canvas, QEvent::MouseButtonPress, QPointF(200.0, 200.0), Qt::LeftButton,
                  Qt::LeftButton, Qt::ShiftModifier);
    sendMouse(&f.canvas, QEvent::MouseMove, QPointF(200.0, 600.0), Qt::NoButton, Qt::LeftButton);
    CHECK(f.canvas.isAimingALine());
    sendMouse(&f.canvas, QEvent::MouseMove, QPointF(600.0, 500.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(&f.canvas, QEvent::MouseButtonRelease, QPointF(600.0, 500.0), Qt::LeftButton,
              Qt::NoButton);
    QCoreApplication::processEvents();
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 400, 350) > 0.0f);
    CHECK_EQ(alphaAt(f.doc, f.track, f.image, f.layer, 200, 400), 0.0f);

    // And reaching for it half way through a free stroke does not retro-fit one:
    // the detour is already on the drawing by then.
    Fixture g;
    sendMouse(&g.canvas, QEvent::MouseButtonPress, QPointF(200.0, 200.0), Qt::LeftButton,
              Qt::LeftButton);
    sendMouseWith(&g.canvas, QEvent::MouseMove, QPointF(200.0, 600.0), Qt::NoButton,
                  Qt::LeftButton, Qt::ShiftModifier);
    CHECK(!g.canvas.isAimingALine());
    sendMouseWith(&g.canvas, QEvent::MouseButtonRelease, QPointF(200.0, 600.0), Qt::LeftButton,
                  Qt::NoButton, Qt::ShiftModifier);
    QCoreApplication::processEvents();
    CHECK(alphaAt(g.doc, g.track, g.image, g.layer, 200, 400) > 0.0f);
}

// The claim the whole design rests on: Shift changes where the dabs go and
// nothing else whatever about the stroke. Asserted as bit equality against the
// same two endpoints drawn freehand, because "looks the same" is exactly what a
// pressure or a spacing that quietly changed would also do.
//
// Driven with the pen and at two different pressures, which the mouse path
// cannot pin at all -- a mouse reports 1.0 throughout, so a line that took its
// far-end weight from the wrong sample would agree with this by accident. The
// wrong sample is a real one and it is right there: a pen lifting reports
// pressure 0, so a line stamped from the *release* is a hairline that fades to
// nothing, and it is the last move this has to read instead.
void aStraightLineIsTheSameMarkAsTheFreehandOne() {
    TEST("a straight line is the same mark the same two endpoints leave freehand");
    Fixture f;
    const ImageId second = f.doc.insertImage(f.track, 1);

    const QPointF anchor(180.0, 260.0);
    const QPointF end(620.0, 430.0);
    const float landed = 0.3f;  // the nib touching down
    const float leaned = 0.9f;  // and the weight on it by the far end

    QPointingDevice stylus(QStringLiteral("test stylus"), 1, QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Pen,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                           1, 0);
    const auto pen = [&](QEvent::Type type, const QPointF& at, double pressure,
                         Qt::KeyboardModifiers modifiers, Qt::MouseButton button,
                         Qt::MouseButtons buttons) {
        QTabletEvent event(type, &stylus, at, f.canvas.mapToGlobal(at), pressure, 0, 0, 0, 0, 0,
                           modifiers, button, buttons);
        QCoreApplication::sendEvent(&f.canvas, &event);
        QCoreApplication::processEvents();
    };

    // Shift, by way of a detour that must leave no trace.
    pen(QEvent::TabletPress, anchor, landed, Qt::ShiftModifier, Qt::LeftButton, Qt::LeftButton);
    pen(QEvent::TabletMove, QPointF(300.0, 700.0), 0.6, Qt::ShiftModifier, Qt::NoButton,
        Qt::LeftButton);
    pen(QEvent::TabletMove, end, leaned, Qt::ShiftModifier, Qt::NoButton, Qt::LeftButton);
    pen(QEvent::TabletRelease, end, 0.0, Qt::ShiftModifier, Qt::LeftButton, Qt::NoButton);

    // The same two points at the same two pressures on the next drawing,
    // freehand and in one move.
    f.canvas.setFrame(1);
    CHECK_EQ(f.canvas.currentImage(), second);
    pen(QEvent::TabletPress, anchor, landed, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    pen(QEvent::TabletMove, end, leaned, Qt::NoModifier, Qt::NoButton, Qt::LeftButton);
    pen(QEvent::TabletRelease, end, 0.0, Qt::NoModifier, Qt::LeftButton, Qt::NoButton);

    const Cel* constrained = f.doc.celAt(f.track, f.image, f.layer);
    const Cel* freehand = f.doc.celAt(f.track, second, f.layer);
    CHECK(constrained != nullptr);
    CHECK(freehand != nullptr);
    if (!constrained || !freehand) return;

    const PixelRect box = animage::paintedBounds(constrained->tiles());
    const PixelRect theirs = animage::paintedBounds(freehand->tiles());
    CHECK(!box.isEmpty());
    CHECK(box.x == theirs.x && box.y == theirs.y && box.width == theirs.width &&
          box.height == theirs.height);

    long long differing = 0;
    for (int y = box.y; y < box.y + box.height; ++y) {
        for (int x = box.x; x < box.x + box.width; ++x) {
            if (constrained->pixel(x, y).a != freehand->pixel(x, y).a) ++differing;
        }
    }
    CHECK_EQ(differing, 0LL);
}

// A line that has not been let go of does not exist yet, so a frame change
// under one carries it to the new drawing whole. That is deliberately not what
// a free stroke does -- a free stroke leaves a piece of itself on every frame it
// passed over, which is how you sketch a moving point.
void aLineChangingFrameLandsOnOneDrawing() {
    TEST("a straight line held across a frame change lands whole on one drawing");
    Fixture f;
    const ImageId second = f.doc.insertImage(f.track, 1);

    sendMouseWith(&f.canvas, QEvent::MouseButtonPress, QPointF(200.0, 200.0), Qt::LeftButton,
                  Qt::LeftButton, Qt::ShiftModifier);
    sendMouseWith(&f.canvas, QEvent::MouseMove, QPointF(600.0, 500.0), Qt::NoButton,
                  Qt::LeftButton, Qt::ShiftModifier);
    f.canvas.setFrame(1);
    CHECK(f.canvas.isAimingALine());
    sendMouseWith(&f.canvas, QEvent::MouseButtonRelease, QPointF(600.0, 500.0), Qt::LeftButton,
                  Qt::NoButton, Qt::ShiftModifier);
    QCoreApplication::processEvents();

    CHECK(alphaAt(f.doc, f.track, second, f.layer, 400, 350) > 0.0f);
    // And the drawing it was aimed from is untouched: no anchor dab, nothing.
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 200, 200) <= 0.0f);
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 400, 350) <= 0.0f);
}

// The eraser and the brush are one path, so this is a check that the branch was
// put where both of them go through it rather than beside the brush's settings.
void shiftErasesInAStraightLineToo() {
    TEST("Shift constrains the eraser as well as the brush");
    Fixture f;
    f.draw(150.0f, 150.0f, 650.0f, 550.0f);  // something to rub out
    CHECK(alphaAt(f.doc, f.track, f.image, f.layer, 400, 350) > 0.0f);

    f.canvas.setTool(CanvasWidget::Tool::Eraser);
    f.canvas.brushSettings().radius = 30.0f;
    // Along the ink, by way of a detour well off it.
    sendMouseWith(&f.canvas, QEvent::MouseButtonPress, QPointF(200.0, 190.0), Qt::LeftButton,
                  Qt::LeftButton, Qt::ShiftModifier);
    sendMouseWith(&f.canvas, QEvent::MouseMove, QPointF(600.0, 190.0), Qt::NoButton,
                  Qt::LeftButton, Qt::ShiftModifier);
    sendMouseWith(&f.canvas, QEvent::MouseMove, QPointF(600.0, 510.0), Qt::NoButton,
                  Qt::LeftButton, Qt::ShiftModifier);
    sendMouseWith(&f.canvas, QEvent::MouseButtonRelease, QPointF(600.0, 510.0), Qt::LeftButton,
                  Qt::NoButton, Qt::ShiftModifier);
    QCoreApplication::processEvents();

    // The rubber went down the ink, not along the top of the drawing.
    CHECK_EQ(alphaAt(f.doc, f.track, f.image, f.layer, 400, 350), 0.0f);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::printf("canvas:\n");
    thePointerSaysWhatAPressOnTheBoxWillDo();
    aDragOutsideTheBoxMovesIt();
    theBoxCursorsTurnWithTheBox();
    theEraserSaysSoBeforeYouDraw();
    turningThePenOverShowsTheEraser();
    theResizeGestureShowsWhatItIsSetting();
    theHandDoesNotGetStuckClosed();
    shiftDrawsAStraightLineAtAnyAngle();
    withoutShiftTheSameGestureFollowsTheHand();
    shiftIsDecidedWhenThePenLands();
    aStraightLineIsTheSameMarkAsTheFreehandOne();
    aLineChangingFrameLandsOnOneDrawing();
    shiftErasesInAStraightLineToo();
    theTimelineIsAHandOnlyWhereADrawingCanBePickedUp();
    pastTheEndOfATrackThereIsNoCardToPickUp();
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
    backspaceErasesOnAColourLayerWhereCutRefuses();
    backspaceSaysWhyWhenItRefuses();
    theBrushRefusesWhereItAlwaysDid();
    pastTheEndOfATrackTheRefusalIsNotSaidTwice();
    aLoopIsDroppedWhenItCatchesNothingOnTheNewLayer();
    theSelectionSurvivesALayerChangeAndNotAFrameChange();
    theWindowTakesItsKeysFromTheTable();
    theTooltipsFollowTheKeys();
    theShortcutsPanelWillNotApplyACollision();
    theTransformKeysFollowTheirBindings();
    theFlipButtonsMirrorTheDrawing();
    aFlippedBoxStillScalesTheRightWay();
    theTransformToolTakesTheWholeDrawing();
    enteringATransformPutsTheOtherToolsDown();
    aLostReleaseDoesNotEndTheUndoHistory();
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
    aFailedSwapPutsThePreviousProjectBack();
    aSwapThatCannotBePutBackKeepsBothCopiesAndNamesThem();
    aRescueThatCannotBeMovedIsNamedWhereItIs();
    aSaveAfterTheFolderVanishedIsStillWhole();
    abrokenProjectDoesNotReplaceTheOpenOne();
    savingTwiceWritesTheSameBytes();
    anIncrementalSaveWritesTheSameProject();
    anIncrementalSaveReplacesWhatWentMissing();
    savingElsewhereCarriesNothingForward();
    openingLeavesTheFolderKnown();
    autosaveWritesOnlyWhenSomethingMoved();
    adifferentEditFromTheSameDepthIsUnsaved();
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
    anExportRefusesTwoLayersInOneFolder();
    anExportRefusesANameTooLongToWrite();
    aNameFieldStopsLongBeforeTheExportWould();
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
    aHeldKeyDoesNotSurviveItsRelease();
    spaceReachesWhateverIsBeingTypedInto();
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
    aLongShotsPanSliderLeavesTheTrackRowWhole();
    theInsertButtonObeysTheOverwriteSetting();
    draggingATracksNameRestacksIt();
    whereADroppedRowLands();
    aLayerDroppedOnAnotherRowRestacksTheStack();
    maximisingFramesTheCanvas();
    doubleClickingALayerRenamesIt();
    renamingACarriedColourLayerLeavesTheArrowOutOfIt();
    doubleClickingATrackRenamesIt();
    theKeyboardBelongsToAnyFieldItIsIn();
    aClickElsewhereTakesTheKeyboardBackFromAField();
    aRenameGivenUpKeepsTheNameAndTheKeyboard();
    aWindowDestroyedMidRenameGivesItUp();
    aWindowIsDestroyedSafelyFromAnyState();
    theLayerPanelSaysWhichTrackItIsShowing();
    aNewColourLayerIsInViewWhenItArrives();
    return testing::summarise("canvas");
}
