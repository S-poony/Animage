// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>

#include "canvas_widget.h"
#include "project_files.h"
#include "color.h"
#include "scene_settings_dialog.h"
#include "timeline_widget.h"

using namespace animage;

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("Animage"));
    resize(1400, 900);

    track_ = doc_.addTrack("main");
    const LayerId first = doc_.addLayer(track_, "layer 1");
    doc_.insertImage(track_, 0);
    doc_.clearHistory();  // an empty scene is the starting point, not an edit

    canvas_ = new CanvasWidget(doc_, this);
    canvas_->setTrack(track_);
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

    connect(canvas_, &CanvasWidget::brushSizeChanged, this, [this](double radius) {
        if (!radius_) return;
        const QSignalBlocker block(radius_);
        radius_->setValue(radius);
    });
    connect(canvas_, &CanvasWidget::colourPicked, this, &MainWindow::applyColour);
    connect(canvas_, &CanvasWidget::viewChanged, this, &MainWindow::syncStatus);
    connect(canvas_, &CanvasWidget::documentChanged, this, [this] {
        timeline_widget_->refresh();
        syncStatus();
    });
    setWindowTitle(QStringLiteral("Untitled - Animage"));
    syncStatus();

    // Space and Z are held modifiers for panning and zooming, not shortcuts, so
    // QAction cannot express them. Without this filter they stop working the
    // moment anything else takes focus -- clicking a checkbox was enough -- and
    // worse, Space then operates whatever widget it landed on.
    qApp->installEventFilter(this);

    canvas_->setFocus();
}

MainWindow::~MainWindow() {
    // The filter is installed on the application, which outlives this window.
    // Removing it here rather than letting ~QObject do it keeps it from seeing
    // events while the children it forwards to are already being destroyed.
    qApp->removeEventFilter(this);
}

// Opened framed on the canvas rather than at one-to-one in a corner of it: the
// first thing to see is the picture you are making.
//
// Queued rather than done here, and certainly not in the constructor. The fit
// is computed from the size of the canvas widget, and that size is not final
// until the layout has run and the docks have taken their share: from the
// constructor it asked for five percent, and from showEvent it was still wide
// enough to push the right-hand edge of the picture off screen. A zero-delay
// timer runs once every pending resize has been delivered.
void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (framed_once_) return;
    framed_once_ = true;
    QTimer::singleShot(0, this, [this] { canvas_->fitToCanvas(); });
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    const bool key_event = event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease;
    if (!key_event || !canvas_) return QMainWindow::eventFilter(watched, event);

    // An application-wide filter also sees the events this filter itself sends,
    // and it sees them again when an unaccepted key propagates to a parent
    // widget. Either route leads straight back here, so the guard has to be a
    // re-entrancy flag rather than a check on the receiver: watching for
    // `watched == canvas_` alone missed the propagation route and recursed
    // until the stack ran out.
    if (forwarding_key_ || watched == canvas_) {
        return QMainWindow::eventFilter(watched, event);
    }

    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() != Qt::Key_Space && key->key() != Qt::Key_Z) {
        return QMainWindow::eventFilter(watched, event);
    }
    // Ctrl+Z and friends are shortcuts and must be left alone.
    if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return QMainWindow::eventFilter(watched, event);
    }

    forwarding_key_ = true;
    QCoreApplication::sendEvent(canvas_, key);
    forwarding_key_ = false;
    return true;
}

void MainWindow::buildActions() {
    QMenu* file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("&Open..."), QKeySequence::Open, this,
                    &MainWindow::openProject);
    file->addAction(QStringLiteral("&Save"), QKeySequence::Save, this,
                    &MainWindow::saveProject);
    file->addAction(QStringLiteral("Save &As..."), QKeySequence::SaveAs, this,
                    &MainWindow::saveProjectAs);
    file->addSeparator();
    // Visible and disabled, because a menu that grows an item later tells you
    // less than one that says what is coming.
    QAction* exporting = file->addAction(QStringLiteral("&Export sequence..."));
    exporting->setEnabled(false);
    exporting->setToolTip(QStringLiteral("Not built yet"));
    for (QAction* action : file->actions()) action->setShortcutContext(Qt::ApplicationShortcut);

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
    // The framerate and the canvas both belong to the scene, not to a track:
    // every track in the scene runs at one framerate and is composited into one
    // picture. Putting either in the timeline panel said otherwise.
    edit->addAction(QStringLiteral("Scene settings..."), this, &MainWindow::chooseSceneSettings);

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
    view->addAction(QStringLiteral("Fit canvas"), QKeySequence(Qt::Key_0), canvas_,
                    &CanvasWidget::fitToCanvas);
    // The drawing is not the canvas, and both are worth being able to frame:
    // one is what you are delivering, the other is everything you have made,
    // including whatever ran off the edge.
    view->addAction(QStringLiteral("Fit drawing"), QKeySequence(Qt::SHIFT | Qt::Key_0), canvas_,
                    &CanvasWidget::fitToDrawing);
    view->addSeparator();

    auto* backgrounds = new QActionGroup(this);
    const std::pair<const char*, CanvasWidget::Background> options[] = {
        {"Paper (white)", CanvasWidget::Background::White},
        {"Transparency (checker)", CanvasWidget::Background::Checker},
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
    // ClickFocus, not the default WheelFocus: scrolling over the toolbar on the
    // way somewhere else should not hand it the keyboard.
    radius_->setFocusPolicy(Qt::ClickFocus);
    connect(radius_, &QDoubleSpinBox::valueChanged, this, &MainWindow::setBrushRadius);
    connect(radius_, &QDoubleSpinBox::editingFinished, this, [this] { canvas_->setFocus(); });
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
    colour_swatch_->setToolTip(QStringLiteral("Brush colour.\n"
                                              "Alt+click on the drawing picks the colour there."));
    colour_swatch_->setCursor(Qt::PointingHandCursor);
    colour_swatch_->setStyleSheet(QStringLiteral("background:#000000;border:1px solid #888;"));
    connect(colour_swatch_, &QPushButton::clicked, this, &MainWindow::chooseColour);
    tools->addWidget(colour_swatch_);

    tools->addSeparator();
    // This acts on the drawing in front of you, not on the layer as a whole, so
    // it belongs with the drawing tools rather than in the layer panel.
    auto* clear = new QPushButton(QStringLiteral("Clear"), this);
    clear->setToolTip(QStringLiteral("Empty the current layer on this drawing only.\n"
                                     "Other drawings keep theirs."));
    clear->setFocusPolicy(Qt::NoFocus);
    connect(clear, &QPushButton::clicked, this, &MainWindow::clearCurrentLayer);
    tools->addWidget(clear);
}

void MainWindow::buildLayerPanel() {
    auto* dock = new QDockWidget(QStringLiteral("Layers"), this);
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);

    // Two columns rather than one, because a colour layer needs two switches:
    // whether it is shown at all, and whether what is shown is the fill or the
    // marks that generated it.
    //
    // Both are the item's own check indicators. The second one used to be a
    // QCheckBox in a widget set on the row, and that quietly broke the first
    // one: an index widget counts as a persistent editor, so the view routes
    // the press into it instead of to the delegate and the row's own tick stops
    // responding. A colour layer could not be hidden at all.
    layer_list_ = new QTreeWidget(panel);
    layer_list_->setColumnCount(2);
    layer_list_->setHeaderLabels({QStringLiteral("Layer"), QStringLiteral("Marks")});
    layer_list_->setRootIsDecorated(false);
    layer_list_->setUniformRowHeights(true);
    layer_list_->header()->setStretchLastSection(false);
    layer_list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layer_list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    // No keyboard focus: Qt would otherwise use Space to toggle the checked
    // item, and Space is worth more as pan than as a visibility toggle you can
    // hit by accident.
    layer_list_->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(layer_list_, 1);
    connect(layer_list_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) { onLayerSelected(); });
    connect(layer_list_, &QTreeWidget::itemChanged, this, &MainWindow::onLayerItemChanged);

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


    panelButton(QStringLiteral("Add layer"), &MainWindow::addLayer);

    auto* colour_layer =
        panelButton(QStringLiteral("Add colour layer"), &MainWindow::addColourLayer);
    colour_layer->setToolTip(
        QStringLiteral("A layer that holds scribbles rather than colour.\n"
                       "Scrawl roughly inside a region with the ordinary brush and the\n"
                       "whole region takes that colour, gaps in the line art included."));

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
    // One number either side. Asymmetric onion skin is a real thing but a rare
    // one, and making it the default meant two controls to set every time.
    onion_ = new QSpinBox(controls);
    onion_->setRange(0, 5);
    onion_->setToolTip(QStringLiteral("Drawings shown either side of this one"));
    onion_->setFocusPolicy(Qt::ClickFocus);
    connect(onion_, &QSpinBox::valueChanged, this, &MainWindow::onOnionChanged);
    connect(onion_, &QSpinBox::editingFinished, this, [this] { canvas_->setFocus(); });
    row->addWidget(onion_);

    row->addStretch(1);
    layout->addWidget(controls);

    timeline_widget_ = new TimelineWidget(doc_, panel);
    timeline_widget_->setTrack(track_);
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

// --- files ---------------------------------------------------------------

void MainWindow::updateTitle() {
    const QString name = project_folder_.isEmpty()
                             ? QStringLiteral("Untitled")
                             : QFileInfo(project_folder_).fileName();
    const bool changed = doc_.undoDepth() != saved_undo_depth_;
    setWindowTitle(QStringLiteral("%1%2 - Animage").arg(name, changed ? QStringLiteral("*")
                                                                     : QString()));
}

void MainWindow::openProject() {
    stopPlayback();
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Open project"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (folder.isEmpty()) return;

    QString error;
    if (!openProjectAt(folder, &error)) {
        // The open document is untouched -- project::load builds a new one and
        // only swaps it in once every cel has come back -- so this is a plain
        // refusal and nothing has been lost.
        QMessageBox::warning(this, QStringLiteral("Cannot open that project"), error);
    }
}

bool MainWindow::openProjectAt(const QString& folder, QString* error) {
    if (!project::load(doc_, folder, error)) return false;
    project_folder_ = folder;
    afterProjectLoaded();
    return true;
}

void MainWindow::afterProjectLoaded() {
    const Scene& scene = doc_.scene();
    track_ = scene.tracks.empty() ? kNoId : scene.tracks.front().id;

    canvas_->setTrack(track_);
    timeline_widget_->setTrack(track_);
    timeline_widget_->setCurrentSlot(0);
    canvas_->setFrame(0);

    const Track* track = doc_.scene().findTrack(track_);
    if (track && !track->layers.empty()) canvas_->setActiveLayer(track->layers.front().id);

    rebuildLayerList();
    timeline_widget_->refresh();
    canvas_->refreshAll();
    canvas_->fitToCanvas();

    saved_undo_depth_ = doc_.undoDepth();
    syncStatus();
    updateTitle();
}

bool MainWindow::saveTo(const QString& folder) {
    QString error;
    if (!project::save(doc_, folder, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot save"), error);
        return false;
    }
    project_folder_ = folder;
    saved_undo_depth_ = doc_.undoDepth();
    updateTitle();
    statusBar()->showMessage(QStringLiteral("Saved to %1").arg(folder), 4000);
    return true;
}

void MainWindow::saveProject() {
    // A document that has never been saved has nowhere to go, so Save asks --
    // which is Save As by another name, and the reason it is not a separate
    // concept internally.
    if (project_folder_.isEmpty()) {
        saveProjectAs();
        return;
    }
    saveTo(project_folder_);
}

void MainWindow::saveProjectAs() {
    stopPlayback();
    QString chosen = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save project as"), project_folder_,
        QStringLiteral("Animage project (*%1)").arg(project::folderSuffix()));
    if (chosen.isEmpty()) return;
    if (!chosen.endsWith(project::folderSuffix())) chosen += project::folderSuffix();

    // A project is a folder, and the file dialog has just offered to replace it
    // as though it were a file. It has not created anything; project::save
    // builds alongside and swaps, so an existing project is only replaced once
    // the new one is complete.
    saveTo(chosen);
}

void MainWindow::buildStatusBar() {
    status_ = new QLabel(this);
    statusBar()->addWidget(status_);
}

void MainWindow::syncStatus() {
    // Every path that changes the document ends up here, which makes it the one
    // place the title's "changed since saved" mark can be kept honest without
    // threading a signal through everything.
    updateTitle();
    if (!status_) return;
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;

    const std::size_t slot = canvas_->frame();
    const ImageId image = track->imageAtSlot(slot);
    status_->setText(
        QStringLiteral("frame %1 / %2   held %3   drawings %4   layers %5   zoom %6%   "
                       "tiles %7   undo %8   %9 fps")
            .arg(slot + 1)
            .arg(track->frameCount())
            .arg(track->exposureOf(image))
            .arg(track->images.size())
            .arg(track->layers.size())
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
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->slots.empty()) return;

    const int count = static_cast<int>(track->slots.size());
    int next = static_cast<int>(timeline_widget_->currentSlot()) + delta;
    next = ((next % count) + count) % count;  // wrap, so scrubbing loops
    timeline_widget_->setCurrentSlot(static_cast<std::size_t>(next));
}

// Moves to the first frame of the previous or next distinct drawing, stepping
// over held frames rather than through them.
void MainWindow::stepDrawing(int direction) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->slots.empty()) return;

    const std::vector<ImageId> neighbours =
        track->distinctNeighbours(timeline_widget_->currentSlot(), 1, direction);
    if (neighbours.empty()) return;

    auto it = std::find(track->slots.begin(), track->slots.end(), neighbours[0]);
    if (it == track->slots.end()) return;
    timeline_widget_->setCurrentSlot(
        static_cast<std::size_t>(std::distance(track->slots.begin(), it)));
}

// After a drawing means after the whole hold. Landing a new drawing in the
// middle of a ten-frame hold splits it in two, which is never what was meant.
std::size_t MainWindow::slotAfterCurrentDrawing() const {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->slots.empty()) return 0;
    return track->runBounds(timeline_widget_->currentSlot()).second + 1;
}

void MainWindow::insertInterval() {
    stopPlayback();
    const std::size_t at = slotAfterCurrentDrawing();
    doc_.insertImage(track_, at);
    timeline_widget_->refresh();
    timeline_widget_->setCurrentSlot(at);
    refreshEverything();
}

void MainWindow::duplicateDrawing() {
    stopPlayback();
    const std::size_t at = slotAfterCurrentDrawing();
    // duplicateImage inserts just after the slot it is given, so hand it the
    // last frame of the hold.
    doc_.duplicateImage(track_, at > 0 ? at - 1 : 0);
    timeline_widget_->refresh();
    timeline_widget_->setCurrentSlot(at);
    refreshEverything();
}

// Deletes the drawing and every frame it is held on. Shortening a hold is a
// different operation and lives on Hold -.
void MainWindow::deleteDrawing() {
    stopPlayback();
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;

    const ImageId image = track->imageAtSlot(timeline_widget_->currentSlot());
    if (image == kNoId) return;
    if (track->exposureOf(image) >= track->slots.size()) {
        return;  // it is the only drawing; leave something to draw on
    }

    doc_.removeDrawing(track_, image);
    refreshEverything();
}

void MainWindow::extendExposure() {
    doc_.extendExposure(track_, timeline_widget_->currentSlot(), 1);
    refreshEverything();
}

void MainWindow::shortenExposure() {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;
    const std::size_t slot = timeline_widget_->currentSlot();
    const ImageId image = track->imageAtSlot(slot);
    if (track->exposureOf(image) <= 1) return;  // that would delete the drawing
    doc_.removeSlot(track_, slot);
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

// One command for the pair, so changing both and then changing your mind is one
// undo rather than two.
void MainWindow::chooseSceneSettings() {
    stopPlayback();
    const int was_framerate = doc_.scene().framerate;
    const int was_width = doc_.scene().width;
    const int was_height = doc_.scene().height;

    SceneSettingsDialog dialog(was_framerate, was_width, was_height, this);

    // The preview writes to the scene directly, around the history. Choosing a
    // resolution means looking at it, and it is not worth an undo entry per
    // number tried on the way -- so nothing is recorded until the dialog is
    // accepted, and cancelling puts back exactly what was there.
    const auto show = [this](int framerate, int width, int height) {
        Scene& scene = doc_.mutableScene();
        scene.framerate = framerate;
        scene.width = width;
        scene.height = height;
        canvas_->refreshAll();
        syncStatus();
    };
    connect(&dialog, &SceneSettingsDialog::previewed, this, show);

    const bool accepted = dialog.exec() == QDialog::Accepted;

    // Back to where we started either way, so that accepting records the whole
    // change as one command rather than recording the difference from whatever
    // the last preview happened to leave behind.
    show(was_framerate, was_width, was_height);
    if (!accepted) return;

    doc_.beginCommand("Scene settings");
    doc_.setCanvasSize(dialog.canvasWidth(), dialog.canvasHeight());
    doc_.setFramerate(dialog.framerate());
    doc_.endCommand();

    // The canvas bounds the colour fills, so they have to be solved again.
    canvas_->refreshAll();
    syncStatus();
}

void MainWindow::togglePlayback() {
    if (playback_timer_->isActive()) {
        stopPlayback();
        return;
    }

    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->slots.size() < 2) return;

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
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->slots.empty()) {
        stopPlayback();
        return;
    }

    const int fps = std::max(1, doc_.scene().framerate);
    const qint64 elapsed = playback_clock_.elapsed();
    const qint64 advanced = elapsed * fps / 1000;
    const std::size_t count = track->slots.size();
    const std::size_t slot =
        (playback_start_slot_ + static_cast<std::size_t>(advanced)) % count;

    if (slot == timeline_widget_->currentSlot()) return;
    timeline_widget_->setCurrentSlot(slot);
}

void MainWindow::onOnionChanged() {
    CanvasWidget::OnionSettings settings = canvas_->onion();
    settings.before = onion_->value();
    settings.after = onion_->value();
    canvas_->setOnion(settings);
}

Layer* MainWindow::currentLayer() {
    Track* track = doc_.mutableScene().findTrack(track_);
    if (!track) return nullptr;
    const int row = layer_list_ ? layer_list_->indexOfTopLevelItem(layer_list_->currentItem()) : -1;
    if (row < 0 || static_cast<std::size_t>(row) >= track->layers.size()) return nullptr;
    return &track->layers[static_cast<std::size_t>(row)];
}

void MainWindow::rebuildLayerList() {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || !layer_list_) return;

    const LayerId active = canvas_->activeLayer();

    updating_list_ = true;
    layer_list_->clear();
    QTreeWidgetItem* active_item = nullptr;
    for (const Layer& layer : track->layers) {
        QString label = QString::fromStdString(layer.name);
        if (layer.kind == LayerKind::Ctg) {
            label += QStringLiteral("   [colour, %1 source%2]")
                         .arg(layer.ctg_sources.size())
                         .arg(layer.ctg_sources.size() == 1 ? "" : "s");
        }
        auto* item = new QTreeWidgetItem(layer_list_);
        item->setText(0, label);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, layer.visible ? Qt::Checked : Qt::Unchecked);

        // A colour layer gets a second tick: show the scribbles rather than the
        // fill. Only a colour layer has one -- an item draws an indicator only
        // in a column that has been given a check state, so the other rows
        // leave that column empty.
        if (layer.kind == LayerKind::Ctg) {
            item->setCheckState(1, layer.show_scribbles ? Qt::Checked : Qt::Unchecked);
            item->setToolTip(1, QStringLiteral(
                                    "Show the scribbles instead of the fill.\n"
                                    "Changes nothing about the drawing, only what is shown."));
        }
        if (layer.id == active) active_item = item;
    }
    if (!active_item && layer_list_->topLevelItemCount() > 0) {
        active_item = layer_list_->topLevelItem(0);
    }
    layer_list_->setCurrentItem(active_item);

    // The second column only means something to a colour layer, so it only
    // appears once there is one. A headed, empty column beside a stack of
    // ordinary layers is a question the panel cannot answer.
    const bool any_colour = std::any_of(track->layers.begin(), track->layers.end(),
                                        [](const Layer& l) { return l.kind == LayerKind::Ctg; });
    layer_list_->setColumnHidden(1, !any_colour);
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

void MainWindow::onLayerItemChanged(QTreeWidgetItem* item, int column) {
    if (updating_list_ || !item) return;
    Track* track = doc_.mutableScene().findTrack(track_);
    if (!track) return;

    const int row = layer_list_->indexOfTopLevelItem(item);
    if (row < 0 || static_cast<std::size_t>(row) >= track->layers.size()) return;

    Layer updated = track->layers[static_cast<std::size_t>(row)];
    if (column == 0) {
        updated.visible = item->checkState(0) == Qt::Checked;
    } else if (column == 1 && updated.kind == LayerKind::Ctg) {
        // What you are looking at, not what is on the layer. Recorded like any
        // other layer property so it survives a rebuild of the panel.
        updated.show_scribbles = item->checkState(1) == Qt::Checked;
    } else {
        return;
    }
    doc_.updateLayer(track_, updated.id, updated);

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
    doc_.updateLayer(track_, updated.id, updated);
    // refreshAll only marks the cache dirty; the composite happens once, in the
    // next paint. Doing it here meant one full-viewport flatten per slider tick.
    canvas_->refreshAll();
}

void MainWindow::clearCurrentLayer() {
    stopPlayback();
    Layer* layer = currentLayer();
    if (!layer) return;
    doc_.clearCel(track_, canvas_->currentImage(), layer->id);
    canvas_->refreshAll();
    syncStatus();
}

// The lowest unused "layer N". Reusing a number after a deletion would give
// two layers the same name, which makes the panel ambiguous.
std::string MainWindow::nextLayerName() const {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return "layer 1";

    for (std::size_t n = 1;; ++n) {
        const std::string candidate = "layer " + std::to_string(n);
        const bool taken = std::any_of(
            track->layers.begin(), track->layers.end(),
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
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;

    const int row =
        layer_list_ ? std::max(0, layer_list_->indexOfTopLevelItem(layer_list_->currentItem())) : 0;
    const LayerId created = doc_.addLayer(track_, nextLayerName(),
                                          static_cast<std::size_t>(row));
    canvas_->setActiveLayer(created);
    rebuildLayerList();
    canvas_->refreshAll();
}

// Every raster layer becomes a barrier by default. Cutting against the rough as
// well as the clean closes gaps that leak from either alone, and there is no
// good reason to make someone ask for that.
//
// It goes to the bottom of the pile rather than above the selected layer, which
// is where an ordinary layer goes. A colour layer is cut against the line art
// and belongs underneath it: created on top, the flat it generates covers the
// very drawing that produced it.
void MainWindow::addColourLayer() {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;

    const std::size_t bottom = track->layers.size();

    doc_.beginCommand("Add colour layer");
    const LayerId created =
        doc_.addLayer(track_, nextColourLayerName(), bottom, LayerKind::Ctg);

    const Track* after = doc_.scene().findTrack(track_);
    if (after) {
        Layer settings = *after->findLayer(created);
        for (const Layer& layer : after->layers) {
            if (layer.kind == LayerKind::Raster) settings.ctg_sources.push_back(layer.id);
        }
        doc_.updateLayer(track_, created, settings);
    }
    doc_.endCommand();

    canvas_->setActiveLayer(created);
    rebuildLayerList();
    canvas_->refreshAll();
}

std::string MainWindow::nextColourLayerName() const {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return "colour 1";
    for (std::size_t n = 1;; ++n) {
        const std::string candidate = "colour " + std::to_string(n);
        const bool taken =
            std::any_of(track->layers.begin(), track->layers.end(),
                        [&](const Layer& l) { return l.name == candidate; });
        if (!taken) return candidate;
    }
}

void MainWindow::removeCurrentLayer() {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->layers.size() <= 1) return;  // never leave nothing to draw on
    Layer* layer = currentLayer();
    if (!layer) return;

    doc_.removeLayer(track_, layer->id);
    const Track* after = doc_.scene().findTrack(track_);
    if (after && !after->layers.empty()) canvas_->setActiveLayer(after->layers.front().id);
    rebuildLayerList();
    canvas_->refreshAll();
}

void MainWindow::moveCurrentLayer(int delta) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || !layer_list_) return;

    const int from = layer_list_->indexOfTopLevelItem(layer_list_->currentItem());
    const int to = from + delta;
    if (from < 0 || to < 0 || static_cast<std::size_t>(to) >= track->layers.size()) return;

    doc_.moveLayer(track_, static_cast<std::size_t>(from), static_cast<std::size_t>(to));
    rebuildLayerList();
    layer_list_->setCurrentItem(layer_list_->topLevelItem(to));
    canvas_->refreshAll();
}

void MainWindow::chooseColour() {
    // The dialog speaks sRGB; the document works in linear light.
    const QColor initial = QColor::fromRgbF(linearToSrgb(colour_r_), linearToSrgb(colour_g_),
                                            linearToSrgb(colour_b_));
    const QColor chosen = QColorDialog::getColor(initial, this, QStringLiteral("Brush colour"));
    if (!chosen.isValid()) return;

    applyColour(srgbToLinear(static_cast<float>(chosen.redF())),
                srgbToLinear(static_cast<float>(chosen.greenF())),
                srgbToLinear(static_cast<float>(chosen.blueF())));
}

void MainWindow::applyColour(float r, float g, float b) {
    colour_r_ = r;
    colour_g_ = g;
    colour_b_ = b;
    canvas_->setBrushColour(r, g, b);

    if (!colour_swatch_) return;
    const auto shown = [](float linear) {
        return static_cast<qreal>(std::clamp(linearToSrgb(linear), 0.0f, 1.0f));
    };
    const QColor swatch = QColor::fromRgbF(shown(r), shown(g), shown(b));
    colour_swatch_->setStyleSheet(
        QStringLiteral("background:%1;border:1px solid #888;").arg(swatch.name()));
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
