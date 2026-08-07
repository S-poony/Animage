// SPDX-License-Identifier: GPL-3.0-or-later
//
// The file lifecycle through AppController's public surface, offscreen:
//
//   * dirty state is a command identity, not a stack depth -- undo-and-branch
//     to the same depth stays dirty, undo back to the saved command is clean;
//   * the leave handshake (New/Open/Close x Save/Discard/Cancel) records the
//     requested action before asking, so every answer does the right thing
//     and a failed save never invents an action of its own;
//   * autosave is a recovery snapshot that does not clear the dirty marker;
//   * the view models are wired to the document's track (the colour-sources
//     model's `track_` used to stay kNoId, which emptied its list).

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTemporaryDir>

#include "app_controller.h"
#include "brush.h"
#include "canvas_view.h"
#include "document.h"
#include "project_io.h"
#include "testing.h"

using namespace animage;
using CanvasWidget = CanvasView;

// The window the tests drive: an AppController with a real canvas attached,
// the same shape test_canvas.cpp uses.
class MainWindow : public AppController {
public:
    MainWindow() {
        canvas_.resize(1200, 800);
        attachCanvas(&canvas_);
    }

    template <typename T>
    T findChild(const QString& = QString()) {
        if constexpr (std::is_same_v<T, CanvasWidget*>) return &canvas_;
        return nullptr;
    }

    QString windowTitle() const { return title(); }
    void resize(int w, int h) { canvas_.resize(w, h); }

    CanvasWidget canvas_;
};

// A document with pixels in it, so a save round trip has something to lose.
Document buildDrawnScene() {
    Document doc;
    const TrackId track = doc.addTrack("main");
    const LayerId ink = doc.addLayer(track, "ink");
    const ImageId first = doc.insertImage(track, 0);

    ScopedCommand command(doc, "Stroke");
    BrushSettings s;
    s.radius = 9.0f;
    s.pressure_affects_opacity = false;
    s.r = 0; s.g = 0; s.b = 0; s.a = 1.0f;
    Brush brush(s);
    brush.begin(doc, track, first, ink, {100.0f, 100.0f, 1.0f});
    brush.extend({400.0f, 260.0f, 1.0f});
    brush.end();

    doc.setCanvasSize(1280, 720);
    doc.setFramerate(12);
    return doc;
}

namespace {

void sendMouse(QQuickItem* widget, QEvent::Type type, const QPointF& at, Qt::MouseButton button,
               Qt::MouseButtons buttons) {
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

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

// The exact failure the old depth-based check had: save at depth N, undo to
// N-1, edit something different, and land back at depth N. The stack depth
// cannot tell the new edit from the saved one; the command identity can.
void dirtyStateIsACommandIdentityNotADepth() {
    TEST("undoing and branching to the same depth still reads as dirty");
    MainWindow window;
    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    // Three strokes: three commands on the history.
    drawWithMouse(canvas, QPointF(100, 100), QPointF(160, 140), 4);
    drawWithMouse(canvas, QPointF(200, 200), QPointF(260, 240), 4);
    drawWithMouse(canvas, QPointF(300, 300), QPointF(360, 340), 4);
    CHECK_EQ(window.undoDepth(), 3);
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    // Saving at depth 3 makes that top command the clean state.
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(window.saveTo(folder));
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
    CHECK_EQ(window.undoDepth(), 3);

    // Undo to depth 2 and undo again back to the saved command: clean.
    window.undo();
    CHECK_EQ(window.undoDepth(), 2);
    CHECK(window.windowTitle().contains(QLatin1Char('*')));
    window.redo();
    CHECK_EQ(window.undoDepth(), 3);
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));

    // Undo to depth 2, then make a DIFFERENT edit: depth 3 again, but the
    // contents differ from what was saved. The depth check would call this
    // "saved"; the command identity must not.
    window.undo();
    drawWithMouse(canvas, QPointF(400, 400), QPointF(460, 440), 4);
    CHECK_EQ(window.undoDepth(), 3);
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    // And the saved contents are now unreachable: no undo level is clean.
    window.undo();
    window.undo();
    window.undo();
    CHECK_EQ(window.undoDepth(), 0);
    CHECK(window.windowTitle().contains(QLatin1Char('*')));
}

// New on a dirty untitled document asks, and each of the three answers does
// exactly one thing. This is the handshake that used to answer Pending::None
// because `leave_pending_` was never recorded for an unsaved document.
void newProjectHandshakeOnAnUntitledDocument() {
    TEST("New on a dirty untitled document: Cancel, Discard and Save each behave");
    MainWindow window;
    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;

    int asked = 0;
    int save_as_requested = 0;
    int new_scene_settings = 0;
    QObject::connect(&window, &AppController::leaveDecisionRequested,
                     [&asked](const QString&) { ++asked; });
    QObject::connect(&window, &AppController::saveFileDialogRequested,
                     [&save_as_requested]() { ++save_as_requested; });
    QObject::connect(&window, &AppController::sceneSettingsRequested,
                     [&new_scene_settings]() { ++new_scene_settings; });

    drawWithMouse(canvas, QPointF(100, 100), QPointF(160, 140), 4);
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    // Cancel: the question is asked, and nothing else happens.
    window.newProject();
    QCoreApplication::processEvents();
    CHECK_EQ(asked, 1);
    window.respondSaveDecision(AppController::Cancel);
    QCoreApplication::processEvents();
    CHECK(window.windowTitle().contains(QLatin1Char('*')));
    CHECK_EQ(window.layerCount(), 1);
    CHECK_EQ(save_as_requested, 0);

    // Discard: a fresh untitled document replaces it, no dialogs involved.
    window.newProject();
    QCoreApplication::processEvents();
    CHECK_EQ(asked, 2);
    window.respondSaveDecision(AppController::Discard);
    QCoreApplication::processEvents();
    CHECK(window.windowTitle().startsWith(QStringLiteral("Untitled")));
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
    CHECK_EQ(window.layerCount(), 1);

    // Save: Save As first; the leave only happens once something is on disk.
    // (The Discard above already performed one New, so reset the counter to
    // measure this handshake only.)
    new_scene_settings = 0;
    drawWithMouse(canvas, QPointF(200, 200), QPointF(260, 240), 4);
    window.newProject();
    QCoreApplication::processEvents();
    CHECK_EQ(asked, 3);
    window.respondSaveDecision(AppController::Save);
    QCoreApplication::processEvents();
    CHECK_EQ(save_as_requested, 1);
    // The Save As dialog has not landed: still the same dirty document, and no
    // New has been performed (so no Scene settings dialog has been raised).
    CHECK(window.windowTitle().contains(QLatin1Char('*')));
    CHECK_EQ(new_scene_settings, 0);

    // The Save As lands: the document is written, then the New happens.
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    window.acceptSaveLocation(scratch.filePath(QStringLiteral("untitled.animage")));
    QCoreApplication::processEvents();
    CHECK_EQ(new_scene_settings, 1);
    CHECK(window.windowTitle().startsWith(QStringLiteral("Untitled")));
    CHECK(!window.windowTitle().contains(QLatin1Char('*')));
    CHECK(QFileInfo::exists(scratch.filePath(QStringLiteral("untitled.animage"))));
}

// The old code assigned Pending::Close after a failed save no matter what the
// leave was for. The action must survive the failed save untouched: the leave
// drops, and the state machine is still usable afterwards. The save is made to
// fail by taking write permission away from the project's own directory, which
// ProjectIO needs to move the old project aside.
void aFailedSaveDropsTheLeaveWithoutInventingAnAction() {
    TEST("a failed save during a leave drops the leave instead of guessing");

    // Running as root, permissions do not block anything and the "failure"
    // would silently succeed. Skip rather than assert something untrue.
    QTemporaryDir probe;
    if (probe.isValid() && QFileInfo(probe.path()).ownerId() == 0) {
        testing::skip("running as root; file permissions cannot force a save failure");
        return;
    }

    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));
    CHECK(ProjectIO::save(buildDrawnScene(), folder, nullptr));

    MainWindow window;
    window.resize(1200, 800);
    CHECK(window.openProjectAt(folder, nullptr));
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    if (!canvas) return;
    drawWithMouse(canvas, QPointF(300, 300), QPointF(360, 340), 4);
    QCoreApplication::processEvents();
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    bool asked = false;
    bool closed = false;
    QObject::connect(&window, &AppController::leaveDecisionRequested,
                     [&asked](const QString&) { asked = true; });
    QObject::connect(&window, &AppController::closeRequested, [&closed]() { closed = true; });

    // Take the project's own directory away: the save cannot land in it.
    const QString dir = scratch.path();
    const QFileDevice::Permissions old_perms = QFile::permissions(dir);
    const QFileDevice::Permissions locked = QFileDevice::ReadOwner | QFileDevice::ExeOwner |
                                            QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                            QFileDevice::ReadOther | QFileDevice::ExeOther;
    CHECK(QFile::setPermissions(dir, locked));
    CHECK(!window.saveTo(folder));  // sanity: the save really does fail
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    window.requestClose();
    QCoreApplication::processEvents();
    CHECK(asked);
    window.respondSaveDecision(AppController::Save);
    QCoreApplication::processEvents();

    // The save failed, so the leave did not happen -- and, crucially, no Close
    // was invented for it.
    CHECK(!closed);
    CHECK(window.windowTitle().contains(QLatin1Char('*')));

    // The handshake still works afterwards: Discard carries the leave out.
    QFile::setPermissions(dir, old_perms);
    window.requestClose();
    QCoreApplication::processEvents();
    window.respondSaveDecision(AppController::Discard);
    QCoreApplication::processEvents();
    CHECK(closed);
}

// The colour-sources model lists the raster layers of the current track. Its
// `track_` used to stay kNoId (setTrack was never called), which emptied the
// list and made the Cut-against box look disconnected.
void theColourSourcesModelKnowsItsTrack() {
    TEST("the colour-sources model lists the track's raster layers");
    MainWindow window;
    CHECK(window.ctgSourcesModel() != nullptr);

    // One raster layer exists from the start.
    CHECK_EQ(window.ctgSourcesModel()->rowCount(), 1);
    QModelIndex row0 = window.ctgSourcesModel()->index(0);
    CHECK_EQ(window.ctgSourcesModel()->data(row0, CtgSourcesModel::NameRole).toString().toStdString(),
             std::string("layer 1"));

    // Adding a colourize layer wires it as a source of the only raster layer.
    window.addColourLayer();
    QCoreApplication::processEvents();
    CHECK_EQ(window.ctgSourcesModel()->rowCount(), 1);
    QModelIndex after_ctg = window.ctgSourcesModel()->index(0);
    CHECK_EQ(window.ctgSourcesModel()->data(after_ctg, CtgSourcesModel::LayerIndexRole).toInt(), 0);
    CHECK_EQ(window.ctgSourcesModel()->data(after_ctg, CtgSourcesModel::CheckedRole).toBool(), true);

    // A second raster layer appears in the list, unticked by default.
    window.addLayer();
    QCoreApplication::processEvents();
    CHECK_EQ(window.ctgSourcesModel()->rowCount(), 2);
    CHECK_EQ(window.ctgSourcesModel()->data(window.ctgSourcesModel()->index(1),
                                            CtgSourcesModel::CheckedRole).toBool(), false);
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    std::printf("controller:\n");
    dirtyStateIsACommandIdentityNotADepth();
    newProjectHandshakeOnAnUntitledDocument();
    aFailedSaveDropsTheLeaveWithoutInventingAnAction();
    theColourSourcesModelKnowsItsTrack();
    return testing::summarise("controller");
}
