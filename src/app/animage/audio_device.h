// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

// An audio output, as the rest of the program is allowed to see it: open at
// rate R, receive a callback asking for N frames, report what has come out,
// stop.
//
// **This is the seam docs/importing.md asks for**, and the reason for it is
// written there in one line: *"the sync arithmetic must be a pure function of
// samples played"*, because GitHub's runners have no audio device and anything
// opening an output there fails or hangs. What that requirement bought is
// already built and already tested without hardware -- `slotForPlayedFrames`
// for the picture's side and `renderAudio` for the sound's. What is left for
// this file is the smaller half of the same decision: keeping Qt Multimedia's
// types out of `MainWindow` and `TimelineWidget`, so that the only file in the
// program that knows what a `QAudioSink` is, is the one below it.
//
// It replaces `audio_check.*`, which was the deployment spike and said in its
// own header that it comes out when this arrives. What the spike found is
// docs/audio-spike.md; two of its numbers are the reason this class is shaped
// the way it is.
//
// --- Opening is expensive, so this is not opened per sound ------------------
//
// `QAudioSink::start()` was measured at **335 ms** on the machine the spike ran
// on. A scrub is a burst of sound on every frame the playhead is dragged past,
// several a second, so a device opened for each burst would be silent for eight
// frames and then say something about the ninth.
//
// So a device stays open and the *content* changes underneath it. That is why
// `Fill` is handed in once at `open` and is called for ever after, on somebody
// else's thread, and why it must answer with silence rather than with nothing
// when there is nothing to play. Whatever a caller wants to change while it
// runs, it changes behind that callback.
//
// --- What the callback may not do -------------------------------------------
//
// `Fill` runs on the device's thread, not the interface thread, and it is on a
// deadline: the samples it does not return in time are an underrun, which is
// heard as a click. So it must not lock anything the interface thread holds for
// long, must not allocate, and must not touch the document -- a `Scene` is
// being edited on the other thread and a pointer into it can be rehashed away
// mid-callback. `AudioProgram` in core/audio_render.h is the shape built for
// this: a value, holding its clips by shared pointer, that a callback can read
// without asking anybody's permission.
class AudioDevice {
public:
    // Fill `frames` interleaved frames of `channels()` floats each, in [-1, 1],
    // and answer how many frames were actually filled.
    //
    // Anything short of what was asked for is made silent by the device, so a
    // caller with nothing to say may return 0. `renderAudio` always fills
    // everything it is given, so the short answer is for a caller that has no
    // program at all.
    using Fill = std::function<std::size_t(float* out, std::size_t frames)>;

    // Whether this build has Qt Multimedia at all. False is not an error: the
    // module is an optional package, so a Qt install without it builds the
    // whole program minus the noise. See src/app/CMakeLists.txt.
    static bool available();

    // Opens an output and starts pulling on `fill` immediately.
    //
    // `rate` and `channels` are a **request**. A driver may refuse them, in
    // which case what it did agree to is `rate()` and `channels()` and the
    // caller must render at those -- which `renderAudio` does by resampling.
    // Asking for the clip's own rate is what makes the common case an exact
    // sample-for-sample read.
    //
    // Null if there is no output, if the module is missing, or if the driver
    // refused everything; `trouble` is then a sentence meant to be shown.
    static std::unique_ptr<AudioDevice> open(int rate, int channels, Fill fill,
                                             QString* trouble = nullptr);

    virtual ~AudioDevice() = default;

    // What the driver agreed to, which may not be what was asked for.
    virtual int rate() const = 0;
    virtual int channels() const = 0;

    // How much audio has come **out of** the device since it was opened, in
    // frames -- samples per channel, which is the only one of frames, bytes and
    // interleaved samples that means the same thing at every channel count.
    //
    // **Played out, not handed over.** That is a measurement and not an
    // assumption: `tests/audio_probe` was written to settle it and
    // docs/audio-spike.md records the readings. It is what lets the picture's
    // slot come from `slotForPlayedFrames(start, playedFrames() - base, ...)`
    // with no buffer-in-flight subtraction, and with the device's output
    // latency already inside the number rather than beside it.
    //
    // It counts from `open` and never restarts, because the device never does.
    // A caller timing something takes its own base at the moment it starts.
    virtual std::int64_t playedFrames() const = 0;

    // Stops pulling. The callback has returned for the last time when this
    // returns, which is what makes it safe to destroy whatever it was reading.
    virtual void stop() = 0;

    // Which output this opened, as an opaque id.
    //
    // **A device is bound to the output it was opened on and never looks
    // again**, which is right -- a stream cannot follow a moving target -- and
    // is why this is here. Somebody plugging in a speaker changes what the
    // machine's *default* output is, and the sink goes on feeding the one that
    // was default when it opened. Compared with `defaultOutputId()`, this is
    // how a caller finds out that the answer has moved on and it should open
    // another. Reported from use: a speaker switched on mid-session, and the
    // scrub went on playing to nothing.
    virtual QString outputId() const = 0;

    // Whether it is still running.
    //
    // The other half of the same problem, for the case where the default did
    // *not* move: an output that is unplugged takes its stream down with it,
    // and what is left says so rather than pulling. There is nothing to be done
    // about that but open another one.
    virtual bool healthy() const = 0;

    // The machine's current default output, or empty where there is none.
    static QString defaultOutputId();

    // Be told when the machine's outputs change, instead of asking.
    //
    // **Two things at once, and the second is the one that bites.** It says the
    // answer to `defaultOutputId()` may have moved -- and holding the watch is
    // also what makes that answer *prompt*: Qt learns about devices coming and
    // going from the system, and it is only listening while something is
    // watching. Asking on its own is what the first version of this did, and it
    // was reported as working for a scrub and not for Play -- which is what a
    // question asked before the answer has arrived looks like.
    //
    // The callback runs on the interface thread, so it may do anything a slot
    // may do, including open another device. **One watcher at a time**: setting
    // one replaces the last, and `{}` clears it. Whoever sets it must clear it
    // before it is destroyed.
    static void watchOutputs(std::function<void()> changed);
};
