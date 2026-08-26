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
//   ./build/tests/shots -platform windows   the style the program really runs
//
// It runs offscreen unless something else has been asked for, so it needs no
// display and no `-platform` flag. Every file it writes is printed with its
// path, because the next thing anyone does is open one.
//
// **These are pictures of the Qt on your machine, and that is the one trap in
// here.** Not of the Qt anybody downloads. The first line of every run says
// which Qt, which platform and which style drew the pictures below it; when a
// shot and a report disagree, read that line before anything else.
//
// Two gaps hide in there, and only one of them has a flag:
//
//   - **Offscreen is a different style.** The offscreen platform has no native
//     style to fall back on, so Qt gives it *fusion*, while the program on
//     somebody's Windows desk runs *windows11*, and the two draw different
//     controls. `-platform windows` closes this one.
//   - **The Qt version is a different Qt**, and no flag closes it. This links
//     against whatever MSYS2 has; `ci.yml` decides what a download gets, per
//     platform, and it is not obliged to agree. A control's appearance is Qt's
//     to change and it does change between versions.
//
// **Issue #75 is what the second gap costs.** A slider handle cut flat at both
// ends of its travel, reported with a screenshot. Qt cut it in the windows11
// style until 6.10.1 -- QTBUG-140649 -- and the reporter was running a build
// made against 6.8.3, while `shots` here was linked against 6.11 and drew a
// handle that had never had the fault. So it reported a whole, well-clear handle
// under `-platform windows`, correctly, about a Qt no user was running. A
// standalone Qt reproducer, built the same way, agreed with it.
//
// That disagreement was then explained as a defect in `QWidget::grab()` -- that
// a grab renders some controls differently from the screen -- and written up
// here as one. **It was not.** The instrument was honest; it was pointed at
// another Qt. Nothing has ever shown `grab()` drawing a control the screen does
// not. See "the same source, two different pictures" in docs/handover.md, which
// is the general form of this and predates #75 by months.
//
// So: layout, presence, ordering, scrolling, what is in view -- ask those here.
// **What does this look like to somebody who downloaded it** -- that question
// has a Qt version in it, and this cannot answer it. Open the download.
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
#include <QColor>
#include <QCursor>
#include <QGuiApplication>
#include <QPalette>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QFontMetrics>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QTimer>
#include <QImage>
#include <QKeyEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QLineEdit>
#include <QPushButton>
#include <QRect>
#include <QString>
#include <QToolBar>
#include <QAbstractButton>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "canvas_widget.h"
#include "document.h"
#include "floating_dock_frame.h"
#include "layer_list.h"
#include "scene_settings_dialog.h"
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

    // The top strip of a dock -- its title bar, whoever drew it. Taken by height
    // rather than by asking for the title bar widget, because a docked panel has
    // no such widget to ask: Qt paints its own.
    static QImage stripOf(QWidget* dock, int magnify = 2) {
        if (!dock) return {};
        const QImage shot = dock->grab().toImage();
        const int tall = std::min(shot.height(), static_cast<int>(30 * shot.devicePixelRatio()));
        const QImage strip = shot.copy(QRect(0, 0, shot.width(), tall));
        return strip.scaled(strip.width() * magnify, strip.height() * magnify,
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
// Several pictures down the page, each with a name over it, so two states of the
// same thing can be compared without flipping between files.
QImage stackOf(const std::vector<std::pair<QString, QImage>>& rows) {
    constexpr int kPad = 10;
    constexpr int kLabel = 22;

    int width = 0;
    int height = kPad;
    // The label has to fit too, or the measurement written beside a picture is
    // clipped off and the picture alone cannot answer the question.
    const QFontMetrics metrics((QFont()));
    for (const auto& [name, picture] : rows) {
        width = std::max({width, picture.width(), metrics.horizontalAdvance(name)});
        height += kLabel + picture.height() + kPad;
    }
    width += 2 * kPad;

    QImage sheet(width, height, QImage::Format_ARGB32);
    sheet.fill(QColor(250, 250, 250));
    QPainter painter(&sheet);
    painter.setPen(QColor(40, 40, 40));

    int y = kPad;
    for (const auto& [name, picture] : rows) {
        painter.drawText(QRect(kPad, y, width - 2 * kPad, kLabel), Qt::AlignVCenter, name);
        y += kLabel;
        painter.drawImage(kPad, y, picture);
        // An outline, or a pale title bar on a pale sheet has no edges to judge.
        painter.drawRect(QRect(kPad, y, picture.width() - 1, picture.height() - 1));
        y += picture.height() + kPad;
    }
    return sheet;
}

// The bounding box of everything that is not the background, which is the only
// honest measure of "how big does this glyph look". Every metric can agree while
// the drawn cross does not, which is exactly what happened in #50.
QRect inkOf(const QImage& picture) {
    if (picture.isNull()) return {};
    const QImage rgb = picture.convertToFormat(QImage::Format_ARGB32);
    const QRgb background = rgb.pixel(0, 0);
    int left = rgb.width();
    int top = rgb.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < rgb.height(); ++y) {
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb here = rgb.pixel(x, y);
            const int difference = std::abs(qRed(here) - qRed(background)) +
                                   std::abs(qGreen(here) - qGreen(background)) +
                                   std::abs(qBlue(here) - qBlue(background));
            if (difference < 40) continue;  // near enough to the background
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    if (right < 0) return {};
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

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

// What the timeline reads out of the palette, printed, alpha first.
//
// The timeline takes every colour it draws from the widget's palette and then
// bends it -- lighter for a dark theme, darker for a light one. That derivation
// is the one thing in the program whose result depends on the Qt underneath
// rather than on this source, so a picture of it is not enough: two builds can
// differ and the picture only ever says which one looks wrong, never why. This
// prints what it starts from.
//
// **Alpha first because alpha is the trap.** QColor::lighter and darker carry it
// through untouched, so a role arriving with any transparency in it took the
// whole structure of the row out -- background, ruler, gutter, every outline --
// while the numbers and the playhead went on drawing, which is a picture that
// looks like a missing outline rather than like a palette. TimelineWidget makes
// them opaque before it bends them now; this is the reading that says whether it
// still has to. WindowText arrives here at alpha 228 and is left alone, so the
// palette this reads is a live example rather than a worry.
QColor opaqueCopy(QColor c) {
    c.setAlpha(255);
    return c;
}

QString hexOf(const QColor& c) {
    return QStringLiteral("#%1%2%3%4")
        .arg(c.alpha(), 2, 16, QLatin1Char('0'))
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'));
}

void printTimelinePalette(const QWidget& widget) {
    const QPalette& source = widget.palette();
    const QColor window = source.color(QPalette::Window);
    const QColor base = source.color(QPalette::Base);
    const bool dark = opaqueCopy(base).lightness() < 128;

    std::printf("      Qt %s, style %s, platform %s\n", qVersion(),
                qPrintable(QApplication::style()->objectName()),
                qPrintable(QGuiApplication::platformName()));
    std::printf("      Window %s  WindowText %s  Base %s  Highlight %s\n",
                qPrintable(hexOf(window)), qPrintable(hexOf(source.color(QPalette::WindowText))),
                qPrintable(hexOf(base)), qPrintable(hexOf(source.color(QPalette::Highlight))));
    // Window is printed and then not used. It is the role this used to build on,
    // and it is the one the shipped build hands over as #00000000 -- so it is
    // worth seeing, precisely because nothing depends on it any more.
    std::printf("      lightness(Base) = %d, so dark = %s\n", opaqueCopy(base).lightness(),
                dark ? "true" : "false");

    // What paletteFor makes of them. A copy of five lines from another file,
    // which is a thing that goes stale -- it has already done so twice, once in
    // the hour between finding a bug and fixing it -- so what it is for is worth
    // being exact about: it is not a second implementation to trust, it is the
    // arithmetic written out where the numbers can be read. The picture beside it
    // is what says whether the widget agrees.
    const QColor solid = opaqueCopy(base);
    const auto stepped = [&](int amount) {
        const int target = dark ? 255 : 0;
        const auto mix = [&](int channel) { return channel + (target - channel) * amount / 100; };
        return QColor(mix(solid.red()), mix(solid.green()), mix(solid.blue()));
    };
    std::printf("      background %s  ruler %s  outline %s  cell %s  cell_held %s\n",
                qPrintable(hexOf(stepped(12))), qPrintable(hexOf(stepped(19))),
                qPrintable(hexOf(stepped(32))), qPrintable(hexOf(solid)),
                qPrintable(hexOf(stepped(6))));
}

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

        {"a-transform-too-large-to-bake",
         "issue #40: the box, its handles and its knob go red when the scale has gone past "
         "what a commit may hold, and the status bar says so -- the preview itself cannot, "
         "because a float is blitted through a bounded picture and looks exactly as good at "
         "a scale that would take a hundred gigabytes to rasterise",
         [](Stage& s) {
             // Zoomed out, because at 1:1 a box this size has no corner on
             // screen and the picture would be a red line across the canvas.
             s.circle(s.centre(), 120.0);
             s.press(Id::Transform);
             Transform huge = s.canvas->transformValues();
             // Wider than tall on purpose: an over-budget box is at least
             // 16384 pixels across whichever way it is shaped, and a square one
             // that big has no top or bottom edge left on screen at the
             // furthest this can zoom out.
             huge.scale_x = 45.0;
             huge.scale_y = 29.0;
             s.canvas->setTransformValues(huge);
             s.canvas->setZoom(0.05, s.centre());
             s.settle();
         }},

        {"a-layer-moved-through-time",
         "issue #25: the box is green rather than blue, and it is round the union of every "
         "drawing on the layer rather than the one in front of you -- the other drawings "
         "are under the float at low opacity, in their own colours, and they move with it",
         [](Stage& s) {
             s.canvas->setZoom(1.0, s.centre());
             // Five drawings, each further right, so the union is visibly wider
             // than any one of them and the ghosts are told apart from the
             // drawing in front of you by where they are rather than by a tint.
             for (int d = 0; d < 5; ++d) {
                 s.circle(s.centre() + QPointF(d * 40.0 - 80.0, 0.0), 70.0);
                 if (d < 4) s.press(Id::InsertDrawing);
             }
             s.choose("Transform layer through time");
             // Moved and turned, because a box that has not moved is a box that
             // proves nothing about what moves with it.
             Transform placed = s.canvas->transformValues();
             placed.dy = -60.0;
             placed.rotation = 10.0;
             s.canvas->setTransformValues(placed);
             s.settle();
         }},

        {"a-layer-moved-through-time-with-onion-skin",
         "the onion skin leaves out the layer being moved: the neighbouring drawings of it "
         "are already on screen, moving, under the float -- without that they would be here "
         "twice over, once still and warm and once moving, and only in the same place until "
         "the first drag",
         [](Stage& s) {
             s.canvas->setZoom(1.0, s.centre());
             for (int d = 0; d < 3; ++d) {
                 s.circle(s.centre() + QPointF(d * 50.0 - 50.0, 0.0), 70.0);
                 if (d < 2) s.press(Id::InsertDrawing);
             }
             // Through the control and not past it, so the readout in the
             // picture agrees with what the canvas is doing.
             if (auto* onion = s.window.findChild<QSpinBox*>(QStringLiteral("onionCount"))) {
                 onion->setValue(2);
             }
             s.settle();
             s.choose("Transform layer through time");
             Transform placed = s.canvas->transformValues();
             placed.dy = -70.0;
             s.canvas->setTransformValues(placed);
             s.settle();
         }},

        {"a-colour-layer-cannot-move-through-time",
         "the layer panel's button is greyed out on a colour layer rather than absent, and "
         "its tooltip is what says why -- a mark on one is a label, and interpolating two "
         "labels invents a third colour that competes for regions on its own account",
         [](Stage& s) {
             s.circle(s.centre(), 120.0);
             s.choose("Add colour layer");
             s.settle();
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

        {"a-coloured-drawing-off-the-frame",
         "a shape crossing the frame line, coloured: the colour carries on past the edge "
         "under the veil, because the drawing does. It used to be cut dead at the frame -- "
         "see docs/colour-without-a-canvas.md. What an export writes is still the frame and "
         "nothing outside it",
         [](Stage& s) {
             s.circle(QPointF(s.canvas->width() - 60.0, s.centre().y()), 120.0);
             s.choose("Add colour layer");
             s.canvas->setBrushColour(0.85f, 0.32f, 0.12f);
             const QPointF middle(s.canvas->width() - 60.0, s.centre().y());
             s.line(middle - QPointF(30.0, 0.0), middle + QPointF(30.0, 14.0), 6);
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

        {"the-timeline-under-a-shot-too-long-to-fit",
         "issue #26: a shot wider than the window, so the pan slider is there. The slider "
         "belongs under the track row and not over it -- the bottom edge of the cells must be "
         "visible above it, and there should be no vertical scrollbar beside it either",
         [](Stage& s) {
             // Sixty, which is comfortably past the fifty or so cells a 1400 px
             // window has room for. The strip only asks for a pan slider once it
             // is wider than the viewport, so a short shot photographs nothing.
             for (int d = 0; d < 60; ++d) s.press(Id::InsertDrawing);
             s.picture = Stage::closeUpOf(s.timelinePanel());
         }},

        {"the-timeline-palette",
         "the cells against the background, with the numbers behind the picture printed. "
         "Every colour here is bent out of a palette role, which makes this the one part "
         "of the interface whose result depends on the Qt it was built against",
         [](Stage& s) {
             for (int i = 0; i < 3; ++i) s.press(Id::HoldLonger);
             s.press(Id::InsertDrawing);
             printTimelinePalette(*s.timeline);
             s.picture = Stage::closeUpOf(s.timelinePanel());
         }},

        {"a-timeline-whose-window-colour-has-alpha",
         "the same row with QPalette::Window given an alpha of 0 and nothing else touched, "
         "and it must look exactly like the one above. This is the downloaded-build bug "
         "held still: lighter and darker carry alpha, so the whole row went white on white "
         "while the numbers and the playhead kept drawing. Nothing else can show it -- the "
         "suite is green either way and the local Qt hands over an opaque Window",
         [](Stage& s) {
             QPalette bent = s.timeline->palette();
             QColor window = bent.color(QPalette::Window);
             window.setAlpha(0);
             bent.setColor(QPalette::Window, window);
             s.timeline->setPalette(bent);
             for (int i = 0; i < 3; ++i) s.press(Id::HoldLonger);
             s.press(Id::InsertDrawing);
             printTimelinePalette(*s.timeline);
             s.picture = Stage::closeUpOf(s.timelinePanel());
         }},

        {"a-timeline-whose-window-colour-is-transparent-black",
         "Window set to #00000000, which is not a hypothetical: it is what Qt 6.8 on Windows "
         "11 hands over, and it broke this row twice. Read as transparent it drew white on "
         "white; forced opaque it drew a black slab, because black is what was under the "
         "transparency. Must look like the two above. Which Qt a download is built against is "
         "in ci.yml and moves -- this holds the palette still so it does not matter",
         [](Stage& s) {
             QPalette bent = s.timeline->palette();
             bent.setColor(QPalette::Window, QColor(0, 0, 0, 0));
             s.timeline->setPalette(bent);
             for (int i = 0; i < 3; ++i) s.press(Id::HoldLonger);
             s.press(Id::InsertDrawing);
             printTimelinePalette(*s.timeline);
             s.picture = Stage::closeUpOf(s.timelinePanel());
         }},

        {"a-timeline-on-a-pure-black-theme",
         "issue #32: Windows' High Contrast Black is a real theme and an accessibility one, "
         "so the people who meet it can least afford a row they cannot read. The cells stay "
         "black -- a high-contrast theme is entitled to be obeyed -- and the background, "
         "ruler and outlines must still separate from them",
         [](Stage& s) {
             QPalette bent = s.timeline->palette();
             bent.setColor(QPalette::Window, QColor(0, 0, 0));
             bent.setColor(QPalette::Base, QColor(0, 0, 0));
             bent.setColor(QPalette::WindowText, QColor(255, 255, 255));
             s.timeline->setPalette(bent);
             for (int i = 0; i < 3; ++i) s.press(Id::HoldLonger);
             s.press(Id::InsertDrawing);
             printTimelinePalette(*s.timeline);
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

        {"a-track-being-renamed",
         "the editor sits exactly over the name it is replacing, in the gutter, with the "
         "old name selected so typing replaces it",
         [](Stage& s) {
             s.choose("Add track");
             s.timeline->renameTrackForTesting(1);
             s.settle();
             s.picture = Stage::closeUpOf(s.timelinePanel());
         }},

        {"a-layer-being-renamed",
         "and the same on a layer row -- the field is the row and is frameless, so the "
         "selected name is the only thing that says it is one; what the editor opens on "
         "is the layer's own name, which on a colour layer carrying its marks is not "
         "what the row says",
         [](Stage& s) {
             static_cast<LayerList*>(s.layer_list)->renameRowForTesting(0);
             s.settle();
             s.picture = Stage::closeUpOf(s.layerPanel());
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
         "the glyphs this program draws rather than the system, read off the widget after "
         "hovering what raises them -- grab() renders the widget and never the pointer",
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

        // Reported from use: float the layer panel, then hide the panels, and
        // the timeline comes back with a white rectangle over it. Three shots
        // rather than one because the report does not say which step shows it,
        // and a picture of the wrong step says nothing.
        {"panels-the-floating-panel",
         "issue #50: the layer panel torn off, as an artist sees it on a second screen. Its "
         "title bar is drawn by us because a native one cannot be pressed with a pen -- so it "
         "has to look like it belongs: one colour from the name down to the buttons, and no "
         "grey bar meeting the window's rounded corners",
         [](Stage& s) {
             auto* dock = qobject_cast<QDockWidget*>(s.layerPanel());
             if (!dock) return;
             dock->setFloating(true);
             s.settle();
             if (auto* frame = dock->findChild<FloatingDockFrame*>()) frame->applyIfNothingIsHeld();
             s.settle();
             dock->resize(280, 380);
             s.settle();
             s.picture = Stage::closeUpOf(dock);
         }},

        {"the-opacity-slider-at-both-ends",
         "the layer opacity slider at 0, in the middle and at 100 -- each end of the travel, "
         "magnified, with the panel's own background round it. Issue #75 was a handle cut flat "
         "at both ends here, and **this can only see that on a Qt that has it**: Qt cut it in "
         "the windows11 style until 6.10.1, and this links against whatever MSYS2 has. On a "
         "newer Qt the handle is whole in all three and that is Qt being fixed, not the fault "
         "being absent. Read the trap at the top of this file",
         [](Stage& s) {
             auto* dock = qobject_cast<QDockWidget*>(s.layerPanel());
             if (!dock) return;
             auto* slider = dock->findChild<QSlider*>();
             QWidget* panel = dock->widget();
             if (!slider || !panel) return;

             std::vector<std::pair<QString, QImage>> rows;
             for (int value : {50, 0, 100}) {
                 slider->setValue(value);
                 s.settle();

                 QStyleOptionSlider option;
                 option.initFrom(slider);
                 option.minimum = slider->minimum();
                 option.maximum = slider->maximum();
                 option.sliderPosition = slider->sliderPosition();
                 option.sliderValue = slider->value();
                 option.orientation = slider->orientation();
                 const QRect handle = slider->style()->subControlRect(
                     QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider);

                 // Photographed from the *panel*. Grabbing the slider alone
                 // renders it into a pixmap of its own size, which crops at
                 // exactly the edge in question and manufactures the very thing
                 // being looked for.
                 const QImage shot = panel->grab().toImage();
                 const qreal dpr = shot.devicePixelRatio();
                 const QRect around = QRect(slider->mapTo(panel, QPoint(0, 0)), slider->size())
                                          .adjusted(-10, -6, 10, 6);
                 // The end the handle is at, not the whole strip: a sliced
                 // circle and a whole one are two pixels apart.
                 const QRect where =
                     value == 100
                         ? around.adjusted(around.width() - 64, 0, 0, 0)
                         : QRect(around.topLeft(), QSize(64, around.height()));
                 const QRect device = QRectF(where.x() * dpr, where.y() * dpr,
                                             where.width() * dpr, where.height() * dpr)
                                          .toRect()
                                          .intersected(shot.rect());
                 const QImage near = shot.copy(device);

                 const QString what =
                     QStringLiteral("%1%% -- handle %2..%3, %4 long, in a slider %5x%6; style %7")
                         .arg(value)
                         .arg(handle.left())
                         .arg(handle.right())
                         .arg(handle.width())
                         .arg(slider->width())
                         .arg(slider->height())
                         .arg(QApplication::style()->objectName());
                 rows.push_back({what, near.scaled(near.width() * 4, near.height() * 4,
                                                   Qt::IgnoreAspectRatio,
                                                   Qt::FastTransformation)});
             }
             s.picture = stackOf(rows);
         }},

        {"the-scene-settings-resolution-slider",
         "the Resolution slider in Scene settings, at each end of its travel -- and the only "
         "picture there is of this dialog, which is half of why it is kept. The same handle "
         "drawn by the same style as the opacity one above, and the same warning: what it shows "
         "depends on which Qt this was built against",
         [](Stage& s) {
             // Built and shown rather than reached through the menu, which opens
             // it modally and would stop the tool here.
             auto* dialog = new SceneSettingsDialog(24, 1920, 1080, false, 0, 12, &s.window);
             dialog->show();
             s.settle();
             auto* slider = dialog->findChild<QSlider*>();
             if (!slider) return;

             std::vector<std::pair<QString, QImage>> rows;
             for (int value : {slider->minimum(), slider->maximum()}) {
                 slider->setValue(value);
                 s.settle();
                 const QImage shot = dialog->grab().toImage();
                 const qreal dpr = shot.devicePixelRatio();
                 const QRect around = QRect(slider->mapTo(dialog, QPoint(0, 0)), slider->size())
                                          .adjusted(-10, -6, 10, 6);
                 const QRect where = value == slider->minimum()
                                         ? QRect(around.topLeft(), QSize(64, around.height()))
                                         : around.adjusted(around.width() - 64, 0, 0, 0);
                 const QRect device = QRectF(where.x() * dpr, where.y() * dpr,
                                             where.width() * dpr, where.height() * dpr)
                                          .toRect()
                                          .intersected(shot.rect());
                 const QImage near = shot.copy(device);
                 rows.push_back({QStringLiteral("%1 px tall -- slider %2..%3, %4 wide; style %5")
                                     .arg(value)
                                     .arg(slider->minimum())
                                     .arg(slider->maximum())
                                     .arg(slider->width())
                                     .arg(QApplication::style()->objectName()),
                                 near.scaled(near.width() * 4, near.height() * 4,
                                             Qt::IgnoreAspectRatio, Qt::FastTransformation)});
             }
             // And the whole dialog under them, which nothing else photographs.
             rows.push_back({QStringLiteral("the dialog it is in"), dialog->grab().toImage()});
             s.picture = stackOf(rows);
         }},

        {"panels-the-close-button-itself",
         "issue #50: Qt's close button on a docked panel, and ours on a floating one, both "
         "magnified. The number beside each is the bounding box of its *ink* -- every metric "
         "agreed while the drawn cross did not, so this measures what is actually painted",
         [](Stage& s) {
             auto* dock = qobject_cast<QDockWidget*>(s.layerPanel());
             if (!dock) return;

             QImage qts;
             if (auto* b = dock->findChild<QAbstractButton*>(QStringLiteral("qt_dockwidget_closebutton"))) {
                 qts = b->grab().toImage();
             }

             dock->setFloating(true);
             s.settle();
             if (auto* frame = dock->findChild<FloatingDockFrame*>()) frame->applyIfNothingIsHeld();
             s.settle();
             dock->resize(280, 360);
             s.settle();

             QImage ours;
             if (QWidget* bar = dock->titleBarWidget()) {
                 if (auto* b = bar->findChild<QAbstractButton*>()) ours = b->grab().toImage();
             }

             const auto magnified = [](const QImage& one) {
                 return one.isNull() ? one
                                     : one.scaled(one.width() * 6, one.height() * 6,
                                                  Qt::IgnoreAspectRatio, Qt::FastTransformation);
             };
             const auto describe = [](const char* who, const QImage& one) {
                 const QRect ink = inkOf(one);
                 return QStringLiteral("%1 -- button %2x%3, ink %4x%5 at %6,%7")
                     .arg(QString::fromUtf8(who))
                     .arg(one.width())
                     .arg(one.height())
                     .arg(ink.width())
                     .arg(ink.height())
                     .arg(ink.x())
                     .arg(ink.y());
             };
             // And the icon on its own, drawn straight at the size the button is
             // told to use. If its ink matches Qt's, the icon is right and the
             // button is scaling it; if it matches ours, we have the wrong icon.
             QImage bare;
             QString bare_what = QStringLiteral("our icon at 16 -- none found");
             if (QWidget* bar = dock->titleBarWidget()) {
                 if (auto* b = bar->findChild<QAbstractButton*>()) {
                     bare = b->icon().pixmap(b->iconSize()).toImage();
                     QImage onto(b->iconSize(), QImage::Format_ARGB32);
                     onto.fill(Qt::white);
                     QPainter into(&onto);
                     into.drawImage(0, 0, bare);
                     into.end();
                     bare = onto;
                     bare_what = QStringLiteral("our icon alone at %1 px").arg(b->iconSize().width());
                 }
             }
             s.picture = stackOf({{describe("Qt's, docked", qts), magnified(qts)},
                                  {describe("ours, floating", ours), magnified(ours)},
                                  {describe(bare_what.toUtf8().constData(), bare), magnified(bare)}});
         }},

        {"panels-the-title-bar-docked-and-floating",
         "issue #50: the same panel's title bar docked (Qt draws it) above, and floating (we "
         "draw it) below. The strip and its close button must be the same size in both -- a "
         "button that grows when a panel is torn off was reported and is what this shot exists "
         "to catch",
         [](Stage& s) {
             auto* dock = qobject_cast<QDockWidget*>(s.layerPanel());
             if (!dock) return;

             // Docked first: Qt's own title bar, which is the thing to match.
             // Its height is where the panel's contents begin.
             const QImage docked = Stage::stripOf(dock);
             const int docked_content_top = dock->widget() ? dock->widget()->geometry().y() : 0;
             // The device pixel ratio of each grab, because if the two differ
             // then this comparison magnifies one more than the other and
             // "bigger" is the picture's fault rather than the widget's.
             const qreal docked_dpr = dock->grab().devicePixelRatio();

             dock->setFloating(true);
             s.settle();
             // The watcher waits for the pointer to be let go, which never
             // happens here because nothing is holding it. Nudged rather than
             // waited for, so the shot is not a race.
             if (auto* frame = dock->findChild<FloatingDockFrame*>()) frame->applyIfNothingIsHeld();
             s.settle();
             dock->resize(280, 360);
             s.settle();
             const QImage floating = Stage::stripOf(dock);

             // The heights, because "it looks bigger" is a guess and a number is
             // not. Qt's docked title bar is however far down its content
             // starts; ours is the widget we supplied.
             const int qt_tall = docked_content_top;
             const int ours_tall = dock->titleBarWidget() ? dock->titleBarWidget()->height() : 0;
             QString ours_icon = QStringLiteral(", dpr %1").arg(dock->grab().devicePixelRatio());
             if (QWidget* bar = dock->titleBarWidget()) {
                 if (auto* b = bar->findChild<QAbstractButton*>()) {
                     ours_icon += QStringLiteral(", button %1x%2, icon %3")
                                      .arg(b->width())
                                      .arg(b->height())
                                      .arg(b->iconSize().width());
                 }
             }
             s.picture = stackOf(
                 {{QStringLiteral("docked -- Qt's own, %1 px tall, dpr %2")
                       .arg(qt_tall)
                       .arg(docked_dpr),
                   docked},
                  {QStringLiteral("floating -- ours, %1 px tall%2").arg(ours_tall).arg(ours_icon),
                   floating}});
         }},

        {"panels-1-the-layer-panel-floated",
         "the layer panel undocked and floating, timeline still where it was: the state the "
         "glitch is reached from, and nothing should be wrong yet",
         [](Stage& s) {
             if (auto* dock = qobject_cast<QDockWidget*>(s.layerPanel())) {
                 dock->setFloating(true);
                 dock->resize(260, 420);
             }
             s.settle();
         }},

        {"panels-2-then-hidden",
         "and both panels hidden from there: the strip the timeline was in should be gone, "
         "not left behind as a pale rectangle",
         [](Stage& s) {
             if (auto* dock = qobject_cast<QDockWidget*>(s.layerPanel())) {
                 dock->setFloating(true);
                 dock->resize(260, 420);
             }
             s.settle();
             s.press(Id::TogglePanels);
         }},

        {"panels-0-what-the-docks-say",
         "not a picture: the docks' visibility, floating state, area and geometry printed at "
         "each step, because the reported glitch is a layout state and a screenshot of it only "
         "says which step looks wrong",
         [](Stage& s) {
             const auto report = [&s](const char* when) {
                 std::printf("      --- %s\n", when);
                 for (QDockWidget* dock : s.window.findChildren<QDockWidget*>()) {
                     const QRect g = dock->geometry();
                     std::printf(
                         "        %-10s visible=%-5s floating=%-5s area=%d  geom=%d,%d %dx%d\n",
                         qPrintable(dock->windowTitle()), dock->isVisible() ? "yes" : "no",
                         dock->isFloating() ? "yes" : "no",
                         static_cast<int>(s.window.dockWidgetArea(dock)), g.x(), g.y(), g.width(),
                         g.height());
                 }
                 if (QWidget* central = s.window.centralWidget()) {
                     const QRect g = central->geometry();
                     std::printf("        canvas     geom=%d,%d %dx%d\n", g.x(), g.y(), g.width(),
                                 g.height());
                 }
             };
             report("as it opens (one track)");
             // One track, so syncTimelineHeight has returned early every time
             // and resizeDocks has never run. Hiding here is the case that was
             // measured before and looked fine.
             s.press(Id::TogglePanels);
             report("hidden, one track");
             s.press(Id::TogglePanels);

             // Now with three, which is what makes syncTimelineHeight actually
             // call resizeDocks. That call is the one difference between this
             // and plain Qt, where the same hide works.
             for (int t = 0; t < 2; ++t) s.choose("Add track");
             s.settle();
             report("three tracks, shown");
             s.press(Id::TogglePanels);
             report("hidden, three tracks");
             s.press(Id::TogglePanels);
             report("shown again, three tracks");
             // setFloating and *not* a synthetic drag on the title bar. A
             // hand-driven drag in plain Qt reclaims the space correctly -- it
             // was checked -- but a synthetic one leaves Qt's drag state machine
             // half finished and reports a fault that is not there. That
             // mistake cost a whole rewrite, so it is written down here rather
             // than repeated.
             if (auto* dock = qobject_cast<QDockWidget*>(s.layerPanel())) {
                 dock->setFloating(true);
                 dock->resize(260, 420);
             }
             s.settle();
             report("layer panel floated");
             s.press(Id::TogglePanels);
             report("panels hidden");
             s.press(Id::TogglePanels);
             report("panels shown again");

             // Resizing while a panel is floating, which is where the reported
             // fault lives. It does *not* reproduce from setFloating: this
             // reports a correct relayout, and only a hand-driven drag freezes
             // it. Kept so that the difference stays visible.
             s.window.resize(kWindowWidth - 60, kWindowHeight + 40);
             s.settle();
             report("window resized while floating");
             // Asked and answered: invalidating and activating the layout does
             // not recover it either, so this is not a relayout that was merely
             // never triggered. The space is still spoken for.
             if (QLayout* layout = s.window.layout()) {
                 layout->invalidate();
                 layout->activate();
             }
             s.settle();
             report("and the layout invalidated and activated");
         }},

        {"panels-4-what-the-timeline-dock-is-made-of",
         "not a picture: every height between the dock and the strip inside it, printed at each "
         "step. #26 and #57 are both a few pixels of the dock going somewhere, and the picture "
         "only ever says that something is a bit short",
         [](Stage& s) {
             QScrollArea* scroll = nullptr;
             for (QWidget* w = s.timeline; w; w = w->parentWidget()) {
                 if (auto* area = qobject_cast<QScrollArea*>(w)) { scroll = area; break; }
             }
             auto* dock = qobject_cast<QDockWidget*>(s.timelinePanel());
             auto* layers = qobject_cast<QDockWidget*>(s.layerPanel());
             if (!scroll || !dock || !layers) return;

             const auto report = [&](const char* when) {
                 QWidget* body = dock->widget();
                 QScrollBar* hbar = scroll->horizontalScrollBar();
                 QScrollBar* vbar = scroll->verticalScrollBar();
                 std::printf("      --- %s\n", when);
                 std::printf("        dock     %d tall, hint %d, spare %d, title bar %d, body %d\n",
                             dock->height(), dock->sizeHint().height(),
                             dock->height() - dock->sizeHint().height(),
                             body ? body->y() : -1, body ? body->height() : -1);
                 std::printf("        scroll   %d tall, hint %d, viewport %d, strip %d "
                             "(min %d, hint %d)\n",
                             scroll->height(), scroll->sizeHint().height(),
                             scroll->viewport()->height(), s.timeline->height(),
                             s.timeline->minimumHeight(), s.timeline->sizeHint().height());
                 std::printf("        bars     h %s %d tall, v %s %d wide\n",
                             hbar->isVisible() ? "shown" : "hidden", hbar->sizeHint().height(),
                             vbar->isVisible() ? "shown" : "hidden", vbar->sizeHint().width());
                 std::printf("        cut off  %d px of the strip is not in the viewport\n",
                             s.timeline->height() - scroll->viewport()->height());
                 std::printf("        layers   %d wide (min %d), area %d, floating %s\n",
                             layers->width(), layers->widget() ? layers->widget()->minimumWidth() : -1,
                             static_cast<int>(s.window.dockWidgetArea(layers)),
                             layers->isFloating() ? "yes" : "no");
             };

             report("as it opens: one track, a shot short enough to fit");

             // Long enough that the strip is wider than the window, which is
             // the whole of #26: the pan slider only exists once it is.
             for (int i = 0; i < 60; ++i) s.press(Id::InsertDrawing);
             s.settle();
             report("60 drawings, so the strip no longer fits across");

             for (int t = 0; t < 2; ++t) s.choose("Add track");
             s.settle();
             report("three tracks, so syncTimelineHeight has asked for two more rows");

             // The last three steps are #57 and #55 asked from code, and the
             // answer is that **neither reproduces**: the timeline holds its
             // height and the layer dock holds its width through all of them.
             //
             // Kept rather than deleted, because that is worth knowing before
             // anybody spends an afternoon on it. setFloating and addDockWidget
             // do not enter Qt's drag, so they do not produce the state a hand
             // produces -- the same reason panels-0 says a synthetic drag lies.
             // Both issues need a real hand, and tests/dock_probe.cpp is where
             // that hand goes.
             layers->setFloating(true);
             s.settle();
             report("layer panel floated (#57 -- and it does not lose height)");

             layers->setFloating(false);
             s.settle();
             report("and put back");

             s.window.addDockWidget(Qt::LeftDockWidgetArea, layers);
             s.settle();
             report("layer panel moved to the left area (#55 -- and it keeps its width)");

             // And the gesture #57 is actually about, which is the *timeline*
             // being torn off and put back -- not another panel leaving. Both
             // ways of putting height into it are tried, because either could be
             // the one that survives: the rows, which is syncTimelineHeight
             // asking through resizeDocks, and a drag, which is a hand.
             s.window.resizeDocks({dock}, {dock->height() + 120}, Qt::Vertical);
             s.settle();
             report("and the timeline dragged 120 px taller");

             dock->setFloating(true);
             s.settle();
             report("timeline floated");

             // What a hand also gets and setFloating alone does not: issue #50's
             // title bar going on, which recreates the window. The watcher waits
             // for the pointer to be let go, and nothing is holding it here, so
             // it is nudged rather than waited for.
             if (auto* frame = dock->findChild<FloatingDockFrame*>()) frame->applyIfNothingIsHeld();
             s.settle();
             report("and our title bar applied to it (#50's frame)");

             dock->setFloating(false);
             s.settle();
             report("timeline docked again (#57 -- does it come back shorter?)");
         }},

        {"panels-5-tearing-the-timeline-off",
         "not a picture: #57. The timeline dock torn off and put back, from three starting "
         "heights, because the reporter found it only loses height when it has not been "
         "dragged taller first -- so how much spare height it holds is the variable",
         [](Stage&) {
             // Three runs of one gesture, and a fresh window for each: a dock
             // that has already been floated once is not the same dock, and
             // running the cases in one window would let the first contaminate
             // the rest. Which is what a Stage is cheap enough for.
             struct Case {
                 const char* what;
                 int tracks;
                 int drag_taller_by;
             };
             for (const Case& c : {Case{"one track, left at the height it opens at", 1, 0},
                                   Case{"three tracks, left at the height syncTimelineHeight asks for", 3, 0},
                                   Case{"three tracks, then dragged 120 px taller by hand", 3, 120}}) {
                 Stage own;
                 auto* dock = qobject_cast<QDockWidget*>(own.timelinePanel());
                 if (!dock) continue;
                 for (int t = 1; t < c.tracks; ++t) own.choose("Add track");
                 if (c.drag_taller_by > 0) {
                     own.window.resizeDocks({dock}, {dock->height() + c.drag_taller_by},
                                            Qt::Vertical);
                 }
                 own.settle();

                 const int before = dock->height();
                 const int hint = dock->sizeHint().height();

                 dock->setFloating(true);
                 own.settle();
                 if (auto* frame = dock->findChild<FloatingDockFrame*>())
                     frame->applyIfNothingIsHeld();
                 own.settle();
                 const int floated = dock->height();

                 dock->setFloating(false);
                 own.settle();
                 const int after = dock->height();

                 std::printf("      %-52s  before %3d (hint %3d, spare %3d)  "
                             "floated %3d  back %3d  %s\n",
                             c.what, before, hint, before - hint, floated, after,
                             after == before ? "kept it"
                                             : (after < before ? "** SHORTER **" : "** taller **"));
             }
         }},

        {"panels-3-then-shown-again",
         "and brought back: the timeline should be drawn in full, with no white rectangle over "
         "it and its height the one it had",
         [](Stage& s) {
             if (auto* dock = qobject_cast<QDockWidget*>(s.layerPanel())) {
                 dock->setFloating(true);
                 dock->resize(260, 420);
             }
             s.settle();
             s.press(Id::TogglePanels);
             s.press(Id::TogglePanels);
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

    // What drew these, said once and at the top, because it is the first thing
    // to check when a picture and a report disagree. Both halves matter. The
    // style: offscreen has no native one to fall back on and gets fusion, while
    // a Windows desk gets windows11, and they do not draw the same controls. The
    // Qt version: a control's appearance is Qt's to change and it does change,
    // so a picture from one Qt says nothing about a build made against another.
    // Issue #75 was the second of those and cost a session. See the note at the
    // top of this file.
    std::printf("\nQt %s, platform %s, style %s\n", qVersion(),
                qPrintable(QGuiApplication::platformName()),
                qPrintable(QApplication::style()->objectName()));
    std::printf("Writing to %s\n\n", qPrintable(QDir::toNativeSeparators(where)));

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
