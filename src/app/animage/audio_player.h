// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "audio_device.h"
#include "audio_render.h"

// What is playing right now, and how far into it the speaker has got.
//
// **One object between the interface and the device**, so that the handover
// between two threads happens in one file instead of at every call site. The
// interface says "play this from here"; the device's callback asks for samples;
// this is the only thing that both of them touch.
//
// See audio_device.h for why the device is opened once and kept open, and
// core/audio_render.h for what an `AudioProgram` is. The short version: opening
// an output costs a third of a second, so a scrub cannot open one per burst.
// The device stays, and what it is playing changes underneath it.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Opens an output. `rate` is a request -- ask for the rate a soundtrack
    // decoded to and the ordinary case is an exact sample-for-sample read.
    // False, with a sentence in `trouble`, if there is nothing to open.
    bool open(int rate, int channels, QString* trouble = nullptr);
    void close();

    // Where a device comes from, so that a test can supply one.
    //
    // **This is the same argument as `slotForPlayedFrames` taking a sample
    // count**, one layer up. GitHub's runners have no audio output, so `open`
    // cannot run there -- and what this class does between "handed to the
    // device" and "played out of it" is exactly the part that must not be
    // wrong, because getting it wrong does not fail, it *leans*: the picture
    // sits a fixed fraction of a frame from the sound, on every frame,
    // invisibly. A fake device answering `playedFrames` on command pins it on
    // every platform with no hardware at all.
    using Opener = std::function<std::unique_ptr<AudioDevice>(
        int rate, int channels, AudioDevice::Fill fill, QString* trouble)>;
    bool openWith(const Opener& opener, int rate, int channels, QString* trouble = nullptr);

    bool running() const { return device_ != nullptr; }

    // What the driver agreed to, which is what a program must be built at.
    int rate() const;
    int channels() const;

    // What was asked for, which is how a caller knows whether a device that is
    // already open is the right one for a new soundtrack.
    int openedFor() const { return asked_rate_; }

    // Play this from its beginning. Replaces whatever was playing, and null is
    // how you say "nothing" -- the device goes on running and goes quiet.
    //
    // **The program is a value and must be finished with before it is handed
    // over.** From here on it is read on the device's thread, and nothing on
    // this side may touch it again.
    void play(std::shared_ptr<const animage::AudioProgram> program);
    void silence() { play(nullptr); }

    // How much of the **current** program has come out of the speaker, in
    // frames of audio. Zero until the first sample of it is audible, which is
    // not the moment it was handed over: there is a buffer's worth of the
    // previous program in front of it.
    //
    // That subtraction is the whole reason this class counts what it hands to
    // the device as well as what the device has played. See the note on
    // `handed` in the implementation.
    std::int64_t playedFrames() const;

private:
    // Everything both threads touch, and nothing else.
    //
    // **The lock is held for a pointer copy and never for a render.** A mutex
    // on an audio callback is a thing to be careful with -- the thread is on a
    // deadline and blocking it is an underrun -- but the critical section here
    // is a handful of assignments, where the work either side of it is not.
    struct Shared {
        std::mutex lock;
        std::shared_ptr<const animage::AudioProgram> program;

        // Bumped on every `play`, which is how the callback knows to start
        // reading the new program from its beginning rather than from wherever
        // it had got to in the last one.
        std::uint64_t epoch = 0;

        // Frames handed to the device since it was opened, counted **before**
        // they are rendered so that a publish landing mid-callback errs on the
        // side of counting them.
        std::int64_t handed = 0;

        // What `handed` was when the current program was published, and
        // therefore the reading of `playedFrames` at which its first sample is
        // heard. Audio comes out in the order it went in, so this is exact
        // rather than an estimate of the buffer.
        std::int64_t base = 0;
    };

    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::unique_ptr<AudioDevice> device_;
    int asked_rate_ = 0;
};
