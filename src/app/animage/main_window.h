// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>

#include "document.h"

class CanvasWidget;
class QListWidget;
class QListWidgetItem;
class QSlider;
class QLabel;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;

// M2: one image, one timeline, no timeline UI yet. Enough to find out whether
// the layer model is pleasant to draw on before building time on top of it.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

private:
    void buildActions();
    void buildLayerPanel();
    void buildStatusBar();

    void rebuildLayerList();
    void syncStatus();

    void addLayer();
    void removeCurrentLayer();
    void moveCurrentLayer(int delta);
    void onLayerSelected();
    void onLayerItemChanged(QListWidgetItem* item);
    void onOpacityChanged(int percent);
    void chooseColour();
    void setBrushRadius(double radius);
    void nudgeBrushRadius(double factor);
    void undo();
    void redo();

    animage::Layer* currentLayer();

    animage::Document doc_;
    animage::TimelineId timeline_ = animage::kNoId;
    animage::ImageId image_ = animage::kNoId;

    CanvasWidget* canvas_ = nullptr;
    QListWidget* layer_list_ = nullptr;
    QSlider* opacity_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* colour_swatch_ = nullptr;
    QCheckBox* pressure_opacity_ = nullptr;

    bool updating_list_ = false;
    float colour_r_ = 0.0f, colour_g_ = 0.0f, colour_b_ = 0.0f;
};
