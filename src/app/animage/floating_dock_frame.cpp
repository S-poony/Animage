// SPDX-License-Identifier: GPL-3.0-or-later
#include "floating_dock_frame.h"

#include "dock_title_strip.h"

#include <QAbstractButton>
#include <QDockWidget>
#include <QGuiApplication>
#include <QStyle>
#include <QTimer>

namespace {
// Long enough not to spin, short enough that the panel does not visibly wear a
// native frame for a moment after being let go.
constexpr int kWaitMs = 30;
}  // namespace

FloatingDockFrame::FloatingDockFrame(QDockWidget* dock) : QObject(dock), dock_(dock) {
    if (!dock_) return;

    // Measured once the window has been laid out, which is the first moment
    // Qt's own title bar is a real thing with a real size. See the members.
    QTimer::singleShot(0, this, [this] { rememberQtsTitleBar(); });

    waiting_ = new QTimer(this);
    waiting_->setInterval(kWaitMs);
    connect(waiting_, &QTimer::timeout, this, &FloatingDockFrame::applyIfNothingIsHeld);

    connect(dock_, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (!floating) {
            waiting_->stop();
            // Docked panels keep Qt's own title bar, so nothing about the
            // ordinary window changes. Ours is only for floating.
            if (dock_->titleBarWidget()) dock_->setTitleBarWidget(nullptr);
            return;
        }
        // Usually mid-drag, so this normally only starts the wait. See the
        // header for why it cannot simply be done here.
        applyIfNothingIsHeld();
        if (!titleBarIsOurs()) waiting_->start();
    });
}

// Qt's own title bar, while it still is one.
//
// A docked panel's contents start below its title bar, so where they start *is*
// its height -- and the buttons in it are children of the dock with their real
// icons and sizes on them. Read here and kept, because none of it is readable
// later: a floating panel with a native frame has Qt's title bar hidden, and a
// floating panel with ours has it replaced.
void FloatingDockFrame::rememberQtsTitleBar() {
    if (!dock_ || dock_->isFloating() || !dock_->widget()) return;
    const int tall = dock_->widget()->geometry().y();
    if (tall > 0) qt_title_height_ = tall;

    if (auto* qts = dock_->findChild<QAbstractButton*>(QStringLiteral("qt_dockwidget_closebutton"))) {
        if (!qts->icon().isNull()) qt_close_icon_ = qts->icon();
        qt_close_size_ = qts->sizeHint();
    }

}

bool FloatingDockFrame::titleBarIsOurs() const {
    return dock_ && dock_->titleBarWidget() != nullptr;
}

void FloatingDockFrame::applyIfNothingIsHeld() {
    if (!dock_) return;

    // Docked while we were waiting: the question no longer applies.
    if (!dock_->isFloating()) {
        if (waiting_) waiting_->stop();
        return;
    }
    if (titleBarIsOurs()) {
        if (waiting_) waiting_->stop();
        return;
    }
    // Still being dragged. Changing the decoration recreates the window, which
    // would take the panel out of the hand holding it -- and that gesture is the
    // one thing here that already worked.
    if (QGuiApplication::mouseButtons() != Qt::NoButton) return;

    if (waiting_) waiting_->stop();
    dock_->setTitleBarWidget(makeTitleBar());
}

// The title bar itself: a name and two buttons, and **no event handlers at all**.
//
// That is the point rather than an omission. Qt reads this widget's geometry to
// know where its own drag begins, so leaving every mouse and tablet event alone
// is what lets `QDockWidget` run the drag, draw the drop preview and choose the
// dock area exactly as it does for a mouse. Anything overridden here is a piece
// of Qt's behaviour silently replaced by a worse one -- which is what the
// previous attempt did, four regressions' worth.
QWidget* FloatingDockFrame::makeTitleBar() {
    if (bar_) return bar_;
    // Height measured off the docked panel rather than derived: the style's own
    // arithmetic for it came out 26 px against Qt's real 24. See
    // rememberQtsTitleBar, and DockTitleStrip for how the rest of it is drawn.
    bar_ = new DockTitleStrip(dock_, qt_title_height_, qt_close_icon_, qt_close_size_);
    return bar_;
}
