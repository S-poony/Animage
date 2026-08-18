// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline_widget.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPalette>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

#include "name_limits.h"

using namespace animage;

namespace {

constexpr int kCellWidth = 26;
constexpr int kRulerHeight = 18;
constexpr int kRowHeight = 46;
constexpr int kEdgeGrab = 5;
constexpr int kDragThreshold = 5;

// The strip of track names down the left. It is what makes a row say which
// track it is without a control on it -- see the handover on why a widget on a
// row is a trap -- and it is where the current track is shown as current. It is
// also the handle a row is restacked by, which is the other thing a strip with
// no controls on it is free to be.
constexpr int kGutterWidth = 104;

// Taken from the widget's palette rather than hardcoded, so the timeline
// belongs to the same application as everything above it -- and follows a dark
// theme too, if the system asks for one.
struct Palette {
    QColor background, ruler, cell, cell_held, outline, tick, text, current, current_text;
    QColor carried, gutter, gutter_current, outside, boundary;
};

// A palette role with its alpha taken off.
QColor opaque(QColor colour) {
    colour.setAlpha(255);
    return colour;
}

// `amount` hundredths of the way from `from` towards the far end: white when the
// theme is dark, black when it is light.
//
// This is what QColor::lighter and darker were doing here, and it replaces them
// because they cannot do it at the ends. They scale the HSV *value*, so the step
// they take is proportional to the colour they start from -- and at zero there is
// nothing to scale. `lighter(180)` on black returns black. Every colour derived
// from a black one lands on the same black, and the row draws as a slab.
//
// Stepping a fixed fraction of the distance to the end instead means the
// separation is the same wherever it starts, which is the property actually
// wanted: these numbers exist to say "a bit darker than the cells" and "enough
// darker to read as a line", and neither of those is a multiple of anything.
QColor stepped(const QColor& from, bool dark, int amount) {
    const int target = dark ? 255 : 0;
    const auto mix = [&](int channel) { return channel + (target - channel) * amount / 100; };
    return QColor(mix(from.red()), mix(from.green()), mix(from.blue()));
}

// Everything structural is derived from Base -- the colour of a cell -- and not
// from Window.
//
// Window is the role this used to build on, and twice now it has not been a
// colour at all. The shipped Windows build's Qt hands it over as #00000000:
// transparent, and black underneath. Both of the ways that went wrong are worth
// keeping, because they look like different bugs and are one.
//
// Transparent first. lighter and darker carry alpha through untouched, so every
// derived colour came out invisible at once -- background, ruler, gutter, every
// outline -- while the drawing numbers and the playhead kept drawing, those being
// roles used as they come rather than bent. The row was white cells on whatever
// was behind the widget. Making the colours opaque fixed that and uncovered the
// second: opaque #00000000 is pure black, `lightness()` reads 0, the theme is
// taken for a dark one, and lighter() cannot lift a black. Same widget, same
// commit, black slab instead of a white one.
//
// So the lesson is not "force the alpha" -- that was half of it, and shipping it
// alone made the picture worse. It is that Window is a role this widget cannot
// substantiate. Base can be: it is what a cell is painted with, it is what every
// text field in the application stands on, and a theme that gets it wrong is
// broken in a way somebody has already reported. It arrives as #ffffff in the
// build that draws this correctly and in the build that does not.
//
// See `the-timeline-palette` in tests/shots.cpp, which prints these, and the two
// situations beside it that hold each failure still. Nothing else can see either:
// the suite is green through both, and the Qt on this desk hands over a Window
// that is opaque and mid-grey, so neither reproduces here by accident.
Palette paletteFor(const QWidget& widget) {
    const QPalette& source = widget.palette();
    const QColor base = opaque(source.color(QPalette::Base));
    const QColor text = source.color(QPalette::WindowText);
    const bool dark = base.lightness() < 128;

    // Hundredths towards the far end, chosen to land on what the derivation from
    // Window used to give on an ordinary light theme -- #e1e1e1 background,
    // #cecece ruler, #aeaeae outline -- so this is the same row, arrived at by a
    // route that cannot collapse.
    Palette p;
    p.cell = base;
    p.background = stepped(base, dark, 12);
    p.ruler = stepped(base, dark, 19);
    p.outline = stepped(base, dark, 32);
    p.gutter = stepped(base, dark, 15);
    p.cell_held = stepped(base, dark, 6);
    p.tick = text;
    p.text = text;
    p.current = opaque(source.color(QPalette::Highlight));
    p.current_text = source.color(QPalette::HighlightedText);
    p.gutter_current = p.current;
    // Not from the palette: this has to mean the same thing in every theme, and
    // "carried" is not a role a system palette has.
    p.carried = QColor(0x5b, 0x9c, 0xd6);
    // A wash over cells outside the shot rather than a different cell colour, so
    // whatever the cell was still reads through it -- held, carried, numbered.
    p.outside = QColor(p.background.red(), p.background.green(), p.background.blue(), 150);
    // "The shot ends here" has to mean the same in every theme, and it is not a
    // role a system palette has.
    p.boundary = QColor(0xd0, 0x45, 0x3c);
    return p;
}

}  // namespace

TimelineWidget::TimelineWidget(Document& document, QWidget* parent)
    : QWidget(parent), doc_(document) {
    setMinimumHeight(kRulerHeight + kRowHeight);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
}

const Track* TimelineWidget::trackAt(std::size_t row) const {
    const std::vector<Track>& tracks = doc_.scene().tracks;
    return (row < tracks.size()) ? &tracks[row] : nullptr;
}

const Track* TimelineWidget::currentTrack() const { return doc_.scene().findTrack(track_); }

std::size_t TimelineWidget::rowOf(TrackId track) const {
    const std::vector<Track>& tracks = doc_.scene().tracks;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].id == track) return i;
    }
    return tracks.size();
}

int TimelineWidget::rowTop(std::size_t row) const {
    return kRulerHeight + static_cast<int>(row) * kRowHeight;
}

// The middle of a cell, and a point in the ruler above it. For tests that press
// where a hand would press rather than working the layout out for themselves --
// which would agree perfectly with a card drawn somewhere nobody can reach.
QPoint TimelineWidget::cellCentreForTesting(std::size_t row, std::size_t slot) const {
    return {kGutterWidth + static_cast<int>(slot) * kCellWidth + kCellWidth / 2,
            rowTop(row) + kRowHeight / 2};
}

QPoint TimelineWidget::gutterPointForTesting(std::size_t row) const {
    return {kGutterWidth / 2, rowTop(row) + kRowHeight / 2};
}

QPoint TimelineWidget::rulerPointForTesting(std::size_t slot) const {
    return {kGutterWidth + static_cast<int>(slot) * kCellWidth + kCellWidth / 2, kRulerHeight / 2};
}

bool TimelineWidget::rowAtY(int y, std::size_t* row) const {
    if (y < kRulerHeight || rowCount() == 0) return false;
    const std::size_t at = static_cast<std::size_t>((y - kRulerHeight) / kRowHeight);
    if (row) *row = std::min(at, rowCount() - 1);
    return true;
}

void TimelineWidget::setTrack(TrackId track) {
    if (track_ == track) return;
    track_ = track;
    refresh();
    Q_EMIT trackChanged(track_);
}

void TimelineWidget::setCurrentSlot(std::size_t slot) {
    // Against the scene and not against one track: the playhead is the
    // timeline's, so it can stand on a frame that this track does not reach.
    const std::size_t frames = doc_.scene().timelineFrames();
    if (frames == 0) return;
    const std::size_t clamped = std::min(slot, frames - 1);
    if (clamped == current_slot_) return;
    current_slot_ = clamped;
    update();
    Q_EMIT currentSlotChanged(current_slot_);
}

void TimelineWidget::refresh() {
    // A rename outlives an undo, a restack or a track being deleted underneath
    // it, none of which the editor would otherwise notice. Its track is held by
    // id, so the only unanswerable case is the track going away.
    if (renaming_ != kNoId) {
        if (!doc_.scene().findTrack(renaming_)) {
            finishRenaming(false);
        } else {
            rename_edit_->setGeometry(gutterRectFor(rowOf(renaming_)));
        }
    }

    const std::size_t frames = doc_.scene().timelineFrames();
    if (frames > 0 && current_slot_ >= frames) {
        current_slot_ = frames - 1;
        Q_EMIT currentSlotChanged(current_slot_);
    }
    updateGeometry();
    setMinimumWidth(kGutterWidth + static_cast<int>(frames + 2) * kCellWidth);
    setMinimumHeight(kRulerHeight + static_cast<int>(std::max<std::size_t>(rowCount(), 1)) *
                                        kRowHeight);
    update();
}

QSize TimelineWidget::sizeHint() const {
    const int frames = static_cast<int>(doc_.scene().timelineFrames());
    const int rows = static_cast<int>(std::max<std::size_t>(rowCount(), 1));
    return {kGutterWidth + (frames + 2) * kCellWidth, kRulerHeight + rows * kRowHeight};
}

std::size_t TimelineWidget::slotAt(int x) const {
    const std::size_t frames = doc_.scene().timelineFrames();
    if (frames == 0) return 0;
    const int index = std::clamp((x - kGutterWidth) / kCellWidth, 0, static_cast<int>(frames) - 1);
    return static_cast<std::size_t>(index);
}

int TimelineWidget::sceneEndX() const {
    return kGutterWidth + static_cast<int>(doc_.scene().shotFrames()) * kCellWidth;
}

bool TimelineWidget::isOnSceneEnd(int x) const {
    // Only when the scene is the one saying where the shot ends. With the
    // setting off there is no boundary drawn, and a grab zone you cannot see is
    // worse than no handle at all.
    if (!doc_.scene().fixed_length) return false;
    return std::abs(x - sceneEndX()) <= kEdgeGrab;
}

std::pair<std::size_t, std::size_t> TimelineWidget::runAt(std::size_t row,
                                                          std::size_t slot) const {
    const Track* line = trackAt(row);
    if (!line) return {slot, slot};
    return line->runBounds(slot);
}

// The numbered card under x, if there is one.
//
// Deliberately not built on slotAt, which *clamps*: past the end of the strip it
// answers "the last slot", and everything downstream then believes there is a
// card under a pointer that is nowhere near one. With every drawing held for a
// frame or two the clamp lands mid-run and the mistake is invisible, so this is
// a bug that only appears in a track of single-frame drawings -- where the last
// slot is its own run, and the whole width of the widget past the strip offers
// that drawing to be dragged.
//
// A clamp is right for the playhead: clicking past the end means the last frame,
// because there is always a frame you are standing on. It is wrong for "what is
// under the pointer", because sometimes the answer is nothing.
bool TimelineWidget::cardAt(std::size_t row, int x, std::size_t* slot) const {
    const Track* line = trackAt(row);
    if (!line || x < kGutterWidth) return false;

    const std::size_t at = static_cast<std::size_t>((x - kGutterWidth) / kCellWidth);
    if (at >= line->slots.size()) return false;
    // A held frame is the same drawing still showing. There is no second object
    // there to pick up, so only the first slot of a run is a card.
    if (line->runBounds(at).first != at) return false;

    if (slot) *slot = at;
    return true;
}

bool TimelineWidget::isOnRunEdge(std::size_t row, int x, std::size_t* run_start) const {
    const Track* line = trackAt(row);
    if (!line || line->slots.empty()) return false;

    const std::size_t slot = slotAt(x);
    if (slot >= line->slots.size()) return false;
    const auto [first, last] = runAt(row, slot);
    const int edge = kGutterWidth + static_cast<int>(last + 1) * kCellWidth;
    if (std::abs(x - edge) > kEdgeGrab) return false;
    if (run_start) *run_start = first;
    return true;
}

// The number the drawing was born with, read straight off the Image. Deriving
// it from position instead meant a drawing renumbered itself the moment it was
// dragged, which is precisely when you need to know which one you are holding.
std::vector<int> TimelineWidget::drawingNumbers(const Track& track) const {
    std::vector<int> numbers;
    numbers.reserve(track.slots.size());
    for (ImageId id : track.slots) {
        const Image* image = track.findImage(id);
        numbers.push_back(image ? image->number : 0);
    }
    return numbers;
}

// What to show on one drawing's card about its colour layers.
//
// A walk over the slots and nothing else, so it costs nothing and is true for
// every drawing whether or not anybody has been there. There was a second thing
// here -- a wedge for a drawing whose carried marks had landed badly -- and it
// was removed; see docs/handover.md for the measurements that took it out.
TimelineWidget::ColourState TimelineWidget::colourStateFor(const Track& track,
                                                           ImageId image) const {
    ColourState state;
    if (image == kNoId) return state;

    for (const Layer& layer : track.layers) {
        if (layer.kind != LayerKind::Ctg || !layer.visible) continue;

        ImageId from = kNoId;
        if (!doc_.ctgScribblesAt(track.id, image, layer.id, &from)) continue;
        state.any = true;
        if (from != image) state.carried = true;
    }
    return state;
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    const Palette colours = paletteFor(*this);

    QPainter painter(this);
    painter.fillRect(rect(), colours.background);
    painter.fillRect(QRect(0, 0, width(), kRulerHeight), colours.ruler);

    QFont font = painter.font();
    font.setPointSizeF(8.5);
    painter.setFont(font);

    const std::size_t frames = doc_.scene().timelineFrames();
    const std::size_t shot = doc_.scene().shotFrames();

    // The ruler, once, across the whole width: it is the scene's time and not
    // any one track's.
    for (std::size_t i = 0; i < frames; ++i) {
        if ((i % 5) != 0) continue;
        const int x = kGutterWidth + static_cast<int>(i) * kCellWidth;
        painter.setPen(colours.tick);
        painter.drawText(QRect(x, 0, kCellWidth, kRulerHeight), Qt::AlignCenter,
                         QString::number(i + 1));
    }

    for (std::size_t row = 0; row < rowCount(); ++row) {
        const Track& line = *trackAt(row);
        const int top = rowTop(row);
        const bool is_current = line.id == track_;

        // The gutter: which track this row is, and which one is being edited.
        const QRect gutter(0, top, kGutterWidth - 2, kRowHeight - 2);
        painter.fillRect(gutter, is_current ? colours.gutter_current : colours.gutter);
        painter.setPen(is_current ? colours.current_text : colours.text);
        painter.drawText(gutter.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         QString::fromStdString(line.name));

        // Nothing at all past the track's last drawing, whatever its end
        // behaviour is. Faint dotted cells were tried there and read worse than
        // an empty row: a cell is a frame you can put a drawing on, so drawing
        // cells you cannot click is a harder thing to explain than the absence
        // of any. What the track *does* out there wants saying at the end of the
        // row rather than along it -- see issue #22.
        const std::vector<int> numbers = drawingNumbers(line);
        for (std::size_t i = 0; i < line.slots.size(); ++i) {
            const int x = kGutterWidth + static_cast<int>(i) * kCellWidth;
            const bool held = i > 0 && line.slots[i - 1] == line.slots[i];
            const QRect cell(x, top + 2, kCellWidth - 1, kRowHeight - 8);

            painter.fillRect(cell, held ? colours.cell_held : colours.cell);
            // Outside the shot: there is a drawing here and you may work on it,
            // but it will not play and will not be exported until the boundary
            // is dragged past it. Said on the cell, because an export that
            // quietly leaves work out is the expensive kind of surprise.
            if (i >= shot) painter.fillRect(cell, colours.outside);

            if (held) {
                // A held drawing is one block with a tail, not a repeated cell.
                painter.setPen(QPen(colours.outline, 1));
                painter.drawLine(cell.center().x(), cell.top() + 3, cell.center().x(),
                                 cell.bottom() - 3);
            } else {
                painter.setPen(QPen(colours.outline, 1));
                painter.drawRect(cell.adjusted(0, 0, -1, -1));
                painter.setPen(colours.text);
                painter.drawText(cell, Qt::AlignCenter, QString::number(numbers[i]));

                // A mark on the card, on the numbered one only: a held frame is
                // the same drawing still showing, so it has nothing of its own
                // to report.
                //
                // The feature is invisible when it works -- a carried mark looks
                // exactly like one you drew -- so the only way to know it is
                // working is to be told. A bar under the number for colour that
                // was carried here.
                const ColourState state = colourStateFor(line, line.slots[i]);
                if (state.carried) {
                    painter.fillRect(
                        QRect(cell.left() + 4, cell.bottom() - 4, cell.width() - 8, 2),
                        colours.carried);
                }
            }
        }

        if (dragging_ && drag_row_ == row) {
            if (drop_overwrites_) {
                // The frames it would take over, outlined where they are.
                const int from = kGutterWidth + static_cast<int>(drop_first_) * kCellWidth;
                const int span = static_cast<int>(drop_last_ - drop_first_ + 1) * kCellWidth;
                painter.setPen(QPen(colours.current, 2));
                painter.drawRect(QRect(from, top + 1, span - 1, kRowHeight - 6));
            } else if (drop_index_ >= 0) {
                // Count past the drawing being carried: it is not in the track
                // it is about to be dropped into.
                int seen = 0;
                int at = static_cast<int>(line.slots.size());
                for (std::size_t i = 0; i < line.slots.size(); ++i) {
                    if (line.slots[i] == drag_image_) continue;
                    if (seen == drop_index_) {
                        at = static_cast<int>(i);
                        break;
                    }
                    ++seen;
                }
                painter.setPen(QPen(colours.current, 3));
                const int x = kGutterWidth + at * kCellWidth;
                painter.drawLine(x, top, x, top + kRowHeight - 4);
            }
        }

        // The rim goes on the current track's row only. There is one playhead
        // and it is the scene's -- the ruler says where it is, across all of
        // them -- but the rim means "this is the frame you are editing", and
        // that is true of exactly one row. Drawn on every row it read as four
        // selections, none of which the brush was going to touch.
        if (is_current && current_slot_ < frames) {
            const int x = kGutterWidth + static_cast<int>(current_slot_) * kCellWidth;
            painter.setPen(QPen(colours.current, 2));
            painter.drawRect(QRect(x, top + 1, kCellWidth - 1, kRowHeight - 6));
        }
    }

    // Where a row being carried would land: a line across the whole width and
    // not only across the gutter, because the whole row goes -- the name is the
    // handle, the row is the thing being moved.
    if (dragging_track_ && track_drop_row_ >= 0) {
        // Pulled inside the bottom edge, or the caret for "below the last row"
        // is the one row of pixels the widget does not have.
        const int y = std::min(rowTop(static_cast<std::size_t>(track_drop_row_)), height() - 2);
        painter.setPen(QPen(colours.current, 3));
        painter.drawLine(0, y, width(), y);
    }

    if (current_slot_ < frames) {
        const int x = kGutterWidth + static_cast<int>(current_slot_) * kCellWidth;
        painter.fillRect(QRect(x, 0, kCellWidth - 1, kRulerHeight), colours.current);
        painter.setPen(colours.current_text);
        painter.drawText(QRect(x, 0, kCellWidth, kRulerHeight), Qt::AlignCenter,
                         QString::number(current_slot_ + 1));
    }

    // Where the shot ends, drawn last so nothing paints over it. A line down the
    // whole panel with a grip in the ruler: the line is the fact, the grip is
    // the control, and the ruler is where a scene-level control belongs -- the
    // rows below are a track's own time.
    //
    // Only when the scene fixes the length. Left to the tracks, the shot ends
    // where the longest one does and the rows already show that by stopping, so
    // the line would say nothing twice -- in a colour that means a constraint,
    // while none is being applied.
    if (doc_.scene().fixed_length) {
        const int end_x = sceneEndX();
        painter.setPen(QPen(colours.boundary, 2));
        painter.drawLine(end_x, 0, end_x, height());
        painter.setBrush(colours.boundary);
        painter.setPen(Qt::NoPen);
        painter.drawRect(QRect(end_x - 3, 0, 6, kRulerHeight));
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int x = static_cast<int>(event->position().x());
    const int y = static_cast<int>(event->position().y());

    // A pen's two taps arrive here as two presses and never as a double click.
    // See DoubleTap: this is the pen's way into the same rename the mouse
    // reaches through mouseDoubleClickEvent, and the two cannot both fire.
    if (taps_.isSecond(event->globalPosition().toPoint(), event->timestamp()) &&
        renameAt(x, y)) {
        return;
    }

    // The ruler is a scrub band, apart from the end-of-shot grip in it. Exposure
    // edges live below it, so dragging along time can never resize a hold by
    // accident, and the grip is the one thing up here that is not scrubbing.
    if (y < kRulerHeight) {
        if (isOnSceneEnd(x)) {
            dragging_end_ = true;
            refreshCursor(x, y);
            // One command for the whole drag, as the exposure stretch does, so
            // dragging the boundary about undoes in a single step.
            doc_.beginCommand("Scene length");
            return;
        }
        scrubbing_ = true;
        setCurrentSlot(slotAt(x));
        return;
    }

    std::size_t row = 0;
    if (!rowAtY(y, &row)) return;
    const Track* line = trackAt(row);
    if (!line) return;

    // Pressing anywhere in a row selects its track. Selecting is what a row is
    // for, and requiring people to find the name strip to do it would make the
    // rest of the row a place where clicking edits a track you are not on.
    setTrack(line->id);

    // The name strip selects, and it is the handle a row is restacked by. There
    // is no frame under it, so nothing else here is competing for the press --
    // which is exactly why the handle is here and not on the row itself.
    if (x < kGutterWidth) {
        may_drag_track_ = true;
        track_drag_row_ = row;
        press_y_ = y;
        return;
    }

    std::size_t run_start = 0;
    if (isOnRunEdge(row, x, &run_start)) {
        stretching_ = true;
        stretch_row_ = row;
        stretch_run_start_ = run_start;
        refreshCursor(x, y);
        // One command for the whole drag: nested commands collapse, so every
        // slot change during the drag undoes in a single step.
        doc_.beginCommand("Change exposure");
        return;
    }

    // The playhead clamps: past the end of the strip you are still standing on
    // the last frame there is.
    setCurrentSlot(slotAt(x));

    // A move does not. Only the numbered card starts one, and past the end of
    // the strip there is no card -- which is the same question the cursor asks,
    // and it has to be the same answer or the pointer is lying.
    std::size_t card = 0;
    if (cardAt(row, x, &card)) {
        may_drag_ = true;
        drag_row_ = row;
        drag_image_ = line->slots[card];
        press_x_ = x;
    }
}

// Where the drawing would land, counted in the track as it will be once the
// drawing has been lifted out of it.
int TimelineWidget::dropIndexFor(int pointer_x) const {
    const Track* line = trackAt(drag_row_);
    if (!line || drag_image_ == kNoId) return 0;

    const int boundary = (pointer_x - kGutterWidth + kCellWidth / 2) / kCellWidth;
    int index = 0;
    for (int i = 0; i < boundary && i < static_cast<int>(line->slots.size()); ++i) {
        if (line->slots[static_cast<std::size_t>(i)] != drag_image_) ++index;
    }
    return index;
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    const int x = static_cast<int>(event->position().x());
    const int y = static_cast<int>(event->position().y());

    if (dragging_end_) {
        // Dragging it says where the shot ends, which is exactly what fixing the
        // length means -- so the drag turns the setting on rather than needing it
        // on first.
        const int frames = static_cast<int>(
            std::lround(static_cast<double>(x - kGutterWidth) / kCellWidth));
        doc_.setSceneLength(true, std::max(1, frames));
        refresh();
        Q_EMIT documentChanged();
        return;
    }

    if (scrubbing_) {
        setCurrentSlot(slotAt(x));
        return;
    }

    // A row is picked up by moving *down or up* the strip, where a drawing is
    // picked up by moving along it. Neither can start the other: they begin on
    // different sides of the gutter.
    if (may_drag_track_ && !dragging_track_ && std::abs(y - press_y_) >= kDragThreshold) {
        dragging_track_ = true;
        refreshCursor(x, y);
    }
    if (dragging_track_) {
        const int at = trackDropRowFor(y);
        if (at != track_drop_row_) {
            track_drop_row_ = at;
            update();
        }
        return;
    }

    if (may_drag_ && !dragging_ && std::abs(x - press_x_) >= kDragThreshold) {
        dragging_ = true;
        refreshCursor(x, y);
    }
    if (dragging_) {
        const Track* line = trackAt(drag_row_);
        const bool overwrites = line && line->overwrite_drawings;
        const std::size_t slot = slotAt(x);

        if (overwrites) {
            // The range it would take over, worked out the same way the model
            // will work it out: the drawing is lifted out first, so the run it
            // lands in is the one it leaves behind.
            std::size_t first = slot;
            std::size_t last = slot;
            bool room = false;
            if (line && slot < line->slots.size()) {
                const auto range = line->overwriteRangeAt(slot);
                if (range) {
                    first = range->first;
                    last = range->second;
                    room = true;
                }
            }
            if (room != drop_overwrites_ || first != drop_first_ || last != drop_last_) {
                drop_overwrites_ = room;
                drop_first_ = first;
                drop_last_ = last;
                drop_index_ = static_cast<int>(slot);
                update();
            }
            return;
        }

        drop_overwrites_ = false;
        const int drop = dropIndexFor(x);
        if (drop != drop_index_) {
            drop_index_ = drop;
            update();
        }
        return;
    }

    if (stretching_) {
        applyStretch(x);
        return;
    }

    refreshTooltip(x, y);
    refreshCursor(x, y);
}

// The boundary between rows nearest y: 0 is above the first row, rowCount() is
// below the last. A boundary and not a row, because a drop lands *between* two
// rows and there is one more of those than there are rows.
int TimelineWidget::trackDropRowFor(int y) const {
    if (rowCount() == 0) return 0;
    const int at = static_cast<int>(
        std::lround(static_cast<double>(y - kRulerHeight) / kRowHeight));
    return std::clamp(at, 0, static_cast<int>(rowCount()));
}

// What the row is, and what it does with a drawing put down on it.
//
// The gutter used to carry the word "overwrite" under the name, and it was
// reported as clutter: it is on nearly every row nearly all the time -- the
// setting is on by default -- so it reads as decoration and stops being seen at
// all. It is here instead, where it is asked for rather than read past, and the
// status bar still says it about the track being edited.
void TimelineWidget::refreshTooltip(int x, int y) {
    std::size_t row = 0;
    const Track* line = (x < kGutterWidth && rowAtY(y, &row)) ? trackAt(row) : nullptr;
    if (!line) {
        setToolTip(QString());
        return;
    }

    QString tip = QString::fromStdString(line->name);
    tip += QStringLiteral("\nDrag the name up or down to restack the tracks.");
    if (line->overwrite_drawings) {
        tip += QStringLiteral(
            "\n\nOverwrite drawings: a drawing put down here spends the rest\n"
            "of the hold rather than lengthening the shot.");
    }
    setToolTip(tip);
}

// One question, asked of everything that is true at once. The same shape as the
// canvas's own -- see CanvasWidget::pointingAt -- and here for the same reason:
// spread across the paths of a move handler, a cursor is decided by whichever
// path happened to run rather than by what is under the pointer.
Qt::CursorShape TimelineWidget::cursorAt(int x, int y) const {
    // A gesture under way outranks what is beneath the pointer.
    if (dragging_ || dragging_track_) return Qt::ClosedHandCursor;
    if (dragging_end_ || stretching_) return Qt::SplitHCursor;

    // The ruler. It scrubs, and it used to say so with a pointing hand -- which
    // is the first thing crossed on the way in from the canvas, so the timeline
    // read as a hand everywhere before you had touched anything. A hand here
    // means one thing now: this drawing can be picked up and moved.
    if (y < kRulerHeight) return isOnSceneEnd(x) ? Qt::SplitHCursor : Qt::ArrowCursor;

    std::size_t row = 0;
    if (!rowAtY(y, &row)) return Qt::ArrowCursor;

    // The name strip, which the row is restacked by. The hand still means one
    // thing -- this can be picked up and moved -- and now there are two things
    // it is true of: a drawing along its track, and a track up its stack.
    if (x < kGutterWidth) return Qt::OpenHandCursor;

    // The boundary between two runs, which stretches the exposure.
    if (isOnRunEdge(row, x, nullptr)) return Qt::SplitHCursor;

    // And only the numbered card can be picked up -- the same function the
    // press asks, which is what stops the pointer promising a drag that will
    // not happen or staying an arrow over one that will.
    return cardAt(row, x, nullptr) ? Qt::OpenHandCursor : Qt::ArrowCursor;
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    const int x = static_cast<int>(event->position().x());
    const int y = static_cast<int>(event->position().y());

    if (dragging_track_) {
        const int from = static_cast<int>(track_drag_row_);
        const int boundary = track_drop_row_;

        dragging_track_ = false;
        may_drag_track_ = false;
        track_drop_row_ = -1;
        refreshCursor(x, y);

        // The caret is a boundary counted in the rows as they stand; moveTrack
        // counts the destination with the row already taken out. Dropping a row
        // on either of its own edges is a move to where it already is, and both
        // land on `from` here rather than needing a case of their own.
        const int rows = static_cast<int>(rowCount());
        if (boundary >= 0 && rows > 1) {
            const int to = std::clamp(boundary > from ? boundary - 1 : boundary, 0, rows - 1);
            if (to != from) {
                doc_.moveTrack(static_cast<std::size_t>(from), static_cast<std::size_t>(to));
                refresh();
                Q_EMIT documentChanged();
            }
        }
        return;
    }
    may_drag_track_ = false;

    if (dragging_) {
        const Track* line = trackAt(drag_row_);
        const TrackId dropped_in = line ? line->id : kNoId;
        const bool overwrites = line && line->overwrite_drawings;
        const ImageId moved = drag_image_;
        const int drop = drop_index_;

        dragging_ = false;
        may_drag_ = false;
        drag_image_ = kNoId;
        drop_index_ = -1;
        drop_overwrites_ = false;
        // Whatever is under the pointer now, which after dropping a card on
        // itself is that card: the hand opens rather than becoming an arrow.
        refreshCursor(x, y);

        if (dropped_in != kNoId && drop >= 0) {
            // Two different questions, and neither answer can be derived from
            // the other: where between two drawings, or which frame to paint
            // over. See Document::moveDrawingOver.
            if (overwrites) {
                doc_.moveDrawingOver(dropped_in, moved, static_cast<std::size_t>(drop));
            } else {
                doc_.moveDrawing(dropped_in, moved, static_cast<std::size_t>(drop));
            }
            refresh();
            const Track* after = doc_.scene().findTrack(dropped_in);
            if (after) {
                const std::size_t at = after->firstSlotOf(moved);
                if (at < after->slots.size()) setCurrentSlot(at);
            }
            Q_EMIT documentChanged();
        }
        return;
    }
    may_drag_ = false;

    if (dragging_end_) {
        dragging_end_ = false;
        refreshCursor(x, y);
        doc_.endCommand();
        Q_EMIT documentChanged();
        return;
    }

    if (scrubbing_) {
        scrubbing_ = false;
        return;
    }
    if (!stretching_) return;
    stretching_ = false;
    refreshCursor(x, y);
    doc_.endCommand();
    Q_EMIT documentChanged();
}

void TimelineWidget::abandonGesture() {
    // One endCommand for each beginCommand, tested separately rather than
    // together: they cannot both be open today, and a counter is not the place
    // to rely on that.
    if (dragging_end_) {
        dragging_end_ = false;
        doc_.endCommand();
        Q_EMIT documentChanged();
    }
    if (stretching_) {
        stretching_ = false;
        doc_.endCommand();
        Q_EMIT documentChanged();
    }

    // Everything else is dropped where it stood. Nothing here writes to the
    // document: a drag that never arrived anywhere did not move a row.
    scrubbing_ = false;
    may_drag_ = false;
    dragging_ = false;
    drag_image_ = animage::kNoId;
    drop_index_ = -1;
    drop_overwrites_ = false;
    may_drag_track_ = false;
    dragging_track_ = false;
    track_drop_row_ = -1;
    update();
}

void TimelineWidget::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    abandonGesture();
}

void TimelineWidget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) abandonGesture();
}

// A double click on the name renames the track.
//
// Only on the name, and not anywhere else in the row: the rest of the row is
// frames, and a double click there is two presses that each mean something --
// selecting a frame -- rather than a gesture of its own. The gutter has nothing
// under it competing for the press, which is the same reason it is the drag
// handle.
//
// The press that opens this has already selected the track and armed a restack
// drag; the release before the double click disarms it, so nothing has to be
// undone here.
bool TimelineWidget::renameAt(int x, int y) {
    if (x >= kGutterWidth) return false;
    std::size_t row = 0;
    if (!rowAtY(y, &row)) return false;
    beginRenaming(row);
    return true;
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    renameAt(static_cast<int>(event->position().x()), static_cast<int>(event->position().y()));
}

QRect TimelineWidget::gutterRectFor(std::size_t row) const {
    return QRect(0, rowTop(row), kGutterWidth - 2, kRowHeight - 2);
}

void TimelineWidget::beginRenaming(std::size_t row) {
    // Opening one on another row keeps what was typed into the first rather
    // than dropping it, which is what re-seeding the editor would do. Before
    // the track is looked up, because renaming one is a document change.
    if (renaming_ != kNoId) finishRenaming(true);

    const Track* line = trackAt(row);
    if (!line) return;

    if (!rename_edit_) {
        rename_edit_ = new QLineEdit(this);
        rename_edit_->setFrame(true);
        // A hard cap rather than a complaint afterwards, and far past anything a
        // gutter this wide can show. See names::kTyped.
        rename_edit_->setMaxLength(names::kTyped);
        // Enter, and losing the editor to a click somewhere else, both mean the
        // name is what it now says. Escape is the only way to leave it alone,
        // and it arrives as a key rather than as a signal -- hence the filter.
        rename_edit_->installEventFilter(this);
        connect(rename_edit_, &QLineEdit::editingFinished, this,
                [this] { finishRenaming(true); });
    }

    renaming_ = line->id;
    rename_edit_->setGeometry(gutterRectFor(row));
    rename_edit_->setText(QString::fromStdString(line->name));
    rename_edit_->selectAll();
    rename_edit_->show();
    // Explicitly, because the timeline itself takes no keyboard focus: without
    // this the editor appears and the keys keep going to the canvas.
    rename_edit_->setFocus(Qt::OtherFocusReason);
}

void TimelineWidget::finishRenaming(bool keep) {
    if (!rename_edit_ || renaming_ == kNoId || finishing_rename_) return;

    // Hiding the editor takes the focus away from it, which emits
    // editingFinished a second time. This is the flag that stops the second one
    // being a second rename.
    finishing_rename_ = true;
    const TrackId renamed = renaming_;
    const QString typed = rename_edit_->text().trimmed();
    renaming_ = kNoId;
    rename_edit_->hide();
    finishing_rename_ = false;

    if (!keep) return;
    const Track* line = doc_.scene().findTrack(renamed);
    // An empty name leaves the track with no label on its row and no prefix on
    // its exported files, so the old one is kept rather than the emptiness
    // accepted -- the same answer the Rename track dialog gives.
    if (!line || typed.isEmpty() || typed.toStdString() == line->name) return;

    TrackProperties props = line->properties();
    props.name = typed.toStdString();
    doc_.updateTrack(renamed, props);
    refresh();
    Q_EMIT documentChanged();
}

bool TimelineWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == rename_edit_ && event->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            finishRenaming(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TimelineWidget::applyStretch(int pointer_x) {
    const Track* line = trackAt(stretch_row_);
    if (!line || stretch_run_start_ >= line->slots.size()) return;
    const TrackId id = line->id;

    const auto [first, last] = runAt(stretch_row_, stretch_run_start_);
    const int current_length = static_cast<int>(last - first) + 1;

    const int wanted_edge = pointer_x - kGutterWidth;
    const int wanted_length =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(wanted_edge) / kCellWidth)) -
                        static_cast<int>(first));
    if (wanted_length == current_length) return;

    if (wanted_length > current_length) {
        doc_.extendExposure(id, first, wanted_length - current_length);
    } else {
        for (int i = 0; i < current_length - wanted_length; ++i) {
            doc_.removeSlot(id, first);
        }
    }

    refresh();
    Q_EMIT documentChanged();
}
