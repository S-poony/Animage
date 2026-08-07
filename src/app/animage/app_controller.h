// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <string>

#include "canvas_view.h"
#include "document.h"
#include "layers_model.h"
#include "project_io.h"
#include "scene_settings_model.h"
#include "timeline_model.h"
class QTimer;

// The colour-layer box: the raster layers this colour layer cuts against.
// Listed as every raster layer in the track with a tick against the ones in
// use, so a layer deleted since stops being offered and one added afterwards
// can be ticked without the set being rebuilt from scratch.
class CtgSourcesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        CheckedRole,
        LayerIndexRole,
    };

    explicit CtgSourcesModel(QObject* parent = nullptr);

    void setDocument(animage::Document* document) { doc_ = document; }
    void setTrack(animage::TrackId track) { track_ = track; }
    // Which raster layers exist and which of them the current CTG layer uses.
    void refresh(const animage::Layer* ctg_layer);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int count() const { return static_cast<int>(raster_layers_.size()); }
    bool empty() const { return raster_layers_.empty(); }
    bool hasSelection() const;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The index (in the model) of the layer with this id, or -1.
    int indexOfLayer(animage::LayerId id) const;

Q_SIGNALS:
    void countChanged();

private:
    animage::Document* doc_ = nullptr;
    animage::TrackId track_ = animage::kNoId;
    // Raster layer ids, in track order.
    std::vector<animage::LayerId> raster_layers_;
    std::vector<bool> used_;
};

// The brain of the interface: owns the document and every command, and exposes
// to QML everything the panels, the toolbar and the dialogs can ask for. It is
// what MainWindow was, minus the widgets: no layout, no dialogs, only the
// state and the verbs, with signals for the interface to show.
class AppController : public QObject {
    Q_OBJECT

    // --- the models -----------------------------------------------------
    Q_PROPERTY(LayersModel* layersModel READ layersModel CONSTANT)
    Q_PROPERTY(TimelineModel* timelineModel READ timelineModel CONSTANT)
    Q_PROPERTY(CtgSourcesModel* ctgSourcesModel READ ctgSourcesModel CONSTANT)
    Q_PROPERTY(SceneSettingsModel* sceneSettingsModel READ sceneSettingsModel CONSTANT)

    // --- the window ------------------------------------------------------
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(bool colourPending READ colourPending NOTIFY colourPendingChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(int undoDepth READ undoDepth NOTIFY stateChanged)

    // --- the canvas ------------------------------------------------------
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY viewChanged)

    // --- the document ----------------------------------------------------
    Q_PROPERTY(int frameCount READ frameCount NOTIFY stateChanged)
    Q_PROPERTY(int drawingCount READ drawingCount NOTIFY stateChanged)
    Q_PROPERTY(int layerCount READ layerCount NOTIFY stateChanged)
    Q_PROPERTY(int tileCount READ tileCount NOTIFY stateChanged)
    Q_PROPERTY(int framerate READ framerate WRITE setFramerate NOTIFY stateChanged)
    Q_PROPERTY(int sceneWidth READ sceneWidth NOTIFY stateChanged)
    Q_PROPERTY(int sceneHeight READ sceneHeight NOTIFY stateChanged)

    // --- the tools -------------------------------------------------------
    Q_PROPERTY(int tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(double brushRadius READ brushRadius WRITE setBrushRadius NOTIFY brushChanged)
    Q_PROPERTY(bool pressureOpacity READ pressureOpacity WRITE setPressureOpacity
                   NOTIFY brushChanged)
    Q_PROPERTY(QColor brushColour READ brushColour NOTIFY brushColourChanged)
    Q_PROPERTY(QColor solidColour READ solidColour NOTIFY brushColourChanged)
    Q_PROPERTY(bool transparentSelected READ transparentSelected NOTIFY brushColourChanged)
    Q_PROPERTY(bool onColourLayer READ onColourLayer NOTIFY layerStateChanged)

    // --- the layers ------------------------------------------------------
    Q_PROPERTY(int currentLayerIndex READ currentLayerIndex WRITE selectLayerIndex
                   NOTIFY layerStateChanged)
    Q_PROPERTY(int layerOpacity READ layerOpacity WRITE setLayerOpacity NOTIFY layerStateChanged)
    Q_PROPERTY(bool layerShowScribbles READ layerShowScribbles NOTIFY layerStateChanged)
    Q_PROPERTY(bool ctgInherit READ ctgInherit WRITE setCtgInherit NOTIFY layerStateChanged)
    Q_PROPERTY(int ctgDirection READ ctgDirection WRITE setCtgDirection NOTIFY layerStateChanged)
    Q_PROPERTY(bool ctgFollow READ ctgFollow WRITE setCtgFollow NOTIFY layerStateChanged)

    // --- the timeline ----------------------------------------------------
    Q_PROPERTY(int currentSlot READ currentSlot WRITE setCurrentSlot NOTIFY slotChanged)
    Q_PROPERTY(int currentHold READ currentHold NOTIFY stateChanged)
    Q_PROPERTY(int onionCount READ onionCount WRITE setOnionCount NOTIFY onionChanged)

    // --- saving ----------------------------------------------------------
    Q_PROPERTY(QString projectName READ projectName NOTIFY stateChanged)
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY stateChanged)

public:
    enum Tool { Brush = 0, Eraser = 1 };
    // The answers a "may I leave the unsaved document?" question can give.
    enum SaveDecision { Save = 0, Discard = 1, Cancel = 2 };
    Q_ENUM(Tool)
    Q_ENUM(SaveDecision)

    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    // The canvas the controller drives. Called by the QML once the scene is
    // up; the controller keeps the document, the canvas paints it.
    Q_INVOKABLE void attachCanvas(CanvasView* canvas);

    animage::Document& documentForTesting() { return doc_; }
    SceneSettingsModel* sceneSettingsModel() { return &scene_settings_model_; }
    LayersModel* layersModel() const { return layers_model_; }
    TimelineModel* timelineModel() const { return timeline_model_; }
    CtgSourcesModel* ctgSourcesModel() const { return ctg_sources_model_; }

    // --- tests and the interface share these -----------------------------
    // The dialogs are QML's, so the accept handlers call these; Q_INVOKABLE
    // keeps them reachable (and the QML-surface test honest).
    Q_INVOKABLE bool openProjectAt(const QString& folder, QString* error = nullptr);
    Q_INVOKABLE bool exportSequencesTo(const QString& folder, bool layers, bool flattened,
                                       QString* error = nullptr);
    bool waitForColour();
    void onAutosaveTick();

    // --- the state, read by QML -----------------------------------------
    QString title() const;
    bool canUndo() const { return doc_.canUndo(); }
    bool canRedo() const { return doc_.canRedo(); }
    QString statusText() const { return status_text_; }
    bool colourPending() const;
    bool playing() const { return playback_timer_ && playback_timer_->isActive(); }
    int undoDepth() const { return static_cast<int>(doc_.undoDepth()); }

    int zoomPercent() const { return zoom_percent_; }
    int frameCount() const;
    int drawingCount() const;
    int layerCount() const;
    int tileCount() const { return static_cast<int>(doc_.totalTileCount()); }
    int framerate() const { return doc_.scene().framerate; }
    int sceneWidth() const { return doc_.scene().width; }
    int sceneHeight() const { return doc_.scene().height; }
    int currentSlot() const { return current_slot_; }
    // How many frames the drawing in front of you is held for. The frame
    // duration control in the timeline reads and changes this.
    int currentHold() const;
    int onionCount() const { return onion_count_; }

    int tool() const { return erasing_ ? Eraser : Brush; }
    double brushRadius() const;
    bool pressureOpacity() const;
    QColor brushColour() const;
    QColor solidColour() const;
    bool transparentSelected() const;
    bool onColourLayer() const;

    int currentLayerIndex() const;
    int layerOpacity() const;
    bool layerShowScribbles() const;
    bool ctgInherit() const;
    int ctgDirection() const;
    bool ctgFollow() const;

    QString projectName() const;
    bool hasProject() const { return !project_folder_.isEmpty(); }

public Q_SLOTS:
    // --- files -----------------------------------------------------------
    // The verbs QML calls. New/Open/Close route through requestLeave() so the
    // unsaved-changes question is asked the same way every time; Save and
    // Export hand off to the file dialogs the interface owns.
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void exportSequences();
    // Where the dialogs' accepted locations go. The dialogs are QML's; the
    // writing is the controller's, and these slots exist so QML can hand the
    // chosen path over.
    void acceptOpenLocation(const QUrl& url);
    void acceptSaveLocation(const QUrl& url);
    bool saveTo(const QString& folder);
    // The window is being told to close; the controller flushes or asks, and
    // answers with closeRequested when the leaving may happen.
    void requestClose();
    // The answer to a "may I leave?" question the interface raised. One
    // explicit state machine answers for New, Open and Close alike: the
    // requested action was recorded before the question was asked, and Save,
    // Discard and Cancel each know what to do with it.
    void respondSaveDecision(int decision);

    // --- the document ----------------------------------------------------
    void undo();
    void redo();
    void setFramerate(int fps);
    // Tells an export running on this thread to stop; the progress dialog's
    // Cancel button is the only caller.
    void cancelExport();

    // --- the tools -------------------------------------------------------
    void setTool(int tool);
    void setBrushRadius(double radius);
    void nudgeBrushRadius(double factor);
    void setPressureOpacity(bool on);
    void chooseBrushColour(const QColor& colour);
    void chooseSolidColour();
    void chooseTransparent();
    void clearCurrentCel();

    // --- the layers ------------------------------------------------------
    void addLayer();
    void addColourLayer();
    void removeCurrentLayer();
    void moveCurrentLayer(int delta);
    void selectLayerIndex(int index);
    void setLayerOpacity(int percent);
    void beginOpacityDrag();
    void endOpacityDrag();
    void setLayerVisible(int index, bool visible);
    void setLayerLocked(int index, bool locked);
    void setLayerName(int index, const QString& name);
    void setLayerShowScribbles(int index, bool show);
    void setCtgSource(int source_index, bool used);
    void setCtgInherit(bool on);
    void setCtgDirection(int direction);
    void setCtgFollow(bool on);

    // --- the timeline ----------------------------------------------------
    void setCurrentSlot(int slot);
    void stepFrame(int delta);
    void stepDrawing(int direction);
    void insertDrawing();
    void duplicateDrawing();
    void deleteDrawing();
    void holdLonger();
    void holdShorter();
    void togglePlayback();
    void stopPlayback();
    void setOnionCount(int count);
    void setCanvasSize(int width, int height);
    // Scene settings previews write to the scene directly, around the history:
    // nothing is recorded until the dialog is accepted, and the controller
    // puts the scene back on the way out.
    void previewSceneSettings(int framerate, int width, int height);
    void restoreSceneSettings(int framerate, int width, int height);
    void commitSceneSettings(int framerate, int width, int height);

    // --- timeline gestures -----------------------------------------------
    void beginStretch(int run_start_slot);
    void stretchTo(int pointer_x, int cell_width);
    void endStretch();
    void beginTimelineDrag(int slot);
    int timelineDropIndexFor(int pointer_x, int cell_width);
    void endTimelineDrag(int pointer_x, int cell_width);

Q_SIGNALS:
    void stateChanged();
    void viewChanged();
    void toolChanged();
    void brushChanged();
    void brushColourChanged();
    void layerStateChanged();
    void slotChanged();
    void onionChanged();
    void colourPendingChanged();
    void playingChanged();

    // The interface raises these; the controller answers.
    void saveFileDialogRequested();
    void openFolderDialogRequested();
    void exportDialogRequested();
    void sceneSettingsRequested();
    // "The document has never been saved / saving failed: what should I do?"
    void leaveDecisionRequested(const QString& question);
    // The window should close; the controller has finished with the document.
    void closeRequested();

    // Export progress, for a progress dialog.
    void exportProgress(int done, int total);
    void exportFinished(bool ok, const QString& message);

    void statusMessage(const QString& message);

private:
    void buildInitialDocument();
    void afterProjectLoaded();
    bool isDirty() const { return doc_.historyToken() != saved_token_; }
    // The one leave handshake. The action is recorded before anything is
    // asked, so the answer -- Save, Discard or Cancel -- always has a
    // decision to make and no branch can invent an action of its own.
    enum class PendingAction { None, New, Open, Close };
    void requestLeave(PendingAction action);
    void perform(PendingAction action);
    void refreshEverything();
    void syncStatus();
    void updateTitle();
    void rebuildLayerList();
    void onLayerSelected();
    void syncColourControls();
    void syncColourLayerPanel();
    void refreshLayerFlags();
    animage::Layer* currentLayer();
    const animage::Layer* currentLayerConst() const;
    std::string nextLayerName() const;
    std::string nextColourLayerName() const;
    void applyColour(float r, float g, float b);
    bool colourIsTransparent() const;
    std::size_t slotAfterCurrentDrawing() const;
    void onPlaybackTick();
    void scheduleCanvasFrame();

    animage::Document doc_;
    animage::TrackId track_ = animage::kNoId;
    CanvasView* canvas_ = nullptr;

    LayersModel* layers_model_ = nullptr;
    TimelineModel* timeline_model_ = nullptr;
    CtgSourcesModel* ctg_sources_model_ = nullptr;

    QTimer* playback_timer_ = nullptr;
    QTimer* autosave_timer_ = nullptr;
    QElapsedTimer playback_clock_;
    std::size_t playback_start_slot_ = 0;

    QString project_folder_;
    // "Clean" means the document on screen matches the last explicit save.
    // The token is the identity of the edit that was on top of the history
    // when that save happened, so undo-back-to-saved-state is clean and an
    // undo-and-edit branch to the same depth is dirty (the review's branch
    // case). Autosave writes a recovery snapshot but never moves this token:
    // only an explicit save establishes clean.
    std::size_t saved_token_ = 0;
    // The edit the last recovery snapshot captured; a fresh autosave is only
    // written when it has moved. Explicit saves move it too, because the
    // explicit save is a fresher recovery snapshot than any autosave.
    std::size_t autosave_token_ = 0;
    ProjectIO::SaveState save_state_;

    // The leave handshake. `pending_action_` is what the user asked for
    // (New, Open, Close) and was recorded before the question was asked.
    // `save_then_action_` is the same action while a Save As dialog is out,
    // and only carries it across the dialog; a failed save drops it rather
    // than inventing a Close.
    PendingAction pending_action_ = PendingAction::None;
    PendingAction save_then_action_ = PendingAction::None;

    std::size_t current_slot_ = 0;
    int onion_count_ = 0;
    bool erasing_ = false;
    bool export_cancel_ = false;
    std::size_t stretch_run_start_ = 0;
    animage::ImageId drag_image_ = animage::kNoId;

    float colour_r_ = 0.0f, colour_g_ = 0.0f, colour_b_ = 0.0f;
    float solid_r_ = 0.0f, solid_g_ = 0.0f, solid_b_ = 0.0f;

    int zoom_percent_ = 100;
    QString status_text_;

    bool updating_layer_panel_ = false;

    SceneSettingsModel scene_settings_model_;
};
