// SPDX-License-Identifier: GPL-3.0-or-later
//
// Issue #50: a floating panel's title bar has to belong to Qt, or a pen cannot
// press it.
//
// **What this can and cannot check.** That a floating dock ends up frameless is
// ordinary widget state and is checked here properly. That a pen can then press
// the title bar is *not* checkable from a test on any platform: whether a press
// on a native frame reaches Qt at all is decided by Windows Ink, and injecting
// at Qt's own boundary means Windows never sees a pen. See #53, and the trap in
// docs/handover.md about offscreen tests of pen input.
//
// So this asserts the mechanism, and the behaviour it buys was confirmed by hand
// with a real stylus. A green run here does not say the pen works.

#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QAbstractButton>
#include <QStyle>


#include "floating_dock_frame.h"
#include "main_window.h"
#include "testing.h"

namespace {

QDockWidget* dockCalled(MainWindow& window, const char* title) {
    for (QDockWidget* dock : window.findChildren<QDockWidget*>()) {
        if (dock->windowTitle() == QString::fromUtf8(title)) return dock;
    }
    return nullptr;
}

// Whether the panel wears Qt's own decoration rather than the window manager's,
// which is what decides if a pen can press its title bar.
bool ownDecoration(const QDockWidget* dock) {
    return dock->titleBarWidget() != nullptr &&
           (dock->windowFlags() & Qt::FramelessWindowHint) != Qt::WindowFlags();
}

// The watcher belongs to the dock, so a test can reach it and drive it rather
// than waiting on its timer -- which would make this a slow test that sometimes
// failed.
void settle(QDockWidget* dock) {
    QCoreApplication::processEvents();
    if (auto* frame = dock->findChild<FloatingDockFrame*>()) frame->applyIfNothingIsHeld();
    QCoreApplication::processEvents();
}

void aDockedPanelKeepsQtsOwnTitleBar() {
    TEST("a docked panel is left with Qt's own title bar");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QDockWidget* layers = dockCalled(window, "Layers");
    CHECK(layers != nullptr);
    if (!layers) return;
    // Docked, the title bar is already Qt's own and inside the main window.
    // Nothing needs replacing, and replacing it would change how the ordinary
    // window looks for no reason.
    CHECK(!layers->isFloating());
    CHECK(layers->titleBarWidget() == nullptr);
}

void aFloatingPanelLosesItsNativeFrame() {
    TEST("a floating panel is given a title bar Qt draws, not the window manager");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QDockWidget* layers = dockCalled(window, "Layers");
    if (!layers) return;

    layers->setFloating(true);
    settle(layers);

    CHECK(layers->isFloating());
    // The assertion the whole issue rests on. Without it the title bar is a
    // native frame, Windows Ink sends no press for it, and the panel cannot be
    // picked up again with a pen. Note it checks both halves: supplying the
    // widget is what makes Qt drop the native frame, and the frame going is what
    // actually matters.
    CHECK(ownDecoration(layers));
}

void bothPanelsGetIt() {
    TEST("the timeline is treated the same as the layer panel");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QDockWidget* timeline = dockCalled(window, "Timeline");
    if (!timeline) return;

    timeline->setFloating(true);
    settle(timeline);

    CHECK(ownDecoration(timeline));
}

void itIsStillFramelessAfterDockingAndFloatingAgain() {
    TEST("docking puts Qt's title bar back, and floating again takes ours");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QDockWidget* layers = dockCalled(window, "Layers");
    if (!layers) return;

    layers->setFloating(true);
    settle(layers);
    CHECK(ownDecoration(layers));

    // Docking has to give the panel back its ordinary appearance, and floating
    // again has to take it once more. This is the case that would catch either
    // half being done only once at startup.
    window.addDockWidget(Qt::RightDockWidgetArea, layers);
    layers->setFloating(false);
    QCoreApplication::processEvents();
    CHECK(!layers->isFloating());
    CHECK(layers->titleBarWidget() == nullptr);

    layers->setFloating(true);
    settle(layers);
    CHECK(ownDecoration(layers));
}

void theStripMatchesQtsOwnTitleBar() {
    TEST("the floating strip is the height Qt gives its docked title bar");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QDockWidget* layers = dockCalled(window, "Layers");
    if (!layers || !layers->widget()) return;

    // Qt's docked title bar is however far down the panel's contents begin.
    const int qts = layers->widget()->geometry().y();
    CHECK(qts > 0);

    layers->setFloating(true);
    settle(layers);
    // A floating panel offscreen has no size until it is given one, and a strip
    // with no width cannot place anything inside itself.
    layers->resize(280, 360);
    QCoreApplication::processEvents();

    QWidget* bar = layers->titleBarWidget();
    CHECK(bar != nullptr);
    if (!bar) return;

    // Reported: everything in the strip got bigger when the panel was undocked,
    // because the strip had no height of its own and grew to fit. Measured off
    // the docked panel rather than derived -- the style's own arithmetic for
    // this gives 26 px where Qt uses 24.
    CHECK_EQ(bar->height(), qts);

    // One button, and it must not take the keyboard: a button that does takes
    // the pen with it, which is the rule every other button here follows.
    const QList<QAbstractButton*> buttons = bar->findChildren<QAbstractButton*>();
    CHECK_EQ(static_cast<int>(buttons.size()), 1);
    if (buttons.isEmpty()) return;
    CHECK_EQ(static_cast<int>(buttons.first()->focusPolicy()), static_cast<int>(Qt::NoFocus));

    CHECK(buttons.first()->isVisible());
    // Deliberately no assertion about *where* the button sits. Offscreen, a
    // floating panel's strip is not reliably laid out, so a geometry check here
    // tests the harness rather than the code -- the same trap that made the
    // re-docking tests worthless. Where it sits is judged by looking, in the
    // shot panels-the-title-bar-docked-and-floating.
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    aDockedPanelKeepsQtsOwnTitleBar();
    aFloatingPanelLosesItsNativeFrame();
    bothPanelsGetIt();
    itIsStillFramelessAfterDockingAndFloatingAgain();
    theStripMatchesQtsOwnTitleBar();

    return testing::summarise("floating dock frame");
}
