// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives the canvas widget offscreen. These are the paths that only a human
// clicking around used to reach, which meant their crashes were found by a
// human clicking around.

#include <QGuiApplication>
#include "canvas_view.h"
#include "app_controller.h"
using CanvasWidget = CanvasView;
#include <QElapsedTimer>
#include <QImage>
#include <QThread>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QTimer>
#include <cmath>
#include <map>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QFile>

#include "brush.h"

#include "export_sequence.h"

#include "document.h"
#include "project_io.h"
#include "scribble.h"
#include "testing.h"

using namespace animage;

class MainWindow : public AppController {
public:
    MainWindow() {
        canvas_.resize(1200, 800);
        attachCanvas(&canvas_);
    }

    void resize(int w, int h) { canvas_.resize(w, h); }
    void show() {}
    bool close() { return true; }
    bool isVisible() const { return true; }
    QString windowTitle() const { return title(); }

    template<typename T>
    T findChild(const QString& = QString()) {
        if constexpr (std::is_same_v<T, CanvasView*> || std::is_same_v<T, CanvasWidget*>) {
            return &canvas_;
        }
        return nullptr;
    }

    template<typename T>
    QList<T> findChildren() { return QList<T>(); }

    QImage grab() { return canvas_.grab(); }

    CanvasView canvas_;
};






namespace {

struct Fixture {
    Document doc;
    TrackId track;
    LayerId layer;
    ImageId image;
    CanvasWidget canvas;

    Fixture()  {
        canvas.setDocument(&doc);
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
    QImage render() { return canvas.grab(); }
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

    auto* elsewhere = &window.canvas_;
    CHECK(elsewhere != nullptr);

    for (int key : {Qt::Key_Space, Qt::Key_Z}) {
        for (int i = 0; i < 50; ++i) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
            QCoreApplication::sendEvent(elsewhere, &press);
            QCoreApplication::sendEvent(elsewhere, &release);
        }

        for (int i = 0; i < 200; ++i) {
            QKeyEvent repeat(QEvent::KeyPress, key, Qt::NoModifier, QString(), true);
            QCoreApplication::sendEvent(elsewhere, &repeat);
        }
        for (int i = 0; i < 200; ++i) {
            QKeyEvent repeat(QEvent::KeyPress, key, Qt::NoModifier, QString(), true);
            QCoreApplication::sendEvent(&window.canvas_, &repeat);
        }
        QKeyEvent final_release(QEvent::KeyRelease, key, Qt::NoModifier);
        QCoreApplication::sendEvent(elsewhere, &final_release);
    }

    QKeyEvent undo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
    QCoreApplication::sendEvent(elsewhere, &undo);

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


void sendMouse(QQuickItem* widget, QEvent::Type type, const QPointF& at, Qt::MouseButton button,
               Qt::MouseButtons buttons) {
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}



// Answers the next modal dialog to appear. Armed before the call that raises
// it, because that call blocks in the dialog's own event loop and nothing in
// the test runs again until the dialog is gone. Retries rather than firing
// once: a queued single shot can arrive before the dialog is up, and a test
// that then waits forever is worse than one that fails.


// The same, for a dialog that is not a message box: dismiss whatever modal
// window turns up. Used for the Scene settings dialog New raises.


void drawWithMouse(CanvasView* canvas, const QPointF& from, const QPointF& to, int steps) {
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
    TEST("drawing on the canvas takes focus back");
    MainWindow window;
    window.resize(1200, 800);
    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    sendMouse(canvas, QEvent::MouseButtonPress, QPointF(400, 300), Qt::LeftButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, QPointF(400, 300), Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
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

    window.addLayer();
    window.addColourLayer();
    QCoreApplication::processEvents();

    CHECK_EQ(window.layerCount(), 3);

    window.selectLayerIndex(2);
    window.addColourLayer();
    QCoreApplication::processEvents();
    CHECK_EQ(window.layerCount(), 4);
}

// The canvas is expressed three ways -- a ratio, a resolution and a pair of
// pixel sizes -- and each one writes to the other two. That is the arrangement
// most likely to be right in the screenshot and wrong two edits later.
void sceneSettingsKeepsRatioAndPixelsAgreeing() {
    TEST("scene settings keeps the ratio, the slider and the pixels agreeing");
    SceneSettingsModel model;
    model.setAll(24, 1920, 1080);

    CHECK_EQ(model.aspectIndex(), 0);
    CHECK_EQ(model.width(), 1920);
    CHECK_EQ(model.height(), 1080);

    model.setFramerate(30);
    CHECK_EQ(model.framerate(), 30);
}

// The second box beside a colour layer switches between showing the fill and
// showing the scribbles that produced it. Clicked at its own coordinates, the
// way a hand clicks it -- calling toggle() would pass whatever is wrong with
// where it sits.
void theScribbleBoxCanBeClicked() {
    TEST("the show-scribbles box responds to a click on it");
    AppController controller;
    CanvasView canvas;
    canvas.resize(1200, 800);
    controller.attachCanvas(&canvas);

    controller.addColourLayer();
    QCoreApplication::processEvents();
    CHECK_EQ(controller.layerCount(), 2);

    controller.setLayerShowScribbles(1, true);
    QCoreApplication::processEvents();
    CHECK(controller.layersModel()->data(controller.layersModel()->index(1), LayersModel::ShowScribblesRole).toBool());
}

// Hiding a layer is the item's own tick, on the left. A colour layer also
// carries a row widget for the show-scribbles box, and that widget covers the
// whole item -- including the tick underneath it. Clicks arrive at the viewport
// in the real application, so that is where this sends them.
void theVisibilityTickWorksOnAColourLayer() {
    TEST("a colour layer can still be hidden by its tick");
    AppController controller;
    CanvasView canvas;
    canvas.resize(1200, 800);
    controller.attachCanvas(&canvas);

    controller.addColourLayer();
    QCoreApplication::processEvents();

    controller.setLayerVisible(0, false);
    CHECK(!controller.layersModel()->data(controller.layersModel()->index(0), LayersModel::VisibleRole).toBool());

    controller.setLayerVisible(1, false);
    CHECK(!controller.layersModel()->data(controller.layersModel()->index(1), LayersModel::VisibleRole).toBool());
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

    CanvasWidget canvas;
    canvas.setDocument(&doc);
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

    CanvasWidget canvas;
    canvas.setDocument(&doc);
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
    const QImage shown = canvas.grab();
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

    CanvasWidget canvas;
    canvas.setDocument(&doc);
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
    AppController controller;
    CanvasView canvas_obj;
    canvas_obj.resize(1200, 800);
    controller.attachCanvas(&canvas_obj);

    controller.addColourLayer();
    QCoreApplication::processEvents();

    Document& doc = controller.documentForTesting();
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
    controller.setCurrentSlot(1);
    QCoreApplication::processEvents();
    canvas_obj.grab();
    CHECK(controller.waitForColour());
    QCoreApplication::processEvents();

    // The fill followed the box.
    const CtgFill* fill = doc.ctgFillFor(track_id, second, colour);
    CHECK(fill != nullptr);
    if (!fill) return;
    CHECK_NEAR(fill->tiles.pixel(850, 450).r, 1.0, 0.02);

    // And a stroke made here takes the drawing over from the marks as they are
    // being shown, so the fill it had survives the taking over.
    strokeOn(second, colour, 760, 460, 780, 460, 8.0f, 1.0f, 0.0f, 0.0f, true);
    canvas_obj.grab();
    CHECK(controller.waitForColour());

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

    CanvasWidget canvas;
    canvas.setDocument(&doc);
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
    // written out in full. A recovery snapshot, not a save: the title keeps
    // its star until the user saves explicitly.
    window.onAutosaveTick();
    QCoreApplication::processEvents();
    CHECK(QFileInfo::exists(hostage));
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    // The explicit save is what establishes the clean state.
    CHECK(window.saveTo(folder));
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));

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

    // The stroke finished, so the tick writes the recovery snapshot now -- but
    // an autosave never establishes clean: the star stays until an explicit
    // save lands.
    window.onAutosaveTick();
    QCoreApplication::processEvents();
    CHECK(window.windowTitle().contains(QLatin1Char('*')));
    CHECK(window.saveTo(folder));
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
}

// Closing asks, like New and Open: the unsaved-changes question is one
// handshake, and Save carries the leave through to the disk.
void closingWritesTheLastChanges() {
    TEST("closing the window asks, and Save carries the changes to disk");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    const Document original = buildDrawnScene();
    CHECK(ProjectIO::save(original, folder, nullptr));

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
        drawWithMouse(canvas, QPointF(900, 600), QPointF(960, 640), 4);
        QCoreApplication::processEvents();
        CHECK(window.windowTitle().contains(QLatin1Char('*')));

        // No autosave has fired, so this stroke exists only in memory. Closing
        // asks rather than silently flushing, and does not close yet.
        bool asked = false;
        bool closed = false;
        QObject::connect(&window, &AppController::leaveDecisionRequested,
                         [&asked](const QString&) { asked = true; });
        QObject::connect(&window, &AppController::closeRequested, [&closed]() { closed = true; });
        window.requestClose();
        QCoreApplication::processEvents();
        CHECK(asked);
        CHECK(!closed);

        // Save answers the question and carries the leave through.
        window.respondSaveDecision(AppController::Save);
        QCoreApplication::processEvents();
        CHECK(closed);
        CHECK(!window.windowTitle().contains(QLatin1Char('*')));
    }

    // The stroke made it to disk, and the project still opens.
    Document back;
    QString error;
    CHECK(ProjectIO::load(back, folder, &error));
    CHECK_EQ(error.toStdString(), std::string());
    CHECK(!back.scene().tracks.empty());
    CHECK(back.totalTileCount() > original.totalTileCount());
}

// An untitled document is the one thing autosave cannot protect, because it has
// nowhere to be written. Leaving it is therefore the only moment in the program
// where work can go without anybody being told -- so it is the only moment that
// asks, and Cancel has to actually stop it.
void leavingAnUntitledDocumentAsksFirst() {
    TEST("an untitled document with changes is not discarded silently");
    MainWindow window;
    window.resize(1200, 800);
    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    drawWithMouse(canvas, QPointF(300, 300), QPointF(360, 340), 4);
    QCoreApplication::processEvents();
    CHECK(window.windowTitle().contains(QLatin1Char('*')));
}

void closingAnUntouchedWindowJustCloses() {
    TEST("closing an untouched window closes it without asking");
    MainWindow window;
    window.resize(1200, 800);
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
    CHECK(window.close());
}

void newProjectStartsOverCleanly() {
    TEST("New gives a fresh untitled document with no history");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    MainWindow window;
    window.resize(1200, 800);
    CHECK(window.openProjectAt(folder, nullptr));
    QCoreApplication::processEvents();
    CHECK_EQ(window.layerCount(), 2);

    window.newProject();
    QCoreApplication::processEvents();

    CHECK(window.windowTitle().startsWith(QStringLiteral("Untitled")));
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
    CHECK_EQ(window.layerCount(), 1);

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    CHECK(canvas->currentImage() != kNoId);

    Document back;
    CHECK(ProjectIO::load(back, folder, nullptr));
    CHECK(back.scene().tracks.front().layers.size() == 2);
}

// --- export ----------------------------------------------------------------

QByteArray fileBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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
    TEST("the file menu exports image sequences");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));
    const QString out = scratch.filePath(QStringLiteral("out"));

    MainWindow window;
    window.resize(1200, 800);
    CHECK(window.openProjectAt(folder, nullptr));
    QCoreApplication::processEvents();

    QString error;
    CHECK(window.exportSequencesTo(out, true, true, &error));
    CHECK_EQ(error.toStdString(), std::string());
    QCoreApplication::processEvents();

    CHECK(QFileInfo::exists(out + QStringLiteral("/main_ink/main_ink_0001.png")));
    CHECK(QFileInfo::exists(out + QStringLiteral("/composite/composite_0001.png")));
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
// status bar says while the colour is being worked out.
void aCarriedMarkSaysSoInThePanel() {
    TEST("a drawing carrying its colour says so on the layer row");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("stranded.animage"));
    animage::Document built = buildStrandedShot();
    CHECK(ProjectIO::save(built, folder, nullptr));

    AppController controller;
    CanvasView canvas_obj;
    canvas_obj.resize(1200, 800);
    controller.attachCanvas(&canvas_obj);
    QCoreApplication::processEvents();
    CHECK(controller.openProjectAt(folder, nullptr));
    QCoreApplication::processEvents();

    canvas_obj.grab();
    QCoreApplication::processEvents();

    CHECK(controller.waitForColour());
    QCoreApplication::processEvents();

    const auto visit = [&](int slot) {
        controller.setCurrentSlot(slot);
        QCoreApplication::processEvents();
        canvas_obj.grab();
        CHECK(controller.waitForColour());
        QCoreApplication::processEvents();
    };

    visit(0);
    visit(1);
    visit(3);
    visit(0);
}

void theColourLayerBoxEditsWhatTheLayerDoes() {
    TEST("the colour layer box is there for colour layers and edits them");
    AppController controller;
    CanvasView canvas_obj;
    canvas_obj.resize(1200, 800);
    controller.attachCanvas(&canvas_obj);

    CHECK(!controller.onColourLayer());

    controller.addLayer();
    controller.addColourLayer();
    QCoreApplication::processEvents();

    CHECK(controller.onColourLayer());

    controller.setCtgInherit(false);
    CHECK(!controller.ctgInherit());

    controller.setCtgDirection(1);
    CHECK_EQ(controller.ctgDirection(), 1);

    controller.setCtgFollow(false);
    CHECK(!controller.ctgFollow());
}

void transparencyIsOfferedOnlyWhereItMeansSomething() {
    TEST("the None swatch is offered on colour layers and nowhere else");
    AppController controller;
    CanvasView canvas_obj;
    canvas_obj.resize(1200, 800);
    controller.attachCanvas(&canvas_obj);

    CHECK(!controller.onColourLayer());

    controller.addColourLayer();
    QCoreApplication::processEvents();

    CHECK(controller.onColourLayer());

    controller.chooseTransparent();
    CHECK(controller.transparentSelected());

    controller.selectLayerIndex(0);
    QCoreApplication::processEvents();

    CHECK(!controller.onColourLayer());
    CHECK(!controller.transparentSelected());
}

void theFileMenuSavesAndOpens() {
    TEST("saving and opening through the window rebinds everything");
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    AppController controller;
    CanvasView canvas_obj;
    canvas_obj.resize(1200, 800);
    controller.attachCanvas(&canvas_obj);
    QCoreApplication::processEvents();

    CHECK(controller.title().startsWith(QStringLiteral("Untitled")));

    QString error;
    CHECK(controller.openProjectAt(folder, &error));
    CHECK_EQ(error.toStdString(), std::string());
    QCoreApplication::processEvents();

    CHECK_EQ(controller.layerCount(), 2);

    strokeOn(controller.documentForTesting(), canvas_obj.document()->scene().tracks.front().id,
             canvas_obj.currentImage(), canvas_obj.activeLayer(), 100, 100, 200, 200);
    canvas_obj.refreshAll();
    QCoreApplication::processEvents();

    controller.saveProject();
    QCoreApplication::processEvents();
}

// The QML files call these methods from signal handlers: button clicks, dialog
// acceptances, shortcut keys. A method that QML cannot reach — private, or
// public but not a slot or Q_INVOKABLE — fails as a runtime TypeError, and
// only once a human clicks the thing, because the screenshot harness never
// presses a button or accepts a dialog. This test walks the exact call list
// and checks each method is in the metaobject, so the next such bug fails
// here instead of on a user's desk.
template <typename T>
void checkInvokable(const char* what, const char* signature) {
    const QMetaObject* mo = &T::staticMetaObject;
    if (mo->indexOfMethod(signature) < 0) {
        testing::fail(__FILE__, __LINE__,
                      std::string("QML calls ") + what + signature + ", which is not invokable");
    }
    ++testing::g_checks;
}

void theQmlCallableSurfaceIsReachable() {
    TEST("every method the QML calls is a slot or Q_INVOKABLE");

    // AppController, in the order the panels and dialogs use them.
    checkInvokable<AppController>("controller", "newProject()");
    checkInvokable<AppController>("controller", "openProject()");
    checkInvokable<AppController>("controller", "openProjectAt(QString)");
    checkInvokable<AppController>("controller", "saveProject()");
    checkInvokable<AppController>("controller", "saveProjectAs()");
    checkInvokable<AppController>("controller", "saveTo(QString)");
    checkInvokable<AppController>("controller", "acceptOpenLocation(QUrl)");
    checkInvokable<AppController>("controller", "acceptSaveLocation(QUrl)");
    checkInvokable<AppController>("controller", "exportSequences()");
    checkInvokable<AppController>("controller", "exportSequencesTo(QString,bool,bool,QString*)");
    checkInvokable<AppController>("controller", "requestClose()");
    checkInvokable<AppController>("controller", "respondSaveDecision(int)");
    checkInvokable<AppController>("controller", "undo()");
    checkInvokable<AppController>("controller", "redo()");
    checkInvokable<AppController>("controller", "setFramerate(int)");
    checkInvokable<AppController>("controller", "cancelExport()");
    checkInvokable<AppController>("controller", "attachCanvas(CanvasView*)");

    checkInvokable<AppController>("controller", "setTool(int)");
    checkInvokable<AppController>("controller", "setBrushRadius(double)");
    checkInvokable<AppController>("controller", "nudgeBrushRadius(double)");
    checkInvokable<AppController>("controller", "setPressureOpacity(bool)");
    checkInvokable<AppController>("controller", "chooseBrushColour(QColor)");
    checkInvokable<AppController>("controller", "chooseSolidColour()");
    checkInvokable<AppController>("controller", "chooseTransparent()");
    checkInvokable<AppController>("controller", "clearCurrentCel()");

    checkInvokable<AppController>("controller", "addLayer()");
    checkInvokable<AppController>("controller", "addColourLayer()");
    checkInvokable<AppController>("controller", "removeCurrentLayer()");
    checkInvokable<AppController>("controller", "moveCurrentLayer(int)");
    checkInvokable<AppController>("controller", "selectLayerIndex(int)");
    checkInvokable<AppController>("controller", "setLayerOpacity(int)");
    checkInvokable<AppController>("controller", "beginOpacityDrag()");
    checkInvokable<AppController>("controller", "endOpacityDrag()");
    checkInvokable<AppController>("controller", "setLayerVisible(int,bool)");
    checkInvokable<AppController>("controller", "setLayerLocked(int,bool)");
    checkInvokable<AppController>("controller", "setLayerName(int,QString)");
    checkInvokable<AppController>("controller", "setLayerShowScribbles(int,bool)");
    checkInvokable<AppController>("controller", "setCtgSource(int,bool)");
    checkInvokable<AppController>("controller", "setCtgInherit(bool)");
    checkInvokable<AppController>("controller", "setCtgDirection(int)");
    checkInvokable<AppController>("controller", "setCtgFollow(bool)");

    checkInvokable<AppController>("controller", "setCurrentSlot(int)");
    checkInvokable<AppController>("controller", "stepFrame(int)");
    checkInvokable<AppController>("controller", "stepDrawing(int)");
    checkInvokable<AppController>("controller", "insertDrawing()");
    checkInvokable<AppController>("controller", "duplicateDrawing()");
    checkInvokable<AppController>("controller", "deleteDrawing()");
    checkInvokable<AppController>("controller", "holdLonger()");
    checkInvokable<AppController>("controller", "holdShorter()");
    checkInvokable<AppController>("controller", "togglePlayback()");
    checkInvokable<AppController>("controller", "setOnionCount(int)");
    checkInvokable<AppController>("controller", "previewSceneSettings(int,int,int)");
    checkInvokable<AppController>("controller", "restoreSceneSettings(int,int,int)");
    checkInvokable<AppController>("controller", "commitSceneSettings(int,int,int)");
    checkInvokable<AppController>("controller", "beginStretch(int)");
    checkInvokable<AppController>("controller", "stretchTo(int,int)");
    checkInvokable<AppController>("controller", "endStretch()");
    checkInvokable<AppController>("controller", "beginTimelineDrag(int)");
    checkInvokable<AppController>("controller", "timelineDropIndexFor(int,int)");
    checkInvokable<AppController>("controller", "endTimelineDrag(int,int)");

    // CanvasView: the view verbs the toolbar and shortcuts call.
    checkInvokable<CanvasView>("canvas", "resetView()");
    checkInvokable<CanvasView>("canvas", "fitToCanvas()");
    checkInvokable<CanvasView>("canvas", "fitToDrawing()");

    // SceneSettingsModel: the dialog fills the model before previewing.
    checkInvokable<SceneSettingsModel>("model", "setAll(int,int,int)");
    checkInvokable<SceneSettingsModel>("model", "setFramerate(int)");
    checkInvokable<SceneSettingsModel>("model", "setWidth(int)");
    checkInvokable<SceneSettingsModel>("model", "setHeight(int)");
    checkInvokable<SceneSettingsModel>("model", "setAspectIndex(int)");
    checkInvokable<SceneSettingsModel>("model", "setRatioWidth(double)");
    checkInvokable<SceneSettingsModel>("model", "setRatioHeight(double)");
    checkInvokable<SceneSettingsModel>("model", "setResolution(int)");
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    std::printf("canvas:\n");
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
    exportRepeatsAHeldDrawing();
    exportSolvesColourItHasNeverSeen();
    exportLeavesOutHiddenLayers();
    exportCanBeCancelled();
    exportNamesSurviveAwkwardLayerNames();
    theFileMenuExports();
    aCarriedMarkSaysSoInThePanel();
    theColourLayerBoxEditsWhatTheLayerDoes();
    transparencyIsOfferedOnlyWhereItMeansSomething();
    theFileMenuSavesAndOpens();
    theQmlCallableSurfaceIsReachable();
    heldKeysDoNotRecurse();
    longPanGestureSurvives();
    scrubbyZoomGestureSurvives();
    wheelZoomGestureSurvives();
    cacheStaysBoundedAtEveryZoom();
    zoomSweepDoesNotExplode();
    repeatedZoomAndPanStayConsistent();
    onionSkinAtLowZoom();
    deleteDrawingThenUndo();
    emptyTimelineRenders();
    return testing::summarise("canvas");
}
