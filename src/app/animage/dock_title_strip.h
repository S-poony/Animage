// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>
#include <QWidget>

class QDockWidget;
class QAbstractButton;

// A dock's title bar, drawn by the style rather than assembled out of widgets.
//
// This exists for `FloatingDockFrame` -- see there for why a floating panel needs
// a title bar of our own at all, which is issue #50.
//
// **It draws itself with `CE_DockWidgetTitle` and places its button with
// `SE_DockWidgetCloseButton`**, which are the same two calls `QDockWidget` makes
// for a docked panel. That is the whole design, and it replaced an attempt to
// reproduce the same appearance out of a `QLabel` and a `QToolButton` in a
// layout. Everything measurable about that attempt was eventually made to match
// -- the strip's height, the button's box, the icon and its resolution, each one
// captured from Qt's real title bar rather than derived -- and it still looked
// wrong, because a title bar is not a row of widgets. The style paints a
// background and a frame behind it, and nothing was painting them.
//
// The lesson is worth more than the code: when the job is "look exactly like
// this widget", ask the style for the same drawing rather than rebuilding the
// picture. Four attempts went into matching numbers before anyone noticed the
// rectangle was missing.
//
// **No mouse or tablet handlers, deliberately.** Qt reads a title bar widget's
// geometry to know where its own drag begins, so leaving every pointer event
// alone is what lets `QDockWidget` run the drag, the drop preview and the dock
// areas exactly as it does for a mouse. The button is a child widget and takes
// its own clicks, which is why the close button works with a pen.
class DockTitleStrip : public QWidget {
    Q_OBJECT

public:
    // Everything here is measured off the panel while it is **docked**, because
    // that is the only time Qt's own title bar is real -- see
    // FloatingDockFrame::rememberQtsTitleBar. Deriving any of it from style
    // metrics was tried four times and was wrong four times: the style's
    // arithmetic gives 26 px where Qt uses 24, and the button's icon is smaller
    // than any rect it sits in would suggest.
    DockTitleStrip(QDockWidget* dock, int height, const QIcon& close_icon, QSize close_size);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void placeButton();

    QDockWidget* dock_ = nullptr;
    QAbstractButton* close_ = nullptr;
    int height_ = 0;
};
