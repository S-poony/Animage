// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QMainWindow>

#include "document.h"

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

// M3: the timeline is on screen. One track, no saving yet.
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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

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
    // The one place a colour becomes the brush's, whether it came from the
    // dialog or from the eyedropper. Linear light, straight.
    void applyColour(float r, float g, float b);
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

    void openProject();
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
    QCheckBox* pressure_opacity_ = nullptr;
    QSpinBox* onion_ = nullptr;
    QAction* play_action_ = nullptr;
    QPushButton* play_button_ = nullptr;
    QAction* brush_action_ = nullptr;
    QAction* eraser_action_ = nullptr;

    QTimer* playback_timer_ = nullptr;
    QElapsedTimer playback_clock_;
    std::size_t playback_start_slot_ = 0;

    // Empty until the project has been saved somewhere. `saved_undo_depth_` is
    // where the history stood when it was last written, which is a cheap and
    // honest test for "changed since": undoing back to it counts as unchanged,
    // because it is.
    QString project_folder_;
    std::size_t saved_undo_depth_ = 0;

    bool updating_list_ = false;
    bool forwarding_key_ = false;
    bool framed_once_ = false;
    float colour_r_ = 0.0f, colour_g_ = 0.0f, colour_b_ = 0.0f;
};
