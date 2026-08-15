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
// event and is done. **A pen does not produce one.** A tablet event nobody
// accepts is promoted to a mouse press and a mouse release -- by Windows Ink, or
// by Qt where the platform does not -- and two taps arrive as two ordinary
// presses with no double click anywhere between them. Reported with a real pen:
// double-clicking a name renamed nothing at all, while the same click with a
// mouse worked.
//
// So the two ways in are exclusive by construction and both are needed: a mouse
// arrives as MouseButtonDblClick and never as a second press, and a pen arrives
// as a second press and never as MouseButtonDblClick. Neither can fire twice for
// one gesture.
//
// The distance is the *touch* one and not the mouse one -- Qt keeps them apart
// because a hand holding a stylus is not a hand holding a mouse, and two taps
// that a person means as one gesture land several pixels apart.
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
