// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>
#include <vector>

#include "document.h"

// The timeline: the scene's shared time axis, with one row per track.
//
// There is no row per layer, and that absence is the whole point of the model --
// timing belongs to the image, so every layer of a drawing is held for exactly
// as long as the drawing is. A row is a track, which is one stack of layers with
// its own time.
//
// One ruler and one playhead across all of them, because the timeline belongs to
// the scene and not to any track in it. Frame N means slot N in every track, and
// tracks are not obliged to be the same length: past its last slot a track's row
// is simply empty. What a track *should* show out there -- nothing, its last
// drawing, or a cycle -- is issue #20 and is deliberately not decided here; it
// is Track::imageAtSlot's to answer when it is.
//
// A drawing held over several frames is one block with a tail, not several
// identical cells, because that is what it is: the same ImageId repeated.
class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(animage::Document& document, QWidget* parent = nullptr);

    // Which track is being edited. Clicking a row is the other way this changes,
    // and either way it is reported by trackChanged.
    void setTrack(animage::TrackId track);
    animage::TrackId track() const { return track_; }

    void setCurrentSlot(std::size_t slot);
    std::size_t currentSlot() const { return current_slot_; }

    void refresh();

    QSize sizeHint() const override;

Q_SIGNALS:
    void currentSlotChanged(std::size_t slot);
    void trackChanged(animage::TrackId track);
    void documentChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    const animage::Track* trackAt(std::size_t row) const;
    const animage::Track* currentTrack() const;
    // Which row a track is in, and how many there are.
    std::size_t rowOf(animage::TrackId track) const;
    std::size_t rowCount() const { return doc_.scene().tracks.size(); }

    // The row under a y, clamped to a real row; false if y is in the ruler.
    bool rowAtY(int y, std::size_t* row) const;
    int rowTop(std::size_t row) const;

public:
    // Where a test should press to land on a cell or in the ruler. Asked of the
    // widget rather than recomputed, for the same reason the transform box's
    // handles are: a test that lays the strip out for itself agrees happily
    // with a card drawn where no hand could reach it.
    QPoint cellCentreForTesting(std::size_t row, std::size_t slot) const;
    QPoint rulerPointForTesting(std::size_t slot) const;

private:

    std::size_t slotAt(int x) const;
    // Where the shot ends, in x. The boundary is grabbed in the ruler and
    // nowhere else: the ruler is the scene's own time, which is what the shot's
    // length belongs to, and it keeps the handle clear of the run edges in the
    // rows below, which are a track's business and sometimes at the same x.
    int sceneEndX() const;
    bool isOnSceneEnd(int x) const;
    // First and last slot of the run of identical ImageIds containing `slot`,
    // in the given row's track.
    std::pair<std::size_t, std::size_t> runAt(std::size_t row, std::size_t slot) const;
    bool isOnRunEdge(std::size_t row, int x, std::size_t* run_start) const;
    // The numbered card under x, if there is one -- the only thing in a row
    // that can be picked up. Not slotAt, which clamps to the last slot and so
    // answers "a card" for the whole width of the widget past the strip.
    bool cardAt(std::size_t row, int x, std::size_t* slot) const;
    std::vector<int> drawingNumbers(const animage::Track& track) const;

    // What a press here would do, and the only thing that decides the cursor.
    //
    // It was decided in five places along mouseMoveEvent, with a flag to
    // remember whether one of them had already fired -- and the ruler, which is
    // the first thing the pointer crosses coming down from the canvas, claimed
    // a pointing hand. So the timeline turned into a hand the moment you
    // entered it, and the hand that means "this drawing can be picked up" was
    // the same hand. Reported, and it had been there from the beginning.
    Qt::CursorShape cursorAt(int x, int y) const;
    void refreshCursor(int x, int y) { setCursor(cursorAt(x, y)); }

    // What a drawing's colour layers are doing, in what the card can show.
    struct ColourState {
        bool any = false;      // a colour layer has marks to show here
        bool carried = false;  // ...and some of them were made on another drawing
    };
    ColourState colourStateFor(const animage::Track& track, animage::ImageId image) const;
    void applyStretch(int pointer_x);
    int dropIndexFor(int pointer_x) const;

    animage::Document& doc_;
    animage::TrackId track_ = animage::kNoId;
    std::size_t current_slot_ = 0;

    bool stretching_ = false;
    std::size_t stretch_row_ = 0;
    std::size_t stretch_run_start_ = 0;

    // Dragging in the ruler band scrubs. Keeping it in its own strip is what
    // stops a scrub from turning into an exposure change by accident.
    bool scrubbing_ = false;
    // Dragging the end-of-shot boundary in the ruler. Doing so fixes the scene
    // length: you are saying where the shot ends, which is the whole of what the
    // setting means.
    bool dragging_end_ = false;

    // Dragging the body of a numbered card reorders the drawing. Only the
    // numbered card can start one: a held frame is not a thing in its own
    // right, it is the same drawing still showing, so there is nothing there
    // to pick up.
    //
    // A drag stays in its own row. Dropping a drawing into another track would
    // have to say what happens to the cels of layers that track does not have,
    // and that is a question this widget has no business answering.
    bool may_drag_ = false;      // pressed on a card, not yet moved far enough
    bool dragging_ = false;
    std::size_t drag_row_ = 0;
    animage::ImageId drag_image_ = animage::kNoId;
    int press_x_ = 0;
    int drop_index_ = -1;        // insertion point, in slots-without-the-drawing
    // Where an overwriting drop would land: the run of slots the drawing would
    // take over. A track that overwrites is not inserting between two frames, it
    // is painting over some of them, so the caret is a range and not a line.
    std::size_t drop_first_ = 0;
    std::size_t drop_last_ = 0;
    bool drop_overwrites_ = false;
};
