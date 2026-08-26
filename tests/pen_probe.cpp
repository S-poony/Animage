// SPDX-License-Identifier: GPL-3.0-or-later
//
// The real Animage canvas, on screen, writing down every event the pen and the
// mouse actually send it.
//
// **The third probe, and it answers a question the other two do not**:
// `dock_probe` asks *is this Qt's?*, `window_probe` asks *is this ours?*, and
// this one asks **what did the hardware send?** — which is the question that
// cannot be answered by reading code at all, because the answer belongs to a
// tablet driver, a Windows Ink setting and a pen somebody is holding.
//
// ```
// ./build/tests/pen_probe          the real window, logging every pen and mouse event
// ```
//
// It exists because a synthetic event proves only that our routing does what we
// wrote. The thing worth knowing is what a real pen produces — and the two are
// different in ways that decide the design:
//
//   - **The barrel button is believed to arrive as a *mouse* right press.** If
//     that is right, then "Alt and the right button, dragged" is one gesture
//     spread across two event streams: the press is a mouse event and the drag
//     that follows it is a run of tablet moves. Everything about how issue #76
//     was fixed rests on that, and it rests on a report rather than a reading.
//   - **Whether the barrel *also* produces a `TabletPress`** decides whether the
//     fix has a hole in it. If it does, that press arrives with the same
//     modifiers and, before #76, would have been the eyedropper on its own.
//   - **Whether Windows Ink promotes anything here.** `CanvasWidget` accepts its
//     tablet events, and Qt only promotes what nobody accepted, so in principle
//     there should be no promoted mouse events at all. In principle.
//
// Everything goes to `pen_probe.log` beside the working directory as well as to
// stdout, because a Qt application on Windows is built `WIN32` and has no
// console to print to.
//
// --- If you are an agent reading this, this file is yours ---------------------
//
// The same standing as `tests/shots.cpp`, `tests/dock_probe.cpp` and
// `tests/window_probe.cpp`: change it, add the reading you need, delete the one
// in your way. Nothing depends on it — no test reads it, `ctest` never runs it,
// and the build does not care.
//
// The one thing worth keeping is that it prints **what the canvas then did**
// beside each event. An event log on its own says what arrived; the point of
// this is the join between what arrived and what it was taken to mean, because
// every fault in this area so far has been an event being read as the wrong
// instruction rather than an event going missing.

#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTextStream>
#include <QTimer>

#include "canvas_widget.h"
#include "main_window.h"

using namespace animage;

namespace {

QFile* g_log = nullptr;

void say(const QString& line) {
    QTextStream(stdout) << line << Qt::endl;
    if (g_log) {
        QTextStream(g_log) << line << "\n";
        g_log->flush();
    }
}

QString buttonName(Qt::MouseButton button) {
    switch (button) {
        case Qt::NoButton: return QStringLiteral("none");
        case Qt::LeftButton: return QStringLiteral("left");
        case Qt::RightButton: return QStringLiteral("right");
        case Qt::MiddleButton: return QStringLiteral("middle");
        default: return QStringLiteral("0x%1").arg(static_cast<int>(button), 0, 16);
    }
}

QString buttonsName(Qt::MouseButtons buttons) {
    if (buttons == Qt::NoButton) return QStringLiteral("none");
    QStringList held;
    if (buttons & Qt::LeftButton) held << QStringLiteral("left");
    if (buttons & Qt::RightButton) held << QStringLiteral("right");
    if (buttons & Qt::MiddleButton) held << QStringLiteral("middle");
    if (held.isEmpty()) held << QStringLiteral("0x%1").arg(static_cast<int>(buttons), 0, 16);
    return held.join(QLatin1Char('+'));
}

QString modifierNames(Qt::KeyboardModifiers modifiers) {
    if (modifiers == Qt::NoModifier) return QStringLiteral("none");
    QStringList held;
    if (modifiers & Qt::ShiftModifier) held << QStringLiteral("Shift");
    if (modifiers & Qt::ControlModifier) held << QStringLiteral("Ctrl");
    if (modifiers & Qt::AltModifier) held << QStringLiteral("Alt");
    if (modifiers & Qt::MetaModifier) held << QStringLiteral("Meta");
    return held.join(QLatin1Char('+'));
}

// Which device Qt says an event came from, and how confidently. The middle
// column of the log is this, because "a mouse event that is really the pen" is
// the whole difficulty of this area -- see eventIsSynthesisedFromPen in
// canvas_widget.cpp, which has to guess at it from timing.
QString deviceName(const QPointingDevice* device) {
    if (!device) return QStringLiteral("(no device)");
    QString kind;
    switch (device->type()) {
        case QInputDevice::DeviceType::Mouse: kind = QStringLiteral("mouse"); break;
        case QInputDevice::DeviceType::Stylus: kind = QStringLiteral("stylus"); break;
        case QInputDevice::DeviceType::Airbrush: kind = QStringLiteral("airbrush"); break;
        case QInputDevice::DeviceType::Puck: kind = QStringLiteral("puck"); break;
        case QInputDevice::DeviceType::TouchScreen: kind = QStringLiteral("touchscreen"); break;
        case QInputDevice::DeviceType::TouchPad: kind = QStringLiteral("touchpad"); break;
        default: kind = QStringLiteral("device%1").arg(static_cast<int>(device->type())); break;
    }
    QString tip;
    switch (device->pointerType()) {
        case QPointingDevice::PointerType::Pen: tip = QStringLiteral("/pen"); break;
        case QPointingDevice::PointerType::Eraser: tip = QStringLiteral("/eraser"); break;
        case QPointingDevice::PointerType::Cursor: tip = QStringLiteral("/cursor"); break;
        case QPointingDevice::PointerType::Generic: tip = QStringLiteral("/generic"); break;
        default: break;
    }
    return kind + tip + QStringLiteral(" \"") + device->name() + QLatin1Char('"');
}

// Every pen and mouse event the canvas is offered, and what the canvas was in
// the middle of afterwards.
//
// An event filter and not the widget's own handlers, so that this reads the same
// events `CanvasWidget` reads without being able to change what it does with
// them. A probe that alters the thing it measures is worse than none.
class Listener : public QObject {
public:
    explicit Listener(CanvasWidget* canvas) : canvas_(canvas) {
        clock_.start();
        canvas_->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        QString what;
        switch (event->type()) {
            case QEvent::TabletPress: what = QStringLiteral("TabletPress"); break;
            case QEvent::TabletMove: what = QStringLiteral("TabletMove"); break;
            case QEvent::TabletRelease: what = QStringLiteral("TabletRelease"); break;
            case QEvent::MouseButtonPress: what = QStringLiteral("MousePress"); break;
            case QEvent::MouseButtonDblClick: what = QStringLiteral("MouseDblClick"); break;
            case QEvent::MouseMove: what = QStringLiteral("MouseMove"); break;
            case QEvent::MouseButtonRelease: what = QStringLiteral("MouseRelease"); break;
            default: return QObject::eventFilter(watched, event);
        }

        QString line;
        if (auto* tablet = dynamic_cast<QTabletEvent*>(event)) {
            // Moves are the bulk of the log and most of them say nothing new, so
            // a hover with no button and no pressure is counted rather than
            // printed. The count is printed when something else happens, which
            // keeps "and then eleven moves went by" in the record without eleven
            // lines of it.
            if (event->type() == QEvent::TabletMove && tablet->buttons() == Qt::NoButton &&
                tablet->pressure() <= 0.0) {
                ++quiet_moves_;
                return QObject::eventFilter(watched, event);
            }
            line = QStringLiteral("%1  button %2  buttons %3  keys %4  pressure %5  at %6,%7")
                       .arg(what.leftJustified(14, QLatin1Char(' ')),
                            buttonName(tablet->button()), buttonsName(tablet->buttons()),
                            modifierNames(tablet->modifiers()))
                       .arg(tablet->pressure(), 0, 'f', 2)
                       .arg(static_cast<int>(tablet->position().x()))
                       .arg(static_cast<int>(tablet->position().y()));
            line += QStringLiteral("\n                  from %1")
                        .arg(deviceName(tablet->pointingDevice()));
        } else if (auto* mouse = dynamic_cast<QMouseEvent*>(event)) {
            if (event->type() == QEvent::MouseMove && mouse->buttons() == Qt::NoButton) {
                ++quiet_moves_;
                return QObject::eventFilter(watched, event);
            }
            line = QStringLiteral("%1  button %2  buttons %3  keys %4                at %5,%6")
                       .arg(what.leftJustified(14, QLatin1Char(' ')),
                            buttonName(mouse->button()), buttonsName(mouse->buttons()),
                            modifierNames(mouse->modifiers()))
                       .arg(static_cast<int>(mouse->position().x()))
                       .arg(static_cast<int>(mouse->position().y()));
            line += QStringLiteral("\n                  from %1")
                        .arg(deviceName(mouse->pointingDevice()));
        }

        if (quiet_moves_ > 0) {
            say(QStringLiteral("        ... %1 quiet moves").arg(quiet_moves_));
            quiet_moves_ = 0;
        }
        say(QStringLiteral("%1  %2").arg(sinceStart(), line));

        // What the canvas made of it, which is the half of this log that is not
        // available anywhere else -- and read *after* it has had it.
        //
        // A filter installed on a widget runs **before** that widget's own
        // handler, so reading the state here would report what the *previous*
        // event left and print it under this one's name. That is worse than not
        // reading it: a wrong attribution in an instrument is how a session goes
        // round the houses. Deferred by a zero timer instead -- the event is
        // delivered synchronously the moment this returns, and the timer fires
        // once that has finished. Two input events arriving inside one pass of
        // the loop could still interleave; nothing here has ever needed to be
        // tighter than that, and a line that looks impossible is the first thing
        // to suspect if one ever does.
        QTimer::singleShot(0, this, [this] {
            say(QStringLiteral("                  -> %1").arg(whatTheCanvasIsDoing()));
        });

        // Never consumed. A probe that alters what it measures is worse than
        // none, and `false` here is "carry on to the widget".
        Q_UNUSED(watched);
        return false;
    }

private:
    QString sinceStart() const {
        const qint64 ms = clock_.elapsed();
        return QStringLiteral("%1.%2")
            .arg(ms / 1000, 4)
            .arg(ms % 1000, 3, 10, QLatin1Char('0'));
    }

    // Read after every event rather than only when it changes, because the
    // question this probe exists for is "what did *that* event do", and a state
    // that did not change is an answer.
    QString whatTheCanvasIsDoing() const {
        QStringList doing;
        doing << (canvas_->isStroking() ? QStringLiteral("STROKING")
                                        : QStringLiteral("not stroking"));
        if (canvas_->toolRing().has_value()) doing << QStringLiteral("SIZE RING SHOWN");
        doing << QStringLiteral("radius %1").arg(canvas_->brushSettings().radius, 0, 'f', 1);
        doing << QStringLiteral("pointer says %1").arg(pointingName());
        return doing.join(QStringLiteral(", "));
    }

    QString pointingName() const {
        switch (canvas_->pointing()) {
            case CanvasWidget::Pointing::Draw: return QStringLiteral("draw");
            case CanvasWidget::Pointing::Erase: return QStringLiteral("erase");
            case CanvasWidget::Pointing::Pick: return QStringLiteral("PICK");
            case CanvasWidget::Pointing::PanReady: return QStringLiteral("pan ready");
            case CanvasWidget::Pointing::Panning: return QStringLiteral("panning");
            case CanvasWidget::Pointing::Zoom: return QStringLiteral("zoom");
            case CanvasWidget::Pointing::SizeBrush: return QStringLiteral("SIZE BRUSH");
            case CanvasWidget::Pointing::Lasso: return QStringLiteral("lasso");
            case CanvasWidget::Pointing::Nothing: return QStringLiteral("nothing");
            default: break;
        }
        return QStringLiteral("something else (%1)")
            .arg(static_cast<int>(canvas_->pointing()));
    }

    CanvasWidget* canvas_ = nullptr;
    QElapsedTimer clock_;
    int quiet_moves_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QFile log(QStringLiteral("pen_probe.log"));
    if (log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) g_log = &log;

    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<CanvasWidget*>();
    if (!canvas) {
        say(QStringLiteral("no canvas in the window; the probe cannot run"));
        return 1;
    }
    Listener listen(canvas);

    say(QStringLiteral("Qt %1, platform %2").arg(qVersion(), app.platformName()));

    say(QStringLiteral("Every pen and mouse event the canvas is offered, and what it did."));
    say(QStringLiteral("Quiet moves -- hovering with nothing held -- are counted, not printed."));
    say(QString());
    say(QStringLiteral("By hand, and each one is a different question:"));
    say(QStringLiteral("  #76  hold Alt, press the pen's BARREL button, and drag with the"));
    say(QStringLiteral("       nib in the air. The brush should resize."));
    say(QStringLiteral("       ** What does the barrel send? ** A MousePress with button"));
    say(QStringLiteral("       right is what the fix assumes. A TabletPress as well, or"));
    say(QStringLiteral("       instead, is a hole in it."));
    say(QStringLiteral("       Then, still holding it, REST THE NIB on the tablet."));
    say(QStringLiteral("       Nothing should say PICK and nothing should say STROKING,"));
    say(QStringLiteral("       and SIZE RING SHOWN should stay all the way through."));
    say(QStringLiteral("  #53  press the pen on a toolbar spin box, a menu, a panel row."));
    say(QStringLiteral("       Which of those arrive here at all, and as what?"));
    say(QString());
    // One synthetic event through the same path, so the log says whether the
    // instrument works before anybody trusts what it does not say.
    //
    // Worth the eight lines. The failure this catches is a log with nothing in
    // it after a careful five minutes with a pen -- which reads as "the pen
    // sends nothing", is the most interesting result this probe could produce,
    // and would be a lie if the filter were simply not installed. A marked line
    // below means everything after it is evidence; no marked line means fix the
    // probe first.
    {
        const QPointF middle(canvas->width() / 2.0, canvas->height() / 2.0);
        QMouseEvent self_test(QEvent::MouseMove, middle, canvas->mapToGlobal(middle),
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        say(QStringLiteral("-- self test: one synthetic move, to prove the log works --"));
        QCoreApplication::sendEvent(canvas, &self_test);
        QCoreApplication::processEvents();
        say(QStringLiteral("-- end of self test. Nothing between the two lines means the"));
        say(QStringLiteral("   probe is broken, not the pen. --"));
        say(QString());
    }


    return app.exec();
}
