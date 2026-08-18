// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QApplication>
#include <QPoint>
#include <QStyleHints>

// Whether a press is the second of a double tap, counted here rather than left
// to the platform.
//
// Qt turns a mouse's second press into QEvent::MouseButtonDblClick and never
// delivers it as a press, so a widget that wants a double click watches for that
// event and is done -- with a mouse. With a pen it depends on **who promotes**,
// and that is a platform decision, not a Qt one.
//
// A tablet event nobody accepts is promoted to mouse events. Which code does the
// promoting is decided by
// QWindowSystemInterfacePrivate::TabletEvent::platformSynthesizesMouse: when it
// is true -- and the Windows plugin sets it true -- Qt keeps out of it entirely
// and whatever the platform sends is what arrives. When it is false, Qt does the
// promoting itself. The two do not behave the same, which is the whole reason
// this class exists:
//
//   - **Qt's own promotion does produce a double click**, and it replaces the
//     second press rather than following it: press, release, MouseButtonDblClick,
//     release. Measured, not assumed. It pairs two taps using
//     touchDoubleTapDistance (10 px here) and not mouseDoubleClickDistance
//     (5 px) -- Qt keeps those apart because a hand holding a stylus is not a
//     hand holding a mouse -- and both share the one mouseDoubleClickInterval.
//   - **Windows Ink's promotion did not.** Reported with a real pen on Windows:
//     two taps arrived as two ordinary presses with no double click between
//     them, so QAbstractItemView's DoubleClicked trigger never fired and
//     double-clicking a name renamed nothing at all, while the same click with a
//     mouse worked.
//
// So this counts presses, which is what the Windows path gives it, and on the
// path where Qt promotes it simply never sees a second press and stays out of
// the way -- the DoubleClicked trigger has already done the job. Both routes
// work and neither can fire twice for one gesture, but not for the reason this
// comment used to give: it is not that a pen never produces a double click, it
// is that a pen never produces a double click *and* a second press.
//
// One edge that follows and is not handled here: on the promoting-by-Qt path the
// first press arms this and the second never arrives, so it stays armed. A
// further tap within the interval and inside the distance would then read as the
// second of a gesture that already finished as a double click. Narrow -- it
// needs three taps in 400 ms within 10 px -- and it costs a spurious rename
// rather than anything worse.
class DoubleTap {
public:
    bool isSecond(const QPoint& global, quint64 timestamp) {
        const auto interval = static_cast<quint64>(QApplication::doubleClickInterval());
        const int reach = QGuiApplication::styleHints()->touchDoubleTapDistance();
        const bool second = armed_ && timestamp >= when_ && timestamp - when_ <= interval &&
                            (global - at_).manhattanLength() <= reach;
        // A third tap is not the second of anything: without this, holding a
        // pen against one spot renames on every tap after the first.
        armed_ = !second;
        at_ = global;
        when_ = timestamp;
        return second;
    }

private:
    QPoint at_;
    quint64 when_ = 0;
    bool armed_ = false;
};
