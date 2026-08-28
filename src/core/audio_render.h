// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "audio_track.h"

namespace animage {

// Turning the soundtracks of a shot into the samples a device asks for.
//
// **This is the other half of the rule audio_track.h states**, and it is here
// for the same reason: the arithmetic a device pulls on must be a pure function
// of numbers, so that a runner with no sound card can still check it. Nothing
// in this file opens anything, and `renderAudio` is a function of its arguments
// alone -- which is what lets `test_audio_render` pin the loop seam, the trim,
// the sub-frame offset and the resampling with no hardware at all. The
// precedent is `exporting::Solve`, and `slotForPlayedFrames` beside it.
//
// It is also what keeps the device's thread away from the document. A device
// callback runs on somebody else's thread and must never wait, so it may not
// read a Scene that the interface thread is editing. What it reads instead is
// an `AudioProgram`: a plain value, built on the interface thread, holding the
// clips by shared pointer so that publishing one copies no samples and so that
// a clip cannot be destroyed while a callback is half-way through it.

// One soundtrack, as the renderer needs it.
//
// **The clip is held, not pointed at.** `Document::audioSamplesFor` answers a
// raw pointer into a map that an import or an undo may rehash underneath it,
// which is fine on the interface thread and fatal on a device's. A shared
// pointer costs one atomic increment when a program is built and nothing at
// all while it plays.
struct AudioSource {
    std::shared_ptr<const AudioClip> clip;
    AudioPlacement placement;
};

// What to play, and where in the shot it starts.
//
// **Immutable once it has been handed to a device.** Changing a soundtrack
// while it is playing means building another one of these and publishing that;
// there is nothing here for two threads to write to.
struct AudioProgram {
    std::vector<AudioSource> sources;

    // The device's rate and channel count, and not any clip's. A clip keeps
    // whatever the file decoded to -- see AudioClip::rate -- and this is what
    // the driver agreed to open at. Where the two differ the renderer
    // resamples; where they agree it reads sample for sample.
    int rate = 0;
    int channels = 2;

    // The picture's rate, which is what turns a position in the shot into a
    // position in a file: an offset is in frames of picture.
    int fps = 24;

    // Where in the shot the first rendered sample sits.
    std::size_t start_slot = 0;

    // Wrap back to slot 0 after this many slots, or 0 not to wrap.
    //
    // **This is the loop seam, and it is one number here exactly as it is one
    // number in `slotForPlayedFrames`.** The picture wraps because the slot it
    // derives from wrapped; the sound wraps because the position it reads from
    // wrapped; both wrap on the same count of played samples, so there is
    // nothing to keep in agreement. See docs/importing.md, "the playback
    // clock", which is where the two-wraps version of this was talked out of.
    std::size_t loop_slots = 0;

    // How many device frames to render before falling silent, or -1 to go on
    // until somebody stops it.
    //
    // A scrub is a burst: one frame's worth of sound from where the playhead
    // landed, and then nothing. Playback is the other case and never ends of
    // its own accord.
    std::int64_t length = -1;

    // Device frames to ramp in at the start and out at the end of that burst.
    //
    // **Not a nicety.** A buffer that begins and ends part-way up a waveform
    // steps the speaker cone, and a step is a click -- one on every frame you
    // drag past, which is a row of clicks over the sound you are trying to
    // hear. A couple of milliseconds is inaudible as a fade and removes all of
    // it.
    std::int64_t fade = 0;

    bool silent() const { return sources.empty() || rate <= 0 || channels <= 0 || fps <= 0; }
};

// Fills `frames` interleaved frames, starting `from` device frames into the
// program.
//
// **`from` is a position and not a cursor**, so the same call twice gives the
// same samples and a caller that loses its place has only to say where it is.
// It is what the device has pulled so far, which is also what makes a program
// re-renderable from a sample count -- the same number the picture's slot comes
// from.
//
// Writes silence rather than nothing wherever there is nothing to play: before
// a sound starts, past its out-point, past `length`, and for every channel of a
// program with no sources at all. A device buffer that is left untouched holds
// whatever it held last time round, which is a stutter of old audio.
void renderAudio(const AudioProgram& program, std::int64_t from, float* out, std::size_t frames);

}  // namespace animage
