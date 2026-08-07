// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_controller.h"

#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <algorithm>

#include "canvas_view.h"
#include "export_sequence.h"
#include "layers_model.h"
#include "tracks_model.h"
#include "scene_settings_model.h"
#include "scribble.h"
#include "timeline_model.h"
#include "color.h"

using namespace animage;

namespace {

// An autosave costs about a tenth of a second on a shot of a hundred drawings
// now that it only writes the cels that moved, so the interval is chosen for
// how much work an animator is willing to lose rather than for what it costs.
constexpr int kAutosaveIntervalMs = 2 * 60 * 1000;

// A stroke and a playback pass are both short. Rather than skip this autosave
// and wait the whole interval again, look back in a moment.
constexpr int kAutosaveRetryMs = 5 * 1000;

}  // namespace

AppController::AppController(QObject* parent) : QObject(parent) {
    layers_model_ = new LayersModel(this);
    tracks_model_ = new TracksModel(this);
    timeline_model_ = new TimelineModel(this);
    ctg_sources_model_ = new CtgSourcesModel(this);
    layers_model_->setDocument(&doc_);
    tracks_model_->setDocument(&doc_);
    timeline_model_->setDocument(&doc_);
    ctg_sources_model_->setDocument(&doc_);

    buildInitialDocument();

    playback_timer_ = new QTimer(this);
    playback_timer_->setTimerType(Qt::PreciseTimer);
    connect(playback_timer_, &QTimer::timeout, this, &AppController::onPlaybackTick);

    // Runs from the start even though an untitled document has nowhere to be
    // written: the tick is what decides, and it is the only place the rules
    // about when a save is allowed live.
    autosave_timer_ = new QTimer(this);
    connect(autosave_timer_, &QTimer::timeout, this, &AppController::onAutosaveTick);
    autosave_timer_->start(kAutosaveIntervalMs);

    updateTitle();
    syncStatus();
}

AppController::~AppController() = default;

void AppController::buildInitialDocument() {
    doc_ = Document();
    track_ = doc_.addTrack("track 1");
    const LayerId first = doc_.addLayer(track_, "layer 1");
    doc_.insertImage(track_, 0);
    doc_.clearHistory();  // an empty scene is the starting point, not an edit

    layers_model_->setTrack(track_);
    tracks_model_->setCurrentTrack(track_);
    timeline_model_->setTrack(track_);
    ctg_sources_model_->setTrack(track_);
    current_slot_ = 0;
    project_folder_.clear();
    save_state_ = ProjectIO::SaveState();
    saved_token_ = doc_.historyToken();
    autosave_token_ = saved_token_;
    (void)first;
}

void AppController::attachCanvas(CanvasView* canvas) {
    canvas_ = canvas;
    canvas_->setDocument(&doc_);
    canvas_->setTrack(track_);
    canvas_->setFrame(current_slot_);
    if (const Track* track = doc_.scene().findTrack(track_);
        track && !track->layers.empty()) {
        canvas_->setActiveLayer(track->layers.front().id);
    }

    connect(canvas_, &CanvasView::brushSizeChanged, this, [this](double) {
        Q_EMIT brushChanged();
    });
    connect(canvas_, &CanvasView::colourPicked, this, &AppController::applyColour);
    connect(canvas_, &CanvasView::viewChanged, this, &AppController::syncStatus);
    connect(canvas_, &CanvasView::documentChanged, this, &AppController::refreshEverything);
    // A fill landed. Queued, because a solve installed while the canvas is
    // painting itself would rebuild the layer panel underneath the paint.
    connect(canvas_, &CanvasView::colourChanged, this,
            [this] {
                timeline_model_->refresh();
                refreshLayerFlags();
                syncStatus();
            },
            Qt::QueuedConnection);

    refreshEverything();
    Q_EMIT colourPendingChanged();
}

// --- files ---------------------------------------------------------------

QString AppController::title() const {
    const QString name = project_folder_.isEmpty()
                             ? QStringLiteral("Untitled")
                             : QFileInfo(project_folder_).fileName();
    const bool changed = doc_.historyToken() != saved_token_;
    return QStringLiteral("%1%2 - Animage").arg(name, changed ? QStringLiteral("*")
                                                              : QString());
}

QString AppController::projectName() const {
    return project_folder_.isEmpty() ? QStringLiteral("Untitled")
                                     : QFileInfo(project_folder_).fileName();
}

void AppController::updateTitle() { Q_EMIT stateChanged(); }

// The one way a document gets left: New, Open and Close all come through here.
// The action is recorded before the user is asked anything, and perform() is
// the only place an action actually happens, so Save/Discard/Cancel can never
// run the wrong one and a failed save can never invent one.
void AppController::requestLeave(PendingAction action) {
    stopPlayback();
    if (!isDirty()) {
        perform(action);
        return;
    }
    pending_action_ = action;
    Q_EMIT leaveDecisionRequested(
        QStringLiteral("This project has unsaved changes.\n\n"
                       "Save them, discard them, or go back."));
}

void AppController::perform(PendingAction action) {
    switch (action) {
        case PendingAction::New:
            buildInitialDocument();
            afterProjectLoaded();
            // "Like launching the application, plus the Scene settings dialog
            // opening": the first question about a new shot is what shape it is.
            Q_EMIT sceneSettingsRequested();
            break;
        case PendingAction::Open:
            Q_EMIT openFolderDialogRequested();
            break;
        case PendingAction::Close:
            Q_EMIT closeRequested();
            break;
        case PendingAction::None:
            break;
    }
}

// The interface asked the question in requestLeave; this is its answer.
void AppController::respondSaveDecision(int decision) {
    const PendingAction action = pending_action_;
    pending_action_ = PendingAction::None;
    switch (decision) {
        case Discard:
            perform(action);
            break;
        case Cancel:
            break;
        case Save: {
            if (project_folder_.isEmpty()) {
                // Never saved anywhere: Save As first, and carry the leave on
                // only once something is actually on disk.
                save_then_action_ = action;
                Q_EMIT saveFileDialogRequested();
            } else if (saveTo(project_folder_)) {
                // Saved in place: the leave carries on. A failed save drops it
                // (the user is told by statusMessage) instead of guessing.
                perform(action);
            }
            break;
        }
        default:
            break;
    }
}

void AppController::newProject() { requestLeave(PendingAction::New); }

void AppController::openProject() { requestLeave(PendingAction::Open); }

void AppController::requestClose() { requestLeave(PendingAction::Close); }

void AppController::acceptOpenLocation(const QUrl& url) { openProjectAt(url.toString()); }

void AppController::acceptSaveLocation(const QUrl& url) { saveTo(url.toString()); }

bool AppController::openProjectAt(const QString& folder_or_url, QString* error) {
    QString folder = QUrl(folder_or_url).isLocalFile()
                          ? QUrl(folder_or_url).toLocalFile()
                          : folder_or_url;
    QFileInfo info(folder);
    if (info.isFile()) {
        folder = info.dir().absolutePath();
    }
    if (!ProjectIO::load(doc_, folder, save_state_, error)) return false;
    project_folder_ = folder;
    afterProjectLoaded();
    return true;
}

void AppController::afterProjectLoaded() {
    const Scene& scene = doc_.scene();
    // Keep current track if it still exists, otherwise fall back to first
    if (!doc_.scene().findTrack(track_)) {
        track_ = scene.tracks.empty() ? kNoId : scene.tracks.front().id;
    }

    layers_model_->setTrack(track_);
    tracks_model_->setCurrentTrack(track_);
    timeline_model_->setTrack(track_);
    if (canvas_) {
        canvas_->setTrack(track_);
        canvas_->setFrame(0);
    }
    current_slot_ = 0;

    const Track* track = doc_.scene().findTrack(track_);
    if (track && !track->layers.empty() && canvas_) {
        canvas_->setActiveLayer(track->layers.front().id);
    }

    rebuildLayerList();
    timeline_model_->refresh();
    if (canvas_) {
        canvas_->refreshAll();
        canvas_->fitToCanvas();
    }

    ctg_sources_model_->setTrack(track_);
    syncColourLayerPanel();
    saved_token_ = doc_.historyToken();
    autosave_token_ = saved_token_;

    syncStatus();
    updateTitle();
    Q_EMIT slotChanged();
}

void AppController::saveProject() {
    // A document that has never been saved has nowhere to go, so Save asks —
    // which is Save As by another name.
    if (project_folder_.isEmpty()) {
        saveProjectAs();
        return;
    }
    saveTo(project_folder_);
}

void AppController::saveProjectAs() {
    stopPlayback();
    Q_EMIT saveFileDialogRequested();
}

bool AppController::saveTo(const QString& folder_or_url) {
    QString folder = QUrl(folder_or_url).isLocalFile()
                          ? QUrl(folder_or_url).toLocalFile()
                          : folder_or_url;
    QFileInfo info(folder);
    if (info.isFile()) {
        folder = info.dir().absolutePath();
    }
    if (!folder.endsWith(QStringLiteral(".animage"), Qt::CaseInsensitive) &&
        !QFileInfo(folder).exists()) {
        folder += QStringLiteral(".animage");
    }
    QString error;
    if (!ProjectIO::save(doc_, folder, save_state_, &error)) {
        Q_EMIT statusMessage(QStringLiteral("Cannot save: %1").arg(error));
        return false;
    }
    project_folder_ = folder;
    // An explicit save is what establishes the clean state. The recovery
    // snapshot the autosave writes is not a save, and never moves saved_token_.
    saved_token_ = doc_.historyToken();
    autosave_token_ = saved_token_;
    updateTitle();
    Q_EMIT statusMessage(QStringLiteral("Saved to %1").arg(folder));

    // A save asked for by the leave handshake carries the leave on with it.
    if (save_then_action_ != PendingAction::None) {
        const PendingAction pending = save_then_action_;
        save_then_action_ = PendingAction::None;
        perform(pending);
    }
    return true;
}

void AppController::exportSequences() {
    stopPlayback();
    Q_EMIT exportDialogRequested();
}

bool AppController::exportSequencesTo(const QString& folder_or_url, bool layers, bool flattened,
                                      QString* error) {
    return exportSequencesTo(folder_or_url, layers, flattened, 0, error);
}

bool AppController::exportSequencesTo(const QString& folder_or_url, bool layers, bool flattened,
                                      int format, QString* error) {
    const QString folder = QUrl(folder_or_url).isLocalFile()
                               ? QUrl(folder_or_url).toLocalFile()
                               : folder_or_url;
    export_cancel_ = false;
    exporting::Options options;
    options.folder = folder;
    options.layers = layers;
    options.flattened = flattened;
    options.format = static_cast<exporting::Format>(format);

    const int total = exporting::fileCount(doc_, options);
    const bool ok = exporting::write(
        doc_, options,
        [this, total](int done, int count, const QString&) {
            Q_EMIT exportProgress(done, std::max(count, total));
            // Keeps the progress dialog alive: the export runs on this thread,
            // and nothing would repaint otherwise.
            QCoreApplication::processEvents();
            return !export_cancel_;
        },
        nullptr, error);

    Q_EMIT exportFinished(ok, ok ? QString() : (error ? *error : QString()));
    return ok;
}

void AppController::cancelExport() { export_cancel_ = true; }

void AppController::onAutosaveTick() {
    // An untitled document has nowhere to go. It is not an error and there is
    // nothing to report.
    if (project_folder_.isEmpty()) return;

    // Nothing has moved since the last write -- the last explicit save or the
    // last autosave, whichever is newer. Undoing back to where a save stood
    // counts as unchanged, because it is.
    if (doc_.historyToken() == autosave_token_) return;

    // Never in the middle of a stroke or a playback pass: a save is a tenth of
    // a second, which is invisible between two strokes and a stutter inside one.
    if ((canvas_ && canvas_->isStroking()) || playback_timer_->isActive()) {
        autosave_timer_->start(kAutosaveRetryMs);
        return;
    }

    QString error;
    if (!ProjectIO::save(doc_, project_folder_, save_state_, &error)) {
        // Deliberately not a dialog. A failing autosave would otherwise
        // interrupt drawing every two minutes.
        Q_EMIT statusMessage(QStringLiteral("Autosave failed: %1").arg(error));
    } else {
        // A recovery snapshot, not a save: the title keeps its star and the
        // dirty flag stays up until the user saves explicitly. `autosave_token_`
        // (not `saved_token_`) is what advances.
        autosave_token_ = doc_.historyToken();
        Q_EMIT statusMessage(QStringLiteral("Recovery snapshot written"));
    }
    autosave_timer_->start(kAutosaveIntervalMs);  // back to the ordinary cadence
}

void AppController::refreshEverything() {
    // If the current track was removed (undo, load), fall back to first
    if (!doc_.scene().findTrack(track_) && !doc_.scene().tracks.empty()) {
        track_ = doc_.scene().tracks.front().id;
        layers_model_->setTrack(track_);
        tracks_model_->setCurrentTrack(track_);
        timeline_model_->setTrack(track_);
        if (canvas_) {
            canvas_->setTrack(track_);
            const Track* t = doc_.scene().findTrack(track_);
            if (t && !t->layers.empty()) canvas_->setActiveLayer(t->layers.front().id);
        }
    }
    tracks_model_->refresh();
    timeline_model_->refresh();
    layers_model_->refresh();
    if (canvas_) canvas_->setFrame(current_slot_);
    rebuildLayerList();
    syncStatus();
    Q_EMIT trackChanged();
}

void AppController::syncStatus() {
    updateTitle();

    const Track* track = doc_.scene().findTrack(track_);
    if (!track) {
        status_text_.clear();
        Q_EMIT stateChanged();
        return;
    }

    const std::size_t slot = canvas_ ? canvas_->frame() : current_slot_;

    // The status line tells the user where they are in the animation and
    // nothing else. Counts of tiles, drawings, layers and undo steps are
    // diagnostics, not navigation, so they live nowhere in the interface.
    const QString colouring =
        colourPending() ? QStringLiteral("   colouring...") : QString();

    status_text_ = QStringLiteral("Frame %1 of %2%3")
                       .arg(slot + 1)
                       .arg(track->frameCount())
                       .arg(colouring);

    zoom_percent_ = canvas_ ? static_cast<int>(std::lround(canvas_->zoom() * 100.0)) : 100;
    Q_EMIT stateChanged();
}

// --- time ----------------------------------------------------------------

int AppController::frameCount() const {
    return static_cast<int>(doc_.scene().frameCount());
}

int AppController::currentTrackIndex() const {
    const auto& tracks = doc_.scene().tracks;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].id == track_) return static_cast<int>(i);
    }
    return -1;
}

bool AppController::overwrite() const {
    const Track* track = doc_.scene().findTrack(track_);
    return track ? track->overwrite_drawings : true;
}

int AppController::trackEnd() const {
    const Track* track = doc_.scene().findTrack(track_);
    return track ? static_cast<int>(track->end) : 0;
}

int AppController::sceneLength() const {
    return doc_.scene().length;
}

int AppController::drawingCount() const {
    const Track* track = doc_.scene().findTrack(track_);
    return track ? static_cast<int>(track->images.size()) : 0;
}

int AppController::layerCount() const {
    const Track* track = doc_.scene().findTrack(track_);
    return track ? static_cast<int>(track->layers.size()) : 0;
}

void AppController::setCurrentSlot(int slot) {
    const std::size_t frames = doc_.scene().frameCount();
    if (frames == 0) return;

    const int clamped = std::min(slot, static_cast<int>(frames) - 1);
    if (clamped < 0) return;
    const std::size_t next = static_cast<std::size_t>(clamped);
    if (next == current_slot_) return;
    current_slot_ = next;

    if (canvas_) canvas_->setFrame(current_slot_);
    // What the panel says about a colour layer is about the drawing you are
    // standing on, so moving changes it even when no fill is rebuilt.
    refreshLayerFlags();
    syncStatus();
    Q_EMIT slotChanged();
}

void AppController::stepFrame(int delta) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->slots.empty()) return;

    const int count = static_cast<int>(track->slots.size());
    int next = static_cast<int>(current_slot_) + delta;
    next = ((next % count) + count) % count;  // wrap, so scrubbing loops
    setCurrentSlot(next);
}

void AppController::stepDrawing(int direction) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->slots.empty()) return;

    const std::vector<ImageId> neighbours =
        track->distinctNeighbours(current_slot_, 1, direction);
    if (neighbours.empty()) return;

    auto it = std::find(track->slots.begin(), track->slots.end(), neighbours[0]);
    if (it == track->slots.end()) return;
    setCurrentSlot(static_cast<int>(std::distance(track->slots.begin(), it)));
}

// After a drawing means after the whole hold. Landing a new drawing in the
// middle of a ten-frame hold splits it in two, which is never what was meant.
std::size_t AppController::slotAfterCurrentDrawing() const {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->slots.empty()) return 0;
    return track->runBounds(current_slot_).second + 1;
}

void AppController::insertDrawing() {
    stopPlayback();
    const std::size_t at = slotAfterCurrentDrawing();
    doc_.insertImage(track_, at);
    timeline_model_->refresh();
    setCurrentSlot(static_cast<int>(at));
    refreshEverything();
}

void AppController::duplicateDrawing() {
    stopPlayback();
    const std::size_t at = slotAfterCurrentDrawing();
    doc_.duplicateImage(track_, at > 0 ? at - 1 : 0);
    timeline_model_->refresh();
    setCurrentSlot(static_cast<int>(at));
    refreshEverything();
}

void AppController::deleteDrawing() {
    stopPlayback();
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;

    const ImageId image = track->imageAtSlot(current_slot_);
    if (image == kNoId) return;
    if (track->exposureOf(image) >= track->slots.size()) {
        return;  // it is the only drawing; leave something to draw on
    }

    doc_.removeDrawing(track_, image);
    refreshEverything();
}

int AppController::currentHold() const {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return 1;
    const ImageId image = track->imageAtSlot(current_slot_);
    if (image == kNoId) return 1;
    return static_cast<int>(track->exposureOf(image));
}

void AppController::holdLonger() {
    doc_.extendExposure(track_, current_slot_, 1);
    refreshEverything();
}

void AppController::holdShorter() {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;
    const ImageId image = track->imageAtSlot(current_slot_);
    if (track->exposureOf(image) <= 1) return;  // that would delete the drawing
    doc_.removeSlot(track_, current_slot_);
    refreshEverything();
}

// Set the hold to an exact number of frames, guarded like the step verbs so
// it never shortens a drawing out of existence. The timeline's unified
// temporal strip types a concrete value here; plus/minus go through
// holdLonger/holdShorter one frame at a time.
void AppController::setCurrentHold(int frames) {
    while (currentHold() > frames) holdShorter();
    while (currentHold() < frames) holdLonger();
}

void AppController::setFramerate(int fps) {
    doc_.setFramerate(fps);
    if (playback_timer_->isActive()) {
        playback_clock_.restart();
        playback_start_slot_ = current_slot_;
    }
    syncStatus();
}

void AppController::setCanvasSize(int width, int height) {
    doc_.setCanvasSize(width, height);
    if (canvas_) canvas_->refreshAll();
    syncStatus();
}

// The scene settings dialog previews by writing to the scene directly, around
// the history: choosing a resolution means looking at it, and it is not worth
// an undo entry per number tried on the way. Nothing is recorded until the
// dialog is accepted, and cancelling puts back exactly what was there.
void AppController::previewSceneSettings(int framerate, int width, int height, int length) {
    Scene& scene = doc_.mutableScene();
    scene.framerate = framerate;
    scene.width = width;
    scene.height = height;
    scene.length = length;
    timeline_model_->refresh();
    if (canvas_) canvas_->refreshAll();
    syncStatus();
}

void AppController::restoreSceneSettings(int framerate, int width, int height, int length) {
    Scene& scene = doc_.mutableScene();
    scene.framerate = framerate;
    scene.width = width;
    scene.height = height;
    scene.length = length;
    timeline_model_->refresh();
    if (canvas_) canvas_->refreshAll();
    syncStatus();
}

// Committed as one command: changing both numbers and then changing your mind
// is one undo rather than two.
void AppController::commitSceneSettings(int framerate, int width, int height, int length) {
    doc_.beginCommand("Scene settings");
    doc_.setCanvasSize(width, height);
    doc_.setFramerate(framerate);
    doc_.setSceneLength(length);
    doc_.endCommand();

    // The canvas bounds the colour fills, so they have to be solved again.
    timeline_model_->refresh();
    if (canvas_) canvas_->refreshAll();
    syncStatus();
}

// --- playback ------------------------------------------------------------

void AppController::togglePlayback() {
    if (playing()) {
        stopPlayback();
        return;
    }

    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->slots.size() < 2) return;

    playback_start_slot_ = current_slot_;
    playback_clock_.start();
    if (canvas_) canvas_->setPlaying(true);
    playback_timer_->start(1);
    Q_EMIT playingChanged();
}

void AppController::stopPlayback() {
    if (!playback_timer_ || !playback_timer_->isActive()) return;
    playback_timer_->stop();
    if (canvas_) {
        canvas_->setPlaying(false);
        canvas_->setFrame(current_slot_);
    }
    Q_EMIT playingChanged();
    syncStatus();
}

// Driven by elapsed time rather than by counting ticks. A timer that fires late
// must not make the whole take run slow, which is exactly the thing playback
// exists to let you judge.
void AppController::onPlaybackTick() {
    const std::size_t count = doc_.scene().frameCount();
    if (count == 0) {
        stopPlayback();
        return;
    }

    const int fps = std::max(1, doc_.scene().framerate);
    const qint64 elapsed = playback_clock_.elapsed();
    const qint64 advanced = elapsed * fps / 1000;
    const std::size_t slot =
        (playback_start_slot_ + static_cast<std::size_t>(advanced)) % count;

    if (slot == current_slot_) return;
    setCurrentSlot(static_cast<int>(slot));
}

// --- tools ---------------------------------------------------------------

double AppController::brushRadius() const {
    if (!canvas_) return 6.0;
    return canvas_->brushSettings().radius;
}

bool AppController::pressureOpacity() const {
    if (!canvas_) return true;
    return canvas_->brushSettings().pressure_affects_opacity;
}

QColor AppController::brushColour() const {
    if (colourIsTransparent()) return {};
    return QColor::fromRgbF(linearToSrgb(colour_r_), linearToSrgb(colour_g_),
                            linearToSrgb(colour_b_));
}

QColor AppController::solidColour() const {
    return QColor::fromRgbF(linearToSrgb(solid_r_), linearToSrgb(solid_g_),
                            linearToSrgb(solid_b_));
}

bool AppController::transparentSelected() const { return colourIsTransparent(); }

void AppController::setTool(int tool) {
    const bool erase = tool == Eraser;
    if (erase == erasing_) return;
    erasing_ = erase;
    if (canvas_) canvas_->setEraser(erase);
    Q_EMIT toolChanged();
    Q_EMIT brushChanged();
}

void AppController::setBrushRadius(double radius) {
    if (!canvas_) return;
    canvas_->brushSettings().radius = static_cast<float>(radius);
    Q_EMIT brushChanged();
}

void AppController::nudgeBrushRadius(double factor) {
    if (!canvas_) return;
    setBrushRadius(brushRadius() * factor);
}

void AppController::setPressureOpacity(bool on) {
    if (!canvas_) return;
    canvas_->brushSettings().pressure_affects_opacity = on;
    Q_EMIT brushChanged();
}

// The dialog speaks sRGB; the document works in linear light.
void AppController::chooseBrushColour(const QColor& colour) {
    if (!colour.isValid()) return;
    applyColour(srgbToLinear(static_cast<float>(colour.redF())),
                srgbToLinear(static_cast<float>(colour.greenF())),
                srgbToLinear(static_cast<float>(colour.blueF())));
}

bool AppController::colourIsTransparent() const {
    return isTransparentScribble(Rgba{colour_r_, colour_g_, colour_b_, 1.0f});
}

void AppController::chooseSolidColour() { applyColour(solid_r_, solid_g_, solid_b_); }

void AppController::chooseTransparent() {
    applyColour(kTransparentScribble.r, kTransparentScribble.g, kTransparentScribble.b);
}

void AppController::applyColour(float r, float g, float b) {
    colour_r_ = r;
    colour_g_ = g;
    colour_b_ = b;
    if (canvas_) canvas_->setBrushColour(r, g, b);

    // Remembered so that leaving a colour layer has a colour to fall back to.
    // The eyedropper cannot produce the transparent label, so this is only
    // ever skipped for a deliberate click on the swatch.
    if (!colourIsTransparent()) {
        solid_r_ = r;
        solid_g_ = g;
        solid_b_ = b;
    }

    syncColourControls();
    Q_EMIT brushColourChanged();
}

void AppController::clearCurrentCel() {
    stopPlayback();
    Layer* layer = currentLayer();
    if (!layer) return;
    if (!canvas_) return;
    doc_.clearCel(track_, canvas_->currentImage(), layer->id);
    if (canvas_) canvas_->refreshAll();
    syncStatus();
}

// --- layers --------------------------------------------------------------

animage::Layer* AppController::currentLayer() {
    Track* track = doc_.mutableScene().findTrack(track_);
    if (!track) return nullptr;
    const int index = currentLayerIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= track->layers.size()) return nullptr;
    return &track->layers[static_cast<std::size_t>(index)];
}

const animage::Layer* AppController::currentLayerConst() const {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return nullptr;
    const int index = currentLayerIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= track->layers.size()) return nullptr;
    return &track->layers[static_cast<std::size_t>(index)];
}

int AppController::currentLayerIndex() const {
    if (!canvas_ || !doc_.scene().findTrack(track_)) return -1;
    const LayerId active = canvas_->activeLayer();
    const Track* track = doc_.scene().findTrack(track_);
    for (std::size_t i = 0; i < track->layers.size(); ++i) {
        if (track->layers[i].id == active) return static_cast<int>(i);
    }
    return track->layers.empty() ? -1 : 0;
}

void AppController::rebuildLayerList() {
    layers_model_->refresh();
    onLayerSelected();
    syncStatus();
}

void AppController::selectLayerIndex(int index) {
    if (!canvas_) return;
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || index < 0 || static_cast<std::size_t>(index) >= track->layers.size()) return;

    canvas_->setActiveLayer(track->layers[static_cast<std::size_t>(index)].id);
    onLayerSelected();
    Q_EMIT layerStateChanged();
}

void AppController::onLayerSelected() {
    Layer* layer = currentLayer();
    if (!layer) return;
    if (canvas_) canvas_->setActiveLayer(layer->id);

    // Stepping off a colour layer with transparency in hand puts the last real
    // colour back, rather than leaving a brush loaded with negative light.
    if (layer->kind != LayerKind::Ctg && colourIsTransparent()) {
        applyColour(solid_r_, solid_g_, solid_b_);
    } else {
        syncColourControls();
    }
    syncColourLayerPanel();
    Q_EMIT layerStateChanged();
    Q_EMIT brushColourChanged();
}

int AppController::layerOpacity() const {
    const Layer* layer = currentLayerConst();
    return layer ? static_cast<int>(std::lround(layer->opacity * 100.0f)) : 100;
}

bool AppController::layerShowScribbles() const {
    const Layer* layer = currentLayerConst();
    return layer && layer->show_scribbles;
}

void AppController::setLayerOpacity(int percent) {
    Layer* layer = currentLayer();
    if (!layer) return;
    Layer updated = *layer;
    updated.opacity = static_cast<float>(percent) / 100.0f;
    doc_.updateLayer(track_, updated.id, updated);
    if (canvas_) canvas_->refreshAll();
    Q_EMIT layerStateChanged();
}

void AppController::beginOpacityDrag() { doc_.beginCommand("Layer opacity"); }

void AppController::endOpacityDrag() {
    doc_.endCommand();
    syncStatus();
}

void AppController::setLayerVisible(int index, bool visible) {
    Track* track = doc_.mutableScene().findTrack(track_);
    if (!track || index < 0 || static_cast<std::size_t>(index) >= track->layers.size()) return;

    Layer updated = track->layers[static_cast<std::size_t>(index)];
    updated.visible = visible;
    doc_.updateLayer(track_, updated.id, updated);
    if (canvas_) canvas_->refreshAll();
    rebuildLayerList();
}

void AppController::setLayerShowScribbles(int index, bool show) {
    Track* track = doc_.mutableScene().findTrack(track_);
    if (!track || index < 0 || static_cast<std::size_t>(index) >= track->layers.size()) return;

    Layer updated = track->layers[static_cast<std::size_t>(index)];
    if (updated.kind != LayerKind::Ctg) return;
    updated.show_scribbles = show;
    doc_.updateLayer(track_, updated.id, updated);
    if (canvas_) canvas_->refreshAll();
    Q_EMIT layerStateChanged();
    syncStatus();
}

void AppController::setLayerLocked(int index, bool locked) {
    Track* track = doc_.mutableScene().findTrack(track_);
    if (!track || index < 0 || static_cast<std::size_t>(index) >= track->layers.size()) return;

    Layer updated = track->layers[static_cast<std::size_t>(index)];
    updated.locked = locked;
    doc_.updateLayer(track_, updated.id, updated);
    if (canvas_) canvas_->refreshAll();
    rebuildLayerList();
}

void AppController::setLayerName(int index, const QString& name) {
    Track* track = doc_.mutableScene().findTrack(track_);
    if (!track || index < 0 || static_cast<std::size_t>(index) >= track->layers.size()) return;

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;

    Layer updated = track->layers[static_cast<std::size_t>(index)];
    updated.name = trimmed.toStdString();
    doc_.updateLayer(track_, updated.id, updated);
    rebuildLayerList();
}

// The lowest unused "layer N". Reusing a number after a deletion would give
// two layers the same name, which makes the panel ambiguous.
std::string AppController::nextLayerName() const {
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

std::string AppController::nextColourLayerName() const {
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

void AppController::addLayer() {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;

    const int row = std::max(0, currentLayerIndex());
    const LayerId created =
        doc_.addLayer(track_, nextLayerName(), static_cast<std::size_t>(row));
    if (!canvas_) return;
    canvas_->setActiveLayer(created);
    rebuildLayerList();
    canvas_->refreshAll();
}

// A colour layer is cut against the line art and belongs underneath it, so it
// goes to the bottom of the pile rather than above the selected layer. The
// sources are the visible raster layers, taken once at creation.
void AppController::addColourLayer() {
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

    if (!canvas_) return;
    canvas_->setActiveLayer(created);
    rebuildLayerList();
    timeline_model_->refresh();
    canvas_->refreshAll();
}

void AppController::removeCurrentLayer() {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->layers.size() <= 1) return;  // never leave nothing to draw on
    Layer* layer = currentLayer();
    if (!layer) return;

    doc_.removeLayer(track_, layer->id);
    const Track* after = doc_.scene().findTrack(track_);
    if (after && !after->layers.empty() && canvas_) {
        canvas_->setActiveLayer(after->layers.front().id);
    }
    rebuildLayerList();
    timeline_model_->refresh();
    if (canvas_) canvas_->refreshAll();
}

void AppController::moveCurrentLayer(int delta) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;

    const int from = currentLayerIndex();
    const int to = from + delta;
    if (from < 0 || to < 0 || static_cast<std::size_t>(to) >= track->layers.size()) return;

    doc_.moveLayer(track_, static_cast<std::size_t>(from), static_cast<std::size_t>(to));
    rebuildLayerList();
    selectLayerIndex(to);
    if (canvas_) canvas_->refreshAll();
}

bool AppController::onColourLayer() const {
    const Layer* layer = currentLayerConst();
    return layer && layer->kind == LayerKind::Ctg;
}

// --- the colour-layer box -------------------------------------------------

bool AppController::ctgInherit() const {
    const Layer* layer = currentLayerConst();
    return layer ? layer->ctg_inherit : false;
}

int AppController::ctgDirection() const {
    const Layer* layer = currentLayerConst();
    if (!layer) return 0;
    return layer->ctg_direction == CtgDirection::Backward  ? 1
           : layer->ctg_direction == CtgDirection::Nearest ? 2
                                                           : 0;
}

bool AppController::ctgFollow() const {
    const Layer* layer = currentLayerConst();
    return layer ? layer->ctg_follow_motion : false;
}

void AppController::syncColourLayerPanel() {
    const Layer* layer = currentLayer();
    ctg_sources_model_->refresh(
        (layer && layer->kind == LayerKind::Ctg) ? layer : nullptr);
    Q_EMIT layerStateChanged();
}

void AppController::setCtgSource(int source_index, bool used) {
    Layer* layer = currentLayer();
    if (!layer || layer->kind != LayerKind::Ctg) return;
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || source_index < 0 ||
        static_cast<std::size_t>(source_index) >= track->layers.size()) {
        return;
    }

    Layer updated = *layer;
    const Layer& other = track->layers[static_cast<std::size_t>(source_index)];
    if (other.kind != LayerKind::Raster) return;
    if (used) {
        if (std::find(updated.ctg_sources.begin(), updated.ctg_sources.end(), other.id) ==
            updated.ctg_sources.end()) {
            updated.ctg_sources.push_back(other.id);
        }
    } else {
        updated.ctg_sources.erase(
            std::remove(updated.ctg_sources.begin(), updated.ctg_sources.end(), other.id),
            updated.ctg_sources.end());
    }
    doc_.updateLayer(track_, updated.id, updated);

    // The barrier moved, so every fill built from it is wrong. They are keyed on
    // the source cels' revisions and those have not moved, so nothing would
    // notice on its own.
    doc_.ctgCache().clear();
    if (canvas_) canvas_->refreshAll();
    // Keep the sources model in step so the tick and the
    // "nothing ticked" warning update without waiting for a layer
    // selection change.
    if (const Layer* cur = currentLayerConst();
        cur && cur->kind == LayerKind::Ctg) {
        ctg_sources_model_->refresh(cur);
    }
    refreshLayerFlags();
    syncStatus();
}

void AppController::setCtgInherit(bool on) {
    Layer* layer = currentLayer();
    if (!layer || layer->kind != LayerKind::Ctg) return;
    Layer updated = *layer;
    updated.ctg_inherit = on;
    doc_.updateLayer(track_, updated.id, updated);

    doc_.ctgCache().clear();
    if (canvas_) canvas_->refreshAll();
    refreshLayerFlags();
    Q_EMIT layerStateChanged();
}

void AppController::setCtgDirection(int direction) {
    Layer* layer = currentLayer();
    if (!layer || layer->kind != LayerKind::Ctg) return;
    Layer updated = *layer;
    updated.ctg_direction = direction == 1   ? CtgDirection::Backward
                            : direction == 2 ? CtgDirection::Nearest
                                             : CtgDirection::Forward;
    doc_.updateLayer(track_, updated.id, updated);

    doc_.ctgCache().clear();
    if (canvas_) canvas_->refreshAll();
    refreshLayerFlags();
    Q_EMIT layerStateChanged();
}

void AppController::setCtgFollow(bool on) {
    Layer* layer = currentLayer();
    if (!layer || layer->kind != LayerKind::Ctg) return;
    Layer updated = *layer;
    updated.ctg_follow_motion = on;
    doc_.updateLayer(track_, updated.id, updated);

    doc_.ctgCache().clear();
    if (canvas_) canvas_->refreshAll();
    refreshLayerFlags();
    Q_EMIT layerStateChanged();
}

// --- tracks ----------------------------------------------------------------

void AppController::addTrack() {
    stopPlayback();
    const std::string name = "track " + std::to_string(doc_.scene().tracks.size() + 1);
    doc_.beginCommand("Add track");
    const TrackId added = doc_.addTrack(name);
    doc_.addLayer(added, "layer 1");
    doc_.insertImage(added, 0);
    doc_.endCommand();
    setCurrentTrackIndex(static_cast<int>(doc_.scene().tracks.size() - 1));
}

void AppController::removeCurrentTrack() {
    if (doc_.scene().tracks.size() <= 1) return;
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;
    const std::size_t index = static_cast<std::size_t>(
        std::distance(doc_.scene().tracks.data(), track));
    doc_.beginCommand("Remove track");
    doc_.removeTrack(track_);
    doc_.endCommand();
    const auto& left = doc_.scene().tracks;
    if (left.empty()) {
        track_ = kNoId;
    } else {
        track_ = left[std::min(index, left.size() - 1)].id;
    }
    refreshEverything();
}

void AppController::renameCurrentTrack(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;
    const Track* track = doc_.scene().findTrack(track_);
    if (!track) return;
    TrackProperties props = track->properties();
    props.name = trimmed.toStdString();
    doc_.updateTrack(track_, props);
    refreshEverything();
}

void AppController::setCurrentTrackIndex(int index) {
    const auto& tracks = doc_.scene().tracks;
    if (index < 0 || static_cast<std::size_t>(index) >= tracks.size()) return;
    const TrackId id = tracks[static_cast<std::size_t>(index)].id;
    if (id == track_) return;
    track_ = id;
    layers_model_->setTrack(track_);
    tracks_model_->setCurrentTrack(track_);
    timeline_model_->setTrack(track_);
    if (canvas_) {
        canvas_->setTrack(track_);
        canvas_->setFrame(current_slot_);
        const Track* t = doc_.scene().findTrack(track_);
        if (t && !t->layers.empty()) canvas_->setActiveLayer(t->layers.front().id);
    }
    refreshEverything();
    Q_EMIT trackChanged();
}

void AppController::setOverwrite(bool on) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || track->overwrite_drawings == on) return;
    TrackProperties props = track->properties();
    props.overwrite_drawings = on;
    doc_.updateTrack(track_, props);
    timeline_model_->refresh();
    Q_EMIT trackChanged();
    syncStatus();
}

void AppController::setTrackEnd(int end) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || static_cast<int>(track->end) == end) return;
    TrackProperties props = track->properties();
    props.end = static_cast<TrackEnd>(end);
    doc_.updateTrack(track_, props);
    timeline_model_->refresh();
    if (canvas_) canvas_->refreshAll();
    Q_EMIT trackChanged();
    syncStatus();
}

void AppController::setSceneLength(int length) {
    if (doc_.scene().length == length) return;
    doc_.beginCommand("Scene length");
    doc_.setSceneLength(length);
    doc_.endCommand();
    timeline_model_->refresh();
    if (canvas_) canvas_->refreshAll();
    Q_EMIT stateChanged();
}

void AppController::refreshLayerFlags() {
    layers_model_->refresh();
}

// --- the colour switch ---------------------------------------------------

void AppController::syncColourControls() {
    Q_EMIT layerStateChanged();
}

// --- onion skin ----------------------------------------------------------

void AppController::setOnionCount(int count) {
    // Kept for callers that think of onion skin as one symmetric value; sets
    // both directions so the interface and the canvas see the same thing.
    setOnionBefore(count);
    setOnionAfter(count);
}

void AppController::setOnionBefore(int count) {
    onion_before_ = count;
    onion_count_ = onion_before_;
    if (canvas_) {
        CanvasView::OnionSettings settings = canvas_->onion();
        settings.before = count;
        canvas_->setOnion(settings);
    }
    Q_EMIT onionChanged();
}

void AppController::setOnionAfter(int count) {
    onion_after_ = count;
    onion_count_ = onion_after_;
    if (canvas_) {
        CanvasView::OnionSettings settings = canvas_->onion();
        settings.after = count;
        canvas_->setOnion(settings);
    }
    Q_EMIT onionChanged();
}

qreal AppController::onionOpacity() const {
    return onion_opacity_;
}

void AppController::setOnionOpacity(qreal opacity) {
    onion_opacity_ = opacity;
    if (canvas_) {
        CanvasView::OnionSettings settings = canvas_->onion();
        settings.opacity = opacity;
        canvas_->setOnion(settings);
    }
    Q_EMIT onionChanged();
}

// --- the timeline gestures -----------------------------------------------

void AppController::beginStretch(int run_start_slot) {
    // One command for the whole drag: nested commands collapse, so every slot
    // change during the drag undoes in a single step.
    doc_.beginCommand("Change exposure");
    stretch_run_start_ = static_cast<std::size_t>(run_start_slot);
}

void AppController::stretchTo(int pointer_x, int cell_width) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || stretch_run_start_ >= track->slots.size()) return;

    const auto [first, last] = track->runBounds(stretch_run_start_);
    const int current_length = static_cast<int>(last - first) + 1;

    const int wanted_length =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(pointer_x) / cell_width)) -
                        static_cast<int>(first));
    if (wanted_length == current_length) return;

    if (wanted_length > current_length) {
        doc_.extendExposure(track_, first, wanted_length - current_length);
    } else {
        for (int i = 0; i < current_length - wanted_length; ++i) {
            doc_.removeSlot(track_, first);
        }
    }

    timeline_model_->refresh();
    if (canvas_) canvas_->refreshAll();
    syncStatus();
}

void AppController::endStretch() {
    doc_.endCommand();
    refreshEverything();
}

void AppController::beginTimelineDrag(int slot) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || slot < 0 || static_cast<std::size_t>(slot) >= track->slots.size()) return;
    drag_image_ = track->slots[static_cast<std::size_t>(slot)];
}

int AppController::timelineDropIndexFor(int pointer_x, int cell_width) {
    const Track* track = doc_.scene().findTrack(track_);
    if (!track || drag_image_ == kNoId) return 0;

    const int boundary = (pointer_x + cell_width / 2) / cell_width;
    int index = 0;
    for (int i = 0; i < boundary && i < static_cast<int>(track->slots.size()); ++i) {
        if (track->slots[static_cast<std::size_t>(i)] != drag_image_) ++index;
    }
    return index;
}

void AppController::endTimelineDrag(int pointer_x, int cell_width) {
    const ImageId moved = drag_image_;
    drag_image_ = kNoId;
    if (moved == kNoId) return;

    const int drop = timelineDropIndexFor(pointer_x, cell_width);
    if (drop < 0) return;

    doc_.moveDrawing(track_, moved, static_cast<std::size_t>(drop));
    timeline_model_->refresh();
    const Track* track = doc_.scene().findTrack(track_);
    if (track) {
        auto it = std::find(track->slots.begin(), track->slots.end(), moved);
        if (it != track->slots.end()) {
            setCurrentSlot(static_cast<int>(std::distance(track->slots.begin(), it)));
        }
    }
    refreshEverything();
}

// --- misc ----------------------------------------------------------------

void AppController::undo() {
    stopPlayback();
    if (!doc_.undo()) return;
    refreshEverything();
}

void AppController::redo() {
    stopPlayback();
    if (!doc_.redo()) return;
    refreshEverything();
}

bool AppController::colourPending() const {
    return canvas_ ? canvas_->colourPending() : false;
}

bool AppController::waitForColour() {
    if (!canvas_) return true;
    return canvas_->settleColour();
}

// --- CtgSourcesModel ------------------------------------------------------

CtgSourcesModel::CtgSourcesModel(QObject* parent) : QAbstractListModel(parent) {}

void CtgSourcesModel::refresh(const animage::Layer* ctg_layer) {
    beginResetModel();
    raster_layers_.clear();
    used_.clear();
    if (doc_) {
        const Track* track = doc_->scene().findTrack(track_);
        if (track) {
            for (const Layer& layer : track->layers) {
                if (layer.kind != LayerKind::Raster) continue;
                raster_layers_.push_back(layer.id);
                used_.push_back(ctg_layer &&
                                std::find(ctg_layer->ctg_sources.begin(),
                                          ctg_layer->ctg_sources.end(),
                                          layer.id) != ctg_layer->ctg_sources.end());
            }
        }
    }
    endResetModel();
    Q_EMIT countChanged();
}

int CtgSourcesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(raster_layers_.size());
}

bool CtgSourcesModel::hasSelection() const {
    return std::any_of(used_.begin(), used_.end(), [](bool v) { return v; });
}

QVariant CtgSourcesModel::data(const QModelIndex& index, int role) const {
    if (!doc_ || !index.isValid()) return {};
    const std::size_t row = static_cast<std::size_t>(index.row());
    if (row >= raster_layers_.size()) return {};
    const Track* track = doc_->scene().findTrack(track_);
    const Layer* layer = track ? track->findLayer(raster_layers_[row]) : nullptr;
    if (!layer) return {};

    switch (role) {
        case NameRole:
            return QString::fromStdString(layer->name);
        case CheckedRole:
            return used_[row];
        case LayerIndexRole: {
            // The model hands out the index in the track's layer list, which
            // is what the controller expects when toggling.
            if (!track) return -1;
            for (std::size_t i = 0; i < track->layers.size(); ++i) {
                if (track->layers[i].id == layer->id) return static_cast<int>(i);
            }
            return -1;
        }
        default:
            return {};
    }
}

QHash<int, QByteArray> CtgSourcesModel::roleNames() const {
    return {
        {NameRole, "name"},
        {CheckedRole, "checked"},
        {LayerIndexRole, "layerIndex"},
    };
}

int CtgSourcesModel::indexOfLayer(LayerId id) const {
    for (std::size_t i = 0; i < raster_layers_.size(); ++i) {
        if (raster_layers_[i] == id) return static_cast<int>(i);
    }
    return -1;
}
