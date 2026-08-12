// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QMainWindow>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

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
    bool openProjectAt(const QString& folder, QString* error = nullptr);

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

    // The action a shortcut id names, or null if the window never made one.
    // For tests that assert the window and the table agree about the keyboard --
    // buildActions reading the table is exactly the thing that would rot
    // silently, because a stale literal still builds and still works.
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
    void syncTooltips();
    // Edit > Keyboard shortcuts. Opens the dialog, and on Apply installs what it
    // hands back and writes it down.
    void chooseShortcuts();

    void buildActions();
    void buildLayerPanel();
    void buildTimelinePanel();
    void buildStatusBar();

    void rebuildLayerList();
    void syncStatus();
    void refreshEverything();
    // Asks the timeline dock for a height that suits the number of tracks, up
    // to four rows. A request and not a constraint: the dock stays draggable in
    // both directions afterwards.
    void syncTimelineHeight();
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
    void onLayerSelected();
    void onLayerItemChanged(QTreeWidgetItem* item, int column);
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

    // Tracks. Adding one puts it at the bottom of the stack and makes it
    // current, because the thing you do next is draw on it.
    void addTrack();
    void renameTrack();
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
    bool saveTo(const QString& folder);
    // Points everything at the document that was just loaded: the canvas, the
    // timeline and the layer panel all hold ids from the old one.
    void afterProjectLoaded();
    void updateTitle();

    void togglePlayback();
    void stopPlayback();
    void onPlaybackTick();
    void onOnionChanged();
    void setFramerate(int fps);

    animage::Layer* currentLayer();

    animage::Document doc_;
    animage::TrackId track_ = animage::kNoId;

    CanvasWidget* canvas_ = nullptr;
    TimelineWidget* timeline_widget_ = nullptr;
    QScrollArea* timeline_scroll_ = nullptr;
    QDockWidget* timeline_dock_ = nullptr;
    QWidget* timeline_controls_ = nullptr;
    // How many rows the dock was last sized for. Without it every refresh --
    // and there is one per frame change -- would shove the dock back to the
    // height the track count implies, undoing a drag the moment you scrubbed.
    int timeline_rows_shown_ = 0;
    LayerList* layer_list_ = nullptr;
    QSlider* opacity_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    QLabel* status_ = nullptr;
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
    bool updating_transform_fields_ = false;

    // Every action the shortcut table names, by id. See setShortcutMode.
    std::unordered_map<shortcuts::Id, QAction*> keyed_actions_;
    shortcuts::Mode mode_ = shortcuts::Mode::Normal;

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
    // The three "past the last drawing" items, with what each one means.
    std::vector<std::pair<QAction*, animage::TrackEnd>> end_actions_;
    bool updating_track_menu_ = false;

    QTimer* playback_timer_ = nullptr;
    QTimer* autosave_timer_ = nullptr;
    QElapsedTimer playback_clock_;
    std::size_t playback_start_slot_ = 0;

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

    bool updating_list_ = false;
    bool forwarding_key_ = false;
    bool framed_once_ = false;
    float colour_r_ = 0.0f, colour_g_ = 0.0f, colour_b_ = 0.0f;

    // The last colour that was a colour, so leaving a colour layer while
    // "transparent" is selected has somewhere to go back to.
    float solid_r_ = 0.0f, solid_g_ = 0.0f, solid_b_ = 0.0f;
};
