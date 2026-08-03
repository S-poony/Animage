// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>
#include <vector>

#include "document.h"

// The timeline in time: one strip of slots. There is no row per layer, and
// that absence is the whole point of the model -- timing belongs to the image,
// so every layer of a drawing is held for exactly as long as the drawing is.
//
// A drawing held over several frames is one block with a tail, not several
// identical cells, because that is what it is: the same ImageId repeated.
class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(animage::Document& document, QWidget* parent = nullptr);

    void setTimeline(animage::TimelineId timeline);
    void setCurrentSlot(std::size_t slot);
    std::size_t currentSlot() const { return current_slot_; }

    void refresh();

    QSize sizeHint() const override;

Q_SIGNALS:
    void currentSlotChanged(std::size_t slot);
    void documentChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    const animage::Timeline* timeline() const;

    std::size_t slotAt(int x) const;
    // First and last slot of the run of identical ImageIds containing `slot`.
    std::pair<std::size_t, std::size_t> runAt(std::size_t slot) const;
    bool isOnRunEdge(int x, std::size_t* run_start) const;
    std::vector<int> drawingNumbers() const;
    void applyStretch(int pointer_x);
    int dropIndexFor(int pointer_x) const;

    animage::Document& doc_;
    animage::TimelineId timeline_ = animage::kNoId;
    std::size_t current_slot_ = 0;

    bool stretching_ = false;
    std::size_t stretch_run_start_ = 0;
    bool hovering_edge_ = false;

    // Dragging in the ruler band scrubs. Keeping it in its own strip is what
    // stops a scrub from turning into an exposure change by accident.
    bool scrubbing_ = false;

    // Dragging the body of a numbered card reorders the drawing. Only the
    // numbered card can start one: a held frame is not a thing in its own
    // right, it is the same drawing still showing, so there is nothing there
    // to pick up.
    bool may_drag_ = false;      // pressed on a card, not yet moved far enough
    bool dragging_ = false;
    animage::ImageId drag_image_ = animage::kNoId;
    int press_x_ = 0;
    int drop_index_ = -1;        // insertion point, in slots-without-the-drawing
};
