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
constexpr int kStripHeight = 64;
constexpr int kEdgeGrab = 5;
constexpr int kDragThreshold = 5;

// Taken from the widget's palette rather than hardcoded, so the timeline
// belongs to the same application as everything above it -- and follows a dark
// theme too, if the system asks for one.
struct Palette {
    QColor background, ruler, cell, cell_held, outline, tick, text, current, current_text;
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
    return p;
}

}  // namespace

TimelineWidget::TimelineWidget(Document& document, QWidget* parent)
    : QWidget(parent), doc_(document) {
    setMinimumHeight(kStripHeight);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
}

const Timeline* TimelineWidget::timeline() const { return doc_.scene().findTimeline(timeline_); }

void TimelineWidget::setTimeline(TimelineId timeline) {
    timeline_ = timeline;
    refresh();
}

void TimelineWidget::setCurrentSlot(std::size_t slot) {
    const Timeline* line = timeline();
    if (!line || line->slots.empty()) return;
    const std::size_t clamped = std::min(slot, line->slots.size() - 1);
    if (clamped == current_slot_) return;
    current_slot_ = clamped;
    update();
    Q_EMIT currentSlotChanged(current_slot_);
}

void TimelineWidget::refresh() {
    const Timeline* line = timeline();
    if (line && !line->slots.empty() && current_slot_ >= line->slots.size()) {
        current_slot_ = line->slots.size() - 1;
        Q_EMIT currentSlotChanged(current_slot_);
    }
    updateGeometry();
    setMinimumWidth(static_cast<int>((line ? line->slots.size() : 0) + 2) * kCellWidth);
    update();
}

QSize TimelineWidget::sizeHint() const {
    const Timeline* line = timeline();
    const int frames = static_cast<int>(line ? line->slots.size() : 0);
    return {(frames + 2) * kCellWidth, kStripHeight};
}

std::size_t TimelineWidget::slotAt(int x) const {
    const Timeline* line = timeline();
    if (!line || line->slots.empty()) return 0;
    const int index = std::clamp(x / kCellWidth, 0, static_cast<int>(line->slots.size()) - 1);
    return static_cast<std::size_t>(index);
}

std::pair<std::size_t, std::size_t> TimelineWidget::runAt(std::size_t slot) const {
    const Timeline* line = timeline();
    if (!line) return {slot, slot};
    return line->runBounds(slot);
}

bool TimelineWidget::isOnRunEdge(int x, std::size_t* run_start) const {
    const Timeline* line = timeline();
    if (!line || line->slots.empty()) return false;

    const std::size_t slot = slotAt(x);
    const auto [first, last] = runAt(slot);
    const int edge = static_cast<int>(last + 1) * kCellWidth;
    if (std::abs(x - edge) > kEdgeGrab) return false;
    if (run_start) *run_start = first;
    return true;
}

// The number the drawing was born with, read straight off the Image. Deriving
// it from position instead meant a drawing renumbered itself the moment it was
// dragged, which is precisely when you need to know which one you are holding.
std::vector<int> TimelineWidget::drawingNumbers() const {
    const Timeline* line = timeline();
    std::vector<int> numbers;
    if (!line) return numbers;

    numbers.reserve(line->slots.size());
    for (ImageId id : line->slots) {
        const Image* image = line->findImage(id);
        numbers.push_back(image ? image->number : 0);
    }
    return numbers;
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    const Palette colours = paletteFor(*this);

    QPainter painter(this);
    painter.fillRect(rect(), colours.background);
    painter.fillRect(QRect(0, 0, width(), kRulerHeight), colours.ruler);

    const Timeline* line = timeline();
    if (!line) return;

    const std::vector<int> numbers = drawingNumbers();
    QFont font = painter.font();
    font.setPointSizeF(8.5);
    painter.setFont(font);
    const QFontMetrics metrics(font);

    for (std::size_t i = 0; i < line->slots.size(); ++i) {
        const int x = static_cast<int>(i) * kCellWidth;
        const bool held = i > 0 && line->slots[i - 1] == line->slots[i];
        const QRect cell(x, kRulerHeight + 2, kCellWidth - 1, kStripHeight - kRulerHeight - 8);

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
        }

        // Frame ruler every five, as on an exposure sheet.
        if ((i % 5) == 0) {
            painter.setPen(colours.tick);
            painter.drawText(QRect(x, 0, kCellWidth, kRulerHeight), Qt::AlignCenter,
                             QString::number(i + 1));
        }
    }

    if (dragging_ && drop_index_ >= 0) {
        // Count past the drawing being carried: it is not in the timeline it is
        // about to be dropped into.
        int seen = 0;
        int at = static_cast<int>(line->slots.size());
        for (std::size_t i = 0; i < line->slots.size(); ++i) {
            if (line->slots[i] == drag_image_) continue;
            if (seen == drop_index_) {
                at = static_cast<int>(i);
                break;
            }
            ++seen;
        }
        painter.setPen(QPen(colours.current, 3));
        painter.drawLine(at * kCellWidth, kRulerHeight, at * kCellWidth, kStripHeight);
    }

    if (current_slot_ < line->slots.size()) {
        const int x = static_cast<int>(current_slot_) * kCellWidth;
        painter.setPen(QPen(colours.current, 2));
        painter.drawRect(QRect(x, kRulerHeight + 1, kCellWidth - 1,
                              kStripHeight - kRulerHeight - 6));
        // Playhead in the ruler, so the scrub band shows where you are.
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

    std::size_t run_start = 0;
    if (isOnRunEdge(x, &run_start)) {
        stretching_ = true;
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
    const Timeline* line = timeline();
    if (line && slot < line->slots.size() && runAt(slot).first == slot) {
        may_drag_ = true;
        drag_image_ = line->slots[slot];
        press_x_ = x;
    }
}

// Where the drawing would land, counted in the timeline as it will be once the
// drawing has been lifted out of it.
int TimelineWidget::dropIndexFor(int pointer_x) const {
    const Timeline* line = timeline();
    if (!line || drag_image_ == kNoId) return 0;

    const int boundary = (pointer_x + kCellWidth / 2) / kCellWidth;
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
        const int drop = dropIndexFor(x);
        if (drop != drop_index_) {
            drop_index_ = drop;
            update();
        }
        return;
    }

    if (!stretching_ && y < kRulerHeight) {
        if (hovering_edge_) {
            hovering_edge_ = false;
        }
        setCursor(Qt::PointingHandCursor);
        return;
    }

    if (stretching_) {
        applyStretch(x);
        return;
    }

    const bool on_edge = isOnRunEdge(x, nullptr);
    if (on_edge) {
        if (!hovering_edge_) {
            hovering_edge_ = true;
            setCursor(Qt::SplitHCursor);
        }
        return;
    }
    hovering_edge_ = false;

    const Timeline* line = timeline();
    const std::size_t slot = slotAt(x);
    const bool on_card = line && slot < line->slots.size() && runAt(slot).first == slot;
    setCursor(on_card ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*) {
    if (dragging_) {
        const ImageId moved = drag_image_;
        const int drop = drop_index_;
        dragging_ = false;
        may_drag_ = false;
        drag_image_ = kNoId;
        drop_index_ = -1;
        setCursor(Qt::ArrowCursor);

        if (drop >= 0) {
            doc_.moveDrawing(timeline_, moved, static_cast<std::size_t>(drop));
            refresh();
            const Timeline* line = timeline();
            if (line) {
                auto it = std::find(line->slots.begin(), line->slots.end(), moved);
                if (it != line->slots.end()) {
                    setCurrentSlot(
                        static_cast<std::size_t>(std::distance(line->slots.begin(), it)));
                }
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
    const Timeline* line = timeline();
    if (!line || stretch_run_start_ >= line->slots.size()) return;

    const auto [first, last] = runAt(stretch_run_start_);
    const int current_length = static_cast<int>(last - first) + 1;

    const int wanted_edge = pointer_x;
    const int wanted_length =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(wanted_edge) / kCellWidth)) -
                        static_cast<int>(first));
    if (wanted_length == current_length) return;

    if (wanted_length > current_length) {
        doc_.extendExposure(timeline_, first, wanted_length - current_length);
    } else {
        for (int i = 0; i < current_length - wanted_length; ++i) {
            doc_.removeSlot(timeline_, first);
        }
    }

    refresh();
    Q_EMIT documentChanged();
}
