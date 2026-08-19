// SPDX-License-Identifier: GPL-3.0-or-later
//
// The real Animage window, on screen, printing what its docks do to each other.
//
// **The other half of `dock_probe`.** That one is plain Qt with none of this
// program in it, and it answers *is this Qt's?* This one is the same readings
// taken from `MainWindow`, and together they answer *is this ours?* — which is
// the only question worth asking first about a dock fault, and the one that was
// answered by reasoning twice and wrong both times before there was an
// instrument. See docs/handover.md.
//
// ```
// ./build/tests/window_probe          the real window, logging every dock change
// ```
//
// It exists because the faults it is for cannot be reached from code.
// `setFloating(true)` and `addDockWidget` do not enter Qt's drag, so they do not
// produce the state a hand produces — measured: the timeline holds its height
// and the layer dock holds its width through both, while a hand loses them. And
// a *synthetic* drag is worse than useless, leaving Qt's state machine half
// finished and reporting symptoms that are not there. So a hand is the only way
// in, and what a hand needs is a program that writes down what it did.
//
// Everything goes to `window_probe.log` beside the working directory as well as
// to stdout, because a Qt application on Windows is built `WIN32` and has no
// console to print to.
//
// --- If you are an agent reading this, this file is yours ---------------------
//
// The same standing as `tests/shots.cpp` and `tests/dock_probe.cpp`: change it,
// add the reading you need, delete the one in your way. Nothing depends on it —
// no test reads it, `ctest` never runs it, and the build does not care.
//
// **Why it is a separate file from `dock_probe` rather than a flag on it.** The
// two must not share a binary. `dock_probe` links Qt directly and has to stay
// free of Animage, because a reproducer containing our code proves nothing about
// Qt; this one links `animage_ui` because the whole point is that it is our
// window. A flag would put them in one target and quietly destroy the thing that
// makes the first one worth having. The forty lines they have in common are the
// price of that, and it is a good price.

#include <QApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QFile>
#include <QHash>
#include <QMainWindow>
#include <QEvent>
#include <QTextStream>
#include <QTimer>

#include <cstdlib>

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

QString stamp() {
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}

QString areaName(QMainWindow& window, QDockWidget* dock) {
    switch (window.dockWidgetArea(dock)) {
        case Qt::LeftDockWidgetArea: return QStringLiteral("left");
        case Qt::RightDockWidgetArea: return QStringLiteral("right");
        case Qt::TopDockWidgetArea: return QStringLiteral("top");
        case Qt::BottomDockWidgetArea: return QStringLiteral("bottom");
        default: return QStringLiteral("nowhere");
    }
}

// Every dock's size, against the size it was at the last report.
//
// **The difference is the reading**, which is why the previous size is carried
// rather than left to whoever is reading the log. Issues #57 and #55 are both a
// dock coming back a few pixels different from how it left, and a column of
// absolute numbers taken two screens apart makes the reader do the subtraction —
// which is exactly the step at which "it looks a bit short" stops being evidence.
//
// The hint and the minimum sit beside it because they are the two sizes Qt could
// plausibly be falling back to. A dock that lands on its hint has been re-fitted;
// one that lands on its minimum has been squeezed; one on neither has given the
// space to a neighbour. The timeline dock is the interesting case: it is held
// well above its own hint by `MainWindow::syncTimelineHeight`, so every pixel
// between the two is a pixel something could take.
class Watcher : public QObject {
public:
    explicit Watcher(QMainWindow& window) : window_(window) {
        for (QDockWidget* dock : window_.findChildren<QDockWidget*>()) {
            connect(dock, &QDockWidget::topLevelChanged, this, [this, dock](bool floating) {
                report(QStringLiteral("%1 is now %2")
                           .arg(dock->windowTitle(), floating ? QStringLiteral("FLOATING")
                                                              : QStringLiteral("docked")));
            });
            // A dock changing side never floats, so topLevelChanged never fires
            // for it — drag the Layers panel from the right edge straight to the
            // left and this is the only signal there is. That is #55's gesture.
            connect(dock, &QDockWidget::dockLocationChanged, this, [this, dock](Qt::DockWidgetArea) {
                report(QStringLiteral("%1 is now in the %2 area")
                           .arg(dock->windowTitle(), areaName(window_, dock)));
            });
            dock->installEventFilter(this);
        }

        // A hand dragging a splitter emits no dock signal at all, so without
        // this the next reading carries a "was" from before the drag and prints
        // the hand's own work as though the panel had jumped. That happened on
        // the first run of this: a panel widened by 192 px and then floated
        // reported "192 px wider" against floating, which is nobody's bug.
        //
        // Coalesced, because a drag is a hundred resizes and a hundred lines is
        // not a reading. The delay only has to outlast the gap between two
        // moves of a hand.
        settling_ = new QTimer(this);
        settling_->setSingleShot(true);
        settling_->setInterval(250);
        // And only when something actually differs: a float and a re-dock resize
        // the dock too and already have a line each, so without this every one
        // of them would be followed by an empty second reading.
        connect(settling_, &QTimer::timeout, this, [this] {
            for (QDockWidget* dock : window_.findChildren<QDockWidget*>()) {
                if (last_size_.value(dock, dock->size()) != dock->size()) {
                    report(QStringLiteral("a panel was resized by hand"));
                    return;
                }
            }
        });
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        // Only a resize nothing else has announced. A float or a re-dock resizes
        // the dock too, and those already have a line of their own.
        if (event->type() == QEvent::Resize && settling_) settling_->start();
        return QObject::eventFilter(watched, event);
    }

public:

    void report(const QString& why) {
        say(QStringLiteral("%1  %2").arg(stamp(), why));
        say(QStringLiteral("  window          %1 x %2").arg(window_.width()).arg(window_.height()));
        if (QWidget* c = window_.centralWidget()) {
            say(QStringLiteral("  canvas          %1, %2   %3 x %4")
                    .arg(c->x()).arg(c->y()).arg(c->width()).arg(c->height()));
        }
        for (QDockWidget* dock : window_.findChildren<QDockWidget*>()) sayDock(dock);
        say(QString());
    }

private:
    void sayDock(QDockWidget* dock) {
        const QSize now = dock->size();
        const QSize was = last_size_.value(dock, now);
        last_size_[dock] = now;

        QString moved;
        if (now.width() != was.width())
            moved += QStringLiteral("  ** %1 px %2 **")
                         .arg(std::abs(now.width() - was.width()))
                         .arg(now.width() < was.width() ? QStringLiteral("NARROWER")
                                                        : QStringLiteral("wider"));
        if (now.height() != was.height())
            moved += QStringLiteral("  ** %1 px %2 **")
                         .arg(std::abs(now.height() - was.height()))
                         .arg(now.height() < was.height() ? QStringLiteral("SHORTER")
                                                          : QStringLiteral("taller"));

        say(QStringLiteral("  %1%2 x %3   was %4 x %5   hint %6 x %7   min %8 x %9   %10%11")
                .arg(dock->windowTitle().leftJustified(16, QLatin1Char(' ')))
                .arg(now.width()).arg(now.height())
                .arg(was.width()).arg(was.height())
                .arg(dock->sizeHint().width()).arg(dock->sizeHint().height())
                .arg(dock->minimumSizeHint().width()).arg(dock->minimumSizeHint().height())
                .arg(dock->isFloating() ? QStringLiteral("FLOATING") : areaName(window_, dock),
                     moved));
    }

    QMainWindow& window_;
    QHash<QDockWidget*, QSize> last_size_;
    QTimer* settling_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QFile log(QStringLiteral("window_probe.log"));
    if (log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) g_log = &log;

    MainWindow window;
    window.resize(1400, 900);
    window.show();
    // After the first show: the docks are laid out and their title bars have a
    // height by then, which is the same reason MainWindow takes its own default
    // layout from showEvent rather than from the constructor.
    QCoreApplication::processEvents();

    Watcher watch(window);
    say(QStringLiteral("Qt %1, platform %2").arg(qVersion(), app.platformName()));
    say(QStringLiteral("By hand, and each one is a different question:"));
    say(QStringLiteral("  #57  drag Timeline out of the window, then drag it back in."));
    say(QStringLiteral("       Does it come back shorter than it left?"));
    say(QStringLiteral("       ** Do NOT drag it taller first. ** The reporter found"));
    say(QStringLiteral("       it only loses height when it has been left alone --"));
    say(QStringLiteral("       a dock that has been dragged keeps what it was given."));
    say(QStringLiteral("  #55  drag Layers from the right edge to the left."));
    say(QStringLiteral("       Does it arrive a different width?"));
    say(QStringLiteral("Anything marked ** changed since the line above it."));
    say(QString());
    watch.report(QStringLiteral("start"));

    return app.exec();
}
