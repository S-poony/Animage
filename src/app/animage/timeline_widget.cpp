// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline_widget.h"

#include <QFontMetrics>
#include <QPalette>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

using namespace animage;

namespace {

constexpr int kCellWidth = 26;
constexpr int kRulerHeight = 18;
constexpr int kRowHeight = 46;
constexpr int kEdgeGrab = 5;
constexpr int kDragThreshold = 5;

// The strip of track names down the left. It is what makes a row say which
// track it is without a control on it -- see the handover on why a widget on a
// row is a trap -- and it is where the current track is shown as current.
constexpr int kGutterWidth = 104;

// Taken from the widget's palette rather than hardcoded, so the timeline
// belongs to the same application as everything above it -- and follows a dark
// theme too, if the system asks for one.
struct Palette {
    QColor background, ruler, cell, cell_held, outline, tick, text, current, current_text;
    QColor carried, gutter, gutter_current;
};

Palette paletteFor(const QWidget& widget) {
    const QPalette& source = widget.palette();
    const QColor window = source.color(QPalette::Window);
    const QColor text = source.color(QPalette::WindowText);
    const bool dark = window.lightness() < 128;

    Palette p;
    p.background = dark ? window.lighter(115) : window.darker(108);
    p.ruler = dark ? window.lighter(135) : window.darker(118);
    p.cell = source.color(QPalette::Base);
    p.cell_held = dark ? p.cell.lighter(115) : p.cell.darker(106);
    p.outline = dark ? window.lighter(180) : window.darker(140);
    p.tick = text;
    p.text = text;
    p.current = source.color(QPalette::Highlight);
    p.current_text = source.color(QPalette::HighlightedText);
    p.gutter = dark ? window.lighter(125) : window.darker(112);
    p.gutter_current = source.color(QPalette::Highlight);
    // Not from the palette: this has to mean the same thing in every theme, and
    // "carried" is not a role a system palette has.
    p.carried = QColor(0x5b, 0x9c, 0xd6);
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
    const std::size_t frames = doc_.scene().frameCount();
    if (frames == 0) return;
    const std::size_t clamped = std::min(slot, frames - 1);
    if (clamped == current_slot_) return;
    current_slot_ = clamped;
    update();
    Q_EMIT currentSlotChanged(current_slot_);
}

void TimelineWidget::refresh() {
    const std::size_t frames = doc_.scene().frameCount();
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
    const int frames = static_cast<int>(doc_.scene().frameCount());
    const int rows = static_cast<int>(std::max<std::size_t>(rowCount(), 1));
    return {kGutterWidth + (frames + 2) * kCellWidth, kRulerHeight + rows * kRowHeight};
}

std::size_t TimelineWidget::slotAt(int x) const {
    const std::size_t frames = doc_.scene().frameCount();
    if (frames == 0) return 0;
    const int index = std::clamp((x - kGutterWidth) / kCellWidth, 0, static_cast<int>(frames) - 1);
    return static_cast<std::size_t>(index);
}

std::pair<std::size_t, std::size_t> TimelineWidget::runAt(std::size_t row,
                                                          std::size_t slot) const {
    const Track* line = trackAt(row);
    if (!line) return {slot, slot};
    return line->runBounds(slot);
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

    const std::size_t frames = doc_.scene().frameCount();

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
        if (line.overwrite_drawings) {
            // A drawing put down here spends the hold rather than lengthening the
            // shot, which changes what every button in the panel above does to
            // this row. It has to be visible from the row itself.
            painter.drawText(gutter.adjusted(6, 0, -4, -4),
                             Qt::AlignBottom | Qt::AlignLeft, QStringLiteral("overwrite"));
        }

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

    if (current_slot_ < frames) {
        const int x = kGutterWidth + static_cast<int>(current_slot_) * kCellWidth;
        painter.fillRect(QRect(x, 0, kCellWidth - 1, kRulerHeight), colours.current);
        painter.setPen(colours.current_text);
        painter.drawText(QRect(x, 0, kCellWidth, kRulerHeight), Qt::AlignCenter,
                         QString::number(current_slot_ + 1));
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int x = static_cast<int>(event->position().x());
    const int y = static_cast<int>(event->position().y());

    // The ruler is a scrub band and nothing else. Exposure edges live below it,
    // so dragging along time can never resize a hold by accident.
    if (y < kRulerHeight) {
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

    // The name strip selects and nothing else: there is no frame under it.
    if (x < kGutterWidth) return;

    std::size_t run_start = 0;
    if (isOnRunEdge(row, x, &run_start)) {
        stretching_ = true;
        stretch_row_ = row;
        stretch_run_start_ = run_start;
        // One command for the whole drag: nested commands collapse, so every
        // slot change during the drag undoes in a single step.
        doc_.beginCommand("Change exposure");
        return;
    }

    const std::size_t slot = slotAt(x);
    setCurrentSlot(slot);

    // Only the numbered card starts a move. Pressing a held frame selects it
    // and nothing more -- there is no separate object there to drag.
    if (slot < line->slots.size() && runAt(row, slot).first == slot) {
        may_drag_ = true;
        drag_row_ = row;
        drag_image_ = line->slots[slot];
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

    if (scrubbing_) {
        setCurrentSlot(slotAt(x));
        return;
    }

    if (may_drag_ && !dragging_ && std::abs(x - press_x_) >= kDragThreshold) {
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
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

    if (!stretching_ && y < kRulerHeight) {
        hovering_edge_ = false;
        setCursor(Qt::PointingHandCursor);
        return;
    }

    if (stretching_) {
        applyStretch(x);
        return;
    }

    std::size_t row = 0;
    if (!rowAtY(y, &row) || x < kGutterWidth) {
        hovering_edge_ = false;
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (isOnRunEdge(row, x, nullptr)) {
        if (!hovering_edge_) {
            hovering_edge_ = true;
            setCursor(Qt::SplitHCursor);
        }
        return;
    }
    hovering_edge_ = false;

    const Track* line = trackAt(row);
    const std::size_t slot = slotAt(x);
    const bool on_card = line && slot < line->slots.size() && runAt(row, slot).first == slot;
    setCursor(on_card ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*) {
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
        setCursor(Qt::ArrowCursor);

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

    if (scrubbing_) {
        scrubbing_ = false;
        return;
    }
    if (!stretching_) return;
    stretching_ = false;
    doc_.endCommand();
    Q_EMIT documentChanged();
}

void TimelineWidget::leaveEvent(QEvent*) {
    if (hovering_edge_) {
        hovering_edge_ = false;
        setCursor(Qt::ArrowCursor);
    }
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
