// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "canvas_widget.h"
#include "color.h"

using namespace animage;

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("Animage"));
    resize(1400, 900);

    timeline_ = doc_.addTimeline("main");
    const LayerId rough = doc_.addLayer(timeline_, "rough");
    image_ = doc_.insertImage(timeline_, 0);

    canvas_ = new CanvasWidget(doc_, this);
    canvas_->setTarget(timeline_, image_);
    canvas_->setActiveLayer(rough);
    setCentralWidget(canvas_);

    buildActions();
    buildLayerPanel();
    buildStatusBar();
    rebuildLayerList();

    connect(canvas_, &CanvasWidget::viewChanged, this, &MainWindow::syncStatus);
    connect(canvas_, &CanvasWidget::documentChanged, this, &MainWindow::syncStatus);
    syncStatus();

    canvas_->setFocus();
}

void MainWindow::buildActions() {
    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    QAction* undo_action = edit->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, this,
                                           &MainWindow::undo);
    QAction* redo_action = edit->addAction(QStringLiteral("&Redo"), QKeySequence::Redo, this,
                                           &MainWindow::redo);
    undo_action->setShortcutContext(Qt::ApplicationShortcut);
    redo_action->setShortcutContext(Qt::ApplicationShortcut);

    QMenu* view = menuBar()->addMenu(QStringLiteral("&View"));
    view->addAction(QStringLiteral("Actual size"), QKeySequence(Qt::Key_1), canvas_,
                    &CanvasWidget::resetView);
    view->addAction(QStringLiteral("Fit drawing"), QKeySequence(Qt::Key_0), canvas_,
                    &CanvasWidget::fitToDrawing);
    view->addSeparator();

    auto* backgrounds = new QActionGroup(this);
    const std::pair<const char*, CanvasWidget::Background> options[] = {
        {"Paper (white)", CanvasWidget::Background::White},
        {"Transparency (checker)", CanvasWidget::Background::Checker},
        {"Light table (black)", CanvasWidget::Background::Black},
    };
    for (const auto& [label, mode] : options) {
        QAction* action = view->addAction(QString::fromUtf8(label));
        action->setCheckable(true);
        action->setChecked(mode == CanvasWidget::Background::White);
        backgrounds->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] { canvas_->setBackground(mode); });
    }

    QToolBar* tools = addToolBar(QStringLiteral("Tools"));
    tools->setMovable(false);

    auto* mode = new QActionGroup(this);
    QAction* draw = tools->addAction(QStringLiteral("Brush"));
    QAction* erase = tools->addAction(QStringLiteral("Eraser"));
    for (QAction* action : {draw, erase}) {
        action->setCheckable(true);
        mode->addAction(action);
    }
    draw->setChecked(true);
    draw->setShortcut(QKeySequence(Qt::Key_B));
    erase->setShortcut(QKeySequence(Qt::Key_E));
    connect(draw, &QAction::triggered, this, [this] { canvas_->setEraser(false); });
    connect(erase, &QAction::triggered, this, [this] { canvas_->setEraser(true); });

    tools->addSeparator();
    tools->addWidget(new QLabel(QStringLiteral(" Size ")));
    radius_ = new QDoubleSpinBox(this);
    radius_->setRange(0.5, 400.0);
    radius_->setDecimals(1);
    radius_->setSingleStep(1.0);
    radius_->setValue(canvas_->brushSettings().radius);
    connect(radius_, &QDoubleSpinBox::valueChanged, this, &MainWindow::setBrushRadius);
    tools->addWidget(radius_);

    // [ and ] are what every drawing program uses; not having them is jarring.
    auto* smaller = new QAction(this);
    smaller->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    smaller->setShortcutContext(Qt::ApplicationShortcut);
    connect(smaller, &QAction::triggered, this, [this] { nudgeBrushRadius(1.0 / 1.25); });
    addAction(smaller);

    auto* larger = new QAction(this);
    larger->setShortcut(QKeySequence(Qt::Key_BracketRight));
    larger->setShortcutContext(Qt::ApplicationShortcut);
    connect(larger, &QAction::triggered, this, [this] { nudgeBrushRadius(1.25); });
    addAction(larger);

    tools->addSeparator();
    auto* colour_button = new QPushButton(QStringLiteral("Colour..."), this);
    connect(colour_button, &QPushButton::clicked, this, &MainWindow::chooseColour);
    tools->addWidget(colour_button);

    colour_swatch_ = new QLabel(this);
    colour_swatch_->setFixedSize(28, 20);
    colour_swatch_->setAutoFillBackground(true);
    colour_swatch_->setStyleSheet(QStringLiteral("background:#000000;border:1px solid #888;"));
    tools->addWidget(colour_swatch_);
}

void MainWindow::buildLayerPanel() {
    auto* dock = new QDockWidget(QStringLiteral("Layers"), this);
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);

    layer_list_ = new QListWidget(panel);
    layout->addWidget(layer_list_, 1);
    connect(layer_list_, &QListWidget::currentRowChanged, this, &MainWindow::onLayerSelected);
    connect(layer_list_, &QListWidget::itemChanged, this, &MainWindow::onLayerItemChanged);

    auto* opacity_row = new QWidget(panel);
    auto* opacity_layout = new QVBoxLayout(opacity_row);
    opacity_layout->setContentsMargins(0, 0, 0, 0);
    opacity_layout->addWidget(new QLabel(QStringLiteral("Layer opacity"), opacity_row));
    opacity_ = new QSlider(Qt::Horizontal, opacity_row);
    opacity_->setRange(0, 100);
    opacity_->setValue(100);
    connect(opacity_, &QSlider::valueChanged, this, &MainWindow::onOpacityChanged);
    opacity_layout->addWidget(opacity_);
    layout->addWidget(opacity_row);

    auto* add = new QPushButton(QStringLiteral("Add layer"), panel);
    connect(add, &QPushButton::clicked, this, &MainWindow::addLayer);
    layout->addWidget(add);

    auto* remove = new QPushButton(QStringLiteral("Remove layer"), panel);
    connect(remove, &QPushButton::clicked, this, &MainWindow::removeCurrentLayer);
    layout->addWidget(remove);

    auto* up = new QPushButton(QStringLiteral("Move up"), panel);
    connect(up, &QPushButton::clicked, this, [this] { moveCurrentLayer(-1); });
    layout->addWidget(up);

    auto* down = new QPushButton(QStringLiteral("Move down"), panel);
    connect(down, &QPushButton::clicked, this, [this] { moveCurrentLayer(1); });
    layout->addWidget(down);

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::buildStatusBar() {
    status_ = new QLabel(this);
    statusBar()->addWidget(status_);
}

void MainWindow::syncStatus() {
    if (!status_) return;
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    const std::size_t layers = timeline ? timeline->layers.size() : 0;
    status_->setText(
        QStringLiteral("zoom %1%   layers %2   tiles %3   undo %4")
            .arg(canvas_->zoom() * 100.0, 0, 'f', 0)
            .arg(layers)
            .arg(doc_.totalTileCount())
            .arg(doc_.undoDepth()));
}

Layer* MainWindow::currentLayer() {
    Timeline* timeline = doc_.mutableScene().findTimeline(timeline_);
    if (!timeline) return nullptr;
    const int row = layer_list_ ? layer_list_->currentRow() : -1;
    if (row < 0 || static_cast<std::size_t>(row) >= timeline->layers.size()) return nullptr;
    return &timeline->layers[static_cast<std::size_t>(row)];
}

void MainWindow::rebuildLayerList() {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline || !layer_list_) return;

    const LayerId active = canvas_->activeLayer();

    updating_list_ = true;
    layer_list_->clear();
    int active_row = 0;
    for (std::size_t i = 0; i < timeline->layers.size(); ++i) {
        const Layer& layer = timeline->layers[i];
        auto* item = new QListWidgetItem(QString::fromStdString(layer.name), layer_list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
        if (layer.id == active) active_row = static_cast<int>(i);
    }
    layer_list_->setCurrentRow(active_row);
    updating_list_ = false;

    onLayerSelected();
    syncStatus();
}

void MainWindow::onLayerSelected() {
    Layer* layer = currentLayer();
    if (!layer) return;
    canvas_->setActiveLayer(layer->id);
    if (opacity_) {
        const QSignalBlocker block(opacity_);
        opacity_->setValue(static_cast<int>(layer->opacity * 100.0f));
    }
}

void MainWindow::onLayerItemChanged(QListWidgetItem* item) {
    if (updating_list_ || !item) return;
    Timeline* timeline = doc_.mutableScene().findTimeline(timeline_);
    if (!timeline) return;

    const int row = layer_list_->row(item);
    if (row < 0 || static_cast<std::size_t>(row) >= timeline->layers.size()) return;

    Layer updated = timeline->layers[static_cast<std::size_t>(row)];
    updated.visible = item->checkState() == Qt::Checked;
    doc_.updateLayer(timeline_, updated.id, updated);

    canvas_->refreshAll();
    syncStatus();
}

void MainWindow::onOpacityChanged(int percent) {
    Layer* layer = currentLayer();
    if (!layer) return;
    Layer updated = *layer;
    updated.opacity = static_cast<float>(percent) / 100.0f;
    doc_.updateLayer(timeline_, updated.id, updated);
    canvas_->refreshAll();
    syncStatus();
}

void MainWindow::addLayer() {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline) return;

    const int row = layer_list_ ? std::max(0, layer_list_->currentRow()) : 0;
    const std::string name = "layer " + std::to_string(timeline->layers.size() + 1);
    const LayerId created = doc_.addLayer(timeline_, name, static_cast<std::size_t>(row));
    canvas_->setActiveLayer(created);
    rebuildLayerList();
    canvas_->refreshAll();
}

void MainWindow::removeCurrentLayer() {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline || timeline->layers.size() <= 1) return;  // never leave nothing to draw on
    Layer* layer = currentLayer();
    if (!layer) return;

    doc_.removeLayer(timeline_, layer->id);
    const Timeline* after = doc_.scene().findTimeline(timeline_);
    if (after && !after->layers.empty()) canvas_->setActiveLayer(after->layers.front().id);
    rebuildLayerList();
    canvas_->refreshAll();
}

void MainWindow::moveCurrentLayer(int delta) {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline || !layer_list_) return;

    const int from = layer_list_->currentRow();
    const int to = from + delta;
    if (from < 0 || to < 0 || static_cast<std::size_t>(to) >= timeline->layers.size()) return;

    doc_.moveLayer(timeline_, static_cast<std::size_t>(from), static_cast<std::size_t>(to));
    rebuildLayerList();
    layer_list_->setCurrentRow(to);
    canvas_->refreshAll();
}

void MainWindow::chooseColour() {
    // The dialog speaks sRGB; the document works in linear light.
    const QColor initial = QColor::fromRgbF(linearToSrgb(colour_r_), linearToSrgb(colour_g_),
                                            linearToSrgb(colour_b_));
    const QColor chosen = QColorDialog::getColor(initial, this, QStringLiteral("Brush colour"));
    if (!chosen.isValid()) return;

    colour_r_ = srgbToLinear(static_cast<float>(chosen.redF()));
    colour_g_ = srgbToLinear(static_cast<float>(chosen.greenF()));
    colour_b_ = srgbToLinear(static_cast<float>(chosen.blueF()));

    BrushSettings& settings = canvas_->brushSettings();
    settings.r = colour_r_;
    settings.g = colour_g_;
    settings.b = colour_b_;

    colour_swatch_->setStyleSheet(
        QStringLiteral("background:%1;border:1px solid #888;").arg(chosen.name()));
}

void MainWindow::setBrushRadius(double radius) {
    canvas_->brushSettings().radius = static_cast<float>(radius);
}

void MainWindow::nudgeBrushRadius(double factor) {
    if (!radius_) return;
    radius_->setValue(radius_->value() * factor);
}

void MainWindow::undo() {
    if (!doc_.undo()) return;
    rebuildLayerList();
    canvas_->refreshAll();
}

void MainWindow::redo() {
    if (!doc_.redo()) return;
    rebuildLayerList();
    canvas_->refreshAll();
}
