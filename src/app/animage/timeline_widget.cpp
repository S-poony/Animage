// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline_widget.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPalette>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "marks.h"
#include "name_limits.h"

using namespace animage;

namespace {

constexpr int kCellWidth = 26;
constexpr int kRulerHeight = 18;
constexpr int kRowHeight = 46;
constexpr int kEdgeGrab = 5;
constexpr int kDragThreshold = 5;

// The strip of track names down the left. It is what makes a row say which
// track it is without a control on it -- see the handover on why a widget on a
// row is a trap -- and it is where the current track is shown as current. It is
// also the handle a row is restacked by, which is the other thing a strip with
// no controls on it is free to be.
constexpr int kGutterWidth = 104;

// Taken from the widget's palette rather than hardcoded, so the timeline
// belongs to the same application as everything above it -- and follows a dark
// theme too, if the system asks for one.
struct Palette {
    QColor background, ruler, cell, cell_held, outline, tick, text, current, current_text;
    QColor carried, gutter, gutter_current, outside, boundary;
};

// A palette role with its alpha taken off.
QColor opaque(QColor colour) {
    colour.setAlpha(255);
    return colour;
}

// `amount` hundredths of the way from `from` towards the far end: white when the
// theme is dark, black when it is light.
//
// This is what QColor::lighter and darker were doing here, and it replaces them
// because they cannot do it at the ends. They scale the HSV *value*, so the step
// they take is proportional to the colour they start from -- and at zero there is
// nothing to scale. `lighter(180)` on black returns black. Every colour derived
// from a black one lands on the same black, and the row draws as a slab.
//
// Stepping a fixed fraction of the distance to the end instead means the
// separation is the same wherever it starts, which is the property actually
// wanted: these numbers exist to say "a bit darker than the cells" and "enough
// darker to read as a line", and neither of those is a multiple of anything.
QColor stepped(const QColor& from, bool dark, int amount) {
    const int target = dark ? 255 : 0;
    const auto mix = [&](int channel) { return channel + (target - channel) * amount / 100; };
    return QColor(mix(from.red()), mix(from.green()), mix(from.blue()));
}

// Everything structural is derived from Base -- the colour of a cell -- and not
// from Window.
//
// Window is the role this used to build on, and twice now it has not been a
// colour at all. The shipped Windows build's Qt hands it over as #00000000:
// transparent, and black underneath. Both of the ways that went wrong are worth
// keeping, because they look like different bugs and are one.
//
// Transparent first. lighter and darker carry alpha through untouched, so every
// derived colour came out invisible at once -- background, ruler, gutter, every
// outline -- while the drawing numbers and the playhead kept drawing, those being
// roles used as they come rather than bent. The row was white cells on whatever
// was behind the widget. Making the colours opaque fixed that and uncovered the
// second: opaque #00000000 is pure black, `lightness()` reads 0, the theme is
// taken for a dark one, and lighter() cannot lift a black. Same widget, same
// commit, black slab instead of a white one.
//
// So the lesson is not "force the alpha" -- that was half of it, and shipping it
// alone made the picture worse. It is that Window is a role this widget cannot
// substantiate. Base can be: it is what a cell is painted with, it is what every
// text field in the application stands on, and a theme that gets it wrong is
// broken in a way somebody has already reported. It arrives as #ffffff in the
// build that draws this correctly and in the build that does not.
//
// See `the-timeline-palette` in tests/shots.cpp, which prints these, and the two
// situations beside it that hold each failure still. Nothing else can see either:
// the suite is green through both, and the Qt on this desk hands over a Window
// that is opaque and mid-grey, so neither reproduces here by accident.
Palette paletteFor(const QWidget& widget) {
    const QPalette& source = widget.palette();
    const QColor base = opaque(source.color(QPalette::Base));
    const QColor text = source.color(QPalette::WindowText);
    const bool dark = base.lightness() < 128;

    // Hundredths towards the far end, chosen to land on what the derivation from
    // Window used to give on an ordinary light theme -- #e1e1e1 background,
    // #cecece ruler, #aeaeae outline -- so this is the same row, arrived at by a
    // route that cannot collapse.
    Palette p;
    p.cell = base;
    p.background = stepped(base, dark, 12);
    p.ruler = stepped(base, dark, 19);
    p.outline = stepped(base, dark, 32);
    p.gutter = stepped(base, dark, 15);
    p.cell_held = stepped(base, dark, 6);
    p.tick = text;
    p.text = text;
    p.current = opaque(source.color(QPalette::Highlight));
    p.current_text = source.color(QPalette::HighlightedText);
    p.gutter_current = p.current;
    // Not from the palette: this has to mean the same thing in every theme, and
    // "carried" is not a role a system palette has. See marks.h, which is where
    // it lives now that the layer panel says the same thing about a colour
    // layer's row.
    p.carried = marks::kCarried;
    // A wash over cells outside the shot rather than a different cell colour, so
    // whatever the cell was still reads through it -- held, carried, numbered.
    p.outside = QColor(p.background.red(), p.background.green(), p.background.blue(), 150);
    // "The shot ends here" has to mean the same in every theme, and it is not a
    // role a system palette has. marks.h, with the one above it.
    p.boundary = marks::kBoundary;
    return p;
}


// A soundtrack's row: where the sound is, and how loud it will be.
//
// **One shape carrying both facts**, which is what makes the row worth having.
// The block is where the sound sits in the shot -- that is what a placement
// offset means, and there is otherwise nothing to see it by -- and the block's
// *height* is the gain. At the bottom it is silent, so nothing needs a separate
// mute; at the top it is unchanged.
//
// **And the waveform, which was left out of the first cut and is in now.**
// docs/importing.md put it out on the grounds that a labelled bar is enough to
// *place* a sound and peaks are a second derived thing to build before anything
// is audible. That was right and it expired the day scrubbing worked: a bar
// says where the sound is, and what somebody reading a track needs is where the
// syllables are.
//
// It is drawn as the block's own top edge rather than as a picture laid over
// it, which is what keeps every sentence already written about this row true.
// The height of the fill is still the level -- the whole shape is scaled by the
// gain, so at the bottom it is flat and silent -- and the block's ends are still
// the crop. One shape, one more fact.
//
// A free function taking what it draws, like the other painting helpers here,
// so that the palette can stay private to this file.
struct AudioRowPaint {
    int top = 0;
    bool current = false;
    QString name;
    double gain = 1.0;
    // The extent, in slots and fractional: a sound placed at frame 12.4 draws
    // its edge four tenths of a cell along, which is the only way the placement
    // being finer than a frame is visible at all.
    double first = 0.0;
    double last = 0.0;

    // How loud the sound is, column by column, or null before the file has
    // decoded -- in which case the block is drawn flat-topped as it always was.
    const animage::AudioPeaks* peaks = nullptr;
    // What the columns have to be turned into sample positions with.
    animage::AudioPlacement placement;
    int clip_rate = 0;
    int fps = 24;
};

// The name column of one row, wherever the scroll has put it.
//
// **Painted in a pass of its own, after every row**, which is the whole of what
// pinning it costs in this file. It used to be the first thing each row drew,
// which was fine while it could not move: a cell drawn afterwards was always to
// the right of it. Pinned, the cells it has to cover are the ones drawn after
// it in the same pass, so it has to come later than all of them. Same shape as
// the ruler, one axis over. See setGutterLeft.
void paintGutter(QPainter& painter, const Palette& colours, const QRect& where,
                 const QString& name, bool current, bool dimmed) {
    painter.fillRect(where, colours.gutter);
    if (current) {
        QColor fill = colours.gutter_current;
        if (dimmed) fill.setAlpha(90);
        painter.fillRect(where, fill);
    }
    painter.setPen(current && !dimmed ? colours.current_text : colours.text);
    painter.drawText(where.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, name);
}

void paintAudioRow(QPainter& painter, const Palette& colours, const AudioRowPaint& row) {
    if (row.last <= row.first) {
        // Nothing decoded, or nothing left in the shot. Said rather than drawn
        // as an empty block: a block of a made-up size would be a picture of
        // something that is not there.
        painter.setPen(colours.tick);
        painter.drawText(QRect(kGutterWidth + 4, row.top, 240, kRowHeight - 4),
                         Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("no sound loaded"));
        return;
    }

    const int x0 = kGutterWidth + static_cast<int>(std::lround(row.first * kCellWidth));
    const int x1 = kGutterWidth + static_cast<int>(std::lround(row.last * kCellWidth));
    const int band_top = row.top + 2;
    const int band_height = kRowHeight - 8;

    // The whole extent, faint: where the sound is, at any level including none.
    // Without it a track dragged to silence would vanish, and a row you cannot
    // see is a row you cannot grab to bring back.
    const QRect extent(x0, band_top, std::max(1, x1 - x0), band_height);
    painter.fillRect(extent, colours.cell_held);

    // And the level, filled from the bottom, because the height *is* the number.
    //
    // Flat-topped where there is nothing to shape it with, and shaped by the
    // sound where there is. Both are the same rectangle scaled by the same
    // gain: the waveform is the top edge of the level bar and not a second
    // drawing on top of it, so turning the sound down shrinks the syllables
    // with it and at the bottom there is a flat line, which is what silent
    // looks like.
    const int filled = static_cast<int>(std::lround(row.gain * band_height));
    if (filled > 0) {
        const int bottom = band_top + band_height;
        const bool shaped = row.peaks && !row.peaks->empty() && row.clip_rate > 0 &&
                            row.fps > 0 && extent.width() > 1;
        if (!shaped) {
            painter.fillRect(QRect(x0, bottom - filled, extent.width(), filled),
                             colours.carried);
        } else {
            // One column of the row at a time, each asking the peaks what the
            // loudest thing between its own left and right edges was. The
            // buckets are narrower than a column by construction -- see
            // peaksOf -- so nothing here is invented between two of them.
            const double seconds_per_pixel =
                1.0 / (static_cast<double>(row.fps) * kCellWidth);
            const double at_x0 =
                (static_cast<double>(x0 - kGutterWidth) / kCellWidth - row.placement.offset_frames) /
                    static_cast<double>(row.fps) +
                row.placement.trim_start_seconds;

            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.carried);
            QPolygon shape;
            shape.reserve(extent.width() * 2 + 2);
            // Along the top, left to right...
            for (int i = 0; i < extent.width(); ++i) {
                const double left = at_x0 + i * seconds_per_pixel;
                const auto sample = [&](double seconds) {
                    return static_cast<std::int64_t>(
                        std::floor(seconds * static_cast<double>(row.clip_rate)));
                };
                const float loud = animage::loudnessBetween(
                    *row.peaks, sample(left), sample(left + seconds_per_pixel));
                // A floor of one pixel, so a quiet passage is a thin line
                // rather than a gap. A row broken into islands would read as a
                // sound that is not there rather than a sound that is soft, and
                // the gaps are also where somebody has to grab to move it.
                const int height = std::max(1, static_cast<int>(std::lround(loud * filled)));
                shape << QPoint(x0 + i, bottom - height);
            }
            // ...and back along the bottom, which closes it into one polygon
            // rather than a column of rectangles with seams between them.
            shape << QPoint(x0 + extent.width() - 1, bottom) << QPoint(x0, bottom);
            painter.drawPolygon(shape);
            painter.setBrush(Qt::NoBrush);
        }
    }
    painter.setPen(QPen(colours.outline, 1));
    painter.drawRect(extent.adjusted(0, 0, -1, -1));

    // **The level as a line across the block, which the waveform took away.**
    // Before the shape was drawn from the sound, the top of the fill *was* the
    // level and there was nothing else it could be. Now the top of the fill is
    // the loudest syllable in view, and everywhere else it is lower -- so the
    // number the drag is setting has no edge of its own left. This is that
    // edge: it sits where a flat block would have ended, the waveform touches
    // it at the file's loudest moment and stays under it everywhere else.
    if (filled > 0) {
        painter.setPen(QPen(colours.outline, 1));
        const int level_y = band_top + band_height - filled;
        painter.drawLine(x0, level_y, x0 + extent.width() - 1, level_y);
    }

    // The level as a number too. A bar says "louder than that one"; an animator
    // setting a reference level under a dialogue track wants to be able to come
    // back to the same place.
    painter.setPen(colours.text);
    painter.drawText(extent.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("%1%").arg(std::lround(row.gain * 100.0)));
}

}  // namespace

TimelineWidget::TimelineWidget(Document& document, QWidget* parent)
    : QWidget(parent), doc_(document) {
    setMinimumHeight(kRulerHeight + kRowHeight);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
}

const Track* TimelineWidget::trackAt(std::size_t row) const {
    const std::vector<Track>& tracks = doc_.scene().tracks;
    return (row < tracks.size()) ? &tracks[row] : nullptr;
}

// Soundtracks occupy the rows after the drawing tracks. Null for a drawing
// row, and null past the end, exactly as trackAt is.
const AudioTrack* TimelineWidget::audioAt(std::size_t row) const {
    const std::vector<AudioTrack>& sounds = doc_.scene().audio_tracks;
    if (row < drawingRowCount()) return nullptr;
    const std::size_t at = row - drawingRowCount();
    return (at < sounds.size()) ? &sounds[at] : nullptr;
}

// The highlight, and the only thing that moves it.
//
// **Drawing is the unambiguous statement that you are done with the sound**,
// which is what `clearAudioHighlight` is for: without it, a soundtrack clicked
// once stays lit for the rest of the session while every stroke lands somewhere
// else, and the row that is bright is not the row being worked on.
void TimelineWidget::setAudioHighlight(TrackId id) {
    if (audio_row_ == id) return;
    audio_row_ = id;
    update();
    Q_EMIT highlightChanged();
}

const Track* TimelineWidget::currentTrack() const { return doc_.scene().findTrack(track_); }

// Where the sound sits, in slots.
//
// The length comes from the decoded clip and not from anything stored, because
// nothing stores it: a clip is derived, and a soundtrack whose file has not
// decoded yet has no length to draw. That is the honest picture -- an empty row
// says "nothing here to place" rather than a block of a made-up size.
std::pair<double, double> TimelineWidget::audioExtent(const AudioTrack& sound) const {
    const AudioClip* clip = doc_.audioSamplesFor(sound.id);
    if (!clip || clip->empty()) return {0.0, 0.0};
    // The *audible* length, so a cropped sound draws the part that is left
    // rather than the part that was imported -- and **unrounded**, which is
    // what makes the crop sub-frame in the picture as well as in the model.
    //
    // Rounding here made the block's right edge jump a whole frame at a time
    // while its left edge slid, so trimming the front looked like it was not
    // sub-frame. It always was; the drawing was lying about it. What the
    // rounded count is for is the timeline's *reach* -- see audibleFrames.
    const double length = audibleFrameSpan(*clip, sound.placement, doc_.scene().framerate);
    if (length <= 0.0) return {0.0, 0.0};

    const double first = sound.placement.offset_frames;
    const double last = first + length;
    if (last <= 0.0) return {0.0, 0.0};
    // Only the front is clamped. A soundtrack may start before the shot does --
    // a breath in front of a word -- and what is off the front is not drawn,
    // while the part that *is* in the shot stays exactly where it belongs.
    return {std::max(0.0, first), last};
}

double TimelineWidget::gainForY(std::size_t row, int y) const {
    const int top = rowTop(row) + 2;

    // **The guard is on a constant, so it belongs at compile time.** The band's
    // height is `kRowHeight` less its margins and nothing about it varies, so a
    // runtime `if` on it is a branch that can never be taken -- which MSVC says
    // out loud (C4127, an error under /WX here, and invisible under GCC). The
    // assertion is also the more useful of the two: setting `kRowHeight` to
    // something the band does not fit in would have failed the build instead of
    // silently returning a gain of zero for every drag.
    static_assert(kRowHeight > 8, "the level band needs room to be dragged in");
    constexpr int height = kRowHeight - 8;

    return std::clamp(double(top + height - y) / double(height), 0.0, 1.0);
}

// Which end of the block the pointer is on, if either. The same grab distance
// a run edge uses, so the two feel like one idea rather than two.
bool TimelineWidget::audioEdgeAt(std::size_t row, int x, bool* is_start) const {
    const AudioTrack* sound = audioAt(row);
    if (!sound) return false;
    const auto [first, last] = audioExtent(*sound);
    if (last <= first) return false;

    const int x0 = kGutterWidth + static_cast<int>(std::lround(first * kCellWidth));
    const int x1 = kGutterWidth + static_cast<int>(std::lround(last * kCellWidth));
    // The start wins a tie, which only happens on a block too narrow to have
    // two ends -- and cropping the front of one is the more useful half.
    if (std::abs(x - x0) <= kEdgeGrab) {
        if (is_start) *is_start = true;
        return true;
    }
    if (std::abs(x - x1) <= kEdgeGrab) {
        if (is_start) *is_start = false;
        return true;
    }
    return false;
}

// What the drag under way means, computed from where the press landed rather
// than by accumulating deltas -- so the result depends on where the pointer is
// and not on how many mouse events happened to arrive.
void TimelineWidget::applyAudioDrag(int x, int y) {
    const AudioTrack* sound = doc_.scene().findAudioTrack(audio_drag_track_);
    if (!sound) return;
    const int fps = std::max(1, doc_.scene().framerate);

    // **Nothing rounds.** A pixel is 1/26 of a frame here, which is about 1.6 ms
    // at 24 fps -- and 1/24 of a second is 42 ms, most of the way to a syllable,
    // so a sound placed to the nearest frame is not placed at all.
    const double moved_frames = double(x - press_x_) / double(kCellWidth);
    AudioPlacement next = audio_drag_from_;

    switch (audio_drag_) {
        case AudioDrag::Gain:
            next.gain = gainForY(audio_drag_row_, y);
            break;

        case AudioDrag::Move:
            next.offset_frames = audio_drag_from_.offset_frames + moved_frames;
            break;

        case AudioDrag::TrimStart: {
            // **The sound stays where it is and the block's front moves in.**
            // Cropping the front means the audio under every remaining frame is
            // the audio that was there before -- so the in-point and the offset
            // move together by the same amount. Moving only the trim would slide
            // the whole take earlier, which is a different gesture and not this
            // one.
            double delta = moved_frames;
            if (const AudioClip* clip = doc_.audioSamplesFor(sound->id)) {
                // Clamped here as well as in the Document, because the offset is
                // derived from the trim: letting the trim clamp on its own would
                // move the sound without cropping it.
                const double whole = clip->rate > 0 ? double(clip->frames()) / clip->rate : 0.0;
                const double room =
                    std::max(0.0, whole - audio_drag_from_.trim_end_seconds - 1.0 / 24.0);
                const double lo = -audio_drag_from_.trim_start_seconds * fps;
                const double hi = (room - audio_drag_from_.trim_start_seconds) * fps;
                delta = std::clamp(delta, lo, std::max(lo, hi));
            }
            next.trim_start_seconds = audio_drag_from_.trim_start_seconds + delta / fps;
            next.offset_frames = audio_drag_from_.offset_frames + delta;
            break;
        }

        case AudioDrag::TrimEnd: {
            // The back end only. Dragging right lengthens, so it *removes* trim.
            double delta = moved_frames;
            if (const AudioClip* clip = doc_.audioSamplesFor(sound->id)) {
                const double whole = clip->rate > 0 ? double(clip->frames()) / clip->rate : 0.0;
                const double room =
                    std::max(0.0, whole - audio_drag_from_.trim_start_seconds - 1.0 / 24.0);
                const double lo = (audio_drag_from_.trim_end_seconds - room) * fps;
                const double hi = audio_drag_from_.trim_end_seconds * fps;
                delta = std::clamp(delta, std::min(lo, hi), hi);
            }
            next.trim_end_seconds = audio_drag_from_.trim_end_seconds - delta / fps;
            break;
        }

        default:
            return;
    }

    doc_.setAudioTrackPlacement(audio_drag_track_, next);
    refresh();
    Q_EMIT documentChanged();
}

std::size_t TimelineWidget::rowOf(TrackId track) const {
    const std::vector<Track>& tracks = doc_.scene().tracks;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].id == track) return i;
    }
    // Then the soundtracks, which are the rows after the drawing rows. An id
    // belongs to exactly one of the two lists -- they come from one counter, so
    // they cannot collide -- and this answers past the end for one that belongs
    // to neither, as it always did.
    const std::vector<AudioTrack>& sounds = doc_.scene().audio_tracks;
    for (std::size_t i = 0; i < sounds.size(); ++i) {
        if (sounds[i].id == track) return tracks.size() + i;
    }
    return tracks.size() + sounds.size();
}

int TimelineWidget::rowTop(std::size_t row) const {
    return kRulerHeight + static_cast<int>(row) * kRowHeight;
}

// The middle of a cell, and a point in the ruler above it. For tests that press
// where a hand would press rather than working the layout out for themselves --
// which would agree perfectly with a card drawn somewhere nobody can reach.
QPoint TimelineWidget::cellCentreForTesting(std::size_t row, std::size_t slot) const {
    return {kGutterWidth + static_cast<int>(slot) * kCellWidth + kCellWidth / 2,
            rowTop(row) + kRowHeight / 2};
}

QPoint TimelineWidget::gutterPointForTesting(std::size_t row) const {
    return {gutter_left_ + kGutterWidth / 2, rowTop(row) + kRowHeight / 2};
}

QPoint TimelineWidget::rulerPointForTesting(std::size_t slot) const {
    return {kGutterWidth + static_cast<int>(slot) * kCellWidth + kCellWidth / 2,
            ruler_top_ + kRulerHeight / 2};
}

bool TimelineWidget::inRuler(int y) const {
    return y >= ruler_top_ && y < ruler_top_ + kRulerHeight;
}

bool TimelineWidget::inGutter(int x) const {
    return x >= gutter_left_ && x < gutter_left_ + kGutterWidth;
}

bool TimelineWidget::rowAtY(int y, std::size_t* row) const {
    // Unchanged by the pinning: rows are where they always were, and what the
    // band does is cover one of them. Whether the pointer is in the band is a
    // separate question, asked first -- see inRuler.
    if (y < kRulerHeight || rowCount() == 0) return false;
    const std::size_t at = static_cast<std::size_t>((y - kRulerHeight) / kRowHeight);
    if (row) *row = std::min(at, rowCount() - 1);
    return true;
}

void TimelineWidget::setTrack(TrackId track) {
    if (track_ == track) return;
    track_ = track;
    refresh();
    Q_EMIT trackChanged(track_);
}

void TimelineWidget::scrubTo(int x, bool always) {
    const std::size_t before = current_slot_;
    setCurrentSlot(slotAt(x));
    if (always || current_slot_ != before) Q_EMIT scrubbed(current_slot_);
}

void TimelineWidget::setRulerTop(int y) {
    const int clamped = std::max(0, y);
    if (clamped == ruler_top_) return;
    ruler_top_ = clamped;
    // The whole widget and not the band: a scroll area repaints only what it
    // newly exposed, and what moved here is a strip that was drawn somewhere
    // else. The timeline is rectangles, so a full repaint is cheap.
    update();
}

void TimelineWidget::setGutterLeft(int x) {
    const int clamped = std::max(0, x);
    if (clamped == gutter_left_) return;
    gutter_left_ = clamped;
    // The whole widget, for setRulerTop's reason.
    update();
    // And the rename editor, if one is open, because it is a real child widget
    // laid over the name rather than something painted -- so nothing repaints
    // it into place and it would be left behind where the gutter used to be.
    // The ruler never had to do this: an editor sits in the gutter, and the
    // gutter is what just moved.
    if (renaming_ != kNoId && rename_edit_) {
        rename_edit_->setGeometry(gutterRectFor(rowOf(renaming_)));
    }
}

void TimelineWidget::setCurrentSlot(std::size_t slot) {
    // Against the scene and not against one track: the playhead is the
    // timeline's, so it can stand on a frame that this track does not reach.
    const std::size_t frames = doc_.timelineFrames();
    if (frames == 0) return;
    const std::size_t clamped = std::min(slot, frames - 1);
    if (clamped == current_slot_) return;
    current_slot_ = clamped;
    update();
    Q_EMIT currentSlotChanged(current_slot_);
}

void TimelineWidget::refresh() {
    // A rename outlives an undo, a restack or a track being deleted underneath
    // it, none of which the editor would otherwise notice. Its track is held by
    // id, so the only unanswerable case is the track going away.
    if (renaming_ != kNoId) {
        const bool gone = renaming_audio_ ? doc_.scene().findAudioTrack(renaming_) == nullptr
                                          : doc_.scene().findTrack(renaming_) == nullptr;
        if (gone) {
            finishRenaming(false);
        } else {
            rename_edit_->setGeometry(gutterRectFor(rowOf(renaming_)));
        }
    }

    const std::size_t frames = doc_.timelineFrames();
    if (frames > 0 && current_slot_ >= frames) {
        current_slot_ = frames - 1;
        Q_EMIT currentSlotChanged(current_slot_);
    }
    updateGeometry();
    setMinimumWidth(kGutterWidth + static_cast<int>(frames + 2) * kCellWidth);
    setMinimumHeight(kRulerHeight + static_cast<int>(std::max<std::size_t>(rowCount(), 1)) *
                                        kRowHeight);
    update();
}

QSize TimelineWidget::sizeHint() const {
    const int frames = static_cast<int>(doc_.timelineFrames());
    const int rows = static_cast<int>(std::max<std::size_t>(rowCount(), 1));
    return {kGutterWidth + (frames + 2) * kCellWidth, kRulerHeight + rows * kRowHeight};
}

std::size_t TimelineWidget::slotAt(int x) const {
    const std::size_t frames = doc_.timelineFrames();
    if (frames == 0) return 0;
    const int index = std::clamp((x - kGutterWidth) / kCellWidth, 0, static_cast<int>(frames) - 1);
    return static_cast<std::size_t>(index);
}

int TimelineWidget::sceneEndX() const {
    return kGutterWidth + static_cast<int>(doc_.scene().shotFrames()) * kCellWidth;
}

bool TimelineWidget::isOnSceneEnd(int x) const {
    // Only when the scene is the one saying where the shot ends. With the
    // setting off there is no boundary drawn, and a grab zone you cannot see is
    // worse than no handle at all.
    if (!doc_.scene().fixed_length) return false;
    // The same sentence, for the reason the gutter being pinned created: scroll
    // far enough right and the boundary passes under the name column, where it
    // is clipped out of the ruler band. Without this it would still be
    // grabbable there -- an invisible handle over the names, which is the exact
    // thing the line above refuses.
    if (inGutter(x)) return false;
    return std::abs(x - sceneEndX()) <= kEdgeGrab;
}

std::pair<std::size_t, std::size_t> TimelineWidget::runAt(std::size_t row,
                                                          std::size_t slot) const {
    const Track* line = trackAt(row);
    if (!line) return {slot, slot};
    return line->runBounds(slot);
}

// The numbered card under x, if there is one.
//
// Deliberately not built on slotAt, which *clamps*: past the end of the strip it
// answers "the last slot", and everything downstream then believes there is a
// card under a pointer that is nowhere near one. With every drawing held for a
// frame or two the clamp lands mid-run and the mistake is invisible, so this is
// a bug that only appears in a track of single-frame drawings -- where the last
// slot is its own run, and the whole width of the widget past the strip offers
// that drawing to be dragged.
//
// A clamp is right for the playhead: clicking past the end means the last frame,
// because there is always a frame you are standing on. It is wrong for "what is
// under the pointer", because sometimes the answer is nothing.
bool TimelineWidget::cardAt(std::size_t row, int x, std::size_t* slot) const {
    const Track* line = trackAt(row);
    if (!line || inGutter(x)) return false;

    const std::size_t at = static_cast<std::size_t>((x - kGutterWidth) / kCellWidth);
    if (at >= line->slots.size()) return false;
    // A held frame is the same drawing still showing. There is no second object
    // there to pick up, so only the first slot of a run is a card.
    if (line->runBounds(at).first != at) return false;

    if (slot) *slot = at;
    return true;
}

bool TimelineWidget::isOnRunEdge(std::size_t row, int x, std::size_t* run_start) const {
    const Track* line = trackAt(row);
    if (!line || line->slots.empty()) return false;

    const std::size_t slot = slotAt(x);
    if (slot >= line->slots.size()) return false;
    const auto [first, last] = runAt(row, slot);
    const int edge = kGutterWidth + static_cast<int>(last + 1) * kCellWidth;
    if (std::abs(x - edge) > kEdgeGrab) return false;
    if (run_start) *run_start = first;
    return true;
}

// The number the drawing was born with, read straight off the Image. Deriving
// it from position instead meant a drawing renumbered itself the moment it was
// dragged, which is precisely when you need to know which one you are holding.
std::vector<int> TimelineWidget::drawingNumbers(const Track& track) const {
    std::vector<int> numbers;
    numbers.reserve(track.slots.size());
    for (ImageId id : track.slots) {
        const Image* image = track.findImage(id);
        numbers.push_back(image ? image->number : 0);
    }
    return numbers;
}

// What to show on one drawing's card about its colour layers.
//
// A walk over the slots and nothing else, so it costs nothing and is true for
// every drawing whether or not anybody has been there. There was a second thing
// here -- a wedge for a drawing whose carried marks had landed badly -- and it
// was removed; see docs/handover.md for the measurements that took it out.
TimelineWidget::ColourState TimelineWidget::colourStateFor(const Track& track,
                                                           ImageId image) const {
    ColourState state;
    if (image == kNoId) return state;

    for (const Layer& layer : track.layers) {
        if (layer.kind != LayerKind::Ctg || !layer.visible) continue;

        ImageId from = kNoId;
        if (!doc_.ctgScribblesAt(track.id, image, layer.id, &from)) continue;
        state.any = true;
        if (from != image) state.carried = true;
    }
    return state;
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    const Palette colours = paletteFor(*this);

    QPainter painter(this);
    painter.fillRect(rect(), colours.background);

    QFont font = painter.font();
    font.setPointSizeF(8.5);
    painter.setFont(font);

    const std::size_t frames = doc_.timelineFrames();
    const std::size_t shot = doc_.scene().shotFrames();

    for (std::size_t row = 0; row < rowCount(); ++row) {
        // **Asked for rather than dereferenced**, which is the one line in this
        // file that a soundtrack row would otherwise have walked straight off
        // the end of. trackAt already answered null past the end; what changed
        // is that there is now something after the end.
        if (const AudioTrack* sound = audioAt(row)) {
            AudioRowPaint band;
            band.top = rowTop(row);
            band.current = sound->id == audio_row_;
            band.name = QString::fromStdString(sound->name);
            band.gain = sound->placement.gain;
            band.placement = sound->placement;
            band.fps = std::max(1, doc_.scene().framerate);
            band.peaks = doc_.audioPeaksFor(sound->id);
            if (const AudioClip* clip = doc_.audioSamplesFor(sound->id)) band.clip_rate = clip->rate;
            std::tie(band.first, band.last) = audioExtent(*sound);
            paintAudioRow(painter, colours, band);
            continue;
        }
        const Track& line = *trackAt(row);
        const int top = rowTop(row);
        const bool is_current = line.id == track_;

        // **One row is pointed at, and the highlight says which.** While a
        // soundtrack is highlighted, the track the brush is on keeps the same
        // colour washed back rather than the full one, and drops the
        // highlighted-text pen with it -- so there is one bright row and one
        // that says "still where the brush is" underneath it.
        //
        // Two rows drawn identically current is what this replaces, and it read
        // as two selections because that is what it looked like. Reported from
        // use, along with the Track menu acting on the row that was not lit.
        // The wash is the highlight over the gutter and not a new colour, so it
        // is right in a dark theme for the same reason everything else here is.
        // The gutter itself is drawn in its own pass below, because it is
        // pinned and has to cover the cells this loop is about to draw. What
        // stays here is nothing: see paintGutter.

        // Nothing at all past the track's last drawing, whatever its end
        // behaviour is. Faint dotted cells were tried there and read worse than
        // an empty row: a cell is a frame you can put a drawing on, so drawing
        // cells you cannot click is a harder thing to explain than the absence
        // of any. What the track *does* out there wants saying at the end of the
        // row rather than along it -- see issue #22.
        const std::vector<int> numbers = drawingNumbers(line);
        for (std::size_t i = 0; i < line.slots.size(); ++i) {
            const int x = kGutterWidth + static_cast<int>(i) * kCellWidth;
            const bool held = i > 0 && line.slots[i - 1] == line.slots[i];
            const QRect cell(x, top + 2, kCellWidth - 1, kRowHeight - 8);

            painter.fillRect(cell, held ? colours.cell_held : colours.cell);
            // Outside the shot: there is a drawing here and you may work on it,
            // but it will not play and will not be exported until the boundary
            // is dragged past it. Said on the cell, because an export that
            // quietly leaves work out is the expensive kind of surprise.
            if (i >= shot) painter.fillRect(cell, colours.outside);

            if (held) {
                // A held drawing is one block with a tail, not a repeated cell.
                painter.setPen(QPen(colours.outline, 1));
                painter.drawLine(cell.center().x(), cell.top() + 3, cell.center().x(),
                                 cell.bottom() - 3);
            } else {
                painter.setPen(QPen(colours.outline, 1));
                painter.drawRect(cell.adjusted(0, 0, -1, -1));
                painter.setPen(colours.text);
                painter.drawText(cell, Qt::AlignCenter, QString::number(numbers[i]));

                // A mark on the card, on the numbered one only: a held frame is
                // the same drawing still showing, so it has nothing of its own
                // to report.
                //
                // The feature is invisible when it works -- a carried mark looks
                // exactly like one you drew -- so the only way to know it is
                // working is to be told. A bar under the number for colour that
                // was carried here.
                const ColourState state = colourStateFor(line, line.slots[i]);
                if (state.carried) {
                    painter.fillRect(
                        QRect(cell.left() + 4, cell.bottom() - 4, cell.width() - 8, 2),
                        colours.carried);
                }
            }
        }

        if (dragging_ && drag_row_ == row) {
            if (drop_overwrites_) {
                // The frames it would take over, outlined where they are.
                const int from = kGutterWidth + static_cast<int>(drop_first_) * kCellWidth;
                const int span = static_cast<int>(drop_last_ - drop_first_ + 1) * kCellWidth;
                painter.setPen(QPen(colours.current, 2));
                painter.drawRect(QRect(from, top + 1, span - 1, kRowHeight - 6));
            } else if (drop_index_ >= 0) {
                // Count past the drawing being carried: it is not in the track
                // it is about to be dropped into.
                int seen = 0;
                int at = static_cast<int>(line.slots.size());
                for (std::size_t i = 0; i < line.slots.size(); ++i) {
                    if (line.slots[i] == drag_image_) continue;
                    if (seen == drop_index_) {
                        at = static_cast<int>(i);
                        break;
                    }
                    ++seen;
                }
                painter.setPen(QPen(colours.current, 3));
                const int x = kGutterWidth + at * kCellWidth;
                painter.drawLine(x, top, x, top + kRowHeight - 4);
            }
        }

        // The rim goes on the current track's row only. There is one playhead
        // and it is the scene's -- the ruler says where it is, across all of
        // them -- but the rim means "this is the frame you are editing", and
        // that is true of exactly one row. Drawn on every row it read as four
        // selections, none of which the brush was going to touch.
        if (is_current && current_slot_ < frames) {
            const int x = kGutterWidth + static_cast<int>(current_slot_) * kCellWidth;
            painter.setPen(QPen(colours.current, 2));
            painter.drawRect(QRect(x, top + 1, kCellWidth - 1, kRowHeight - 6));
        }
    }

    // Where a row being carried would land: a line across the whole width and
    // not only across the gutter, because the whole row goes -- the name is the
    // handle, the row is the thing being moved.
    if (dragging_track_ && track_drop_row_ >= 0) {
        // Pulled inside the bottom edge, or the caret for "below the last row"
        // is the one row of pixels the widget does not have.
        const int y = std::min(rowTop(static_cast<std::size_t>(track_drop_row_)), height() - 2);
        painter.setPen(QPen(colours.current, 3));
        painter.drawLine(0, y, width(), y);
    }

    // Where the shot ends: a line down the whole panel, drawn before the ruler
    // band so the band's grip sits on top of its own end of it.
    //
    // Only when the scene fixes the length. Left to the tracks, the shot ends
    // where the longest one does and the rows already show that by stopping, so
    // the line would say nothing twice -- in a colour that means a constraint,
    // while none is being applied.
    const bool bounded = doc_.scene().fixed_length;
    const int end_x = bounded ? sceneEndX() : 0;
    if (bounded) {
        painter.setPen(QPen(colours.boundary, 2));
        painter.drawLine(end_x, 0, end_x, height());
    }

    // --- the gutter, after every row, because it is pinned across all of them -
    //
    // One pass over the rows again rather than a line inside the first, and the
    // reason is the pinning: at `gutter_left_` the column sits on top of cells
    // that the row loop draws *after* it would have drawn the name. Same
    // decision as the ruler being painted last, on the other axis.
    //
    // Before the ruler and not after it, so the top-left corner stays the
    // ruler's exactly as it is today -- the band already spans the gutter's
    // columns, and swapping which of the two owns the corner would be a change
    // to what the timeline looks like unscrolled, which this is not.
    for (std::size_t row = 0; row < rowCount(); ++row) {
        const QRect where = gutterRectFor(row);
        if (const AudioTrack* sound = audioAt(row)) {
            paintGutter(painter, colours, where, QString::fromStdString(sound->name),
                        sound->id == audio_row_, false);
            continue;
        }
        if (const Track* line = trackAt(row)) {
            const bool is_current = line->id == track_;
            paintGutter(painter, colours, where, QString::fromStdString(line->name), is_current,
                        is_current && audio_row_ != kNoId);
        }
    }

    // --- the ruler, last, because it is pinned on top of whatever it reaches --
    //
    // At the top of the widget until something scrolls, which is what it always
    // was; `ruler_top_` is the scroll area saying how far the rows have gone
    // past it. Painted after them rather than before, because a band that is
    // meant to stay put has to cover what slides under it -- see setRulerTop.
    const int band = ruler_top_;
    painter.fillRect(QRect(0, band, width(), kRulerHeight), colours.ruler);

    // **Nothing in the band is drawn over the pinned gutter**, and the band's
    // own fill is the one thing that still is.
    //
    // The corner belongs to both, and it stays the ruler's plain colour, which
    // is exactly what it looks like unscrolled. What must not be there is the
    // band's *contents*: a frame number above the name column would be
    // labelling a cell the gutter is covering, the playhead's marker would be
    // pointing at a frame nobody can see, and the end-of-shot grip would be a
    // control sitting on top of the names and draggable from there. Scrolled
    // right, all three land over the gutter without this.
    //
    // Saved and restored rather than left set, because everything below is
    // inside the same painter and the clip would outlive the band.
    painter.save();
    painter.setClipRect(QRect(gutter_left_ + kGutterWidth, band, width(), kRulerHeight));

    // The frame numbers, once, across the whole width: it is the scene's time
    // and not any one track's.
    for (std::size_t i = 0; i < frames; ++i) {
        if ((i % 5) != 0) continue;
        const int x = kGutterWidth + static_cast<int>(i) * kCellWidth;
        painter.setPen(colours.tick);
        painter.drawText(QRect(x, band, kCellWidth, kRulerHeight), Qt::AlignCenter,
                         QString::number(i + 1));
    }

    if (current_slot_ < frames) {
        const int x = kGutterWidth + static_cast<int>(current_slot_) * kCellWidth;
        painter.fillRect(QRect(x, band, kCellWidth - 1, kRulerHeight), colours.current);
        painter.setPen(colours.current_text);
        painter.drawText(QRect(x, band, kCellWidth, kRulerHeight), Qt::AlignCenter,
                         QString::number(current_slot_ + 1));
    }

    // And the grip, which is the control where the line is the fact. The ruler
    // is where a scene-level control belongs -- the rows are a track's own time.
    if (bounded) {
        painter.setBrush(colours.boundary);
        painter.setPen(Qt::NoPen);
        painter.drawRect(QRect(end_x - 3, band, 6, kRulerHeight));
    }
    painter.restore();
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int x = static_cast<int>(event->position().x());
    const int y = static_cast<int>(event->position().y());

    // A pen's two taps arrive here as two presses and never as a double click.
    // See DoubleTap: this is the pen's way into the same rename the mouse
    // reaches through mouseDoubleClickEvent, and the two cannot both fire.
    if (taps_.isSecond(event->globalPosition().toPoint(), event->timestamp()) &&
        renameAt(x, y)) {
        return;
    }

    // The ruler is a scrub band, apart from the end-of-shot grip in it. Exposure
    // edges live below it, so dragging along time can never resize a hold by
    // accident, and the grip is the one thing up here that is not scrubbing.
    if (inRuler(y)) {
        if (isOnSceneEnd(x)) {
            dragging_end_ = true;
            refreshCursor(x, y);
            // One command for the whole drag, as the exposure stretch does, so
            // dragging the boundary about undoes in a single step.
            doc_.beginCommand("Scene length");
            return;
        }
        scrubbing_ = true;
        scrubTo(x, true);
        return;
    }

    std::size_t row = 0;
    if (!rowAtY(y, &row)) return;

    // **A soundtrack row moves the highlight and leaves the brush where it
    // was.** Handing this id to setTrack would call findTrack with something
    // that is not a Track: the canvas, the panel and the menus would all find
    // nothing to point at, the brush would stop working, and nothing would say
    // why. See docs/importing.md, "the two selections".
    if (const AudioTrack* sound = audioAt(row)) {
        setAudioHighlight(sound->id);
        // The name strip is the handle for nothing here -- a soundtrack has no
        // compositing order, so there is no restack to start.
        if (inGutter(x)) return;

        audio_drag_track_ = sound->id;
        audio_drag_row_ = row;
        audio_drag_from_ = sound->placement;
        press_x_ = x;
        press_y_ = y;

        // The ends are unambiguous, so they start on the press. The body is
        // not: sideways is a move and up-and-down is a level, and which one it
        // is has not happened yet.
        bool is_start = false;
        if (audioEdgeAt(row, x, &is_start)) {
            audio_drag_ = is_start ? AudioDrag::TrimStart : AudioDrag::TrimEnd;
            // One command for the whole drag, as the exposure stretch does:
            // nested commands collapse, so a crop found by eye undoes in one
            // step rather than in fifty.
            doc_.beginCommand("Crop sound");
            refreshCursor(x, y);
            return;
        }

        // **No command yet, and nothing applied.** A press that never moves is
        // a click that selected the row, and a click that set the level to
        // wherever it landed would mean you could not select a soundtrack
        // without changing it.
        audio_drag_ = AudioDrag::Deciding;
        return;
    }

    const Track* line = trackAt(row);
    if (!line) return;

    // Selecting a drawing row puts the highlight back on it.
    setAudioHighlight(kNoId);

    // Pressing anywhere in a row selects its track. Selecting is what a row is
    // for, and requiring people to find the name strip to do it would make the
    // rest of the row a place where clicking edits a track you are not on.
    setTrack(line->id);

    // The name strip selects, and it is the handle a row is restacked by. There
    // is no frame under it, so nothing else here is competing for the press --
    // which is exactly why the handle is here and not on the row itself.
    if (inGutter(x)) {
        may_drag_track_ = true;
        track_drag_row_ = row;
        press_y_ = y;
        return;
    }

    std::size_t run_start = 0;
    if (isOnRunEdge(row, x, &run_start)) {
        stretching_ = true;
        stretch_row_ = row;
        stretch_run_start_ = run_start;
        refreshCursor(x, y);
        // One command for the whole drag: nested commands collapse, so every
        // slot change during the drag undoes in a single step.
        doc_.beginCommand("Change exposure");
        return;
    }

    // The playhead clamps: past the end of the strip you are still standing on
    // the last frame there is.
    setCurrentSlot(slotAt(x));

    // A move does not. Only the numbered card starts one, and past the end of
    // the strip there is no card -- which is the same question the cursor asks,
    // and it has to be the same answer or the pointer is lying.
    std::size_t card = 0;
    if (cardAt(row, x, &card)) {
        may_drag_ = true;
        drag_row_ = row;
        drag_image_ = line->slots[card];
        press_x_ = x;
    }
}

// Where the drawing would land, counted in the track as it will be once the
// drawing has been lifted out of it.
int TimelineWidget::dropIndexFor(int pointer_x) const {
    const Track* line = trackAt(drag_row_);
    if (!line || drag_image_ == kNoId) return 0;

    const int boundary = (pointer_x - kGutterWidth + kCellWidth / 2) / kCellWidth;
    int index = 0;
    for (int i = 0; i < boundary && i < static_cast<int>(line->slots.size()); ++i) {
        if (line->slots[static_cast<std::size_t>(i)] != drag_image_) ++index;
    }
    return index;
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    const int x = static_cast<int>(event->position().x());
    const int y = static_cast<int>(event->position().y());

    if (dragging_end_) {
        // Dragging it says where the shot ends, which is exactly what fixing the
        // length means -- so the drag turns the setting on rather than needing it
        // on first.
        const int frames = static_cast<int>(
            std::lround(static_cast<double>(x - kGutterWidth) / kCellWidth));
        doc_.setSceneLength(true, std::max(1, frames));
        refresh();
        Q_EMIT documentChanged();
        return;
    }

    if (scrubbing_) {
        scrubTo(x, false);
        return;
    }

    // A soundtrack's row. The gesture, once decided, follows the pointer
    // wherever it goes: a drag that wandered out of its row is still the drag
    // it started as, and letting go of it because the hand moved would be
    // maddening.
    if (audio_drag_ == AudioDrag::Deciding) {
        const int dx = std::abs(x - press_x_);
        const int dy = std::abs(y - press_y_);
        if (std::max(dx, dy) >= kDragThreshold) {
            // Whichever axis moved further. The same way this file already
            // tells a drawing drag (along a row) from a track restack (across
            // one) -- except that here both start inside the row, so the
            // threshold decides rather than the side of the gutter.
            audio_drag_ = dx > dy ? AudioDrag::Move : AudioDrag::Gain;
            doc_.beginCommand(dx > dy ? "Move sound" : "Set level");
            refreshCursor(x, y);
        }
        return;
    }
    if (audio_drag_ != AudioDrag::None) {
        applyAudioDrag(x, y);
        return;
    }

    // A row is picked up by moving *down or up* the strip, where a drawing is
    // picked up by moving along it. Neither can start the other: they begin on
    // different sides of the gutter.
    if (may_drag_track_ && !dragging_track_ && std::abs(y - press_y_) >= kDragThreshold) {
        dragging_track_ = true;
        refreshCursor(x, y);
    }
    if (dragging_track_) {
        const int at = trackDropRowFor(y);
        if (at != track_drop_row_) {
            track_drop_row_ = at;
            update();
        }
        return;
    }

    if (may_drag_ && !dragging_ && std::abs(x - press_x_) >= kDragThreshold) {
        dragging_ = true;
        refreshCursor(x, y);
    }
    if (dragging_) {
        const Track* line = trackAt(drag_row_);
        const bool overwrites = line && line->overwrite_drawings;
        const std::size_t slot = slotAt(x);

        if (overwrites) {
            // The range it would take over, worked out the same way the model
            // will work it out: the drawing is lifted out first, so the run it
            // lands in is the one it leaves behind.
            std::size_t first = slot;
            std::size_t last = slot;
            bool room = false;
            if (line && slot < line->slots.size()) {
                const auto range = line->overwriteRangeAt(slot);
                if (range) {
                    first = range->first;
                    last = range->second;
                    room = true;
                }
            }
            if (room != drop_overwrites_ || first != drop_first_ || last != drop_last_) {
                drop_overwrites_ = room;
                drop_first_ = first;
                drop_last_ = last;
                drop_index_ = static_cast<int>(slot);
                update();
            }
            return;
        }

        drop_overwrites_ = false;
        const int drop = dropIndexFor(x);
        if (drop != drop_index_) {
            drop_index_ = drop;
            update();
        }
        return;
    }

    if (stretching_) {
        applyStretch(x);
        return;
    }

    refreshTooltip(x, y);
    refreshCursor(x, y);
}

// The boundary between rows nearest y: 0 is above the first row, rowCount() is
// below the last. A boundary and not a row, because a drop lands *between* two
// rows and there is one more of those than there are rows.
// **Bounded by the drawing rows and not by all of them.** Soundtracks sit
// under every track and are not part of the compositing stack, so there is no
// boundary below them to drop a track on -- and `moveTrack` counts in
// `scene.tracks`, so a caret that reached into the audio rows would hand it an
// index past the end of the list it indexes.
int TimelineWidget::trackDropRowFor(int y) const {
    if (drawingRowCount() == 0) return 0;
    const int at = static_cast<int>(
        std::lround(static_cast<double>(y - kRulerHeight) / kRowHeight));
    return std::clamp(at, 0, static_cast<int>(drawingRowCount()));
}

// What the row is, and what it does with a drawing put down on it.
//
// The gutter used to carry the word "overwrite" under the name, and it was
// reported as clutter: it is on nearly every row nearly all the time -- the
// setting is on by default -- so it reads as decoration and stops being seen at
// all. It is here instead, where it is asked for rather than read past, and the
// status bar still says it about the track being edited.
void TimelineWidget::refreshTooltip(int x, int y) {
    std::size_t row = 0;
    if (rowAtY(y, &row)) {
        if (const AudioTrack* sound = audioAt(row)) {
            // On the whole row rather than the gutter only. A track's tooltip
            // is on its name because the cells beside it are full of drawings
            // to say things about; a soundtrack's row has one thing in it, and
            // "drag this up and down" is not discoverable from a coloured bar.
            QString tip = QStringLiteral("%1\n\nDrag up or down to set the level. "
                                         "At the bottom it is silent.\n"
                                         "Audio is not exported.")
                              .arg(QString::fromStdString(sound->name));
            // **And which file it came from, which is what makes renaming one
            // safe**: the label on the row and the file in the project's
            // `audio/` folder are two different things, and losing sight of the
            // second was the one real objection to letting the first be
            // changed.
            //
            // Said here rather than through a `QEvent::ToolTip` override, which
            // is where it was and which cost every other tooltip in this file.
            // That override answered *all* tooltip events and returned true, so
            // `QWidget::event` -- the thing that reads the `toolTip()` property
            // and shows it -- never ran, and the track tooltips set below were
            // never seen by anybody. One place decides what a row says.
            tip += sound->source.empty()
                       ? QStringLiteral("\n\nNo file.")
                       : QStringLiteral("\n\nFrom %1, in the project's audio folder.\n"
                                        "Renaming this row does not rename that file.")
                             .arg(QString::fromStdString(sound->source));
            setToolTip(tip);
            return;
        }
    }
    const Track* line = (inGutter(x) && rowAtY(y, &row)) ? trackAt(row) : nullptr;
    if (!line) {
        setToolTip(QString());
        return;
    }

    QString tip = QString::fromStdString(line->name);
    tip += QStringLiteral("\nDrag the name up or down to restack the tracks.");
    if (line->overwrite_drawings) {
        tip += QStringLiteral(
            "\n\nOverwrite drawings: a drawing put down here spends the rest\n"
            "of the hold rather than lengthening the shot.");
    }
    setToolTip(tip);
}

// One question, asked of everything that is true at once. The same shape as the
// canvas's own -- see CanvasWidget::pointingAt -- and here for the same reason:
// spread across the paths of a move handler, a cursor is decided by whichever
// path happened to run rather than by what is under the pointer.
Qt::CursorShape TimelineWidget::cursorAt(int x, int y) const {
    // A gesture under way outranks what is beneath the pointer.
    if (dragging_ || dragging_track_) return Qt::ClosedHandCursor;
    if (dragging_end_ || stretching_) return Qt::SplitHCursor;

    // The ruler. It scrubs, and it used to say so with a pointing hand -- which
    // is the first thing crossed on the way in from the canvas, so the timeline
    // read as a hand everywhere before you had touched anything. A hand here
    // means one thing now: this drawing can be picked up and moved.
    if (inRuler(y)) return isOnSceneEnd(x) ? Qt::SplitHCursor : Qt::ArrowCursor;

    std::size_t row = 0;
    if (!rowAtY(y, &row)) return Qt::ArrowCursor;

    // A soundtrack's row. Its name strip picks nothing up -- there is no
    // compositing order to restack -- and the rest of it is the level, dragged
    // up and down. The pointer says which of those two it is before the hand
    // arrives, which is what stops the open hand promising a restack that
    // cannot happen here.
    if (isAudioRow(row)) {
        if (inGutter(x)) return Qt::ArrowCursor;
        // An end crops, which is the same shape of gesture as stretching an
        // exposure and says so with the same cursor. The body does two things
        // and cannot promise either, so it says both.
        if (audioEdgeAt(row, x, nullptr)) return Qt::SplitHCursor;
        return Qt::SizeAllCursor;
    }

    // The name strip, which the row is restacked by. The hand still means one
    // thing -- this can be picked up and moved -- and now there are two things
    // it is true of: a drawing along its track, and a track up its stack.
    if (inGutter(x)) return Qt::OpenHandCursor;

    // The boundary between two runs, which stretches the exposure.
    if (isOnRunEdge(row, x, nullptr)) return Qt::SplitHCursor;

    // And only the numbered card can be picked up -- the same function the
    // press asks, which is what stops the pointer promising a drag that will
    // not happen or staying an arrow over one that will.
    return cardAt(row, x, nullptr) ? Qt::OpenHandCursor : Qt::ArrowCursor;
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    const int x = static_cast<int>(event->position().x());
    const int y = static_cast<int>(event->position().y());

    if (dragging_track_) {
        const int from = static_cast<int>(track_drag_row_);
        const int boundary = track_drop_row_;

        dragging_track_ = false;
        may_drag_track_ = false;
        track_drop_row_ = -1;
        refreshCursor(x, y);

        // The caret is a boundary counted in the rows as they stand; moveTrack
        // counts the destination with the row already taken out. Dropping a row
        // on either of its own edges is a move to where it already is, and both
        // land on `from` here rather than needing a case of their own.
        // The drawing rows, for trackDropRowFor's reason: this is an index
        // into scene.tracks and the soundtrack rows are not in it.
        const int rows = static_cast<int>(drawingRowCount());
        if (boundary >= 0 && rows > 1) {
            const int to = std::clamp(boundary > from ? boundary - 1 : boundary, 0, rows - 1);
            if (to != from) {
                doc_.moveTrack(static_cast<std::size_t>(from), static_cast<std::size_t>(to));
                refresh();
                Q_EMIT documentChanged();
            }
        }
        return;
    }
    may_drag_track_ = false;

    if (dragging_) {
        const Track* line = trackAt(drag_row_);
        const TrackId dropped_in = line ? line->id : kNoId;
        const bool overwrites = line && line->overwrite_drawings;
        const ImageId moved = drag_image_;
        const int drop = drop_index_;

        dragging_ = false;
        may_drag_ = false;
        drag_image_ = kNoId;
        drop_index_ = -1;
        drop_overwrites_ = false;
        // Whatever is under the pointer now, which after dropping a card on
        // itself is that card: the hand opens rather than becoming an arrow.
        refreshCursor(x, y);

        if (dropped_in != kNoId && drop >= 0) {
            // Two different questions, and neither answer can be derived from
            // the other: where between two drawings, or which frame to paint
            // over. See Document::moveDrawingOver.
            if (overwrites) {
                doc_.moveDrawingOver(dropped_in, moved, static_cast<std::size_t>(drop));
            } else {
                doc_.moveDrawing(dropped_in, moved, static_cast<std::size_t>(drop));
            }
            refresh();
            const Track* after = doc_.scene().findTrack(dropped_in);
            if (after) {
                const std::size_t at = after->firstSlotOf(moved);
                if (at < after->slots.size()) setCurrentSlot(at);
            }
            Q_EMIT documentChanged();
        }
        return;
    }
    may_drag_ = false;

    if (dragging_end_) {
        dragging_end_ = false;
        refreshCursor(x, y);
        doc_.endCommand();
        Q_EMIT documentChanged();
        return;
    }

    if (scrubbing_) {
        scrubbing_ = false;
        return;
    }

    if (audio_drag_ != AudioDrag::None) {
        // Deciding never opened one: it is a click that selected a row.
        const bool had_command = audio_drag_ != AudioDrag::Deciding;
        audio_drag_ = AudioDrag::None;
        audio_drag_track_ = kNoId;
        refreshCursor(x, y);
        if (had_command) {
            doc_.endCommand();
            Q_EMIT documentChanged();
        }
        return;
    }

    if (!stretching_) return;
    stretching_ = false;
    refreshCursor(x, y);
    doc_.endCommand();
    Q_EMIT documentChanged();
}

void TimelineWidget::abandonGesture() {
    // One endCommand for each beginCommand, tested separately rather than
    // together: they cannot both be open today, and a counter is not the place
    // to rely on that.
    if (dragging_end_) {
        dragging_end_ = false;
        doc_.endCommand();
        Q_EMIT documentChanged();
    }
    if (stretching_) {
        stretching_ = false;
        doc_.endCommand();
        Q_EMIT documentChanged();
    }
    if (audio_drag_ != AudioDrag::None) {
        const bool had_command = audio_drag_ != AudioDrag::Deciding;
        audio_drag_ = AudioDrag::None;
        audio_drag_track_ = animage::kNoId;
        if (had_command) {
            doc_.endCommand();
            Q_EMIT documentChanged();
        }
    }

    // Everything else is dropped where it stood. Nothing here writes to the
    // document: a drag that never arrived anywhere did not move a row.
    scrubbing_ = false;
    may_drag_ = false;
    dragging_ = false;
    drag_image_ = animage::kNoId;
    drop_index_ = -1;
    drop_overwrites_ = false;
    may_drag_track_ = false;
    dragging_track_ = false;
    track_drop_row_ = -1;
    update();
}

void TimelineWidget::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    abandonGesture();
}

void TimelineWidget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) abandonGesture();
}

// A double click on the name renames the track.
//
// Only on the name, and not anywhere else in the row: the rest of the row is
// frames, and a double click there is two presses that each mean something --
// selecting a frame -- rather than a gesture of its own. The gutter has nothing
// under it competing for the press, which is the same reason it is the drag
// handle.
//
// The press that opens this has already selected the track and armed a restack
// drag; the release before the double click disarms it, so nothing has to be
// undone here.
bool TimelineWidget::renameAt(int x, int y) {
    if (!inGutter(x)) return false;
    std::size_t row = 0;
    if (!rowAtY(y, &row)) return false;
    beginRenaming(row);
    return true;
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    renameAt(static_cast<int>(event->position().x()), static_cast<int>(event->position().y()));
}

QRect TimelineWidget::gutterRectFor(std::size_t row) const {
    // Wherever the scroll has put the column, which is what makes this one
    // rectangle rather than two: the paint, the rename editor and the hit test
    // all take it from here, so a pinned gutter cannot end up drawn in one
    // place and pressed in another.
    return QRect(gutter_left_, rowTop(row), kGutterWidth - 2, kRowHeight - 2);
}

void TimelineWidget::beginRenaming(std::size_t row) {
    // Opening one on another row keeps what was typed into the first rather
    // than dropping it, which is what re-seeding the editor would do. Before
    // the track is looked up, because renaming one is a document change.
    if (renaming_ != kNoId) finishRenaming(true);

    // Which list the row is in is settled once, here, and remembered -- because
    // what `renaming_` names is an id, and an id handed to the wrong lookup
    // answers nothing rather than something plausible. That is the shape of
    // failure the two selections exist to prevent, and it would arrive here as
    // a soundtrack's new name written onto a track.
    const AudioTrack* sound = audioAt(row);
    const Track* line = sound ? nullptr : trackAt(row);
    if (!sound && !line) return;
    renaming_audio_ = sound != nullptr;

    if (!rename_edit_) {
        rename_edit_ = new QLineEdit(this);
        rename_edit_->setFrame(true);
        // A hard cap rather than a complaint afterwards, and far past anything a
        // gutter this wide can show. See names::kTyped.
        rename_edit_->setMaxLength(names::kTyped);
        // Enter, and losing the editor to a click somewhere else, both mean the
        // name is what it now says. Escape is the only way to leave it alone,
        // and it arrives as a key rather than as a signal -- hence the filter.
        rename_edit_->installEventFilter(this);
        connect(rename_edit_, &QLineEdit::editingFinished, this,
                [this] { finishRenaming(true); });
    }

    renaming_ = sound ? sound->id : line->id;
    rename_edit_->setGeometry(gutterRectFor(row));
    rename_edit_->setText(QString::fromStdString(sound ? sound->name : line->name));
    rename_edit_->selectAll();
    rename_edit_->show();
    // Explicitly, because the timeline itself takes no keyboard focus: without
    // this the editor appears and the keys keep going to the canvas.
    rename_edit_->setFocus(Qt::OtherFocusReason);
}

void TimelineWidget::finishRenaming(bool keep) {
    if (!rename_edit_ || renaming_ == kNoId || finishing_rename_) return;

    // Hiding the editor takes the focus away from it, which emits
    // editingFinished a second time. This is the flag that stops the second one
    // being a second rename.
    finishing_rename_ = true;
    const TrackId renamed = renaming_;
    const QString typed = rename_edit_->text().trimmed();
    renaming_ = kNoId;
    rename_edit_->hide();
    finishing_rename_ = false;

    if (!keep) return;

    // A soundtrack's name is a label and its `source` is the file, which is
    // what makes renaming one safe: nothing on disk is touched and the row's
    // tooltip goes on saying which file it came from.
    if (renaming_audio_) {
        const AudioTrack* sound = doc_.scene().findAudioTrack(renamed);
        if (!sound || typed.isEmpty() || typed.toStdString() == sound->name) return;
        doc_.renameAudioTrack(renamed, typed.toStdString());
        refresh();
        Q_EMIT documentChanged();
        return;
    }

    const Track* line = doc_.scene().findTrack(renamed);
    // An empty name leaves the track with no label on its row and no prefix on
    // its exported files, so the old one is kept rather than the emptiness
    // accepted -- the same answer the Rename track dialog gives.
    if (!line || typed.isEmpty() || typed.toStdString() == line->name) return;

    TrackProperties props = line->properties();
    props.name = typed.toStdString();
    doc_.updateTrack(renamed, props);
    refresh();
    Q_EMIT documentChanged();
}

bool TimelineWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == rename_edit_ && event->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            finishRenaming(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TimelineWidget::applyStretch(int pointer_x) {
    const Track* line = trackAt(stretch_row_);
    if (!line || stretch_run_start_ >= line->slots.size()) return;
    const TrackId id = line->id;

    const auto [first, last] = runAt(stretch_row_, stretch_run_start_);
    const int current_length = static_cast<int>(last - first) + 1;

    const int wanted_edge = pointer_x - kGutterWidth;
    const int wanted_length =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(wanted_edge) / kCellWidth)) -
                        static_cast<int>(first));
    if (wanted_length == current_length) return;

    if (wanted_length > current_length) {
        doc_.extendExposure(id, first, wanted_length - current_length);
    } else {
        for (int i = 0; i < current_length - wanted_length; ++i) {
            doc_.removeSlot(id, first);
        }
    }

    refresh();
    Q_EMIT documentChanged();
}
