// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

#include "canvas_widget.h"
#include "color.h"
#include "timeline_widget.h"

using namespace animage;

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("Animage"));
    resize(1400, 900);

    timeline_ = doc_.addTimeline("main");
    const LayerId first = doc_.addLayer(timeline_, "layer 1");
    doc_.insertImage(timeline_, 0);
    doc_.clearHistory();  // an empty scene is the starting point, not an edit

    canvas_ = new CanvasWidget(doc_, this);
    canvas_->setTimeline(timeline_);
    canvas_->setFrame(0);
    canvas_->setActiveLayer(first);
    setCentralWidget(canvas_);

    playback_timer_ = new QTimer(this);
    playback_timer_->setTimerType(Qt::PreciseTimer);
    connect(playback_timer_, &QTimer::timeout, this, &MainWindow::onPlaybackTick);

    buildActions();
    buildLayerPanel();
    buildTimelinePanel();
    buildStatusBar();
    rebuildLayerList();

    connect(canvas_, &CanvasWidget::viewChanged, this, &MainWindow::syncStatus);
    connect(canvas_, &CanvasWidget::documentChanged, this, [this] {
        timeline_widget_->refresh();
        syncStatus();
    });
    syncStatus();

    canvas_->setFocus();
}

void MainWindow::buildActions() {
    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    QAction* undo_action = edit->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, this,
                                           &MainWindow::undo);
    // Ctrl+Shift+Z, not the Ctrl+Y that QKeySequence::Redo gives on Windows.
    QAction* redo_action = edit->addAction(QStringLiteral("&Redo"),
                                           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this,
                                           &MainWindow::redo);
    undo_action->setShortcutContext(Qt::ApplicationShortcut);
    redo_action->setShortcutContext(Qt::ApplicationShortcut);

    edit->addSeparator();
    // The framerate belongs to the scene, not to the timeline: every timeline
    // in the scene runs at it. Putting the control in the timeline panel said
    // otherwise.
    edit->addAction(QStringLiteral("Scene framerate..."), this, &MainWindow::chooseFramerate);

    QMenu* animation = menuBar()->addMenu(QStringLiteral("&Animation"));
    play_action_ = animation->addAction(QStringLiteral("Play"), QKeySequence(Qt::Key_Return), this,
                                        &MainWindow::togglePlayback);
    animation->addSeparator();
    animation->addAction(QStringLiteral("Previous frame"), QKeySequence(Qt::Key_Left), this,
                         [this] { stepFrame(-1); });
    animation->addAction(QStringLiteral("Next frame"), QKeySequence(Qt::Key_Right), this,
                         [this] { stepFrame(1); });
    // Up and down move by drawing, skipping over held frames -- the two
    // questions "what is next in time" and "what is the next drawing" are
    // different, and both get asked constantly.
    animation->addAction(QStringLiteral("Previous drawing"), QKeySequence(Qt::Key_Up), this,
                         [this] { stepDrawing(-1); });
    animation->addAction(QStringLiteral("Next drawing"), QKeySequence(Qt::Key_Down), this,
                         [this] { stepDrawing(1); });
    animation->addSeparator();
    animation->addAction(QStringLiteral("Insert drawing"), QKeySequence(Qt::Key_Insert), this,
                         &MainWindow::insertInterval);
    animation->addAction(QStringLiteral("Duplicate drawing"),
                         QKeySequence(Qt::CTRL | Qt::Key_D), this, &MainWindow::duplicateDrawing);
    animation->addAction(QStringLiteral("Delete drawing"), QKeySequence(Qt::Key_Delete), this,
                         &MainWindow::deleteDrawing);
    animation->addSeparator();
    animation->addAction(QStringLiteral("Hold longer"), QKeySequence(Qt::Key_Plus), this,
                         &MainWindow::extendExposure);
    animation->addAction(QStringLiteral("Hold shorter"), QKeySequence(Qt::Key_Minus), this,
                         &MainWindow::shortenExposure);

    for (QAction* action : animation->actions()) {
        action->setShortcutContext(Qt::ApplicationShortcut);
    }

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
    brush_action_ = tools->addAction(QStringLiteral("Brush"));
    eraser_action_ = tools->addAction(QStringLiteral("Eraser"));
    for (QAction* action : {brush_action_, eraser_action_}) {
        action->setCheckable(true);
        action->setShortcutContext(Qt::ApplicationShortcut);
        mode->addAction(action);
    }
    brush_action_->setChecked(true);
    brush_action_->setShortcut(QKeySequence(Qt::Key_B));
    eraser_action_->setShortcut(QKeySequence(Qt::Key_E));
    connect(brush_action_, &QAction::triggered, this, [this] {
        canvas_->setEraser(false);
        syncToolSettings();
    });
    connect(eraser_action_, &QAction::triggered, this, [this] {
        canvas_->setEraser(true);
        syncToolSettings();
    });

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

    // Pressure driving opacity as well as size suits some hands and not
    // others, exactly as in Photoshop. Size stays on pressure regardless;
    // a brush that does not thin out is not worth having on a tablet.
    pressure_opacity_ = new QCheckBox(QStringLiteral("Pressure → opacity"), this);
    pressure_opacity_->setChecked(canvas_->brushSettings().pressure_affects_opacity);
    // brushSettings() returns the active tool's settings, so this and the size
    // box edit the brush or the eraser depending on which is selected.
    connect(pressure_opacity_, &QCheckBox::toggled, this, [this](bool on) {
        canvas_->brushSettings().pressure_affects_opacity = on;
    });
    tools->addWidget(pressure_opacity_);

    tools->addSeparator();
    auto* colour_button = new QPushButton(QStringLiteral("Colour..."), this);
    connect(colour_button, &QPushButton::clicked, this, &MainWindow::chooseColour);
    tools->addWidget(colour_button);

    // The swatch is a button too: a colour patch is the thing people click.
    colour_swatch_ = new QPushButton(this);
    colour_swatch_->setFixedSize(28, 20);
    colour_swatch_->setToolTip(QStringLiteral("Brush colour"));
    colour_swatch_->setCursor(Qt::PointingHandCursor);
    colour_swatch_->setStyleSheet(QStringLiteral("background:#000000;border:1px solid #888;"));
    connect(colour_swatch_, &QPushButton::clicked, this, &MainWindow::chooseColour);
    tools->addWidget(colour_swatch_);
}

void MainWindow::buildLayerPanel() {
    auto* dock = new QDockWidget(QStringLiteral("Layers"), this);
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);

    layer_list_ = new QListWidget(panel);
    // No keyboard focus: Qt would otherwise use Space to toggle the checked
    // item, and Space is worth more as pan than as a visibility toggle you can
    // hit by accident.
    layer_list_->setFocusPolicy(Qt::NoFocus);
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
    opacity_->setFocusPolicy(Qt::NoFocus);
    // The drag is one command, not one per tick. Nested commands collapse, so
    // opening it on press and closing it on release leaves a single undo step
    // however many values the slider passed through.
    connect(opacity_, &QSlider::sliderPressed, this, &MainWindow::beginOpacityDrag);
    connect(opacity_, &QSlider::sliderReleased, this, &MainWindow::endOpacityDrag);
    connect(opacity_, &QSlider::valueChanged, this, &MainWindow::onOpacityChanged);
    opacity_layout->addWidget(opacity_);
    layout->addWidget(opacity_row);

    const auto panelButton = [&](const QString& text, auto handler) {
        auto* b = new QPushButton(text, panel);
        b->setFocusPolicy(Qt::NoFocus);  // keep Space and the pen with the canvas
        connect(b, &QPushButton::clicked, this, handler);
        layout->addWidget(b);
        return b;
    };

    auto* clear = panelButton(QStringLiteral("Clear this drawing's layer"),
                              &MainWindow::clearCurrentLayer);
    clear->setToolTip(QStringLiteral("Empty the current layer on the current drawing only.\n"
                                     "Other drawings keep theirs."));

    auto* add = panelButton(QStringLiteral("Add layer"), &MainWindow::addLayer);
    (void)add;

    panelButton(QStringLiteral("Remove layer"), &MainWindow::removeCurrentLayer);
    panelButton(QStringLiteral("Move up"), [this] { moveCurrentLayer(-1); });
    panelButton(QStringLiteral("Move down"), [this] { moveCurrentLayer(1); });

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::buildTimelinePanel() {
    auto* dock = new QDockWidget(QStringLiteral("Timeline"), this);
    dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* controls = new QWidget(panel);
    auto* row = new QHBoxLayout(controls);
    row->setContentsMargins(0, 0, 0, 0);

    const auto button = [&](const QString& text, const QString& tip, auto handler) {
        auto* b = new QPushButton(text, controls);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);  // keep the pen working after a click
        connect(b, &QPushButton::clicked, this, handler);
        row->addWidget(b);
        return b;
    };

    play_button_ = button(QStringLiteral("Play"),
                          QStringLiteral("Play the timeline in a loop (Enter)"),
                          &MainWindow::togglePlayback);
    row->addSpacing(12);

    button(QStringLiteral("+ Drawing"),
           QStringLiteral("Insert a new empty drawing after this one (Insert)"),
           &MainWindow::insertInterval);
    button(QStringLiteral("Duplicate"),
           QStringLiteral("Copy this drawing into a new one (Ctrl+D)\n"
                          "A real copy, not a hold: the cels are independent."),
           &MainWindow::duplicateDrawing);
    button(QStringLiteral("Delete drawing"),
           QStringLiteral("Delete this drawing and every frame it is held on (Delete).\n"
                          "To shorten a hold instead, use Hold -."),
           &MainWindow::deleteDrawing);

    row->addSpacing(12);
    button(QStringLiteral("Hold +"),
           QStringLiteral("Hold this drawing one frame longer (+)\n"
                          "Repeats the same drawing; costs nothing."),
           &MainWindow::extendExposure);
    button(QStringLiteral("Hold -"), QStringLiteral("Hold it one frame less (-)"),
           &MainWindow::shortenExposure);

    row->addSpacing(16);
    row->addWidget(new QLabel(QStringLiteral("Onion"), controls));
    onion_before_ = new QSpinBox(controls);
    onion_before_->setRange(0, 5);
    onion_before_->setPrefix(QStringLiteral("-"));
    onion_before_->setToolTip(QStringLiteral("Drawings shown before this one"));
    onion_before_->setFocusPolicy(Qt::NoFocus);
    connect(onion_before_, &QSpinBox::valueChanged, this, &MainWindow::onOnionChanged);
    row->addWidget(onion_before_);

    onion_after_ = new QSpinBox(controls);
    onion_after_->setRange(0, 5);
    onion_after_->setPrefix(QStringLiteral("+"));
    onion_after_->setToolTip(QStringLiteral("Drawings shown after this one"));
    onion_after_->setFocusPolicy(Qt::NoFocus);
    connect(onion_after_, &QSpinBox::valueChanged, this, &MainWindow::onOnionChanged);
    row->addWidget(onion_after_);

    row->addStretch(1);
    layout->addWidget(controls);

    timeline_widget_ = new TimelineWidget(doc_, panel);
    timeline_widget_->setTimeline(timeline_);
    connect(timeline_widget_, &TimelineWidget::currentSlotChanged, this,
            &MainWindow::onSlotChanged);
    connect(timeline_widget_, &TimelineWidget::documentChanged, this,
            &MainWindow::refreshEverything);

    auto* scroll = new QScrollArea(panel);
    scroll->setWidget(timeline_widget_);
    scroll->setWidgetResizable(true);
    scroll->setFixedHeight(96);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(scroll);

    dock->setWidget(panel);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

void MainWindow::buildStatusBar() {
    status_ = new QLabel(this);
    statusBar()->addWidget(status_);
}

void MainWindow::syncStatus() {
    if (!status_) return;
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline) return;

    const std::size_t slot = canvas_->frame();
    const ImageId image = timeline->imageAtSlot(slot);
    status_->setText(
        QStringLiteral("frame %1 / %2   held %3   drawings %4   layers %5   zoom %6%   "
                       "tiles %7   undo %8   %9 fps")
            .arg(slot + 1)
            .arg(timeline->frameCount())
            .arg(timeline->exposureOf(image))
            .arg(timeline->images.size())
            .arg(timeline->layers.size())
            .arg(canvas_->zoom() * 100.0, 0, 'f', 0)
            .arg(doc_.totalTileCount())
            .arg(doc_.undoDepth())
            .arg(doc_.scene().framerate));
}

void MainWindow::refreshEverything() {
    timeline_widget_->refresh();
    canvas_->setFrame(timeline_widget_->currentSlot());
    rebuildLayerList();
    syncStatus();
}

// --- time ----------------------------------------------------------------

void MainWindow::onSlotChanged(std::size_t slot) {
    canvas_->setFrame(slot);
    syncStatus();
}

void MainWindow::stepFrame(int delta) {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline || timeline->slots.empty()) return;

    const int count = static_cast<int>(timeline->slots.size());
    int next = static_cast<int>(timeline_widget_->currentSlot()) + delta;
    next = ((next % count) + count) % count;  // wrap, so scrubbing loops
    timeline_widget_->setCurrentSlot(static_cast<std::size_t>(next));
}

// Moves to the first frame of the previous or next distinct drawing, stepping
// over held frames rather than through them.
void MainWindow::stepDrawing(int direction) {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline || timeline->slots.empty()) return;

    const std::vector<ImageId> neighbours =
        timeline->distinctNeighbours(timeline_widget_->currentSlot(), 1, direction);
    if (neighbours.empty()) return;

    auto it = std::find(timeline->slots.begin(), timeline->slots.end(), neighbours[0]);
    if (it == timeline->slots.end()) return;
    timeline_widget_->setCurrentSlot(
        static_cast<std::size_t>(std::distance(timeline->slots.begin(), it)));
}

void MainWindow::insertInterval() {
    stopPlayback();
    doc_.insertImage(timeline_, timeline_widget_->currentSlot() + 1);
    timeline_widget_->refresh();
    timeline_widget_->setCurrentSlot(timeline_widget_->currentSlot() + 1);
    refreshEverything();
}

void MainWindow::duplicateDrawing() {
    stopPlayback();
    doc_.duplicateImage(timeline_, timeline_widget_->currentSlot());
    timeline_widget_->refresh();
    timeline_widget_->setCurrentSlot(timeline_widget_->currentSlot() + 1);
    refreshEverything();
}

// Deletes the drawing and every frame it is held on. Shortening a hold is a
// different operation and lives on Hold -.
void MainWindow::deleteDrawing() {
    stopPlayback();
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline) return;

    const ImageId image = timeline->imageAtSlot(timeline_widget_->currentSlot());
    if (image == kNoId) return;
    if (timeline->exposureOf(image) >= timeline->slots.size()) {
        return;  // it is the only drawing; leave something to draw on
    }

    doc_.removeDrawing(timeline_, image);
    refreshEverything();
}

void MainWindow::extendExposure() {
    doc_.extendExposure(timeline_, timeline_widget_->currentSlot(), 1);
    refreshEverything();
}

void MainWindow::shortenExposure() {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline) return;
    const std::size_t slot = timeline_widget_->currentSlot();
    const ImageId image = timeline->imageAtSlot(slot);
    if (timeline->exposureOf(image) <= 1) return;  // that would delete the drawing
    doc_.removeSlot(timeline_, slot);
    refreshEverything();
}

void MainWindow::setFramerate(int fps) {
    doc_.setFramerate(fps);
    if (playback_timer_->isActive()) {
        playback_clock_.restart();
        playback_start_slot_ = timeline_widget_->currentSlot();
    }
    syncStatus();
}

void MainWindow::chooseFramerate() {
    bool accepted = false;
    const int fps = QInputDialog::getInt(this, QStringLiteral("Scene framerate"),
                                         QStringLiteral("Frames per second:"),
                                         doc_.scene().framerate, 1, 120, 1, &accepted);
    if (accepted) setFramerate(fps);
}

void MainWindow::togglePlayback() {
    if (playback_timer_->isActive()) {
        stopPlayback();
        return;
    }

    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline || timeline->slots.size() < 2) return;

    playback_start_slot_ = timeline_widget_->currentSlot();
    playback_clock_.start();
    canvas_->setPlaying(true);
    playback_timer_->start(1);
    if (play_action_) play_action_->setText(QStringLiteral("Stop"));
    if (play_button_) play_button_->setText(QStringLiteral("Stop"));
    syncStatus();
}

void MainWindow::stopPlayback() {
    if (!playback_timer_->isActive()) return;
    playback_timer_->stop();
    canvas_->setPlaying(false);
    if (play_action_) play_action_->setText(QStringLiteral("Play"));
    if (play_button_) play_button_->setText(QStringLiteral("Play"));
    canvas_->setFrame(timeline_widget_->currentSlot());
    syncStatus();
}

// Driven by elapsed time rather than by counting ticks. A timer that fires late
// must not make the whole take run slow, which is exactly the thing playback
// exists to let you judge.
void MainWindow::onPlaybackTick() {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline || timeline->slots.empty()) {
        stopPlayback();
        return;
    }

    const int fps = std::max(1, doc_.scene().framerate);
    const qint64 elapsed = playback_clock_.elapsed();
    const qint64 advanced = elapsed * fps / 1000;
    const std::size_t count = timeline->slots.size();
    const std::size_t slot =
        (playback_start_slot_ + static_cast<std::size_t>(advanced)) % count;

    if (slot == timeline_widget_->currentSlot()) return;
    timeline_widget_->setCurrentSlot(slot);
}

void MainWindow::onOnionChanged() {
    CanvasWidget::OnionSettings settings = canvas_->onion();
    settings.before = onion_before_->value();
    settings.after = onion_after_->value();
    canvas_->setOnion(settings);
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

void MainWindow::beginOpacityDrag() { doc_.beginCommand("Layer opacity"); }

void MainWindow::endOpacityDrag() {
    doc_.endCommand();
    syncStatus();
}

void MainWindow::onOpacityChanged(int percent) {
    Layer* layer = currentLayer();
    if (!layer) return;
    Layer updated = *layer;
    updated.opacity = static_cast<float>(percent) / 100.0f;
    doc_.updateLayer(timeline_, updated.id, updated);
    // refreshAll only marks the cache dirty; the composite happens once, in the
    // next paint. Doing it here meant one full-viewport flatten per slider tick.
    canvas_->refreshAll();
}

void MainWindow::clearCurrentLayer() {
    stopPlayback();
    Layer* layer = currentLayer();
    if (!layer) return;
    doc_.clearCel(timeline_, canvas_->currentImage(), layer->id);
    canvas_->refreshAll();
    syncStatus();
}

// The lowest unused "layer N". Reusing a number after a deletion would give
// two layers the same name, which makes the panel ambiguous.
std::string MainWindow::nextLayerName() const {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline) return "layer 1";

    for (std::size_t n = 1;; ++n) {
        const std::string candidate = "layer " + std::to_string(n);
        const bool taken = std::any_of(
            timeline->layers.begin(), timeline->layers.end(),
            [&](const Layer& l) { return l.name == candidate; });
        if (!taken) return candidate;
    }
}

void MainWindow::syncToolSettings() {
    const BrushSettings& settings = canvas_->brushSettings();
    if (radius_) {
        const QSignalBlocker block(radius_);
        radius_->setValue(settings.radius);
    }
    if (pressure_opacity_) {
        const QSignalBlocker block(pressure_opacity_);
        pressure_opacity_->setChecked(settings.pressure_affects_opacity);
    }
}

void MainWindow::addLayer() {
    const Timeline* timeline = doc_.scene().findTimeline(timeline_);
    if (!timeline) return;

    const int row = layer_list_ ? std::max(0, layer_list_->currentRow()) : 0;
    const LayerId created = doc_.addLayer(timeline_, nextLayerName(),
                                          static_cast<std::size_t>(row));
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
    stopPlayback();
    if (!doc_.undo()) return;
    refreshEverything();
}

void MainWindow::redo() {
    stopPlayback();
    if (!doc_.redo()) return;
    refreshEverything();
}
