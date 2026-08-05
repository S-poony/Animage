// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QMainWindow>

#include "document.h"
#include "project_files.h"

class CanvasWidget;
class TimelineWidget;
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

    // Judges every drawing's colour fills so the timeline can flag the ones
    // worth looking at without anybody having visited them. Runs a quarter of a
    // second after the edits stop; public so a test need not wait.
    void auditColourFills();

    // Whether the drawing at `slot` is flagged on any colour layer. What the
    // timeline card draws, and the only way to ask about a drawing without
    // going there -- which is exactly what a test of this feature must not do.
    bool colourFlagAt(std::size_t slot) const;

    // Writes the sequences to `folder` with a progress dialog over them. What
    // exportSequences does once it knows where; also how a test drives it.
    bool exportSequencesTo(const QString& folder, bool layers, bool flattened,
                           QString* error = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    // Autosave means the disk is meant to be current, so closing flushes rather
    // than warning: there is no unsaved state to ask about, only state that has
    // not been written yet.
    void closeEvent(QCloseEvent* event) override;

private:
    void buildActions();
    void buildLayerPanel();
    void buildTimelinePanel();
    void buildStatusBar();

    void rebuildLayerList();
    void syncStatus();
    void refreshEverything();

    void addLayer();
    void addColourLayer();
    void removeCurrentLayer();
    void moveCurrentLayer(int delta);
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

    void onSlotChanged(std::size_t slot);
    void stepFrame(int delta);
    void stepDrawing(int direction);
    std::size_t slotAfterCurrentDrawing() const;
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
    // Asks where and what, then writes the sequences with a progress dialog.
    // Exposed the same way openProjectAt is, for the same reason: a test cannot
    // answer a file dialog.
    void exportSequences();
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
    QTreeWidget* layer_list_ = nullptr;
    QSlider* opacity_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* colour_swatch_ = nullptr;
    QPushButton* transparent_swatch_ = nullptr;

    QTimer* audit_timer_ = nullptr;
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

    QTimer* playback_timer_ = nullptr;
    QTimer* autosave_timer_ = nullptr;
    QElapsedTimer playback_clock_;
    std::size_t playback_start_slot_ = 0;

    // Empty until the project has been saved somewhere. `saved_undo_depth_` is
    // where the history stood when it was last written, which is a cheap and
    // honest test for "changed since": undoing back to it counts as unchanged,
    // because it is.
    QString project_folder_;
    std::size_t saved_undo_depth_ = 0;

    // What the last save or open put in `project_folder_`, so the next save
    // only re-encodes the drawings that moved. Held by the window because it
    // describes this document in that folder and nothing outside the pair
    // means anything.
    project::SaveState save_state_;

    bool updating_list_ = false;
    bool forwarding_key_ = false;
    bool framed_once_ = false;
    float colour_r_ = 0.0f, colour_g_ = 0.0f, colour_b_ = 0.0f;

    // The last colour that was a colour, so leaving a colour layer while
    // "transparent" is selected has somewhere to go back to.
    float solid_r_ = 0.0f, solid_g_ = 0.0f, solid_b_ = 0.0f;
};
