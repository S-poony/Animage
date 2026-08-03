// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives the canvas widget offscreen. These are the paths that only a human
// clicking around used to reach, which meant their crashes were found by a
// human clicking around.

#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <cmath>

#include "brush.h"
#include "canvas_widget.h"
#include "main_window.h"
#include "document.h"
#include "testing.h"

using namespace animage;

namespace {

struct Fixture {
    Document doc;
    TimelineId timeline;
    LayerId layer;
    ImageId image;
    CanvasWidget canvas;

    Fixture() : canvas(doc) {
        timeline = doc.addTimeline("main");
        layer = doc.addLayer(timeline, "layer 1");
        image = doc.insertImage(timeline, 0);
        canvas.resize(1280, 800);
        canvas.setTimeline(timeline);
        canvas.setFrame(0);
        canvas.setActiveLayer(layer);
    }

    void draw(float x0, float y0, float x1, float y1) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings settings;
        settings.radius = 12.0f;
        settings.pressure_affects_opacity = false;
        Brush brush(settings);
        brush.begin(doc, timeline, image, layer, {x0, y0, 1.0f});
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
    f.doc.insertImage(f.timeline, 1);
    f.doc.insertImage(f.timeline, 2);

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
    f.doc.extendExposure(f.timeline, 0, 4);

    const ImageId second = f.doc.insertImage(f.timeline, 5);
    f.canvas.setFrame(5);
    CHECK_EQ(f.canvas.currentImage(), second);
    CHECK(!f.render().isNull());

    f.canvas.setFrame(0);
    f.doc.removeDrawing(f.timeline, f.image);
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
    TEST("a timeline with no frames still renders");
    Fixture f;
    f.draw(10.0f, 10.0f, 90.0f, 90.0f);
    f.doc.removeDrawing(f.timeline, f.image);

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

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::printf("canvas:\n");
    heldKeysDoNotRecurse();
    cacheStaysBoundedAtEveryZoom();
    zoomSweepDoesNotExplode();
    repeatedZoomAndPanStayConsistent();
    onionSkinAtLowZoom();
    deleteDrawingThenUndo();
    emptyTimelineRenders();
    return testing::summarise("canvas");
}
