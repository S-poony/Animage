// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.h"

#include "name_limits.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QPalette>
#include <QDialogButtonBox>
#include <QDockWidget>

#include "floating_dock_frame.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLineEdit>
#include <QSinglePointEvent>
#include <QListWidget>
#include <QDialog>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QFileInfo>
#include <QLayout>
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
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QLibraryInfo>
#include <QTimer>
#include <QVersionNumber>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindowStateChangeEvent>
#include <algorithm>
#include <cmath>

#include "canvas_widget.h"
#include "ctg_solver.h"
#include "image_import.h"
#include "layer_list.h"
#include "marks.h"
#include "export_sequence.h"
#include "project_io.h"
#include "color.h"
#include "scribble.h"
#include "scene_settings_dialog.h"
#include "audio_device.h"
#include "audio_import.h"
#include "audio_import_dialog.h"
#include "sequence_import_dialog.h"
#include "shortcuts_dialog.h"
#include "timeline_widget.h"

using namespace animage;

namespace {

// Says why an operation refused -- "Cannot cut: that layer is locked" -- for
// the five that share `CanvasWidget::refuseHere`'s vocabulary. One function
// rather than a `showMessage` at each call site, because the rule about what
// *not* to say has to live somewhere:
//
// **a refusal the status line is already showing is not repeated.** Past the
// end of a track that line permanently reads "you cannot draw past the end of a
// track", and a temporary message hides the line while it is up -- so saying
// the same thing in other words covers over the words that were already saying
// it. `Refusal::NoDrawing` is exactly that case, and so far the only one.
// How much sound a scrub burst plays, past the frame it starts on.
//
// **One frame's worth is what docs/importing.md asks for, and one frame's worth
// is too short to recognise.** At 24 fps a frame is 42 ms, which is under half a
// syllable: dragging fast it does not matter, because each burst is cut off by
// the next and what you hear is continuous, but the moment you stop on a frame
// to listen, 42 ms is a blip you cannot tell a *b* from a *d* in. So a burst is
// the frame it stands on or 90 ms, whichever is longer -- about a syllable, and
// at 11 fps or slower the frame is already longer than that and wins.
//
// The number is here rather than spread through scrubAudio because it is a
// judgement about ears and is expected to be argued with. It has been argued
// with once already: 120 ms was the first guess and came back as slightly too
// long from the first person to drag the playhead over a line of dialogue.
constexpr int kScrubFloorMs = 90;

// How long to wait for the first sample of a take to come out of the device
// before giving up on it and playing to the wall clock instead.
//
// **A bound on a wait that is normally a fifth of a second.** The picture holds
// on the frame Play was pressed on until the sound reaches it, which is what
// starting together means -- the spike measured a 250 ms buffer, so that hold
// is about a quarter of a second and is the cost of the whole feature. What
// this catches is the case where it never arrives: a driver that accepted the
// stream and reports nothing, or hardware that went away between the import and
// the press. A second is four times the measured buffer, and a picture that
// starts a second late is recoverable where one that never starts is not.
constexpr int kAudioStartWaitMs = 1000;

// And a ramp at each end of it. A buffer that begins and ends part-way up a
// waveform steps the speaker cone, which is a click on every frame dragged
// past. Three milliseconds is inaudible as a fade and removes all of it.
constexpr int kScrubFadeMs = 3;

void sayCannot(QStatusBar* bar, const char* verb, CanvasWidget::Refusal refusal) {
    if (!bar || refusal == CanvasWidget::Refusal::None) return;
    if (refusal == CanvasWidget::Refusal::NoDrawing) return;

    bar->showMessage(QStringLiteral("Cannot %1: %2")
                         .arg(QString::fromLatin1(verb), CanvasWidget::explain(refusal)),
                     6000);
}


// Where a project opens when the one on disk is damaged. Beside the original
// and named after it, so the two sit together in a file dialog and it is
// obvious which came from which.
//
// "rescued" is already this program's word for a copy kept when something went
// wrong -- `folder.rescued-<ms>` is what a save that cannot be put in place
// leaves behind. Same idea, so the same word rather than a second one.
QString rescuedFolderFor(const QString& folder) {
    const QFileInfo info(folder);
    const QString parent = info.absolutePath();
    const QString name = info.fileName();

    // The number goes in front of the name rather than inside it, so the
    // `.animage` suffix stays where it belongs whatever the project is called.
    QString candidate = parent + QStringLiteral("/rescued_") + name;
    for (int n = 2; QFileInfo::exists(candidate); ++n) {
        candidate = parent + QStringLiteral("/rescued_%1_").arg(n) + name;
    }
    return candidate;
}

// Which drawings the unreadable files actually cost, named the way an animator
// names them -- a drawing number and a layer -- rather than as cel filenames,
// which say nothing to the person who has to decide what to redraw.
//
// One cel can be held by two drawings, which is what duplicating a drawing as a
// link does, so one lost file can cost more than one of them. `drawings` counts
// the drawings and not the files, because that is the number being asked about.
QStringList describeLostDrawings(const Scene& scene, const ProjectIO::Damage& damage,
                                 int* drawings) {
    std::vector<std::pair<int, QString>> numbered;
    std::vector<std::pair<TrackId, ImageId>> affected;
    const bool several_tracks = scene.tracks.size() > 1;

    for (const ProjectIO::Damage::Lost& lost : damage.lost) {
        bool named = false;
        for (const Track& track : scene.tracks) {
            for (const auto& [image_id, image] : track.images) {
                for (const auto& [layer_id, cel] : image.cels) {
                    if (cel != lost.cel) continue;
                    const Layer* layer = track.findLayer(layer_id);
                    QString line = QStringLiteral("drawing %1").arg(image.number);
                    if (layer) {
                        line += QStringLiteral(" - layer \"%1\"")
                                    .arg(QString::fromStdString(layer->name));
                    }
                    if (several_tracks) {
                        line += QStringLiteral(" - track \"%1\"")
                                    .arg(QString::fromStdString(track.name));
                    }
                    numbered.emplace_back(image.number, line);
                    affected.emplace_back(track.id, image_id);
                    named = true;
                }
            }
        }
        // Cannot happen -- the manifest is walked from the drawings -- and kept
        // rather than asserted away, because a list that silently dropped an
        // entry would be a list nobody could count on.
        if (!named) {
            numbered.emplace_back(0, QStringLiteral("%1 (not held by any drawing)").arg(lost.file));
        }
    }

    // By drawing number, which is the order they were made in and the order
    // somebody will go looking through them.
    std::stable_sort(numbered.begin(), numbered.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::sort(affected.begin(), affected.end());
    affected.erase(std::unique(affected.begin(), affected.end()), affected.end());
    if (drawings) *drawings = static_cast<int>(affected.size());

    QStringList lines;
    for (const auto& [number, line] : numbered) lines.push_back(line);
    return lines;
}
// An autosave costs about a tenth of a second on a shot of a hundred drawings
// now that it only writes the cels that moved, so the interval is chosen for
// how much work an animator is willing to lose rather than for what it costs.
constexpr int kAutosaveIntervalMs = 2 * 60 * 1000;

// A stroke and a playback pass are both short. Rather than skip this autosave
// and wait the whole interval again, look back in a moment.
constexpr int kAutosaveRetryMs = 5 * 1000;

// How long after a maximise or a restore a canvas resize is still part of it.
// Long enough for the platform to deliver both events in either order and for a
// window manager to animate, short enough that no later resize is caught by it.
constexpr int kReframeWindowMs = 500;

// Whether the keyboard currently belongs to something a character can be typed
// into. Both are asked for: a spin box holds a line edit but keeps the focus
// itself, through a focus proxy, so the line edit is never the answer.
bool isTypingInto(const QWidget* widget) {
    return qobject_cast<const QLineEdit*>(widget) != nullptr ||
           qobject_cast<const QAbstractSpinBox*>(widget) != nullptr;
}

// What a track does past its last drawing, in one word, for the menu item and
// for the sentence the status bar makes of it.
//
// Deliberately not project_io's endName, which returns the same three words:
// those are the file format. Sharing one function would mean that improving the
// wording here silently changed what scene.json means, and the two are free to
// disagree -- the format's words are pinned by the round-trip tests and these
// are pinned by nothing but taste.
// A scroll area that leaves room for its own horizontal scrollbar.
//
// **Issue #26, and it is one line of Qt.** `QScrollArea::sizeHint` adds the
// horizontal scrollbar's height only when the policy is `ScrollBarAlwaysOn`. The
// timeline's is `ScrollBarAsNeeded`, so the dock is sized as though the pan
// slider does not exist -- and when a shot grows wider than the window the
// slider appears and takes its height out of the viewport instead. Measured on
// a 1400 px window: viewport 68 px for a 64 px strip with no slider, and 54 px
// for the same strip the moment there is one, so ten pixels of the track row are
// outside the viewport with a slider sitting where they should be. That is the
// whole of what "the slider goes slightly over the track" is.
//
// Reserved always rather than asked for when the slider appears. Asking for it
// on the transition would be exact -- the dock would be the right height with a
// slider and with none -- and it would also move the dock while the animator is
// working: a timeline deliberately dragged down to one row would spring taller
// the moment the shot outgrew the window, which is the same complaint
// `timeline_rows_shown_` exists to stop. So the height is spent once, at the
// size the dock opens at, and nothing moves it afterwards.
//
// The scrollbar is asked rather than assumed, because its height is the style's
// and the screen's -- 14 px on this desk and not a number to write down.
class ReservingScrollArea : public QScrollArea {
public:
    using QScrollArea::QScrollArea;

    QSize sizeHint() const override {
        QSize wanted = QScrollArea::sizeHint();
        wanted.setHeight(wanted.height() + horizontalScrollBar()->sizeHint().height());
        return wanted;
    }
};

QString endWord(TrackEnd end) {
    switch (end) {
        case TrackEnd::HoldLast: return QStringLiteral("hold");
        case TrackEnd::Cycle: return QStringLiteral("cycle");
        case TrackEnd::Nothing: break;
    }
    return QStringLiteral("nothing");
}

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("Animage"));
    resize(1400, 900);

    // "track 1" and not "main". It is the first of several -- the model and the
    // timeline both take more than one already, only the interface does not --
    // and it is the prefix on every exported file name, where "main_" beside a
    // second track called something else would be the one track that did not
    // say which number it was.
    track_ = doc_.addTrack("track 1");
    const LayerId first = doc_.addLayer(track_, "layer 1");
    doc_.insertImage(track_, 0);
    doc_.clearHistory();  // an empty scene is the starting point, not an edit

    canvas_ = new CanvasWidget(doc_, this);
    canvas_->setTrack(track_);
    canvas_->setFrame(0);
    canvas_->setActiveLayer(first);
    // Where an import's bytes are is this window's business -- it depends on
    // whether the project has been saved and into which folder -- so the canvas
    // asks rather than working it out. See CanvasWidget::setImportLocator.
    canvas_->setImportLocator(
        [this](const std::string& name) { return importPathFor(name); });
    connect(canvas_, &CanvasWidget::importUnreadable, this, &MainWindow::onImportUnreadable);
    // Queued for the reason the colour's is: this fires from the poll that
    // installs a frame, and the paint that asked for it may still be on the
    // stack -- touching the status bar or the panels from inside a paint is how
    // a widget gets rebuilt underneath itself.
    connect(canvas_, &CanvasWidget::referenceFramesChanged, this, [this] { syncStatus(); },
            Qt::QueuedConnection);
    setCentralWidget(canvas_);

    playback_timer_ = new QTimer(this);
    playback_timer_->setTimerType(Qt::PreciseTimer);
    connect(playback_timer_, &QTimer::timeout, this, &MainWindow::onPlaybackTick);

    // Runs from the start even though an untitled document has nowhere to be
    // written: the tick is what decides, and it is the only place the rules
    // about when a save is allowed live.
    autosave_timer_ = new QTimer(this);
    connect(autosave_timer_, &QTimer::timeout, this, &MainWindow::onAutosaveTick);
    autosave_timer_->start(kAutosaveIntervalMs);

    buildMenus();
    buildToolBar();
    buildLayerPanel();
    buildTimelinePanel();
    // After both of them: this is the half of the View menu that names the docks.
    buildPanelViewActions();
    buildStatusBar();
    rebuildLayerList();
    syncTrackMenu();
    // After all of them, because the tooltips that name a key are registered as
    // the panels are built and this is what fills the key in.
    syncTooltips();

    // And the machine's speakers, which change without the document changing --
    // so nothing else here would ever ask. After the panels, because the
    // callback reads the timeline. Cleared in the destructor: the watch outlives
    // this window. See AudioDevice::watchOutputs.
    AudioDevice::watchOutputs([this] { onAudioOutputsChanged(); });

    connect(canvas_, &CanvasWidget::brushSizeChanged, this, [this](double radius) {
        if (!radius_) return;
        const QSignalBlocker block(radius_);
        radius_->setValue(radius);
    });
    connect(canvas_, &CanvasWidget::colourPicked, this, &MainWindow::applyColour);
    connect(canvas_, &CanvasWidget::viewChanged, this, &MainWindow::syncStatus);
    connect(canvas_, &CanvasWidget::transformBegan, this, &MainWindow::onTransformBegan);
    connect(canvas_, &CanvasWidget::transformNumbersChanged, this,
            &MainWindow::syncTransformFields);
    connect(canvas_, &CanvasWidget::transformEnded, this, &MainWindow::onTransformEnded);
    // Not through sayCannot, which times its message out after six seconds: the
    // other five refusals are over as soon as they are read, and this one is
    // true until the box is scaled back down. It replaces the standing message
    // that put itself up when the ceiling was crossed, so that pressing the key
    // visibly answers rather than appearing to do nothing, and syncTransformFields
    // takes whichever of the two is up back down again.
    //
    // Timed out where there is no box left, which is the one refusal that
    // works the other way: a bake that ran out of memory has already put the
    // layer back and taken its own transform down, so nothing will ever call
    // syncTransformFields again and a standing message would stand for the rest
    // of the session. Asked of the canvas rather than of the reason, because
    // what decides it is whether there is still something on screen the message
    // is about.
    connect(canvas_, &CanvasWidget::transformRefused, this, [this](CanvasWidget::Refusal why) {
        const QString said = QStringLiteral("Cannot bake this: %1").arg(CanvasWidget::explain(why));
        if (canvas_->transformIsLive()) {
            statusBar()->showMessage(said);
        } else {
            statusBar()->showMessage(said, 8000);
        }
    });
    // What is about to happen, put up before it happens.
    //
    // `repaint` and not `update`, and not processEvents either. The bake holds
    // the interface thread until it is done, so an ordinary update would queue a
    // paint that runs after the wait it was meant to explain -- the message
    // would appear and vanish in the same frame, having described nothing.
    // repaint paints now, synchronously. processEvents would paint too, and
    // would also deliver input into the middle of a gesture that is halfway
    // through committing itself, which is a re-entry this has no need to invite.
    connect(canvas_, &CanvasWidget::layerTransformStarted, this, [this](int drawings) {
        statusBar()->showMessage(
            (drawings == 1)
                ? QStringLiteral("Transforming 1 drawing on this layer...")
                : QStringLiteral("Transforming %1 drawings on this layer...").arg(drawings));
        statusBar()->repaint();
    });

    // How many drawings one press wrote. The number is the whole point of
    // saying anything at all: a layer bake is the one gesture in this program
    // where a single Apply rewrites work you are not looking at, and counting
    // the rows in the timeline to find out how much is not a reasonable thing
    // to ask of anybody.
    connect(canvas_, &CanvasWidget::layerTransformed, this, [this](int drawings) {
        statusBar()->showMessage(
            (drawings == 1)
                ? QStringLiteral("Transformed 1 drawing on this layer")
                : QStringLiteral("Transformed %1 drawings on this layer").arg(drawings),
            4000);
    });
    connect(canvas_, &CanvasWidget::drawingRefusedOnImport, this,
            &MainWindow::offerToConvertRefusedLayer);
    connect(canvas_, &CanvasWidget::selectionChanged, this, &MainWindow::syncStatus);
    connect(canvas_, &CanvasWidget::documentChanged, this, [this] {
        // **Drawing says you are done with the sound.** A soundtrack clicked
        // once would otherwise stay lit for the rest of the session while every
        // stroke landed somewhere else, and the bright row would not be the row
        // being worked on. This is the canvas's own signal, so what reaches it
        // is a stroke, a fill or a transform -- all of them drawing.
        timeline_widget_->clearAudioHighlight();
        syncTrackMenu();
        timeline_widget_->refresh();
        // The first stroke on a new layer is what takes "nothing is drawn on
        // this layer yet" away, and it arrives here rather than through the
        // panel or the playhead -- so without this the button stays greyed out
        // until you happen to change layer. Cheap: it counts cels, not pixels.
        syncLayerButtons();
        syncStatus();
    });
    // A fill landed. Queued, because a solve installed while the
    // canvas is painting itself would rebuild the layer panel underneath the
    // paint, which is a way to delete a widget while it is drawing.
    connect(
        canvas_, &CanvasWidget::colourChanged, this,
        [this] {
            timeline_widget_->refresh();
            refreshLayerFlags();
            syncStatus();
        },
        Qt::QueuedConnection);
    setWindowTitle(QStringLiteral("Untitled - Animage"));
    syncStatus();

    // Space and Z are held modifiers for panning and zooming, not shortcuts, so
    // QAction cannot express them. Without this filter they stop working the
    // moment anything else takes focus -- clicking a checkbox was enough -- and
    // worse, Space then operates whatever widget it landed on.
    qApp->installEventFilter(this);

    // Which mode the shortcuts are in follows the keyboard, and this is where it
    // is asked. See setTyping: the question is "is the keyboard in a field", and
    // the only honest answer to that is which widget has the focus.
    connect(qApp, &QApplication::focusChanged, this,
            [this](QWidget*, QWidget* now) { setTyping(isTypingInto(now)); });

    canvas_->setFocus();
}

MainWindow::~MainWindow() {
    // This window stops listening to the application before it starts taking
    // itself apart. Both halves outlive it -- the event filter is installed on
    // qApp, and so is the focus connection that decides the shortcut mode -- and
    // letting ~QObject undo them would leave both live for the whole of the
    // destruction below, delivering to a window whose children are going or
    // gone. Measured: without the disconnect, taking the children apart moves
    // the focus twice more and setTyping is called both times, the second with
    // `canvas_` already deleted.
    qApp->removeEventFilter(this);
    disconnect(qApp, nullptr, this, nullptr);

    // And it stops listening to the machine's speakers, for the same reason and
    // one moment earlier than it would matter: the watch outlives this window
    // -- one QMediaDevices for the life of the process, see
    // AudioDevice::watchOutputs -- and a callback into a window that is taking
    // itself apart is a callback into `timeline_widget_` after it has gone.
    AudioDevice::watchOutputs({});

    // A rename still open is given up here, in the last moment this is still a
    // MainWindow.
    //
    // A window is destroyed from the top down: this body runs, then every
    // member of this class is destroyed -- `doc_`, `keyed_actions_`,
    // `typing_editors_` -- and only then does ~QWidget destroy the children.
    // Destroying an open editor is what *finishes* a rename, so a rename left
    // open reports itself from there: `renamed` calls renameLayer on a document
    // that no longer exists, and `renaming` calls setTyping on a window that is
    // no longer a window. Both were seen; see issue #51.
    //
    // So the editors are closed while everything they will reach is still
    // alive. Cutting the wires instead does not work and is measurably worse
    // than doing nothing: with `renamed` cleared, the delegate falls back to
    // Qt's own setModelData, which writes the name into the model, which emits
    // itemChanged, which is connected to onLayerItemChanged -- the same dead
    // document, reached by a third route, and that one *writes* to it. Counted
    // over one run of test_canvas, clearing the two callbacks turns three late
    // calls into nine.
    //
    // The three steps below are in the order they are for a reason, and each
    // one is the reason the next is safe.

    // A rename is given up rather than finished. It has to come before the
    // other two, because both of those close the editor -- and an editor closed
    // any other way *commits*, so shutting a window down would silently apply a
    // half-typed name.
    if (layer_list_) layer_list_->abandonRename();
    if (timeline_widget_) timeline_widget_->abandonRename();

    // Whatever holds the keyboard lets go of it now. Every number field on the
    // toolbar reports through QAbstractSpinBox::editingFinished, which fires on
    // losing focus -- so a window closed with the keyboard in the brush size box
    // otherwise ran `[this] { canvas_->setFocus(); }` from inside ~QWidget,
    // reading a `canvas_` that had already been destroyed. Measured: it fires.
    // Letting go here means it has already reported by the time anything is
    // destroyed, and a field that has lost focus does not lose it twice.
    if (QWidget* holding = focusWidget()) holding->clearFocus();

    // And the children go while every member they might touch is still here,
    // which is the general form of the two steps above rather than a third
    // special case: after this there is nothing left for ~QWidget to destroy,
    // so there is no longer a moment in which a child of this window is alive
    // and this window's members are not.
    //
    // In reverse order of construction, which is the same rule C++ uses for
    // members and for the same reason: the canvas is built first and is what
    // the later widgets refer back to, so it is the last thing that should go.
    const QList<QWidget*> owned = findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
    for (auto it = owned.crbegin(); it != owned.crend(); ++it) delete *it;
    // Absent rather than dangling. Most of this class already asks `if
    // (canvas_)` before using it, and this is what makes those questions
    // truthful for the rest of the destruction.
    canvas_ = nullptr;
    timeline_widget_ = nullptr;
    layer_list_ = nullptr;
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
    QTimer::singleShot(0, this, [this] {
        canvas_->fitToCanvas();
        // Again, now that the dock has been laid out and its title bar has a
        // height to read. From the constructor there was nothing to measure and
        // the strip came up exactly one title bar short.
        timeline_rows_shown_ = 0;
        syncTimelineHeight();
        // The same reason, one step further: this is the first moment the window
        // has a laid-out answer to what it looks like, so it is the first moment
        // worth recording as the view to go back to. Saved from the constructor
        // it would be the layout these two lines exist to correct.
        default_layout_ = saveState();
        default_layout_rows_ = timeline_rows_shown_;
    });
}

// Maximising the window frames the canvas in it, and restoring frames it again.
//
// The canvas keeps its zoom and its pan across a resize, and the pan is the
// image point at the widget's *top left* -- so a window made twice as big shows
// the same drawing at the same size in the same corner with new emptiness beside
// it. Maximising is the moment that is most obviously wrong, and it is the one
// the program acts on.
//
// Deliberately not every resize. The drawing surface has no edges, so working
// outside the canvas is ordinary here rather than exceptional, and a view that
// snapped back to the canvas whenever a dock moved would take you off what you
// were drawing. Maximising is a deliberate act with an obvious intent; dragging
// an edge is not.
//
// Restoring reframes for the same reason and not for symmetry's sake: a canvas
// fitted to a full screen is too big for the window that comes back, so leaving
// it would trade the original complaint for its mirror image.
//
// **Two events decide this and their order is not fixed**, which is the whole of
// what was hard about it, and both of the first two versions got it wrong in a
// different direction.
//
// The state change and the canvas's resize both have to have happened before a
// fit means anything: fitting on the state change alone fits to the window that
// is going away, and a zero-delay timer does not fix that because the platform's
// resize is not guaranteed to have arrived by the time the timer runs. Then
// asking `isMaximized()` on the resize instead fails the other way round -- the
// widget can be resized before the window state is updated, so the question
// answers "no change" and nothing ever fits. That is what was reported.
//
// So neither event decides on its own. The state change *arms* it, and then a
// fit happens on every canvas resize for a short moment afterwards, plus once
// straight away for the case where the canvas does not change size at all. A fit
// is idempotent and costs nothing, so fitting two or three times through a
// maximise is not a problem -- what matters is that the last one runs after the
// last resize, and this is true whichever way round they arrive.
void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange || !canvas_) return;

    const auto* state = static_cast<QWindowStateChangeEvent*>(event);
    // Maximised-ness only. Minimising and coming back is not a reframe: the
    // window comes back the size it went away.
    if (((state->oldState() ^ windowState()) & Qt::WindowMaximized) == 0) return;

    reframing_ = true;
    reframe_since_.start();
    QTimer::singleShot(0, this, [this] { reframeIfArmed(); });
}

void MainWindow::reframeIfArmed() {
    if (!reframing_ || !canvas_) return;
    // Disarmed lazily, by the first resize that arrives too late to be part of
    // this one -- dragging a dock an hour later must not reframe anything.
    if (reframe_since_.elapsed() > kReframeWindowMs) {
        reframing_ = false;
        return;
    }
    canvas_->fitToCanvas();
}

// Hand the keyboard to the canvas, if a press on `pressed` should take it off
// whatever field currently holds it. See eventFilter for why this exists.
//
// **The parent chain and not `pressed` itself**, which is the whole subtlety. A
// press is answered by the nearest widget above it that would take the keyboard,
// and that is rarely the widget the pointer is over: a spin box holds the focus
// for the line edit inside it, through a focus proxy, so the line edit a click
// actually lands on reports Qt::NoFocus. Asking only about `pressed` therefore
// took the keyboard off one number field and gave it to the canvas on the way to
// another -- which is the bug this exists to fix, in the opposite direction.
//
// The walk stops at this window. What it is looking for on the way up:
//
// - The field that holds the keyboard. The press is inside it, so it keeps it.
// - Anything that takes focus on a click. Qt is about to move the keyboard there
//   itself and this must not race it.
//
// Two more things are left alone before the walk starts. Nothing is being typed
// into, so there is nothing to take. And a press that is not the left button --
// the one that opens a context menu -- is not somebody going somewhere else.
//
// Finally, `pressed` must be in this window. A menu, a dialog or a floating dock
// is not "somewhere else in this window" in any sense the person clicking would
// recognise, and taking the keyboard out of one would be a new bug in place of
// the old one.
void MainWindow::takeTheKeyboardBackFrom(QWidget* pressed) {
    QWidget* holding = QApplication::focusWidget();
    if (!pressed || !holding || !isTypingInto(holding)) return;
    if (pressed->window() != this) return;

    for (QWidget* w = pressed; w; w = w->parentWidget()) {
        if (w == holding) return;
        if ((w->focusPolicy() & Qt::ClickFocus) != 0) return;
        if (w == this) break;
    }
    canvas_->setFocus(Qt::MouseFocusReason);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // The transform bar is a child of the canvas and so belongs to no layout.
    // Nothing else is going to move it when the canvas changes size, and a
    // window dragged narrower would otherwise leave it hanging off the edge.
    if (event->type() == QEvent::Resize && watched == canvas_) {
        if (transform_bar_ && transform_bar_->isVisible()) placeTransformBar();
        // The only resizes the view is reframed for are the ones a maximise or a
        // restore armed. See changeEvent.
        reframeIfArmed();
    }

    // A press anywhere but the field itself takes the keyboard back from it.
    //
    // The canvas and the timeline are the only two widgets in this window that
    // take focus on a click. Every other one is deliberately Qt::NoFocus, each
    // for a good reason -- Space toggles a visibility tick rather than panning,
    // a button that took the keyboard would take the pen with it -- and the
    // unnoticed cost of that is the other half of the same fact: **a widget that
    // will not take the keyboard cannot take it away either.** So a number being
    // typed into kept the keyboard through a click on the opacity slider, on the
    // empty part of the layer panel, on a swatch; and a rename kept it through a
    // click on another row, leaving an editor open on one layer while a
    // different one was selected. Both reported.
    //
    // Since the shortcut mode follows the focus -- see setTyping -- a field that
    // will not let go also leaves every shortcut switched off, silently, until
    // something that does take focus is clicked. That is what made this worth
    // fixing rather than living with.
    //
    // Deliberately not a change of focus policy on those widgets: that is what
    // would bring Space back to the tick boxes. The click is what changes, and
    // only when there is a field to take the keyboard from.
    const bool press = event->type() == QEvent::MouseButtonPress ||
                       event->type() == QEvent::TabletPress;
    if (press && canvas_ && static_cast<QSinglePointEvent*>(event)->button() == Qt::LeftButton) {
        takeTheKeyboardBackFrom(qobject_cast<QWidget*>(watched));
    }

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

    // A key whose press we forwarded has its release forwarded too, whatever is
    // true by the time that release arrives.
    //
    // Both guards below ask about the state *now*, and the state changes while a
    // key is held down. Alt+Tab is Alt going down: a Space release on the way
    // back therefore carries Alt, hit the modifier guard, and was dropped -- with
    // its press already delivered. The canvas went on believing Space was held
    // and every pen press panned instead of drawing, for the rest of the
    // session, with nothing on screen saying why. Opening a rename editor
    // between the press and the release does the same thing through the guard
    // above it.
    //
    // A press that was never forwarded falls through to the guards as before, so
    // a bare release with no press of its own is unchanged -- which is a case
    // that already existed and is already tested.
    if (event->type() == QEvent::KeyRelease && forwarded_keys_.erase(key->key()) > 0) {
        forwarding_key_ = true;
        QCoreApplication::sendEvent(canvas_, key);
        forwarding_key_ = false;
        return true;
    }

    // Not while something is being typed into. Space and Z are pan and zoom
    // only when the keyboard belongs to the canvas; in a name being renamed, in
    // a spin box on the transform bar, or in any dialog's field, they are
    // characters -- and this filter was eating them, so a track renamed "rough
    // pass" arrived called "roughpass" and nothing said why.
    //
    // The focus widget and not `watched`: a key event goes to whatever has the
    // keyboard, and this filter sees it again at each parent it propagates to.
    if (isTypingInto(QApplication::focusWidget())) {
        return QMainWindow::eventFilter(watched, event);
    }
    // Ctrl+Z and friends are shortcuts and must be left alone.
    if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return QMainWindow::eventFilter(watched, event);
    }

    // Remembered before it goes, so that the release above can find it however
    // the keyboard has changed in the meantime. Auto-repeat re-inserts the same
    // key, which a set makes free.
    if (event->type() == QEvent::KeyPress) forwarded_keys_.insert(key->key());

    forwarding_key_ = true;
    QCoreApplication::sendEvent(canvas_, key);
    forwarding_key_ = false;
    return true;
}

// One action, from the row of the shortcut table that names it.
//
// The point is not that it saves a line at each call site. It is that "what key
// is this on" and "when is it live" are now two columns of one table instead of
// fifteen literals and a set of setEnabled calls nobody can enumerate. See
// shortcuts.h.
QAction* MainWindow::makeAction(shortcuts::Id id, std::function<void()> handler) {
    const shortcuts::Entry& entry = shortcuts::entryFor(id);
    auto* action = new QAction(QString::fromUtf8(entry.label), this);
    // From the bindings and not from the row: the row is what the key was to
    // begin with, and what it is now is whatever the person holding the keyboard
    // last said. An empty sequence is a row unbound on purpose, and QAction
    // takes that to mean no shortcut, which is exactly right.
    action->setShortcut(shortcuts::current().sequenceFor(id));
    // Application-wide, as every shortcut here has always been: the canvas holds
    // the keyboard and the menus still have to answer for it. What was missing
    // was the other half -- which of them are live at all.
    action->setShortcutContext(Qt::ApplicationShortcut);
    if (handler) {
        connect(action, &QAction::triggered, this, [call = std::move(handler)] { call(); });
    }
    keyed_actions_[id] = action;
    return action;
}

QAction* MainWindow::actionForTesting(shortcuts::Id id) const {
    const auto found = keyed_actions_.find(id);
    return (found == keyed_actions_.end()) ? nullptr : found->second;
}

void MainWindow::adoptShortcuts(const shortcuts::Bindings& bindings) {
    shortcuts::current() = bindings;
    for (const auto& [id, action] : keyed_actions_) {
        action->setShortcut(shortcuts::current().sequenceFor(id));
    }
    // The canvas needs nothing: it asks the bindings at the moment a key
    // arrives rather than holding a copy of them. That is the whole reason the
    // transform keys are asked for by id.
    syncTooltips();
}

void MainWindow::keyedTip(QAction* on, shortcuts::Id id, const QString& what,
                          const QString& more, const std::vector<shortcuts::Id>& also) {
    if (!on) return;
    keyed_tips_.push_back({on, nullptr, id, what, more, also});
}

void MainWindow::keyedTip(QWidget* on, shortcuts::Id id, const QString& what,
                          const QString& more, const std::vector<shortcuts::Id>& also) {
    if (!on) return;
    keyed_tips_.push_back({nullptr, on, id, what, more, also});
}

// Changes what a keyed tooltip says, keeping the key it names.
//
// The tooltips are composed from a table so that rebinding a key rewrites every
// sentence that mentions one; this reaches into that table rather than around
// it, so a tooltip that changes with the gesture still gets its key from the
// same place as all the others.
void MainWindow::setKeyedTipText(QWidget* on, const QString& what) {
    for (KeyedTip& tip : keyed_tips_) {
        if (tip.widget != on) continue;
        if (tip.what == what) return;  // nothing to do, and syncTooltips is not free
        tip.what = what;
        syncTooltips();
        return;
    }
}

void MainWindow::setKeyedTipText(QAction* on, const QString& what, const QString& more) {
    for (KeyedTip& tip : keyed_tips_) {
        if (tip.action != on) continue;
        if (tip.what == what && tip.more == more) return;  // syncTooltips is not free
        tip.what = what;
        tip.more = more;
        syncTooltips();
        return;
    }
}

void MainWindow::syncTooltips() {
    const shortcuts::Bindings& keys = shortcuts::current();
    const auto spelled = [&keys](shortcuts::Id id) {
        return keys.sequenceFor(id).toString(QKeySequence::NativeText);
    };

    for (const KeyedTip& tip : keyed_tips_) {
        QString text = tip.what;
        // An unbound action gets the sentence and no parentheses. "( )" after a
        // sentence is worse than nothing: it reads as a key that failed to
        // print rather than as one that is not there.
        const QString key = spelled(tip.id);
        if (!key.isEmpty()) text += QStringLiteral(" (%1)").arg(key);
        if (!tip.more.isEmpty()) {
            QString rest = tip.more;
            for (const shortcuts::Id named : tip.also) rest = rest.arg(spelled(named));
            text += QLatin1Char('\n') + rest;
        }
        if (tip.action) tip.action->setToolTip(text);
        if (tip.widget) tip.widget->setToolTip(text);
    }
}

void MainWindow::chooseShortcuts() {
    ShortcutsDialog dialog(shortcuts::current(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    adoptShortcuts(dialog.bindings());

    QString trouble;
    if (shortcuts::current().save(shortcuts::userFilePath(), &trouble)) return;
    // The keyboard has already changed and works. What has failed is only
    // remembering it, so this says exactly that rather than implying the
    // rebinding did not happen.
    QMessageBox::warning(this, QStringLiteral("Animage"),
                         QStringLiteral("The shortcuts are changed, but could not be written "
                                        "down, so they will be back as they were next time:\n%1")
                             .arg(trouble));
}

// What the keyboard means, changed in one place.
//
// A disabled QAction does not consume its shortcut, and that is the whole
// mechanism rather than a side effect: turning Play off is what frees Return for
// a transform to validate with, and turning the frame steps off is what frees
// the arrows to nudge with. Nothing here has to know what the other mode does
// with the key it is giving up.
//
// Undo and Redo are absent from the list on purpose -- they are live in both
// modes and *redefined* in one, which the handlers ask about.
void MainWindow::setShortcutMode(shortcuts::Mode mode) {
    for (const auto& [id, action] : keyed_actions_) {
        action->setEnabled(shortcuts::liveIn(shortcuts::entryFor(id).modes, mode));
    }
    // The button goes with the action it duplicates. A button that still works
    // while its own shortcut does not is worse than either alone.
    if (play_button_) play_button_->setEnabled(mode == shortcuts::Mode::Normal);
}

// The keyboard belongs to a field, so the shortcuts let go of it.
//
// This is the transform's own mechanism and it is here for the same collision:
// Return is Play, and Return is also how a field is finished. A disabled QAction
// does not consume its shortcut, so turning Play off is exactly what lets Return
// reach whatever is being typed into -- which is what "the fact lives in the
// table and one function acts on it" is for. Doing it with a setEnabled call
// from the field's own code would be the thing shortcuts.h exists to stop.
//
// **Driven by the focus, and by nothing else.** This used to be reported by the
// two rename editors, each telling the window when it opened and closed, and
// that was wrong twice over. It was a count rather than a flag, because two
// editors can overlap for a moment and a flag would have handed the keyboard
// back with one still on screen. And it covered renames and nothing else, so
// Return in the brush size box was still Play: the box never got the key, the
// value was never applied, and the animation started playing instead. Every
// other field in the window had it too -- the onion count and the five numbers
// on the transform bar.
//
// The focus answers both. It is single-valued, so the overlap that needed
// counting cannot arise -- whoever has the keyboard now is the answer -- and it
// knows nothing about renaming, so a field added tomorrow is covered by having
// been added. See isTypingInto for what counts as a field, and note that it asks
// about the spin box as well as the line edit inside it: a spin box keeps the
// focus itself, through a focus proxy.
//
// Coming back, the mode is asked for rather than assumed: a transform can be
// live while a name is typed, and going back to Normal there would leave the
// nudge keys stepping frames instead.
void MainWindow::setTyping(bool typing) {
    if (typing) {
        setShortcutMode(shortcuts::Mode::Typing);
        return;
    }
    setShortcutMode(canvas_ && canvas_->transformIsLive() ? shortcuts::Mode::Transform
                                                          : shortcuts::Mode::Normal);
}

void MainWindow::buildMenus() {
    using shortcuts::Id;

    QMenu* file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(makeAction(Id::NewProject, [this] { newProject(); }));
    file->addAction(makeAction(Id::OpenProject, [this] { openProject(); }));
    file->addAction(makeAction(Id::SaveProject, [this] { saveProject(); }));
    file->addAction(makeAction(Id::SaveProjectAs, [this] { saveProjectAs(); }));
    file->addSeparator();
    // A still and a sequence are separate items because they arrive at
    // different times and do different things, not because the file picker
    // cannot tell them apart. See docs/importing.md for what "Video" will ask
    // on top of these two.
    QMenu* import = file->addMenu(QStringLiteral("&Import"));
    import->addAction(QStringLiteral("&Image..."), this, &MainWindow::importImage);
    import->addAction(QStringLiteral("Image &sequence..."), this,
                      &MainWindow::importImageSequence);
    // Present whether or not this build can decode anything: an item that comes
    // and goes with the Qt somebody installed is an item nobody can find twice,
    // and "why can I not do this here" is the question a disabled control
    // exists to answer. It says so when pressed.
    import->addAction(QStringLiteral("&Audio..."), this, &MainWindow::importAudio);
    file->addSeparator();
    // No key, so no row: the table is what the keyboard does, and an action with
    // nothing bound to it has nothing to say there.
    file->addAction(QStringLiteral("&Export sequences..."), this, &MainWindow::exportSequences);

    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    edit->addAction(makeAction(Id::Undo, [this] { undo(); }));
    edit->addAction(makeAction(Id::Redo, [this] { redo(); }));

    edit->addSeparator();
    // Beside Undo and Redo, and nothing goes in the Track menu: a selection is
    // an argument to editing operations rather than a property of a track.
    edit->addAction(makeAction(Id::Cut, [this] { clipboard(Clipboard::Cut); }));
    edit->addAction(makeAction(Id::Copy, [this] { clipboard(Clipboard::Copy); }));
    edit->addAction(makeAction(Id::Paste, [this] { clipboard(Clipboard::Paste); }));
    edit->addSeparator();
    edit->addAction(makeAction(Id::SelectAll, [this] { canvas_->selectEverything(); }));
    edit->addAction(makeAction(Id::Deselect, [this] { canvas_->clearSelection(); }));
    edit->addAction(makeAction(Id::EraseSelection, [this] {
        // Said out loud, like its four siblings on the same refusal list. This
        // used to discard a `bool`, so on a locked layer Ctrl+X named the
        // reason and Backspace did nothing at all with nothing said.
        const CanvasWidget::Refusal refusal = canvas_->eraseSelection();
        if (refusal != CanvasWidget::Refusal::None) {
            sayCannot(statusBar(), "erase", refusal);
            return;
        }
        refreshEverything();
    }));

    edit->addSeparator();
    // The framerate and the canvas both belong to the scene, not to a track:
    // every track in the scene runs at one framerate and is composited into one
    // picture. Putting either in the timeline panel said otherwise.
    edit->addAction(QStringLiteral("Scene settings..."), this, &MainWindow::chooseSceneSettings);
    // Beside it rather than in a Preferences of its own, which there is not one
    // of: this is the second thing about the program that is the animator's
    // rather than the project's, and the first is next to it.
    edit->addAction(QStringLiteral("Keyboard shortcuts..."), this, &MainWindow::chooseShortcuts);

    QMenu* animation = menuBar()->addMenu(QStringLiteral("&Animation"));
    play_action_ = makeAction(Id::Play, [this] { togglePlayback(); });
    animation->addAction(play_action_);
    animation->addSeparator();
    animation->addAction(makeAction(Id::PreviousFrame, [this] { stepFrame(-1); }));
    animation->addAction(makeAction(Id::NextFrame, [this] { stepFrame(1); }));
    animation->addAction(makeAction(Id::PreviousDrawing, [this] { stepDrawing(-1); }));
    animation->addAction(makeAction(Id::NextDrawing, [this] { stepDrawing(1); }));
    animation->addSeparator();
    animation->addAction(makeAction(Id::InsertDrawing, [this] { insertInterval(); }));
    animation->addAction(makeAction(Id::DuplicateDrawing, [this] { duplicateDrawing(); }));
    animation->addAction(makeAction(Id::DeleteDrawing, [this] { deleteDrawing(); }));
    animation->addSeparator();
    animation->addAction(makeAction(Id::HoldLonger, [this] { extendExposure(); }));
    animation->addAction(makeAction(Id::HoldShorter, [this] { shortenExposure(); }));

    // A menu of its own rather than more of Edit: a track is the other thing a
    // scene is made of, beside its layers, and the layer panel already has a
    // place of its own.
    QMenu* track_menu = menuBar()->addMenu(QStringLiteral("&Track"));
    track_menu->addAction(QStringLiteral("Add track"), this, &MainWindow::addTrack);
    track_menu->addAction(QStringLiteral("Duplicate track"), this,
                          &MainWindow::duplicateCurrentTrack);
    track_menu->addAction(QStringLiteral("Rename track..."), this, &MainWindow::renameTrack);
    track_menu->addAction(QStringLiteral("Delete track"), this, &MainWindow::removeCurrentTrack);
    track_menu->addSeparator();
    overwrite_action_ = track_menu->addAction(QStringLiteral("Overwrite drawings"));
    overwrite_action_->setCheckable(true);
    overwrite_action_->setToolTip(
        QStringLiteral("A new drawing lands on the playhead and takes over the rest of the\n"
                       "hold, instead of going in after it and making the shot longer."));
    connect(overwrite_action_, &QAction::toggled, this, &MainWindow::setOverwriteDrawings);

    // What the track shows once the playhead is past its last drawing. Tracks
    // share one timeline and are not obliged to be the same length, so this is
    // an ordinary question: a background drawn once under a character animated
    // over forty frames, or a four-drawing cycle for a walk.
    //
    // One word each, because the submenu's own title is the rest of the
    // sentence: "Past the last drawing > Hold" says the whole thing once. The
    // items used to repeat it -- "Hold the last drawing" under "Past the last
    // drawing" -- which is the same words twice and reads as though the two
    // might mean different things.
    QMenu* end_menu = track_menu->addMenu(QStringLiteral("Past the last drawing"));
    end_menu_ = end_menu;
    auto* ends = new QActionGroup(this);
    ends->setExclusive(true);
    const TrackEnd choices[] = {TrackEnd::Nothing, TrackEnd::HoldLast, TrackEnd::Cycle};
    for (const TrackEnd behaviour : choices) {
        // The same word the status bar says, with a capital because it begins a
        // menu item rather than a sentence about the track.
        QString word = endWord(behaviour);
        word[0] = word[0].toUpper();
        QAction* action = end_menu->addAction(word);
        action->setCheckable(true);
        ends->addAction(action);
        end_actions_.push_back({action, behaviour});
        connect(action, &QAction::triggered, this,
                [this, behaviour] { setTrackEnd(behaviour); });
    }

    QMenu* view = menuBar()->addMenu(QStringLiteral("&View"));
    view_menu_ = view;
    view->addAction(makeAction(Id::ActualSize, [this] { canvas_->resetView(); }));
    view->addAction(makeAction(Id::FitCanvas, [this] { canvas_->fitToCanvas(); }));
    // The drawing is not the canvas, and both are worth being able to frame:
    // one is what you are delivering, the other is everything you have made,
    // including whatever ran off the edge.
    view->addAction(makeAction(Id::FitDrawing, [this] { canvas_->fitToDrawing(); }));
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

    view->addSeparator();
    // Checked because the veil is what the canvas normally looks like; turning
    // it off is the exception, taken when what ran off the edge is the thing
    // being looked at.
    QAction* passe_partout = view->addAction(QStringLiteral("&Passe-partout"));
    passe_partout->setCheckable(true);
    passe_partout->setChecked(canvas_->passePartout());
    connect(passe_partout, &QAction::toggled, this,
            [this](bool shown) { canvas_->setPassePartout(shown); });
}

// The rest of the View menu, once there are docks to name.
//
// Split from buildMenus because of the order the window is built in and for no
// other reason: the menus come before either panel, so there is nothing to ask
// for a toggle action from yet.
//
// Why the menu has these at all is issue #50. A dock's title bar has a close
// button, and a close button is a *click*, so a pen works it perfectly well.
// Dragging a dock is a drag, and with a pen that does not work -- so a panel
// shut by accident had no way back that a pen could reach, and nothing in the
// window even said the panel was a thing that could be shown again. The docks
// keep their close buttons; what was missing was the way back.
void MainWindow::buildPanelViewActions() {
    if (!view_menu_ || !layer_dock_ || !timeline_dock_) return;

    view_menu_->addSeparator();

    // Qt's own action per dock rather than one written here: it takes the dock's
    // window title for its label, it checks and unchecks itself as the dock is
    // shown and hidden however that happens, and that "however" is the point --
    // the close button in the title bar keeps it in step for free.
    view_menu_->addAction(layer_dock_->toggleViewAction());
    view_menu_->addAction(timeline_dock_->toggleViewAction());

    // And both at once, on Tab.
    //
    // The condition is "either is showing" rather than "both are", which is what
    // makes the key a toggle instead of something that sometimes does nothing:
    // with one panel already closed, Tab takes the other away and the next press
    // brings both back. Answering "are they both up?" would leave the first
    // press showing a panel the hand was trying to hide.
    view_menu_->addAction(makeAction(shortcuts::Id::TogglePanels, [this] {
        const bool anything_shown = layer_dock_->isVisible() || timeline_dock_->isVisible();
        layer_dock_->setVisible(!anything_shown);
        timeline_dock_->setVisible(!anything_shown);
    }));

    view_menu_->addSeparator();
    // No key, so no row in the table -- the same rule the export entry follows.
    // This is a thing you go to the menu for once, not a thing a hand reaches
    // for while drawing.
    view_menu_->addAction(QStringLiteral("&Restore default view"), this,
                          &MainWindow::restoreDefaultView);
}

// Everything about where the panels are, back to how the window opened.
//
// One QByteArray does all of it. QMainWindow::saveState records each dock's
// area, whether it is floating, whether it is shown and how big it is, so there
// is no separate list of things to put back and no way for one of them to be
// forgotten -- which is the failure this shape is chosen to avoid.
void MainWindow::restoreDefaultView() {
    // Empty until the window has been shown once. Nothing sensible to restore
    // to before then, and the menu entry is reachable only from a shown window.
    if (default_layout_.isEmpty()) return;
    restoreState(default_layout_);

    // The timeline's height is a dock size, so it came back with the rest -- but
    // it came back at the height that was right for the track count the layout
    // was *saved* at. syncTimelineHeight works in differences, so handing it
    // that count is what makes it grow the strip to the count there is now.
    // Without this line, restoring a one-track layout into a three-track scene
    // would leave the strip two rows short and nothing would ever put them back.
    timeline_rows_shown_ = default_layout_rows_;
    syncTimelineHeight();
}

// Unstick the window's layout after a panel has been dragged out of it.
//
// **Issue #54, and it is Qt's bug rather than ours** -- reported upstream as
// [QTBUG-147209][] and **already fixed there**, which is why this is scoped to
// the one release that has it rather than run for ever.
//
// | Qt | what `endDrag` does with the flag | |
// |---|---|---|
// | 6.11.0 | `restore()`, defaulting to clear | correct |
// | **6.11.1** | `restore(QInternal::KeepSavedState)` | **the bug** |
// | 6.11.2 | `restore(QInternal::ClearSavedState)` | fixed |
//
// A regression, introduced by "Code cleanup in QMainWindowLayout" turning a
// defaulted bool into an enum and picking the wrong enumerator at one call
// site, and fixed by e9a22af5ab7f. So the CI builds never had it -- `ci.yml`
// pins Qt 6.8 -- and neither will a local build once MSYS2 moves past 6.11.1.
// **When the oldest Qt anyone builds this with is 6.11.2, delete all of this.**
//
// It reproduces in a plain `QMainWindow` with a central widget, a status bar and
// two stock docks, with none of this program in it.
//
// [QTBUG-147209]: https://bugreports.qt.io/browse/QTBUG-147209
//
// `QMainWindowLayout::setGeometry` begins with
//
//     if (savedState.isValid() || ...) return;
//
// and `savedState` is the copy the layout takes when a dock is *unplugged* at
// the start of a drag. A drag that ends with the panel outside the window never
// clears it. That is measured and not inferred: Qt does receive the mouse
// release, does run `endDrag`, does drop the mouse grab and does reclaim the
// space the panel left -- and leaves that one flag set behind it. From then on
// every child of the window keeps the geometry it had. The canvas stays wider
// than the window and the status bar sits above the bottom with unpainted window
// behind it, and nothing lays any of it out again for as long as the state
// lasts.
//
// What gets noticed first is a **blank strip** where the panel used to be, which
// is this seen through the canvas's `WA_OpaquePaintEvent`: the canvas is not
// covering that region and nothing else paints it either. Docking the panel
// again clears the flag, because docking is a `plug` and `plug` does clear it --
// which is the whole reason the fault presents as intermittent rather than as a
// window that is simply broken.
//
// **A round trip through the saved layout is the only cure that works, and that
// is measured rather than chosen.** Hiding and showing the dock,
// `setDockOptions`, `addDockWidget` again, `invalidate()` and `setTitleBarWidget`
// were each applied to the frozen state and each left it frozen.
// `QMainWindow::restoreState` clears `savedState` on its way through, so saving
// the layout and putting it straight back leaves every panel exactly where it is
// and unsticks the one thing that was stuck.
//
// Nothing has to be put back afterwards, which is the difference from Restore
// default view: that reads back an *older* layout and has to tell
// `syncTimelineHeight` what track count the layout was saved at. This saves the
// layout the window is in at that moment, so the timeline dock comes back at the
// height it already had and `timeline_rows_shown_` still describes it.
//
// **And it was checked that the round trip moves nothing**, because the obvious
// worry about it is that Qt writes a floating panel's geometry into the state
// and reads it back through `constrainedRect`. It does not move the panel, with
// Qt's decoration or with the frameless one issue #50 gives it -- both measured.
// Eight lines that took the geometry before and put it back after were written
// against a panel that was reported landing below the pointer, and then deleted:
// the panel does that with this fix and without it, so it is not this. It is
// issue #56 instead.
//
// **`shots` cannot see any of this**, and it is worth saying why rather than
// leaving somebody to try. The state cannot be reached by code: `setFloating`
// makes a panel float without ever entering Qt's drag, so it does not freeze
// anything, and a synthetic drag sent to a `QDockWidget` leaves Qt's own state
// machine half finished and produces convincing symptoms that are not this one.
// A real hand is the only way in. `QWidget::grab()` would force a repaint on top
// of that, so even a frozen window photographs clean -- the geometry is wrong,
// the painting is not.
// Whether the Qt this is *running* against is the one that leaves the flag set.
// The runtime version and not QT_VERSION: Qt is a DLL here, so the build that
// compiled this and the build that will run it need not agree.
static bool qtForgetsToClearTheDragState() {
    const QVersionNumber qt = QLibraryInfo::version();
    return qt >= QVersionNumber(6, 11, 1) && qt < QVersionNumber(6, 11, 2);
}

void MainWindow::wakeLayout() {
    if (!qtForgetsToClearTheDragState()) return;
    if (waking_layout_) return;
    waking_layout_ = true;
    // Out of whatever call stack asked for it. The settled() this answers can
    // arrive from inside Qt's own restoreState -- Restore default view floats a
    // panel, and a panel floated with nothing held settles at once -- and
    // starting another restoreState from in there is asking for trouble.
    QTimer::singleShot(0, this, [this] {
        restoreState(saveState());
        waking_layout_ = false;
    });
}

// Split from the menus at the seam that was already in the source: nothing
// crosses it. The menu half touches overwrite_action_ and end_actions_; this
// half touches the four tool actions and their group, the size box, the
// pressure box, the two swatches and the transform bar. Both share makeAction
// and keyedTip, which were already outside either.
void MainWindow::buildToolBar() {
    using shortcuts::Id;

    QToolBar* tools = addToolBar(QStringLiteral("Tools"));
    tools->setMovable(false);

    // One exclusive group: these compete for the pen, which is what a tool is.
    // Picking any of them while a transform is live commits it -- reaching for
    // the brush means you have finished placing the drawing, and it is the same
    // rule as changing frame. That is setTool's job now, and not spelled out
    // three times here: this group and the canvas's Tool are the same fact.
    auto* mode = new QActionGroup(this);
    brush_action_ = makeAction(Id::Brush, [this] {
        canvas_->setTool(CanvasWidget::Tool::Brush);
        syncToolSettings();
    });
    eraser_action_ = makeAction(Id::Eraser, [this] {
        canvas_->setTool(CanvasWidget::Tool::Eraser);
        syncToolSettings();
    });
    lasso_action_ = makeAction(Id::Lasso, [this] {
        canvas_->setTool(CanvasWidget::Tool::Lasso);
        syncToolSettings();
    });
    transform_action_ = makeAction(Id::Transform, [this] { chooseTransformTool(); });
    for (QAction* action : {brush_action_, eraser_action_, lasso_action_, transform_action_}) {
        action->setCheckable(true);
        mode->addAction(action);
        tools->addAction(action);
    }
    brush_action_->setChecked(true);
    // Every tool says which key it is on, including the two that said nothing at
    // all. A toolbar button with no tooltip is a button whose key you find out
    // about by reading the source.
    keyedTip(brush_action_, Id::Brush, QStringLiteral("Draw on the layer you are on"),
             QStringLiteral("The pen's pressure sets the width.\n"
                            "Alt+click picks up the colour under the pointer."));
    keyedTip(eraser_action_, Id::Eraser, QStringLiteral("Rub out what is under the pointer"),
             QStringLiteral("Alt+right-drag changes its size, the same as the brush's."));
    keyedTip(lasso_action_, Id::Lasso,
             QStringLiteral("Loop round part of the drawing to transform, copy or erase it"),
             QStringLiteral("It does not clip the brush: you can still draw anywhere.\n"
                            "A click clears it, and it is not saved with the project."));
    keyedTip(transform_action_, Id::Transform,
             QStringLiteral("Move, turn or resize this drawing on the layer you are on"),
             QStringLiteral("With nothing selected it takes the whole drawing.\n"
                            "%1 applies, %2 cancels, and the nudge keys move it a pixel at a "
                            "time."),
             {Id::TransformApply, Id::TransformCancel});

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

    // In no menu and no toolbar, so they need adding to a widget by hand: an
    // action nothing holds has a shortcut nothing listens for.
    addAction(makeAction(Id::SmallerBrush, [this] { nudgeBrushRadius(1.0 / 1.25); }));
    addAction(makeAction(Id::LargerBrush, [this] { nudgeBrushRadius(1.25); }));

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

    // Two halves of one switch: the colour on the left, no colour on the right,
    // and whichever is rimmed is what the brush is holding. Adjacent and
    // unspaced, because two patches with a gap between them are two controls
    // and these are two positions of one.
    //
    // Transparency is a colour on a colour layer, so it belongs among the
    // colours rather than in a mode somewhere else -- and it needs to *look*
    // like one, which a button saying "None" does not. There is nothing about
    // that word to say it has anything to do with colour.
    //
    // Neither half opens the dialog. Clicking a swatch chooses what it shows,
    // which is what a swatch does everywhere; changing the colour is what the
    // Colour... button is for. Having the patch do both meant there was no way
    // to simply select the colour you could already see.
    auto* swatches = new QWidget(this);
    auto* switch_row = new QHBoxLayout(swatches);
    switch_row->setContentsMargins(0, 0, 0, 0);
    switch_row->setSpacing(0);

    colour_swatch_ = new QPushButton(swatches);
    colour_swatch_->setFixedSize(30, 22);
    colour_swatch_->setFocusPolicy(Qt::NoFocus);
    colour_swatch_->setCursor(Qt::PointingHandCursor);
    colour_swatch_->setToolTip(
        QStringLiteral("Paint with this colour.\n"
                       "Colour... changes it; Alt+click on the drawing picks one up."));
    connect(colour_swatch_, &QPushButton::clicked, this, &MainWindow::chooseSolidColour);
    switch_row->addWidget(colour_swatch_);

    transparent_swatch_ = new QPushButton(swatches);
    transparent_swatch_->setFixedSize(30, 22);
    transparent_swatch_->setFocusPolicy(Qt::NoFocus);
    transparent_swatch_->setCursor(Qt::PointingHandCursor);
    transparent_swatch_->setToolTip(
        QStringLiteral("Scribble no colour at all.\n"
                       "Fills the region it wins with nothing, and takes colour\n"
                       "back off the spots it covers.\n"
                       "Colour layers only -- a mark there is a label, not paint."));
    connect(transparent_swatch_, &QPushButton::clicked, this, &MainWindow::chooseTransparent);
    switch_row->addWidget(transparent_swatch_);

    tools->addWidget(swatches);

    tools->addSeparator();
    // This acts on the drawing in front of you, not on the layer as a whole, so
    // it belongs with the drawing tools rather than in the layer panel.
    auto* clear = new QPushButton(QStringLiteral("Clear"), this);
    clear->setToolTip(QStringLiteral("Empty the current layer on this drawing only.\n"
                                     "Other drawings keep theirs."));
    clear->setFocusPolicy(Qt::NoFocus);
    connect(clear, &QPushButton::clicked, this, &MainWindow::clearCurrentLayer);
    tools->addWidget(clear);

    buildTransformBar();

    // Said rather than assumed. Every action is enabled by default and the
    // window opens in Normal, so this changes nothing today -- and it is the
    // only place the enabled state is ever set, which is the property worth
    // having: there is one answer to "why is this action off" and it is here.
    setShortcutMode(shortcuts::Mode::Normal);
}

// The bar exists only while a transform does.
//
// Not a panel and not a permanent row of controls: what is on it belongs to the
// transform that is happening rather than to the track or to the program. A
// permanently visible scope control that is off by default is a trap precisely
// *because* it is off by default -- you forget it is on in the one session where
// it is, and an ordinary drag then rewrites the whole shot.
//
// And it floats *over* the canvas rather than taking a row above it. As a
// QToolBar it was in the window's layout, so the canvas lost its height on the
// way in and got it back on the way out -- which moved the drawing on screen at
// the start and end of every transform, exactly while you were placing it. A
// child of the canvas has no layout to belong to and costs the canvas nothing;
// what it costs instead is being positioned by hand, in placeTransformBar.
void MainWindow::buildTransformBar() {
    transform_bar_ = new QFrame(canvas_);
    transform_bar_->setObjectName(QStringLiteral("transformBar"));
    // Opaque and framed: it sits over the drawing, and a strip of controls with
    // line art showing through it is unreadable in exactly the case it is for.
    transform_bar_->setFrameShape(QFrame::StyledPanel);
    transform_bar_->setAutoFillBackground(true);
    transform_bar_->setVisible(false);

    auto* row = new QHBoxLayout(transform_bar_);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(4);

    // What this transform is *of*, first on the bar, because it is the thing
    // that decides what everything to the right of it will write. A layer bake
    // and a drawing transform have the same five fields and the same Apply, and
    // they differ by two orders of magnitude in what pressing it costs.
    transform_scope_ = new QLabel(transform_bar_);
    transform_scope_->setToolTip(
        QStringLiteral("Whether this moves the drawing in front of you or every\n"
                       "drawing of the layer. Which one it is was decided by how the\n"
                       "transform was started: the Transform tool takes this drawing,\n"
                       "the layer panel's button takes the layer."));
    row->addWidget(transform_scope_);
    row->addSpacing(8);

    const auto number = [&](const QString& label, double lowest, double highest, int decimals,
                            const QString& suffix) {
        row->addWidget(new QLabel(label, transform_bar_));
        auto* box = new QDoubleSpinBox(transform_bar_);
        box->setRange(lowest, highest);
        box->setDecimals(decimals);
        box->setSuffix(suffix);
        // ClickFocus and not the default WheelFocus, and editingFinished hands
        // the keyboard back: the same discipline as the brush size box, and it
        // is why the canvas has the keyboard at all.
        box->setFocusPolicy(Qt::ClickFocus);
        connect(box, &QDoubleSpinBox::valueChanged, this,
                [this](double) { onTransformFieldEdited(); });
        connect(box, &QDoubleSpinBox::editingFinished, this, [this] { canvas_->setFocus(); });
        row->addWidget(box);
        return box;
    };

    const auto whole = [&](const QString& label) {
        row->addWidget(new QLabel(label, transform_bar_));
        auto* box = new QSpinBox(transform_bar_);
        box->setRange(-100000, 100000);
        box->setSuffix(QStringLiteral(" px"));
        box->setFocusPolicy(Qt::ClickFocus);
        connect(box, &QSpinBox::valueChanged, this, [this](int) { onTransformFieldEdited(); });
        connect(box, &QSpinBox::editingFinished, this, [this] { canvas_->setFocus(); });
        row->addWidget(box);
        return box;
    };

    // Whole pixels, because a translation is the transform this is mostly for
    // and half a pixel of it cannot be aimed at -- it only resamples.
    transform_dx_ = whole(QStringLiteral("dx"));
    transform_dy_ = whole(QStringLiteral("dy"));
    transform_rotation_ =
        number(QStringLiteral("rotation"), -3600.0, 3600.0, 1, QString::fromUtf8("°"));
    // Two scale fields and not one. The bar could show a single number only if
    // every handle scaled both axes, and the corner/edge distinction is what
    // frees Shift to constrain rotation, which is worth more.
    transform_scale_x_ = number(QStringLiteral("scale X"), 1.0, 10000.0, 1,
                                QStringLiteral("%"));
    transform_scale_y_ = number(QStringLiteral("scale Y"), 1.0, 10000.0, 1,
                                QStringLiteral("%"));

    // The two mirrors, next to the numbers rather than beside Apply: they change
    // what the transform is, the way the fields do, and Apply is what ends it.
    //
    // Checkable, because a flip is a state of the transform and not a thing that
    // happens to the drawing -- pressing it twice is exactly where you started,
    // and until Apply nothing has been written either way.
    row->addSpacing(8);
    const auto mirror = [&](const QString& text, const QString& tip, FlipAxis axis) {
        auto* b = new QPushButton(text, transform_bar_);
        b->setCheckable(true);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);  // keep the pen and the keyboard on the canvas
        connect(b, &QPushButton::clicked, this, [this, axis] { flipTransform(axis); });
        row->addWidget(b);
        return b;
    };
    transform_flip_x_ =
        mirror(QStringLiteral("Flip X"),
               QStringLiteral("Mirror it left to right, about the middle of the box.\n"
                              "A mirror is exact: it moves the pixels and does not resample "
                              "them."),
               FlipAxis::X);
    transform_flip_y_ = mirror(QStringLiteral("Flip Y"),
                               QStringLiteral("Mirror it top to bottom, about the middle of "
                                              "the box."),
                               FlipAxis::Y);

    row->addSpacing(12);
    const auto button = [&](const QString& text, shortcuts::Id id, const QString& what,
                            const QString& more, auto handler) {
        auto* b = new QPushButton(text, transform_bar_);
        b->setFocusPolicy(Qt::NoFocus);  // keep the pen and the keyboard on the canvas
        keyedTip(b, id, what, more);
        connect(b, &QPushButton::clicked, this, handler);
        row->addWidget(b);
        return b;
    };
    transform_apply_ = button(QStringLiteral("Apply"), shortcuts::Id::TransformApply,
                              QStringLiteral("Bake it into the drawing"), QString(),
                              [this] { canvas_->applyTransform(); });
    button(QStringLiteral("Cancel"), shortcuts::Id::TransformCancel,
           QStringLiteral("Put it back where it was"),
           QStringLiteral("Nothing was written, so this leaves no undo step."),
           [this] { canvas_->cancelTransform(); });
}

// Where the bar sits on the canvas.
//
// Centred along the top, which is the one place that is neither a corner
// somebody has panned their drawing into nor the middle of what is being moved.
// It is a child of the canvas and so belongs to no layout, which is the whole
// point -- appearing costs the canvas no height -- and the price is that its
// position is worked out here and nowhere else.
void MainWindow::placeTransformBar() {
    if (!transform_bar_ || !canvas_) return;

    constexpr int kEdge = 8;
    const QSize wanted = transform_bar_->sizeHint();
    const int width = std::min(wanted.width(), std::max(0, canvas_->width() - 2 * kEdge));
    const int x = std::max(kEdge, (canvas_->width() - width) / 2);
    transform_bar_->setGeometry(x, kEdge, width, wanted.height());
}

// Entering the tool is what starts a transform. There is no "transform
// selection" button because the tool is the button, which is what makes "press
// it with nothing selected and it boxes the whole drawing" fall out.
void MainWindow::chooseTransformTool() {
    stopPlayback();
    if (canvas_->transformIsLive()) return;

    // **An imported *still* goes through the placement path, whichever door was
    // used.** The tool takes the drawing in front of you, and for a reference
    // layer of one file that is one picture -- so both doors mean the same
    // thing and there is nothing for them to disagree about. Refusing and
    // pointing at the other button would be a rule with no consequence behind
    // it, which is the kind of rule people learn as "this program is fussy".
    //
    // **A sequence is where the argument stops**, and the routing stops with
    // it. "Both doors mean one thing when there is one picture" was always the
    // reason, and a sequence is several -- so the tool, which says it moves
    // *this drawing*, has nothing here it can honestly do. It refuses through
    // the ordinary path, where `Refusal::ReferenceLayer` already names the way
    // in: convert it to drawings first. The tool is greyed out there too, so
    // the refusal is something you see before you reach for it rather than
    // after -- see syncLayerButtons.
    //
    // "Two doors and no switch" is untouched by this: what that refuses is
    // changing the scope of a gesture already on screen, and this is decided
    // before there is one.
    if (const Layer* layer = currentLayer(); layer && layer->kind == LayerKind::Reference &&
                                             layer->reference_sources.size() <= 1) {
        transformLayerThroughTime();
        return;
    }

    const CanvasWidget::Refusal refusal = canvas_->beginTransform();
    if (refusal == CanvasWidget::Refusal::None) return;

    // Said out loud. A tool that silently does nothing is a bug as far as
    // anybody holding the pen is concerned -- the same reason the status bar
    // says why the brush will not draw past the end of a track, and the reason
    // `sayCannot` stays quiet about that one rather than saying it twice.
    sayCannot(statusBar(), "transform", refusal);
    brush_action_->setChecked(true);
    canvas_->setTool(CanvasWidget::Tool::Brush);
    syncToolSettings();
}

void MainWindow::clipboard(Clipboard what) {
    stopPlayback();

    CanvasWidget::Refusal refusal = CanvasWidget::Refusal::None;
    const char* verb = "copy";
    switch (what) {
        case Clipboard::Cut:
            verb = "cut";
            refusal = canvas_->cutSelection();
            break;
        case Clipboard::Copy: refusal = canvas_->copySelection(); break;
        case Clipboard::Paste:
            verb = "paste";
            refusal = canvas_->paste();
            break;
    }

    if (refusal != CanvasWidget::Refusal::None) {
        // **A refusal whose reason is "convert it first" offers to do that**,
        // rather than saying no and leaving somebody to find the way forward.
        // The brush has worked this way since the conversion existed; this is
        // the same rule applied to the other three gestures that meet the same
        // wall, on the user's call.
        //
        // Only this one refusal. The rest are true refusals -- a locked layer,
        // an empty clipboard, marks copied off a different kind of layer -- and
        // turning any of those into an offer would be a dialog proposing
        // something nobody asked about.
        if (refusal == CanvasWidget::Refusal::ReferenceLayer) {
            offerToConvertRefusedLayer();
            return;
        }
        sayCannot(statusBar(), verb, refusal);
        return;
    }
    // Only a cut has written anything. A paste has not: it arrives as a float,
    // and refreshEverything would put the canvas back on the timeline's frame --
    // which is the thing that commits a float, so a paste would bake itself
    // before it could be placed.
    if (what == Clipboard::Cut) refreshEverything();
    syncStatus();
}

void MainWindow::onTransformBegan() {
    // A paste arrives here without anybody having pressed the tool, and the
    // float it lands in is a transform like any other -- so the tool says so.
    // Only the button: the canvas set its own Tool when it took the float, and
    // this handler putting the other tools down by hand is what used to leave
    // the eraser up behind a checked Transform button.
    if (transform_action_ && !transform_action_->isChecked()) {
        transform_action_->setChecked(true);
    }
    if (transform_scope_) {
        // Set before the bar is placed, because it changes the bar's width and
        // the placement is worked out from the size hint.
        //
        // Three words rather than two, and the third is the one that carries
        // news. "Whole layer" and "This drawing" both say how much is about to
        // be written over; "Placing" says that nothing is -- the numbers are
        // stored and the picture is derived from the file again at them, so
        // this box can be opened and adjusted for ever at no cost to the
        // picture. That is the opposite of what the other two mean, and it is
        // not something anybody would assume from a box that looks the same.
        transform_scope_->setText(canvas_->transformIsPlacement()
                                      ? QStringLiteral("Placing — nothing is written")
                                  : canvas_->transformIsWholeLayer()
                                      ? QStringLiteral("Whole layer")
                                      : QStringLiteral("This drawing"));
    }
    if (transform_bar_) {
        // Placed before it is shown: the fields have just been given their
        // values, so this is the first moment its size hint means anything.
        placeTransformBar();
        transform_bar_->setVisible(true);
        transform_bar_->raise();
    }
    // What Apply does depends on which gesture is on screen, and the two
    // differ in the one way this program is most careful about: a bake writes
    // every drawing it touches and a placement writes nothing at all. A button
    // that says "bake" over a gesture that stores would be undoing, in the
    // place somebody looks for reassurance, exactly what storing is for.
    if (transform_apply_) {
        setKeyedTipText(transform_apply_,
                        canvas_->transformIsPlacement()
                            ? QStringLiteral("Put the picture down here. Nothing is written")
                            : QStringLiteral("Bake it into the drawing"));
    }

    setShortcutMode(shortcuts::Mode::Transform);
    syncTransformFields();
    syncStatus();
}

void MainWindow::onTransformEnded() {
    // The standing message goes with the thing it was about. Committed or
    // cancelled, there is no longer a box for it to be describing -- and it has
    // no timeout to take it down on its own.
    if (!transform_fitted_) {
        statusBar()->clearMessage();
        transform_fitted_ = true;
    }
    if (transform_bar_) transform_bar_->setVisible(false);
    setShortcutMode(shortcuts::Mode::Normal);
    // Back to the brush, which is what you were going to do next. setChecked
    // does not emit triggered, so this cannot come back round through the
    // handler that commits a transform.
    if (transform_action_ && transform_action_->isChecked()) {
        brush_action_->setChecked(true);
        syncToolSettings();
    }
    refreshEverything();
}

void MainWindow::syncTransformFields() {
    if (!transform_dx_) return;
    const Transform values = canvas_->transformValues();

    updating_transform_fields_ = true;
    transform_dx_->setValue(static_cast<int>(std::lround(values.dx)));
    transform_dy_->setValue(static_cast<int>(std::lround(values.dy)));
    transform_rotation_->setValue(values.rotation);
    transform_scale_x_->setValue(values.scale_x * 100.0);
    transform_scale_y_->setValue(values.scale_y * 100.0);
    // The buttons say which way round the drawing is now, which is why they are
    // checkable and why this is the only place they are set. setChecked emits
    // toggled and not clicked, and the handler is on clicked, so this cannot
    // come back round and flip it again.
    if (transform_flip_x_) transform_flip_x_->setChecked(values.flip_x);
    if (transform_flip_y_) transform_flip_y_->setChecked(values.flip_y);
    updating_transform_fields_ = false;

    // A state and not an event, so it is put up when the ceiling is crossed and
    // taken down when it is crossed back -- with no timeout in between.
    //
    // A timed message was wrong twice over here. It says the box is red while
    // it may already be blue again, and at 10000% the box is the one thing that
    // cannot correct it: an over-budget box is at least sixteen thousand pixels
    // across, so at the furthest this zooms out its nearest edge is still off
    // the viewport and the bar is the only place the answer appears at all.
    //
    // Only on the change, because a drag crosses the ceiling on one move and
    // then goes on sending them.
    const bool fits = canvas_->transformFitsInMemory();
    if (fits != transform_fitted_) {
        if (fits) {
            statusBar()->clearMessage();
        } else {
            statusBar()->showMessage(
                QStringLiteral("Scaled too large to bake into a drawing. Scale it back down, or "
                               "cancel it."));
        }
    }
    transform_fitted_ = fits;
}

void MainWindow::flipTransform(FlipAxis axis) {
    if (!canvas_->transformIsLive()) return;

    Transform values = canvas_->transformValues();
    if (axis == FlipAxis::X) {
        values.flip_x = !values.flip_x;
    } else {
        values.flip_y = !values.flip_y;
    }
    // Through setTransformValues like the fields, which puts the pivot back to
    // the middle of what was picked up -- so the mirror is about the middle of
    // the box whatever the last handle drag pivoted on, and the numbers go on
    // meaning what they said.
    canvas_->setTransformValues(values);
}

void MainWindow::onTransformFieldEdited() {
    if (updating_transform_fields_ || !canvas_->transformIsLive()) return;

    // Started from what the transform is rather than from the fields alone, so
    // that the two flips -- which have no field -- survive typing a number.
    Transform values = canvas_->transformValues();
    values.dx = transform_dx_->value();
    values.dy = transform_dy_->value();
    values.rotation = transform_rotation_->value();
    values.scale_x = transform_scale_x_->value() / 100.0;
    values.scale_y = transform_scale_y_->value() / 100.0;
    canvas_->setTransformValues(values);
}

void MainWindow::buildLayerPanel() {
    auto* dock = new QDockWidget(QStringLiteral("Layers"), this);
    // saveState skips a dock with no object name and warns, so the layout that
    // Restore default view reads back would come up missing this panel. The name
    // is stored in that state and is not shown anywhere.
    dock->setObjectName(QStringLiteral("layersDock"));
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    // What lets a pen pick this panel up again once it is floating. See
    // FloatingDockFrame and issue #50. The moment it works out -- the drag is
    // over -- is also when this window's layout has to be unstuck; issue #54.
    connect(new FloatingDockFrame(dock), &FloatingDockFrame::settled, this,
            &MainWindow::wakeLayout);
    layer_dock_ = dock;

    auto* panel = new QWidget(dock);
    // Wide enough for the colour-layer box before it is there.
    //
    // The box appears and disappears as you move between layers, and a dock
    // that resizes to fit it drags the canvas sideways every time you click a
    // colour layer -- a lot of movement to pay for a panel. Claiming the width
    // up front costs a few pixels beside a stack of ordinary layers and nothing
    // else; the two widest things in the box are told not to ask for more than
    // this, so nothing inside it can push the number up again.
    panel->setMinimumWidth(232);
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
    layer_list_ = new LayerList(panel);
    layer_list_->setColumnCount(2);
    // The first of these is replaced with the current track's name on every
    // rebuild -- see rebuildLayerList, which is where it means something. This
    // is the value before the first one runs and nothing else.
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
    // Restacking is a drag, and the list is told to change nothing by it: it
    // says where the row landed, the document moves the layer, and the panel is
    // rebuilt from the document. See LayerList.
    layer_list_->reordered = [this](int from, int to) { moveLayerTo(from, to); };
    // And renaming is the same shape: a double click opens an editor on the
    // layer's own name, and what is typed goes to the document.
    layer_list_->nameOf = [this](int row) -> QString {
        const Track* track = doc_.scene().findTrack(track_);
        if (!track || row < 0 || static_cast<std::size_t>(row) >= track->layers.size()) {
            return QString();
        }
        return QString::fromStdString(track->layers[static_cast<std::size_t>(row)].name);
    };
    layer_list_->renamed = [this](int row, const QString& name) { renameLayer(row, name); };

    // Everything a colour layer has that an ordinary one does not, in one box
    // that is simply absent the rest of the time. A group of controls greyed
    // out on every layer but one is a permanent question about a layer you may
    // not even have.
    colour_settings_ = new QGroupBox(QStringLiteral("Colour layer"), panel);
    // Takes the width it is given and asks for none of its own.
    //
    // A minimum on the panel was not enough and the measurement said so: the
    // dock is sized from the panel's *preferred* width, not its minimum, so the
    // box appearing took it from 274 to 292 and selecting a colour layer shoved
    // the canvas eighteen pixels sideways. Ignored drops its sizeHint out of
    // that sum entirely, and the panel's minimum below is what actually
    // reserves the room.
    colour_settings_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* colour_layout = new QVBoxLayout(colour_settings_);
    colour_layout->setContentsMargins(6, 4, 6, 6);
    colour_layout->setSpacing(4);

    colour_layout->addWidget(new QLabel(QStringLiteral("Cut against"), colour_settings_));
    ctg_sources_ = new QListWidget(colour_settings_);
    ctg_sources_->setMaximumHeight(90);
    ctg_sources_->setFocusPolicy(Qt::NoFocus);
    // A long layer name must scroll, not widen the dock.
    ctg_sources_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ctg_sources_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    ctg_sources_->setMinimumWidth(0);
    ctg_sources_->setToolTip(
        QStringLiteral("The line art this layer's fill stops at.\n"
                       "More than one is normal: cutting against a rough as well as a\n"
                       "clean closes gaps that leak from either alone."));
    connect(ctg_sources_, &QListWidget::itemChanged, this, &MainWindow::onCtgSourcesChanged);
    colour_layout->addWidget(ctg_sources_);

    ctg_inherit_ = new QCheckBox(QStringLiteral("Carry marks to drawings with none"),
                                 colour_settings_);
    ctg_inherit_->setFocusPolicy(Qt::NoFocus);
    ctg_inherit_->setToolTip(
        QStringLiteral("Colour the first drawing of a run and the run is coloured.\n"
                       "A drawing you scribble on takes over from there.\n"
                       "Off, a drawing with no marks of its own is simply empty."));
    connect(ctg_inherit_, &QCheckBox::toggled, this, &MainWindow::onCtgSettingChanged);
    colour_layout->addWidget(ctg_inherit_);

    auto* direction_row = new QWidget(colour_settings_);
    auto* direction_layout = new QHBoxLayout(direction_row);
    direction_layout->setContentsMargins(16, 0, 0, 0);
    direction_layout->addWidget(new QLabel(QStringLiteral("Carry"), direction_row));
    ctg_direction_ = new QComboBox(direction_row);
    ctg_direction_->addItem(QStringLiteral("forwards"));
    ctg_direction_->addItem(QStringLiteral("backwards"));
    ctg_direction_->addItem(QStringLiteral("both ways"));
    ctg_direction_->setFocusPolicy(Qt::NoFocus);
    ctg_direction_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ctg_direction_->setMinimumContentsLength(8);
    ctg_direction_->setToolTip(
        QStringLiteral("Forwards: a drawing shows the nearest earlier drawing's marks.\n"
                       "Backwards: the nearest later one's -- for colouring the drawing\n"
                       "you have in front of you and having it apply to the run before it.\n"
                       "Both ways: whichever is fewer drawings off, which is what fills the\n"
                       "gaps between drawings you have coloured rather than only what\n"
                       "follows them."));
    connect(ctg_direction_, &QComboBox::currentIndexChanged, this,
            [this](int) { onCtgSettingChanged(); });
    direction_layout->addWidget(ctg_direction_, 1);
    colour_layout->addWidget(direction_row);

    ctg_follow_ = new QCheckBox(QStringLiteral("Move marks with the drawing"),
                                colour_settings_);
    ctg_follow_->setFocusPolicy(Qt::NoFocus);
    ctg_follow_->setToolTip(
        QStringLiteral("Where marks are carried to a drawing, move them by however far\n"
                       "the line art has moved between the two, rather than leaving them\n"
                       "where they were drawn.\n\n"
                       "One shift for the whole drawing, measured from the line art each\n"
                       "time and stored nowhere. Left alone, a carried mark holds its\n"
                       "region until the drawing has moved about half that region's width\n"
                       "-- which between two drawings is not much."));
    connect(ctg_follow_, &QCheckBox::toggled, this, &MainWindow::onCtgSettingChanged);
    colour_layout->addWidget(ctg_follow_);

    layout->addWidget(colour_settings_);

    auto* opacity_row = new QWidget(panel);
    auto* opacity_layout = new QVBoxLayout(opacity_row);
    opacity_layout->setContentsMargins(0, 0, 0, 0);
    opacity_layout->addWidget(new QLabel(QStringLiteral("Layer opacity"), opacity_row));
    opacity_ = new QSlider(Qt::Horizontal, opacity_row);
    opacity_->setRange(0, 100);
    opacity_->setValue(100);
    opacity_->setFocusPolicy(Qt::NoFocus);
    // If the handle here or on Scene settings' Resolution looks cut flat at 0%
    // or 100%, that is QTBUG-140649 in the windows11 style and it is not ours to
    // fix: Qt cut it at both ends of the travel until 6.10.1. Issue #75 has a
    // workaround that was written for it, believed for a day, and deleted --
    // read that before writing another one.
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

    // Issue #25, and it is a button here rather than a switch on the transform
    // bar. The bar exists only while a transform does, so a scope control on it
    // would be asking you to change what a gesture already on screen is about;
    // a button is a second door into the gesture, which is the same shape the
    // Transform tool already has -- the tool is the button, and so is this.
    //
    // Beside Add and Remove because it is the third thing you do to a layer as
    // a whole, and what it costs is on that scale: one press writes every
    // drawing in the layer.
    layer_transform_ = panelButton(QStringLiteral("Transform layer through time"),
                                   &MainWindow::transformLayerThroughTime);

    // **No Convert to drawings button, on the user's call**, and the argument it
    // reverses was written right here: that a popup is discoverability and a
    // button is findability, and a control that comes and goes is one nobody can
    // find twice. What that argument missed is that nobody goes looking for
    // this. Converting is not a thing you set out to do -- it is what you find
    // out you need when a stroke will not land, and the refusal is already
    // where that happens. A permanent button for it was a fourth control in the
    // panel, greyed on every layer but one, teaching a concept the program can
    // raise for itself at the only moment anybody wants it.

    // No Move up and Move down. The stack is restacked by dragging a row, which
    // is one gesture for any distance where the buttons were one click per
    // position -- and it is the gesture the timeline's rows now take too.
    layer_remove_ = panelButton(QStringLiteral("Remove layer"), &MainWindow::removeCurrentLayer);

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::buildTimelinePanel() {
    auto* dock = new QDockWidget(QStringLiteral("Timeline"), this);
    // See the layer dock: without this the saved layout would not have it.
    dock->setObjectName(QStringLiteral("timelineDock"));
    dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    // See the layer dock: the pen reaches this one back the same way, and the
    // layout needs the same waking when this one is dragged out.
    connect(new FloatingDockFrame(dock), &FloatingDockFrame::settled, this,
            &MainWindow::wakeLayout);
    timeline_dock_ = dock;

    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* controls = new QWidget(panel);
    auto* row = new QHBoxLayout(controls);
    row->setContentsMargins(0, 0, 0, 0);

    // Every one of these buttons duplicates an action, so every one of them used
    // to end its tooltip in a typed-out key: "(Enter)", "(Ctrl+D)", "(Delete)".
    // They are composed from the bindings now -- see keyedTip.
    const auto button = [&](const QString& text, shortcuts::Id id, const QString& what,
                            const QString& more, auto handler) {
        auto* b = new QPushButton(text, controls);
        b->setFocusPolicy(Qt::NoFocus);  // keep the pen working after a click
        keyedTip(b, id, what, more);
        connect(b, &QPushButton::clicked, this, handler);
        row->addWidget(b);
        return b;
    };

    play_button_ = button(QStringLiteral("Play"), shortcuts::Id::Play,
                          QStringLiteral("Play the timeline in a loop"), QString(),
                          &MainWindow::togglePlayback);
    row->addSpacing(12);

    button(QStringLiteral("+ Drawing"), shortcuts::Id::InsertDrawing,
           QStringLiteral("Insert a new empty drawing after this one"), QString(),
           &MainWindow::insertInterval);
    button(QStringLiteral("Duplicate"), shortcuts::Id::DuplicateDrawing,
           QStringLiteral("Copy this drawing into a new one"),
           QStringLiteral("A real copy, not a hold: the cels are independent."),
           &MainWindow::duplicateDrawing);
    button(QStringLiteral("Delete drawing"), shortcuts::Id::DeleteDrawing,
           QStringLiteral("Delete this drawing and every frame it is held on"),
           QStringLiteral("To shorten a hold instead, use Hold -."),
           &MainWindow::deleteDrawing);

    row->addSpacing(12);
    button(QStringLiteral("Hold +"), shortcuts::Id::HoldLonger,
           QStringLiteral("Hold this drawing one frame longer"),
           QStringLiteral("Repeats the same drawing; costs nothing."),
           &MainWindow::extendExposure);
    button(QStringLiteral("Hold -"), shortcuts::Id::HoldShorter,
           QStringLiteral("Hold it one frame less"), QString(), &MainWindow::shortenExposure);

    row->addSpacing(16);
    row->addWidget(new QLabel(QStringLiteral("Onion"), controls));
    // One number either side. Asymmetric onion skin is a real thing but a rare
    // one, and making it the default meant two controls to set every time.
    onion_ = new QSpinBox(controls);
    // Named so a test or a screenshot can set it the way a hand does. Nothing
    // shows an object name; what it buys is that a situation which turns the
    // onion skin on photographs a readout that agrees with it, rather than
    // reaching past the control and leaving it reading zero.
    onion_->setObjectName(QStringLiteral("onionCount"));
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
    // Narrower than the one above, and deliberately: only a drag or a click in
    // the ruler makes a noise. See TimelineWidget::scrubbed.
    connect(timeline_widget_, &TimelineWidget::scrubbed, this, &MainWindow::scrubAudio);
    connect(timeline_widget_, &TimelineWidget::documentChanged, this,
            &MainWindow::refreshEverything);
    // Clicking a row is the other way the current track changes, and it has to
    // reach the canvas and the layer panel exactly as the menu does.
    connect(timeline_widget_, &TimelineWidget::trackChanged, this,
            &MainWindow::setCurrentTrack);
    // The Track menu acts on the row you are pointed at, so it has to be told
    // when that row stops being a track. See TimelineWidget::highlightChanged.
    connect(timeline_widget_, &TimelineWidget::highlightChanged, this,
            &MainWindow::syncTrackMenu);

    timeline_scroll_ = new ReservingScrollArea(panel);
    timeline_scroll_->setWidget(timeline_widget_);
    timeline_scroll_->setWidgetResizable(true);
    timeline_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // The ruler stays put while the rows go past it. Soundtracks are under
    // every drawing row, so a scene with a few tracks is one you scroll down to
    // reach the sound -- and the ruler is where scrubbing happens, which is the
    // only way to hear it. See TimelineWidget::setRulerTop.
    connect(timeline_scroll_->verticalScrollBar(), &QScrollBar::valueChanged, timeline_widget_,
            &TimelineWidget::setRulerTop);
    // And the names stay put while the frames go past them, which is the same
    // sentence on the other axis and the one a timeline needs more often: a
    // shot is long sideways where it is short downwards, so scrolling right is
    // ordinary -- and it used to leave rows of identical cells with nothing
    // saying which track was which. See TimelineWidget::setGutterLeft.
    connect(timeline_scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, timeline_widget_,
            &TimelineWidget::setGutterLeft);
    timeline_scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // A floor low enough to drag the panel down to a single row, and no ceiling
    // at all: how tall the timeline should be is the animator's business, and
    // the track count only chooses where it starts. See syncTimelineHeight.
    timeline_scroll_->setMinimumHeight(kRulerAndRows / 2);
    layout->addWidget(timeline_scroll_);

    dock->setWidget(panel);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    syncTimelineHeight();
}

// --- files ---------------------------------------------------------------

void MainWindow::updateTitle() {
    const QString name = project_folder_.isEmpty()
                             ? QStringLiteral("Untitled")
                             : QFileInfo(project_folder_).fileName();
    const bool changed = doc_.historyStamp() != saved_history_stamp_;
    setWindowTitle(QStringLiteral("%1%2 - Animage").arg(name, changed ? QStringLiteral("*")
                                                                     : QString()));
}

bool MainWindow::leaveCurrentDocument() {
    stopPlayback();
    // A float is document state that is not in the document, so leaving without
    // committing loses it -- and leaving is not a way to discard, which is the
    // rule the rest of this function is built on.
    canvas_->settleTransform();
    if (doc_.historyStamp() == saved_history_stamp_) return true;  // nothing to lose

    if (!project_folder_.isEmpty()) {
        QString error;
        if (ProjectIO::save(doc_, project_folder_, save_state_, &error, imports_)) {
            saved_history_stamp_ = doc_.historyStamp();
            updateTitle();
            return true;
        }
        return QMessageBox::warning(
                   this, QStringLiteral("Cannot save"),
                   QStringLiteral("%1\n\nGoing on now loses the changes since the last save.")
                       .arg(error),
                   QMessageBox::Discard | QMessageBox::Cancel,
                   QMessageBox::Cancel) == QMessageBox::Discard;
    }

    // Never saved anywhere, so autosave has had nowhere to write it and this is
    // the one moment the work can be lost without anybody being told. It is the
    // reason New waited for autosave rather than arriving with it.
    const auto choice = QMessageBox::question(
        this, QStringLiteral("Save this project?"),
        QStringLiteral("This project has never been saved, so nothing has been written to disk."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Discard) return true;

    // Save As can itself be cancelled, or fail, and either way the answer to
    // "may I leave" is no -- otherwise choosing Save loses the work.
    saveProjectAs();
    return doc_.historyStamp() == saved_history_stamp_;
}

void MainWindow::resetToNewDocument() {
    // The same three steps the window starts with, and the same clearHistory:
    // a fresh document that arrives with setup already on the undo stack is a
    // fresh document you can undo into an invalid one.
    doc_ = Document();
    track_ = doc_.addTrack("track 1");
    doc_.addLayer(track_, "layer 1");
    doc_.insertImage(track_, 0);
    doc_.clearHistory();

    project_folder_.clear();
    save_state_ = ProjectIO::SaveState();

    // Everything downstream holds ids from the document that has just gone --
    // the same rebinding an open needs, for the same reason.
    afterProjectLoaded();
}

void MainWindow::newProject() {
    if (!leaveCurrentDocument()) return;
    resetToNewDocument();
    // "Like launching the application, plus the Scene settings dialog opening":
    // the first question about a new shot is what shape it is.
    chooseSceneSettings();
}

void MainWindow::openProject() {
    if (!leaveCurrentDocument()) return;
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Open project"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (folder.isEmpty()) return;

    QString error;
    if (!openProjectAt(folder, &error)) {
        // An empty message is the damaged-project question answered no. Nothing
        // has happened and there is nothing to report; a warning box saying so
        // would be the program arguing with an answer it asked for.
        if (error.isEmpty()) return;
        // The open document is untouched -- ProjectIO::load builds a new one and
        // only swaps it in once every cel has come back -- so this is a plain
        // refusal and nothing has been lost.
        QMessageBox::warning(this, QStringLiteral("Cannot open that project"), error);
    }
}

// The question a damaged project opens with. Built rather than reached for as a
// QMessageBox because what the animator needs is a *list* -- which drawings are
// gone -- and a list has to scroll when a badly synced folder lost a hundred of
// them, and has to be selectable so it can be pasted into a message to whoever
// else is on the show.
//
// The count comes first and in bold, because "how much did I lose" is the
// question asked before any other and the list is only the answer to the second
// one.
bool MainWindow::askAboutDamage(const QString& folder, const QString& rescued,
                                const Document& loaded, const ProjectIO::Damage& damage) {
    int drawings = 0;
    const QStringList lines = describeLostDrawings(loaded.scene(), damage, &drawings);

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("damaged-project"));
    dialog.setWindowTitle(QStringLiteral("This project is damaged"));
    dialog.setMinimumWidth(520);

    auto* layout = new QVBoxLayout(&dialog);

    auto* headline = new QLabel(drawings == 1
                                    ? QStringLiteral("1 drawing could not be read.")
                                    : QStringLiteral("%1 drawings could not be read.").arg(drawings),
                                &dialog);
    QFont emphasis = headline->font();
    emphasis.setBold(true);
    headline->setFont(emphasis);
    layout->addWidget(headline);

    auto* list = new QPlainTextEdit(&dialog);
    list->setObjectName(QStringLiteral("lost-drawings"));
    list->setReadOnly(true);
    list->setPlainText(lines.join(QStringLiteral("\n")));
    layout->addWidget(list, 1);

    // Said plainly, because the thing people fear at this point is that opening
    // it will make things worse.
    auto* what_happens = new QLabel(
        QStringLiteral("Animage will open a rescued copy at:\n%1\n\nThe damaged project at %2 "
                       "will not be changed, so nothing will be lost that is not lost already. "
                       "The other drawings come back as they were; the ones above come back "
                       "empty.")
            .arg(rescued, folder),
        &dialog);
    what_happens->setWordWrap(true);
    what_happens->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(what_happens);

    auto* buttons = new QDialogButtonBox(&dialog);
    // Named for what it does rather than "OK". This writes a folder to
    // somebody's disk, which is not what OK usually means.
    buttons->addButton(QStringLiteral("Open rescued copy"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    return dialog.exec() == QDialog::Accepted;
}

bool MainWindow::openProjectAt(const QString& folder, QString* error) {
    // Read into a document of its own rather than over the open one, so that
    // everything below -- the question, the rescued copy, and either of those
    // failing -- happens with the open project still open and still intact.
    // `ProjectIO::load` builds in isolation anyway; this only moves the moment
    // of adoption past the decision.
    Document loaded;
    ProjectIO::SaveState state;
    ProjectIO::Damage damage;
    if (!ProjectIO::load(loaded, folder, state, error, &damage)) return false;

    QString home = folder;
    if (damage.any()) {
        home = rescuedFolderFor(folder);
        if (!askAboutDamage(folder, home, loaded, damage)) {
            // Cancelled rather than failed: nothing was written and nothing was
            // replaced. An empty message is how the caller tells the two apart.
            if (error) error->clear();
            return false;
        }

        // Written now rather than left to the next autosave tick. A document
        // straight off disk has nothing to autosave -- its history stamp is
        // already the saved one -- so a rescued copy that existed only in
        // memory would sit unwritten until the first stroke, which is the
        // opposite of what the dialog has just promised.
        //
        // And written from a *fresh* state. The state `load` filled in
        // describes the damaged folder, and in it a cel that could not be read
        // is recorded at the revision of the empty cel standing in for it -- so
        // it claims a file matches pixels that file has never held. Kept, it
        // would carry the damaged file forward into the rescued copy as though
        // it were current, which is the damage being propagated rather than
        // noticed.
        ProjectIO::SaveState fresh;
        if (!ProjectIO::save(loaded, home, fresh, error)) return false;
        state = std::move(fresh);
    }

    doc_ = std::move(loaded);
    save_state_ = std::move(state);
    project_folder_ = home;
    // Anything the previous project's imports were reachable under belonged to
    // that project. Kept, a name that both projects happen to use would resolve
    // to the wrong file on the next save.
    imports_ = ProjectIO::Imports();
    // A different document, so what it could not read has not been said yet.
    reported_missing_imports_ = false;
    unreadable_imports_.clear();
    afterProjectLoaded();
    return true;
}

void MainWindow::afterProjectLoaded() {
    // **First, and before anything can wait.** Everything that replaces `doc_`
    // comes through here, so this is the one place that can say "the document
    // under you is not the one you were asked about" -- and two things are
    // holding questions about the old one: the decodes in flight below, and any
    // refreshAudioSamples suspended in its own nested event loop, which reads
    // this number back when it wakes.
    ++document_epoch_;

    // The imported pictures being decoded for the project that has just gone.
    // A ReferenceCache generation cannot tell one document from another -- see
    // CanvasWidget::forgetImports -- so an answer about a drawing in the old
    // project would be installed against whatever holds that id in this one.
    canvas_->forgetImports();

    // Then the scene. A soundtrack's samples are derived and went with the
    // document that was replaced, so this is one of the two moments they are
    // missing for a project that has them -- an import decodes as it arrives,
    // and the other is an undo of a deletion, which refreshEverything covers.
    refreshAudioSamples();

    const Scene& scene = doc_.scene();
    track_ = scene.tracks.empty() ? kNoId : scene.tracks.front().id;

    canvas_->setTrack(track_);
    timeline_widget_->setTrack(track_);
    timeline_widget_->setCurrentSlot(0);
    canvas_->setFrame(0);

    const Track* track = doc_.scene().findTrack(track_);
    if (track && !track->layers.empty()) canvas_->setActiveLayer(track->layers.front().id);

    rebuildLayerList();
    syncTrackMenu();
    // A load changes the track count as surely as Add track does, and nothing
    // else here would tell the dock. Issue #74: without this, a three-track
    // project opened into a one-track window kept the height for one row and
    // hid the other two behind a scrollbar, until the next refresh -- an undo,
    // which is what the report found -- put them back.
    syncTimelineHeight();
    timeline_widget_->refresh();
    canvas_->refreshAll();
    canvas_->fitToCanvas();

    saved_history_stamp_ = doc_.historyStamp();

    syncStatus();
    updateTitle();
}

// What the export folder is called if nobody says otherwise: the project's own
// name, without the suffix that makes it a project folder. An untitled document
// has no name to borrow, so it gets a word rather than an empty box.
QString MainWindow::defaultExportName() const {
    if (project_folder_.isEmpty()) return QStringLiteral("untitled");
    QString name = QFileInfo(project_folder_).fileName();
    if (name.endsWith(ProjectIO::folderSuffix())) {
        name.chop(ProjectIO::folderSuffix().size());
    }
    return name.isEmpty() ? QStringLiteral("untitled") : name;
}

// Makes `folder` somewhere an export can be written, or says no.
//
// An export replaces what was there rather than merging into it, because a
// merge is silent and produces something that looks right: re-export a shot you
// have cut short and the old export's later frames sit after the new ones,
// reading downstream as a well-formed sequence of the wrong length. So the
// answer to "there is already an export here" is to delete it, once.
//
// And the answer to "there is something here that is not an export" is no. A
// recursive delete is not a thing to weigh up against a folder we do not
// recognise -- the project folder itself is the obvious way to point this at
// hundreds of drawings -- so it is refused and another name is asked for.
bool MainWindow::clearTheWayFor(const QString& folder, const QString& called) {
    switch (exporting::occupantOf(folder)) {
        case exporting::Occupant::Nothing:
            return true;

        case exporting::Occupant::SomethingElse:
            QMessageBox::warning(
                this, QStringLiteral("Cannot export"),
                QStringLiteral("There is already something in \"%1\" that is not an export, so "
                               "it will not be written over.\n\nChoose another name.")
                    .arg(called));
            return false;

        case exporting::Occupant::AnExport:
            break;
    }

    QMessageBox ask(QMessageBox::Question, QStringLiteral("Overwrite the export?"),
                    QStringLiteral("\"%1\" already has an export in it. Overwrite it?\n\n"
                                   "Everything currently in the folder will be deleted.")
                        .arg(called),
                    QMessageBox::Cancel, this);
    QPushButton* overwrite =
        ask.addButton(QStringLiteral("Overwrite"), QMessageBox::DestructiveRole);
    // Cancel is the default, because the other button deletes a folder.
    ask.setDefaultButton(QMessageBox::Cancel);
    ask.exec();
    if (ask.clickedButton() != overwrite) return false;

    QString error;
    if (!exporting::removeExport(folder, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot export"), error);
        return false;
    }
    return true;
}

void MainWindow::exportSequences() {
    stopPlayback();

    // What first, then where: the folder dialog is the commitment, and being
    // asked to commit before knowing what you are committing to is backwards.
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Export sequences"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("Over the canvas rectangle.\n"
                                                "Hidden layers are not written."),
                                 &dialog));

    // The two formats answer different questions and the label says which,
    // because the difference is invisible until somebody compares two files and
    // concludes one of them is broken.
    auto* format = new QComboBox(&dialog);
    format->addItem(QStringLiteral("16-bit PNG"), static_cast<int>(exporting::Format::Png));
    format->addItem(QStringLiteral("OpenEXR (half, linear)"),
                    static_cast<int>(exporting::Format::Exr));
    auto* format_note = new QLabel(&dialog);
    format_note->setWordWrap(true);
    const auto describe = [format_note](int index) {
        format_note->setText(index == 1
                                 ? QStringLiteral("Lossless: the pixels as they were composited, "
                                                  "linear light with premultiplied alpha. Not the "
                                                  "same numbers as the PNG.")
                                 : QStringLiteral("sRGB, unpremultiplied. Converts on purpose, and "
                                                  "loses some of what a drawing holds."));
    };
    connect(format, &QComboBox::currentIndexChanged, &dialog, describe);
    describe(0);

    auto* per_layer = new QCheckBox(QStringLiteral("One sequence per layer"), &dialog);
    per_layer->setChecked(true);
    auto* flattened = new QCheckBox(QStringLiteral("The flattened picture"), &dialog);
    layout->addWidget(per_layer);
    layout->addWidget(flattened);

    // The sequences go in a folder of their own rather than loose in whatever
    // was chosen. An export is a dozen folders and hundreds of files, and
    // dropping that into a directory that already had something in it is how
    // two exports become one unreadable pile.
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(defaultExportName(), &dialog);
    name->selectAll();
    form->addRow(QStringLiteral("Format"), format);
    form->addRow(QString(), format_note);
    form->addRow(QStringLiteral("Folder name"), name);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // Nothing selected is not a thing to export, and nor is a folder with no
    // name. Better said by greying the button than by an error after the folder
    // has been chosen.
    const auto sync = [&] {
        buttons->button(QDialogButtonBox::Ok)
            ->setEnabled((per_layer->isChecked() || flattened->isChecked()) &&
                         !name->text().trimmed().isEmpty());
    };
    connect(per_layer, &QCheckBox::toggled, &dialog, sync);
    connect(flattened, &QCheckBox::toggled, &dialog, sync);
    connect(name, &QLineEdit::textChanged, &dialog, sync);
    sync();

    if (dialog.exec() != QDialog::Accepted) return;

    const QString parent = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Export into"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (parent.isEmpty()) return;

    const QString called = name->text().trimmed();
    const QString folder = QDir(parent).filePath(called);

    // Before the folder is emptied, and that is the whole point of asking here
    // rather than leaving it to `write`. An export replaces what was in the
    // folder, so clearing comes first -- and a refusal after clearing would have
    // thrown away the previous export to produce nothing.
    QString clash;
    if (per_layer->isChecked() && exporting::namesCollide(doc_, &clash)) {
        QMessageBox::warning(this, QStringLiteral("Cannot export"), clash);
        return;
    }
    if (!clearTheWayFor(folder, called)) return;

    const auto chosen = static_cast<exporting::Format>(format->currentData().toInt());
    QString error;
    if (!exportSequencesTo(folder, per_layer->isChecked(), flattened->isChecked(), chosen,
                           &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot export"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported to %1").arg(folder), 4000);
}

bool MainWindow::exportSequencesTo(const QString& folder, bool layers, bool flattened,
                                   exporting::Format format, QString* error) {
    exporting::Options options;
    options.folder = folder;
    options.format = format;
    options.layers = layers;
    options.flattened = flattened;

    const int total = exporting::fileCount(doc_, options);
    QProgressDialog progress(QStringLiteral("Exporting %1 frames...").arg(total),
                             QStringLiteral("Cancel"), 0, std::max(total, 1), this);
    // Modal to this window, so the pen cannot reach the document while it is
    // being read frame by frame -- the event loop below is what keeps the
    // progress bar alive, and it lets everything else in too.
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);

    // A solver of its own for the duration, and not the canvas's.
    //
    // Results are collected rather than delivered, so a solver has exactly one
    // owner: everything CtgSolver hands back goes to whoever calls collect(),
    // and the canvas drops what it did not ask for. Sharing one would mean a
    // repaint during the export -- which the progress dialog causes -- quietly
    // swallowing the answer the export was waiting for. One worker, so one
    // max-flow at a time, which is also the memory bound.
    CtgSolver solver;

    // Hands one solve to it and waits without freezing the window: the event
    // loop goes on running, so the dialog paints and Cancel is answered. This
    // is the whole of "export does not solve on the interface thread" -- the
    // thread waits, but it waits somewhere it can still draw.
    const auto solve = [&](const CtgKey& key, const CtgJob& job, CtgFill& out) {
        solver.request(key, job, /*want_labels=*/true);

        QEventLoop loop;
        QTimer poll;
        poll.setInterval(16);
        connect(&poll, &QTimer::timeout, &loop, [&] {
            if (solver.idle() || progress.wasCanceled()) loop.quit();
        });
        poll.start();
        loop.exec();

        if (progress.wasCanceled()) {
            solver.cancelAll();
            return false;
        }
        for (CtgSolver::Result& result : solver.collect()) {
            if (result.key == key) out = std::move(result.fill);
        }
        // An abandoned solve produces nothing, and nothing is not an answer.
        // Saying so stops the export rather than writing a blank sequence.
        return out.valid;
    };

    const bool ok = exporting::write(
        doc_, options,
        [&progress](int done, int count, const QString& what) {
            progress.setMaximum(std::max(count, 1));
            progress.setValue(done);
            if (!what.isEmpty()) progress.setLabelText(what);
            QCoreApplication::processEvents();
            return !progress.wasCanceled();
        },
        solve, error);
    progress.close();
    return ok;
}

bool MainWindow::saveTo(const QString& folder) {
    QString error;
    if (!ProjectIO::save(doc_, folder, save_state_, &error, imports_)) {
        QMessageBox::warning(this, QStringLiteral("Cannot save"), error);
        return false;
    }
    project_folder_ = folder;
    saved_history_stamp_ = doc_.historyStamp();
    updateTitle();
    statusBar()->showMessage(QStringLiteral("Saved to %1").arg(folder), 4000);
    return true;
}

void MainWindow::onAutosaveTick() {
    // An untitled document has nowhere to go. It is not an error and there is
    // nothing to report: New and Save As are what give it somewhere.
    if (project_folder_.isEmpty()) return;

    // Nothing has moved since the last write. Undoing back to where the last
    // save stood counts as unchanged, because it is.
    if (doc_.historyStamp() == saved_history_stamp_) return;

    // Never in the middle of a stroke or a playback pass: a save is a tenth of
    // a second, which is invisible between two strokes and a stutter inside one.
    if ((canvas_ && canvas_->isStroking()) || playback_timer_->isActive()) {
        autosave_timer_->start(kAutosaveRetryMs);
        return;
    }

    QString error;
    if (!ProjectIO::save(doc_, project_folder_, save_state_, &error, imports_)) {
        // Deliberately not a dialog. A failing autosave would otherwise
        // interrupt drawing every two minutes, which is worse than the failure
        // it is reporting -- and Save still says so properly when asked.
        statusBar()->showMessage(QStringLiteral("Autosave failed: %1").arg(error), 8000);
    } else {
        saved_history_stamp_ = doc_.historyStamp();
        updateTitle();
        statusBar()->showMessage(QStringLiteral("Autosaved"), 2000);
    }
    autosave_timer_->start(kAutosaveIntervalMs);  // back to the ordinary cadence
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Autosave decided that the disk is always current, which means there is
    // nothing for an unsaved-changes warning to warn about -- and it also means
    // the last two minutes must not fall off the end. So closing writes rather
    // than asks, on exactly the same terms as New and Open.
    if (!leaveCurrentDocument()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
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
        QStringLiteral("Animage project (*%1)").arg(ProjectIO::folderSuffix()));
    if (chosen.isEmpty()) return;
    if (!chosen.endsWith(ProjectIO::folderSuffix())) chosen += ProjectIO::folderSuffix();

    // A project is a folder, and the file dialog has just offered to replace it
    // as though it were a file. It has not created anything; ProjectIO::save
    // builds alongside and swaps, so an existing project is only replaced once
    // the new one is complete.
    saveTo(chosen);
}

void MainWindow::buildStatusBar() {
    status_ = new QLabel(this);
    statusBar()->addWidget(status_);

    // Before the rate, so that the rate stays the right-most thing and keeps
    // the position its own comment promises: the permanent area is right
    // aligned as a group, so this appearing widens the group leftwards.
    //
    // Red, and the same red the rate uses when it is dropping and the shortcuts
    // dialog uses for a colliding key -- there is one colour in this program
    // for "what you are looking at is not right" and a second would only invite
    // a third. It earns that here: the canvas is blank or part-blank while this
    // is up, and a shot you cannot see is exactly that state.
    //
    // Set once rather than on a transition, because unlike the rate this label
    // is never anything but red -- it is either saying something wrong is on
    // screen, or it is hidden.
    pictures_loading_ = new QLabel(this);
    QPalette loading = pictures_loading_->palette();
    loading.setColor(QPalette::WindowText, QColor(190, 40, 40));
    pictures_loading_->setPalette(loading);
    pictures_loading_->hide();
    statusBar()->addPermanentWidget(pictures_loading_);

    // Permanent, so it sits at the right-hand end and the main text growing or
    // shrinking cannot move it. Hidden until something is playing: it has
    // nothing to say otherwise, and appearing at the start of a take and going
    // again at the end are both moments nobody is judging motion.
    playback_rate_ = new QLabel(this);
    playback_rate_->hide();
    statusBar()->addPermanentWidget(playback_rate_);
}

// What playback is actually managing, said out loud.
//
// Playback works its slot out from a clock, so a frame that takes longer than
// its share does not slow the take down -- the frames underneath it are never
// asked for. Nothing on screen said so, which makes a shot that is dropping
// indistinguishable from a shot that is badly timed, and judging timing is the
// whole purpose of the mode. bench_playback answers this for the developer on a
// synthetic shot; this answers it for whoever is watching, on theirs.
//
// Three things about how it measures, each of which was a choice:
//
// - **From paints, not from ticks.** onPlaybackTick skips slots the clock has
//   already passed, so counting slot changes very nearly counts frames shown --
//   and stops doing so exactly when two of them collapse into one paint, which
//   is what happens once playback overruns. CanvasWidget::paintCount cannot be
//   wrong about it.
// - **A rolling window and not a total.** A count of drops since Play means
//   less the longer you watch, and a take that recovers has to read as
//   recovered. Roughly the last second.
// - **Four times a second, not twenty-four.** The label is a widget and setting
//   its text repaints it, so an instrument updated every frame would be adding
//   to the thing it is measuring.
void MainWindow::updatePlaybackRate() {
    if (!playback_rate_ || !canvas_) return;

    constexpr qint64 kSampleMs = 250;
    constexpr std::size_t kSamplesKept = 5;  // about a second of window
    constexpr std::size_t kSamplesNeeded = 3;

    const qint64 now = playback_clock_.elapsed();
    if (now - last_rate_sample_ms_ < kSampleMs) return;
    last_rate_sample_ms_ = now;

    rate_samples_.emplace_back(now, canvas_->paintCount());
    while (rate_samples_.size() > kSamplesKept) rate_samples_.pop_front();
    // Nothing is claimed off half a window. The first readings of a take are
    // the ones the caches are cold for, and a rate that opened by accusing the
    // program of dropping and then corrected itself would teach people to
    // ignore it.
    if (rate_samples_.size() < kSamplesNeeded) return;

    const qint64 span = rate_samples_.back().first - rate_samples_.front().first;
    if (span <= 0) return;
    const double rate =
        static_cast<double>(rate_samples_.back().second - rate_samples_.front().second) *
        1000.0 / static_cast<double>(span);

    const int nominal = std::max(1, doc_.scene().framerate);
    // A tenth of a frame of slack. The window is a second of wall clock and the
    // paints inside it do not divide into it exactly, so demanding the whole
    // rate would leave the warning on permanently -- which is how a warning
    // stops being read. Same lesson as the "overwrite" label on the gutter.
    const bool dropping = rate < static_cast<double>(nominal) * 0.9;

    if (dropping) {
        playback_rate_->setText(QStringLiteral("dropping frames: %1 of %2 fps")
                                    .arg(rate, 0, 'f', 0)
                                    .arg(nominal));
    } else {
        playback_rate_->setText(QStringLiteral("playing %1 fps").arg(nominal));
    }

    // Red when it is dropping, and the same red the shortcuts dialog marks a
    // colliding key with -- there is one colour in this program for "this is
    // wrong" and a second one would only invite a third.
    //
    // Set on the transition and not on every reading. A palette change
    // re-polishes the widget, and this runs four times a second for as long as
    // somebody watches a take.
    if (dropping != rate_dropping_) {
        rate_dropping_ = dropping;
        QPalette palette = playback_rate_->palette();
        palette.setColor(QPalette::WindowText,
                         dropping ? QColor(190, 40, 40)
                                  : QApplication::palette().color(QPalette::WindowText));
        playback_rate_->setPalette(palette);
    }
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

    // A solve happens on another thread now, so the colour on screen can be a
    // moment behind the drawing and there would otherwise be nothing to say so.
    // It is the whole of the visible difference: the program does not stop, and
    // what you are looking at is the last answer until the next one lands.
    // `colouring...` said a solve was happening and never how much was left,
    // which was enough while only the drawing in front of you was ever solved.
    // With fills accumulating across a take -- issue #85 -- there is a
    // quantity, and it is the one somebody watching a take wants: how much of
    // the shot has colour on it yet.
    //
    // Same rule as the imported pictures one line down, and the same reason for
    // it: shown while a solve is outstanding rather than while fills are merely
    // missing, because a drawing is only solved once it has been looked at and
    // a number standing still reads as stuck.
    QString colouring;
    if (canvas_->colourPending()) {
        const ImportsReady coloured = drawingsColoured();
        colouring = coloured.wanted > 0
                        ? QStringLiteral("   colouring %1/%2")
                              .arg(coloured.ready)
                              .arg(coloured.wanted)
                        : QStringLiteral("   colouring...");
    }

    // How much of an imported sequence is on hand, and only while some of it is
    // not. A number rather than a word, because the complaint this answers is
    // that a shot playing before its pictures are decoded looks exactly like a
    // shot that is broken -- the playhead advances over a blank canvas and
    // nothing anywhere says why. "loading" alone would not settle that either;
    // what tells somebody it is working is that the number moves.
    //
    // **Shown while a decode is outstanding, and not while frames are merely
    // missing**, which is the same rule `colouring...` follows one line up and
    // is not the rule this had first.
    //
    // A frame is only ever asked for when it is on screen, so a sequence you
    // have scrubbed part of sits at 40 of 151 with nothing whatever happening.
    // A number that does not move is worse than no number: it is exactly what
    // "stuck" looks like, and this exists to tell working from stuck.
    //
    // The denominator is still the whole sequence rather than what is
    // outstanding, because during a take every frame is visited and the climb
    // from 1 to 151 is the progress somebody is actually waiting on. "How many
    // are being worked out right now" would read 1 for the whole of it.
    //
    // Its own widget at the right-hand end rather than a phrase in the middle
    // of this line, and worded rather than abbreviated. `pictures 43/151` was
    // both: it sat between the zoom and the tile count where nobody watching
    // the canvas would find it, and it named a number without saying what the
    // number was about. The form is the rate's -- "N of M" after a colon --
    // because the two sit next to each other and read as one instrument.
    if (pictures_loading_) {
        const MainWindow::ImportsReady pictures =
            canvas_->referenceFramesPending() ? importsReady() : ImportsReady{};
        if (pictures.wanted > 0) {
            pictures_loading_->setText(QStringLiteral("loading imported pictures: %1 of %2")
                                           .arg(pictures.ready)
                                           .arg(pictures.wanted));
            pictures_loading_->show();
        } else {
            pictures_loading_->hide();
        }
    }

    // Past this track's last drawing there is no slot and no cel, so there is
    // nothing to draw on -- while the canvas may still be showing something,
    // because a track that holds or cycles goes on contributing to the picture.
    // Said out loud, because a brush that does nothing is otherwise a bug.
    QString past = (image == kNoId && slot >= track->frameCount())
                       ? QStringLiteral("   you cannot draw past the end of a track")
                       : QString();

    // The other two ways the brush will not draw where you are standing, and
    // for the same reason as the line above: a brush that does nothing is
    // otherwise a bug. Locking is not reachable from the interface yet, so the
    // hidden one is the one that bites -- hiding a layer is one click, and
    // going on drawing on it is easy.
    //
    // Only when the line above is not already saying so. Two phrases for one
    // fault is one too many, which is the rule `sayCannot` follows as well.
    if (past.isEmpty()) {
        switch (canvas_->whyTheBrushWillNotDraw()) {
            case CanvasWidget::Refusal::LockedLayer:
                past = QStringLiteral("   this layer is locked");
                break;
            case CanvasWidget::Refusal::HiddenLayer:
                past = QStringLiteral("   this layer is hidden");
                break;
            case CanvasWidget::Refusal::NoLayer:
                past = QStringLiteral("   no layer is selected");
                break;
            case CanvasWidget::Refusal::ReferenceLayer:
                // Named as what it is rather than as "locked", which is what
                // this would have said before the kind was asked ahead of the
                // lock: unlocking would not help, and sending somebody to try
                // is worse than saying nothing.
                past = QStringLiteral("   this is an imported picture");
                break;
            default: break;
        }
    }

    // Standing past the end of the shot is a different thing again: there may be
    // a drawing here and you may edit it, but it will not play and will not be
    // exported until the boundary is dragged past it. Silence about that is how
    // work goes missing from an export.
    const QString outside = (slot >= doc_.scene().shotFrames())
                                ? QStringLiteral("   outside the shot")
                                : QString();

    // A selection has no panel and no mode, so the status bar is the only place
    // it says it is there at all -- and it does clip what Transform and Erase
    // selection act on, even though it deliberately does not clip the brush.
    const QString selected =
        canvas_->hasSelection() ? QStringLiteral("   selection") : QString();

    // The history is capped in bytes, so the steps alone no longer say what it
    // costs -- one of them can be worth forty of another. This is the number
    // the cap is applied to, and the only way to see it about to bite.
    const QString history =
        QStringLiteral("undo %1 (%2 MB)")
            .arg(doc_.undoDepth())
            .arg(static_cast<double>(doc_.historyBytes()) / (1024.0 * 1024.0), 0, 'f', 0);

    // What this track does out past its own end, said whether or not the
    // playhead is anywhere near it: tracks are not the same length, so which of
    // the three a track is doing decides what the shot looks like at frames the
    // track has no drawings for, and there is nowhere else it is written down.
    //
    // The whole phrase and not the bare word. "hold" alone, a few characters
    // from "held 5", would be two unrelated meanings of one root on one line --
    // and the word only means anything at all next to what it is about.
    //
    // "overwrite" keeps its old rule of appearing only when it is true, rather
    // than growing an "insert" to say the other thing: it is on by default and
    // is not expected to be changed, so a word for the ordinary case would be a
    // word on every row of every scene saying nothing.
    const QString settings =
        (track->overwrite_drawings ? QStringLiteral("overwrite, ") : QString()) +
        QStringLiteral("%1 past the last drawing").arg(endWord(track->end));

    // The frame count is the scene's and the rest is the current track's, which
    // is the distinction the whole panel now rests on: one timeline, several
    // tracks along it. Saying "frame 3 / 12" from a track of 12 while the shot
    // ran to 40 would be the timeline lying about its own length.
    status_->setText(
        QStringLiteral("frame %1 / %2   %3 (%4)   held %5   drawings %6   layers %7   zoom %8%   "
                       "tiles %9   %10   %11 fps%12%13%14%15")
            .arg(slot + 1)
            .arg(doc_.scene().shotFrames())
            .arg(QString::fromStdString(track->name))
            .arg(settings)
            .arg(track->exposureOf(image))
            .arg(track->images.size())
            .arg(track->layers.size())
            .arg(canvas_->zoom() * 100.0, 0, 'f', 0)
            .arg(doc_.totalTileCount())
            .arg(history)
            .arg(doc_.scene().framerate)
            .arg(colouring)
            .arg(past)
            .arg(outside)
            .arg(selected));
}

// The timeline dock follows the number of tracks, up to a point.
//
// A fixed height was right while there was exactly one row and hid the second
// the moment there were two -- with a scrollbar as the only sign, which reads
// as a panel that is merely scrolled rather than one that is too small. Past
// four rows it stops growing, because the dock is taking height from the canvas
// and the canvas is what the shot is drawn on.
//
// Asked for rather than constrained, and that distinction is the whole of why
// this was wrong twice. Pinning the minimum and the maximum to the wanted height
// does make the dock the right size, and it also welds it there: the dock cannot
// then be dragged smaller than whatever the track count last asked for, and Qt
// will not shrink a dock just because the widget inside it wants less. So the
// scroll area keeps a small floor and no ceiling -- drag it wherever you like,
// either way -- and the height is *requested* through resizeDocks when the
// number of rows changes.
void MainWindow::syncTimelineHeight() {
    if (!timeline_scroll_ || !timeline_widget_ || !timeline_dock_) return;

    // **Soundtracks are rows too**, and this is the function that has to know
    // it. It moves the dock by the rows that came or went rather than sizing
    // it, so a route that adds a row without telling it leaves the strip at the
    // height for the count before -- which is issue #74, where a load that added
    // two tracks left the strip at the height for one and the symptom was that
    // Ctrl+Z put it back, undo being the next thing that called this at all.
    //
    // An audio import adds a row that is not a track, which is a case this
    // function had never seen.
    const std::size_t row_count =
        doc_.scene().tracks.size() + doc_.scene().audio_tracks.size();
    const int rows =
        std::min(static_cast<int>(std::max<std::size_t>(row_count, 1)), kMaxDockRows);
    const int was = timeline_rows_shown_;
    if (rows == was) return;  // nothing about the height has changed

    timeline_rows_shown_ = rows;

    // Moved by a *difference*, and the difference is exactly the rows that came
    // or went: a row is kRowHeight and everything wrapped round the strip --
    // the scroll area's frame, the horizontal scrollbar, the buttons above it,
    // the dock's own title bar -- costs the same whatever the row count is. So
    // "be one row taller than you are" needs to measure none of it.
    //
    // Two versions of this measured instead and both were wrong. Adding the
    // wrapping up forgot a different piece each time and put the bottom row
    // under it. Reading the strip's own height is worse and looks better: with
    // a resizable scroll area the widget is stretched to the viewport, so its
    // height is the space available and never the space wanted, and comparing
    // the two says "no growth needed" every time.
    //
    // The first call has nothing to take a difference from, and needs none: the
    // panel's own sizeHint is the right height for one row, which is what the
    // dock starts at anyway.
    if (was == 0) return;
    resizeDocks({timeline_dock_}, {timeline_dock_->height() + (rows - was) * kRowHeight},
                Qt::Vertical);
}

void MainWindow::refreshEverything() {
    // Nothing about imported pictures happens here any more, and the absence is
    // worth a line because a call used to be first in this function.
    //
    // Re-deriving ran on the interface thread and could afford to, because
    // every drawing of a still's track showed the same picture and the only
    // thing that changed it was a placement -- which arrives here. A sequence
    // shows a different frame at every drawing and the playhead never comes
    // through this function at all, so the ask moved to the paint, which is the
    // one thing that reliably happens when what is on screen changes. See
    // CanvasWidget::requestReferenceFrames.
    //
    // What that costs is that a picture arrives a frame or two after the
    // placement does, instead of within this call. That is the same bargain the
    // colour already makes and for the same reason.
    syncTimelineHeight();
    timeline_widget_->refresh();
    canvas_->setFrame(timeline_widget_->currentSlot());
    rebuildLayerList();
    syncStatus();
    // A soundtrack can arrive here by an import and leave by an undo, and the
    // output should match what the document holds either way. Free when it
    // already does.
    //
    // **The samples and not only the device**, because an undo is the other way
    // a soundtrack arrives here with nothing decoded: removing one throws its
    // samples away deliberately, on the understanding that bringing it back
    // decodes the file again. This is where that happens. It ends in
    // refreshAudioDevice itself, so the two are one call.
    refreshAudioSamples();
}

// --- importing -----------------------------------------------------------

namespace {

// What an imported file is called inside the project, given what it is called
// outside it.
//
// Two things happen here. The name is reduced to something a folder on any of
// the three platforms will take, and it is made unique against the imports the
// scene already names -- because two modelsheets from two folders are very
// often both called `model.png`, and the second one silently becoming the first
// is a picture quietly replaced.
// `fallback_suffix` is what a file with no extension at all is called, and it
// is a parameter because this is shared with soundtracks now. It defaulted to
// "png" when pictures were the only caller, which would have named an
// extensionless soundtrack `dialogue.png` inside `audio/` -- harmless to the
// decoder, which reads the bytes, and confusing to every human who opened the
// folder afterwards.
std::string importNameIn(std::unordered_set<std::string>& taken, const QString& path,
                         const char* fallback_suffix = "png") {
    QString stem = QFileInfo(path).completeBaseName();
    QString suffix = QFileInfo(path).suffix().toLower();
    // Everything in the *stem* that is not a letter, a digit or a dash becomes
    // a dash, which is the rule the export already uses for track and layer
    // names -- and the underscore is not an oversight in that list. It is the
    // export's separator and nothing else is, so a name that kept one would put
    // an extra field in a file name; see export_sequence.h. One rule rather
    // than two, so that nothing has to remember which names are allowed what.
    //
    // A dot goes the same way, and only one survives: the one put back below,
    // between the stem and the suffix. `model.v2.png` lands as `model-v2.png`,
    // so a name has exactly one extension and nothing downstream has to decide
    // which of two dots ends it.
    for (QChar& c : stem) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-')) c = QLatin1Char('-');
    }
    if (stem.isEmpty()) stem = QStringLiteral("import");
    if (suffix.isEmpty()) suffix = QLatin1String(fallback_suffix);

    const QString base = stem + QLatin1Char('.') + suffix;
    if (!taken.count(base.toStdString())) {
        taken.insert(base.toStdString());
        return base.toStdString();
    }
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1-%2.%3").arg(stem).arg(n).arg(suffix);
        if (taken.count(candidate.toStdString())) continue;
        taken.insert(candidate.toStdString());
        return candidate.toStdString();
    }
}

// What the document already has spoken for. Gathered once by a caller that is
// about to name several files, because the answer for the second file has to
// take account of the first -- and the document does not know about the first
// until the whole import is over.
std::unordered_set<std::string> importNamesTaken(const Document& doc) {
    std::unordered_set<std::string> taken;
    for (const std::string& name : ProjectIO::importsReferencedBy(doc)) taken.insert(name);
    return taken;
}

// The one-file case, which is every caller that is not importing a sequence.
std::string importNameFor(const Document& doc, const QString& path) {
    std::unordered_set<std::string> taken = importNamesTaken(doc);
    return importNameIn(taken, path);
}

// What to call the track and its layer, given the file's name.
//
// Made unique against the tracks already there, which matters more than it
// looks. Importing the same picture twice is an ordinary thing to do -- one to
// keep and one to move about -- and two tracks with the same name reach the
// export as two sequences claiming one folder. The export catches that and
// refuses with a clear message, so nothing is ever silently overwritten; what
// it costs is an export that stops until you go and rename something, at the
// end of the job rather than at the start of it.
//
// The file inside `imports/` is made unique separately, in importNameFor, and
// for a different reason: two pictures from two folders are very often both
// called `model.png`, and there the collision would be a picture quietly
// replaced rather than an export refused.
std::string trackNameFor(const Document& doc, const QString& path) {
    const QString stem = QFileInfo(path).completeBaseName();
    const std::string base = stem.isEmpty() ? std::string("import") : stem.toStdString();

    std::unordered_set<std::string> taken;
    for (const Track& track : doc.scene().tracks) taken.insert(track.name);
    if (!taken.count(base)) return base;
    for (int n = 2;; ++n) {
        const std::string candidate = base + " " + std::to_string(n);
        if (!taken.count(candidate)) return candidate;
    }
}

// What a picture of this size will cost, in the units somebody can act on.
QString costOf(int width, int height) {
    const std::size_t tiles = image_import::tileCountFor(width, height);
    // A tile is 128x128 RGBA half = exactly 128 KB, so tiles/8 is megabytes.
    const double megabytes = static_cast<double>(tiles) / 8.0;
    return QStringLiteral("%1 x %2 pixels, %3 tiles, about %4 MB in memory")
        .arg(width)
        .arg(height)
        .arg(tiles)
        .arg(megabytes, 0, 'f', megabytes < 10.0 ? 1 : 0);
}

}  // namespace

// File ▸ Import ▸ Image.
//
// The picture lands as a **reference layer**: a layer that holds no cels at all
// and shows the imported file, derived and memoised rather than stored. What
// that buys and what it gives up is docs/importing.md; the two consequences
// visible from here are that the brush refuses on the layer's kind, and that a
// save writes the file rather than a drawing.
//
// Placed at 1:1 with its top-left at the origin, which is where this stops
// short of the plan: positioning it is the transform box, and on a reference
// layer that box has to store its answer instead of baking it. Until then the
// canvas is the only rectangle in the model and drawing outside it is already
// allowed, so a modelsheet larger than the frame is visible and usable, just
// not yet movable.
void MainWindow::importImage() {
    stopPlayback();

    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import image"), QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.tif *.tiff *.bmp *.webp);;All files (*)"));
    if (path.isEmpty()) return;

    // Asked before anything is decoded, so that "this is 70 MB" arrives before
    // the wait rather than after it.
    const image_import::Survey survey = image_import::survey(path);
    if (!survey.ok) {
        QMessageBox::warning(this, QStringLiteral("Import image"),
                             QStringLiteral("%1 cannot be read: %2")
                                 .arg(QFileInfo(path).fileName(), survey.trouble));
        return;
    }

    const QString recap = QStringLiteral("%1\n\n%2\n\nIt will be shown from its file rather "
                                         "than drawn, so it cannot be painted on until it is "
                                         "converted to drawings.")
                              .arg(QFileInfo(path).fileName(), costOf(survey.width, survey.height));
    if (QMessageBox::question(this, QStringLiteral("Import image"), recap,
                              QMessageBox::Ok | QMessageBox::Cancel,
                              QMessageBox::Ok) != QMessageBox::Ok) {
        return;
    }

    QString trouble;
    if (!importImageFrom(path, &trouble)) {
        QMessageBox::warning(this, QStringLiteral("Import image"),
                             QStringLiteral("%1 cannot be read: %2")
                                 .arg(QFileInfo(path).fileName(), trouble));
    }
}

bool MainWindow::importImageFrom(const QString& path, QString* trouble) {
    QString why;
    image_import::Converted converted;
    TileGrid pixels = image_import::decode(path, &why, &converted);
    if (pixels.empty() && !why.isEmpty()) {
        if (trouble) *trouble = why;
        return false;
    }

    const std::string import_name = importNameFor(doc_, path);
    const std::string shown = trackNameFor(doc_, path);

    TrackId added = kNoId;
    ImageId drawing = kNoId;
    LayerId layer_id = kNoId;
    {
        // One command, so that an import undoes in one step. The decoded pixels
        // are deliberately not in it: they are derived, no cel holds them, and
        // there is nothing for the journal to put back.
        doc_.beginCommand("Import image");
        added = doc_.addTrack(shown);
        layer_id = doc_.addLayer(added, shown, 0, LayerKind::Reference);
        drawing = doc_.insertImage(added, 0);

        if (const Track* track = doc_.scene().findTrack(added)) {
            if (const Layer* layer = track->findLayer(layer_id)) {
                Layer updated = *layer;
                updated.reference_sources = {import_name};
                doc_.updateLayer(added, layer_id, updated);
            }
        }
        // Which of the layer's files this drawing shows: the first, there being
        // one. Said rather than assumed even here, because absence is what makes
        // a reference layer empty at a drawing, and a still that did not say so
        // would be a layer pointed at a file and showing nothing.
        doc_.setSourceFrame(added, drawing, layer_id, 0);

        // Held past its last drawing, or the modelsheet is on frame 0 and
        // nowhere else -- which is not a reference, it is a flash.
        if (const Track* track = doc_.scene().findTrack(added)) {
            TrackProperties properties = track->properties();
            properties.end = TrackEnd::HoldLast;
            doc_.updateTrack(added, properties);
        }
        doc_.endCommand();
    }

    imports_.pending[import_name] = path;
    // Installed at the placement the layer has, which for a fresh import is the
    // identity -- so this is the decoded picture unchanged. Going through the
    // same call refreshReferenceFrames uses is what keeps one answer to "what
    // is this layer showing".
    doc_.setReferenceFrame(added, drawing, layer_id, Transform{}, std::move(pixels));

    setCurrentTrack(added);
    // An import adds a track, which is one of the routes that has to say so:
    // syncTimelineHeight moves the dock by the rows that came or went rather
    // than sizing it, so a route that does not call it leaves the strip at the
    // height for the track count before. refreshEverything calls it.
    refreshEverything();

    // A colour conversion that nobody is told about is the failure this whole
    // colour path exists to avoid: the picture arrives, it looks plausible, and
    // every swatch in it is a different colour from the one the artist chose.
    // Converting is right; doing it silently is not.
    if (!converted.from.isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("Imported %1 and converted its colours from %2 to sRGB")
                .arg(QFileInfo(path).fileName(), converted.from),
            8000);
    }
    return true;
}

// File ▸ Import ▸ Image sequence.
//
// A file picker, a survey and a recap in front of importSequenceFrom, which is
// where the importing happens. Same division as the still and for the same
// reason: a modal dialog is not something `shots` or a test can drive.
void MainWindow::importImageSequence() {
    stopPlayback();

    const QStringList picked = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Import image sequence"), QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.tif *.tiff *.bmp *.webp);;All files (*)"));
    if (picked.isEmpty()) return;

    SequenceImportDialog::Found found;
    found.ordering = image_import::order(picked);

    // Surveyed rather than decoded, so the recap can say what this costs before
    // paying it. A survey reads a header; on two hundred frames the decode is
    // the whole cost being described and doing it here would make the recap the
    // slow part of the import.
    //
    // The size is the first readable file's. Frames of different sizes are
    // allowed -- each is placed by the same layer placement and simply covers a
    // different rectangle -- so this is a description of the sequence rather
    // than a rule it has to meet.
    for (const QString& path : found.ordering.paths) {
        const image_import::Survey one = image_import::survey(path);
        if (!one.ok) {
            ++found.unreadable;
            continue;
        }
        if (found.width == 0) {
            found.width = one.width;
            found.height = one.height;
        }
    }

    if (found.width == 0) {
        QMessageBox::warning(this, QStringLiteral("Import image sequence"),
                             QStringLiteral("None of those %1 files is a picture this build can "
                                            "read, so there is nothing to import.")
                                 .arg(picked.size()));
        return;
    }

    SequenceImportDialog dialog(found, static_cast<int>(timeline_widget_->currentSlot()) + 1, this);
    if (dialog.exec() != QDialog::Accepted) return;

    const SequenceImportDialog::Answer answer = dialog.answer();
    QString trouble;
    if (!importSequenceFrom(found.ordering.paths, answer.start_frame, answer.half_size,
                            &trouble)) {
        QMessageBox::warning(this, QStringLiteral("Import image sequence"), trouble);
    }
}

bool MainWindow::importSequenceFrom(const std::vector<QString>& paths, int start_frame,
                                    bool half_size, QString* trouble) {
    if (paths.empty()) {
        if (trouble) *trouble = QStringLiteral("no files were given");
        return false;
    }

    // Named against the scene before anything is added, and one at a time,
    // because two frames of one sequence can arrive from two folders under the
    // same name -- importNameFor is what makes each unique, and it answers
    // against the imports the *document* already names.
    //
    // So the layer is pointed at its files as they are named, and the pending
    // list is filled at the same time. That list is what a save copies from
    // before the project has a folder of its own.
    const std::string shown = trackNameFor(doc_, paths.front());

    TrackId added = kNoId;
    LayerId layer_id = kNoId;
    std::vector<ImageId> drawings;
    {
        // One command, so an import of two hundred frames undoes in one step.
        // It costs the drawings and the slots and no pixels at all: a reference
        // layer has no cels, so there is nothing here for the journal to hold.
        doc_.beginCommand("Import image sequence");
        added = doc_.addTrack(shown);
        layer_id = doc_.addLayer(added, shown, 0, LayerKind::Reference);

        // The frames the sequence does not start on. A start frame of one adds
        // none of these; anything later leaves the track empty until then,
        // which is what "starts at frame 12" has to look like -- there is no
        // other way for a track to be silent at its beginning.
        const int before = std::max(0, start_frame - 1);
        for (int i = 0; i < before; ++i) doc_.insertImage(added, static_cast<std::size_t>(i));

        // Named against the document *and against each other*, in one pass, and
        // then written to the layer once.
        //
        // Once is not tidiness. `updateLayer` journals a copy of the whole layer
        // list, and the list this is filling in is on it -- so naming the files
        // one at a time and writing after each would put two hundred copies of a
        // growing two-hundred-name vector on the undo stack, for one command
        // that undoes in one step anyway. Quadratic in the length of a sequence,
        // where nothing else about an import is.
        std::unordered_set<std::string> taken = importNamesTaken(doc_);
        std::vector<std::string> named;
        named.reserve(paths.size());
        for (const QString& path : paths) {
            named.push_back(importNameIn(taken, path));
            imports_.pending[named.back()] = path;
        }
        if (const Track* track = doc_.scene().findTrack(added)) {
            if (const Layer* layer = track->findLayer(layer_id)) {
                Layer updated = *layer;
                updated.reference_sources = named;
                doc_.updateLayer(added, layer_id, updated);
            }
        }

        for (std::size_t i = 0; i < paths.size(); ++i) {
            const ImageId drawing = doc_.insertImage(added, static_cast<std::size_t>(before) + i);
            drawings.push_back(drawing);
            // On 1s: one drawing per file. An image sequence has no frame rate
            // of its own and inventing one is inventing information.
            doc_.setSourceFrame(added, drawing, layer_id, static_cast<int>(i));
        }

        if (half_size) {
            // A placement and not a separate kind of import, which is what
            // makes it undoable, adjustable afterwards through Place this
            // picture, and cheaper rather than dearer: the derive step applies
            // the scale, so a half-size frame caches a quarter of the tiles
            // instead of full ones being shrunk on the way to the screen.
            if (const Track* track = doc_.scene().findTrack(added)) {
                if (const Layer* layer = track->findLayer(layer_id)) {
                    Layer placed = *layer;
                    placed.placement.scale_x = 0.5;
                    placed.placement.scale_y = 0.5;
                    doc_.updateLayer(added, layer_id, placed);
                }
            }
        }

        // Nothing past the last frame, which is what an animation does and is
        // deliberately not the still's HoldLast. A modelsheet is meant to stay
        // up for the whole shot; an animatic is a stretch of timing that ends
        // where it ends. Both are the track's own setting afterwards.
        if (const Track* track = doc_.scene().findTrack(added)) {
            TrackProperties properties = track->properties();
            properties.end = TrackEnd::Nothing;
            doc_.updateTrack(added, properties);
        }
        doc_.endCommand();
    }

    setCurrentTrack(added);
    // An import adds a track, which is one of the routes that has to say so:
    // syncTimelineHeight moves the dock by the rows that came or went rather
    // than sizing it, so a route that does not call it leaves the strip at the
    // height for the track count before. refreshEverything calls it.
    refreshEverything();
    return true;
}

// File ▸ Import ▸ Audio.
//
// A file picker and a decode in front of importAudioFrom, and the decode is
// deliberately *before* the recap rather than after it. The sequence dialog
// surveys instead, because there a decode is two hundred files and the whole
// cost being described; here it is one file and tens of milliseconds, and it is
// the only thing that knows how long the sound is -- which is the number the
// recap exists to show.
void MainWindow::importAudio() {
    stopPlayback();

    if (!audio_import::available()) {
        // Present but refusing, rather than absent. An item that comes and goes
        // with the Qt somebody installed is an item nobody can find twice.
        QMessageBox::warning(
            this, QStringLiteral("Import audio"),
            QStringLiteral("This build has no audio support: Qt Multimedia was not found "
                           "when it was configured, so there is nothing here that can read a "
                           "sound file."));
        return;
    }

    const QString picked = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import audio"), QString(),
        QStringLiteral("Audio (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus);;All files (*)"));
    if (picked.isEmpty()) return;

    QString trouble;
    if (!importAudioFrom(picked, 0, &trouble)) {
        QMessageBox::warning(this, QStringLiteral("Import audio"), trouble);
        return;
    }
}

// Brings a soundtrack in. See the header for why this decodes rather than
// deferring to something later.
//
// `start_frame` of 0 means "ask" -- the dialog is raised and the answer used.
// Any other value is taken as given and nothing is asked, which is what lets a
// test and `shots` drive an import. That is the same division the still and the
// sequence have, arranged differently because here the decode has to happen
// before the question can be put.
bool MainWindow::importAudioFrom(const QString& path, int start_frame, QString* trouble,
                                 bool extend_shot) {
    const audio_import::Decoded decoded = audio_import::decode(path);
    if (!decoded.ok) {
        if (trouble) *trouble = decoded.trouble;
        return false;
    }

    if (start_frame == 0) {
        AudioImportDialog::Found found;
        found.file = QFileInfo(path).fileName();
        found.rate = decoded.clip.rate;
        found.channels = decoded.clip.channels;
        found.frames = decoded.clip.frames();
        found.scene_fps = doc_.scene().framerate;
        found.file_bytes = QFileInfo(path).size();
        found.trouble = decoded.trouble;
        found.sound_frames = decoded.clip.framesAtFps(doc_.scene().framerate);
        found.shot_frames = doc_.scene().shotFrames();
        found.length_is_fixed = doc_.scene().fixed_length;

        AudioImportDialog dialog(found, static_cast<int>(timeline_widget_->currentSlot()) + 1,
                                 this);
        if (dialog.exec() != QDialog::Accepted) return true;  // cancelled, not failed
        const AudioImportDialog::Answer answer = dialog.answer();
        start_frame = answer.start_frame;
        extend_shot = answer.extend_shot;
    }

    // Named against what the document already spoke for, exactly as a picture
    // is, and in the *audio* namespace: `imports/` and `audio/` are two
    // folders, so a sound and a picture may both be called `take-3.x` without
    // either shadowing the other.
    std::unordered_set<std::string> taken;
    for (const std::string& name : ProjectIO::audioReferencedBy(doc_)) taken.insert(name);
    const std::string source = importNameIn(taken, path, "wav");

    // Frame 1 is slot 0, and a frame before 1 is a negative offset. The
    // conversion is here rather than in the dialog for the reason the sequence
    // import has it here: a slot index is this file's unit and a frame number
    // is the user's.
    const int offset = start_frame - 1;

    TrackId added = kNoId;
    {
        // One command for the whole import, so it undoes in one step. Nested
        // commands join the outermost, which is what makes three calls one
        // entry -- without this, adding the track and placing it were already
        // two, and lengthening the shot would have been a third.
        doc_.beginCommand("Import audio");
        added = doc_.addAudioTrack(QFileInfo(path).completeBaseName().toStdString(), source);
        AudioPlacement placed;
        placed.offset_frames = offset;
        doc_.setAudioTrackPlacement(added, placed);

        if (extend_shot) {
            // **Never shorter than it is**, whatever the offset. The box says
            // "reach the end of the sound", not "be exactly the sound" -- and a
            // shot that shrank would take drawings out of the export, which is
            // not something an audio import may do.
            const long long reach =
                static_cast<long long>(offset) +
                static_cast<long long>(decoded.clip.framesAtFps(doc_.scene().framerate));
            const long long now = static_cast<long long>(doc_.scene().shotFrames());
            doc_.setSceneLength(true, static_cast<int>(std::max(reach, now)));
        }
        doc_.endCommand();
    }

    // Outside the command, because it is not an edit: installing decoded
    // samples changes no document state, only the memo of what a file decodes
    // to. Same rule as setReferenceFrame.
    doc_.setAudioSamples(added, decoded.clip);

    // What a save copies from, until the project has a folder of its own.
    imports_.pending_audio[source] = path;

    if (trouble) *trouble = decoded.trouble;
    // refreshEverything calls syncTimelineHeight, which is what an import that
    // adds a row has to reach. A soundtrack's row is not a track's, and that
    // function now counts both.
    refreshEverything();
    return true;
}

// Decodes every soundtrack the scene names and has no samples for.
//
// **On a load, and on every refresh, because there are two ways to arrive here
// with a track whose samples are gone.** A clip is derived data and goes with
// the document it belonged to, so a project opened from disk has tracks naming
// files and nothing decoded. The second way is an undo: `removeAudioTrack`
// throws the samples away rather than carrying megabytes on the undo stack, and
// says an undo re-decodes -- which was a promise nothing kept while this ran
// only after a load. What it produced was a soundtrack that came back on Ctrl+Z
// with no waveform, no length and no sound, until the project was saved and
// reopened. So refreshEverything calls this, and the test for "already decoded"
// is a hash lookup per soundtrack, which is what makes that affordable.
//
// **A file that will not read is remembered as an empty clip**, which is the
// same rule a reference frame follows and for a sharper version of the same
// reason: without it, every caller that finds nothing would try again, and one
// of those callers is going to be a device callback.
void MainWindow::refreshAudioSamples() {
    // **Re-entrant, and that has to be handled rather than forbidden.** The
    // decode below runs a nested event loop, so anything the user does inside
    // it that reaches refreshEverything comes back here -- a second Ctrl+Z, a
    // gesture ending on the timeline -- and finds the very track being decoded
    // still missing, because nothing is installed until the decode returns. It
    // would then decode it again inside itself, once per event.
    //
    // Guarded on the document rather than outright, because there is one
    // re-entry that must be let through: the one afterProjectLoaded makes for a
    // project opened from inside that loop. That call is about another
    // document, and refusing it would leave the new project's sound undecoded
    // for as long as it stayed open.
    if (decoding_audio_ && decoding_audio_for_ == document_epoch_) return;

    // What is missing, gathered before anything is decoded.
    //
    // **The scene's own list may not be walked across a decode.**
    // `audio_import::decode` runs a nested event loop -- it is the only way to
    // drive QAudioDecoder, which reports through signals -- and a nested event
    // loop dispatches user input with no modal dialog in front of it. So File >
    // Open is reachable from the middle of this, and it replaces the document:
    // `scene_.audio_tracks` is destroyed and rebuilt, and a range-for standing
    // in it is then walking freed memory. Ids and names copied out first cannot
    // be invalidated by anything the decode lets in.
    std::vector<std::pair<TrackId, std::string>> missing;
    for (const AudioTrack& track : doc_.scene().audio_tracks) {
        if (doc_.audioSamplesFor(track.id)) continue;
        missing.emplace_back(track.id, track.source);
    }
    if (missing.empty()) {
        refreshAudioDevice();
        return;
    }

    // Which document these are about. Anything that replaces it goes through
    // afterProjectLoaded, which moves this on -- and which decodes the new
    // document's own soundtracks on the way, so abandoning here loses nothing.
    const std::uint64_t opened = document_epoch_;

    // Saved and put back rather than simply cleared, so that the nested call
    // described above hands the outer one its own state again on the way out.
    const bool was_decoding = decoding_audio_;
    const std::uint64_t was_decoding_for = decoding_audio_for_;
    decoding_audio_ = true;
    decoding_audio_for_ = opened;

    bool replaced = false;
    for (const auto& [id, source] : missing) {
        const QString path = audioPathFor(source);
        AudioClip clip;
        if (!path.isEmpty()) {
            const audio_import::Decoded decoded = audio_import::decode(path);
            if (decoded.ok) clip = decoded.clip;
        }

        // Asked *after* the decode, because the decode is where the event loop
        // was. A document that has been replaced is not one to write into: the
        // ids in `missing` name drawings and rows that no longer exist, and the
        // same small numbers name different ones now.
        if (document_epoch_ != opened) {
            replaced = true;
            break;
        }
        // And the row itself, which the same event loop could have deleted or
        // repointed at another file. Nothing here is worth installing against a
        // track that has stopped naming the file it was decoded from. Left
        // uninstalled rather than guessed at, so the next refresh asks again.
        const AudioTrack* still = doc_.scene().findAudioTrack(id);
        if (!still || still->source != source) continue;

        doc_.setAudioSamples(id, std::move(clip));
    }

    decoding_audio_ = was_decoding;
    decoding_audio_for_ = was_decoding_for;

    // Whoever replaced the document has already opened the output for it.
    if (replaced) return;
    refreshAudioDevice();
}

// The rate to open an output at, which is the first soundtrack's own.
int MainWindow::audioRate() const {
    for (const AudioTrack& track : doc_.scene().audio_tracks) {
        const AudioClip* clip = doc_.audioSamplesFor(track.id);
        if (clip && clip->rate > 0 && !clip->empty()) return clip->rate;
    }
    return 0;
}

void MainWindow::refreshAudioDevice() {
    const int rate = audioRate();
    if (rate <= 0) {
        // No sound in this project, so no reason to hold somebody's sound card.
        audio_.close();
        audio_tried_rate_ = 0;
        audio_trouble_.clear();
        return;
    }
    // **And the output it is on may have moved.** A device is bound to the
    // output it opened and cannot follow one, so somebody switching a speaker
    // on -- which changes what the machine's default is -- leaves the scrub
    // playing to whatever was default before. Asked here rather than watched
    // for, because this already runs whenever anything changes and the answer
    // is only acted on by opening another device, which is not a thing to do in
    // the background. Reported from use.
    if (audio_.running() && audio_.openedFor() == rate && audio_.onTheRightOutput()) return;
    // A rate that has not changed and a device that has: forget the failure so
    // the open below is attempted rather than skipped by the guard.
    if (audio_.running()) audio_tried_rate_ = 0;

    // **A failure is remembered rather than retried.** This runs whenever the
    // document changes and opening a device is a third of a second, so a
    // machine with no audio output would otherwise spend that again on every
    // stroke. A soundtrack at another rate arriving is what makes it worth
    // asking again.
    if (!audio_.running() && audio_tried_rate_ == rate) return;
    audio_tried_rate_ = rate;

    QString trouble;
    if (audio_.open(rate, 2, &trouble)) {
        audio_trouble_.clear();
        return;
    }
    audio_trouble_ = trouble;
    // Said, and not raised. A machine with no sound card is not a fault in the
    // project somebody just opened, and a dialog in front of it would say
    // otherwise.
    statusBar()->showMessage(QStringLiteral("No sound: %1").arg(trouble), 8000);
}

animage::AudioProgram MainWindow::audioProgramAt(std::size_t slot) const {
    AudioProgram program;
    // The device's numbers and not a clip's: a driver that refused the rate
    // asked for is the case renderAudio resamples in, and it can only do that
    // if it is told what it is rendering for.
    program.rate = audio_.rate();
    program.channels = audio_.channels();
    program.fps = std::max(1, doc_.scene().framerate);
    program.start_slot = slot;
    for (const AudioTrack& track : doc_.scene().audio_tracks) {
        std::shared_ptr<const AudioClip> clip = doc_.sharedAudioSamplesFor(track.id);
        if (!clip || clip->empty()) continue;
        // Shared, not copied, and this is the one place in the program that
        // asks for it that way -- everything after this line is read on the
        // device's thread. See Document::sharedAudioSamplesFor.
        program.sources.push_back({std::move(clip), track.placement});
    }
    return program;
}

void MainWindow::scrubAudio(std::size_t slot) {
    // Playback has the device while a take is running, and dragging the ruler
    // during one is a request to move the playhead rather than to hear a frame.
    if (playback_timer_->isActive()) return;

    // **Has the machine's idea of where sound goes changed under us?** Somebody
    // switching a speaker on does not touch the document, so nothing else here
    // would ask -- and the first they would know of it is a scrub that makes no
    // noise. Asked at most a few times a second rather than on every frame the
    // playhead passes. Bounded rather than measured: a drag is dozens of these a
    // second and what the check costs has not been measured, so it is asked at a
    // rate that cannot matter either way.
    //
    // Reopening costs a third of a second, and it happens *before* the burst is
    // published, so the burst that noticed still plays -- late, once, and then
    // it is right again.
    if (!audio_device_checked_.isValid() || audio_device_checked_.elapsed() > 500) {
        audio_device_checked_.restart();
        if (audio_.running() && !audio_.onTheRightOutput()) refreshAudioDevice();
    }

    if (!audio_.running()) return;

    auto program = std::make_shared<AudioProgram>(audioProgramAt(slot));
    if (program->sources.empty() || program->rate <= 0) return;

    const std::int64_t rate = program->rate;
    program->length = std::max<std::int64_t>(rate / program->fps,
                                             rate * kScrubFloorMs / 1000);
    program->fade = rate * kScrubFadeMs / 1000;
    // Nothing to loop: a burst is a burst, and a shot that ended under it would
    // wrap the sound round to frame 0 half-way through a syllable.
    audio_.play(std::move(program));
}

// Where a soundtrack file's bytes are right now. importPathFor, for `audio/`.
QString MainWindow::audioPathFor(const std::string& name) const {
    if (name.empty()) return QString();
    const QString file = QString::fromStdString(name);
    if (!project_folder_.isEmpty()) {
        const QString inside = project_folder_ + QStringLiteral("/audio/") + file;
        if (QFileInfo::exists(inside)) return inside;
    }
    const auto pending = imports_.pending_audio.find(name);
    return (pending == imports_.pending_audio.end()) ? QString() : pending->second;
}

// Where an imported file's bytes are right now.
//
// The project folder once it has one, and the path it was imported from until
// then. Same order and same reason as ProjectIO::Imports: the folder is the
// answer for every import except one that has not been saved yet.
QString MainWindow::importPathFor(const std::string& name) const {
    if (name.empty()) return QString();
    const QString file = QString::fromStdString(name);
    if (!project_folder_.isEmpty()) {
        const QString inside = project_folder_ + QStringLiteral("/imports/") + file;
        if (QFileInfo::exists(inside)) return inside;
    }
    const auto pending = imports_.pending.find(name);
    return (pending == imports_.pending.end()) ? QString() : pending->second;
}

// The imported picture at 1:1, with no placement applied at all.
//
// What a placement gesture floats. The stored placement is *absolute* rather
// than a delta -- the box opens at the numbers the layer is already placed at,
// so dragging edits them rather than composing onto them -- which means the
// pixels underneath have to be the unplaced ones. Feeding it the placed grid
// from the cache would apply the placement twice.
//
// Decoded rather than kept, and only for as long as a gesture holds it. The
// precedent is LayerFootprint, which owns copies of its grids rather than
// pointing at the document's; the alternative here is a second permanent grid
// per import, which is the memory the reference shape exists not to spend.
TileGrid MainWindow::importAtOneToOne(const Layer& layer, int frame, QString* trouble) const {
    if (frame < 0 || static_cast<std::size_t>(frame) >= layer.reference_sources.size()) {
        if (trouble) *trouble = QStringLiteral("this drawing shows no frame of the import");
        return {};
    }
    const QString path = importPathFor(layer.reference_sources[static_cast<std::size_t>(frame)]);
    if (path.isEmpty()) {
        if (trouble) *trouble = QStringLiteral("the imported file is not where the project left it");
        return {};
    }
    return image_import::decode(path, trouble);
}

// What converting this layer would cost, from the file headers.
//
// Surveyed and never decoded, which is the same division the sequence import
// already makes and for the same reason: on a hundred and fifty frames the
// decode *is* the cost being described, so paying it to describe it would make
// the recap the slow part. A survey is a header read, and doing one per frame
// is milliseconds against a conversion that is seconds.
MainWindow::ConversionCost MainWindow::conversionCostFor(TrackId track_id,
                                                         LayerId layer_id) const {
    ConversionCost cost;

    const Track* track = doc_.scene().findTrack(track_id);
    const Layer* layer = track ? track->findLayer(layer_id) : nullptr;
    if (!layer || layer->kind != LayerKind::Reference) return cost;

    // The one question the popup's wording turns on, asked of the placement
    // rather than guessed at from it. Both exact paths keep every pixel: a
    // whole-pixel move re-keys tile handles and a mirror permutes them, and
    // neither reads a pixel through a filter. See Transform.
    cost.resampled =
        !layer->placement.isWholePixelTranslation() && !layer->placement.isAxisMirror();

    const std::vector<ImageId> drawings = doc_.referenceDrawings(track_id, layer_id);
    cost.drawings = drawings.size();

    // Two drawings can point at one frame -- a duplicated drawing does exactly
    // that -- and the file is then surveyed once and counted twice, because
    // each drawing gets a cel of its own and the memory is per cel.
    struct Sized {
        bool ok = false;
        std::size_t tiles = 0;
    };
    std::unordered_map<int, Sized> by_frame;

    for (const ImageId drawing : drawings) {
        const Image* record = track->findImage(drawing);
        if (!record) continue;
        const int frame = record->sourceFrameFor(layer_id);

        auto known = by_frame.find(frame);
        if (known == by_frame.end()) {
            Sized measured;
            if (frame >= 0 && static_cast<std::size_t>(frame) < layer->reference_sources.size()) {
                const QString path =
                    importPathFor(layer->reference_sources[static_cast<std::size_t>(frame)]);
                const image_import::Survey survey = image_import::survey(path);
                if (survey.ok) {
                    measured.ok = true;
                    // Through the placement, because that is what the pixels
                    // being written have been through -- an import at half size
                    // converts to a quarter of the tiles, and a recap quoting
                    // the file's own size would be describing a picture nobody
                    // is looking at. transformedBounds is what the resampler
                    // itself uses to size its destination.
                    const PixelRect placed = transformedBounds(
                        matrixOf(layer->placement), PixelRect{0, 0, survey.width, survey.height});
                    measured.tiles = tilesCovering(placed);
                }
            }
            known = by_frame.emplace(frame, measured).first;
        }

        if (known->second.ok) {
            cost.tiles += known->second.tiles;
        } else {
            ++cost.unreadable;
        }
    }
    return cost;
}

// Converts a reference layer into ordinary drawings. The doing; the asking is
// convertLayerToDrawings.
//
// **The decode is the whole cost and it happens here, on the interface thread.**
// That is the opposite of everywhere else an import is read -- a scrub decodes
// on a worker precisely so the program does not stop -- and it is right here for
// a reason a worker would not improve: this is one command that has to be all or
// nothing, so there is nothing useful to do while it runs and nothing that may
// be allowed to touch the document meanwhile. What it costs is a window that
// sits still, which is why the caller puts the frame count in front of somebody
// first and why the cursor says so while it happens.
bool MainWindow::convertReferenceLayerFrom(TrackId track_id, LayerId layer_id, QString* trouble) {
    const Track* track = doc_.scene().findTrack(track_id);
    const Layer* layer = track ? track->findLayer(layer_id) : nullptr;
    if (!layer || layer->kind != LayerKind::Reference) {
        if (trouble) *trouble = QStringLiteral("that is not an imported picture");
        return false;
    }
    if (doc_.referenceDrawings(track_id, layer_id).empty()) {
        if (trouble) *trouble = QStringLiteral("this import shows nothing on any drawing");
        return false;
    }

    // Copied, because `convertReferenceLayer` rewrites the layer as it goes and
    // this closure is read on every frame it has not got from the cache.
    const std::vector<std::string> sources = layer->reference_sources;
    const Transform placement = layer->placement;

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const Document::LayerBake done = doc_.convertReferenceLayer(
        track_id, layer_id, [&](const int frame) -> TileGrid {
            if (frame < 0 || static_cast<std::size_t>(frame) >= sources.size()) return {};
            const QString path = importPathFor(sources[static_cast<std::size_t>(frame)]);
            if (path.isEmpty()) return {};
            TileGrid decoded = image_import::decode(path);
            // Placed here rather than by the document, for ReferenceDecoder's
            // reason: what the conversion writes has to be what the compositor
            // draws, and the compositor is handed grids that are already placed.
            // Doing it anywhere else would be a second answer to "what is this
            // layer showing", which is the one thing the derive step exists to
            // have only one of.
            if (decoded.empty() || placement.isIdentity()) return decoded;
            return transformTiles(decoded, placement);
        });
    QGuiApplication::restoreOverrideCursor();

    if (done.ran_out_of_memory) {
        if (trouble) {
            *trouble = QStringLiteral(
                "there was not enough memory to convert the whole layer -- every drawing it "
                "had already written has been put back, so nothing has changed");
        }
        return false;
    }
    if (done.drawings == 0) {
        if (trouble) *trouble = QStringLiteral("nothing was converted");
        return false;
    }

    // The frames this layer had derived are still in the cache and are left
    // there rather than swept out, which is worth a line because sweeping looks
    // like the tidy thing to do. They are keyed on the drawing and the layer
    // and are valid under the placement they were made at -- so undoing the
    // conversion brings the import back with its pictures already in hand,
    // instead of re-decoding a sequence somebody has just decided against.
    // Nothing reads them meanwhile: the layer is no longer a Reference one, so
    // requestReferenceFrames does not ask about it. The cache's own bound is
    // what eventually drops them.
    // Which reaches rebuildLayerList and through it syncLayerButtons, and both
    // have something to say now: the row's flag stops calling this an import,
    // and the panel button changes what it does under an unchanged name --
    // storing a placement before, writing every drawing after.
    refreshEverything();
    return true;
}

MainWindow::ImportsReady MainWindow::importsReady() const {
    ImportsReady out;
    const ReferenceCache& cache = doc_.referenceCache();

    for (const Track& track : doc_.scene().tracks) {
        for (const Layer& layer : track.layers) {
            if (layer.kind != LayerKind::Reference || layer.reference_sources.empty()) continue;
            // A hidden layer is never asked for, so counting it would put a
            // denominator in the status bar that nothing can ever reach -- the
            // number would stick, and a number that does not move is worse than
            // no number at all, because it reads as stuck rather than as done.
            if (!layer.visible) continue;

            for (const auto& [id, image] : track.images) {
                // Absent means the layer is empty at this drawing, exactly as a
                // missing cel does. Nothing to decode, so nothing to wait for.
                if (image.sourceFrameFor(layer.id) == Image::kNoSourceFrame) continue;
                ++out.wanted;
                // Asked without renewing it: see ReferenceCache::has. Counting
                // through `find` would touch every frame of the sequence on
                // every status update and flatten the order that keeps a
                // scrub's own frames resident.
                if (cache.has(CtgKey{id, layer.id}, layer.placement)) ++out.ready;
            }
        }
    }
    return out;
}

MainWindow::ImportsReady MainWindow::drawingsColoured() const {
    ImportsReady out;
    const CtgFillCache& cache = doc_.ctgCache();

    for (const Track& track : doc_.scene().tracks) {
        for (const Layer& layer : track.layers) {
            if (layer.kind != LayerKind::Ctg) continue;
            // A hidden layer is never solved, so counting it would put a
            // denominator up that nothing can reach. Same rule as importsReady,
            // and wrong in the same way without it.
            if (!layer.visible) continue;

            for (const auto& [id, image] : track.images) {
                ++out.wanted;
                // Asked without renewing it: see CtgFillCache::has.
                if (cache.has(CtgKey{id, layer.id})) ++out.ready;
            }
        }
    }
    return out;
}

// Says, once per document, that some imported pictures could not be read.
//
// This is what is left of a function that used to derive every frame here. The
// deriving moved to a worker the paint asks -- see
// CanvasWidget::requestReferenceFrames -- and reporting stayed behind, because
// how often a person should be told is a question about the document and the
// canvas has no view of one.
//
// **Gathered rather than shown as they arrive.** A paint asks for every frame
// on screen, so one folder going missing is several reports in the same
// instant, and the cache being emptied brings them all back. So they are
// collected and one box is shown, and the flag is what stops a second one --
// a missing file stays missing, and a message box on every refresh would make
// the program unusable rather than informative.
//
// The project still opens either way. An import that will not read costs that
// picture; the drawings are untouched, and refusing to open a shot because a
// modelsheet has gone would be a worse trade than any this program makes
// elsewhere.
void MainWindow::onImportUnreadable(const QString& name, const QString& trouble) {
    const QString said = QStringLiteral("%1 (%2)").arg(name, trouble);
    if (unreadable_imports_.contains(said)) return;
    unreadable_imports_.append(said);

    // Not in the same turn of the event loop as the paint that found it: this
    // arrives from requestReferenceFrames, which runs inside paintEvent, and a
    // modal dialog opened from inside a paint re-enters the widget it is
    // painting. Queued to the next turn, by which time the rest of the
    // sequence's failures have been gathered too, so the box says all of them.
    if (import_report_queued_) return;
    import_report_queued_ = true;
    QTimer::singleShot(0, this, &MainWindow::reportUnreadableImports);
}

void MainWindow::reportUnreadableImports() {
    import_report_queued_ = false;
    if (reported_missing_imports_ || unreadable_imports_.isEmpty()) return;
    reported_missing_imports_ = true;
    QMessageBox::warning(this, QStringLiteral("Imported pictures"),
                         QStringLiteral("This project could not read %1 of its imported "
                                        "pictures, and those layers will be blank:\n\n%2")
                             .arg(unreadable_imports_.size())
                             .arg(unreadable_imports_.join(QStringLiteral("\n"))));
}

// Waits for every imported picture on screen to be decoded and installed.
//
// **Never on an ordinary path.** It is here for tests, which change a placement
// directly and then want the consequence, and for anything that must not run
// ahead of the picture. Waiting for a decode on the interface thread is the
// thing the worker exists to stop.
bool MainWindow::settleReferenceFrames(int timeout_ms) {
    return canvas_ && canvas_->settleReferenceFrames(timeout_ms);
}

// --- tracks --------------------------------------------------------------

// The one place the current track changes. Everything downstream holds a track
// id -- the canvas composites all of them but draws into this one, the layer
// panel lists this one's layers, and every timing button acts on it -- so they
// are all rebound here rather than at each call site.
void MainWindow::setCurrentTrack(TrackId track) {
    if (track == kNoId || track == track_) {
        syncTrackMenu();
        return;
    }
    stopPlayback();
    track_ = track;

    canvas_->setTrack(track_);
    timeline_widget_->setTrack(track_);

    // The active layer belonged to the track we have just left, and a layer id
    // from another track is not a layer this one has.
    const Track* now = doc_.scene().findTrack(track_);
    canvas_->setActiveLayer((now && !now->layers.empty()) ? now->layers.front().id : kNoId);

    rebuildLayerList();
    syncTrackMenu();
    // The dock is sized by how many tracks there are, and a track appearing or
    // going is exactly when that changes. Cheap to ask twice -- it returns at
    // once when the row count is what it already was -- and the alternative is
    // remembering to ask on every path that adds or removes one.
    syncTimelineHeight();
    canvas_->refreshAll();
    syncStatus();
}

// New tracks go under the ones already there. Index 0 composites on top, so
// adding at the bottom leaves what you can already see where it was -- a new
// track arriving in front of the drawing you are working on would be a surprise
// every time, and moving it back is not something the interface offers yet.
void MainWindow::addTrack() {
    stopPlayback();
    const std::string name = "track " + std::to_string(doc_.scene().tracks.size() + 1);

    doc_.beginCommand("Add track");
    const TrackId added = doc_.addTrack(name);
    doc_.addLayer(added, "layer 1");
    // One drawing, so the track has somewhere to draw from the moment it exists.
    // Without it the new track is a row of nothing and the brush does nothing,
    // with no way to tell that from a bug.
    doc_.insertImage(added, 0);
    doc_.endCommand();

    setCurrentTrack(added);
    refreshEverything();
}

// The menu's way in. Double-clicking the name in the gutter is the other, and
// this one stays because a menu item is discoverable and a double click is not.
//
// Built rather than QInputDialog::getText, only so that the field can be given
// the same cap the in-place editors have: the static helper offers no way to
// reach its line edit, and a dialog that accepts a name the row next to it would
// refuse is two different rules for one thing.
// The row you are pointed at, which may be a soundtrack.
//
// **The Track menu follows the highlight, and that is the whole of what the two
// selections owe the interface.** `track_` goes on meaning "where the brush
// is", because five things read it and every one of them wants a real Track --
// but a menu item acting on a row that is not the lit one is a menu item acting
// on something nobody pointed at. Reported from use: Delete track, with a
// soundtrack highlighted, offered to delete a drawing track.
void MainWindow::renameTrack() {
    const TrackId sound_id = timeline_widget_->highlightedAudio();
    const AudioTrack* sound = doc_.scene().findAudioTrack(sound_id);
    const Track* track = sound ? nullptr : doc_.scene().findTrack(track_);
    if (!sound && !track) return;

    QInputDialog ask(this);
    ask.setWindowTitle(sound ? QStringLiteral("Rename soundtrack")
                             : QStringLiteral("Rename track"));
    ask.setLabelText(QStringLiteral("Name"));
    ask.setTextValue(QString::fromStdString(sound ? sound->name : track->name));
    if (auto* field = ask.findChild<QLineEdit*>()) field->setMaxLength(names::kTyped);
    if (ask.exec() != QDialog::Accepted) return;

    const QString trimmed = ask.textValue().trimmed();
    // A track with no name has no row label and no prefix on its exported
    // files, so the old one is kept rather than accepting the emptiness.
    if (trimmed.isEmpty()) return;

    if (sound) {
        // The label and not the file: `source` is what is in `audio/` and this
        // does not touch it. The row's tooltip goes on saying which file it is.
        doc_.renameAudioTrack(sound_id, trimmed.toStdString());
        refreshEverything();
        return;
    }

    TrackProperties props = track->properties();
    props.name = trimmed.toStdString();
    doc_.updateTrack(track_, props);
    refreshEverything();
}

// The whole track again, layers, drawings and all.
//
// **On a soundtrack this is also how a scene gets a second one.** The model has
// been a list since audio arrived and the row loop has always walked it; what
// was missing was any way to put another one in, and duplicating the one you
// have is the obvious one -- the same take at two placements is an ordinary
// thing to want while a shot is being timed.
void MainWindow::duplicateCurrentTrack() {
    stopPlayback();

    if (const AudioTrack* sound =
            doc_.scene().findAudioTrack(timeline_widget_->highlightedAudio())) {
        const TrackId made = doc_.duplicateAudioTrack(sound->id);
        if (made == kNoId) return;
        // The samples across rather than a second decode of the same file:
        // `core` does not decode, and the clip is already here. A copy of a few
        // megabytes against tens of milliseconds of disk and codec.
        if (const AudioClip* clip = doc_.audioSamplesFor(sound->id))
            doc_.setAudioSamples(made, *clip);
        refreshEverything();
        return;
    }

    const TrackId made = doc_.duplicateTrack(track_);
    if (made == kNoId) return;
    // Onto the copy, which is what every other "make me a new one" in this
    // program does: adding a track, adding a layer and duplicating a drawing
    // all leave you standing on the thing you just made.
    setCurrentTrack(made);
    refreshEverything();
}

void MainWindow::removeCurrentTrack() {
    // A soundtrack first, for renameTrack's reason: the highlight is what the
    // menu acts on. There is no "a scene needs at least one" rule here -- a
    // scene with no sound is the ordinary case, not an empty one.
    if (const AudioTrack* sound = doc_.scene().findAudioTrack(timeline_widget_->highlightedAudio())) {
        if (QMessageBox::question(
                this, QStringLiteral("Delete this soundtrack?"),
                QStringLiteral("\"%1\" will go from the shot. The file stays in the project "
                               "folder, and this can be undone.")
                    .arg(QString::fromStdString(sound->name)),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) != QMessageBox::Yes) {
            return;
        }
        stopPlayback();
        doc_.removeAudioTrack(sound->id);
        // The highlight goes back to the drawing row, because the row it was on
        // is not there any more.
        timeline_widget_->clearAudioHighlight();
        refreshEverything();
        return;
    }

    if (doc_.scene().tracks.size() <= 1) {
        // The same rule as the last drawing: leave something to draw on.
        QMessageBox::information(this, QStringLiteral("Cannot delete that track"),
                                 QStringLiteral("A scene needs at least one track."));
        return;
    }
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;

    if (QMessageBox::question(
            this, QStringLiteral("Delete this track?"),
            QStringLiteral("\"%1\" and every drawing on it will go. This can be undone.")
                .arg(QString::fromStdString(track->name)),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }

    stopPlayback();
    const std::size_t index = static_cast<std::size_t>(
        std::distance(doc_.scene().tracks.data(), track));
    doc_.removeTrack(track_);

    // Whichever track took its place in the stack, or the last one if it was
    // the bottom: leaving the current track dangling would point the canvas and
    // the layer panel at a track that no longer exists.
    const std::vector<Track>& left = doc_.scene().tracks;
    track_ = kNoId;
    setCurrentTrack(left[std::min(index, left.size() - 1)].id);
    refreshEverything();
}

void MainWindow::setOverwriteDrawings(bool overwrite) {
    if (updating_track_menu_) return;  // the menu being told, not the user asking
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->overwrite_drawings == overwrite) return;

    TrackProperties props = track->properties();
    props.overwrite_drawings = overwrite;
    doc_.updateTrack(track_, props);
    // The timeline draws the setting on the row, and a drag behaves differently
    // under it, so the row has to be redrawn as well as the menu ticked.
    timeline_widget_->refresh();
    syncStatus();
}

void MainWindow::setTrackEnd(TrackEnd behaviour) {
    if (updating_track_menu_) return;  // the menu being told, not the user asking
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->end == behaviour) return;

    TrackProperties props = track->properties();
    props.end = behaviour;
    doc_.updateTrack(track_, props);

    // It changes what is on screen at every frame past this track's last
    // drawing, and what the row says about itself.
    timeline_widget_->refresh();
    canvas_->refreshAll();
    syncStatus();
}

void MainWindow::syncTrackMenu() {
    if (!overwrite_action_) return;
    // **Off while a soundtrack is highlighted**, because neither of these means
    // anything for one: a soundtrack has no drawings to overwrite and no last
    // drawing to be past. They would otherwise act on the track the brush is
    // on, which is not the row anybody is pointed at -- the same confusion
    // Rename and Delete had, in the two items that cannot simply follow the
    // highlight instead.
    const bool on_sound =
        timeline_widget_ &&
        doc_.scene().findAudioTrack(timeline_widget_->highlightedAudio()) != nullptr;
    const Track* track = on_sound ? nullptr : doc_.scene().findTrack(track_);
    updating_track_menu_ = true;
    overwrite_action_->setEnabled(track != nullptr);
    overwrite_action_->setChecked(track && track->overwrite_drawings);
    for (const auto& [action, behaviour] : end_actions_) {
        action->setEnabled(track != nullptr);
        action->setChecked(track && track->end == behaviour);
    }
    // And the submenu itself. Greying its three items leaves a menu that still
    // opens to offer a choice and then refuses all of it, which is worse than
    // one that does not open: the second says there is nothing here, the first
    // says there is something here you may not have.
    if (end_menu_) end_menu_->menuAction()->setEnabled(track != nullptr);
    updating_track_menu_ = false;
}

// --- time ----------------------------------------------------------------

void MainWindow::onSlotChanged(std::size_t slot) {
    canvas_->setFrame(slot);
    // What the panel says about a colour layer is about the drawing you are
    // standing on, so moving changes it even when no fill is rebuilt. Arriving
    // at a drawing that was already solved fires nothing at all, and without
    // this the row went on showing the flag from the drawing you left.
    refreshLayerFlags();
    syncStatus();
}

// Over the scene and not over one track: the playhead is the timeline's, so
// stepping walks the whole shot however long the track being edited is.
void MainWindow::stepFrame(int delta) {
    // Everything reachable rather than the shot: stepping has to get to a
    // drawing that sits past a fixed scene length, or it could not be edited --
    // and to a soundtrack running past the drawings, which is the ordinary
    // first state of a lipsync shot and the one place stepping matters most.
    const int count = static_cast<int>(doc_.timelineFrames());
    if (count <= 0) return;

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

// Where a new drawing goes is the track's decision, not this window's: with
// "overwrite drawings" on it lands on the playhead and spends the rest of the
// hold, and without it it goes in after the whole hold and the shot gets a frame
// longer. Both live in Document::addDrawing, so the two buttons here do the same
// thing whichever the track is set to -- and neither has to know which.
void MainWindow::goToNewDrawing(ImageId made) {
    timeline_widget_->refresh();
    const Track* track = doc_.scene().findTrack(track_);
    if (track && made != kNoId) {
        // Asked rather than assumed. Under overwrite the drawing does not land
        // where the playhead was, and it does not land after the hold either.
        const std::size_t at = track->firstSlotOf(made);
        if (at < track->slots.size()) timeline_widget_->setCurrentSlot(at);
    }
    refreshEverything();
}

void MainWindow::insertInterval() {
    stopPlayback();
    goToNewDrawing(doc_.addDrawing(track_, timeline_widget_->currentSlot()));
}

void MainWindow::duplicateDrawing() {
    stopPlayback();
    goToNewDrawing(doc_.duplicateDrawing(track_, timeline_widget_->currentSlot()));
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

// One command for the pair, so changing both and then changing your mind is one
// undo rather than two.
void MainWindow::chooseSceneSettings() {
    stopPlayback();
    const int was_framerate = doc_.scene().framerate;
    const int was_width = doc_.scene().width;
    const int was_height = doc_.scene().height;
    const int was_length = doc_.scene().length;
    const bool was_fixed = doc_.scene().fixed_length;

    SceneSettingsDialog dialog(was_framerate, was_width, was_height, was_fixed, was_length,
                               static_cast<int>(doc_.scene().longestTrack()), this);

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
    doc_.setSceneLength(dialog.fixedLength(), dialog.sceneLength());
    doc_.endCommand();

    // The canvas bounds the colour fills, so they have to be solved again, and
    // the shot's length changes how far the timeline runs.
    timeline_widget_->refresh();
    canvas_->refreshAll();
    syncStatus();
}

void MainWindow::togglePlayback() {
    if (playback_timer_->isActive()) {
        stopPlayback();
        return;
    }

    if (doc_.scene().shotFrames() < 2) return;

    playback_start_slot_ = timeline_widget_->currentSlot();

    // **The sound first, because the picture is about to be derived from it.**
    // A take with a soundtrack in it plays to the device's count of what has
    // come out; one without plays to the wall clock exactly as it always did,
    // and `playing_to_audio_` is which. See onPlaybackTick.
    //
    // The output is checked here rather than trusted: Play is a deliberate
    // press, so a third of a second spent opening the right device is a cost
    // worth paying once, where doing it on a scrub would not be.
    refreshAudioDevice();
    startPlaybackAudio(playback_start_slot_);

    playback_clock_.start();
    rate_samples_.clear();
    last_rate_sample_ms_ = 0;
    // Not red, and said out loud rather than left over from the last take: a
    // readout that opens red because the previous shot was heavy is accusing
    // this one of something it has not done yet.
    rate_dropping_ = false;
    QPalette opening = playback_rate_->palette();
    opening.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::WindowText));
    playback_rate_->setPalette(opening);
    // Nominal until there is a window to say otherwise, which is the honest
    // opening claim: nothing has been dropped yet.
    playback_rate_->setText(
        QStringLiteral("playing %1 fps").arg(std::max(1, doc_.scene().framerate)));
    playback_rate_->show();
    canvas_->setPlaying(true);
    playback_timer_->start(1);
    if (play_action_) play_action_->setText(QStringLiteral("Stop"));
    if (play_button_) play_button_->setText(QStringLiteral("Stop"));
    syncStatus();
}

void MainWindow::startPlaybackAudio(std::size_t slot) {
    playing_to_audio_ = false;
    if (!audio_.running()) return;

    auto program = std::make_shared<AudioProgram>(audioProgramAt(slot));
    if (program->sources.empty() || program->rate <= 0) return;

    // The loop, which is one number here and one number in slotForPlayedFrames
    // -- the picture wraps because the count it is derived from wrapped, and
    // the sound wraps because the position it is read from wrapped. There is
    // nothing to keep in agreement. See docs/importing.md, "the playback clock".
    program->loop_slots = doc_.scene().shotFrames();
    // A ramp in, so a take that starts part-way up a waveform does not open
    // with a click. Nothing at the seam: a loop is the same shot again and a
    // dip at every pass would be the more noticeable of the two.
    program->fade = std::int64_t(program->rate) * kScrubFadeMs / 1000;
    audio_.play(std::move(program));
    playing_to_audio_ = true;
}

// Somebody plugged a speaker in, switched one on, or pulled one out.
//
// **Told rather than asked**, which is the whole of the fix here. The first
// version only looked when something else made it look -- at Play, and a few
// times a second during a scrub -- and it was reported as working for a scrub
// and not for Play. That is what a question asked before the answer has arrived
// looks like: Qt learns about a device from the system, and holding the watch
// is what makes it listen at all. See AudioDevice::watchOutputs.
void MainWindow::onAudioOutputsChanged() {
    const bool was_playing = playing_to_audio_;
    // **The remembered failure is forgotten here and nowhere else.** It exists
    // so that a machine with no audio output does not spend a third of a second
    // trying again on every edit -- and the news that the machine's outputs
    // have changed is exactly the news that makes trying again worthwhile.
    // Without this, plugging a speaker into a machine that had none when the
    // project opened would be remembered as still having none.
    audio_tried_rate_ = 0;
    refreshAudioDevice();
    if (!was_playing) return;

    // A take was running on the device that has just been replaced, and the
    // count it derives from starts again with the new one. **Re-anchored on the
    // frame it has reached** rather than the frame Play was pressed on, or the
    // take would jump back to where it started.
    playback_start_slot_ = timeline_widget_->currentSlot();
    startPlaybackAudio(playback_start_slot_);
    playback_clock_.start();
    rate_samples_.clear();
    last_rate_sample_ms_ = 0;
}

void MainWindow::stopPlayback() {
    if (!playback_timer_->isActive()) return;
    playback_timer_->stop();
    // The device stays open and goes quiet, which is the whole reason it is
    // kept open: the next scrub is a burst away rather than a third of a
    // second away. See audio_device.h.
    audio_.silence();
    playing_to_audio_ = false;
    playback_rate_->hide();
    rate_samples_.clear();
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
    // The whole shot, which is the longest track: playing only as far as the
    // track being edited would cut the take short whenever a background ran
    // longer than the character on it. What a shorter track shows out there is
    // Track::imageAtSlot's answer -- nothing, today.
    // The shot and not the timeline: playback stops at the boundary, which is
    // what makes a fixed length worth setting. Drawings past it are still there,
    // they are simply not in the take.
    const std::size_t count = doc_.scene().shotFrames();
    if (count == 0) {
        stopPlayback();
        return;
    }

    const int fps = std::max(1, doc_.scene().framerate);
    std::size_t slot = 0;

    if (playing_to_audio_) {
        // **The one line the whole of docs/importing.md's "playback clock" is
        // about.** The picture's position comes from how much audio has come
        // out of the device rather than from how long ago Play was pressed, and
        // three of the four ways two clocks come apart stop existing rather
        // than being separately corrected: the device's output latency is
        // already inside the count, a loop seam wraps both together because
        // there is only one number, and an interface stall cannot reach a
        // number that is not counted on this thread.
        const std::int64_t played = audio_.playedFrames();
        if (played <= 0) {
            // Nothing has been heard yet. **The picture holds on the frame Play
            // was pressed on**, which is what starting together means and is
            // the visible cost of the feature: about a buffer, a quarter of a
            // second on the machine the spike measured.
            //
            // The readout is not sampled through the hold. It counts paints
            // against wall time, and a quarter-second of deliberate stillness
            // inside its first window reads as dropped frames -- which is a
            // warning crying wolf on the one take it should be trusted on.
            rate_samples_.clear();
            last_rate_sample_ms_ = playback_clock_.elapsed();

            // And a bound on the wait, for a device that will never report. See
            // kAudioStartWaitMs. The clock restarts so the take begins now
            // rather than a second in.
            if (playback_clock_.elapsed() > kAudioStartWaitMs) {
                playing_to_audio_ = false;
                playback_clock_.start();
                last_rate_sample_ms_ = 0;
            }
            return;
        }
        slot = slotForPlayedFrames(playback_start_slot_, played, audio_.rate(), fps, count);
    } else {
        const qint64 elapsed = playback_clock_.elapsed();
        const qint64 advanced = elapsed * fps / 1000;
        slot = (playback_start_slot_ + static_cast<std::size_t>(advanced)) % count;
    }

    // Before the early return below, not after it. This tick fires every
    // millisecond and mostly finds the slot unchanged, and the reading has to
    // keep up during a stall -- which is exactly when the slot is changing
    // least often.
    //
    // Still against the wall clock, and deliberately: what it measures is
    // whether the *interface* is keeping up, so a picture derived from the
    // sound must not be judged against the sound as well. That would be the
    // instrument and the thing it measures being the same number.
    updatePlaybackRate();

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

// What a row says, including what this layer is doing on the drawing in front
// of you.
//
// On that drawing and no other, which is what makes it belong in the layer
// panel: the panel says what the layer is doing *now*. The timeline is where
// the same facts are laid out across time, and walking onto a flagged drawing
// is what brings its flag in here.
QString MainWindow::layerLabel(const Layer& layer, ImageId here) const {
    const QString name = QString::fromStdString(layer.name);
    if (layer.kind != LayerKind::Ctg) return name;

    // A glyph and the name, and everything else in the tooltip. The panel is a
    // couple of hundred pixels wide, and a row that spells out what it is doing
    // is a row elided to "..." -- which says less than nothing, because it
    // hides the very words that were the point.
    ImageId from = kNoId;
    if (doc_.ctgScribblesAt(track_, here, layer.id, &from) && from != here) {
        return QStringLiteral("← ") + name;
    }
    return name;
}

// What a row is, said in the row itself -- issue #84.
//
// Three kinds and three treatments, and the reason they are not the same
// treatment is that **the brush behaves differently on each**, which is the
// thing a row was failing to say. On a reference layer the brush, the eraser
// and cutting all refuse: there is no cel and none that could be made. On a
// colour layer the brush *does* work -- it lays down scribbles -- and what
// refuses is cut, copy and transform. On a raster layer everything works.
//
// | kind | what the row does | why that colour |
// |---|---|---|
// | Raster | nothing | there is nothing to say |
// | Colour | the carried blue | the same blue a carried cell is drawn in, one panel over: it is the same machinery and saying so twice in two colours would be worse than not saying it |
// | Reference | the theme's disabled grey | the one colour every theme already uses for "you cannot act here" |
//
// **The grey was chosen when that was the whole truth about one of these rows,
// and it is no longer.** This paragraph used to carry the prediction rather
// than the fact: it said the treatment would have to be revisited the day
// *convert to drawings* existed, and pointed at docs/importing.md for a feature
// that was then on the queue. It exists now, and so does *place this picture* --
// both are buttons in this panel, both act on a reference layer, and neither
// refuses. What still refuses is the brush, the eraser and cutting, which is
// narrower than "you cannot act here" and is what the row is actually saying.
//
// The treatment itself was not revisited when those landed. That is recorded
// here as what happened rather than argued either way.
//
// Everything else goes in the tooltip and not in the row, for `layerLabel`'s
// reason: the panel is a couple of hundred pixels wide, and a row that spells
// out what it is doing is a row elided to "..." -- which hides the very words
// that were the point.
void MainWindow::applyLayerFlag(QTreeWidgetItem* item, const Layer& layer, ImageId here) {
    if (layer.kind == LayerKind::Reference) {
        item->setForeground(0, QBrush(palette().color(QPalette::Disabled, QPalette::Text)));
        QString tip = QStringLiteral(
            "An imported picture. Its pixels come from a file rather than from a\n"
            "drawing, so the brush, the eraser and the transform all refuse here.\n"
            "The way in is to convert it to drawings first.");
        if (!layer.reference_sources.empty()) {
            // The file, because the layer's name is a label somebody may have
            // changed and this is the only place the two can be told apart.
            // Not the whole list of a sequence: two hundred file names is a
            // tooltip nobody reads.
            tip += QStringLiteral("\n\nFrom %1")
                       .arg(QString::fromStdString(layer.reference_sources.front()));
            if (layer.reference_sources.size() > 1) {
                tip += QStringLiteral(" and %1 more, in the project's imports folder.")
                           .arg(layer.reference_sources.size() - 1);
            } else {
                tip += QStringLiteral(", in the project's imports folder.");
            }
        }
        item->setToolTip(0, tip);
        return;
    }

    if (layer.kind != LayerKind::Ctg) {
        item->setForeground(0, QBrush());
        item->setToolTip(0, QString());
        return;
    }

    const Track* track = doc_.scene().findTrack(track_);
    QString tip = QStringLiteral("A colour layer: it holds scribbles, and the fill is worked\n"
                                 "out from them and the %1 layer%2 it is cut against.")
                      .arg(layer.ctg_sources.size())
                      .arg(layer.ctg_sources.size() == 1 ? "" : "s");

    ImageId from = kNoId;
    const bool carried =
        doc_.ctgScribblesAt(track_, here, layer.id, &from) && from != here;
    if (carried) {
        const Image* source = track ? track->findImage(from) : nullptr;
        tip += QStringLiteral("\n\nThe marks here were made on drawing %1 and carried to this\n"
                              "one. Scribbling here detaches this drawing, and the drawings\n"
                              "after it follow this one instead.")
                   .arg(source ? source->number : 0);
    }

    // The same blue a carried cell is drawn in, and for the same reason -- see
    // marks.h. A colour layer is where the colour-through-time machinery lives,
    // and the row saying so in the colour the timeline already says it in is
    // one fact learned once instead of two.
    item->setForeground(0, QBrush(marks::kCarried));
    item->setToolTip(0, tip);
}

// Fills the colour-layer box from the layer in hand, or hides it.
//
// Sources are listed as every raster layer in the track with a tick against the
// ones this layer cuts against, rather than as the ids it happens to hold: a
// layer that has since been deleted simply stops being offered, and one added
// afterwards can be ticked without the set having to be rebuilt from scratch.
void MainWindow::syncColourLayerPanel() {
    if (!colour_settings_) return;

    const Layer* layer = currentLayer();
    const Track* track = doc_.scene().findTrack(track_);
    if (!layer || !track || layer->kind != LayerKind::Ctg) {
        colour_settings_->setVisible(false);
        return;
    }
    colour_settings_->setVisible(true);

    updating_colour_panel_ = true;
    ctg_sources_->clear();
    for (const Layer& other : track->layers) {
        if (other.kind != LayerKind::Raster) continue;  // a flat has no edges to cut along
        auto* item = new QListWidgetItem(QString::fromStdString(other.name), ctg_sources_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(other.id));
        const bool used = std::find(layer->ctg_sources.begin(), layer->ctg_sources.end(),
                                    other.id) != layer->ctg_sources.end();
        item->setCheckState(used ? Qt::Checked : Qt::Unchecked);
    }

    ctg_inherit_->setChecked(layer->ctg_inherit);
    ctg_direction_->setCurrentIndex(layer->ctg_direction == CtgDirection::Backward  ? 1
                                    : layer->ctg_direction == CtgDirection::Nearest ? 2
                                                                                    : 0);
    ctg_follow_->setChecked(layer->ctg_follow_motion);

    // All three of these are about what gets carried, so none of them means
    // anything while nothing is. Greyed rather than hidden, so the shape of the
    // choice stays visible: this is what you would be choosing if you turned it
    // on.
    ctg_direction_->setEnabled(layer->ctg_inherit);
    ctg_follow_->setEnabled(layer->ctg_inherit);
    updating_colour_panel_ = false;
}

void MainWindow::onCtgSourcesChanged() {
    if (updating_colour_panel_) return;
    Layer* layer = currentLayer();
    if (!layer || layer->kind != LayerKind::Ctg) return;

    Layer updated = *layer;
    updated.ctg_sources.clear();
    for (int row = 0; row < ctg_sources_->count(); ++row) {
        const QListWidgetItem* item = ctg_sources_->item(row);
        if (item->checkState() != Qt::Checked) continue;
        updated.ctg_sources.push_back(
            static_cast<LayerId>(item->data(Qt::UserRole).toULongLong()));
    }
    doc_.updateLayer(track_, updated.id, updated);

    // The barrier moved, so every fill built from it is wrong. They are keyed on
    // the source cels' revisions and those have not moved, so nothing would
    // notice on its own.
    doc_.ctgCache().clear();
    canvas_->refreshAll();
    // What the row says about the layer is about the layer, so it is said now
    // rather than whenever the next fill happens to land. It used to ride on
    // the re-solve this causes, which was invisible while a solve finished
    // inside the paint that started it and became a stale panel the moment the
    // solve moved to another thread.
    refreshLayerFlags();
    syncStatus();
}

void MainWindow::onCtgSettingChanged() {
    if (updating_colour_panel_) return;
    Layer* layer = currentLayer();
    if (!layer || layer->kind != LayerKind::Ctg) return;

    Layer updated = *layer;
    updated.ctg_inherit = ctg_inherit_->isChecked();
    updated.ctg_direction = ctg_direction_->currentIndex() == 1   ? CtgDirection::Backward
                            : ctg_direction_->currentIndex() == 2 ? CtgDirection::Nearest
                                                                  : CtgDirection::Forward;
    updated.ctg_follow_motion = ctg_follow_->isChecked();
    doc_.updateLayer(track_, updated.id, updated);

    // Which drawing's marks a drawing reads is not part of the fill's key
    // either -- it is resolved on the way in -- so the same applies.
    doc_.ctgCache().clear();
    ctg_direction_->setEnabled(updated.ctg_inherit);
    ctg_follow_->setEnabled(updated.ctg_inherit);
    canvas_->refreshAll();
    refreshLayerFlags();
    syncStatus();
}

// Waits for every solve in flight, for a test that needs to look at the result
// rather than at the fact that it was asked for.
bool MainWindow::waitForColour() { return canvas_->settleColour(); }

// The rows already exist; only what they report has moved.
//
// Deliberately not rebuildLayerList. That clears the tree and builds new items,
// which throws away the selection -- and because a fill is solved inside a
// paint and reported afterwards, it would delete rows out from under anything
// still holding one. A test was holding one, and said so by crashing. A solve
// finishing changes two words and a colour, so it changes two words and a
// colour.
void MainWindow::refreshLayerFlags() {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || !layer_list_) return;
    const ImageId here = canvas_->currentImage();

    const int rows = std::min(layer_list_->topLevelItemCount(),
                              static_cast<int>(track->layers.size()));
    updating_list_ = true;
    for (int row = 0; row < rows; ++row) {
        const Layer& layer = track->layers[static_cast<std::size_t>(row)];
        QTreeWidgetItem* item = layer_list_->topLevelItem(row);
        item->setText(0, layerLabel(layer, here));
        applyLayerFlag(item, layer, here);
    }
    updating_list_ = false;
    // This is the frame-change path, and whether the layer in front of you can
    // be moved through time depends on the frame: past a track's last drawing
    // there is nothing to float. Nothing else here would ask.
    syncLayerButtons();
}

void MainWindow::rebuildLayerList() {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || !layer_list_) return;

    // Whose layers these are, in the one place in this panel that had nothing to
    // say and room to say it.
    //
    // Nothing here used to name the track. The dock is titled "Layers" and the
    // column beneath it was headed "Layer", which is the same word twice and no
    // information -- while the fact a reader actually needs is that these are
    // *this track's* layers and another track's may be called exactly the same
    // thing. That fact was on screen, in the status bar, eight readings along and
    // the width of the window away from the rows it qualifies; being far from the
    // decision is the same as being absent. So the dock says what the panel is
    // and the header says whose, and the two read as a pair.
    //
    // Here rather than in buildLayerPanel because this is the one function every
    // path that changes the current track ends in -- clicking a row in the
    // timeline through setCurrentTrack, Add and Delete track, undo, Open, New --
    // and it is also where a *rename* of the track arrives, by both routes: the
    // gutter editor through documentChanged, and Track > Rename track through
    // refreshEverything.
    //
    // The tooltip says the name in full and says what it is, which is the answer
    // to both of the things a bare name in a header cannot do: a long one is
    // elided in a column this narrow, and "rough" alone could be read as another
    // layer rather than as the track the layers belong to.
    const QString called = QString::fromStdString(track->name);
    layer_list_->headerItem()->setText(0, called);
    layer_list_->headerItem()->setToolTip(0, QStringLiteral("Layers of \"%1\"").arg(called));

    const LayerId active = canvas_->activeLayer();

    updating_list_ = true;
    layer_list_->clear();
    QTreeWidgetItem* active_item = nullptr;
    const ImageId here = canvas_->currentImage();
    for (const Layer& layer : track->layers) {
        auto* item = new QTreeWidgetItem(layer_list_);
        item->setText(0, layerLabel(layer, here));
        // Draggable, and never a place to drop another layer *onto*: a stack is
        // flat, so the only two answers a drop has are above this row and below
        // it. With the row itself drop-enabled there is a third, and Qt draws it
        // as a box round the row that means something this panel cannot do.
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled |
                        Qt::ItemIsEditable) &
                       ~Qt::ItemIsDropEnabled);
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
        applyLayerFlag(item, layer, here);
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
    if (!layer) {
        // An empty panel -- a track whose last layer has just been removed --
        // and the button has to say so rather than go on offering the answer it
        // gave about a layer that is not there any more.
        syncLayerButtons();
        return;
    }
    canvas_->setActiveLayer(layer->id);
    if (opacity_) {
        const QSignalBlocker block(opacity_);
        opacity_->setValue(static_cast<int>(layer->opacity * 100.0f));
    }

    // Stepping off a colour layer with transparency in hand puts the last real
    // colour back, rather than leaving a brush loaded with negative light.
    if (layer->kind != LayerKind::Ctg && colourIsTransparent()) {
        applyColour(solid_r_, solid_g_, solid_b_);
    } else {
        syncColourControls();
    }
    syncColourLayerPanel();
    syncLayerButtons();
}

// Whether the layer in front of you can be moved through time, and what to say
// when it cannot.
//
// Greyed out and explained rather than absent: a button that comes and goes as
// you move between layers is a button nobody can find twice, and "why can I not
// do this here" is exactly the question a disabled control exists to answer.
//
// **The list is refuseHere's list, in refuseHere's order**, and both halves of
// that matter. The order is not cosmetic -- the kind is asked ahead of the
// lock, because unlocking a colour layer would not make this work and naming
// the lock would send somebody to fix the wrong thing. And a reason missing
// from here is worse than one in the wrong place: the button stays pressable,
// the press refuses, and the answer arrives as a status line that goes away
// instead of a tooltip that is there when you look for it. "There is no drawing
// at this frame" was that reason, and it is the one that changes as the
// playhead moves rather than as the selection does -- which is why
// refreshLayerFlags asks this again and not only onLayerSelected.
void MainWindow::syncLayerButtons() {
    if (!layer_transform_) return;

    // How many drawings a whole-layer gesture would touch, whichever kind of
    // layer it is.
    //
    // **Two lists answer this and they are not interchangeable.** An ordinary
    // layer says it with cels; a reference layer has none at all, and what says
    // it is not blank at a drawing is a `source_frames` entry -- so
    // `layerDrawings` reports every import as empty. Asking through one function
    // is what stops the two rules below drifting apart, which is how a
    // duplicated import came to be greyed on a count of its files.
    const auto wholeLayerDrawings = [this](const Layer& of) {
        return of.kind == LayerKind::Reference ? doc_.referenceDrawings(track_, of.id).size()
                                               : doc_.layerDrawings(track_, of.id).size();
    };

    const Layer* layer = currentLayer();
    QString refusal;
    if (canvas_->currentImage() == kNoId) {
        refusal = QStringLiteral("there is no drawing at this frame.\n"
                                 "This track stops earlier; scrub back to a frame it has\n"
                                 "a drawing on and the whole layer still moves together.");
    } else if (!layer) {
        refusal = QStringLiteral("there is no layer selected");
    } else if (layer->kind == LayerKind::Ctg) {
        refusal = QStringLiteral("a colour layer cannot be transformed.\n"
                                 "A mark on one is a label rather than paint, and turning or\n"
                                 "scaling it would blend two labels into a third colour that\n"
                                 "then competes for regions on its own account.");
    } else if (layer->locked) {
        refusal = QStringLiteral("this layer is locked");
    } else if (!layer->visible) {
        refusal = QStringLiteral("this layer is hidden");
    } else if (wholeLayerDrawings(*layer) == 0) {
        // Last but one, because beginLayerTransform asks these two last. Cels
        // and not ink: a layer with no cel anywhere has nothing to move and is
        // the case worth catching, which is a brand-new layer. Asking about the
        // ink would mean paintedBounds over every drawing, which reads pixels
        // -- too much to pay every time the playhead moves, and the answer it
        // would add is a layer whose cels are all empty, which the grid
        // releases anyway.
        //
        // Through the same helper as the count below, so an import whose
        // drawings have all been deleted is caught here rather than falling
        // through to an enabled button with nothing to act on.
        refusal = layer->kind == LayerKind::Reference
                      ? QStringLiteral("this import shows nothing on any drawing")
                      : QStringLiteral("nothing is drawn on this layer yet");
    } else if (wholeLayerDrawings(*layer) == 1) {
        // **Through time needs a second drawing to be through**, and this is one
        // sentence covering both kinds of layer rather than two rules that
        // happen to agree. With one drawing the button used to hand the gesture
        // to the Transform tool -- which is the right gesture and the wrong
        // button: it read "Transform layer through time" and did something else,
        // on the most ordinary layer there is.
        //
        // **Counted in drawings and never in files**, which is a correction a
        // report made. Keyed on the file count, an imported still that had been
        // duplicated stayed greyed for ever: one file, two drawings, and a
        // control refusing on the grounds that there is one of something there
        // are plainly two of. What is being asked is whether there is a second
        // drawing for the gesture to reach *through*, and a duplicated import
        // has one.
        //
        // The Transform tool is still keyed on files, and the two are different
        // questions rather than an inconsistency left behind: this asks whether
        // there is any time to move through, and the tool asks whether "this
        // drawing" names one picture -- which on any number of drawings of a
        // single file it does. So a duplicated still is the one layer where both
        // doors are open, and each is open for its own reason.
        refusal = layer->kind == LayerKind::Reference
                      ? QStringLiteral("this is one imported picture on one drawing, and\n"
                                       "through time needs a second to be through; use the\n"
                                       "Transform tool, which moves it without writing anything.")
                      : QStringLiteral("there is only one drawing on this layer.\n"
                                       "Through time needs a second one to be through; use the\n"
                                       "Transform tool, which is the same gesture with one.");
    }

    layer_transform_->setEnabled(refusal.isEmpty());

    // **One name for both, on the user's call, and this reverses an argument
    // made right here.** The button used to read "Place this picture" on an
    // import, on the grounds that one word for two things whose costs differ by
    // everything would be the button telling the same lie its tooltip exists to
    // prevent.
    //
    // What that missed is which way the surprise runs. The tooltip is there to
    // stop somebody being caught out by something expensive and irreversible --
    // and on an import this is the opposite in every respect: nothing is
    // written, it undoes by being adjusted again, and it never costs the
    // picture any quality however often you do it. Somebody who presses a
    // button expecting every drawing to be rewritten, and finds that nothing
    // was, has not been lied to in any way that can hurt them. Two names for
    // one gesture is a distinction the program was asking people to learn for
    // its own benefit rather than for theirs.
    //
    // **The tooltip still differs, and that is not a hedge.** It is where the
    // good news goes, and the alternative is not silence but a falsehood: the
    // ordinary tooltip says "it is baked on Apply: every drawing in the layer
    // is written", which on an import is simply untrue.
    const bool placing = layer && layer->kind == LayerKind::Reference;
    layer_transform_->setText(QStringLiteral("Transform layer through time"));
    layer_transform_->setToolTip(
        !refusal.isEmpty()
            ? QStringLiteral("Cannot transform this layer through time: %1").arg(refusal)
        : placing
            // Worth saying because it is the opposite of every other warning on
            // this button, and because nobody would assume it: a placement is
            // stored rather than written, so it can be adjusted again and again
            // and the picture is still one resample away from the file.
            ? QStringLiteral("Move, turn or scale the imported picture.\n\n"
                             "Nothing is written: where it sits is stored, and the picture is\n"
                             "made from the file again each time you move it. Adjusting it\n"
                             "never costs it any quality, however often you do it.")
            : QStringLiteral("Move, turn or scale every drawing of this layer at once.\n\n"
                             "It is baked on Apply: every drawing in the layer is written,\n"
                             "in one undo step. A nudge or a flip is exact; turning or\n"
                             "scaling resamples every drawing, once."));

    // **And the Transform tool itself, which a sequence import has nothing for.**
    //
    // A still routes to the placement and the tool is the right door for it. A
    // sequence is several pictures, so "move this drawing" has nothing it can
    // honestly do -- the way in is to convert it to drawings, which is what the
    // tooltip says. Greyed rather than left to refuse when pressed, because a
    // tool that looks available and then bounces you back to the brush is a
    // tool you try twice.
    if (transform_action_) {
        const bool sequence = layer && layer->kind == LayerKind::Reference &&
                              layer->reference_sources.size() > 1;
        // Left checked while disabled it would sit there looking chosen and
        // doing nothing, so the brush takes over -- the same thing
        // chooseTransformTool does when the canvas refuses.
        if (sequence && transform_action_->isChecked()) {
            brush_action_->setChecked(true);
            canvas_->setTool(CanvasWidget::Tool::Brush);
            syncToolSettings();
        }
        transform_action_->setEnabled(!sequence);
        setKeyedTipText(
            transform_action_,
            sequence ? QStringLiteral("Cannot transform an imported sequence")
                     : QStringLiteral("Move, turn or resize this drawing on the layer you are on"),
            // Points at the panel button by the name it now has. The two are
            // not the same gesture and the tooltip has to say which is which:
            // this tool moves one drawing, and a sequence has no one drawing to
            // move, where the panel's control moves the whole layer together --
            // which is exactly what a sequence wants.
            sequence ? QStringLiteral(
                           "This tool moves one drawing, and an imported sequence is several.\n\n"
                           "To move, turn or scale the whole sequence at once, use \"Transform\n"
                           "layer through time\" in the layer panel -- which writes nothing on\n"
                           "an import, so it costs the picture no quality however often you\n"
                           "do it. To paint on it, convert it to drawings first.")
                     : QStringLiteral(
                           "With nothing selected it takes the whole drawing.\n"
                           "%1 applies, %2 cancels, and the nudge keys move it a pixel at a "
                           "time."));
    }

    // Remove layer, which had the same fault the button above was built to
    // avoid and had it from the start: `removeCurrentLayer` returns without a
    // word on a track with one layer, so the button was dead and nothing said
    // why. Every fresh track has one layer and so does every import, which is
    // what made it easy to meet.
    //
    // The guard itself is right and is not what changes here -- a track with no
    // layers has nowhere to draw, and the answer to that is not to allow it but
    // to say so. Greyed out rather than removed, for the reason above: a
    // control that comes and goes is one nobody can find twice.
    if (!layer_remove_) return;
    const Track* track = doc_.scene().findTrack(track_);
    const bool last_one = !track || track->layers.size() <= 1;
    layer_remove_->setEnabled(!last_one);
    layer_remove_->setToolTip(
        last_one ? QStringLiteral("Cannot remove this layer: it is the only one on this track,\n"
                                  "and a track with no layers has nowhere to draw.\n\n"
                                  "Add another layer first, or delete the whole track from\n"
                                  "the Track menu.")
                 : QStringLiteral("Remove this layer and every drawing on it, from this track."));
}

// The layer panel's button. The canvas owns the gesture; this is the door.
void MainWindow::transformLayerThroughTime() {
    // An imported picture is placed rather than baked, and what the gesture
    // floats is the file at 1:1 -- the stored placement is absolute, so the
    // pixels under it must be unplaced. Decoded here because this is the side
    // of the seam that knows where `imports/` is; the canvas is handed pixels
    // and never a path.
    TileGrid unplaced;
    if (const Layer* layer = currentLayer();
        layer && layer->kind == LayerKind::Reference) {
        QString trouble;
        // The frame in front of you, because that is the picture the box is
        // drawn round. A placement is a property of the whole layer, so which
        // frame is floated changes nothing about what Apply stores -- but a box
        // fitted to a different frame than the one on screen would be a box
        // round nothing.
        int frame = Image::kNoSourceFrame;
        if (const Track* track = doc_.scene().findTrack(track_)) {
            if (const Image* image = track->findImage(canvas_->currentImage())) {
                frame = image->sourceFrameFor(layer->id);
            }
        }
        unplaced = importAtOneToOne(*layer, frame, &trouble);
        if (unplaced.empty()) {
            statusBar()->showMessage(
                QStringLiteral("Cannot place this picture: %1")
                    .arg(trouble.isEmpty() ? QStringLiteral("it has nothing in it") : trouble),
                6000);
            return;
        }
    }

    const CanvasWidget::Refusal why = canvas_->beginLayerTransform(std::move(unplaced));
    if (why != CanvasWidget::Refusal::None) {
        sayCannot(statusBar(), "transform this layer through time", why);
        return;
    }
    // The pen has to go back to the canvas, or the first thing the box hears is
    // nothing at all. The panel's buttons are NoFocus for this reason and the
    // canvas is where the keyboard lives; this is the one that also matters for
    // the pen, because the gesture that follows is entirely on the canvas.
    canvas_->setFocus();
}

// The way back from an import: the whole layer, converted to drawings.
//
// **Everything before the conversion is this function saying what it will do**,
// and the reason it is this much text is that all of it is either irreversible
// or expensive. What is said, in order: how many drawings, what they weigh,
// what happens to the quality, and what happens to the undo history. See
// docs/importing.md, "convert to drawings", which is where each was argued.
void MainWindow::convertLayerToDrawings() { askToConvertLayer(false); }

void MainWindow::askToConvertLayer(const bool because_a_stroke_was_refused) {
    stopPlayback();

    const Layer* layer = currentLayer();
    if (!layer || layer->kind != LayerKind::Reference) {
        statusBar()->showMessage(
            QStringLiteral("Cannot convert to drawings: this is not an imported picture"), 6000);
        return;
    }
    const LayerId layer_id = layer->id;
    const QString name = QString::fromStdString(layer->name);
    // Taken now rather than read off `layer` further down, because two of the
    // ways out below run an event loop and a pointer into the layer vector is
    // good for exactly as long as nothing edits the document.
    const bool one_file = layer->reference_sources.size() == 1;

    const ConversionCost cost = conversionCostFor(track_, layer_id);
    if (cost.drawings == 0) {
        statusBar()->showMessage(
            QStringLiteral("Cannot convert to drawings: this import shows nothing on any drawing"),
            6000);
        return;
    }

    // **Sometimes it refuses, and that is not a gap to be closed later.** The
    // case this serves is scanned line art -- tens of drawings, which is what
    // colouring an import means -- and it cannot serve a two-hundred-frame
    // video, because nothing can: those pixels do not fit in memory and
    // colouring them was never the point. Said before the work rather than
    // discovered partway through it, which is the difference between a number
    // somebody can act on and a window that stops.
    //
    // The same ceiling one drawing's commit is measured against, and it is a
    // ceiling rather than a bound on the growth -- `commitFitsInBudget`'s layer
    // form measures against what the layer already holds, and a reference layer
    // holds nothing at all, so that form would answer about zero.
    if (cost.tiles > kCommitTileBudget) {
        QMessageBox::warning(
            this, QStringLiteral("Convert to drawings"),
            QStringLiteral(
                "This layer is too long to convert.\n\n"
                "%1 is %2 drawings, about %3 GB of pixels, and converting writes all of them "
                "at once. The most that can be written in one go is about %4 GB.\n\n"
                "Two things make it fit: importing a shorter range of the source, and "
                "importing at half size, which is a quarter of the pixels.")
                .arg(name)
                .arg(cost.drawings)
                .arg(static_cast<double>(cost.tiles) / 8192.0, 0, 'f', 1)
                .arg(static_cast<double>(kCommitTileBudget) / 8192.0, 0, 'f', 1));
        return;
    }

    // Arriving from a refused stroke, this says why it turned up -- and it is a
    // line on the front of the recap rather than a question of its own, because
    // two dialogs asking the same thing is the program not hearing the first
    // answer. Reported.
    QString recap;
    if (because_a_stroke_was_refused) {
        recap = QStringLiteral("%1 is an imported picture, shown from its files rather than "
                               "drawn, so there is nothing here to paint on.\n\n")
                    .arg(name);
    }

    // A tile is 128x128 RGBA half = exactly 128 KB, so tiles/8 is megabytes.
    const double megabytes = static_cast<double>(cost.tiles) / 8.0;
    recap +=
        QStringLiteral("Convert %1 to drawings?\n\n"
                       "%2 %3 will be written, about %4 MB of pixels. It can then be painted "
                       "on, erased, and used as the line art a colour layer cuts against.\n\n")
            .arg(name)
            .arg(cost.drawings)
            .arg(cost.drawings == 1 ? QStringLiteral("drawing") : QStringLiteral("drawings"))
            .arg(megabytes, 0, 'f', megabytes < 10.0 ? 1 : 0);

    // **What is lost is the future, and not this step.** The conversion writes
    // the grid that is already on screen, so any resampling a placement asked
    // for happened when the picture was derived and happens once. The user's
    // instinct was to say whether this is lossless; that is the right thing to
    // key on and the wrong tense to say it in.
    //
    // And it must not claim more than that. A JPEG or a video frame was lossy
    // before it reached this program, so "every pixel is kept" is a statement
    // about this step and never about the artwork.
    recap += cost.resampled
                 ? QStringLiteral(
                       "The drawings will hold exactly what you see now. Placing it again "
                       "afterwards would resample what has already been resampled -- so if you "
                       "have not placed it yet, place it first.\n\n")
                 : QStringLiteral("Every pixel of the files is kept exactly as it is.\n\n");

    if (cost.unreadable > 0) {
        // Said rather than refused, which is the house rule, and said here
        // rather than afterwards because afterwards is one undo too late.
        recap += QStringLiteral("%1 of the frames cannot be read and will convert to empty "
                                "drawings.\n\n")
                     .arg(cost.unreadable);
    }

    // **This paragraph said the wrong thing first, and what it says now was
    // measured.** docs/importing.md predicted that a conversion "will clear the
    // rest of the undo history", by analogy with a layer bake, and this recap
    // was written to warn about that. It does not: a bake displaces every tile
    // of every drawing and the journal holds all of them, where a conversion
    // writes into cels that did not exist and so displaces nothing. The history
    // is charged nothing at all -- pinned in test_transform, because a wrong
    // sentence in a dialog is worse than no sentence.
    //
    // What is true, and is the thing worth saying instead: the files stay in
    // the project, so this is reversible in the strong sense and not only in
    // the undo-stack sense. That was the user's call and it took a change to
    // the save -- which used to carry forward only the files something still
    // named, and would therefore have taken the scan out of the project the
    // next time it wrote. See ProjectIO::save.
    // Keyed on the number of *files* and not on the number of drawings, which
    // are not the same number: a duplicated drawing is two drawings showing one
    // file. Small, and it is the sort of small that reads as the program not
    // knowing what it is talking about.
    recap += QStringLiteral(
        "It can be undone, and it costs the undo history almost nothing. The imported %1 "
        "in the project folder %2 kept either way, so nothing is thrown away.")
                 .arg(one_file ? QStringLiteral("file") : QStringLiteral("files"),
                      one_file ? QStringLiteral("is") : QStringLiteral("are"));

    if (QMessageBox::question(this, QStringLiteral("Convert to drawings"), recap,
                              QMessageBox::Ok | QMessageBox::Cancel,
                              QMessageBox::Ok) != QMessageBox::Ok) {
        return;
    }

    QString trouble;
    if (!convertReferenceLayerFrom(track_, layer_id, &trouble)) {
        QMessageBox::warning(this, QStringLiteral("Convert to drawings"),
                             QStringLiteral("%1 was not converted: %2").arg(name, trouble));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Converted %1 to %2 %3 -- it can be painted on now")
                                 .arg(name)
                                 .arg(cost.drawings)
                                 .arg(cost.drawings == 1 ? QStringLiteral("drawing")
                                                         : QStringLiteral("drawings")),
                             8000);
}

// Somebody tried to do something an imported picture has no pixels for. The
// refusal is where the question actually gets asked, so this is where the offer
// is made.
//
// **Four gestures reach here**: the brush, and copy, cut and paste. All of them
// want a cel and an import has none, so for all of them the honest answer is
// not "no" but "not until it is converted" -- and a program that knows the way
// forward should offer it rather than name it. There is no button for this at
// all any more, on the user's call: converting is not something anybody sets
// out to do, it is what you find out you need at exactly these four moments.
//
// **Queued rather than raised from inside the press**, and that is not caution
// about re-entrancy alone. `tabletEvent` ignores every event while a modal
// dialog is up, which is right -- the dialog has a better claim on the pen --
// but the pen *release* is one of those events, and a gesture that loses its
// release is the trap docs/handover.md puts first among the traps. Nothing is
// open here, the stroke having been refused before it began, so this is the
// cheap end of that rule rather than an instance of it; the queue is what keeps
// it the cheap end.
void MainWindow::offerToConvertRefusedLayer() {
    // Pen-downs arrive faster than a dialog opens, and a hand resting on the
    // canvas is a stream of them. One at a time, and asking again after a
    // Cancel is right -- somebody who tries to draw a second time has asked the
    // question a second time.
    if (offering_conversion_) return;

    const Layer* layer = currentLayer();
    if (!layer || layer->kind != LayerKind::Reference) return;

    offering_conversion_ = true;
    QTimer::singleShot(0, this, [this] {
        // Asked again on the way in: the queue means a frame has passed, and a
        // layer that is no longer an import -- converted from the panel in the
        // meantime, or undone away -- must not be offered a conversion.
        const Layer* still = currentLayer();
        const bool offer = still && still->kind == LayerKind::Reference;
        // Cleared before the dialog rather than after it, because that dialog
        // is modal: leaving this set across it would leave it set for as long
        // as somebody reads the recap, and a second pen-down on a picture they
        // have decided against would then be ignored rather than answered.
        offering_conversion_ = false;
        // **Straight to the recap, which is the only question there is.** This
        // used to ask "convert this layer?" here and then hand over to a recap
        // that asked the same thing again with the numbers on it -- two dialogs
        // for one decision, reported as exactly that. What the refusal is
        // entitled to add is the sentence saying why it turned up, and that is
        // now the first line of the recap rather than a dialog of its own.
        if (offer) askToConvertLayer(true);
    });
}

// What the panel's rename editor typed, applied to the document.
//
// refreshLayerFlags and not rebuildLayerList: the editor is still closing when
// this runs, and clearing the tree out from under it deletes the very row the
// delegate is about to finish with. The label is all that changed, which is
// exactly what refreshLayerFlags is for.
void MainWindow::renameLayer(int row, const QString& name) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || row < 0 || static_cast<std::size_t>(row) >= track->layers.size()) return;

    const Layer& layer = track->layers[static_cast<std::size_t>(row)];
    const QString trimmed = name.trimmed();
    // A layer with no name has no row label and no folder of its own in an
    // export, so the old one is kept -- the same answer renaming a track gives.
    if (trimmed.isEmpty() || trimmed.toStdString() == layer.name) return;

    Layer updated = layer;
    updated.name = trimmed.toStdString();
    doc_.updateLayer(track_, updated.id, updated);

    refreshLayerFlags();
    syncStatus();
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
    // Hiding a layer is one of the two things that takes the layer transform
    // away, and it is the only one of them the panel can do -- so the button
    // has to be asked again here and not only when the selection moves.
    syncLayerButtons();
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
    // Cancelled rather than committed: emptying the layer throws these pixels
    // away, so baking them first would be one resample spent on nothing.
    canvas_->cancelTransform();
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
    showCurrentLayer();
    canvas_->refreshAll();
}

// The layers you can see become the barrier. Cutting against the rough as well
// as the clean closes gaps that leak from either alone, so several sources is
// the default rather than something to ask for -- but a layer you have hidden
// is one you have said you are not working against, and taking it anyway meant
// a fill stopping at a line nobody could see.
//
// Only raster layers: another colour layer is a flat, and a flat has no edges
// to cut along. Taken once, at creation, and editable afterwards -- the set is
// a property of the layer and not a reading of what happens to be on screen
// when the solver runs, or toggling a rough for a moment would silently
// recolour the shot.
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
            if (layer.kind == LayerKind::Raster && layer.visible) {
                settings.ctg_sources.push_back(layer.id);
            }
        }
        doc_.updateLayer(track_, created, settings);
    }
    doc_.endCommand();

    canvas_->setActiveLayer(created);
    rebuildLayerList();
    // A colour layer goes to the bottom of the stack, which in a track of any
    // size is off the end of the panel -- so this is the one that has to be
    // scrolled to, and issue #12 is what happens when it is not. See
    // showCurrentLayer.
    showCurrentLayer();
    timeline_widget_->refresh();  // as above: the cards say what the colour is doing
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

// Deleting the layer a transform is holding would leave the float pointing at
// something that is not there. Committed first, so the drawing that was picked
// up is part of what the deletion undoes.
void MainWindow::removeCurrentLayer() {
    canvas_->settleTransform();
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->layers.size() <= 1) return;  // never leave nothing to draw on
    Layer* layer = currentLayer();
    if (!layer) return;

    doc_.removeLayer(track_, layer->id);
    const Track* after = doc_.scene().findTrack(track_);
    if (after && !after->layers.empty()) canvas_->setActiveLayer(after->layers.front().id);
    rebuildLayerList();
    // The timeline draws what the colour layers are doing -- a bar under a
    // drawing whose colour was carried there -- so removing one changes it.
    // Nothing else was going to tell it: a card is painted from the layer list
    // and a layer list that has lost a layer looks the same until it repaints.
    timeline_widget_->refresh();
    canvas_->refreshAll();
}

// A row dropped somewhere else in the list.
//
// The selection is not put back by index afterwards, which is what the buttons
// did: the rebuild picks the row by `canvas_->activeLayer()`, so the panel
// follows the layer you were holding rather than the position it went to. The
// two agree here and stop agreeing the moment anything else moves a layer.
void MainWindow::moveLayerTo(int from, int to) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;
    const int layers = static_cast<int>(track->layers.size());
    if (from < 0 || to < 0 || from >= layers || to >= layers || from == to) return;

    doc_.moveLayer(track_, static_cast<std::size_t>(from), static_cast<std::size_t>(to));
    rebuildLayerList();
    showCurrentLayer();
    canvas_->refreshAll();
}

// Where issue #12 was: the list scrolls itself when the current row changes, and
// it scrolls against the panel as it stands at that moment.
//
// Selecting a colour layer *changes* that panel. The Colour layer box appears
// under the list and takes about half its height with it, so a scroll worked out
// for a list of 32 rows was applied to one of 17 -- and the layer you had just
// made, which on a colour layer is the bottom one, was left below the fold in
// any track with enough layers to need scrolling at all.
//
// activate() is what makes this one step: it lays the panel out now, so the
// scroll below is measured against the height the list actually ends up with
// rather than the one it is about to stop having.
void MainWindow::showCurrentLayer() {
    if (!layer_list_) return;
    if (QWidget* panel = layer_list_->parentWidget()) {
        if (QLayout* laid_out = panel->layout()) laid_out->activate();
    }
    if (QTreeWidgetItem* item = layer_list_->currentItem()) {
        layer_list_->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
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

bool MainWindow::colourIsTransparent() const {
    return isTransparentScribble(Rgba{colour_r_, colour_g_, colour_b_, 1.0f});
}

void MainWindow::chooseSolidColour() { applyColour(solid_r_, solid_g_, solid_b_); }

void MainWindow::chooseTransparent() {
    applyColour(kTransparentScribble.r, kTransparentScribble.g, kTransparentScribble.b);
}

void MainWindow::applyColour(float r, float g, float b) {
    colour_r_ = r;
    colour_g_ = g;
    colour_b_ = b;
    canvas_->setBrushColour(r, g, b);

    // Remembered so that leaving a colour layer has a colour to fall back to.
    // The eyedropper cannot produce the transparent label -- it reads what was
    // composited, and what was composited is a colour or is nothing at all --
    // so this is only ever skipped for a deliberate click on the swatch.
    if (!colourIsTransparent()) {
        solid_r_ = r;
        solid_g_ = g;
        solid_b_ = b;
    }

    syncColourControls();
}

namespace {

// The accent a selected swatch is rimmed with, and the edge an unselected one
// keeps so that the two are the same size.
constexpr char kAccent[] = "#1fb6a6";
constexpr char kSwatchEdge[] = "#6a6a6e";

// The slash over nothing that every program uses for "no colour". A patch
// cannot show the absence of colour by being some colour.
constexpr char kNoneFill[] =
    "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
    "stop:0 #ffffff, stop:0.42 #ffffff, stop:0.44 #d02020,"
    "stop:0.56 #d02020, stop:0.58 #ffffff, stop:1 #ffffff);";

// The same mark, muted, for a layer where it means nothing. Greyed rather than
// hidden: it is still worth knowing the position is there.
constexpr char kNoneFillDisabled[] =
    "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
    "stop:0 #7a7a7e, stop:0.42 #7a7a7e, stop:0.44 #8f6a6a,"
    "stop:0.56 #8f6a6a, stop:0.58 #7a7a7e, stop:1 #7a7a7e);";

// Named by type on purpose. A stylesheet with no selector applies to the widget
// *and* to everything it owns, and a tooltip counts -- so a swatch styled
// `background:#c00` handed its own colour to its tooltip, and the slashed one
// drew a red streak straight through the text of it. Reported, and it was never
// intended. `QPushButton { ... }` keeps it on the button.
QString swatchStyle(const QString& fill, bool selected) {
    return QStringLiteral("QPushButton { %1 border:2px solid %2; }")
        .arg(fill, QLatin1String(selected ? kAccent : kSwatchEdge));
}

}  // namespace

// The switch, in both its positions.
//
// The left half always shows the last real colour rather than whatever is in
// hand, so the control has two visible positions instead of one position and a
// blank -- with the colour hidden while transparency was chosen there would be
// nothing to switch back to, only a button to press and hope.
//
// The right half only means something where a mark is a label. Off a colour
// layer it is greyed, and if it was the colour in hand on the way out the last
// real one comes back, so "transparency selected on a raster layer" is a state
// that cannot be arrived at rather than one caught at the moment it would do
// damage.
void MainWindow::syncColourControls() {
    if (!colour_swatch_ || !transparent_swatch_) return;

    const Track* track = doc_.scene().findTrack(track_);
    const Layer* layer = track ? track->findLayer(canvas_->activeLayer()) : nullptr;
    const bool on_colour_layer = layer && layer->kind == LayerKind::Ctg;
    const bool none_chosen = colourIsTransparent();

    const auto shown = [](float linear) {
        return static_cast<qreal>(std::clamp(linearToSrgb(linear), 0.0f, 1.0f));
    };
    const QColor solid = QColor::fromRgbF(shown(solid_r_), shown(solid_g_), shown(solid_b_));
    const QString fill = QStringLiteral("background:%1;").arg(solid.name());
    colour_swatch_->setStyleSheet(swatchStyle(fill, !none_chosen));

    transparent_swatch_->setEnabled(on_colour_layer);
    transparent_swatch_->setStyleSheet(
        swatchStyle(QLatin1String(on_colour_layer ? kNoneFill : kNoneFillDisabled),
                    on_colour_layer && none_chosen));
}

void MainWindow::setBrushRadius(double radius) {
    canvas_->brushSettings().radius = static_cast<float>(radius);
}

void MainWindow::nudgeBrushRadius(double factor) {
    if (!radius_) return;
    radius_->setValue(radius_->value() * factor);
}

// Undo and Redo are not disabled during a transform, they are redefined: Ctrl+Z
// cancels it. Undoing the stroke before it would be answering a question nobody
// asked, and there is no undo entry for the transform itself to reach -- nothing
// has been written.
void MainWindow::undo() {
    stopPlayback();
    if (canvas_->transformIsLive()) {
        canvas_->cancelTransform();
        return;
    }
    if (!doc_.undo()) return;
    refreshEverything();
}

void MainWindow::redo() {
    stopPlayback();
    if (canvas_->transformIsLive()) return;
    if (!doc_.redo()) return;
    refreshEverything();
}
