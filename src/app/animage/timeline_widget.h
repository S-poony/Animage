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

    // Which track is being edited. Clicking a *drawing* row is the other way
    // this changes, and either way it is reported by trackChanged.
    //
    // **This is one of two selections and it is the narrow one.** Five things
    // read it -- the canvas, the layer panel, the Track menu, the drawing
    // buttons and the status bar -- and every one of them wants a real `Track`.
    // Clicking an audio row leaves it exactly where it was, which is why there
    // is a second selection below rather than a guard in five places. See
    // docs/importing.md, "the two selections".
    void setTrack(animage::TrackId track);
    animage::TrackId track() const { return track_; }

    // Which row is highlighted, when that row is a soundtrack rather than a
    // track. `kNoId` means a drawing row is highlighted and `track()` says
    // which.
    //
    // The wide selection: it may name something that is not a `Track` at all,
    // so nothing that needs a `Track` may read it. What it drives today is the
    // painting; what it will drive is the properties panel.
    animage::TrackId highlightedAudio() const { return audio_row_; }

    // Put the highlight back on the drawing row. What calls it is a stroke
    // landing on the canvas: drawing says you are done with the sound, and a
    // row that stays lit while every stroke lands elsewhere is a row lying
    // about what is being worked on.
    void clearAudioHighlight() { setAudioHighlight(animage::kNoId); }

    // How far down the widget the ruler should be drawn, which is how far the
    // scroll area has scrolled it up.
    //
    // **The ruler is pinned rather than moved out of the scrolled widget**, and
    // the two come to the same picture. Taking it out would reserve its 18
    // pixels above the viewport; leaving it in spends the same 18 covering the
    // top of it. Either way a row at the top of the view is cut off by the same
    // amount and comes out from under by scrolling -- so the version that keeps
    // the end-of-shot line one `drawLine` through both bands, and keeps the
    // sideways scrolling free, is the one worth having.
    //
    // What it is for: soundtracks sit under every drawing row, so a scene with
    // a few tracks is one you scroll down to reach the sound -- and the ruler
    // is where scrubbing happens, which is the only way to hear it. A ruler
    // that scrolled away took the scrub band off the screen exactly when it was
    // wanted.
    void setRulerTop(int y);

    // The same for the gutter, sideways: how far into the widget the column of
    // names should be drawn, which is how far the scroll area has scrolled it
    // left.
    //
    // **The ruler's argument, turned ninety degrees, and it holds unchanged.**
    // Lifting the names into a strip of their own beside the viewport would
    // reserve their hundred pixels; leaving them in spends the same hundred
    // covering the left of it. Either way the cell at the left edge is hidden
    // by the same amount and comes out from under by scrolling -- so the
    // version that keeps one widget, one paint and one set of coordinates is
    // the one worth having. See setRulerTop, and docs/handover.md on why the
    // split that looked better was the same picture.
    //
    // What it is for: a shot is long sideways where it is short downwards, so
    // scrolling right is the ordinary thing to do in a timeline -- and doing it
    // took the names off the screen, leaving rows of identical cells with
    // nothing saying which track was which. The row you are pointed at is a
    // colour in the gutter, so it went too.
    void setGutterLeft(int x);

    void setCurrentSlot(std::size_t slot);
    std::size_t currentSlot() const { return current_slot_; }

    void refresh();

    // Give up an open rename without keeping what was typed, as Escape does.
    //
    // For a window on its way out: an editor that is still open when the
    // window is destroyed reports its rename during the destruction, which is
    // far too late for anything to answer it. See MainWindow's destructor.
    void abandonRename() { finishRenaming(false); }

    QSize sizeHint() const override;

Q_SIGNALS:
    void currentSlotChanged(std::size_t slot);

    // The playhead was dragged or clicked in the ruler, and this is where it
    // landed.
    //
    // **Narrower than currentSlotChanged on purpose.** That one fires for every
    // way the playhead moves -- the arrow keys, playback, a card being clicked,
    // a track being deleted underneath it -- and what wants this one is scrub
    // audio, which must make a noise when somebody is reading a track and stay
    // out of the way when somebody is drawing. Stepping through frames with the
    // keyboard while animating is not a request to hear anything.
    void scrubbed(std::size_t slot);

    // The highlight moved between a drawing row and a soundtrack row.
    //
    // What listens is the Track menu, which acts on the row you are pointed at
    // -- so it has to be told when that stops being a track. `trackChanged`
    // cannot carry it: clicking a soundtrack row deliberately leaves the
    // current track exactly where it was.
    void highlightChanged();
    void trackChanged(animage::TrackId track);
    void documentChanged();

public:
    // Rename the track in `row` in place, as a double click on its name does.
    // Public so a test can open the editor without a double click: what is worth
    // pinning is what the editor does with what is typed into it, and Qt's own
    // double click is not the part that would be wrong.
    void renameTrackForTesting(std::size_t row) { beginRenaming(row); }
    QLineEdit* renameEditorForTesting() const { return rename_edit_; }

protected:
    // A soundtrack row says which file it came from, and that is what makes
    // renaming one safe: the label on the row and the file in the project's
    // `audio/` folder are two different things, and losing sight of the second
    // was the one real objection to letting the first be changed.
    bool event(QEvent* event) override;

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

    // **Rows are drawing tracks first and soundtracks under all of them.** A
    // soundtrack has no compositing order, so letting one be dragged into the
    // middle of the stack would imply a depth it does not have -- and putting
    // them last means every row index a drawing track has is the index it
    // always had.
    //
    // `trackAt` answers null for a soundtrack's row, which is not a special
    // case bolted on: it already answered null for a row past the end, so every
    // call site that was written to survive that survives this too. The one
    // that dereferenced without asking is the paint loop, and it now asks.
    const animage::Track* trackAt(std::size_t row) const;
    const animage::AudioTrack* audioAt(std::size_t row) const;
    bool isAudioRow(std::size_t row) const { return audioAt(row) != nullptr; }

    const animage::Track* currentTrack() const;
    // Which row a track is in, and how many there are.
    std::size_t rowOf(animage::TrackId track) const;
    std::size_t drawingRowCount() const { return doc_.scene().tracks.size(); }
    std::size_t rowCount() const {
        return drawingRowCount() + doc_.scene().audio_tracks.size();
    }

    // Whether a y is in the pinned ruler band, wherever the scroll has put it.
    // Every gesture asks this before it asks which row it is on, because the
    // band is on top of whatever is under it.
    //
    // Not inline, so that the band's height stays written down once, beside the
    // rest of the geometry in the .cpp.
    bool inRuler(int y) const;

    // Whether an x is in the pinned gutter, wherever the scroll has put it.
    // Asked wherever `x < kGutterWidth` used to be, and for inRuler's reason:
    // the column is on top of whatever is under it, so where it *is* and where
    // the cells are are two different questions.
    //
    // `slotAt` is deliberately not one of the callers. Cells are where they
    // always were and what the gutter does is cover some -- exactly as
    // `rowAtY` was left alone when the ruler was pinned.
    bool inGutter(int x) const;

    // The row under a y, clamped to a real row; false if y is above the rows.
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
    // The whole rectangle, for the one assertion the centre cannot make: that
    // where the column is drawn and where the rename editor is put are the
    // same place once the strip has been scrolled. See setGutterLeft.
    QRect gutterRectForTesting(std::size_t row) const { return gutterRectFor(row); }

    // Where a soundtrack's block is drawn, in slots, fractional.
    //
    // Exposed for the same reason the points above are: a test that works the
    // extent out for itself agrees happily with a block drawn somewhere else.
    // What this pins is that cropping the front does not move the back -- the
    // one property a person can see, and the one that was wrong when the length
    // was rounded to whole frames while the start was not.
    std::pair<double, double> audioExtentForTesting(std::size_t row) const {
        const animage::AudioTrack* sound = audioAt(row);
        return sound ? audioExtent(*sound) : std::pair<double, double>{0.0, 0.0};
    }

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

    // Move the playhead as a scrub does, and say so.
    //
    // `always` is for the press that begins one: clicking the ruler on the
    // frame the playhead is already standing on is a request to hear that
    // frame, and a slot that did not change is not a reason to stay quiet.
    // While the pointer is moving, only a change is worth a sound -- twenty
    // bursts of the same frame is not scrubbing, it is a buzz.
    void scrubTo(int x, bool always);

    // The one place `audio_row_` is written, so that nothing can move the
    // highlight without the Track menu hearing about it.
    void setAudioHighlight(animage::TrackId id);

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

    // --- a soundtrack's row -------------------------------------------------
    //
    // **The bar is painted here and hit-tested here, and is not a QSlider.**
    // A widget placed on a row disables that row's own hit testing -- the trap
    // this file already records for the rename editor and the layer panel
    // already paid for. A real slider would work, and everything else in that
    // row would stop responding.
    //
    // The axis is free, which is worth knowing rather than discovering: in a
    // drawing row a horizontal drag moves a drawing and a vertical drag in the
    // gutter restacks the track. A soundtrack row has no cards to pick up, so a
    // vertical drag inside it collides with nothing.

    // Where the audible part of the sound sits, in slots: [first, last).
    // Fractional, because the offset is -- a sound placed to the nearest frame
    // is not placed. Empty when nothing has decoded or the trim leaves nothing.
    std::pair<double, double> audioExtent(const animage::AudioTrack& sound) const;

    // The gain a drag at `y` in this row means. Clamped to 0..1, and measured
    // from the *bottom* of the row: the bar's height in the row is the level,
    // so at the bottom it is silent and no separate mute is needed.
    double gainForY(std::size_t row, int y) const;

    // **Three gestures in one row, and one of them is decided rather than
    // chosen.** Dragging an end crops; dragging the body sideways moves the
    // sound along the shot; dragging it up and down sets the level. The ends
    // are unambiguous and start on the press. The other two share a press and
    // are told apart by which way the pointer goes first -- exactly how this
    // file already tells a drawing drag (along the row) from a track restack
    // (across it).
    //
    // Nothing rounds to a frame. 1/24 of a second is 42 ms, which is most of
    // the way to a syllable, so a sound placed to the nearest frame is not
    // placed at all -- and a pixel is 1/26 of a frame at this cell width, which
    // is the precision the gesture actually has.
    enum class AudioDrag { None, Deciding, Move, Gain, TrimStart, TrimEnd };
    void applyAudioDrag(int x, int y);

    // Where the block's two ends are, in x, for the grab zones and the cursor.
    bool audioEdgeAt(std::size_t row, int x, bool* is_start) const;

    // Where the ruler is drawn, in this widget's own coordinates: 0 until
    // somebody scrolls. See setRulerTop.
    int ruler_top_ = 0;
    // And where the gutter is, the same way and for the same reasons.
    int gutter_left_ = 0;

    animage::Document& doc_;
    animage::TrackId track_ = animage::kNoId;

    // The second selection. See highlightedAudio(): kNoId means the highlighted
    // row is a drawing row, and `track_` says which.
    animage::TrackId audio_row_ = animage::kNoId;

    std::size_t current_slot_ = 0;

    // A gesture in a soundtrack's row. One command for the whole drag, as the
    // exposure stretch does, so a level found by ear or a sound nudged into
    // place undoes in a single step rather than in fifty.
    //
    // `audio_drag_from_` is the placement as it was when the press landed, and
    // every drag is computed *from* it rather than by accumulating deltas. That
    // is the same reason the transform box holds absolute numbers: accumulating
    // makes the result depend on how many mouse events arrived, which is not
    // something a person can aim at.
    AudioDrag audio_drag_ = AudioDrag::None;
    animage::TrackId audio_drag_track_ = animage::kNoId;
    std::size_t audio_drag_row_ = 0;
    animage::AudioPlacement audio_drag_from_;

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
    // Which of the two lists `renaming_` names, settled when the editor opens.
    // An id handed to the wrong lookup answers nothing rather than something
    // plausible, and what that would look like here is a soundtrack's new name
    // written onto a track.
    bool renaming_audio_ = false;
    // A pen's double tap, which Qt does not turn into a double click for us.
    DoubleTap taps_;
    // editingFinished also fires when the editor loses focus, which is what
    // hiding it does. Without this, applying a name applies it twice.
    bool finishing_rename_ = false;
};
