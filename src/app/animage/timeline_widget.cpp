// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline_widget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

using namespace animage;

namespace {

constexpr int kCellWidth = 26;
constexpr int kStripHeight = 62;
constexpr int kEdgeGrab = 5;

const QColor kBackground(38, 38, 42);
const QColor kCell(70, 72, 80);
const QColor kCellHeld(56, 58, 64);
const QColor kCurrent(235, 180, 60);
const QColor kText(230, 230, 230);

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
    if (!line || slot >= line->slots.size()) return {slot, slot};

    const ImageId id = line->slots[slot];
    std::size_t first = slot;
    while (first > 0 && line->slots[first - 1] == id) --first;
    std::size_t last = slot;
    while (last + 1 < line->slots.size() && line->slots[last + 1] == id) ++last;
    return {first, last};
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

// The drawing number an animator would write on the paper: the count of
// distinct drawings so far, not the frame index.
std::vector<int> TimelineWidget::drawingNumbers() const {
    const Timeline* line = timeline();
    std::vector<int> numbers;
    if (!line) return numbers;

    numbers.reserve(line->slots.size());
    std::vector<ImageId> seen;
    for (ImageId id : line->slots) {
        auto it = std::find(seen.begin(), seen.end(), id);
        if (it == seen.end()) {
            seen.push_back(id);
            numbers.push_back(static_cast<int>(seen.size()));
        } else {
            numbers.push_back(static_cast<int>(it - seen.begin()) + 1);
        }
    }
    return numbers;
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kBackground);

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
        const QRect cell(x, 16, kCellWidth - 1, kStripHeight - 24);

        painter.fillRect(cell, held ? kCellHeld : kCell);

        if (held) {
            // A held drawing is one block with a tail, not a repeated cell.
            painter.setPen(QPen(QColor(120, 122, 132), 1));
            painter.drawLine(cell.center().x(), cell.top() + 3, cell.center().x(),
                             cell.bottom() - 3);
        } else {
            painter.setPen(QPen(QColor(150, 152, 162), 1));
            painter.drawRect(cell.adjusted(0, 0, -1, -1));
            painter.setPen(kText);
            const QString label = QString::number(numbers[i]);
            painter.drawText(cell, Qt::AlignCenter, label);
        }

        // Frame ruler every five, as on an exposure sheet.
        if ((i % 5) == 0) {
            painter.setPen(QColor(150, 150, 160));
            painter.drawText(QRect(x, 0, kCellWidth, 14), Qt::AlignCenter,
                             QString::number(i + 1));
        }
    }

    if (current_slot_ < line->slots.size()) {
        const int x = static_cast<int>(current_slot_) * kCellWidth;
        painter.setPen(QPen(kCurrent, 2));
        painter.drawRect(QRect(x, 15, kCellWidth - 1, kStripHeight - 22));
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int x = static_cast<int>(event->position().x());

    std::size_t run_start = 0;
    if (isOnRunEdge(x, &run_start)) {
        stretching_ = true;
        stretch_run_start_ = run_start;
        // One command for the whole drag: nested commands collapse, so every
        // slot change during the drag undoes in a single step.
        doc_.beginCommand("Change exposure");
        return;
    }

    setCurrentSlot(slotAt(x));
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    const int x = static_cast<int>(event->position().x());

    if (stretching_) {
        applyStretch(x);
        return;
    }

    const bool on_edge = isOnRunEdge(x, nullptr);
    if (on_edge != hovering_edge_) {
        hovering_edge_ = on_edge;
        setCursor(on_edge ? Qt::SplitHCursor : Qt::ArrowCursor);
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*) {
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
