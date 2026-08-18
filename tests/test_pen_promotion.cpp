// SPDX-License-Identifier: GPL-3.0-or-later
//
// What Qt does with a pen event nobody accepts. Issue #53.
//
// **This is the boundary the rest of the suite cannot reach**, and the reason it
// could not is worth stating: every other test here builds a `QTabletEvent` and
// sends it straight to a widget, which skips Qt's hit-testing, its promotion and
// its double-click detection entirely. Those tests can show that a handler works
// when somebody hands it an event; they cannot show what arrives.
//
// `QWindowSystemInterface::handleTabletEvent` injects *at* the platform
// boundary, so everything above it runs. That is what these assert.
//
// **The honest limit, and it is half the mechanism.** Whether Qt promotes at all
// is decided by `QWindowSystemInterfacePrivate::TabletEvent::platformSynthesizesMouse`:
// true -- and the Windows plugin sets it true -- means Qt keeps out entirely and
// whatever the platform sends is what arrives. So on Windows the promoted events
// are Windows Ink's, and no test here can reach them: injecting at Qt's boundary
// means Windows never sees a pen and so never promotes. A green run here is not
// a statement that a real stylus works, and #53 says so too.
//
// What it does pin is Qt's half, which is the half that could change silently
// under a Qt upgrade -- and which was written down wrongly for months, as "a pen
// produces no double click", until it was measured.

#include <QApplication>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QStyleHints>
#include <QTabletEvent>
#include <QWidget>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>
#include <qpa/qwindowsysteminterface_p.h>

#include <algorithm>
#include <vector>

#include "testing.h"

namespace {

std::vector<QEvent::Type> seen;

// Never accepts a tablet event, which is the condition promotion depends on --
// and is exactly what the layer panel and the timeline do.
class Probe : public QWidget {
public:
    void tabletEvent(QTabletEvent* e) override { e->ignore(); }
    void mousePressEvent(QMouseEvent* e) override { note(e); }
    void mouseReleaseEvent(QMouseEvent* e) override { note(e); }
    void mouseDoubleClickEvent(QMouseEvent* e) override { note(e); }

private:
    void note(QMouseEvent* e) {
        seen.push_back(e->type());
        e->accept();
    }
};

bool sawA(QEvent::Type type) {
    return std::find(seen.begin(), seen.end(), type) != seen.end();
}

// One tap: down then up at the same spot.
void tap(QWindow* w, const QPointingDevice* stylus, QPointF at, ulong when) {
    const QPointF global = w->mapToGlobal(at.toPoint());
    QWindowSystemInterface::handleTabletEvent(w, when, stylus, at, global, Qt::LeftButton, 1.0, 0, 0,
                                              0, 0, 0);
    QWindowSystemInterface::handleTabletEvent(w, when + 10, stylus, at, global, Qt::NoButton, 0.0, 0,
                                              0, 0, 0, 0);
    QWindowSystemInterface::flushWindowSystemEvents();
    QCoreApplication::processEvents();
}

struct Fixture {
    Probe probe;
    QPointingDevice stylus{QStringLiteral("test stylus"),
                           1,
                           QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Pen,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                           1,
                           1};

    Fixture() {
        probe.resize(400, 400);
        probe.show();
        QWindowSystemInterface::flushWindowSystemEvents();
        QCoreApplication::processEvents();
    }
    QWindow* window() { return probe.windowHandle(); }

    void twoTaps(QPointF first, QPointF second, ulong gap) {
        seen.clear();
        tap(window(), &stylus, first, 1000);
        tap(window(), &stylus, second, 1000 + gap);
    }
};

// Every test here is about what Qt itself does, so each one asks for Qt to be
// the promoter and puts the flag back afterwards. Left alone, the value is the
// platform's -- true on Windows -- and nothing would be promoted at all.
struct QtPromotes {
    bool was = QWindowSystemInterfacePrivate::TabletEvent::platformSynthesizesMouse;
    QtPromotes() { QWindowSystemInterfacePrivate::TabletEvent::setPlatformSynthesizesMouse(false); }
    ~QtPromotes() { QWindowSystemInterfacePrivate::TabletEvent::setPlatformSynthesizesMouse(was); }
};

void whoPromotesIsAPlatformDecision() {
    TEST("with the platform promoting, Qt sends no mouse events of its own");
    // The fact the whole issue turned on, and the one that makes every claim
    // below conditional. On Windows this is the live setting.
    QWindowSystemInterfacePrivate::TabletEvent::setPlatformSynthesizesMouse(true);
    Fixture f;
    if (!f.window()) return;
    f.twoTaps({200, 200}, {200, 200}, 50);

    CHECK(seen.empty());
    CHECK(!sawA(QEvent::MouseButtonPress));
    CHECK(!sawA(QEvent::MouseButtonDblClick));
}

void qtsOwnPromotionDoesSendADoubleClick() {
    TEST("with Qt promoting, two taps produce a double click and not a second press");
    // Written down wrongly for months as "a pen produces no double click". It
    // does -- and it *replaces* the second press rather than following it, which
    // is what makes DoubleTap and Qt's own DoubleClicked trigger safe to have
    // both of: neither can fire twice for one gesture.
    QtPromotes promoting;
    Fixture f;
    if (!f.window()) return;
    f.twoTaps({200, 200}, {200, 200}, 50);

    CHECK(sawA(QEvent::MouseButtonPress));
    CHECK(sawA(QEvent::MouseButtonDblClick));
    // Exactly one press: the second tap arrived as the double click instead.
    CHECK_EQ(static_cast<int>(std::count(seen.begin(), seen.end(), QEvent::MouseButtonPress)), 1);
}

void aPenIsPairedAtTheTouchDistance() {
    TEST("two taps pair at the touch distance and not the mouse one");
    // The generous half, and the reason DoubleTap measures with
    // touchDoubleTapDistance: a hand holding a stylus is not a hand holding a
    // mouse, and two taps meant as one gesture land several pixels apart.
    QtPromotes promoting;
    const int mouse_d = QGuiApplication::styleHints()->mouseDoubleClickDistance();
    const int touch_d = QGuiApplication::styleHints()->touchDoubleTapDistance();
    // If a Qt release ever makes these equal there is nothing to tell apart, and
    // the test would pass while asserting nothing. Say so instead.
    CHECK(touch_d > mouse_d);
    if (touch_d <= mouse_d) return;

    Fixture f;
    if (!f.window()) return;
    // Between the two: past what a mouse would be allowed, inside what a stylus
    // is.
    const double between = (mouse_d + touch_d) / 2.0;
    f.twoTaps({200, 200}, {200 + between, 200}, 50);
    CHECK(sawA(QEvent::MouseButtonDblClick));
}

void tapsTooFarApartInTimeDoNotPair() {
    TEST("two taps beyond the double-click interval stay two presses");
    QtPromotes promoting;
    Fixture f;
    if (!f.window()) return;

    const ulong past = static_cast<ulong>(QGuiApplication::styleHints()->mouseDoubleClickInterval()) +
                       500;
    f.twoTaps({200, 200}, {200, 200}, past);

    CHECK(!sawA(QEvent::MouseButtonDblClick));
    CHECK_EQ(static_cast<int>(std::count(seen.begin(), seen.end(), QEvent::MouseButtonPress)), 2);
}

void thereIsOnlyOneInterval() {
    TEST("there is one double-click interval and two distances");
    // Recorded because it is the shape of the thing and it is easy to misremember
    // the other way round -- the margin Qt widens for a pen is the distance, and
    // the time is shared with the mouse.
    CHECK(QGuiApplication::styleHints()->mouseDoubleClickInterval() > 0);
    CHECK(QGuiApplication::styleHints()->touchDoubleTapDistance() >
          QGuiApplication::styleHints()->mouseDoubleClickDistance());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    whoPromotesIsAPlatformDecision();
    qtsOwnPromotionDoesSendADoubleClick();
    aPenIsPairedAtTheTouchDistance();
    tapsTooFarApartInTimeDoNotPair();
    thereIsOnlyOneInterval();

    return testing::summarise("pen promotion");
}
