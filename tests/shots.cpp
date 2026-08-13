// SPDX-License-Identifier: GPL-3.0-or-later
//
// Not a test -- a way to look at the interface, for issue #28.
//
// Every interface bug this project has recorded was caught by looking, and none
// by a green build: the mis-encoded character, the "Add colour layer" button an
// edit silently failed to add, the two identical red swatches with nothing to
// say which was the control, the transform box drawn 128 pixels clear of the
// drawing on every side. What was missing was never the will to look but the
// scaffolding -- building lasso and transform meant writing a throwaway
// screenshot function into test_canvas.cpp, building, looking and deleting it
// again, four times. A cycle that costs a build and leaves debris in a test file
// is a cycle nobody runs when they are nearly finished, which is exactly when it
// is worth running.
//
// So this is that function, kept. It drives the real MainWindow through a list
// of named situations and writes one PNG each.
//
// **Nothing here asserts.** It is a stopwatch's cousin rather than a test: it
// lives with the benchmarks, `ctest` never runs it, and it carries the
// benchmarks' instruction -- run it before and after anything that touches the
// canvas. Golden-image comparison is deliberately absent and would be a mistake:
// font rendering differs across platforms and Qt versions, so it would go red on
// CI for reasons that are not bugs, and a red CI that means nothing is worse
// than no CI at all.
//
// Run it:
//
//   cmake --build build --target shots
//   ./build/tests/shots                every situation, into build/shots/
//   ./build/tests/shots --list         their names and what each is for
//   ./build/tests/shots transform      only the ones whose name says transform
//   ./build/tests/shots -o somewhere   write them somewhere else
//
// It runs offscreen unless something else has been asked for, so it needs no
// display and no `-platform` flag. Every file it writes is printed with its
// path, because the next thing anyone does is open one.
//
// --- If you are an agent reading this, this file is yours ---------------------
//
// You are allowed -- expected -- to change it. Add the situation you need,
// bend one that is nearly right, delete one that is in your way. Nothing depends
// on any of these being present: no test reads them, no reference images have to
// be kept in step, and the build does not care. A situation added to chase one
// bug and deleted afterwards has cost a recompile of one file and left no debris
// anywhere, which is the whole reason this exists instead of the throwaway
// function it replaces.
//
// Two things are worth keeping as they are. The three-line shape of a situation:
// one nobody can add in three lines is one nobody adds at the end of an
// afternoon, and the end of an afternoon is when looking pays. And the honesty
// about what a screenshot cannot show -- `QWidget::grab()` renders the widget
// and never the pointer, which is why the cursors are read off the widget
// instead. See "what the pointer says" in docs/handover.md.

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QStatusBar>
#include <QTimer>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QLineEdit>
#include <QPushButton>
#include <QRect>
#include <QString>
#include <QToolBar>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "canvas_widget.h"
#include "document.h"
#include "main_window.h"
#include "shortcuts.h"
#include "shortcuts_dialog.h"
#include "timeline_widget.h"

using namespace animage;
using Id = shortcuts::Id;

namespace {

// Big enough that no panel is scrolled and nothing in a menu is elided. The
// same size test_canvas uses, so a situation that misbehaves here can be moved
// into a test without the layout changing underneath it.
constexpr int kWindowWidth = 1400;
constexpr int kWindowHeight = 900;

constexpr double kTau = 6.283185307179586;

// A cursor bitmap is 32 px square, which is unreadable at the size it is used.
constexpr int kCursorMagnify = 4;

const char* nameOf(CanvasWidget::Pointing pointing) {
    using Pointing = CanvasWidget::Pointing;
    switch (pointing) {
        case Pointing::Draw: return "Draw";
        case Pointing::Erase: return "Erase";
        case Pointing::Pick: return "Pick";
        case Pointing::PanReady: return "PanReady";
        case Pointing::Panning: return "Panning";
        case Pointing::Zoom: return "Zoom";
        case Pointing::SizeBrush: return "SizeBrush";
        case Pointing::Lasso: return "Lasso";
        case Pointing::Move: return "Move";
        case Pointing::Rotate: return "Rotate";
        case Pointing::ScaleHorizontal: return "ScaleHorizontal";
        case Pointing::ScaleVertical: return "ScaleVertical";
        case Pointing::ScaleFalling: return "ScaleFalling";
        case Pointing::ScaleRising: return "ScaleRising";
        case Pointing::Nothing: return "Nothing";
    }
    return "?";
}

// Only the shapes this program actually asks for. Anything else is a decision
// somebody has just made and has not written down here yet.
const char* nameOf(Qt::CursorShape shape) {
    switch (shape) {
        case Qt::ArrowCursor: return "Qt::ArrowCursor";
        case Qt::CrossCursor: return "Qt::CrossCursor";
        case Qt::OpenHandCursor: return "Qt::OpenHandCursor";
        case Qt::ClosedHandCursor: return "Qt::ClosedHandCursor";
        case Qt::SizeHorCursor: return "Qt::SizeHorCursor";
        case Qt::SizeVerCursor: return "Qt::SizeVerCursor";
        case Qt::SizeBDiagCursor: return "Qt::SizeBDiagCursor";
        case Qt::SizeFDiagCursor: return "Qt::SizeFDiagCursor";
        case Qt::SizeAllCursor: return "Qt::SizeAllCursor";
        case Qt::ForbiddenCursor: return "Qt::ForbiddenCursor";
        case Qt::BitmapCursor: return "drawn here";
        default: return "another system cursor";
    }
}

// What the pointer was, somewhere it was put on purpose. A drawn cursor's shape
// is BitmapCursor whatever is on it, so the decision is recorded beside the
// glyph: the picture alone cannot tell the rubber from the pipette.
struct CursorRow {
    QString about;
    QString decided;
    QCursor cursor;
};

// The window, laid out and ready to be photographed, with everything a
// situation needs to reach.
//
// MainWindow keeps its widgets private, so they are found by type. That is what
// test_canvas does and for the same reason: the alternative is a friend
// declaration or a pile of accessors that exist for nothing but this.
struct Stage {
    MainWindow window;
    CanvasWidget* canvas = nullptr;
    TimelineWidget* timeline = nullptr;
    QTreeWidget* layer_list = nullptr;

    // What to save instead of the window. Left empty -- which is what nearly
    // every situation wants -- the window is what is photographed.
    QImage picture;

    Stage() {
        window.resize(kWindowWidth, kWindowHeight);
        window.show();
        canvas = window.findChild<CanvasWidget*>();
        timeline = window.findChild<TimelineWidget*>();
        layer_list = window.findChild<QTreeWidget*>();
        settle();
    }

    Document& doc() { return window.documentForTesting(); }

    // The track the interface is working on. Asked rather than assumed to be
    // the first: a situation that adds one is working on the one it added.
    TrackId track() const { return timeline ? timeline->track() : kNoId; }

    void settle(int rounds = 3) {
        for (int i = 0; i < rounds; ++i) QCoreApplication::processEvents();
    }

    // Real time passing, with the event loop running through it. `settle` is a
    // fixed number of rounds and is what almost everything wants; this is for
    // the situations that photograph something which only exists after a while
    // -- playback's rate readout says nothing until its window has filled, so a
    // fixed number of rounds would picture the opening claim and not a reading.
    //
    // A real nested loop and *not* `while (elapsed < ms) processEvents()`, which
    // is what this was first. Polling like that never lets the loop idle, and
    // widget repaints are flushed on the idle pass -- so a 24 fps playback
    // photographed through it painted about twenty times a second and the rate
    // readout quite correctly called it dropping. The program was fine and the
    // harness was the thing losing frames. bench_playback, which has always used
    // a nested loop here, paints at 24 and was the measurement that said so.
    void spin(int milliseconds) {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
        loop.exec();
    }

    // --- doing things ------------------------------------------------------

    // A menu item, by what it means rather than by the key it happens to be on.
    void press(Id id) {
        if (QAction* action = window.actionForTesting(id)) action->trigger();
        settle();
    }

    // Anything with a label instead: a button in the panels, or one of the menu
    // items the shortcut table does not name because they have no key.
    void choose(const char* label) {
        const QString wanted = QString::fromUtf8(label);
        for (QPushButton* button : window.findChildren<QPushButton*>()) {
            if (button->text() == wanted) {
                button->click();
                settle();
                return;
            }
        }
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().remove(QLatin1Char('&')) == wanted) {
                action->trigger();
                settle();
                return;
            }
        }
        std::printf("      nothing called \"%s\" to press -- has it been renamed?\n", label);
    }

    QPointF centre() const { return QRectF(canvas->rect()).center(); }

    // A stroke through the real handlers: press, move, release, exactly as a
    // mouse does it. Points are in the canvas widget's own coordinates, so
    // centre() and an offset from it survive the window being resized.
    void stroke(const std::vector<QPointF>& through) {
        if (through.size() < 2) return;
        sendMouse(canvas, QEvent::MouseButtonPress, through.front(), Qt::LeftButton,
                  Qt::LeftButton);
        for (std::size_t i = 1; i < through.size(); ++i) {
            sendMouse(canvas, QEvent::MouseMove, through[i], Qt::NoButton, Qt::LeftButton);
        }
        sendMouse(canvas, QEvent::MouseButtonRelease, through.back(), Qt::LeftButton,
                  Qt::NoButton);
        settle();
    }

    // --- a gesture, on any widget, said one piece at a time -----------------
    //
    // Two reasons this is not another stroke(). The canvas is not the only
    // thing in the window with gestures on it -- the timeline has four, and the
    // layer panel one -- and a situation that had to build QMouseEvents by hand
    // to reach them is a situation nobody writes at the end of an afternoon,
    // which is the whole of what this file is for.
    //
    // And the interesting pictures are the ones taken *during* a gesture. A
    // drop caret exists only while a row is in hand; so does the range an
    // overwriting drop would take over, and the closed hand above both. So
    // nothing here releases for you: a situation that wants the picture
    // mid-drag simply does not call releaseOn, and one that wants the result
    // does.
    void pressOn(QWidget* on, QPointF at) {
        pointer_ = at;
        sendMouse(on, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        settle(1);
    }

    // Several moves and not one jump, from wherever the last one left off. A
    // widget that decides for itself when a press has become a drag is watching
    // the pointer cross a threshold, and a single leap to the destination is a
    // drag some handlers never notice starting.
    void dragTo(QWidget* on, QPointF at, int steps = 6) {
        const QPointF from = pointer_;
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            sendMouse(on, QEvent::MouseMove, from + (at - from) * t, Qt::NoButton,
                      Qt::LeftButton);
        }
        pointer_ = at;
        settle(1);
    }

    void releaseOn(QWidget* on, QPointF at) {
        pointer_ = at;
        sendMouse(on, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
        settle();
    }

    // Enough steps that the brush lays a continuous line rather than two dabs.
    void line(QPointF from, QPointF to, int steps = 16) {
        std::vector<QPointF> through;
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            through.push_back(from + (to - from) * t);
        }
        stroke(through);
    }

    // A closed curve, which is what line art is made of and the only thing a
    // colour fill has an inside to fill.
    void circle(QPointF middle, double radius, int steps = 48) {
        std::vector<QPointF> through;
        for (int i = 0; i <= steps; ++i) {
            const double t = i * kTau / steps;
            through.push_back(middle + QPointF(radius * std::cos(t), radius * std::sin(t)));
        }
        stroke(through);
    }

    void hover(QPointF at) { hover(canvas, at); }
    void hover(QWidget* on, QPointF at) {
        pointer_ = at;
        sendMouse(on, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
        settle(1);
    }

    // Held keys are read from the events rather than from the machine -- the
    // canvas is deliberately not asking QGuiApplication, which would answer
    // about whichever keys the person running this happens to be leaning on.
    //
    // So a modifier has to be said twice, and this is the trap: the canvas
    // takes Alt from the key event *and* re-reads it off every mouse event
    // afterwards, because a real window system stamps the live modifier state
    // on all of them. A hover sent with NoModifier therefore un-holds it, and
    // the eyedropper cursor was reported absent when it was the harness that
    // had let go. `held_` is what stamps it back on.
    void hold(int key) {
        held_ |= modifierFor(key);
        sendKey(QEvent::KeyPress, key);
    }
    void letGo(int key) {
        held_ &= ~modifierFor(key);
        sendKey(QEvent::KeyRelease, key);
    }

    // Nothing in the interface can lock a layer yet -- the model carries the
    // flag and the panel has no switch for it -- so it is set where it lives.
    // That absence is itself worth seeing, which is why the situation exists.
    void lockActiveLayer() {
        Track* on = doc().mutableScene().findTrack(track());
        if (!on) return;
        const Layer* layer = on->findLayer(canvas->activeLayer());
        if (!layer) return;
        Layer locked = *layer;
        locked.locked = true;
        doc().updateLayer(track(), locked.id, locked);
        canvas->refreshAll();
        settle();
    }

    // A fill is asked for by a paint and worked out on a worker, and the first
    // answer is a coarse one that a finer solve replaces. So the picture has to
    // be asked for before there is anything to wait for, and asked for again
    // before the coarse answer is the last word.
    void settleColour() {
        for (int i = 0; i < 3; ++i) {
            canvas->grab();
            window.waitForColour();
            settle();
        }
        canvas->refreshAll();
        settle();
    }

    // --- what to photograph ------------------------------------------------

    QWidget* dockCalled(const char* title) const {
        const QString wanted = QString::fromUtf8(title);
        for (QDockWidget* dock : window.findChildren<QDockWidget*>()) {
            if (dock->windowTitle() == wanted) return dock;
        }
        return nullptr;
    }
    QWidget* layerPanel() const { return dockCalled("Layers"); }
    QWidget* timelinePanel() const { return dockCalled("Timeline"); }

    // One panel on its own, magnified. Worth having: a picture of the whole
    // window is 1400 px across, and what was wrong with the colour panel was
    // two swatches the same shade of red.
    //
    // It stops at the last thing in the widget rather than at the widget's own
    // edge, because magnifying empty space costs what it magnifies: a toolbar
    // is as wide as the window and its controls end halfway, so 3x of the whole
    // strip is 4200 px of which two thirds is grey, and anything that then fits
    // that width to a screen has thrown away more than the magnification added.
    // Measured from the top-left corner out, so a dock keeps the title bar it
    // paints itself and no child sits in.
    // A patch of the canvas, magnified, about a point in the canvas's own
    // coordinates. What a picture of the whole window cannot show is a rim: the
    // filter bug that "a turned drawing" is about was a two-pixel stair-step on
    // an edge, invisible at 1400 px across and unarguable at eight times.
    //
    // It is here rather than in the situation that first needed it because
    // getting it right took three builds -- the first crop missed the ink, the
    // second had the transform box's own outline lying across the thing being
    // judged, and the third did not follow the drawing when it was scaled. None
    // of that is about transforms, and the next situation that wants a close-up
    // should not pay for it again.
    QImage closeUpAt(QPointF at, QSize area, int magnify = 8) const {
        const QImage shot = canvas->grab().toImage();
        const QRect patch(static_cast<int>(at.x()) - area.width() / 2,
                          static_cast<int>(at.y()) - area.height() / 2, area.width(),
                          area.height());
        const QImage used = shot.copy(patch.intersected(shot.rect()));
        return used.scaled(used.width() * magnify, used.height() * magnify,
                           Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    static QImage closeUpOf(QWidget* widget, int magnify = 2) {
        if (!widget) return {};
        const QImage shot = widget->grab().toImage();
        const QImage used = shot.copy(contentsOf(widget, shot.rect()));
        return used.scaled(used.width() * magnify, used.height() * magnify,
                           Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    static QRect contentsOf(const QWidget* widget, QRect whole) {
        int right = 0;
        int bottom = 0;
        for (const QObject* child : widget->children()) {
            const auto* as_widget = qobject_cast<const QWidget*>(child);
            if (!as_widget || as_widget->isHidden()) continue;
            right = std::max(right, as_widget->geometry().right());
            bottom = std::max(bottom, as_widget->geometry().bottom());
        }
        if (right <= 0 || bottom <= 0) return whole;  // nothing laid out to measure
        constexpr int kBreathingRoom = 6;
        return whole.intersected(
            QRect(0, 0, right + kBreathingRoom + 1, bottom + kBreathingRoom + 1));
    }

    // The pointer, put somewhere on purpose and then read off the widget.
    // Hovering is half of what this checks -- the glyph is the other half, and
    // a right glyph raised in the wrong place is still wrong.
    CursorRow pointerAt(const char* about, QPointF at) {
        hover(at);
        return {QString::fromUtf8(about),
                QStringLiteral("%1  (%2)")
                    .arg(QString::fromUtf8(nameOf(canvas->pointing())),
                         QString::fromUtf8(nameOf(canvas->cursor().shape()))),
                canvas->cursor()};
    }

private:
    // Space and Z are held keys and not modifiers, so they have none: they
    // travel by the key events alone, which is how the canvas reads them.
    static Qt::KeyboardModifiers modifierFor(int key) {
        switch (key) {
            case Qt::Key_Alt: return Qt::AltModifier;
            case Qt::Key_Shift: return Qt::ShiftModifier;
            case Qt::Key_Control: return Qt::ControlModifier;
            default: return Qt::NoModifier;
        }
    }

    void sendMouse(QWidget* to, QEvent::Type type, QPointF at, Qt::MouseButton button,
                   Qt::MouseButtons buttons) {
        QMouseEvent event(type, at, to->mapToGlobal(at), button, buttons, held_);
        QCoreApplication::sendEvent(to, &event);
    }

    void sendKey(QEvent::Type type, int key) {
        QKeyEvent event(type, key, held_);
        QCoreApplication::sendEvent(canvas, &event);
        settle(1);
    }

    Qt::KeyboardModifiers held_ = Qt::NoModifier;
    // Where the pointer was last put, so a drag continues from it rather than
    // making every situation say twice where it started.
    QPointF pointer_;
};

// The cursors, side by side, each on paper and on ink -- because the rule they
// follow is light under dark, and a cursor crosses both by definition. A glyph
// that reads on white and vanishes on black has half a rule.
QImage sheetOf(const std::vector<CursorRow>& rows) {
    constexpr int kPatch = 32 * kCursorMagnify;
    constexpr int kText = 470;
    constexpr int kPad = 12;

    const int width = kText + 2 * kPatch + 3 * kPad;
    const int height = static_cast<int>(rows.size()) * (kPatch + kPad) + kPad;
    QImage sheet(width, height, QImage::Format_RGB32);
    sheet.fill(QColor(245, 245, 247));

    QPainter painter(&sheet);
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int top = kPad + static_cast<int>(i) * (kPatch + kPad);
        painter.setPen(QColor(30, 30, 34));
        painter.drawText(QRect(kPad, top, kText - kPad, kPatch / 2), Qt::AlignVCenter,
                         rows[i].about);
        painter.setPen(QColor(120, 120, 130));
        painter.drawText(QRect(kPad, top + kPatch / 2, kText - kPad, kPatch / 2),
                         Qt::AlignVCenter, rows[i].decided);

        const QPixmap glyph = rows[i].cursor.pixmap();
        if (glyph.isNull()) {
            // A system cursor: the platform draws it and there is nothing here
            // to show. Saying so is the point -- these are the ones a change to
            // the drawn cursors cannot break.
            painter.setPen(QColor(170, 170, 178));
            painter.drawText(QRect(kText, top, 2 * kPatch + kPad, kPatch), Qt::AlignCenter,
                             QStringLiteral("drawn by the system"));
            continue;
        }

        const QImage big = glyph.toImage().scaled(kPatch, kPatch, Qt::KeepAspectRatio,
                                                  Qt::FastTransformation);
        const int paper = kText;
        const int ink = kText + kPatch + kPad;
        painter.fillRect(QRect(paper, top, kPatch, kPatch), QColor(255, 255, 255));
        painter.fillRect(QRect(ink, top, kPatch, kPatch), QColor(22, 22, 26));
        painter.drawImage(paper, top, big);
        painter.drawImage(ink, top, big);
        painter.setPen(QColor(205, 205, 212));
        painter.drawRect(QRect(paper, top, kPatch, kPatch));
        painter.drawRect(QRect(ink, top, kPatch, kPatch));
    }
    painter.end();
    return sheet;
}

// -----------------------------------------------------------------------------
// The situations.
//
// Three lines each: the file name, one sentence saying what to look at, and
// what to do. Add yours here -- the list is read in order and nothing else
// refers to it.
// -----------------------------------------------------------------------------

struct Situation {
    const char* name;   // the file it writes, without the extension
    const char* about;  // what to look at, in one sentence
    std::function<void(Stage&)> set_up;
};

// The same seven degrees at three moments -- before, live, and committed -- so
// that what the preview shows and what the commit bakes can be put side by side.
//
// These three stay because the thing they watch regresses silently: the resample
// is one expression, nothing on screen announces which filter ran, and the
// version before this one damaged every rotation for months under a green suite.
// The tests pin the arithmetic; only a picture says whether a rim still looks
// like a rim. See "what a commit does to a line" in docs/handover.md.
//
// At 1:1, so nothing here is the display path's own reduction, and magnified,
// because the whole question is what happened to two pixels.
// Where a track's name is, which is the handle a row is restacked by. In the
// gutter, so it is an x the strip does not reach and a y the row's cells do.
QPointF trackName(const Stage& s, int row) {
    return QPointF(20.0,
                   s.timeline->cellCentreForTesting(static_cast<std::size_t>(row), 0).y());
}

// A shot with something actually drawn on every drawing.
//
// Worth its own helper because the first version of the playback situations
// inserted empty drawings and photographed a status bar reading "tiles 0" --
// an empty scene composites in no time at all, so the readout said it was
// keeping up and was quite right. What was wrong was the fixture, which is the
// same trap the handover records about drawGappedBox.
void drawnShot(Stage& s, int drawings) {
    for (int d = 0; d < drawings; ++d) {
        s.circle(s.centre() + QPointF(d * 6.0, 0.0), 180.0);
        s.press(Id::InsertDrawing);
    }
    s.circle(s.centre(), 180.0);
}

enum class Turned { Untouched, Live, Committed };

void turnedArc(Stage& s, Turned when) {
    s.canvas->setZoom(1.0, s.centre());
    s.circle(s.centre(), 150.0);
    if (when != Turned::Untouched) {
        s.press(Id::Transform);
        Transform turned = s.canvas->transformValues();
        turned.rotation = 7.0;
        s.canvas->setTransformValues(turned);
        s.settle();
    }
    if (when == Turned::Committed) {
        s.canvas->applyTransform();
        s.settle();
    }
    // Off the top-left of the arc: inside the box, and clear of every edge and
    // handle it draws. The overlay is the one thing that cannot be compared
    // against a picture that has none.
    s.picture = s.closeUpAt(s.centre() + QPointF(-75.0, -130.0), QSize(120, 30));
}

const std::vector<Situation>& situations() {
    static const std::vector<Situation> list = {
        {"the-window-as-it-opens",
         "menus, tools, layer panel, timeline and status bar, all in their places",
         [](Stage&) {}},

        {"a-transform-box-round-a-drawing",
         "the box should sit on the ink: #28 was raised on one drawn 128 px clear of "
         "the drawing on every side, because it was made from tile-aligned bounds",
         [](Stage& s) {
             s.circle(s.centre(), 160.0);
             s.press(Id::Transform);
         }},

        {"a-flipped-drawing",
         "issue #24: Flip X is down, the bar has two more buttons on it, and the preview is "
         "mirrored about the middle of the box -- an arc opening the other way, in the same "
         "place, at the same size",
         [](Stage& s) {
             s.canvas->setZoom(1.0, s.centre());
             // An arc rather than a circle, and off-centre: a mirror of
             // something symmetrical is a picture of nothing happening.
             std::vector<QPointF> arc;
             for (int i = 0; i <= 32; ++i) {
                 const double t = kTau * (0.10 + 0.42 * i / 32.0);
                 arc.push_back(s.centre() + QPointF(150.0 * std::cos(t), 150.0 * std::sin(t)));
             }
             s.stroke(arc);
             s.press(Id::Transform);
             s.choose("Flip X");
             s.picture = s.canvas->grab().toImage();
         }},

        {"a-transform-box-round-something-tiny",
         "the handles are a fixed screen size, so a box narrower than three of them has "
         "nowhere left to put them -- and the numeric bar is the only way to place it",
         [](Stage& s) {
             s.line(s.centre(), s.centre() + QPointF(9.0, 6.0), 3);
             s.press(Id::Transform);
         }},

        {"a-turned-drawing-before-anything",
         "the arc as drawn, magnified: what the other two are compared against",
         [](Stage& s) { turnedArc(s, Turned::Untouched); }},

        {"a-turned-drawing-while-it-is-live",
         "the float, blitted by Qt through a QTransform: the arc turned seven degrees, "
         "still live",
         [](Stage& s) { turnedArc(s, Turned::Live); }},

        {"a-turned-drawing-once-it-is-committed",
         "the same seven degrees after Apply, through transformTiles -- the rim should "
         "be the rim it was, and was stair-steps until the filter was one filter",
         [](Stage& s) { turnedArc(s, Turned::Committed); }},

        {"a-straight-line-being-aimed",
         "Shift, mid-gesture: the band runs from where the pen landed to where it is now "
         "and ignores the loop the hand made in between -- and no ink has been laid down "
         "yet, because a straight line writes nothing until the pen lifts",
         [](Stage& s) {
             // Something to line the mark up against, which is what the band is
             // for: a picture of it over bare paper says nothing about whether
             // it can be seen where it matters.
             s.circle(s.centre(), 170.0);
             s.hold(Qt::Key_Shift);
             s.pressOn(s.canvas, s.centre() + QPointF(-230.0, -150.0));
             // Well off the line, so the picture shows a path being discarded
             // rather than a hand that happened to be steady.
             s.dragTo(s.canvas, s.centre() + QPointF(-260.0, 190.0));
             // An oblique angle on purpose: the constraint is to a line and not
             // to an axis, and a horizontal one would be a picture of either.
             s.dragTo(s.canvas, s.centre() + QPointF(250.0, 120.0));
             s.picture = s.canvas->grab().toImage();
         }},

        {"a-lasso-round-part-of-a-drawing",
         "the loop, and what it says about what is caught: a selection here clips "
         "nothing, so the outline is the whole of what it looks like",
         [](Stage& s) {
             s.circle(s.centre(), 170.0);
             s.press(Id::Lasso);
             s.circle(s.centre() + QPointF(120.0, 0.0), 110.0);
         }},

        {"a-transform-of-what-the-lasso-caught",
         "the box should be round what the loop caught and not round the whole drawing",
         [](Stage& s) {
             s.circle(s.centre(), 170.0);
             s.press(Id::Lasso);
             s.circle(s.centre() + QPointF(120.0, 0.0), 110.0);
             s.press(Id::Transform);
         }},

        {"a-locked-layer-refuses",
         "the status bar is the whole of the feedback -- a tool that silently does "
         "nothing is a bug to anybody holding the pen",
         [](Stage& s) {
             s.circle(s.centre(), 140.0);
             s.lockActiveLayer();
             s.press(Id::Transform);
         }},

        {"the-colour-panel",
         "the layer panel with a colour layer selected, and the Colour layer box that "
         "appears only there: what the fill is cut against, and what it does with time",
         [](Stage& s) {
             s.circle(s.centre(), 150.0);
             s.choose("Add colour layer");
             s.picture = Stage::closeUpOf(s.layerPanel());
         }},

        {"the-colour-switch-on-the-toolbar",
         "the two swatches have to be told apart -- which is the colour and which is "
         "None -- and they shipped once as two reds nothing distinguished",
         [](Stage& s) {
             s.choose("Add colour layer");  // None means nothing off a colour layer
             s.picture = Stage::closeUpOf(s.window.findChild<QToolBar*>(), 3);
         }},

        {"a-coloured-drawing",
         "one scribble inside a shape, solved: the mark is over the fill and invisible "
         "wherever the two agreed. Waits for a max-flow, so this one is slow",
         [](Stage& s) {
             s.circle(s.centre(), 170.0);
             s.choose("Add colour layer");
             s.canvas->setBrushColour(0.85f, 0.32f, 0.12f);  // the swatch would need a dialog
             s.line(s.centre() - QPointF(30.0, 0.0), s.centre() + QPointF(30.0, 14.0), 6);
             s.settleColour();
             s.picture = s.canvas->grab().toImage();
         }},

        {"the-timeline-with-three-tracks",
         "one row per track under one ruler and one playhead; the blue rim marking the "
         "frame being edited belongs to the current track's row and to no other",
         [](Stage& s) {
             for (int t = 0; t < 3; ++t) {
                 for (int d = 0; d <= t; ++d) {
                     s.press(Id::HoldLonger);
                     s.press(Id::HoldLonger);
                     s.press(Id::InsertDrawing);
                 }
                 if (t < 2) s.choose("Add track");
             }
             s.press(Id::PreviousFrame);
             s.picture = Stage::closeUpOf(s.timelinePanel());
         }},

        {"a-track-being-restacked",
         "left mid-drag on purpose: the caret is where the row would land, and it goes "
         "across the whole width because the whole row moves and not just its name",
         [](Stage& s) {
             s.choose("Add track");
             s.choose("Add track");
             // The bottom row picked up by its name and carried to the top, and
             // never let go of -- a caret only exists while a row is in hand.
             s.pressOn(s.timeline, trackName(s, 2));
             s.dragTo(s.timeline, trackName(s, 0) - QPointF(0.0, 8.0));
             s.picture = Stage::closeUpOf(s.timelinePanel());
         }},

        {"a-drawing-being-dropped-on-a-hold",
         "the caret is a range and not a line: on a track that overwrites -- which is "
         "the default -- a drop takes over the rest of the hold it lands in rather than "
         "being inserted between two frames",
         [](Stage& s) {
             for (int i = 0; i < 6; ++i) s.press(Id::HoldLonger);
             s.press(Id::InsertDrawing);  // drawing 2 spends the rest of the hold
             s.pressOn(s.timeline, QPointF(s.timeline->cellCentreForTesting(0, 0)));
             s.dragTo(s.timeline, QPointF(s.timeline->cellCentreForTesting(0, 3)));
             s.picture = Stage::closeUpOf(s.timelinePanel());
         }},

        {"the-playback-rate-while-it-keeps-up",
         "the right-hand end of the status bar while playing: nominal, because a shot "
         "this size in this window has time to spare -- and pinned there as a permanent "
         "widget so the main text changing length cannot shuffle it sideways",
         [](Stage& s) {
             drawnShot(s, 8);
             s.press(Id::Play);
             s.spin(1500);
             s.picture = Stage::closeUpOf(s.window.statusBar());
         }},

        {"the-playback-rate-while-it-drops",
         "the same shot at 240 fps, which no animation is -- it is the only lever this "
         "window has, since dropping at 24 needs a 4K viewport and shots is 1400 px. "
         "The state is real: the frame overruns the budget and the readout has to say "
         "so, rather than letting a dropping playback pass for a badly timed shot",
         [](Stage& s) {
             drawnShot(s, 8);
             s.doc().setFramerate(240);
             s.press(Id::Play);
             s.spin(1500);
             s.picture = Stage::closeUpOf(s.window.statusBar());
         }},

        {"a-colour-layer-at-the-bottom-of-a-long-stack",
         "issue #12: the colour layer is the bottom row and has to be in view the moment "
         "it is made -- the scroll is against the panel *with* the Colour layer box in it",
         [](Stage& s) {
             for (int i = 0; i < 30; ++i) s.choose("Add layer");
             s.choose("Add colour layer");
             s.picture = Stage::closeUpOf(s.layerPanel());
         }},

        {"the-keyboard-shortcuts-panel",
         "issue #14: the groups are ordered by what somebody who has just opened this is "
         "likely hunting for rather than by menu order, and the search box is what keeps "
         "the list from being something anybody has to read all of",
         [](Stage& s) {
             auto* dialog = new ShortcutsDialog(shortcuts::current(), &s.window);
             dialog->show();
             s.settle();
             s.picture = dialog->grab().toImage();
         }},

        {"searching-the-shortcuts-panel",
         "the search has to reach into the folded groups, or half the list is unsearchable "
         "and nothing says so -- this one lands in the held keys, which open folded",
         [](Stage& s) {
             auto* dialog = new ShortcutsDialog(shortcuts::current(), &s.window);
             dialog->show();
             if (auto* search = dialog->findChild<QLineEdit*>()) {
                 search->setText(QStringLiteral("colour"));
             }
             s.settle();
             s.picture = dialog->grab().toImage();
         }},

        {"a-shortcut-that-collides",
         "Fit drawing put back on Shift+0, which is issue #14 itself -- two different "
         "sequences and one chord on AZERTY. The clash should be named in words, both rows "
         "marked, and Apply refused until one of them moves",
         [](Stage& s) {
             shortcuts::Bindings clashing;
             clashing.set(Id::FitDrawing,
                          QKeySequence(QStringLiteral("Shift+0"), QKeySequence::PortableText));
             auto* dialog = new ShortcutsDialog(clashing, &s.window);
             dialog->show();
             s.settle();
             s.picture = dialog->grab().toImage();
         }},

        {"the-drawn-cursors",
         "the three glyphs the system has none for, read off the widget after hovering "
         "what raises them -- grab() renders the widget and never the pointer",
         [](Stage& s) {
             std::vector<CursorRow> rows;
             s.circle(s.centre(), 150.0);
             rows.push_back(s.pointerAt("the brush, over the drawing", s.centre()));
             s.press(Id::Eraser);
             rows.push_back(s.pointerAt("the eraser, which nothing else announces",
                                        s.centre()));
             s.press(Id::Brush);
             s.hold(Qt::Key_Alt);
             rows.push_back(s.pointerAt("Alt: the eyedropper", s.centre()));
             s.letGo(Qt::Key_Alt);
             s.hold(Qt::Key_Space);
             rows.push_back(s.pointerAt("Space: a pan is ready", s.centre()));
             s.letGo(Qt::Key_Space);
             s.press(Id::Transform);
             rows.push_back(s.pointerAt("the rotation knob above the box",
                                        s.canvas->rotationHandleForTesting()));
             rows.push_back(s.pointerAt("inside the box",
                                        s.canvas->transformCentreForTesting()));
             rows.push_back(s.pointerAt("the top-left handle",
                                        s.canvas->transformHandlesForTesting()[0]));
             s.picture = sheetOf(rows);
         }},
    };
    return list;
}

// -----------------------------------------------------------------------------

void listThem() {
    std::printf("\n%d situations:\n\n", static_cast<int>(situations().size()));
    for (const Situation& situation : situations()) {
        std::printf("  %-40s %s\n", situation.name, situation.about);
    }
    std::printf(
        "\nA name, or any part of one, runs only the ones it matches.\n"
        "Adding one is three lines at the bottom of tests/shots.cpp, and it is\n"
        "meant to be edited -- see the comment at the top of that file.\n");
}

}  // namespace

int main(int argc, char** argv) {
    // Offscreen unless something else has been asked for. Nothing here wants a
    // window on screen, a build machine has no display to give one, and a flag
    // you have to remember is a flag that gets forgotten once and then read as
    // "the tool is broken".
    bool platform_chosen = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("-platform")) platform_chosen = true;
    }
    if (!platform_chosen) qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    // Beside the build rather than beside wherever this was run from, so the
    // pictures land somewhere .gitignore already covers and the same command
    // means the same thing from any directory.
    QString where = QDir::cleanPath(QCoreApplication::applicationDirPath() +
                                    QStringLiteral("/../shots"));
    QString only;
    bool list_only = false;

    // QApplication removes its own arguments, but not on every platform and not
    // in every build, so the two that take a value are stepped over by name.
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--list") || arg == QStringLiteral("-l")) {
            list_only = true;
        } else if ((arg == QStringLiteral("-o") || arg == QStringLiteral("--out")) &&
                   i + 1 < argc) {
            where = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == QStringLiteral("-platform") || arg == QStringLiteral("-style")) {
            ++i;
        } else if (arg.startsWith(QLatin1Char('-'))) {
            continue;
        } else {
            only = arg;
        }
    }

    if (list_only) {
        listThem();
        return 0;
    }

    if (!QDir().mkpath(where)) {
        std::printf("cannot make %s\n", qPrintable(QDir::toNativeSeparators(where)));
        return 1;
    }

    std::printf("\nWriting to %s\n\n", qPrintable(QDir::toNativeSeparators(where)));

    int written = 0;
    for (const Situation& situation : situations()) {
        if (!only.isEmpty() &&
            !QString::fromUtf8(situation.name).contains(only, Qt::CaseInsensitive)) {
            continue;
        }

        std::printf("  %s\n      %s\n", situation.name, situation.about);

        // A window of its own each time. Situations are read one at a time and
        // must not be able to leave state in each other -- a tool still
        // selected, a track still added -- which is exactly the class of bug
        // this file is for finding and not for having.
        Stage stage;
        if (!stage.canvas) {
            std::printf("      no canvas in the window; the tool cannot run\n");
            return 1;
        }
        situation.set_up(stage);
        stage.settle();

        const QImage picture =
            stage.picture.isNull() ? stage.window.grab().toImage() : stage.picture;
        const QString file =
            QDir(where).filePath(QString::fromUtf8(situation.name) + QStringLiteral(".png"));
        if (picture.save(file)) {
            std::printf("      %s  (%dx%d)\n\n", qPrintable(QDir::toNativeSeparators(file)),
                        picture.width(), picture.height());
            ++written;
        } else {
            std::printf("      could not write %s\n\n",
                        qPrintable(QDir::toNativeSeparators(file)));
        }
    }

    if (written == 0) {
        std::printf("Nothing matched \"%s\".\n", qPrintable(only));
        listThem();
        return 1;
    }

    std::printf(
        "%d written. Nothing here asserts: what is wrong is what looks wrong.\n"
        "Run it before and after anything that touches the canvas.\n",
        written);
    return 0;
}
