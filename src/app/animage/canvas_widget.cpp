// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_widget.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointingDevice>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <thread>
#include <vector>

#include "color.h"

using namespace animage;

namespace {

constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 32.0;
constexpr int kCheckerSize = 8;
constexpr double kScrubbyZoomPerPixel = 0.006;
constexpr double kSizeDragPerPixel = 0.012;
// Cached beyond the viewport, so a small pan does not recomposite. Measured
// rather than guessed: at 192 the padding more than doubled the cost of every
// full refresh -- every frame change, every opacity tick -- to buy free panning
// nobody had asked for. 64 keeps most of the benefit for a third of the price.
constexpr int kCacheMargin = 64;
// How long after a tablet event a mouse event is still assumed to have been
// promoted from it by the platform rather than produced by a real mouse.
constexpr qint64 kPenMouseWindowMs = 250;

// A transform's handles, in screen pixels and not image pixels, because that is
// where the hand is: the same box at 25% and at 400% zoom is two completely
// different targets.
constexpr double kTransformHandleSize = 9.0;
constexpr double kTransformHandleGrab = 8.0;
// The rotation handle, on a stem out from the middle of the top edge. Every
// program that has one puts it there, and a gesture nobody can see is a gesture
// nobody uses -- which is what the band below was on its own.
constexpr double kTransformRotateStem = 26.0;
constexpr double kTransformRotateKnob = 5.0;
// And just outside a corner, which stays because it is where a hand reaches
// without being told. It is the shortcut for the gizmo, not a replacement.
constexpr double kTransformRotateBand = 24.0;
// Below this the box has no interior left to press: what is there is handles,
// and moving comes from them or from the numeric fields. The mirror case is
// worth remembering too -- a whole-cel transform while zoomed in puts every
// handle off screen, and then the fields are the only grab there is.
constexpr double kTransformSmallestInterior = 16.0;
// Scale is clamped positive rather than allowed through zero. A negative scale
// is a mirror, and a mirror must be an exact index permutation rather than a
// bilinear resample at scale -1 -- which carries a half-pixel phase error and
// gives a blurred mirror that nothing complains about. See issue #24.
constexpr double kSmallestScale = 0.01;
constexpr double kRotationSnap = 15.0;
// What separates a click from a drag, in screen pixels so that it means the same
// thing at every zoom -- and never a threshold on the loop's area, because a
// legitimate selection can be a single eyelash.
constexpr double kDragThreshold = 4.0;
constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
// Below this the ring under the pointer is a smudge on the crosshair rather
// than a size, so it is not drawn at all. A one-pixel circle says less than
// nothing: it says the tool is one pixel across when the tool is six and the
// view is at 20%.
constexpr double kSmallestToolRing = 2.0;

// Cursors this program draws, because the system has none for what they mean.
//
// Both are built once, on first use, and deliberately never destroyed: a static
// QPixmap outlives QGuiApplication and destroying one after it has gone is
// undefined on some platforms. A cursor's worth of pixels is not a leak worth
// arguing about.
//
// Light under dark in both, the rule the transform box already follows: a
// cursor crosses paper and ink by definition, and a one-colour glyph disappears
// against one of them.
constexpr int kDrawnCursorSize = 32;

// Rotation has no standard cursor anywhere -- every system cursor is a size, a
// hand or an arrow -- so the circular arrow every program that turns things has
// settled on is drawn here.
QCursor buildRotateCursor() {
    QPixmap pixmap(kDrawnCursorSize, kDrawnCursorSize);
    pixmap.fill(Qt::transparent);

    const QPointF centre(16.0, 16.0);
    constexpr double kRadius = 8.0;
    const QRectF circle(centre.x() - kRadius, centre.y() - kRadius, 2 * kRadius, 2 * kRadius);

    // Most of a turn, with a gap where the arrowhead goes, so that the glyph
    // reads as a movement and not as a ring.
    constexpr double kFrom = 300.0;
    constexpr double kSweep = 250.0;
    QPainterPath arc;
    arc.arcMoveTo(circle, kFrom);
    arc.arcTo(circle, kFrom, kSweep);

    // At the end of the sweep, pointing the way the arc travels. Qt's arc angles
    // run anticlockwise on screen, so the tangent does too.
    const double end = (kFrom + kSweep) * kRadiansPerDegree;
    const QPointF tip_at(centre.x() + kRadius * std::cos(end), centre.y() - kRadius * std::sin(end));
    const QPointF along(-std::sin(end), -std::cos(end));
    const QPointF across(-along.y(), along.x());
    QPolygonF head;
    head << tip_at + along * 5.5 << tip_at + across * 4.0 << tip_at - across * 4.0;

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255), 3.6, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(arc);
    painter.setPen(QPen(QColor(255, 255, 255), 3.6, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin));
    painter.drawPolygon(head);
    painter.setPen(QPen(QColor(20, 20, 24), 1.6, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(arc);
    painter.setPen(QPen(QColor(20, 20, 24), 1.0));
    painter.setBrush(QColor(20, 20, 24));
    painter.drawPolygon(head);
    painter.end();

    return QCursor(pixmap, kDrawnCursorSize / 2, kDrawnCursorSize / 2);
}

// The eraser, which is the tool with nothing else on screen to announce it.
//
// A drawn glyph rather than a circle at the tool's radius, and the reason is
// worth keeping: **anything drawn by the widget arrives a frame late.** The
// pointer is moved by the hardware and a ring is painted by us, so a ring
// following the pointer trails behind it at exactly the speed the hand is
// moving. It was built that way first and it reads as lag, because it is lag.
// What must sit under a moving pointer has to *be* the cursor.
QCursor buildEraseCursor() {
    QPixmap pixmap(kDrawnCursorSize, kDrawnCursorSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Drawn upright and turned, like the pipette: the working end lands at the
    // bottom left, where a hand holding one would put it.
    painter.translate(16.0, 16.0);
    painter.rotate(45.0);

    const QRectF block(-5.0, -7.5, 10.0, 15.5);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255), 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawRoundedRect(block, 1.6, 1.6);
    painter.setPen(QPen(QColor(20, 20, 24), 1.4));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawRoundedRect(block, 1.6, 1.6);
    // The band across it, which is the whole of what makes a white block read
    // as a rubber and says which end of it does the rubbing.
    painter.drawLine(QPointF(-5.0, 3.0), QPointF(5.0, 3.0));
    painter.end();

    // The middle of the working face, turned: (0, 8) about the middle.
    return QCursor(pixmap, 10, 22);
}

// Alt picks the colour under the pointer, which is a press that does something
// with nothing on screen to say so -- the same complaint as the transform box,
// one modifier away. A pipette is what every program draws for it.
QCursor buildPickCursor() {
    QPixmap pixmap(kDrawnCursorSize, kDrawnCursorSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Drawn upright and turned, so the shape is written once: the tip ends up
    // at the bottom left, where a hand holding a pipette would put it.
    painter.translate(16.0, 16.0);
    painter.rotate(45.0);

    // A needle and a bulb, kept apart. Drawn as one filled shape the two ran
    // together into a lump at this size -- a cursor is thirty-two pixels and
    // the first version proved it by looking like a thumb.
    QPainterPath needle;
    needle.moveTo(0.0, 10.5);  // the tip, which is the hotspot
    needle.lineTo(-1.9, 5.0);
    needle.lineTo(1.9, 5.0);
    needle.closeSubpath();
    needle.addRect(QRectF(-1.2, -1.0, 2.4, 6.0));
    // Narrow and square-shouldered rather than round. A circle on a stem is a
    // magnifying glass, and this program has a zoom gesture on a held key --
    // two glyphs a hand could confuse are worse than one glyph fewer.
    const QRectF bulb(-2.8, -11.5, 5.6, 10.0);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255), 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(needle);
    painter.drawRoundedRect(bulb, 1.6, 1.6);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 20, 24));
    painter.drawPath(needle);
    // The bulb is hollow, which is the whole of what makes the glyph read: a
    // dark outline round white says pipette, and a second dark lump says
    // nothing at all.
    painter.setPen(QPen(QColor(20, 20, 24), 1.4));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawRoundedRect(bulb, 1.6, 1.6);
    painter.end();

    // Where the tip landed once the glyph was turned: (0, 10.5) about the middle.
    return QCursor(pixmap, 9, 23);
}

const QCursor& rotateCursor() {
    static const QCursor* cursor = new QCursor(buildRotateCursor());
    return *cursor;
}

const QCursor& eraseCursor() {
    static const QCursor* cursor = new QCursor(buildEraseCursor());
    return *cursor;
}

const QCursor& pickCursor() {
    static const QCursor* cursor = new QCursor(buildPickCursor());
    return *cursor;
}

QCursor cursorFor(CanvasWidget::Pointing pointing) {
    using Pointing = CanvasWidget::Pointing;
    switch (pointing) {
        // The brush and the lasso both place a point, and a cross is what says
        // where it will land.
        case Pointing::Draw:
        case Pointing::Lasso: return QCursor(Qt::CrossCursor);
        // The eraser puts its own glyph *in place of* the cross rather than
        // beside it. A ring at its radius was the first version and it was
        // wrong twice over: it trailed the pointer, and two marks under one
        // hand read as two pointers rather than as one tool.
        case Pointing::Erase: return eraseCursor();
        case Pointing::Pick: return pickCursor();
        case Pointing::PanReady: return QCursor(Qt::OpenHandCursor);
        case Pointing::Panning: return QCursor(Qt::ClosedHandCursor);
        // Two different things, one gesture: a horizontal drag changing a
        // number. The cursor says what the hand has to do, and it is the same.
        case Pointing::Zoom:
        case Pointing::SizeBrush: return QCursor(Qt::SizeHorCursor);
        case Pointing::Move: return QCursor(Qt::SizeAllCursor);
        case Pointing::Rotate: return rotateCursor();
        case Pointing::ScaleHorizontal: return QCursor(Qt::SizeHorCursor);
        case Pointing::ScaleVertical: return QCursor(Qt::SizeVerCursor);
        case Pointing::ScaleFalling: return QCursor(Qt::SizeFDiagCursor);
        case Pointing::ScaleRising: return QCursor(Qt::SizeBDiagCursor);
        // Not a drawing cursor and not a sizing one: an arrow, which is what
        // the rest of the interface uses for "this is not a place to draw".
        case Pointing::Nothing: return QCursor(Qt::ArrowCursor);
    }
    return QCursor(Qt::CrossCursor);
}

// Which way a handle stretches the drawing, on screen.
//
// The box turns, so the answer turns with it: the top edge of a box rotated a
// quarter turn stretches sideways, and a cursor that still said "vertical"
// there would be describing the drawing's own axes, which are not the ones the
// hand is moving along.
CanvasWidget::Pointing scalePointingFor(int handle, double rotation_degrees) {
    // Outward from the box, clockwise from the top left, in screen directions.
    // Even is a corner and odd is an edge middle, the same as the handles.
    constexpr double kOutward[8][2] = {{-1.0, -1.0}, {0.0, -1.0}, {1.0, -1.0}, {1.0, 0.0},
                                       {1.0, 1.0},   {0.0, 1.0},  {-1.0, 1.0}, {-1.0, 0.0}};
    const std::size_t index = static_cast<std::size_t>(handle & 7);
    const double radians = rotation_degrees * kRadiansPerDegree;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    // The scale is not applied. A corner of a box squashed flat still points
    // nearly sideways, and it still scales both axes: what the cursor names is
    // the operation, turned to where the hand has to drag for it.
    const double x = kOutward[index][0] * cosine - kOutward[index][1] * sine;
    const double y = kOutward[index][0] * sine + kOutward[index][1] * cosine;

    // A size cursor points both ways, so only the line matters and not which
    // end of it the handle is on.
    double angle = std::atan2(y, x) / kRadiansPerDegree;
    while (angle < 0.0) angle += 180.0;
    while (angle >= 180.0) angle -= 180.0;

    if (angle < 22.5 || angle >= 157.5) return CanvasWidget::Pointing::ScaleHorizontal;
    if (angle < 67.5) return CanvasWidget::Pointing::ScaleFalling;
    if (angle < 112.5) return CanvasWidget::Pointing::ScaleVertical;
    return CanvasWidget::Pointing::ScaleRising;
}

// Where the rotation knob sits, given the eight handles: out from the middle of
// the top edge, away from the box, at a fixed distance on screen.
//
// Away from the *centre* rather than along a fixed axis, so it stays outside the
// box however far the box has been turned -- at 180 degrees "up" is into it.
QPointF rotationGizmo(const std::array<QPointF, 8>& handles) {
    const QPointF centre = (handles[0] + handles[4]) / 2.0;
    QPointF away = handles[1] - centre;
    const double reach = std::hypot(away.x(), away.y());
    // A box with no height at all has no direction to go: straight up on screen
    // is the only answer left, and it is better than none.
    if (reach < 1e-6) return handles[1] - QPointF(0.0, kTransformRotateStem);
    away /= reach;
    return handles[1] + away * kTransformRotateStem;
}

// A corner or an edge middle of an untransformed box, clockwise from the top
// left. Even is a corner, odd is an edge middle.
animage::Vec2 handleInImage(const animage::PixelRect& bounds, int index) {
    const double left = bounds.x;
    const double top = bounds.y;
    const double right = bounds.x + bounds.width;
    const double bottom = bounds.y + bounds.height;
    const double middle_x = (left + right) / 2.0;
    const double middle_y = (top + bottom) / 2.0;

    switch (index & 7) {
        case 0: return {left, top};
        case 1: return {middle_x, top};
        case 2: return {right, top};
        case 3: return {right, middle_y};
        case 4: return {right, bottom};
        case 5: return {middle_x, bottom};
        case 6: return {left, bottom};
        default: return {left, middle_y};
    }
}

// Linear to sRGB through a lookup table. The conversion is per pixel of the
// viewport on every recomposite, and std::pow in that loop is measurable.
const std::array<quint8, 4096>& srgbTable() {
    static const std::array<quint8, 4096> table = [] {
        std::array<quint8, 4096> values{};
        for (int i = 0; i < 4096; ++i) {
            const float linear = static_cast<float>(i) / 4095.0f;
            const float encoded = std::clamp(linearToSrgb(linear), 0.0f, 1.0f);
            values[static_cast<std::size_t>(i)] =
                static_cast<quint8>(std::lround(encoded * 255.0f));
        }
        return values;
    }();
    return table;
}

quint8 toSrgbByte(float linear) {
    const int index = static_cast<int>(std::lround(std::clamp(linear, 0.0f, 1.0f) * 4095.0f));
    return srgbTable()[static_cast<std::size_t>(index)];
}

bool comesFromAStylus(const QPointingDevice* device) {
    if (!device) return false;
    const QInputDevice::DeviceType type = device->type();
    return type == QInputDevice::DeviceType::Stylus ||
           type == QInputDevice::DeviceType::Airbrush || type == QInputDevice::DeviceType::Puck;
}

// Threads cost about as much to start as a small band costs to convert, so
// only spread work that is worth spreading. The same shape as the
// compositor's own heuristic, and for the same reason.
int chooseWorkerCount(int rows, int columns) {
    const long long work = static_cast<long long>(rows) * columns;
    if (work < 64'000) return 1;
    const unsigned hardware = std::thread::hardware_concurrency();
    const int available = static_cast<int>(hardware ? hardware : 1u);
    return std::clamp(std::min(available, rows / 32), 1, 8);
}

PixelRect intersect(const PixelRect& a, const PixelRect& b) {
    const int x0 = std::max(a.x, b.x);
    const int y0 = std::max(a.y, b.y);
    const int x1 = std::min(a.x + a.width, b.x + b.width);
    const int y1 = std::min(a.y + a.height, b.y + b.height);
    if (x1 <= x0 || y1 <= y0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

bool contains(const PixelRect& outer, const PixelRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

// The view, moved to a whole number of screen pixels.
//
// This is what makes a screen-resolution cache worth having. Entry e of the
// cache begins at image x = e/zoom, which lands on a whole screen pixel exactly
// when pan * zoom is whole -- and then the cache blits one entry to one pixel,
// a copy rather than a resample. Off that alignment Qt filters the cache
// against itself at roughly 1:1, which is pure blur: measured at 4.2 RMS
// against a curve drawn at display size, where the aligned blit gives 1.7.
//
// Nothing is given up for it. A pan is a gesture in screen pixels to begin
// with, and the largest correction this can make is half of one.
QPointF onWholeScreenPixels(const QPointF& pan, double zoom) {
    if (zoom <= 0.0) return pan;
    return {std::round(pan.x() * zoom) / zoom, std::round(pan.y() * zoom) / zoom};
}

PixelRect unite(const PixelRect& a, const PixelRect& b) {
    if (a.isEmpty()) return b;
    if (b.isEmpty()) return a;
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.width, b.x + b.width);
    const int y1 = std::max(a.y + a.height, b.y + b.height);
    return {x0, y0, x1 - x0, y1 - y0};
}

}  // namespace

CanvasWidget::CanvasWidget(Document& document, QWidget* parent)
    : QWidget(parent), doc_(document) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TabletTracking);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    brush_settings_.radius = 6.0f;
    brush_settings_.hardness = 0.6f;
    brush_settings_.pressure_affects_opacity = true;
    brush_settings_.r = brush_settings_.g = brush_settings_.b = 0.0f;
    brush_settings_.a = 1.0f;

    eraser_settings_ = brush_settings_;
    eraser_settings_.radius = 18.0f;
    eraser_settings_.hardness = 0.85f;
    eraser_settings_.pressure_affects_opacity = false;
    eraser_settings_.erase = true;

    ctg_poll_ = new QTimer(this);
    ctg_poll_->setInterval(16);
    connect(ctg_poll_, &QTimer::timeout, this, &CanvasWidget::collectColour);

    // The one place the cursor is set, from the start. Nothing else in this
    // file calls setCursor.
    refreshPointer();

    clock_.start();
}

CanvasWidget::~CanvasWidget() {
    // Before anything else this object owns goes away. A solve holds only its
    // own copy of the drawing, so nothing here is unsafe -- but a max-flow that
    // has not been told the window is closing goes on running for a second or
    // two after it has, and the process stays up with nothing on screen.
    ctg_solver_.cancelAll();
}

void CanvasWidget::setTrack(TrackId track) {
    // Leaving commits, like changing frame does. A float that followed you to
    // another track would be a transform of the wrong drawing waiting to happen.
    if (transform_ && track != track_) applyTransform();
    track_ = track;
    setFrame(slot_);
}

void CanvasWidget::setFrame(std::size_t slot) {
    // Changing frame commits. A float that follows you to another drawing is a
    // paste onto the wrong drawing waiting to happen, and there is nothing
    // useful it could mean out there.
    if (transform_ && slot != slot_) applyTransform();
    // And the loop goes with it, while surviving a change of layer. A loop is
    // geometry in image space, so re-lifting it from another layer of the same
    // drawing is meaningful; carrying it to another drawing is how you transform
    // the wrong thing.
    if (slot != slot_) clearSelection();

    // Clamped to the scene and not to the current track: the timeline is shared,
    // so frame 40 is a real frame of the shot even when the track being edited
    // stops at 12. Standing there simply means this track has no drawing, which
    // the rest of this handles -- the canvas still shows whatever the other
    // tracks have, and there is nothing here to draw on until one is added.
    // Everything reachable, not just the shot: a track may run past a fixed
    // scene length, and those drawings are still there to be worked on.
    const std::size_t frames = doc_.scene().timelineFrames();
    slot_ = (frames == 0) ? 0 : std::min(slot, frames - 1);

    // What the track *holds* here, not what it shows. Past its last drawing
    // there is no slot and no cel, so there is nothing to draw on -- and the end
    // behaviour is a fact about the picture rather than about the track's
    // contents, which is the same reason a layer's own export sequence stops
    // where the track does while the composite carries on.
    //
    // The canvas still shows the held or cycled drawing, because that is the
    // picture. You can see it out there and not draw on it, and the timeline
    // says so: those frames are drawn dotted and faint, because they are not
    // frames anybody exposed.
    const Track* track = doc_.scene().findTrack(track_);
    const ImageId next = track ? track->imageAtSlot(slot_) : kNoId;
    const bool changed = next != image_;
    image_ = next;

    // A stroke in progress when the frame changes carries on onto the new
    // drawing. Holding the pen down through playback then leaves a mark on
    // each frame it passed over, which is how you sketch a moving point.
    if (changed && stroking_) rebindStrokeToCurrentImage();

    // Marked, not rebuilt. Holding an arrow key produces frame changes faster
    // than a rebuild takes, and rebuilding on each one let the queue run away:
    // the playhead carried on moving for a while after the key came up.
    onion_dirty_ = true;
    refreshAll();
}

void CanvasWidget::setActiveLayer(LayerId layer) {
    if (layer == active_layer_) return;
    // A transform is of one layer -- the active one -- so choosing another says
    // you are done with this one. Guarded on the layer actually changing,
    // because rebuilding the layer panel reselects the same row constantly and
    // a transform must not be committed by a repaint.
    if (transform_) applyTransform();
    active_layer_ = layer;
}

void CanvasWidget::setEraser(bool erasing) {
    erasing_ = erasing;
    // Which tool is up is half of what the pointer answers, so picking one is a
    // reason to ask again. This is where the eraser stopped being invisible.
    refreshPointer();
}

void CanvasWidget::setBrushColour(float r, float g, float b) {
    brush_settings_.r = r;
    brush_settings_.g = g;
    brush_settings_.b = b;
}

void CanvasWidget::setBackground(Background background) {
    background_ = background;
    refreshAll();
}

// A repaint and not a refresh: the veil is painted over the blit, so nothing
// composited changes and the cache stays good.
void CanvasWidget::setPassePartout(bool shown) {
    if (passe_partout_ == shown) return;
    passe_partout_ = shown;
    update();
}

void CanvasWidget::setOnion(const OnionSettings& settings) {
    onion_settings_ = settings;
    onion_dirty_ = true;
    refreshAll();
}

void CanvasWidget::setPlaying(bool playing) {
    if (playing_ == playing) return;
    playing_ = playing;
    onion_dirty_ = true;
    refreshAll();
}

// Asks for any CTG fill whose scribbles or line art have moved to be solved
// somewhere else.
//
// Nothing is computed here. What was a max-flow inside a paint -- capped at
// about 512x512 precisely so that it could not stop the program -- is now a
// hash compared against the cached fill and, if they differ, a snapshot handed
// to a worker thread. The old fill stays on screen until the new one lands,
// which is the honest thing to show: it is what the drawing looked like a
// moment ago, and the alternative is the colour blinking out on every stroke.
//
// The bookkeeping in ctg_asked_ is what stops this asking again on the next
// paint for a solve that is already running. A widget repaints many times a
// second, and the solver's rule is that the newest question wins -- so without
// it, every paint would call off the answer the last paint was waiting for and
// no fill would ever be finished.
// Every track and not only the one being edited, because every track is on
// screen: a colour layer whose fill was never asked for simply does not draw,
// so a background track would have arrived coloured and gone blank the moment
// the character track was selected.
void CanvasWidget::requestCtgFills() {
    // Never during a stroke, whichever layer is being drawn on. Every dab bumps
    // the cel's revision, so asking whenever a fill looked stale would start a
    // solve per dab -- and each one would supersede the last, so the pen would
    // be leaving a trail of abandoned max-flows and no fill at all.
    //
    // This used to test for a scribble in progress, which covered drawing on the
    // colour layer and missed drawing on the line art it is cut against. Inking
    // over a filled drawing therefore re-solved on every dab, and it is the same
    // trap either way: the solve belongs at the end of the stroke.
    if (stroking_) return;

    dropStaleColourRequests(/*only_this_frame=*/true);

    const std::uint64_t generation = doc_.ctgCache().generation();
    const CtgSettings settings;
    for (const Track& track_here : doc_.scene().tracks) {
      const TrackId track_id = track_here.id;
      // What is on screen needs a fill, wherever in its own time it came from:
      // a cycling track's colour has to be solved out past its last drawing too.
      const ImageId image = track_here.imageShownAt(slot_);
      if (image == kNoId) continue;

      for (const Layer& layer : track_here.layers) {
        if (layer.kind != LayerKind::Ctg || !layer.visible) continue;
        // Nothing to solve for a layer showing its scribbles: the fill would be
        // computed and then not drawn.
        if (layer.show_scribbles) continue;

        const CtgInputs wanted = ctgInputsFor(doc_, track_id, image, layer.id, settings);
        if (!wanted.valid) continue;

        // Coarse first, then as fine as the drawing deserves. The first answer
        // is bounded so that it arrives while the stroke that caused it is
        // still recent -- about a tenth of a second -- and the second is bounded
        // only by what a max-flow costs in memory, which at 1080p is full
        // resolution. Nothing waits for either.
        //
        // A fill is finished when it is as fine as it was asked for, or when it
        // already had the largest allowance there is. Without the second half a
        // drawing too large for even the full budget would be re-solved for
        // ever, arriving at the same coarse answer every time.
        const CtgFill* held = doc_.ctgFillFor(track_id, image, layer.id);
        const bool current = held && held->valid && held->inputs == wanted.hash;
        if (current && (held->step <= std::max(1, settings.downscale) ||
                        held->budget >= kFullSolveBudget)) {
            continue;
        }
        const long long budget = current ? kFullSolveBudget : kInteractiveSolveBudget;

        const ColourAsked asked{image, layer.id, true};
        const auto already = ctg_asked_.find(asked);
        if (already != ctg_asked_.end() && already->second.inputs == wanted.hash) {
            continue;  // already being worked out
        }

        ctg_asked_[asked] = {wanted.hash, generation};
        ctg_solver_.request({image, layer.id},
                            ctgJobFor(doc_, track_id, image, layer.id, settings, budget), true);
      }
    }

    noteColourPending();
}

// Starts and stops the poll, and says when the answer to "is the colour being
// worked out" has changed.
//
// Both because they are the same event. Nothing is waiting for a solve, but the
// status bar is entitled to say one is happening: it is the whole of the
// visible difference between solving here and solving elsewhere, and without it
// a fill that is a second out of date looks like a fill that is wrong.
void CanvasWidget::noteColourPending() {
    const bool pending = !ctg_asked_.empty();
    if (ctg_poll_) {
        if (pending && !ctg_poll_->isActive()) ctg_poll_->start();
        if (!pending) ctg_poll_->stop();
    }
    if (pending == colour_was_pending_) return;
    colour_was_pending_ = pending;
    Q_EMIT colourChanged();
}

// Whether any track shows this drawing at the frame the playhead is on. "The
// drawing on screen" is no longer one drawing: several tracks are composited
// and each has its own, so leaving a frame means leaving all of them.
bool CanvasWidget::isShownNow(ImageId image) const {
    if (image == kNoId) return false;
    for (const Track& track : doc_.scene().tracks) {
        if (track.imageShownAt(slot_) == image) return true;
    }
    return false;
}

// Requests whose answer nobody is waiting for any more.
//
// A fill for a frame that has been left, or -- fill or judgement, on screen
// or not -- one about a document that has since been thrown away. Playing a
// coloured shot is twenty-four of the first a second against solves taking a
// tenth of one, and a queue that fills faster than it drains never catches up.
//
// Judgements are not dropped for being about another drawing: being about the
// drawings you are not looking at is the whole of what they are for.
void CanvasWidget::dropStaleColourRequests(bool only_this_frame) {
    const std::uint64_t generation = doc_.ctgCache().generation();
    for (auto it = ctg_asked_.begin(); it != ctg_asked_.end();) {
        const bool left = only_this_frame && it->first.tiles && !isShownNow(it->first.image);
        if (!left && it->second.generation == generation) {
            ++it;
            continue;
        }
        ctg_solver_.cancel({it->first.image, it->first.layer}, it->first.tiles);
        it = ctg_asked_.erase(it);
    }
}

// Takes what the solver has finished and puts it in the document.
//
// This runs on the interface thread and it is the only place a solved fill or
// verdict enters the document, which is the whole of the threading discipline
// here: the worker is handed a copy and hands back an answer, and every write
// to the document stays on the thread that owns it.
void CanvasWidget::collectColour() {
    bool filled = false;
    dropStaleColourRequests(/*only_this_frame=*/false);

    for (CtgSolver::Result& result : ctg_solver_.collect()) {
        const ColourAsked key{result.key.image, result.key.layer, result.wanted_tiles};
        const auto asked = ctg_asked_.find(key);
        // An answer to a question that has since been asked again, about a
        // drawing that has since been left, or about a document that has since
        // been replaced. All ordinary, and all dropped: what it would have
        // replaced is at worst as old.
        if (asked == ctg_asked_.end() || asked->second.inputs != result.fill.inputs) continue;

        ctg_asked_.erase(asked);
        // Where the marks ended up. The Marks column and the first stroke on a
        // carrying drawing both have to agree with the fill about that. See
        // Document::ctgShiftAt.
        doc_.ctgShifts()[result.key] = result.fill.carried_by;
        doc_.ctgCache().store(result.key, std::move(result.fill));
        filled = true;
    }

    if (!filled) {
        noteColourPending();
        return;
    }
    colour_was_pending_ = !ctg_asked_.empty();
    if (ctg_poll_ && ctg_asked_.empty()) ctg_poll_->stop();

    // A regenerated fill changes colour across whole regions, nowhere near
    // wherever the pen was, so all of it is redrawn. Marking only the stroke's
    // own rectangle left the new fill beside the stroke and the old one
    // everywhere else, and hiding and showing the layer repainted the lot -- so
    // the same operation appeared to have two behaviours.
    //
    dirty_everything_ = true;
    update();
    Q_EMIT colourChanged();
}

bool CanvasWidget::settleColour(int timeout_ms) {
    QElapsedTimer clock;
    clock.start();
    while (true) {
        // Asking again is what carries the ladder up a rung: the coarse answer
        // being installed is exactly what makes the fine one worth asking for,
        // and in use it is the repaint that does this. Waiting without it would
        // settle on the first answer and call that the colour.
        requestCtgFills();
        if (ctg_asked_.empty()) return true;
        if (clock.elapsed() > timeout_ms) return false;
        ctg_solver_.waitUntilIdle();
        collectColour();

        // A solve that came back to nobody -- superseded, or about a drawing
        // that has been left -- leaves its entry behind, and waiting for an
        // answer that will never arrive is a hang. Nothing outstanding at the
        // solver means nothing more is coming.
        if (!ctg_asked_.empty() && ctg_solver_.idle()) {
            ctg_asked_.clear();
            return false;
        }
    }
    return true;
}

// While a scribble is being drawn its layer shows the marks rather than the
// fill, so the pen leaves something visible without a solve behind it. A view
// flag, so it is set directly and never recorded: undo has no business
// restoring what you were looking at.
void CanvasWidget::setScribblePreview(LayerId layer_id, bool previewing) {
    Track* track = doc_.mutableScene().findTrack(track_);
    if (!track) return;
    Layer* layer = track->findLayer(layer_id);
    if (!layer || layer->kind != LayerKind::Ctg) return;

    if (previewing) {
        scribble_preview_layer_ = layer_id;
        scribble_preview_was_showing_ = layer->show_scribbles;
        layer->show_scribbles = true;
    } else {
        layer->show_scribbles = scribble_preview_was_showing_;
        scribble_preview_layer_ = kNoId;
    }
}

// Flattens the neighbouring drawings into one tinted, faded layer. Previous
// drawings go warm and later ones cool, which is the convention every animator
// already reads without being told.
void CanvasWidget::rebuildOnion() {
    onion_.resize(0, 0);
    if (playing_ || track_ == kNoId) return;
    if (onion_settings_.before <= 0 && onion_settings_.after <= 0) return;

    const Track* track = doc_.scene().findTrack(track_);
    if (!track || cached_region_.isEmpty()) return;

    onion_.resize(cache_step_.entriesAcross(cached_region_.x, cached_region_.width),
                  cache_step_.entriesAcross(cached_region_.y, cached_region_.height));

    struct Ghost {
        ImageId image;
        float weight;
        float r, g, b;
    };
    std::vector<Ghost> ghosts;

    const auto collect = [&](int count, int direction, float r, float g, float b) {
        if (count <= 0) return;
        const std::vector<ImageId> neighbours =
            track->distinctNeighbours(slot_, count, direction);
        for (std::size_t i = 0; i < neighbours.size(); ++i) {
            const float falloff =
                static_cast<float>(count - static_cast<int>(i)) / static_cast<float>(count);
            ghosts.push_back({neighbours[i], onion_settings_.opacity * falloff, r, g, b});
        }
    };
    collect(onion_settings_.before, -1, 0.85f, 0.15f, 0.10f);
    collect(onion_settings_.after, +1, 0.10f, 0.40f, 0.85f);

    // Furthest first, so the nearest drawing ends up on top.
    std::stable_sort(ghosts.begin(), ghosts.end(),
                     [](const Ghost& a, const Ghost& b) { return a.weight < b.weight; });

    Framebuffer ghost_frame;
    for (const Ghost& ghost : ghosts) {
        compositor_.composite(doc_, track_, ghost.image, cached_region_, ghost_frame,
                              cache_step_);
        for (int y = 0; y < onion_.height(); ++y) {
            const Rgba* source = ghost_frame.row(y);
            Rgba* destination = onion_.row(y);
            for (int x = 0; x < onion_.width(); ++x) {
                const float alpha = std::clamp(source[x].a, 0.0f, 1.0f) * ghost.weight;
                if (alpha <= 0.0f) continue;
                // Tinted silhouette: the shape is what matters, not the colour
                // the neighbouring drawing happens to be.
                const Rgba tinted{ghost.r * alpha, ghost.g * alpha, ghost.b * alpha, alpha};
                destination[x] = over(tinted, destination[x]);
            }
        }
    }
}

QPointF CanvasWidget::pan() const { return onWholeScreenPixels(pan_, zoom_); }

// Both transforms go through pan() rather than pan_, so what the pen is told it
// is touching is what the blit actually put there. The half-pixel of alignment
// is applied identically to input and output, which is the only way the two can
// agree.
QPointF CanvasWidget::imageFromWidget(const QPointF& widget_point) const {
    const QPointF at = pan();
    return {at.x() + widget_point.x() / zoom_, at.y() + widget_point.y() / zoom_};
}

QPointF CanvasWidget::widgetFromImage(const QPointF& image_point) const {
    const QPointF at = pan();
    return {(image_point.x() - at.x()) * zoom_, (image_point.y() - at.y()) * zoom_};
}

PixelRect CanvasWidget::visibleImageRect() const {
    const QPointF top_left = imageFromWidget({0.0, 0.0});
    const QPointF bottom_right = imageFromWidget({static_cast<double>(width()),
                                                  static_cast<double>(height())});
    const int x0 = static_cast<int>(std::floor(top_left.x())) - 1;
    const int y0 = static_cast<int>(std::floor(top_left.y())) - 1;
    const int x1 = static_cast<int>(std::ceil(bottom_right.x())) + 1;
    const int y1 = static_cast<int>(std::ceil(bottom_right.y())) + 1;
    return {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
}

long long CanvasWidget::cacheEntryCount() const {
    return static_cast<long long>(display_.width()) * display_.height();
}

void CanvasWidget::ensureCacheCoversView() {
    const PixelRect wanted = visibleImageRect();

    // One cache entry per screen pixel, never finer than one per image pixel.
    //
    // This is the whole of issue #11, and it replaces a memory budget that was
    // making decisions nobody attributed to it. The cache used to be held at
    // one entry per *image* pixel reduced by an integer step, which forces two
    // things at once: the size grows as viewport/zoom^2, so it must be capped,
    // and an integer step cannot follow a continuous zoom, so there must be a
    // zoom at which it doubles -- with the cap deciding where. A stroke was
    // crisp at 62% and jagged at 60% because a memory limit landed between
    // them, and the boundary moved with the window size, so the same percentage
    // meant different sharpness in different windows.
    //
    // Held at one entry per screen pixel, the size is about the viewport at
    // every zoom, the sampling ratio is 1/zoom and continuous, and reducing a
    // block to its average becomes what the compositor's loop is already for.
    // Everything allocated here -- the cache, the onion buffer, the scratch the
    // compositor writes into -- is now bounded by the window rather than by how
    // much of the image the window can see. At 5% zoom that used to ask for
    // half a gigabyte.
    const SampleStep step = SampleStep::fromRatio(std::max(1.0, 1.0 / zoom_));

    // The margin is a fixed number of *screen* pixels, converted to image
    // pixels here. Held in image pixels instead it was 192 at 100% zoom and
    // 6144 at 3200%, so the cache -- and the rectangle the blit has to scale it
    // into -- grew without bound the further you zoomed in. Nothing spends it
    // any more: it costs the same fraction of a viewport-sized cache whatever
    // the zoom, so there is no longer a band where a pan recomposited on every
    // single mouse move.
    const int margin = std::max(1, static_cast<int>(std::lround(kCacheMargin / zoom_)));

    // Snapped out to entry boundaries, so that a partial refresh lands on the
    // same entries a full one would and no entry is averaged from part of its
    // block.
    const PixelRect padded =
        snapToSampleGrid(step, {wanted.x - margin, wanted.y - margin, wanted.width + 2 * margin,
                                wanted.height + 2 * margin});

    // Panning inside the cached margin, or zooming in, needs no new composite
    // at all -- the cache already holds those pixels and the blit just samples
    // a different part of it. Recompositing on every mouse move was what made
    // navigation feel heavy.
    if (!display_.isNull() && step == cache_step_ && contains(cached_region_, wanted)) {
        const long long cached_area =
            static_cast<long long>(cached_region_.width) * cached_region_.height;
        const long long wanted_area = static_cast<long long>(wanted.width) * wanted.height;
        // Unless the cache has become far larger than the view needs, which
        // happens after zooming a long way in and would waste the memory.
        if (cached_area <= wanted_area * 4) return;
    }

    cache_step_ = step;
    cached_region_ = padded;

    display_ = QImage(step.entriesAcross(padded.x, padded.width),
                      step.entriesAcross(padded.y, padded.height), QImage::Format_RGB32);
    onion_dirty_ = true;
    dirty_everything_ = true;
}

void CanvasWidget::refreshAll() {
    dirty_everything_ = true;
    update();
}

void CanvasWidget::markDirty(const PixelRect& region) {
    if (dirty_everything_) return;
    pending_dirty_ = unite(pending_dirty_, region);
}

// Composites `region` and writes it into the cached sRGB image. This is the
// only place the linear working space becomes display pixels.
void CanvasWidget::refreshRegion(const PixelRect& region) {
    // No test for a current drawing any more: what is composited is the whole
    // scene at this frame, so the track being edited having nothing here says
    // nothing about whether there is a picture to draw.
    if (display_.isNull()) return;

    PixelRect area = intersect(region, cached_region_);
    if (area.isEmpty()) return;

    // Snap out to the sampling grid, so the entries this produces are exactly
    // the ones a full refresh would have produced. The grid is anchored at the
    // image origin rather than at the cached region, which is what makes that
    // true whatever the region happens to be and wherever the view has panned
    // to; the cached region is snapped too, so intersecting keeps it aligned.
    area = intersect(snapToSampleGrid(cache_step_, area), cached_region_);
    if (area.isEmpty()) return;

    // The layer being transformed stands in for itself: what is left of it after
    // the lift is drawn in its own place in the stack, and what was picked up is
    // drawn on top through the matrix. That is the whole of why the document
    // does not have to be written until the transform is committed.
    const SubstitutedLayer substituted =
        transform_ ? SubstitutedLayer{transform_->layer, &transform_->remaining}
                   : SubstitutedLayer{};
    compositor_.compositeScene(doc_, slot_, area, scratch_, cache_step_, substituted);

    // Where this patch of entries sits in the cache.
    const long long first_column = cache_step_.entryAt(area.x);
    const long long first_row = cache_step_.entryAt(area.y);
    const int column_base =
        static_cast<int>(first_column - cache_step_.entryAt(cached_region_.x));
    const int row_base = static_cast<int>(first_row - cache_step_.entryAt(cached_region_.y));

    const bool checker = background_ == Background::Checker;
    const float flat = 1.0f;

    // Detached once, here, before any worker exists. QImage is copy-on-write and
    // scanLine() is the non-const accessor that triggers the copy, so calling it
    // from several threads is a race over the detach rather than over the
    // pixels. bits() does the same detach once, and the rows are then addressed
    // by hand.
    uchar* const base_line = display_.bits();
    const qsizetype stride = display_.bytesPerLine();

    // One band of rows: paper, onion, drawing, sRGB. Rows are independent --
    // each reads one row of the scratch and writes one scanline of the cache --
    // so this splits exactly the way the compositor already splits.
    //
    // It has to. This loop, not the flattening, is the larger part of a refresh
    // by a wide margin: compositing a full viewport measures 3-8 ms and this
    // measured 37, and it is the same 37 ms whether the drawing has 66 tiles or
    // 2425 of them, because the work is per output pixel rather than per stroke.
    // The compositor was parallelised years before this loop was even timed --
    // `bench_composite` only ever watched the other end. See `bench_zoom`.
    const auto convert_rows = [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            const Rgba* source = scratch_.row(y);
            const int row = row_base + y;
            if (row < 0 || row >= display_.height()) continue;
            auto* destination = reinterpret_cast<QRgb*>(base_line + row * stride);
            // Only the checker needs to know where the entry sits in the
            // drawing, and it is the uncommon background, so the mapping back
            // to image coordinates is paid for only when it is asked for.
            const int image_y = checker ? cache_step_.entryBegin(first_row + y) : 0;

            for (int x = 0; x < scratch_.width(); ++x) {
                const int column = column_base + x;
                if (column < 0 || column >= display_.width()) continue;
                float background = flat;
                if (checker) {
                    const int image_x = cache_step_.entryBegin(first_column + x);
                    const bool light =
                        (((image_x / kCheckerSize) + (image_y / kCheckerSize)) & 1) == 0;
                    background = light ? 1.0f : 0.78f;
                }

                // Paper, then the onion skin over it, then the drawing over that.
                Rgba base{background, background, background, 1.0f};
                if (!onion_.isEmpty() && row < onion_.height() && column < onion_.width()) {
                    base = over(onion_.row(row)[column], base);
                }

                const Rgba& pixel = source[x];
                const float keep = 1.0f - std::clamp(pixel.a, 0.0f, 1.0f);
                const float r = pixel.r + base.r * keep;
                const float g = pixel.g + base.g * keep;
                const float b = pixel.b + base.b * keep;

                destination[column] = qRgb(toSrgbByte(r), toSrgbByte(g), toSrgbByte(b));
            }
        }
    };

    const int rows = scratch_.height();
    const int workers = chooseWorkerCount(rows, scratch_.width());
    if (workers <= 1) {
        convert_rows(0, rows);
        return;
    }

    const int band = (rows + workers - 1) / workers;
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(workers) - 1);
    for (int w = 1; w < workers; ++w) {
        const int y_begin = std::min(rows, w * band);
        const int y_end = std::min(rows, y_begin + band);
        if (y_begin >= y_end) break;
        pool.emplace_back(convert_rows, y_begin, y_end);
    }
    convert_rows(0, std::min(rows, band));  // this thread takes the first band
    for (std::thread& worker : pool) worker.join();
}

void CanvasWidget::repaintImageRect(const PixelRect& region) {
    const QPointF top_left = widgetFromImage({static_cast<double>(region.x),
                                              static_cast<double>(region.y)});
    const QPointF bottom_right =
        widgetFromImage({static_cast<double>(region.x + region.width),
                         static_cast<double>(region.y + region.height)});
    update(QRectF(top_left, bottom_right).normalized().adjusted(-2, -2, 2, 2).toAlignedRect());
}

void CanvasWidget::resizeEvent(QResizeEvent*) {
    ensureCacheCoversView();
    update();
}

void CanvasWidget::paintEvent(QPaintEvent* event) {
    ensureCacheCoversView();

    // All the compositing for this frame happens here, once, however many
    // edits arrived since the last paint.
    //
    // The colour is asked for here and never worked out here. What arrives is
    // whatever was solved by the time this ran; a fill landing later brings the
    // paint on itself.
    requestCtgFills();
    if (onion_dirty_) {
        rebuildOnion();
        onion_dirty_ = false;
        dirty_everything_ = true;
    }
    if (dirty_everything_) {
        refreshRegion(cached_region_);
        dirty_everything_ = false;
        pending_dirty_ = {};
    } else if (!pending_dirty_.isEmpty()) {
        refreshRegion(pending_dirty_);
        pending_dirty_ = {};
    }

    QPainter painter(this);
    painter.setClipRect(event->rect());
    painter.fillRect(event->rect(), QColor(48, 48, 52));

    if (display_.isNull()) return;

    // Nearest-neighbour when magnified far enough that the pixels are the thing
    // being looked at; smooth below that.
    //
    // Two corrections to what this used to be, both measured. The factor the
    // blit applies is `cache_step_ * zoom_`, not `zoom_`: with the cache held
    // at screen resolution that product is 1 at every zoom below 100% and the
    // zoom itself above, which is the number the question was always about.
    //
    // And the threshold was 1.0, which meant nearest-neighbour from 101%
    // upwards. At 109% that duplicates one pixel column in eleven -- a
    // staircase along every curve, invisible on an orthogonal edge, which is
    // exactly the shape of the complaint. Measured against the same curve drawn
    // at display size, it doubled the error a smooth blit gives. Nobody
    // inspecting pixels is doing it at 109%; by 300% they are, and there
    // nearest is the honest thing to show.
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          blitInterpolatesAt(cache_step_.ratio() * zoom_));

    // The rectangle the cached *entries* cover, not the integer rectangle round
    // them. With a fractional step an entry boundary falls between two image
    // pixels, so the cached region is up to a pixel wider than the entries in
    // it actually span -- and blitting into that stretches the cache by a part
    // in a thousand, which over the width of a window slides a curve a pixel
    // off where the drawing says it is. Both corners go through the view
    // transform for the same reason: a size computed from the entry count would
    // accumulate the same error.
    const long long first_column = cache_step_.entryAt(cached_region_.x);
    const long long first_row = cache_step_.entryAt(cached_region_.y);
    const QPointF origin = widgetFromImage(
        {cache_step_.entryEdge(first_column), cache_step_.entryEdge(first_row)});
    const QPointF corner =
        widgetFromImage({cache_step_.entryEdge(first_column + display_.width()),
                         cache_step_.entryEdge(first_row + display_.height())});
    painter.drawImage(QRectF(origin, corner), display_);

    drawCanvasFrame(painter);

    // Dim what is not moving.
    //
    // Selecting on one layer while looking at a composite of every track is a
    // real surprise: you loop around a character and only the ink lifts. The
    // veil is what says which of the things on screen the gesture is about, and
    // it goes under the float rather than over it for the same reason.
    if (transform_) {
        painter.fillRect(rect(), QColor(255, 255, 255, 110));
        drawTransformPreview(painter);
    } else {
        drawSelection(painter);
    }

    // Over everything, because it is where the hand is rather than part of the
    // picture.
    drawToolRing(painter);
}

// The canvas: the rectangle that will be exported, outlined, with everything
// outside it veiled unless the veil has been turned off.
//
// Drawing outside stays allowed and is not discouraged -- roughs run off the
// edge, and a surface with no edges is the point of the tile model. But the
// picture has a boundary, and until it was drawn there was no way to know where
// it was: the only rectangle on screen was the region a colour fill happened to
// solve, which looked like a canvas and was not one.
//
// Which is why the outline is drawn whether or not the veil is. Hiding the veil
// is about seeing what runs off the edge at full strength, not about forgetting
// where the edge is.
void CanvasWidget::drawCanvasFrame(QPainter& painter) {
    const PixelRect canvas = doc_.scene().canvas();
    if (canvas.isEmpty()) return;

    const QPointF top_left = widgetFromImage({static_cast<double>(canvas.x),
                                              static_cast<double>(canvas.y)});
    const QPointF bottom_right =
        widgetFromImage({static_cast<double>(canvas.x + canvas.width),
                         static_cast<double>(canvas.y + canvas.height)});
    const QRectF frame(top_left, bottom_right);

    painter.save();
    if (passe_partout_) {
        // Odd-even filling, so the inner rectangle punches a hole in the outer
        // one and the veil covers everything except the picture.
        QPainterPath outside;
        outside.addRect(QRectF(rect()));
        outside.addRect(frame);

        painter.setPen(Qt::NoPen);
        painter.fillPath(outside, QColor(28, 28, 32, 150));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(150, 150, 160), 1.0));
    painter.drawRect(frame);
    painter.restore();
}

// --- selection -----------------------------------------------------------

void CanvasWidget::setLassoing(bool lassoing) {
    if (lassoing_ == lassoing) return;
    lassoing_ = lassoing;
    drawing_lasso_ = false;
    refreshPointer();
    update();
}

void CanvasWidget::clearSelection() {
    if (selection_.isEmpty()) return;
    selection_ = Selection{};
    update();
    Q_EMIT selectionChanged();
}

void CanvasWidget::selectEverything() {
    const Cel* cel = doc_.celAt(track_, image_, active_layer_);
    const PixelRect bounds = cel ? paintedBounds(cel->tiles()) : PixelRect{};
    if (bounds.isEmpty()) {
        clearSelection();
        return;
    }

    const double left = bounds.x;
    const double top = bounds.y;
    const double right = bounds.x + bounds.width;
    const double bottom = bounds.y + bounds.height;
    selection_ = Selection{{{left, top}, {right, top}, {right, bottom}, {left, bottom}}};
    update();
    Q_EMIT selectionChanged();
}

// The active layer split along the loop. With no loop everything is lifted and
// nothing stays, which is what makes the Transform tool's two cases one path.
Lift CanvasWidget::liftForTransform() const {
    const Cel* cel = doc_.celAt(track_, image_, active_layer_);
    if (!cel) return {};

    if (selection_.isEmpty()) {
        Lift whole;
        whole.lifted = cel->tiles();
        return whole;
    }
    return liftThrough(cel->tiles(), rasterise(selection_, paintedBounds(cel->tiles())));
}

bool CanvasWidget::eraseSelection() {
    if (selection_.isEmpty() || track_ == kNoId || image_ == kNoId) return false;

    const Track* track = doc_.scene().findTrack(track_);
    const Layer* layer = track ? track->findLayer(active_layer_) : nullptr;
    if (!layer || layer->locked || !layer->visible) return false;

    const Cel* cel = doc_.celAt(track_, image_, active_layer_);
    if (!cel) return false;
    const Lift split = liftThrough(cel->tiles(), rasterise(selection_, paintedBounds(cel->tiles())));
    if (split.lifted.empty()) return false;  // the loop covered no ink

    {
        ScopedCommand command(doc_, "Erase selection");
        if (Cel* writable = doc_.celForWriting(track_, image_, active_layer_)) {
            writable->replaceTiles(split.remaining, doc_.journal());
        }
    }

    clearSelection();
    refreshAll();
    Q_EMIT documentChanged();
    return true;
}

void CanvasWidget::beginLasso(const QPointF& widget_point) {
    drawing_lasso_ = true;
    lasso_passed_threshold_ = false;
    lasso_press_widget_ = widget_point;

    const QPointF at = imageFromWidget(widget_point);
    selection_ = Selection{{{at.x(), at.y()}}};
    update();
}

void CanvasWidget::extendLasso(const QPointF& widget_point) {
    if (!drawing_lasso_) return;

    // The ordinary drag threshold, in *screen* pixels, so it means the same
    // thing at every zoom -- and never a threshold on the loop's area. A
    // legitimate selection can be a single eyelash: long, thin, and near-zero
    // area. A click clears the selection; a drag makes one however small it is.
    if (!lasso_passed_threshold_ &&
        QLineF(lasso_press_widget_, widget_point).length() < kDragThreshold) {
        return;
    }
    lasso_passed_threshold_ = true;

    const QPointF at = imageFromWidget(widget_point);
    // Points closer together than a pixel say nothing the one before did not.
    if (!selection_.loop.empty()) {
        const Vec2& last = selection_.loop.back();
        if (std::abs(last.x - at.x()) < 1.0 && std::abs(last.y - at.y()) < 1.0) return;
    }
    selection_.loop.push_back({at.x(), at.y()});
    update();
}

void CanvasWidget::endLasso() {
    if (!drawing_lasso_) return;
    drawing_lasso_ = false;

    if (!lasso_passed_threshold_) {
        // A click, which clears. Nothing is lost that cannot be recreated in
        // two seconds, which is the whole reason a selection can be this cheap.
        selection_ = Selection{};
        update();
        Q_EMIT selectionChanged();
        return;
    }

    // An empty lasso must not become select-all. A loop enclosing no
    // non-transparent pixel is the same as no selection -- there is nothing to
    // lift -- but "no selection" also means "transform everything", so a stray
    // loop over blank paper would quietly become a whole-drawing transform.
    // Clear it, and stop.
    const Cel* cel = doc_.celAt(track_, image_, active_layer_);
    const Lift split =
        cel ? liftThrough(cel->tiles(), rasterise(selection_, paintedBounds(cel->tiles())))
            : Lift{};
    if (split.lifted.empty()) selection_ = Selection{};

    update();
    Q_EMIT selectionChanged();
}

void CanvasWidget::drawSelection(QPainter& painter) const {
    if (selection_.loop.size() < 2) return;

    QPolygonF loop;
    for (const Vec2& point : selection_.loop) {
        loop << widgetFromImage(QPointF(point.x, point.y));
    }

    painter.save();
    painter.setBrush(Qt::NoBrush);
    // Two passes, light under dark: a one-colour outline disappears against
    // whichever of paper and ink it happens to cross, and a lasso crosses both
    // by definition.
    painter.setPen(QPen(QColor(255, 255, 255, 200), 3.0));
    painter.drawPolygon(loop);
    QPen dashes(QColor(20, 20, 20), 1.0, Qt::DashLine);
    painter.setPen(dashes);
    painter.drawPolygon(loop);
    painter.restore();
}

// --- clipboard -----------------------------------------------------------

// Everything copy, cut and paste have to check before they touch anything: the
// same list the brush checks, plus the layer kind.
CanvasWidget::Refusal CanvasWidget::refuseHere() const {
    if (track_ == kNoId || image_ == kNoId) return Refusal::NoDrawing;

    const Track* track = doc_.scene().findTrack(track_);
    const Layer* layer = track ? track->findLayer(active_layer_) : nullptr;
    if (!layer) return Refusal::NoLayer;
    if (layer->kind == LayerKind::Ctg) return Refusal::ColourLayer;
    if (layer->locked) return Refusal::LockedLayer;
    if (!layer->visible) return Refusal::HiddenLayer;
    return Refusal::None;
}

CanvasWidget::Refusal CanvasWidget::copySelection() {
    const Refusal refusal = refuseHere();
    if (refusal != Refusal::None) return refusal;

    Lift split = liftForTransform();
    if (split.lifted.empty()) return Refusal::NothingDrawn;

    clipboard_ = std::move(split.lifted);
    clipboard_kind_ = LayerKind::Raster;
    return Refusal::None;
}

CanvasWidget::Refusal CanvasWidget::cutSelection() {
    const Refusal refusal = refuseHere();
    if (refusal != Refusal::None) return refusal;

    Lift split = liftForTransform();
    if (split.lifted.empty()) return Refusal::NothingDrawn;

    {
        ScopedCommand command(doc_, "Cut");
        if (Cel* cel = doc_.celForWriting(track_, image_, active_layer_)) {
            cel->replaceTiles(split.remaining, doc_.journal());
        }
    }

    clipboard_ = std::move(split.lifted);
    clipboard_kind_ = LayerKind::Raster;
    clearSelection();
    refreshAll();
    Q_EMIT documentChanged();
    return Refusal::None;
}

CanvasWidget::Refusal CanvasWidget::paste() {
    if (transform_) applyTransform();

    const Refusal refusal = refuseHere();
    if (refusal != Refusal::None) return refusal;
    if (clipboard_.empty()) return Refusal::NothingCopied;

    const Track* track = doc_.scene().findTrack(track_);
    const Layer* layer = track ? track->findLayer(active_layer_) : nullptr;
    if (!layer || layer->kind != clipboard_kind_) return Refusal::DifferentLayerKind;

    const PixelRect bounds = paintedBounds(clipboard_);
    if (bounds.isEmpty()) return Refusal::NothingCopied;

    LiveTransform live;
    live.track = track_;
    live.image = image_;
    live.layer = active_layer_;
    live.bounds = bounds;
    live.lifted = clipboard_;
    // Nothing was taken out of the drawing, so what stands in the layer's place
    // is the whole of it. That one line is the entire difference between a paste
    // and a transform.
    const Cel* cel = doc_.celAt(track_, image_, active_layer_);
    if (cel) live.remaining = cel->tiles();
    live.pasted = true;
    live.values.pivot_x = bounds.x + bounds.width / 2.0;
    live.values.pivot_y = bounds.y + bounds.height / 2.0;
    transform_ = std::move(live);

    buildTransformPicture();
    refreshAll();
    refreshPointer();
    Q_EMIT transformBegan();
    return Refusal::None;
}

// --- transform -----------------------------------------------------------

QString CanvasWidget::explain(Refusal refusal) {
    switch (refusal) {
        case Refusal::None: return {};
        case Refusal::NoDrawing:
            return QStringLiteral("there is no drawing here to transform");
        case Refusal::NoLayer: return QStringLiteral("no layer is selected");
        case Refusal::LockedLayer: return QStringLiteral("that layer is locked");
        case Refusal::HiddenLayer: return QStringLiteral("that layer is hidden");
        case Refusal::ColourLayer:
            return QStringLiteral("a colour layer holds labels rather than paint, so there is "
                                  "nothing here that can be resampled");
        case Refusal::NothingDrawn:
            return QStringLiteral("nothing is drawn on this layer");
        case Refusal::NothingCopied: return QStringLiteral("nothing has been copied");
        case Refusal::DifferentLayerKind:
            return QStringLiteral("what was copied came off a different kind of layer");
    }
    return {};
}

CanvasWidget::Refusal CanvasWidget::beginTransform() {
    if (transform_) return Refusal::None;

    // Refuse where the brush refuses, and for the same reasons: past the end of
    // a track there is no slot and no cel and so nothing to edit, a locked or
    // hidden layer is not being drawn on either. Easy to forget here precisely
    // because this is not the brush.
    //
    // And on the layer kind, never on a guess about the pixels. A mark on a
    // colour layer is a label: alpha is exactly 0 or 1 and the colour is a key,
    // so any interpolation invents a third colour that competes for regions on
    // its own account -- and the transparent label is negative light, which
    // blended against a real colour classifies as transparent and swallows it.
    const Refusal refusal = refuseHere();
    if (refusal != Refusal::None) return refusal;

    // The selection, or the whole cel if there is none. One path and not two,
    // which is exactly what "the tool is the button" buys.
    Lift split = liftForTransform();
    // The ink's bounds and not the tiles': a box 128 pixels bigger than the
    // drawing on every side is a picture of the tile grid, which is an
    // implementation detail nobody asked to see. This is also what freeing
    // emptied tiles was a prerequisite for -- without it the box would still be
    // drawn round a mark that was rubbed out.
    const PixelRect bounds = paintedBounds(split.lifted);
    if (bounds.isEmpty()) return Refusal::NothingDrawn;

    LiveTransform live;
    live.track = track_;
    live.image = image_;
    live.layer = active_layer_;
    live.bounds = bounds;
    live.lifted = std::move(split.lifted);
    live.remaining = std::move(split.remaining);
    live.values.pivot_x = bounds.x + bounds.width / 2.0;
    live.values.pivot_y = bounds.y + bounds.height / 2.0;
    transform_ = std::move(live);

    buildTransformPicture();
    refreshAll();
    // Four outcomes of a press appear at once, so the question is asked again
    // before the hand has had to move to find out which of them is where.
    refreshPointer();
    Q_EMIT transformBegan();
    return Refusal::None;
}

// The float, built once.
//
// Bounded absolutely rather than by the window, which is the one place in this
// program that is the right way round: what is being held is one layer of one
// drawing, its size is known when the transform starts, and rebuilding it as the
// view moves would mean recompositing on every pan of a gesture whose whole
// point is to be looked at from several places. A drawing large enough to be
// reduced here previews slightly softer than it commits, which it does anyway.
void CanvasWidget::buildTransformPicture() {
    if (!transform_) return;
    LiveTransform& live = *transform_;

    constexpr int kLongestSide = 2048;
    const int longest = std::max(live.bounds.width, live.bounds.height);
    live.step = SampleStep::fromRatio(std::max(1.0, static_cast<double>(longest) / kLongestSide));
    live.covers = snapToSampleGrid(live.step, live.bounds);

    // The lifted half only, and through compositeGrids because these pixels are
    // not in the document and never will be until the transform is committed.
    const Track* track = doc_.scene().findTrack(live.track);
    const Layer* layer = track ? track->findLayer(live.layer) : nullptr;
    if (!layer) return;

    Framebuffer pixels;
    // The layer's own opacity is deliberately not applied: the preview shows
    // where the pixels are going, and dimming them for a layer setting would
    // make a transform on a half-opacity layer look like a transform that had
    // lost something.
    Layer opaque = *layer;
    opaque.opacity = 1.0f;
    const std::vector<LayerPass> pass{{&live.lifted, &opaque}};
    compositor_.compositeGrids(pass, live.covers, pixels, live.step);
    if (pixels.isEmpty()) {
        live.picture = QImage();
        return;
    }

    live.picture = QImage(pixels.width(), pixels.height(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < pixels.height(); ++y) {
        const Rgba* source = pixels.row(y);
        auto* destination = reinterpret_cast<QRgb*>(live.picture.scanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            const float alpha = std::clamp(source[x].a, 0.0f, 1.0f);
            if (alpha <= 0.0f) {
                destination[x] = 0u;
                continue;
            }
            // Unpremultiply, encode, premultiply again. Qt's premultiplied
            // format wants sRGB bytes scaled by alpha, and applying the curve to
            // an already-premultiplied number is a different quantity: it shows
            // as a rim of the wrong lightness round everything soft, which on
            // line art is the whole of the line.
            const int a = static_cast<int>(std::lround(alpha * 255.0f));
            const auto encode = [&](float channel) {
                return static_cast<int>(std::lround(toSrgbByte(channel / alpha) * alpha));
            };
            destination[x] = qRgba(encode(source[x].r), encode(source[x].g), encode(source[x].b),
                                   a);
        }
    }
}

void CanvasWidget::centreTransformPivot() {
    if (!transform_) return;
    const PixelRect& bounds = transform_->bounds;
    repivot(transform_->values, bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0);
}

Transform CanvasWidget::transformValues() const {
    return transform_ ? transform_->values : Transform{};
}

void CanvasWidget::setTransformValues(const Transform& values) {
    if (!transform_) return;
    const PixelRect& bounds = transform_->bounds;
    Transform wanted = values;
    // The fields are always about the middle of what was picked up, whatever
    // the last handle drag pivoted on. Assigned rather than repivoted: these
    // five numbers are the whole state, so there is nothing to preserve.
    wanted.pivot_x = bounds.x + bounds.width / 2.0;
    wanted.pivot_y = bounds.y + bounds.height / 2.0;
    transform_->values = wanted;
    update();
    Q_EMIT transformNumbersChanged();
}

void CanvasWidget::nudgeTransform(int dx, int dy) {
    if (!transform_) return;
    transform_->values.dx += dx;
    transform_->values.dy += dy;
    update();
    Q_EMIT transformNumbersChanged();
}

void CanvasWidget::applyTransform() {
    if (!transform_) return;

    // Taken and cleared before anything below runs. Committing writes the
    // document, which repaints, and a repaint that still saw a live transform
    // would draw the float over the pixels it had just become.
    const LiveTransform live = *transform_;
    transform_.reset();
    grab_ = Grab::None;

    // An identity writes nothing at all. Picking a drawing up, looking at it and
    // putting it back is not an edit, and it must not cost a resample or an
    // undo step -- a commit softens line art, so one that changed nothing would
    // be a pure loss.
    if (!live.values.isIdentity() || live.pasted) {
        ScopedCommand command(doc_, live.pasted ? "Paste" : "Transform");
        // Re-checked rather than trusted. Nothing in the interface can delete
        // the layer under a live transform today, and "cannot happen" is worth
        // being wrong about cheaply.
        if (Cel* cel = doc_.celForWriting(live.track, live.image, live.layer)) {
            // The moved half over the half that stayed. With no selection there
            // is nothing underneath and mergeOver hands the moved grid straight
            // back, which is what keeps a whole-drawing translation bit-exact.
            cel->replaceTiles(mergeOver(transformTiles(live.lifted, live.values), live.remaining),
                              doc_.journal());
        }
        // The loop described where those pixels were, and they are not there
        // any more. Keeping it would offer a second transform of a shape that
        // has moved out from under it.
        clearSelection();
    }

    refreshAll();
    refreshPointer();
    Q_EMIT documentChanged();
    Q_EMIT transformEnded();
}

void CanvasWidget::cancelTransform() {
    if (!transform_) return;
    transform_.reset();
    grab_ = Grab::None;
    refreshAll();
    refreshPointer();
    Q_EMIT transformEnded();
}

std::array<QPointF, 8> CanvasWidget::transformHandles() const {
    std::array<QPointF, 8> handles{};
    if (!transform_) return handles;

    const PixelRect& bounds = transform_->bounds;
    const Matrix m = matrixOf(transform_->values);

    const double left = bounds.x;
    const double top = bounds.y;
    const double right = bounds.x + bounds.width;
    const double bottom = bounds.y + bounds.height;
    const double middle_x = (left + right) / 2.0;
    const double middle_y = (top + bottom) / 2.0;

    // Clockwise from the top left, corners and edge middles alternating, so
    // that a handle's index says what it does: even is a corner, odd is an edge.
    const Vec2 in_image[8] = {{left, top},      {middle_x, top},    {right, top},
                              {right, middle_y}, {right, bottom},   {middle_x, bottom},
                              {left, bottom},   {left, middle_y}};

    for (std::size_t i = 0; i < 8; ++i) {
        const Vec2 moved = apply(m, in_image[i]);
        handles[i] = widgetFromImage(QPointF(moved.x, moved.y));
    }

    // Below about three handles across there is nowhere on the edge left to put
    // them, so they go outside it. Measured on screen and not in image pixels,
    // because that is where the hand is: the same box at 25% and at 400% zoom
    // is two completely different targets.
    const QPointF centre = (handles[0] + handles[4]) / 2.0;
    const double across = QLineF(handles[0], handles[2]).length();
    const double down = QLineF(handles[0], handles[6]).length();
    const double push_x = (across < 3 * kTransformHandleSize) ? kTransformHandleSize : 0.0;
    const double push_y = (down < 3 * kTransformHandleSize) ? kTransformHandleSize : 0.0;
    if (push_x > 0.0 || push_y > 0.0) {
        for (QPointF& handle : handles) {
            QPointF away = handle - centre;
            const double length = std::hypot(away.x(), away.y());
            if (length < 1e-6) continue;
            away /= length;
            handle += QPointF(away.x() * push_x, away.y() * push_y);
        }
    }
    return handles;
}

QPointF CanvasWidget::rotationHandleForTesting() const {
    if (!transform_) return {};
    return rotationGizmo(transformHandles());
}

QPointF CanvasWidget::transformCentreForTesting() const {
    if (!transform_) return {};
    const std::array<QPointF, 8> handles = transformHandles();
    return (handles[0] + handles[4]) / 2.0;
}

void CanvasWidget::drawTransformPreview(QPainter& painter) {
    const LiveTransform& live = *transform_;
    const Matrix m = matrixOf(live.values);

    if (!live.picture.isNull()) {
        // Three transforms, applied in order: the float's own pixels to image
        // coordinates, the transform itself, and the view. Qt composes
        // row-vector style, so the order here reads the same way the point
        // travels.
        const long long first_column = live.step.entryAt(live.covers.x);
        const long long first_row = live.step.entryAt(live.covers.y);
        const double ratio = live.step.ratio();
        const QTransform from_picture(ratio, 0.0, 0.0, ratio, live.step.entryEdge(first_column),
                                      live.step.entryEdge(first_row));
        const QTransform moving(m.a, m.c, m.b, m.d, m.tx, m.ty);
        const QPointF at = pan();
        const QTransform to_widget(zoom_, 0.0, 0.0, zoom_, -at.x() * zoom_, -at.y() * zoom_);

        painter.save();
        painter.setTransform(from_picture * moving * to_widget);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(QPointF(0.0, 0.0), live.picture);
        painter.restore();
    }

    // The box, and the handles on it.
    const std::array<QPointF, 8> handles = transformHandles();
    QPolygonF box;
    box << handles[0] << handles[2] << handles[4] << handles[6];

    // The stem out to the rotation knob, drawn with the box so that the knob
    // reads as part of it rather than as something floating nearby.
    const QPointF knob = rotationGizmo(handles);

    painter.save();
    painter.setBrush(Qt::NoBrush);
    // Light under dark, both here and on the stem: the box crosses paper and ink
    // by definition, and a one-colour outline disappears against one of them.
    painter.setPen(QPen(QColor(255, 255, 255, 160), 3.0));
    painter.drawPolygon(box);
    painter.drawLine(handles[1], knob);
    painter.setPen(QPen(QColor(60, 130, 240), 1.0));
    painter.drawPolygon(box);
    painter.drawLine(handles[1], knob);

    const double half = kTransformHandleSize / 2.0;
    for (const QPointF& handle : handles) {
        const QRectF square(handle.x() - half, handle.y() - half, kTransformHandleSize,
                            kTransformHandleSize);
        painter.setBrush(QColor(255, 255, 255));
        painter.setPen(QPen(QColor(60, 130, 240), 1.0));
        painter.drawRect(square);
    }

    // Round, where the eight that resize are square. Two shapes for two
    // operations, which is the only thing on the box saying that this one turns
    // the drawing rather than stretching it.
    painter.setBrush(QColor(255, 255, 255));
    painter.setPen(QPen(QColor(60, 130, 240), 1.0));
    painter.drawEllipse(knob, kTransformRotateKnob, kTransformRotateKnob);
    painter.restore();
}

// What a press on the box would grab.
//
// Asked by the press and by the pointer, which is the whole point of it being a
// function: dragging *at* a corner scales and dragging just outside one rotates,
// and while these were two pieces of code they looked identical on screen and
// there was nothing to say they were not the same answer.
//
// Handles are tested before the interior because they sometimes sit outside the
// box, and anything asking "is this inside" first would eat half of them.
CanvasWidget::BoxTarget CanvasWidget::boxTargetAt(const QPointF& widget_point) const {
    if (!transform_) return {};

    const std::array<QPointF, 8> handles = transformHandles();

    // The knob first: it is the one thing on the box that says what it does, and
    // it is drawn outside the box where nothing else is competing for the press.
    if (QLineF(widget_point, rotationGizmo(handles)).length() <=
        kTransformRotateKnob + kTransformHandleGrab) {
        return {Grab::Rotate, -1};
    }

    for (int i = 0; i < 8; ++i) {
        if (QLineF(widget_point, handles[static_cast<std::size_t>(i)]).length() <=
            kTransformHandleGrab) {
            return {Grab::Handle, i};
        }
    }

    // And a ring just outside each corner does the same as the knob, which is
    // where a hand reaches without being told. It stays now that the knob
    // exists -- that was asked and answered -- and what it needed was for the
    // pointer to say so, since a band nobody can see is otherwise a second
    // unlabelled way to do a labelled thing.
    for (int i = 0; i < 8; i += 2) {
        const double reach = QLineF(widget_point, handles[static_cast<std::size_t>(i)]).length();
        if (reach <= kTransformRotateBand) return {Grab::Rotate, -1};
    }

    QPolygonF box;
    box << handles[0] << handles[2] << handles[4] << handles[6];
    const bool roomy = QLineF(handles[0], handles[2]).length() > kTransformSmallestInterior &&
                       QLineF(handles[0], handles[6]).length() > kTransformSmallestInterior;
    if (roomy && box.containsPoint(widget_point, Qt::OddEvenFill)) return {Grab::Move, -1};

    return {};
}

// And the pivot the gesture wants, which is the half that writes something down.
bool CanvasWidget::beginTransformDrag(const QPointF& widget_point) {
    if (!transform_) return false;

    const BoxTarget target = boxTargetAt(widget_point);
    if (target.grab == Grab::None) return false;

    grab_image_ = imageFromWidget(widget_point);
    if (target.grab == Grab::Handle) {
        // Scale about the handle opposite, which is what makes dragging one
        // corner leave the other exactly where it was. Letting the handle decide
        // what a drag means is what frees Shift to constrain the rotation to
        // fifteen-degree steps and a move to an axis.
        const Vec2 anchor = handleInImage(transform_->bounds, target.handle + 4);
        repivot(transform_->values, anchor.x, anchor.y);
    } else {
        centreTransformPivot();
    }

    grab_values_ = transform_->values;
    grab_ = target.grab;
    grabbed_handle_ = target.grab == Grab::Handle ? target.handle : -1;
    refreshPointer();
    return true;
}

bool CanvasWidget::continueTransformDrag(const QPointF& widget_point) {
    if (!transform_ || grab_ == Grab::None) return false;

    const QPointF now = imageFromWidget(widget_point);
    const bool constrained = (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
    Transform values = grab_values_;

    switch (grab_) {
        case Grab::Move: {
            double moved_x = now.x() - grab_image_.x();
            double moved_y = now.y() - grab_image_.y();
            if (constrained) {
                if (std::abs(moved_x) >= std::abs(moved_y)) {
                    moved_y = 0.0;
                } else {
                    moved_x = 0.0;
                }
            }
            // Whole pixels. A drag that lands half a pixel off resamples the
            // whole drawing for a placement nobody could have aimed at, and
            // registration is the transform this is mostly for.
            values.dx = grab_values_.dx + std::round(moved_x);
            values.dy = grab_values_.dy + std::round(moved_y);
            break;
        }

        case Grab::Rotate: {
            // About where the middle of the box is now, which is the pivot plus
            // the translation: the pivot maps to itself under rotation and
            // scale, so that is the one point the box turns around on screen.
            const double centre_x = values.pivot_x + values.dx;
            const double centre_y = values.pivot_y + values.dy;
            const double was = std::atan2(grab_image_.y() - centre_y, grab_image_.x() - centre_x);
            const double is = std::atan2(now.y() - centre_y, now.x() - centre_x);
            double turned = grab_values_.rotation + (is - was) / kRadiansPerDegree;
            if (constrained) turned = std::round(turned / kRotationSnap) * kRotationSnap;
            values.rotation = turned;
            break;
        }

        case Grab::Handle: {
            const Vec2 handle = handleInImage(transform_->bounds, grabbed_handle_);
            const Vec2 arm{handle.x - values.pivot_x, handle.y - values.pivot_y};

            // Where the handle has to land, measured before the rotation is
            // applied: what is left once the turn is taken back out is exactly
            // the scale, because scale * arm is the only thing between them.
            const double radians = -values.rotation * kRadiansPerDegree;
            const double cosine = std::cos(radians);
            const double sine = std::sin(radians);
            const double to_x = now.x() - values.pivot_x - values.dx;
            const double to_y = now.y() - values.pivot_y - values.dy;
            const Vec2 wanted{cosine * to_x - sine * to_y, sine * to_x + cosine * to_y};

            if ((grabbed_handle_ & 1) == 0) {
                // A corner scales both axes by one factor, and the factor is
                // measured against where the handle was rather than against the
                // box -- a drawing already squashed to half height stays
                // squashed when a corner grows it, which is what "uniform"
                // means once the two axes can differ at all.
                const Vec2 from{grab_values_.scale_x * arm.x, grab_values_.scale_y * arm.y};
                const double along = from.x * from.x + from.y * from.y;
                if (along > 1e-9) {
                    const double factor = (wanted.x * from.x + wanted.y * from.y) / along;
                    values.scale_x = std::max(kSmallestScale, grab_values_.scale_x * factor);
                    values.scale_y = std::max(kSmallestScale, grab_values_.scale_y * factor);
                }
            } else if (std::abs(arm.x) > std::abs(arm.y)) {
                values.scale_x = std::max(kSmallestScale, wanted.x / arm.x);
            } else if (std::abs(arm.y) > 1e-9) {
                values.scale_y = std::max(kSmallestScale, wanted.y / arm.y);
            }
            break;
        }

        case Grab::None: return false;
    }

    transform_->values = values;
    update();
    Q_EMIT transformNumbersChanged();
    return true;
}

void CanvasWidget::endTransformDrag() {
    if (grab_ == Grab::None) return;
    grab_ = Grab::None;
    grabbed_handle_ = -1;
    // Back to the middle between gestures, so that what the numeric fields say
    // means one thing however the last drag pivoted.
    centreTransformPivot();
    update();
    refreshPointer();
    Q_EMIT transformNumbersChanged();
}

// --- the pointer ---------------------------------------------------------

// What a press would do here, asked of everything that is true at once.
//
// The order is the order a press resolves in, and it has to be: a pointer that
// answers a different question from the press under it is worse than the
// crosshair this replaced, which at least never claimed anything.
CanvasWidget::Pointing CanvasWidget::pointingAt(const QPointF& widget_point) const {
    // A gesture already under way outranks the rest. What is beneath the
    // pointer stopped mattering when the button went down, and this is the half
    // that was getting lost: the cursor was put back by hand at the end of each
    // gesture, from a chain of held-key tests repeated at three call sites, and
    // a path that forgot one left the closed hand on screen.
    if (panning_) return Pointing::Panning;
    if (zooming_) return Pointing::Zoom;
    if (sizing_) return Pointing::SizeBrush;
    if (picking_) return Pointing::Pick;

    // Then the held keys, which say what a press will do wherever it lands --
    // including over a transform box, because navigation is available inside
    // every tool and the box would otherwise appear to swallow it.
    if (space_held_) return Pointing::PanReady;
    if (zoom_key_held_) return Pointing::Zoom;
    if (alt_held_) return Pointing::Pick;

    if (transform_) {
        // Mid-drag the answer is what was grabbed and not what is underneath:
        // a corner handle dragged past the opposite one leaves the pointer
        // nowhere near the box, and the gesture is still a scale.
        const BoxTarget target =
            grab_ == Grab::None ? boxTargetAt(widget_point) : BoxTarget{grab_, grabbed_handle_};
        switch (target.grab) {
            case Grab::Move: return Pointing::Move;
            case Grab::Rotate: return Pointing::Rotate;
            case Grab::Handle: return scalePointingFor(target.handle, transform_->values.rotation);
            case Grab::None: break;
        }
        // Off the box, where a press does nothing at all. That is the fourth
        // outcome the crosshair used to be shown for, and the only one of the
        // four that is worth saying with a cursor from another family.
        return Pointing::Nothing;
    }

    if (lassoing_) return Pointing::Lasso;
    // The pen turned over is the eraser as much as the button is, and it is the
    // case with nothing else on screen to announce it.
    return (erasing_ || hover_eraser_) ? Pointing::Erase : Pointing::Draw;
}

void CanvasWidget::updatePointerAt(const QPointF& widget_point) {
    pointer_at_ = widget_point;
    refreshPointer();
}

void CanvasWidget::refreshPointer() {
    const Pointing now = pointingAt(pointer_at_);
    // Only when the answer changes. Every mouse move comes through here, and a
    // cursor is set through the platform rather than into a variable. Empty
    // until the first call, which is what lets the constructor use this instead
    // of being a second place that knows what a cursor is.
    if (pointing_ != now) {
        pointing_ = now;
        setCursor(cursorFor(now));
    }
    updateToolRing();
}

std::optional<CanvasWidget::ToolRing> CanvasWidget::toolRing() const {
    // Only while the radius is being dragged, and there is a rule behind that
    // rather than a preference. What the widget draws is a frame behind where
    // the pointer is, so nothing drawn here can follow a pointer without
    // trailing it -- which is why the eraser is a cursor and this is not.
    //
    // This one is anchored to where the drag began and holds still while the
    // pointer moves away from it, so there is nothing to trail: the pointer is
    // measuring a distance out from that point and the circle is what the
    // distance means. A ring that travelled with it would also be the one thing
    // on screen not holding still to be compared against.
    if (!sizing_) return std::nullopt;
    const BrushSettings& tool = erasing_ ? eraser_settings_ : brush_settings_;
    return ToolRing{size_anchor_widget_, tool.radius * zoom_};
}

// What the ring covers on screen, or an empty rectangle when there is none.
QRect CanvasWidget::toolRingRect() const {
    const std::optional<ToolRing> ring = toolRing();
    if (!ring || ring->radius < kSmallestToolRing) return {};
    const double reach = ring->radius + 3.0;  // the outline has a width of its own
    return QRectF(ring->at.x() - reach, ring->at.y() - reach, 2 * reach, 2 * reach)
        .toAlignedRect();
}

void CanvasWidget::updateToolRing() {
    const QRect wanted = toolRingRect();
    if (wanted == ring_drawn_) return;

    // Both rectangles, because the ring has to come off where it was as well as
    // go on where it is. Repainting the whole widget per mouse move would be the
    // easy version, and it would recomposite the viewport on every one of them.
    if (!ring_drawn_.isNull()) update(ring_drawn_);
    if (!wanted.isNull()) update(wanted);
    ring_drawn_ = wanted;
}

void CanvasWidget::drawToolRing(QPainter& painter) {
    const std::optional<ToolRing> ring = toolRing();
    // Where the ring is now, recorded by the paint that drew it rather than by
    // whatever asked for that paint. A zoom changes the radius on screen
    // without the pointer moving at all, and something has to know where the
    // last one went in order to take it off.
    ring_drawn_ = toolRingRect();
    if (!ring || ring->radius < kSmallestToolRing) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    // Light under dark, the same rule as the transform box: a circle over line
    // art crosses paper and ink by definition.
    painter.setPen(QPen(QColor(255, 255, 255, 170), 3.0));
    painter.drawEllipse(ring->at, ring->radius, ring->radius);
    painter.setPen(QPen(QColor(30, 30, 34), 1.0));
    painter.drawEllipse(ring->at, ring->radius, ring->radius);
    painter.restore();
}

// --- picking -------------------------------------------------------------

// Samples the flattened drawing under the pointer, in the space it is stored
// in, and hands the colour to whoever is listening.
//
// This is the eyedropper, and it is here rather than on the colour dialog's
// "pick screen colour" for a reason that is not a matter of taste. That picker
// only ever hears about mouse buttons, and on Windows a pen produces none it can
// hear: Qt routes pen input as a tablet event and then discards the legacy mouse
// messages Windows promotes from it, so the click never arrives however the
// canvas behaves. Hence colours could be picked with a mouse and not with a pen.
//
// Sampling the document is the better answer regardless. The screen picker reads
// back what the monitor was showing -- after sRGB encoding to eight bits, after
// the zoom filter, with the onion skin and the paper mixed into it -- and calls
// the result the colour you drew with. This reads the colour that was stored.
//
// The paper is not part of the drawing, so an empty pixel has nothing to pick
// and the brush is left alone rather than being set to white.
// Every track, because picking has to answer for the pixel you pointed at and
// the pixel you pointed at may belong to a track you are not editing.
bool CanvasWidget::pickColourAt(const QPointF& image_point) {
    const PixelRect one{static_cast<int>(std::floor(image_point.x())),
                        static_cast<int>(std::floor(image_point.y())), 1, 1};
    Framebuffer sample;
    // Through whatever a live transform has left standing in the layer's place,
    // for the same reason the display cache is: those pixels are not there any
    // more, and picking a colour off them would be picking a colour off a
    // drawing that is no longer under the pointer.
    const SubstitutedLayer substituted =
        transform_ ? SubstitutedLayer{transform_->layer, &transform_->remaining}
                   : SubstitutedLayer{};
    compositor_.compositeScene(doc_, slot_, one, sample, SampleStep{}, substituted);
    if (sample.isEmpty()) return false;

    // A CTG layer contributes the fill it last generated, which is what is on
    // screen and so what picking from it should mean.
    const Rgba pixel = sample.pixel(0, 0);
    if (pixel.a < 0.01f) return false;

    Q_EMIT colourPicked(pixel.r / pixel.a, pixel.g / pixel.a, pixel.b / pixel.a);
    return true;
}

// --- strokes -------------------------------------------------------------

void CanvasWidget::beginStroke(const QPointF& image_point, float pressure) {
    if (track_ == kNoId || image_ == kNoId || active_layer_ == kNoId) return;

    const Track* track = doc_.scene().findTrack(track_);
    const Layer* layer = track ? track->findLayer(active_layer_) : nullptr;
    if (!layer || layer->locked || !layer->visible) return;

    BrushSettings settings = (stylus_eraser_ || erasing_) ? eraser_settings_ : brush_settings_;
    settings.erase = stylus_eraser_ || erasing_;

    // On a CTG layer the stroke is a label, not paint. Pressure must not thin
    // the mark into a half-vote for a colour, and a soft rim would be read as
    // scribbled or not depending on a threshold, which is no way to decide --
    // so the rim is not written at all. See BrushSettings::label.
    if (layer->kind == LayerKind::Ctg) {
        settings.pressure_affects_opacity = false;
        settings.hardness = 1.0f;
        settings.opacity = 1.0f;
        settings.label = true;
    } else if (!settings.erase &&
               isTransparentScribble(Rgba{settings.r, settings.g, settings.b, 1.0f})) {
        // The transparent label is a scribble, not paint: on a raster layer it
        // would be a stroke of negative light. The interface puts the colour
        // back when you leave a colour layer, so this cannot happen -- it is
        // here because "cannot happen" is worth being wrong about cheaply, and
        // the alternative is pixels no filter will ever make sense of.
        return;
    }
    brush_.settings() = settings;

    doc_.beginCommand(settings.erase ? "Erase" : "Stroke");
    stroking_ = true;

    brush_.begin(doc_, track_, image_, active_layer_,
                 {static_cast<float>(image_point.x()), static_cast<float>(image_point.y()),
                  pressure});

    last_image_point_ = image_point;
    last_pressure_ = pressure;
    scribbling_ = layer->kind == LayerKind::Ctg;
    if (scribbling_) setScribblePreview(active_layer_, true);

    // A scribble changes the fill across the whole region, not just where the
    // pen went, so there is nothing useful to mark dirty. Repainting only the
    // stroke's own rectangle left the rest of the region showing the previous
    // colour until some unrelated event forced a repaint -- which looked for
    // all the world like paint left on the brush.
    if (scribbling_) {
        refreshAll();
        return;
    }

    const int radius = static_cast<int>(std::ceil(settings.radius)) + 2;
    markDirty({static_cast<int>(std::floor(image_point.x())) - radius,
               static_cast<int>(std::floor(image_point.y())) - radius, 2 * radius, 2 * radius});
    repaintImageRect({static_cast<int>(std::floor(image_point.x())) - radius,
                      static_cast<int>(std::floor(image_point.y())) - radius, 2 * radius,
                      2 * radius});
}

void CanvasWidget::extendStroke(const QPointF& image_point, float pressure) {
    if (!stroking_) return;

    brush_.extend({static_cast<float>(image_point.x()), static_cast<float>(image_point.y()),
                   pressure});

    if (scribbling_) {
        last_image_point_ = image_point;
        last_pressure_ = pressure;
        refreshAll();
        return;
    }

    const int radius = static_cast<int>(std::ceil(brush_.settings().radius)) + 2;
    const QRectF segment = QRectF(last_image_point_, image_point).normalized();
    const PixelRect dirty{static_cast<int>(std::floor(segment.left())) - radius,
                          static_cast<int>(std::floor(segment.top())) - radius,
                          static_cast<int>(std::ceil(segment.width())) + 2 * radius,
                          static_cast<int>(std::ceil(segment.height())) + 2 * radius};
    markDirty(dirty);
    repaintImageRect(dirty);

    last_image_point_ = image_point;
    last_pressure_ = pressure;
}

// The frame moved under an active stroke. Close the brush on the old drawing
// and reopen it on the new one at the same place, inside the same command, so
// the whole gesture is still a single undo step.
void CanvasWidget::rebindStrokeToCurrentImage() {
    brush_.end();
    if (image_ == kNoId || active_layer_ == kNoId) {
        stroking_ = false;
        doc_.endCommand();
        return;
    }
    brush_.begin(doc_, track_, image_, active_layer_,
                 {static_cast<float>(last_image_point_.x()),
                  static_cast<float>(last_image_point_.y()), last_pressure_});
}

void CanvasWidget::endStroke() {
    if (!stroking_) return;
    brush_.end();
    doc_.endCommand();
    stroking_ = false;
    stylus_eraser_ = false;

    if (scribbling_) {
        scribbling_ = false;
        setScribblePreview(scribble_preview_layer_, false);
    }

    // The pen lifting is what triggers the solve, so the fill has to be rebuilt
    // and redrawn even though nothing else has changed. That holds whichever
    // layer was drawn on: a stroke on the line art moves the boundary a colour
    // is cut against just as surely as a scribble does.
    refreshAll();
    Q_EMIT documentChanged();
}

// --- navigation ----------------------------------------------------------

// The modifiers come from the event rather than from the keyboard. They used to
// be read with QGuiApplication::keyboardModifiers(), which answers for the
// machine and not for the event -- so the resize gesture could not be driven by
// a test at all, and #5 was about a gesture nothing was watching.
bool CanvasWidget::beginNavigation(const QPointF& widget_point, Qt::MouseButton button,
                                   Qt::KeyboardModifiers modifiers) {
    // Alt and the right button, dragged sideways, resizes the brush without
    // leaving the drawing -- the gesture Photoshop and Krita already taught
    // everyone's hands.
    if (button == Qt::RightButton && (modifiers & Qt::AltModifier)) {
        sizing_ = true;
        size_anchor_widget_ = widget_point;
        radius_at_press_ = brushSettings().radius;
        refreshPointer();
        return true;
    }
    if (zoom_key_held_) {
        zooming_ = true;
        zoom_anchor_widget_ = widget_point;
        zoom_at_press_ = zoom_;
        refreshPointer();
        return true;
    }
    if (button == Qt::MiddleButton || (space_held_ && button == Qt::LeftButton)) {
        panning_ = true;
        pan_anchor_widget_ = widget_point;
        pan_anchor_image_ = pan_;
        refreshPointer();
        return true;
    }
    return false;
}

bool CanvasWidget::continueNavigation(const QPointF& widget_point) {
    if (sizing_) {
        const double dx = widget_point.x() - size_anchor_widget_.x();
        const float radius = static_cast<float>(
            std::clamp(radius_at_press_ * std::exp(dx * kSizeDragPerPixel), 0.5, 400.0));
        brushSettings().radius = radius;
        // The ring is the whole of what this gesture shows, so it is put back on
        // screen here rather than waiting for the next thing to repaint.
        updateToolRing();
        Q_EMIT brushSizeChanged(radius);
        return true;
    }
    if (zooming_) {
        // Scrubby zoom: drag right to come in, left to go out, about the point
        // the drag started from.
        const double dx = widget_point.x() - zoom_anchor_widget_.x();
        setZoom(zoom_at_press_ * std::exp(dx * kScrubbyZoomPerPixel), zoom_anchor_widget_);
        return true;
    }
    if (panning_) {
        // Absolute from where the drag began, so this one never accumulated --
        // but it stores an exact pan now like everything else, and lets pan()
        // do the aligning. One rule about where rounding happens is easier to
        // keep than three call sites that each remember to.
        const QPointF moved = widget_point - pan_anchor_widget_;
        pan_ = {pan_anchor_image_.x() - moved.x() / zoom_,
                pan_anchor_image_.y() - moved.y() / zoom_};
        ensureCacheCoversView();
        update();
        Q_EMIT viewChanged();
        return true;
    }
    return false;
}

void CanvasWidget::endNavigation() {
    if (!panning_ && !zooming_ && !sizing_) return;
    panning_ = false;
    zooming_ = false;
    sizing_ = false;
    // This used to be the held-key chain written out by hand, and two others
    // like it were elsewhere. Whatever is true now is what the pointer says now.
    refreshPointer();
}

// --- input ---------------------------------------------------------------

void CanvasWidget::tabletEvent(QTabletEvent* event) {
    // Qt drops mouse events aimed at a window a modal dialog has blocked. It
    // does not do the same for tablet events, which are delivered by hit test
    // and arrive here regardless -- so the pen drew on the canvas behind an
    // open dialog, and took the keyboard focus off it on the way in. Leave the
    // event alone: whatever is modal has a better claim on the pen than we do.
    if (QApplication::activeModalWidget()) {
        event->ignore();
        return;
    }

    // A control sitting on the canvas gets the pen, and this is what lets it.
    //
    // The transform bar is a child widget, so it floats over the drawing rather
    // than taking a row of the window. A QSpinBox has no tabletEvent, so it
    // ignores the pen and Qt propagates the event to the parent -- here,
    // translated into our coordinates -- and accepting it would do two wrong
    // things at once: the press would start a transform drag on the canvas
    // underneath, and Qt only synthesises a mouse event for a tablet event that
    // *nobody* accepted, so the button would never be clickable with a pen at
    // all. It was not, and it was reported.
    //
    // Every other panel in the window is a sibling rather than a child, which is
    // why none of them ever needed this.
    if (childAt(event->position().toPoint()) != nullptr) {
        event->ignore();
        return;
    }

    event->accept();
    last_tablet_ms_ = clock_.elapsed();

    // Qt gives click-focus for a mouse press but not for a tablet press, so an
    // artist who has touched a spin box in the toolbar never gets the keyboard
    // back by drawing -- and the spin box's line edit then eats B and E as text
    // rather than letting them switch tool. Taken explicitly, for both devices.
    if (event->type() == QEvent::TabletPress) setFocus(Qt::MouseFocusReason);

    const QPointF widget_point = event->position();

    // Which way up the pen is, on every event and not only on a press. Turning
    // it over is the one way of reaching for the eraser that changes nothing on
    // screen, which is half of what #4 was about.
    if (const QPointingDevice* hovering = event->pointingDevice()) {
        hover_eraser_ = hovering->pointerType() == QPointingDevice::PointerType::Eraser;
    }
    alt_held_ = (event->modifiers() & Qt::AltModifier) != 0;
    updatePointerAt(widget_point);

    // The tip, whatever button the platform calls it: the pen's own gestures
    // here are Space-drag and held Z, and both are the tip with a key down.
    if (event->type() == QEvent::TabletPress &&
        beginNavigation(widget_point, Qt::LeftButton, event->modifiers())) {
        return;
    }
    if ((panning_ || zooming_ || sizing_) && event->type() == QEvent::TabletMove) {
        continueNavigation(widget_point);
        return;
    }
    if ((panning_ || zooming_ || sizing_) && event->type() == QEvent::TabletRelease) {
        endNavigation();
        return;
    }

    // Alt and the tip picks the colour under it, the gesture every drawing
    // program already taught everyone's hands. Nothing is drawn and no command
    // is opened: picking a colour is not an edit.
    //
    // Taken when the pen lifts rather than when it lands, so the pen can be slid
    // onto the exact pixel while it is down. Landing a nib precisely is the hard
    // part; sliding it once it is on the tablet is not.
    //
    // Which is only worth having if you can see what you are sliding onto, so
    // the colour follows the pen the whole way down and the release simply stops
    // it moving. Sampling is a one-pixel composite, far below what a pen move
    // already costs, and nothing is being drawn meanwhile -- so the live value
    // can be the real brush colour rather than a preview of one, and there is
    // one path instead of two that have to agree.
    //
    // An empty pixel leaves the colour alone, here as everywhere: dragging out
    // over bare paper holds the last colour rather than snatching it away, so
    // the release commits what is shown even when it lands on nothing.
    if (event->type() == QEvent::TabletPress && (event->modifiers() & Qt::AltModifier)) {
        picking_ = true;
        pickColourAt(imageFromWidget(widget_point));
        return;
    }
    if (picking_) {
        if (event->type() == QEvent::TabletMove) {
            pickColourAt(imageFromWidget(widget_point));
        } else if (event->type() == QEvent::TabletRelease) {
            picking_ = false;
            pickColourAt(imageFromWidget(widget_point));
        }
        return;  // no stroke, whatever happened to Alt in the meantime
    }

    // A live transform, or the lasso, takes the pen: the tools are exclusive, so
    // while one of them holds it the brush is not competing for it.
    if (transform_) {
        switch (event->type()) {
            case QEvent::TabletPress: beginTransformDrag(widget_point); break;
            case QEvent::TabletMove: continueTransformDrag(widget_point); break;
            case QEvent::TabletRelease: endTransformDrag(); break;
            default: break;
        }
        return;
    }
    if (lassoing_) {
        switch (event->type()) {
            case QEvent::TabletPress: beginLasso(widget_point); break;
            case QEvent::TabletMove: extendLasso(widget_point); break;
            case QEvent::TabletRelease: endLasso(); break;
            default: break;
        }
        return;
    }

    const QPointingDevice* device = event->pointingDevice();
    if (event->type() == QEvent::TabletPress && device) {
        stylus_eraser_ = device->pointerType() == QPointingDevice::PointerType::Eraser;
    }

    const QPointF image_point = imageFromWidget(widget_point);
    const float pressure = static_cast<float>(event->pressure());

    switch (event->type()) {
        case QEvent::TabletPress: beginStroke(image_point, pressure); break;
        case QEvent::TabletMove:
            if (stroking_) extendStroke(image_point, pressure);
            break;
        case QEvent::TabletRelease: endStroke(); break;
        default: break;
    }
}

// Windows Ink promotes every pen event to a mouse event as well. Acting on
// those would draw the stroke a second time at an invented pressure.
//
// The question is "did a pen produce this event", so the device is asked first.
// It does not always say -- a promoted event arrives claiming to be the mouse --
// so the fallback is how long ago the pen was last heard from. That used to be
// "has this canvas ever seen a tablet event", which is true forever afterwards:
// touching the tablet once left the mouse unable to draw for the rest of the
// session, with nothing on screen to say why.
//
// The window only has to outlast the promotion, which follows its pen event
// immediately. No hand puts down a pen and clicks a mouse inside a quarter of a
// second, so nothing real is caught by it.
bool CanvasWidget::eventIsSynthesisedFromPen(QMouseEvent* event) const {
    if (comesFromAStylus(event->pointingDevice())) return true;
    if (last_tablet_ms_ < 0) return false;
    return clock_.elapsed() - last_tablet_ms_ < kPenMouseWindowMs;
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    alt_held_ = (event->modifiers() & Qt::AltModifier) != 0;
    if (!eventIsSynthesisedFromPen(event)) hover_eraser_ = false;
    updatePointerAt(event->position());
    if (beginNavigation(event->position(), event->button(), event->modifiers())) return;
    if (eventIsSynthesisedFromPen(event)) return;
    if (event->button() != Qt::LeftButton) return;
    if (event->modifiers() & Qt::AltModifier) {
        // Shown from the moment the button goes down and followed until it comes
        // up; see tabletEvent for why the live value is the colour itself.
        picking_ = true;
        pickColourAt(imageFromWidget(event->position()));
        return;
    }
    if (transform_) {
        beginTransformDrag(event->position());
        return;
    }
    if (lassoing_) {
        beginLasso(event->position());
        return;
    }
    beginStroke(imageFromWidget(event->position()), 1.0f);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    // Before anything decides to swallow the event. A hover-driven pointer needs
    // the move handler to run with no button down and with a transform live,
    // which is exactly the path that used to return here having done nothing --
    // so the box knew what a press would do and never said.
    //
    // A real mouse means the pen has been put down, whichever way up it was.
    alt_held_ = (event->modifiers() & Qt::AltModifier) != 0;
    if (!eventIsSynthesisedFromPen(event)) hover_eraser_ = false;
    updatePointerAt(event->position());

    if (continueNavigation(event->position())) return;
    if (eventIsSynthesisedFromPen(event)) return;
    if (picking_) {
        pickColourAt(imageFromWidget(event->position()));
        return;
    }
    if (transform_) {
        continueTransformDrag(event->position());
        return;
    }
    if (lassoing_) {
        extendLasso(event->position());
        return;
    }
    if (stroking_) extendStroke(imageFromWidget(event->position()), 1.0f);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    alt_held_ = (event->modifiers() & Qt::AltModifier) != 0;
    updatePointerAt(event->position());
    if (panning_ || zooming_ || sizing_) {
        endNavigation();
        return;
    }
    if (eventIsSynthesisedFromPen(event)) return;
    if (picking_) {
        picking_ = false;
        pickColourAt(imageFromWidget(event->position()));
        return;
    }
    if (transform_) {
        endTransformDrag();
        return;
    }
    if (lassoing_) {
        endLasso();
        return;
    }
    endStroke();
}

void CanvasWidget::setZoom(double zoom, const QPointF& widget_anchor) {
    const double clamped = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (std::abs(clamped - zoom_) < 1e-9) return;

    // Keep the image point under the anchor where it is. Written out in full
    // rather than measured as the difference of two imageFromWidget calls, and
    // applied to the exact pan rather than the aligned one. Both matter.
    //
    // Measuring it through imageFromWidget reads the view *after* rounding to a
    // screen pixel, so the half-pixel of alignment lands in the difference; and
    // storing the rounded result put that half-pixel into the state the next
    // event would measure from. A scrubby zoom is one gesture delivered as many
    // events, so the errors did not cancel, they compounded -- a random walk in
    // both axes, and worst in the one the gesture never meant to touch.
    //
    // It scaled with the *event rate*, not with the zoom, which is why it read
    // as "slow zooming wanders". The same 300 px drag: 3 px of vertical drift
    // delivered as 6 events, 21 px as 300, 153 px as 600. Held exact it is
    // 0.00 px at every rate, and what reaches the screen is bounded by the half
    // pixel the alignment is allowed to move it. See tests/test_render.cpp.
    const double was = 1.0 / zoom_;
    const double now = 1.0 / clamped;
    pan_ += QPointF(widget_anchor.x() * (was - now), widget_anchor.y() * (was - now));
    zoom_ = clamped;

    ensureCacheCoversView();
    update();
    Q_EMIT viewChanged();
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) return;
    setZoom(zoom_ * std::pow(1.2, steps), event->position());
    event->accept();
}

void CanvasWidget::resetView() {
    zoom_ = 1.0;
    pan_ = {0.0, 0.0};
    ensureCacheCoversView();
    update();
    Q_EMIT viewChanged();
}

void CanvasWidget::fitToCanvas() { fitTo(doc_.scene().canvas()); }

void CanvasWidget::fitToDrawing() { fitTo(imageBounds(doc_, track_, image_)); }

void CanvasWidget::fitTo(const PixelRect& bounds) {
    if (bounds.isEmpty() || width() <= 0 || height() <= 0) {
        resetView();
        return;
    }

    const double scale_x = static_cast<double>(width()) / bounds.width;
    const double scale_y = static_cast<double>(height()) / bounds.height;
    zoom_ = std::clamp(std::min(scale_x, scale_y) * 0.9, kMinZoom, kMaxZoom);
    pan_ = {bounds.x + bounds.width / 2.0 - width() / (2.0 * zoom_),
            bounds.y + bounds.height / 2.0 - height() / (2.0 * zoom_)};

    ensureCacheCoversView();
    update();
    Q_EMIT viewChanged();
}

// Space and Z are accepted here whether or not they are auto-repeats. That
// looks like a detail and is not: an ignored key propagates to the parent
// widget, where the application-wide filter sees it again and forwards it back
// here, which propagates again. Holding either key past the auto-repeat delay
// used to recurse until the stack ran out -- a crash a few seconds into every
// pan and every zoom.
void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    // The keys a live transform takes over. They arrive here rather than being
    // actions of their own because the actions that own them normally have been
    // disabled -- a disabled QAction does not consume its shortcut, so Return
    // and the arrows fall through to whatever has the keyboard, which is this.
    // See shortcuts.h.
    if (transform_) {
        const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        switch (event->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                applyTransform();
                event->accept();
                return;
            case Qt::Key_Escape:
                cancelTransform();
                event->accept();
                return;
            case Qt::Key_Left: nudgeTransform(-step, 0); event->accept(); return;
            case Qt::Key_Right: nudgeTransform(step, 0); event->accept(); return;
            case Qt::Key_Up: nudgeTransform(0, -step); event->accept(); return;
            case Qt::Key_Down: nudgeTransform(0, step); event->accept(); return;
            default: break;
        }
    }

    switch (event->key()) {
        case Qt::Key_Space:
            if (!event->isAutoRepeat()) {
                space_held_ = true;
                refreshPointer();
            }
            event->accept();
            return;
        case Qt::Key_Z:
            // Held, not toggled: a zoom you have to switch back out of costs
            // more attention than the zoom is worth.
            if (!event->isAutoRepeat()) {
                zoom_key_held_ = true;
                refreshPointer();
            }
            event->accept();
            return;
        case Qt::Key_Alt:
            // Watched and deliberately not accepted. Alt is the eyedropper here
            // and the menu bar's own key on Windows, and taking it would be
            // buying a cursor with the menus.
            alt_held_ = true;
            refreshPointer();
            break;
        default: break;
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Space:
            if (!event->isAutoRepeat()) {
                space_held_ = false;
                refreshPointer();
            }
            event->accept();
            return;
        case Qt::Key_Z:
            if (!event->isAutoRepeat()) {
                zoom_key_held_ = false;
                refreshPointer();
            }
            event->accept();
            return;
        case Qt::Key_Alt:
            alt_held_ = false;
            refreshPointer();
            break;
        default: break;
    }
    QWidget::keyReleaseEvent(event);
}
