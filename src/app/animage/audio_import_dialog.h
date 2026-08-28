// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QString>

#include <cstddef>

class QCheckBox;
class QSpinBox;

// What File ▸ Import ▸ Audio… asks before it adds a soundtrack.
//
// **A recap first, and one setting beside it**, which is the shape the sequence
// dialog already has: the file has been picked and decoded by the time this
// opens, so what is left is to say what came out of it and to offer the one
// thing that is genuinely a choice.
//
// The setting is a placement offset in frames. It **belongs to the shot** and
// travels with the project -- it is not the per-machine sync calibration, which
// is deferred, measured in milliseconds and would be a preference rather than
// anything in `scene.json`. The two look alike and only this one is a fact
// about the animation. See docs/importing.md, "the playback clock".
//
// **It says that audio is not exported.** That sentence is true today and stops
// being true the day video export ships, which is the last item in
// docs/importing.md and is exactly why it is written in one place.
//
// Deliberately does no work: it reads what it is handed and hands back one
// number, so a test and `shots` can drive an import without answering it.
class AudioImportDialog : public QDialog {
    Q_OBJECT

public:
    // What the caller found out before opening this.
    struct Found {
        QString file;        // the name as it will appear, for the recap
        int rate = 0;        // samples per second
        int channels = 0;
        std::size_t frames = 0;   // samples per channel
        int scene_fps = 24;       // to say the length in frames as well as seconds
        qint64 file_bytes = 0;    // what is on disk, to compare against
        QString trouble;          // a decode that succeeded with something to say

        // How long the sound runs, in frames of picture, and how long the shot
        // is now. Together these decide whether the shot needs lengthening at
        // all -- see `extend_shot` below.
        std::size_t sound_frames = 0;
        std::size_t shot_frames = 0;
        // Whether the scene has been *told* how long the shot is, as against
        // taking it from whatever the tracks add up to.
        bool length_is_fixed = false;
    };

    struct Answer {
        // The frame the sound starts on, counting from 1 as the timeline does.
        // Never a slot index; the conversion is the caller's.
        //
        // May be negative or zero: a line of dialogue with a breath in front of
        // it wants the word on frame 1 and the breath falling off the start.
        // The box lets that in rather than clamping, because refusing it would
        // refuse an ordinary thing to want.
        int start_frame = 1;

        // Whether to make the shot reach the end of the sound.
        //
        // **The box appears only when it would change something, and is ticked
        // only when nothing has decided the length yet.** Three cases, and the
        // rule reads the same in all of them:
        //
        // - the sound fits inside the shot: no box at all, because there is
        //   nothing to offer;
        // - the sound runs past a shot whose length nobody has fixed: ticked,
        //   because a shot being made up as it goes has no length yet and the
        //   sound is the thing being animated to;
        // - the sound runs past a length somebody has fixed: offered and *not*
        //   ticked. Saying how long the shot is is a decision, and an import
        //   has no business overruling one that has already been made.
        //
        // Why it matters at all: playback derives its slot from
        // `Scene::shotFrames`, so a three-second soundtrack in a shot of one
        // drawing plays one frame and stops. Widening the timeline lets the
        // playhead be dragged over the sound; only this lets Play run it.
        // `scene.h` names animating to a soundtrack as the case `fixed_length`
        // exists for.
        bool extend_shot = false;
    };

    AudioImportDialog(const Found& found, int playhead_frame, QWidget* parent = nullptr);

    Answer answer() const;

private:
    QSpinBox* start_ = nullptr;
    QCheckBox* extend_ = nullptr;  // absent when the sound already fits
};
