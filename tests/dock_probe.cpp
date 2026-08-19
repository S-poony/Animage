// SPDX-License-Identifier: GPL-3.0-or-later
//
// A plain Qt main window with docks in it, and none of Animage.
//
// **This is for asking questions about Qt that Animage cannot ask from inside
// itself.** When a fault involves `QMainWindow`, `QDockWidget` or the dock
// layout, the first question is always whether it is ours or Qt's, and the only
// answer worth having is a program with none of ours in it. Issue #54 is what it
// was built for -- a panel dragged out of the window stopped the layout running
// -- and it settled that in one run: plain Qt did it too.
//
// It is not a test and must not become one, for the same reasons `shots` is not.
// Nothing asserts, `ctest` never runs it, and it needs a real display and in
// places a real hand. It is a *probe*: it prints what Qt is doing so a person
// can read it.
//
// ```
// ./build/tests/dock_probe              a window to drag panels around by hand
// ./build/tests/dock_probe --bench      try the cures, print a table, exit
// ./build/tests/dock_probe --frameless  give the floating panel a title bar
//                                       widget, as FloatingDockFrame does
// ```
//
// Everything it prints goes to `dock_probe.log` beside the working directory as
// well as to stdout, because a Qt application on Windows is built `WIN32` and
// has no console to print to.
//
// **What it can see that a test cannot.** With Qt's private headers present it
// reads `QMainWindowLayout`'s own drag bookkeeping -- `savedState`,
// `currentGapPos`, `pluggingWidget`, `movingSeparator` -- and prints a line
// whenever any of it changes. That needs no unexported symbol:
// `QMainWindowLayoutState::isValid()` is `rect.isValid()`, and `rect` is a
// public member. Without those headers everything else still works and the state
// column reads "not available".
//
// It also turns on Qt's own `qt.widgets.dockwidgets` logging and folds it into
// the same log, in order, so Qt's account of a drag sits beside the
// measurements.
//
// **Two things learned using it, worth knowing before you trust it.**
//
// A synthetic drag sent to a `QDockWidget` leaves Qt's drag state machine half
// finished and produces convincing symptoms that are not real -- so the *drag*
// has to be done by hand. See the trap in docs/handover.md.
//
// But the state a hand produces can then be **forged**, which is what `--bench`
// does, and that is where the leverage is: measure the broken state once by
// hand, discover it is one flag, set that flag directly, and try a dozen
// candidate cures by machine in a second each.

#include <QApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QLibraryInfo>
#include <QLoggingCategory>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>
#include <vector>

#ifdef ANIMAGE_HAVE_QT_PRIVATE
#include <private/qmainwindowlayout_p.h>
#endif

namespace {

QFile* g_log = nullptr;
QPlainTextEdit* g_view = nullptr;
bool g_saying = false;

// Appending to the view lays it out, which delivers events, which is where the
// drag state is sampled from -- so writing a line can produce the next line. The
// file always gets it; the view only when we are not already inside a write.
void say(const QString& line) {
    QTextStream(stdout) << line << Qt::endl;
    if (g_log) {
        QTextStream(g_log) << line << "\n";
        g_log->flush();
    }
    if (g_view && !g_saying) {
        g_saying = true;
        g_view->appendPlainText(line);
        g_saying = false;
    }
}

QString stamp() {
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}

// Qt narrates unplug, gap insertion and plug under this category. Routing it
// into the same log is what makes it possible to see which branch of
// QDockWidgetPrivate::endDrag was taken, which is not otherwise observable.
bool g_in_handler = false;
void logQt(QtMsgType, const QMessageLogContext&, const QString& text) {
    if (g_in_handler) return;
    g_in_handler = true;
    say(QStringLiteral("%1  qt       %2").arg(stamp(), text));
    g_in_handler = false;
}

const char* eventName(QEvent::Type t) {
    switch (t) {
        case QEvent::MouseButtonPress: return "MousePress";
        case QEvent::MouseButtonRelease: return "MouseRelease";
        case QEvent::MouseButtonDblClick: return "MouseDblClick";
        case QEvent::MouseMove: return "MouseMove";
        case QEvent::NonClientAreaMouseButtonPress: return "NC-Press";
        case QEvent::NonClientAreaMouseButtonRelease: return "NC-Release";
        case QEvent::NonClientAreaMouseMove: return "NC-Move";
        case QEvent::NonClientAreaMouseButtonDblClick: return "NC-DblClick";
        default: return nullptr;
    }
}

// A central widget that paints itself opaquely, the way the canvas does, so a
// region it has stopped covering shows as unpainted window rather than being
// quietly filled in by whatever was behind it.
class Slab : public QWidget {
public:
    explicit Slab(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent);
        auto* box = new QVBoxLayout(this);
        box->setContentsMargins(12, 12, 12, 12);
        g_view = new QPlainTextEdit(this);
        g_view->setReadOnly(true);
        box->addWidget(g_view);
    }

protected:
    void paintEvent(QPaintEvent* e) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(40, 44, 52));
        QWidget::paintEvent(e);
    }
};

class Probe : public QMainWindow {
public:
    Probe() {
        // The Qt version goes in the title because that is the first thing to
        // check and the easiest to assume. Reading Qt's source at a branch that
        // is ahead of the tag you are running cost hours on issue #54.
        setWindowTitle(QStringLiteral("dock_probe -- Qt %1").arg(QT_VERSION_STR));
        setCentralWidget(new Slab(this));
        statusBar()->showMessage(QStringLiteral("status bar"));

        side_ = makeDock(QStringLiteral("Side"), QStringLiteral("sideDock"));
        addDockWidget(Qt::RightDockWidgetArea, side_);
        under_ = makeDock(QStringLiteral("Under"), QStringLiteral("underDock"));
        addDockWidget(Qt::BottomDockWidgetArea, under_);

        if (qApp->arguments().contains(QStringLiteral("--frameless"))) {
            // What FloatingDockFrame does, and the only lever that makes Qt
            // stand down from native decoration. See issue #50.
            side_->setTitleBarWidget(new QLabel(QStringLiteral(" Side"), side_));
        }

        resize(1100, 760);
        baseline_ = saveState();
        qApp->installEventFilter(this);
    }

    // What the window's children occupy against what the window is. This is the
    // whole symptom of issue #54 and it needs no private header: if the status
    // bar is not at the bottom, the layout did not run.
    void report(const QString& why) {
        QWidget* c = centralWidget();
        QWidget* sb = statusBar();
        const int got_bottom = sb ? sb->geometry().bottom() + 1 : -1;
        const bool adrift = sb && got_bottom != height();
        const bool too_wide = c && c->width() > width();

        say(QStringLiteral("%1  %2").arg(stamp(), why));
        say(QStringLiteral("  window          %1 x %2").arg(width()).arg(height()));
        if (c)
            say(QStringLiteral("  central widget  %1, %2   %3 x %4")
                    .arg(c->x()).arg(c->y()).arg(c->width()).arg(c->height()));
        if (sb)
            say(QStringLiteral("  status bar      %1, %2   %3 x %4   bottom %5, window bottom %6")
                    .arg(sb->x()).arg(sb->y()).arg(sb->width()).arg(sb->height())
                    .arg(got_bottom).arg(height()));
        say(QStringLiteral("  docks           Side %1, Under %2")
                .arg(side_->isFloating() ? QStringLiteral("FLOATING") : QStringLiteral("docked"),
                     under_->isFloating() ? QStringLiteral("FLOATING") : QStringLiteral("docked")));
        say(adrift || too_wide
                ? QStringLiteral("  ** FROZEN -- children are laid out for a window this is not **")
                : QStringLiteral("  ok -- children fill the window"));
        say(QString());
    }

    // Try each candidate cure against a forged frozen layout, and say which ones
    // work. The forging is the point: a hand test established that the whole of
    // issue #54 was one flag with everything else already cleaned up, so it can
    // be reproduced without a drag and the cures tried by machine.
    void runBench() {
#ifdef ANIMAGE_HAVE_QT_PRIVATE
        auto* mwl = static_cast<QMainWindowLayout*>(layout());
        if (!mwl) return;

        struct Cure {
            const char* name;
            std::function<void()> apply;
        };
        const std::vector<Cure> cures = {
            {"restoreState(saveState())", [this] { restoreState(saveState()); }},
            {"setFloating(false) then setFloating(true)", [this] {
                side_->setFloating(false);
                side_->setFloating(true);
            }},
            {"separator press and release, no move", [this] {
                // endSeparatorMove clears savedState unconditionally, and a
                // press with no move between moves nothing. Surgical, and
                // unusable in earnest: it needs a separator to exist, and with
                // every panel floating there is none.
                const QPoint p(width() / 2, under_->geometry().top() - 2);
                QMouseEvent press(QEvent::MouseButtonPress, p, mapToGlobal(p),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease, p, mapToGlobal(p),
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(this, &press);
                QCoreApplication::sendEvent(this, &release);
            }},
            {"hide() then show()", [this] { side_->hide(); side_->show(); }},
            {"setDockOptions, changed and put back", [this] {
                const auto was = dockOptions();
                setDockOptions(was ^ QMainWindow::AnimatedDocks);
                setDockOptions(was);
            }},
            {"addDockWidget again", [this] { addDockWidget(Qt::RightDockWidgetArea, side_); }},
            {"setTitleBarWidget, as issue #50 does", [this] {
                side_->setTitleBarWidget(new QLabel(QStringLiteral(" Side"), side_));
            }},
            {"invalidate, updateGeometry, activate", [this] {
                layout()->invalidate();
                updateGeometry();
                layout()->activate();
            }},
        };

        say(QString());
        say(QStringLiteral("==== which call unfreezes the layout? (Qt %1) ====").arg(QT_VERSION_STR));
        for (const auto& cure : cures) {
            restoreState(baseline_);
            side_->setFloating(true);
            QCoreApplication::sendPostedEvents();

            // QMainWindowLayoutState's copy assignment cannot be called from
            // outside Qt -- QDockAreaLayoutItem's destructor is not exported --
            // but isValid() is rect.isValid() and nothing else, and that is the
            // whole of what setGeometry tests.
            mwl->savedState.rect = mwl->layoutState.rect;

            const QRect before = side_->geometry();
            const bool frozen_before = probeFrozen();
            cure.apply();
            QCoreApplication::sendPostedEvents();
            const bool frozen_after = probeFrozen();
            const QRect after = side_->geometry();

            say(QStringLiteral("  %1").arg(QString::fromLatin1(cure.name), -44)
                + QStringLiteral("froze: %1   after: %2   savedState %3   panel %4%5")
                      .arg(frozen_before ? QStringLiteral("yes")
                                         : QStringLiteral("NO -- forge failed"),
                           frozen_after ? QStringLiteral("still frozen")
                                        : QStringLiteral("LAYOUT RUNS"),
                           mwl->savedState.rect.isValid() ? QStringLiteral("VALID")
                                                          : QStringLiteral("empty"),
                           side_->isFloating() ? QStringLiteral("floating")
                                               : QStringLiteral("RE-DOCKED"),
                           before == after ? QStringLiteral(", did not move")
                                           : QStringLiteral(", MOVED")));
        }
        say(QStringLiteral("==== end ===="));
        say(QString());
#else
        say(QStringLiteral("--bench needs Qt's private headers; this build has none."));
#endif
    }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QMainWindow::resizeEvent(e);
        // After the layout has had its turn at this resize.
        QMetaObject::invokeMethod(
            this, [this] { report(QStringLiteral("resize")); }, Qt::QueuedConnection);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (const char* name = eventName(event->type())) {
            QString who;
            for (QObject* o = watched; o; o = o->parent()) {
                if (o == side_) { who = QStringLiteral("Side dock"); break; }
                if (o == under_) { who = QStringLiteral("Under dock"); break; }
                if (o == this) { who = QStringLiteral("main window"); break; }
            }
            if (!who.isEmpty()) {
                const bool moving = event->type() == QEvent::MouseMove ||
                                    event->type() == QEvent::NonClientAreaMouseMove;
                if (!moving) {
                    auto* me = static_cast<QMouseEvent*>(event);
                    say(QStringLiteral("%1  event    %2 on %3   at %4,%5   buttons=%6")
                            .arg(stamp(), QString::fromLatin1(name), who)
                            .arg(me->globalPosition().toPoint().x())
                            .arg(me->globalPosition().toPoint().y())
                            .arg(int(me->buttons())));
                } else if (who != moves_) {
                    say(QStringLiteral("%1  event    %2 on %3 (further moves not listed)")
                            .arg(stamp(), QString::fromLatin1(name), who));
                    moves_ = who;
                }
            }
        }
        // Sampled per event rather than on a timer: a flag cleared and set again
        // inside one turn of the loop would fall between two timer samples.
        sampleDragState();
        return QMainWindow::eventFilter(watched, event);
    }

private:
    // Prints only when something changes, so the log is a list of transitions.
    void sampleDragState() {
#ifdef ANIMAGE_HAVE_QT_PRIVATE
        auto* mwl = static_cast<QMainWindowLayout*>(layout());
        if (!mwl) return;
        const QString now =
            QStringLiteral("savedState=%1 movingSeparator=%2 plugging=%3 gap=%4 grabber=%5")
                .arg(mwl->savedState.rect.isValid() ? QStringLiteral("VALID")
                                                    : QStringLiteral("empty"),
                     mwl->movingSeparator.isEmpty() ? QStringLiteral("none")
                                                    : QStringLiteral("HELD"),
                     mwl->pluggingWidget ? QStringLiteral("yes") : QStringLiteral("no"),
                     QString::number(mwl->currentGapPos.size()),
                     QWidget::mouseGrabber()
                         ? QString::fromLatin1(QWidget::mouseGrabber()->metaObject()->className())
                         : QStringLiteral("none"));
        if (now != last_state_) {
            say(QStringLiteral("%1  state    %2").arg(stamp(), now));
            last_state_ = now;
        }
#endif
    }

    // Resize, ask whether the children followed, and put the size back. That is
    // the whole symptom: setGeometry either ran or returned early.
    bool probeFrozen() {
        const QSize was = size();
        resize(was.width() + 40, was.height() + 40);
        QCoreApplication::sendPostedEvents();
        layout()->activate();
        const bool adrift = statusBar()->geometry().bottom() + 1 != height() ||
                            centralWidget()->width() > width();
        resize(was);
        QCoreApplication::sendPostedEvents();
        layout()->activate();
        return adrift;
    }

    QDockWidget* makeDock(const QString& title, const QString& name) {
        auto* d = new QDockWidget(title, this);
        // saveState skips a dock with no object name and warns about it.
        d->setObjectName(name);
        auto* body = new QLabel(QStringLiteral("  %1  ").arg(title), d);
        body->setMinimumSize(160, 90);
        body->setAutoFillBackground(true);
        d->setWidget(body);
        connect(d, &QDockWidget::topLevelChanged, this, [this, title](bool floating) {
            report(QStringLiteral("%1 is now %2")
                       .arg(title, floating ? QStringLiteral("FLOATING")
                                            : QStringLiteral("docked")));
        });
        return d;
    }

    QDockWidget* side_ = nullptr;
    QDockWidget* under_ = nullptr;
    QByteArray baseline_;
    QString last_state_;
    QString moves_;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QFile log(QStringLiteral("dock_probe.log"));
    if (log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) g_log = &log;
    QLoggingCategory::setFilterRules(QStringLiteral("qt.widgets.dockwidgets.debug=true"));
    qInstallMessageHandler(logQt);

    Probe w;
    w.show();

    say(QStringLiteral("Qt %1 at build, %2 at run, platform %3")
            .arg(QStringLiteral(QT_VERSION_STR), QLibraryInfo::version().toString(),
                 app.platformName()));
#ifndef ANIMAGE_HAVE_QT_PRIVATE
    say(QStringLiteral("Built without Qt's private headers: the drag state is not available."));
#endif
    say(QStringLiteral("By hand: drag a panel out of the window, let go, then resize the window."));
    say(QString());
    w.report(QStringLiteral("start"));

    if (app.arguments().contains(QStringLiteral("--bench"))) {
        QTimer::singleShot(600, &w, [&w] {
            w.runBench();
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
