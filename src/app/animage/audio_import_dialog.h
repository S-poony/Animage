// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QString>

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
        QString trouble;          // a decode that succeeded with something to say
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
    };

    AudioImportDialog(const Found& found, int playhead_frame, QWidget* parent = nullptr);

    Answer answer() const;

private:
    QSpinBox* start_ = nullptr;
};
