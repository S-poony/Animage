// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>
#include <QObject>
#include <QSize>

class QDockWidget;
class QTimer;
class QWidget;

// Keeps a floating panel's title bar reachable by a pen.
//
// **Issue #50, and the cause is not where three attempts looked for it.** A
// floating `QDockWidget` on Windows is given a *native* window frame, so its
// title bar is non-client area -- owned by the window manager rather than by Qt.
// Qt delivers clicks there as `QEvent::NonClientAreaMouseButtonPress`, and that
// is the path a mouse re-docks a panel through.
//
// **Windows Ink never generates those for a pen.** Measured with a real stylus:
// hovering a floating panel's title bar produces a stream of
// `NonClientAreaMouseMove` and no press ever arrives, so `QDockWidget`'s drag is
// never entered and the panel cannot be brought back. Nothing about event
// routing could have helped, because there was no event to route.
//
// So the decoration is made Qt's own, which puts the title bar in client area
// where the pen already reaches -- as it must, since dragging a *docked* panel
// has always worked.
//
// **The lever is `setTitleBarWidget` and nothing else.** Setting
// `Qt::FramelessWindowHint` directly does not work: measured, the flag is
// discarded and `windowFlags()` comes back unchanged. Qt decides native
// decoration for itself and only stands down when a title bar widget is
// supplied. So one is, and it is deliberately **passive** -- it draws a name and
// a close button and handles no mouse or tablet events at all, leaving the drag,
// the drop preview, the dock areas and the close button to Qt exactly as they
// are for a mouse.
//
// That distinction is the whole lesson of the attempt this replaces. An earlier
// version also called `setTitleBarWidget`, but drove the gesture by hand as
// well, and cost a close button that could not be clicked, an oversized cross, a
// drop preview with three geometry faults, and undocking-over-the-canvas
// silently removed. Supplying a title bar is cheap; taking over what a title bar
// does is not.
//
// **Two traps in the timing.**
//
// `topLevelChanged(true)` arrives *during* the drag that floats the panel, and
// changing the decoration destroys and recreates the window -- which would break
// the gesture in progress, and that gesture is the one case that already worked.
// So it is applied only once nothing is held down, which is what the timer waits
// for.
//
// And it is taken off again when the panel docks, so the docked appearance is
// Qt's own and untouched. Only a floating panel wears ours.
class FloatingDockFrame : public QObject {
    Q_OBJECT

public:
    // Watches `dock` and owns nothing. Install one per dock.
    explicit FloatingDockFrame(QDockWidget* dock);

    // Whether the panel currently wears Qt's own decoration rather than the
    // window manager's -- which is what decides if a pen can press its title
    // bar. For a test to ask without inspecting window flags.
    bool titleBarIsOurs() const;
    // What the timer would do, run now. A test drives this rather than sleeping.
    void applyIfNothingIsHeld();

Q_SIGNALS:
    // The drag that floated this panel has finished, and nothing is held down.
    //
    // Emitted where the decoration is changed, because that moment is already
    // worked out here and there should be one thing deciding it rather than two.
    // **Issue #54 wants the same moment for an unrelated reason**: Qt leaves the
    // main window's layout frozen after a drag that ends with a panel outside
    // the window, and the cure has to be applied once the drag is really over.
    // See MainWindow::wakeLayout.
    void settled();

private:
    QWidget* makeTitleBar();
    void rememberQtsTitleBar();

    QDockWidget* dock_ = nullptr;
    QWidget* bar_ = nullptr;
    QTimer* waiting_ = nullptr;

    // What Qt's own title bar is, measured while the panel is **docked** --
    // which is the only time it is real. Floating with a native frame, Qt hides
    // its title bar and its buttons, so anything read off them then is a
    // leftover rather than a measurement. Three attempts at deriving these from
    // style metrics were each wrong in a different way.
    int qt_title_height_ = 0;
    QIcon qt_close_icon_;
    QSize qt_close_size_;
};
