// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QMainWindow>

#include "document.h"

class CanvasWidget;
class TimelineWidget;
class QListWidget;
class QListWidgetItem;
class QSlider;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QAction;
class QTimer;

// M3: the timeline is on screen. One timeline, no saving yet.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildActions();
    void buildLayerPanel();
    void buildTimelinePanel();
    void buildStatusBar();

    void rebuildLayerList();
    void syncStatus();
    void refreshEverything();

    void addLayer();
    void removeCurrentLayer();
    void moveCurrentLayer(int delta);
    void onLayerSelected();
    void onLayerItemChanged(QListWidgetItem* item);
    void onOpacityChanged(int percent);
    void beginOpacityDrag();
    void endOpacityDrag();
    void clearCurrentLayer();
    std::string nextLayerName() const;
    void chooseColour();
    void syncToolSettings();
    void setBrushRadius(double radius);
    void nudgeBrushRadius(double factor);
    void undo();
    void redo();

    void onSlotChanged(std::size_t slot);
    void stepFrame(int delta);
    void stepDrawing(int direction);
    void insertInterval();
    void duplicateDrawing();
    void deleteDrawing();
    void extendExposure();
    void shortenExposure();
    void chooseFramerate();

    void togglePlayback();
    void stopPlayback();
    void onPlaybackTick();
    void onOnionChanged();
    void setFramerate(int fps);

    animage::Layer* currentLayer();

    animage::Document doc_;
    animage::TimelineId timeline_ = animage::kNoId;

    CanvasWidget* canvas_ = nullptr;
    TimelineWidget* timeline_widget_ = nullptr;
    QListWidget* layer_list_ = nullptr;
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

    bool updating_list_ = false;
    float colour_r_ = 0.0f, colour_g_ = 0.0f, colour_b_ = 0.0f;
};
