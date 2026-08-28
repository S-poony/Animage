// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ids.h"

namespace animage {

// A soundtrack in the scene.
//
// **It is its own list on the Scene and not a Track with a kind flag**, which
// is what docs/fr/modele-de-donnees.md has and what the code makes sharper.
// `Track` carries layers, slots, an image map, drawing numbers,
// overwrite_drawings, TrackEnd, blend, celSourceFor and nearestWithCel; audio
// answers "not applicable" to every one of them. About twenty places walk
// `scene.tracks`, and a kind flag would put a guard in all of them -- against
// this project's own recorded lesson that policy spread over call sites rots.
// A second list is empty in every project that exists today, and every loop
// that exists goes on meaning exactly what it means now.
//
// See docs/importing.md, "audio is not a track".
//
// **The interface unifies what the model separates.** An audio track is a row
// in the timeline under every drawing row, because it has no compositing order
// and letting it be dragged into the middle of the stack would imply a depth it
// does not have. It has no layers, and is not given a one-row layer list so
// that a gain control has somewhere to live -- that would mean every
// currentLayer() call site handling a layer that is not one.
struct AudioTrack {
    TrackId id = kNoId;
    std::string name;

    // The file inside the project's `audio/` folder, as a bare name. Not a
    // path: a project is a self-contained folder and nothing here may point
    // outside it. Same rule, and the same reason, as an import's source.
    std::string source;

    // Where the sound sits in the shot, in frames. **This belongs to the shot
    // and is saved**, and it is emphatically not the deferred per-machine sync
    // calibration -- that one describes a driver, is measured in milliseconds,
    // and is a preference rather than anything in scene.json. The two look
    // alike and only this one travels with a project to another computer.
    //
    // Signed, because a soundtrack that starts before the shot does is
    // ordinary: an animator given a line of dialogue with a breath in front of
    // it puts the word on frame 1 and lets the breath fall off the start.
    int offset_frames = 0;

    // What you will hear, 0 to 1. The height of the bar in the timeline row
    // *is* this number -- the row shows the level rather than describing it,
    // and at the bottom it is silent, so no separate mute is needed.
    //
    // Linear here and not in decibels, because what stores it is what the bar's
    // height is read off. Where a human-facing curve is wanted it belongs at
    // the gesture, not in the field.
    double gain = 1.0;
};

// What a soundtrack file decoded to.
//
// **Derived, and never written to a project.** Losing it costs a decode of a
// file that is sitting in the project folder -- the same bargain a reference
// frame's tiles make, and the reason both live on the Document rather than on
// the thing they describe. A ten-second file is tens of milliseconds to decode
// and single-digit megabytes to hold, against 17 MB for one HD picture frame,
// so there is nothing here worth bounding.
//
// Interleaved, at whatever rate and channel count the decode produced, because
// that is the shape a device is fed in and converting on the way in would be a
// second thing that could be wrong about a file. `rate` is the clip's own and
// not the device's; whoever opens the device asks for this one.
struct AudioClip {
    int rate = 0;
    int channels = 0;
    std::vector<float> samples;  // interleaved: frame f, channel c is [f * channels + c]

    // Frames, meaning samples per channel -- the unit the sync arithmetic
    // speaks. Bytes and interleaved samples both mean different things at
    // different channel counts, and this is the only one of the three that does
    // not.
    std::size_t frames() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }

    bool empty() const { return frames() == 0; }

    // How long it runs, in frames of picture. Rounded up, because a file that
    // ends a tenth of the way into a frame still makes a sound on it.
    std::size_t framesAtFps(int fps) const {
        if (rate <= 0 || fps <= 0) return 0;
        const std::size_t n = frames();
        return (n * static_cast<std::size_t>(fps) + static_cast<std::size_t>(rate) - 1) /
               static_cast<std::size_t>(rate);
    }
};

// --- the sync arithmetic ----------------------------------------------------
//
// **These are pure functions of a sample count, and that is a requirement
// rather than a style.** GitHub's runners have no audio device, so anything
// opening an output there fails or hangs -- which means the arithmetic the
// whole of lipsync rests on could never be tested if it lived inside something
// that owns a QAudioSink. Passing the count in lets a test drive it with a fake
// and pin the loop seam and the stall case with no hardware at all. The
// precedent is exporting::Solve: the thing that needs a resource is handed in,
// so the logic can be tested without it.
//
// See docs/importing.md, "the playback clock", and docs/audio-spike.md for the
// measurement that says where the sample count comes from:
// QAudioSink::processedUSecs() counts audio **played out** of the device, so
// playedMs() uses it as it comes with nothing subtracted.

// Which slot the picture should be on, given how much audio has come out.
//
// **This is the one line that makes lipsync right**, replacing a slot derived
// from the system clock. Three of the four ways two clocks come apart stop
// existing rather than being separately corrected: the device's fixed output
// latency is already inside `played`, a loop seam wraps both together because
// there is only one number, and an interface stall cannot touch it because it
// is not counted on the interface thread.
//
// `played` is frames of audio -- samples per channel -- and not bytes or
// interleaved samples, because that is the only one of the three that means the
// same thing at every channel count.
//
// Done in one step rather than through milliseconds. `played * fps / rate` is
// what `elapsed_ms * fps / 1000` with `elapsed_ms = played * 1000 / rate`
// reduces to, without the intermediate rounding -- and that rounding is a
// truncation to whole milliseconds, which at 24 fps is a fortieth of a frame
// thrown away on every single tick.
std::size_t slotForPlayedFrames(std::size_t start_slot, std::int64_t played, int rate, int fps,
                                std::size_t count);

// Which sample of the file is heard at this slot, given where the track sits.
//
// Negative when the slot is before the sound starts -- a caller feeding a
// device wants silence there, and saying so with a signed number is better than
// a bool it can forget to check. Also negative past nothing: running off the
// *end* of a file is the file's own length to answer, which this cannot see and
// does not pretend to.
std::int64_t sampleForSlot(std::size_t slot, int offset_frames, int rate, int fps);

}  // namespace animage
