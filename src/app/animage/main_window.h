// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "audio_player.h"
#include "document.h"
#include "export_sequence.h"
#include "project_io.h"
#include "shortcuts.h"

class CanvasWidget;
class TimelineWidget;
class LayerList;
class QTreeWidget;
class QTreeWidgetItem;
class QSlider;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QAction;
class QTimer;
class QGroupBox;
class QListWidget;
class QComboBox;
class QFrame;
class QScrollArea;

// The application window: canvas, timeline, layers, and the File menu. One
// track. A project saves, opens, autosaves over itself and exports.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow() override;

    // Opens a project by path. What the menu item does once it has asked where,
    // and the way a test drives it: what has gone wrong here before is not the
    // file but the canvas and the panels still holding ids from the document
    // that was just replaced.
    // False on a project that would not open *and* on one the user declined to
    // rescue, which are told apart by `error`: a message means it failed, an
    // empty one means the damaged-project question was answered no and nothing
    // has happened at all.
    bool openProjectAt(const QString& folder, QString* error = nullptr);

    // Writes the project to `folder` and makes that the project's home, which
    // is what Save and Save As both end in. Public for the same reason
    // openProjectAt is: the paths that reach it in the program go through a
    // file dialog, and a test cannot answer one.
    //
    // Reports a failure in a message box rather than to the caller, so a test
    // driving it is asserting that saving worked and never inspecting why it
    // did not.
    bool saveTo(const QString& folder);

    // What the autosave timer does when it fires. Public for the same reason as
    // openProjectAt: the interval is two minutes, so a test that waited for it
    // would not be a test. Writes over the project without asking and without a
    // dialog if it fails; silent when there is nothing to write; deferred while
    // the pen is down or the animation is playing.
    void onAutosaveTick();

    // Waits for every colour solve in flight and installs it. For tests that
    // want the answers rather than the asking. Never call it from anything the
    // user is waiting on.
    bool waitForColour();

    // The document this window is editing. For tests that need to build a
    // situation the interface can only reach with a tablet in somebody's hand.
    animage::Document& documentForTesting() { return doc_; }

    // Brings a picture in from `path` and puts it on a new track as a reference
    // layer. Everything File ▸ Import ▸ Image does except ask: no file dialog,
    // no recap, no message box.
    //
    // Public because the asking and the doing want separating anyway --
    // docs/importing.md asks for each import to be "a function the drop handler
    // can call", and a modal file dialog is not something `shots` or a test can
    // drive. This is that function, and the menu item is a dialog in front of
    // it.
    //
    // False with `trouble` filled in when the file cannot be read; the document
    // is untouched in that case.
    bool importImageFrom(const QString& path, QString* trouble = nullptr);

    // The same for a sequence: the files already in the order they will be
    // shown in, landing on a new track from `start_frame` (1 for the first
    // frame of the shot), one drawing each.
    //
    // **Nothing is decoded here.** The still's version decodes because it
    // already has to, to find out whether the file reads at all; a sequence of
    // two hundred would be two hundred decodes on the interface thread, so the
    // drawings are pointed at their files and the paint asks for them. What
    // that means for a caller is that the pictures are not on screen when this
    // returns -- see settleReferenceFrames.
    //
    // A file that will not read is kept in the list rather than dropped,
    // because the position in that list is what each drawing points at.
    // Removing one would move every frame after it onto the wrong picture.
    //
    // False, with the document untouched, only when there are no files at all.
    bool importSequenceFrom(const std::vector<QString>& paths, int start_frame, bool half_size,
                            QString* trouble = nullptr);

    // What converting a reference layer to drawings would cost, asked before it
    // is paid and from the file headers rather than by decoding anything.
    //
    // The tile count is what the refusal is decided on and what the popup
    // quotes, so it is one number worked out in one place: a survey of each
    // frame the conversion would read, through the layer's placement, counted
    // in tiles the way the grid would key them.
    //
    // Not exact, and it does not have to be: sizes may differ from frame to
    // frame and a survey is a header read, so this is the same kind of bound
    // `commitFitsInBudget` is -- conservative enough to refuse before the
    // machine does, with `Document::convertReferenceLayer`'s rescue as the
    // other half. See docs/importing.md, "convert to drawings".
    struct ConversionCost {
        std::size_t drawings = 0;
        std::size_t tiles = 0;
        // How many of those frames could not be read at all. They convert to
        // empty drawings rather than stopping the conversion, and the popup
        // says so -- a hole somebody is told about is a hole they can fix by
        // undoing, and one they are not told about is a hole they find months
        // later.
        int unreadable = 0;
        // Whether the placement resamples. The two exact paths -- a whole-pixel
        // move and an axis mirror -- keep every pixel, and this is the same
        // question `transformTiles` branches on rather than a second opinion
        // about it. It decides which of the two things the popup is allowed to
        // say about quality.
        bool resampled = false;
    };
    ConversionCost conversionCostFor(animage::TrackId track, animage::LayerId layer) const;

    // Converts a reference layer to ordinary drawings: every frame it was
    // showing written into a cel, in one command, and the layer left a raster
    // one. See Document::convertReferenceLayer, which is where the loop is.
    //
    // Public and dialog-free for importImageFrom's reason: the asking and the
    // doing want separating, and a modal dialog is not something `shots` or a
    // test can drive. Nothing here asks whether it fits -- the caller does,
    // through conversionCostFor -- because a refusal has to arrive before the
    // wait rather than after it.
    //
    // False with `trouble` filled in when nothing was converted, and the
    // document is untouched in that case.
    bool convertReferenceLayerFrom(animage::TrackId track, animage::LayerId layer,
                                   QString* trouble = nullptr);

    // Brings a soundtrack in from `path` and adds it to the scene, starting on
    // `start_frame` (1 for the first frame of the shot, and less than 1 is
    // allowed -- a breath in front of a word falls off the start).
    //
    // Public for importImageFrom's reasons: the asking and the doing want
    // separating, and a modal dialog is not something `shots` or a test can
    // drive.
    //
    // **It decodes, and the decode is the whole cost.** Unlike a sequence there
    // is nothing to defer to a paint: a soundtrack is one file, tens of
    // milliseconds, and what would ask for it later is a device callback that
    // must never wait on a disk. So it is read here, in full, before the track
    // exists.
    //
    // False with `trouble` filled in when nothing could be decoded, and the
    // document is untouched in that case. True with `trouble` *also* filled in
    // when something was read and there is still something to say -- a file cut
    // short, or one read only as far as the cap.
    // `extend_shot` makes the shot reach the end of the sound, which is what
    // the dialog's checkbox asks. Off for a caller that says nothing, because a
    // programmatic import should not quietly redefine what gets exported.
    bool importAudioFrom(const QString& path, int start_frame, QString* trouble = nullptr,
                         bool extend_shot = false);

    // Waits for every imported picture on screen to be decoded and installed.
    //
    // Public so a test can change a placement directly and then ask for the
    // consequence, which is otherwise only reachable through a live gesture and
    // a wait. **Never on an ordinary path** -- the decode runs on a worker
    // precisely so that nothing on the interface thread waits for one.
    bool settleReferenceFrames(int timeout_ms = 30000);

    // How much of every imported sequence in the scene has been decoded.
    //
    // For the status bar, and it is the answer to a real complaint: an import
    // whose frames are not ready draws nothing, correctly -- compositing may
    // not start a decode -- so playing a shot before they are all in shows the
    // playhead advancing over a blank canvas with nothing anywhere saying why.
    // "Working" and "broken" look identical, and only a number tells them
    // apart, because a number moves.
    //
    // Public so a test can pin what it counts. Whether it *appears* is a thing
    // to judge by looking -- it comes and goes with a decode being outstanding,
    // which is a race no screenshot can catch -- but the counting rules are not,
    // and they are what would go wrong silently.
    struct ImportsReady {
        int ready = 0;
        int wanted = 0;
    };
    ImportsReady importsReady() const;

    // How much of the shot has colour on it, in the same shape and for the same
    // reason. A shot is coloured by scribbling one drawing and letting the marks
    // carry, so every other drawing has no fill until something solves it --
    // and until issue #85 nothing ever did during a take. With fills
    // accumulating, this is what says how far along a take has got.
    //
    // Counts a fill that exists rather than one that is up to date. A fill whose
    // inputs have moved still draws, and asking each one whether it is current
    // would mean hashing every barrier cel of every drawing, four times a
    // second.
    ImportsReady drawingsColoured() const;

    // The action a shortcut id names, or null if the window never made one.
    // For tests that assert the window and the table agree about the keyboard --
    // the window building its actions from the table is exactly the thing that
    // would rot silently, because a stale literal still builds and still works.
    QAction* actionForTesting(shortcuts::Id id) const;

    // Installs a set of bindings: every action gets its new key, and every
    // tooltip that names one is composed again. What the shortcuts dialog hands
    // back when Apply is pressed, and how a test rebinds something without one.
    //
    // It does not write them down. Saving is the menu handler's job, because a
    // set of bindings adopted for the length of a test is not a settings file.
    void adoptShortcuts(const shortcuts::Bindings& bindings);

    // Writes the sequences to `folder` with a progress dialog over them, and
    // `folder` is the whole destination: the export dialog asks for a name and
    // joins it to the directory that was chosen, so a shot's sequences land in
    // a folder of their own. What exportSequences does once it knows where;
    // also how a test drives it.
    bool exportSequencesTo(const QString& folder, bool layers, bool flattened,
                           exporting::Format format = exporting::Format::Png,
                           QString* error = nullptr);

    // What that name is before the user changes it: the project's own, without
    // the suffix that makes a folder a project. "untitled" for a document that
    // has never been saved anywhere.
    QString defaultExportName() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    // Maximising and restoring frame the canvas in the window. See the comment
    // on it: this arms the reframe rather than doing it, because the resize that
    // makes it meaningful has not necessarily arrived yet.
    void changeEvent(QEvent* event) override;
    // Autosave means the disk is meant to be current, so closing flushes rather
    // than warning: there is no unsaved state to ask about, only state that has
    // not been written yet.
    void closeEvent(QCloseEvent* event) override;

private:
    // Makes the action a shortcut id names -- its label, its key, its
    // application-wide context -- and remembers it, so that changing mode is one
    // loop over the table rather than setEnabled calls spread through the code
    // that changes mode. The caller puts it in whatever menu or toolbar it
    // belongs to; an action in neither needs addAction to be live at all.
    QAction* makeAction(shortcuts::Id id, std::function<void()> handler);

    // The one place the keyboard changes meaning. A disabled QAction does not
    // consume its shortcut, which is the whole mechanism: what goes quiet here
    // is what the canvas gets to hear.
    void setShortcutMode(shortcuts::Mode mode);
    // A rename editor has opened or closed somewhere. See the comment on it:
    // Return is Play, so the shortcuts have to let go of the keyboard.
    void setTyping(bool typing);
    // A press on something that will not take the keyboard still takes it off a
    // field that holds one. See the definition for the four cases it leaves
    // alone.
    void takeTheKeyboardBackFrom(QWidget* pressed);

    // A tooltip that has to say which key it is on.
    //
    // The key is never typed into the sentence. Every one of these used to end
    // in a literal "(Ctrl+D)" or "(Enter)", which was already only as true as
    // the last person to move a binding, and rebinding makes it false the moment
    // anybody uses it. So the sentence says what the control does and the key is
    // appended here, from the bindings, every time they change.
    //
    // `more` is whatever follows on its own lines, and it may name other
    // shortcuts as %1, %2 in the order of `also` -- for the same reason: prose
    // that spells a key out is prose that goes stale.
    void keyedTip(QAction* on, shortcuts::Id id, const QString& what,
                  const QString& more = QString(), const std::vector<shortcuts::Id>& also = {});
    void keyedTip(QWidget* on, shortcuts::Id id, const QString& what,
                  const QString& more = QString(), const std::vector<shortcuts::Id>& also = {});
    // Changes what one keyed tooltip says, keeping the key it names.
    void setKeyedTipText(QWidget* on, const QString& what);
    // The same for an action, and it replaces both halves of the tip rather
    // than the first: a control that has just been greyed out wants a different
    // second paragraph too, and the one it had is about a gesture that can no
    // longer be started.
    void setKeyedTipText(QAction* on, const QString& what, const QString& more);
    void syncTooltips();
    // Edit > Keyboard shortcuts. Opens the dialog, and on Apply installs what it
    // hands back and writes it down.
    void chooseShortcuts();

    // The menu bar, and the toolbar. Two functions and not one because the
    // seam between them is real -- see the comment on buildToolBar.
    void buildMenus();
    void buildToolBar();
    void buildLayerPanel();
    void buildTimelinePanel();
    // The part of the View menu that needs the docks to exist, so it cannot be
    // built with the rest of the menus: buildMenus runs before either panel.
    void buildPanelViewActions();
    void buildStatusBar();

    void rebuildLayerList();
    void syncStatus();
    void refreshEverything();


    // An imported file the canvas could not turn into a picture. Gathered, and
    // said once per document; see the definitions for why the saying is queued
    // out of the paint that found it.
    void onImportUnreadable(const QString& name, const QString& trouble);
    void reportUnreadableImports();
    // Asks the timeline dock for a height that suits the number of tracks, up
    // to four rows. A request and not a constraint: the dock stays draggable in
    // both directions afterwards.
    void syncTimelineHeight();
    // Puts the docks back where the window opened them: position, floating,
    // shown, and size. See default_layout_.
    void restoreDefaultView();
    // Unsticks the window's layout after a panel has been dragged out of it.
    // Issue #54, and Qt's bug rather than ours -- the reasoning is on the
    // definition.
    void wakeLayout();
    // The timeline's own metrics, so the dock asks for a height that matches
    // what the widget will draw.
    static constexpr int kRowHeight = 46;
    static constexpr int kRulerAndRows = 18 + kRowHeight;
    // Past this the dock stops growing and the strip scrolls: the height it
    // takes comes out of the canvas, and the canvas is what is drawn on.
    static constexpr int kMaxDockRows = 4;

    void addLayer();
    void addColourLayer();
    void removeCurrentLayer();
    // What a row dropped somewhere else in the list means. Both indices are rows
    // in the panel, and `to` is counted with the dragged row taken out of it --
    // see LayerList and Document::moveLayer, which agree about that.
    void moveLayerTo(int from, int to);
    // Puts the current layer in view, now rather than eventually. See issue #12:
    // the list scrolls itself when the selection changes, and it does it against
    // the panel as it stands at that moment rather than the one it is about to
    // become.
    void showCurrentLayer();
    // Frames the canvas if a maximise or a restore has just armed one. Asked
    // from the canvas's resize and from a queued call after the state change,
    // because neither event on its own is enough; see the comment on it.
    void reframeIfArmed();
    void onLayerSelected();
    void onLayerItemChanged(QTreeWidgetItem* item, int column);
    // What a double click on a layer's name typed. The panel does not edit
    // itself: it says what was typed, and the list is rebuilt from the document.
    void renameLayer(int row, const QString& name);
    void onOpacityChanged(int percent);
    void beginOpacityDrag();
    void endOpacityDrag();
    void clearCurrentLayer();
    std::string nextLayerName() const;
    std::string nextColourLayerName() const;
    void chooseColour();

    // Scribble "nothing here" rather than a colour. Only means anything on a
    // colour layer, where a mark is a label and one of the labels can be the
    // absence of colour; on a raster layer it would be paint made of negative
    // light. The swatch is disabled off a colour layer and the colour is put
    // back to the last real one on the way out, so the state cannot be reached
    // by wandering into it.
    // The two positions of the colour switch. Neither opens the dialog: a
    // swatch chooses what it shows, and Colour... is what changes it.
    void chooseSolidColour();
    void chooseTransparent();

    bool colourIsTransparent() const;
    void syncColourControls();

    // The one place a colour becomes the brush's, whether it came from the
    // dialog, the eyedropper or the transparent swatch. Linear light, straight.
    void applyColour(float r, float g, float b);
    // What a layer's row says, and the flag on it. Shared by the full rebuild
    // and by the in-place refresh a finished solve triggers.
    // The colour-layer box: what it is cut against, and what it does with time.
    void syncColourLayerPanel();
    void onCtgSourcesChanged();
    void onCtgSettingChanged();

    QString layerLabel(const animage::Layer& layer, animage::ImageId here) const;
    void applyLayerFlag(QTreeWidgetItem* item, const animage::Layer& layer,
                        animage::ImageId here);
    void refreshLayerFlags();

    void syncToolSettings();
    void setBrushRadius(double radius);
    void nudgeBrushRadius(double factor);
    void undo();
    void redo();

    // The bar the numeric fields live on, which exists only while a transform
    // does. The scope of a transform belongs to that transform, not to the
    // track and not to the program: a permanently visible control that is off
    // by default is a trap precisely because it is off by default.
    void buildTransformBar();
    // The bar is a child of the canvas rather than a row in the window, so
    // nothing lays it out. Called when it appears and whenever the canvas is
    // resized.
    void placeTransformBar();
    void onTransformBegan();
    void onTransformEnded();
    // The layer panel's button: every drawing of the active layer, moved
    // together. Issue #25.
    void transformLayerThroughTime();
    // The layer panel's other whole-layer button: the way back from an import.
    // Puts the cost, and what is and is not lost, before doing any of it.
    void convertLayerToDrawings();
    // The same recap, with a line in front of it saying why it turned up
    // uninvited. **One dialog and not two**, which is the whole reason this
    // takes a flag rather than the refusal raising its own question first: a
    // popup that asks "convert this?", takes Yes, and then asks "convert this?"
    // again with more words is a program that did not hear the first answer.
    void askToConvertLayer(bool because_a_stroke_was_refused);
    // The same offer, arriving from the other side: somebody has just tried to
    // draw on an imported picture, and the refusal is where the question
    // actually gets asked. Both doors are wanted -- the popup is
    // discoverability and the button is findability, and a control that comes
    // and goes as you move between layers is one nobody can find twice.
    void offerToConvertRefusedLayer();
    // Whether the layer panel's two buttons that can refuse can be pressed on
    // the layer in front of you, and what their tooltips say when they cannot.
    void syncLayerButtons();
    void syncTransformFields();
    void onTransformFieldEdited();

    // Which way a flip mirrors. Named rather than a bool, because "flip
    // horizontal" is ambiguous in exactly the way that matters here: what is
    // named is the *axis*, and X mirrors left to right.
    enum class FlipAxis { X, Y };
    // Issue #24. A state of the live transform and not an operation on the
    // drawing: pressing it twice is where you started, and nothing is written
    // until the transform is applied like any other.
    void flipTransform(FlipAxis axis);
    // What the Transform tool does, including saying why when it will not.
    void chooseTransformTool();

    // Cut, copy and paste, which differ from each other by so little that three
    // handlers would be three copies of the same refusal reporting.
    enum class Clipboard { Cut, Copy, Paste };
    void clipboard(Clipboard what);

    // Importing. One item per kind of thing being imported rather than one
    // that sniffs the file, because they arrive at different times and ask
    // different questions. See docs/importing.md.
    void importImage();
    void importImageSequence();
    void importAudio();

    // Decodes every soundtrack the scene names and has no samples for.
    //
    // Called after a load, and that is the only thing that needs it: a decoded
    // clip is derived data thrown away with the document it belonged to, and an
    // import decodes as it arrives. Cheap when there is nothing to do, which is
    // every call but the one after opening a project with sound in it.
    void refreshAudioSamples();

    // Opens or closes the audio output to match what the document holds.
    //
    // **A project with a soundtrack keeps an output open; one without keeps
    // none.** That is the shape opening costs forced: a scrub is a burst of
    // sound on every frame the playhead is dragged past, and opening a device
    // is a third of a second, so it cannot be done per burst. Opening it when
    // the sound arrives rather than when the first burst does is what stops
    // the first burst being the silent one.
    //
    // Cheap and idempotent, so it can be called from anywhere the document may
    // have changed. It reopens only when the rate it should be running at
    // changes, which is when a soundtrack arrives or goes away.
    void refreshAudioDevice();

    // The soundtrack rate to open a device at, or 0 for "no sound here".
    //
    // The first decoded clip's own rate, so the ordinary case -- one soundtrack
    // -- is read sample for sample with no resampling at all. A second
    // soundtrack at another rate is resampled to this one, which is the right
    // way round: the file somebody is animating to should be the exact one.
    int audioRate() const;

    // Everything the scene's soundtracks are, as a device's thread may read
    // them, starting at `slot`. Empty sources when there is nothing to play.
    animage::AudioProgram audioProgramAt(std::size_t slot) const;

    // A burst of sound from where the playhead just landed.
    //
    // What TimelineWidget::scrubbed is for, and only that: reading a track
    // means dragging back and forth over a syllable until you can hear which
    // frame the consonant is on. Stepping frames with the keyboard while
    // drawing is not that, and makes no noise.
    void scrubAudio(std::size_t slot);

    // Hand the running take's sound to the device, from `slot`.
    //
    // Two callers, which is why it is not inside togglePlayback: pressing Play,
    // and the machine's outputs changing under a take that is already running.
    // The second re-anchors on the frame it has reached rather than the one
    // Play was pressed on, because the count it derives from starts again with
    // the device.
    void startPlaybackAudio(std::size_t slot);

    // The machine's outputs changed: somebody plugged a speaker in, switched
    // one on, or pulled one out.
    void onAudioOutputsChanged();

    // Where an imported file's bytes are: inside the project once it has been
    // saved, and at the path it came from until then. Empty when neither.
    QString importPathFor(const std::string& name) const;
    QString audioPathFor(const std::string& name) const;

    // One frame of an import with no placement applied, which is what a
    // placement gesture floats. `frame` indexes `Layer::reference_sources`, so
    // it is a drawing's `Image::sourceFrameFor` and never a slot. See the
    // definition: the stored placement is absolute, so the pixels under it have
    // to be the unplaced ones.
    animage::TileGrid importAtOneToOne(const animage::Layer& layer, int frame,
                                       QString* trouble = nullptr) const;


    // Derives the pixels of every reference layer whose picture is missing or
    // was derived at a placement the layer no longer has. Cheap when there is
    // nothing to do, which is nearly always; see the definition for why it runs
    // here rather than from the paint.
    //
    // Reports what it could not read rather than failing. A missing import
    // costs that picture; the drawings are not affected, and refusing the whole
    // project over a reference would be worse than opening it with the
    // reference blank and saying so.

    // Tracks. Adding one puts it at the bottom of the stack and makes it
    // current, because the thing you do next is draw on it.
    void addTrack();
    void renameTrack();

    // The row you are pointed at, again, and it works on a soundtrack too --
    // which is also how a second soundtrack gets into a scene, the model having
    // been a list all along.
    void duplicateCurrentTrack();
    void removeCurrentTrack();
    void setOverwriteDrawings(bool overwrite);
    void setTrackEnd(animage::TrackEnd behaviour);
    // Points the canvas, the layer panel and the menus at another track.
    void setCurrentTrack(animage::TrackId track);
    // What the Track menu says about the track you are on.
    void syncTrackMenu();

    void onSlotChanged(std::size_t slot);
    void stepFrame(int delta);
    void stepDrawing(int direction);
    // Refreshes and puts the playhead on a drawing that has just been made.
    // Where that is has to be asked rather than assumed: on a track that
    // overwrites, a new drawing does not land after the hold.
    void goToNewDrawing(animage::ImageId made);
    void insertInterval();
    void duplicateDrawing();
    void deleteDrawing();
    void extendExposure();
    void shortenExposure();
    void chooseSceneSettings();

    void newProject();
    // Called before anything that replaces or abandons the open document.
    // Returns false to mean "the user cancelled, do not go".
    //
    // A project with a folder is simply written -- autosave already decided the
    // disk is current, so leaving is not a way to discard. A project without one
    // is the single case autosave cannot cover, and is the only case that asks.
    bool leaveCurrentDocument();
    // The document the application starts with, which is also what New makes.
    void resetToNewDocument();
    void openProject();
    // Asks what, where and under what name, then writes the sequences with a
    // progress dialog. Exposed the same way openProjectAt is, for the same
    // reason: a test cannot answer a file dialog.
    void exportSequences();
    // Asks about, and then empties, an export folder that already has an export
    // in it; refuses outright if what is in it is not one. False means do not
    // export. See the comment on it: an export replaces rather than merges.
    bool clearTheWayFor(const QString& folder, const QString& called);
    void saveProject();
    void saveProjectAs();
    // Points everything at the document that was just loaded: the canvas, the
    // timeline and the layer panel all hold ids from the old one.
    void afterProjectLoaded();

    // What a project with unreadable drawings in it opens with: how many were
    // lost, which ones, where the rescued copy is going and that the damaged
    // original will not be touched. True to go ahead.
    bool askAboutDamage(const QString& folder, const QString& rescued,
                        const animage::Document& loaded, const ProjectIO::Damage& damage);
    void updateTitle();

    void togglePlayback();
    void stopPlayback();
    void onPlaybackTick();
    // The effective frame rate, from paints rather than from ticks. See the
    // definition for why it is a rolling window and why it is not updated on
    // every frame.
    void updatePlaybackRate();
    void onOnionChanged();

    animage::Layer* currentLayer();

    animage::Document doc_;
    animage::TrackId track_ = animage::kNoId;

    CanvasWidget* canvas_ = nullptr;
    TimelineWidget* timeline_widget_ = nullptr;
    QScrollArea* timeline_scroll_ = nullptr;
    QDockWidget* timeline_dock_ = nullptr;
    QDockWidget* layer_dock_ = nullptr;
    // Kept because the dock entries are added to it after the panels are built
    // and not while the menus are.
    QMenu* view_menu_ = nullptr;
    // What the window looked like when it had just opened and settled, as
    // QMainWindow writes it: where each dock is, whether it is floating, whether
    // it is shown at all, and how big it is. One QByteArray covers all four,
    // which is why "restore the default view" needs nothing else -- the timeline
    // height is a dock size like any other.
    //
    // Taken after the first show and not in the constructor. See showEvent: from
    // the constructor the dock has not been laid out and its title bar has no
    // height to read, and a layout saved there is the one the program itself
    // then corrects.
    QByteArray default_layout_;
    // The track count `default_layout_` was saved at, so that restoring it can
    // hand syncTimelineHeight the difference it works in. Restoring a layout
    // saved for one track into a scene with three would otherwise leave the
    // strip two rows short.
    int default_layout_rows_ = 0;
    // Whether a wake is already on its way. The restoreState a wake performs can
    // float a dock, which arrives back here as another settled(); one round trip
    // is the fix and a second is a loop. See wakeLayout.
    bool waking_layout_ = false;
    // How many rows the dock was last sized for. Without it every refresh --
    // and there is one per frame change -- would shove the dock back to the
    // height the track count implies, undoing a drag the moment you scrubbed.
    int timeline_rows_shown_ = 0;
    LayerList* layer_list_ = nullptr;
    QSlider* opacity_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    QLabel* status_ = nullptr;
    // Right-hand end of the status bar, and only while the animation is
    // playing. A permanent widget rather than part of `status_` so that the
    // main text changing length cannot shuffle it sideways: what it says has to
    // be readable out of the corner of an eye that is watching the canvas.
    QLabel* playback_rate_ = nullptr;
    // Beside it, and to its left, while a sequence is still being decoded.
    //
    // A permanent widget for the same reason the rate is one, and it is the
    // reason it stopped being a phrase in the middle of `status_`: what this
    // says has to be readable out of the corner of an eye that is watching a
    // blank canvas and wondering whether the program has stopped. Buried
    // between the zoom and the tile count it was neither findable nor legible.
    //
    // Added *before* the rate so the rate stays the right-most thing. The
    // permanent area is right-aligned as a group, so a widget appearing to the
    // left of the rate widens the group leftwards and leaves the rate exactly
    // where it was -- which is the promise the comment above makes.
    QLabel* pictures_loading_ = nullptr;
    // Enabled per layer rather than per program: a colour layer cannot be
    // transformed at all, and a locked or hidden one is not being edited.
    QPushButton* layer_transform_ = nullptr;
    // Whether the popup offered on a refused stroke is already up. A pen-down
    // on an import raises it, and pen-downs arrive faster than a dialog opens;
    // without this a hand resting on the canvas stacks them.
    bool offering_conversion_ = false;
    // The transform bar's Apply, kept because what it does depends on the
    // gesture: it bakes a drawing and it stores a placement, and one tooltip
    // for both would be false half the time.
    QPushButton* transform_apply_ = nullptr;
    // The same, for the one other panel button that can refuse: a track must
    // keep a layer, so the last one cannot be removed.
    QPushButton* layer_remove_ = nullptr;
    QPushButton* colour_swatch_ = nullptr;
    QPushButton* transparent_swatch_ = nullptr;

    QGroupBox* colour_settings_ = nullptr;
    QListWidget* ctg_sources_ = nullptr;
    QCheckBox* ctg_inherit_ = nullptr;
    QComboBox* ctg_direction_ = nullptr;
    QCheckBox* ctg_follow_ = nullptr;
    bool updating_colour_panel_ = false;
    QCheckBox* pressure_opacity_ = nullptr;
    QSpinBox* onion_ = nullptr;
    QAction* play_action_ = nullptr;
    QPushButton* play_button_ = nullptr;
    QAction* brush_action_ = nullptr;
    QAction* eraser_action_ = nullptr;
    QAction* lasso_action_ = nullptr;
    QAction* transform_action_ = nullptr;

    QFrame* transform_bar_ = nullptr;
    QSpinBox* transform_dx_ = nullptr;
    QSpinBox* transform_dy_ = nullptr;
    QDoubleSpinBox* transform_rotation_ = nullptr;
    QDoubleSpinBox* transform_scale_x_ = nullptr;
    QDoubleSpinBox* transform_scale_y_ = nullptr;
    // Checkable, because a flip is a state of the transform rather than a thing
    // that happens: the button shows which way round the drawing currently is.
    QPushButton* transform_flip_x_ = nullptr;
    QPushButton* transform_flip_y_ = nullptr;
    // What the bar is a transform *of*: this drawing, or the whole layer
    // through time. A label and never a control -- there are two doors into the
    // gesture and the scope is fixed by which one you came through, so a switch
    // here would be the persistent checkbox docs/lasso-and-transform.md
    // refused, one gesture further along.
    QLabel* transform_scope_ = nullptr;
    bool updating_transform_fields_ = false;
    // Whether the transform fitted last time the fields were read, so that the
    // status bar says it has stopped fitting once rather than on every move.
    bool transform_fitted_ = true;

    // Every action the shortcut table names, by id. See setShortcutMode.
    std::unordered_map<shortcuts::Id, QAction*> keyed_actions_;

    // Every tooltip that has to name a key, so that changing one is a loop
    // rather than a hunt through the file for parentheses.
    struct KeyedTip {
        QAction* action = nullptr;  // one of these two, never both
        QWidget* widget = nullptr;
        shortcuts::Id id{};
        QString what;
        QString more;
        std::vector<shortcuts::Id> also;
    };
    std::vector<KeyedTip> keyed_tips_;

    QAction* overwrite_action_ = nullptr;
    // The three "past the last drawing" items, with what each one means, and
    // the submenu they are in.
    //
    // **The submenu is held as well as its items, because disabling the items
    // is not disabling the menu.** A submenu whose every entry is greyed still
    // opens, which offers a choice and then refuses all of it -- reported from
    // use on a soundtrack row, where none of the three means anything.
    std::vector<std::pair<QAction*, animage::TrackEnd>> end_actions_;
    QMenu* end_menu_ = nullptr;
    bool updating_track_menu_ = false;

    // The one audio output in the program, and the one place a soundtrack is
    // handed to another thread. See audio_player.h.
    AudioPlayer audio_;
    // The last reason opening one failed, so a machine with no sound card is
    // told once rather than on every edit, and the rate that failed, so it is
    // not retried on every edit either.
    QString audio_trouble_;
    int audio_tried_rate_ = 0;
    // When the output was last checked against the machine's default. A drag is
    // dozens of scrub bursts a second and what the check costs is not known, so
    // it is bounded rather than repeated.
    QElapsedTimer audio_device_checked_;

    QTimer* playback_timer_ = nullptr;
    QTimer* autosave_timer_ = nullptr;
    QElapsedTimer playback_clock_;
    std::size_t playback_start_slot_ = 0;

    // Whether this take's picture is being driven by the sound rather than by
    // the wall clock. True only while a soundtrack is actually coming out of a
    // device, so a scene with no sound in it plays exactly as it always did.
    // See onPlaybackTick.
    bool playing_to_audio_ = false;

    // Samples of (milliseconds into this playback, canvas paints so far), taken
    // a few times a second and kept for about a second. The rate is the
    // difference across the window, which is what makes it a rate and not a
    // running average of the whole take -- a take that recovers should read as
    // recovered.
    std::deque<std::pair<qint64, std::uint64_t>> rate_samples_;
    qint64 last_rate_sample_ms_ = 0;
    // Whether the readout is currently red, so the palette is set on the
    // transition rather than four times a second for the length of a take.
    bool rate_dropping_ = false;

    // Empty until the project has been saved somewhere. `saved_history_stamp_`
    // is which state of the document was written, taken from
    // `Document::historyStamp`, so undoing back to it counts as unchanged --
    // because it is -- and every other position in the history counts as
    // changed, including one the history happens to have the same depth at.
    QString project_folder_;
    std::uint64_t saved_history_stamp_ = 0;

    // What the last save or open put in `project_folder_`, so the next save
    // only re-encodes the drawings that moved. Held by the window because it
    // describes this document in that folder and nothing outside the pair
    // means anything.
    ProjectIO::SaveState save_state_;

    // Files imported this session that are not inside a project folder yet,
    // by the name the layer knows them as.
    //
    // This exists for one moment and no other: between importing into a
    // project that has never been saved, and that project's first save. After
    // that the bytes are in `imports/` and the folder is what a save reads
    // them from -- which is the whole point of copying them in. Entries are
    // kept rather than pruned because a Save As has to be able to reach back
    // to the same original, and one path per import is nothing.
    ProjectIO::Imports imports_;

    // Which document is open, counted rather than named.
    //
    // **The one number that tells one document from another**, and it exists
    // because nothing else in the program does. A cache generation counts how
    // often that cache has been emptied, so two projects loaded one after the
    // other read the same; ids restart from a counter per document, so small
    // ones collide as a matter of course; and a history stamp is equal across
    // two freshly loaded documents. Bumped in afterProjectLoaded, which every
    // replacement of `doc_` goes through.
    //
    // What reads it is anything that lets go of the interface thread and comes
    // back expecting the same document: refreshAudioSamples, whose decode runs
    // a nested event loop, is the one today.
    std::uint64_t document_epoch_ = 0;

    // Whether a soundtrack is being decoded right now, and for which document.
    //
    // Decoding runs a nested event loop, so refreshAudioSamples can be entered
    // again from inside itself. Both halves are needed: the flag stops it
    // decoding the same track once per event that arrives during the wait, and
    // the epoch is what still lets through the one re-entry that has to run --
    // a project opened from inside that loop, whose own soundtracks nothing
    // else will decode. See refreshAudioSamples.
    bool decoding_audio_ = false;
    std::uint64_t decoding_audio_for_ = 0;

    // Whether this document has already been told about imports it cannot read.
    // The paint asks for every frame on screen and a missing file stays
    // missing, so without this the message box would come back for ever.
    bool reported_missing_imports_ = false;
    // What could not be read, gathered so that one folder going missing is one
    // box naming every file rather than a box per frame.
    QStringList unreadable_imports_;
    // Whether the box is already on its way. See onImportUnreadable: the report
    // is queued out of the paint that found it, and several frames of one
    // sequence fail in the same paint.
    bool import_report_queued_ = false;

    bool updating_list_ = false;
    bool forwarding_key_ = false;
    // Keys whose press this filter sent to the canvas, so that their release
    // goes the same way however the keyboard has changed since. See eventFilter:
    // asking the guards again on the way out is what left the canvas panning
    // for ever after Alt+Tab.
    std::unordered_set<int> forwarded_keys_;
    bool framed_once_ = false;
    // A maximise or a restore that has not settled yet: every canvas resize
    // within kReframeWindowMs of it frames the canvas again. See changeEvent.
    bool reframing_ = false;
    QElapsedTimer reframe_since_;

    float colour_r_ = 0.0f, colour_g_ = 0.0f, colour_b_ = 0.0f;

    // The last colour that was a colour, so leaving a colour layer while
    // "transparent" is selected has somewhere to go back to.
    float solid_r_ = 0.0f, solid_g_ = 0.0f, solid_b_ = 0.0f;
};
