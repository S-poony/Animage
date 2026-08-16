// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>
#include <vector>

#include "document.h"
#include "double_tap.h"

class QLineEdit;

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
// is simply empty. What a track *shows* out there -- nothing, its last drawing,
// or a cycle -- is Track::end, and this widget deliberately draws none of it:
// the row stops at the last drawing and what happens beyond is said once, in the
// status bar, rather than repeated along the row. See issue #22.
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
    // A name is being typed into, or has stopped being. The window turns the
    // keyboard shortcuts off while it is true: Return is Play, and Return is
    // also how a rename is finished. See shortcuts::Mode::Typing.
    void renamingChanged(bool renaming);

public:
    // Rename the track in `row` in place, as a double click on its name does.
    // Public so a test can open the editor without a double click: what is worth
    // pinning is what the editor does with what is typed into it, and Qt's own
    // double click is not the part that would be wrong.
    void renameTrackForTesting(std::size_t row) { beginRenaming(row); }
    QLineEdit* renameEditorForTesting() const { return rename_edit_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    // The release is not coming: the keyboard has gone elsewhere, or the window
    // has stopped being active. See abandonGesture.
    void focusOutEvent(QFocusEvent* event) override;
    void changeEvent(QEvent* event) override;
    // Watches the rename editor for Escape, which QLineEdit has no signal for.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Close whatever gesture is open, without completing it.
    //
    // Two of them hold a Document command open from press to release, and
    // Document::beginCommand counts depth: one left open means nothing reaches
    // the undo stack again for the rest of the session, silently. The same trap
    // as CanvasWidget::abandonGesture, on the other widget that opens commands.
    //
    // A drag is dropped rather than completed. Letting go of a row somewhere
    // this widget cannot see is not a drop, so the row stays where it was
    // instead of landing wherever the pointer happened to be.
    void abandonGesture();

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
    // The name strip of a row: what selects the track, what a restack is
    // dragged by, and what a double click renames.
    QPoint gutterPointForTesting(std::size_t row) const;

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

    // What the row under the pointer is, asked for rather than written on it.
    // The gutter is a hundred pixels wide and a track has a name in it; a second
    // line of text there was clutter on every row of every scene.
    void refreshTooltip(int x, int y);

    // The boundary between rows a drop at `y` would land on, in [0, rowCount()].
    int trackDropRowFor(int y) const;

    // Renaming a track in place: a line edit laid over the name in the gutter.
    //
    // A widget on a row disables that row's own hit testing -- the trap the
    // layer panel already records -- and this is the one shape that does not
    // mind, because it exists only while it is being typed into and the row
    // underneath is exactly what it has taken over.
    // Renames whatever is at (x, y), if that is a name at all. True if it was:
    // the mouse's double click and the pen's second tap both come through here.
    bool renameAt(int x, int y);
    void beginRenaming(std::size_t row);
    // `keep` false is Escape: the editor goes away and the name does not move.
    void finishRenaming(bool keep);
    // Where the editor sits, which is the name's own rectangle in the gutter.
    QRect gutterRectFor(std::size_t row) const;

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

    // Dragging a row by its name restacks the track: index 0 is the top of the
    // timeline and the top group of the composite, so this is how a background
    // is put behind a character.
    //
    // A drag of its own rather than the same one the cards use, because the two
    // answer different questions -- which frames, against which rows -- and
    // there is nowhere they can be confused: one starts in the gutter and the
    // other cannot.
    bool may_drag_track_ = false;
    bool dragging_track_ = false;
    std::size_t track_drag_row_ = 0;
    int press_y_ = 0;
    int track_drop_row_ = -1;  // the boundary it would land on, in rows

    // The rename editor, made once and reused. Which track it is editing is
    // held as an id and not as a row, so a restack while it is open cannot
    // rename the track that took the row's place.
    QLineEdit* rename_edit_ = nullptr;
    animage::TrackId renaming_ = animage::kNoId;
    // A pen's double tap, which Qt does not turn into a double click for us.
    DoubleTap taps_;
    // editingFinished also fires when the editor loses focus, which is what
    // hiding it does. Without this, applying a name applies it twice.
    bool finishing_rename_ = false;
};
