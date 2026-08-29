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
// Where a soundtrack sits, how much of it is used, and how loud.
//
// **Two units, and each is in the frame of reference of the thing it
// describes.** That is not an inconsistency to tidy up: it is what keeps both
// numbers still correct after somebody changes the scene's frame rate.
//
// The *offset* is a fact about the shot. You placed the sound so a consonant
// lands on the drawing at frame 12; in frames, it stays on that drawing when
// the rate changes. In seconds it would slide off the drawing it was matched
// to.
//
// The *trim* is a fact about the sound. "Start 0.3 seconds into the file" goes
// on meaning the same moment of the recording whatever the picture does around
// it. In frames, a rate change would re-point it into a different part of the
// take.
struct AudioPlacement {
    // Where the sound starts, in frames of picture. **Fractional**, because
    // placing a sound to the nearest frame is not placing it: 1/24 of a second
    // is 42 ms, which is most of the way to a syllable. A drag moves it by
    // whatever a pixel is worth and nothing rounds it.
    //
    // Signed, because a soundtrack that starts before the shot does is
    // ordinary: an animator given a line of dialogue with a breath in front of
    // it puts the word on frame 1 and lets the breath fall off the start.
    //
    // Not the deferred per-machine sync calibration, which describes a driver,
    // is measured in milliseconds and is a preference rather than anything in
    // scene.json. The two look alike and only this one travels with a project.
    double offset_frames = 0.0;

    // What you will hear, 0 to 1. The height of the bar in the timeline row
    // *is* this number -- the row shows the level rather than describing it,
    // and at the bottom it is silent, so no separate mute is needed.
    //
    // Linear here and not in decibels, because what stores it is what the bar's
    // height is read off. Where a human-facing curve is wanted it belongs at
    // the gesture, not in the field.
    double gain = 1.0;

    // How much of the file is skipped at each end, in seconds.
    //
    // **Non-destructive: the file is untouched and nothing is re-encoded.** A
    // trim moves two numbers, so it costs nothing, undoes like anything else,
    // and can be taken back to the whole take at any point -- which is the
    // whole reason to do it this way rather than by cutting samples.
    double trim_start_seconds = 0.0;
    double trim_end_seconds = 0.0;
};

struct AudioTrack {
    TrackId id = kNoId;
    std::string name;

    // The file inside the project's `audio/` folder, as a bare name. Not a
    // path: a project is a self-contained folder and nothing here may point
    // outside it. Same rule, and the same reason, as an import's source.
    std::string source;

    // Everything a gesture on the row can change, in one struct, so that an
    // undo entry is one swap and a caller cannot write half of it. The same
    // shape Track uses for TrackProperties.
    AudioPlacement placement;
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

// Which sample of the file is heard at this slot, given where the track sits
// and how much of it is trimmed away.
//
// **Negative means the sound has not started, and the number says by how
// much.** A caller feeding a device plays silence until the index reaches zero
// and then reads on -- which is what a sound placed at frame 12.5 needs, since
// half of frame 12 is silence and half of it is sound. A sentinel could not
// express that, which is why this returns a signed index rather than -1.
//
// It does not know where the file ends. That is `lastAudibleSample`, which
// needs the clip and this deliberately does not take one: the picture's side of
// the arithmetic is a pure function of numbers, and keeping it that way is what
// lets a test drive it with no decoded audio at all.
std::int64_t sampleForSlot(std::size_t slot, const AudioPlacement& placement, int rate, int fps);

// One past the last sample the trim leaves audible, given the clip.
//
// A caller reading at or past this plays silence: the trim is an out-point, and
// running off it is not an error any more than running off the end of the file
// is.
std::int64_t lastAudibleSample(const AudioClip& clip, const AudioPlacement& placement);

// How long the audible part of the sound is, in seconds.
double audibleSeconds(const AudioClip& clip, const AudioPlacement& placement);

// The same in frames of picture, and **there are two of these on purpose.**
// Folding them back into one is a tempting tidy-up and would put a bug back.
//
// `audibleFrames` rounds *up* to whole frames and answers "how many frames does
// this occupy" -- which is what the timeline's reach needs, since a sound
// ending a tenth of the way into a frame still makes a noise on that frame and
// the playhead has to be able to get there.
//
// `audibleFrameSpan` does not round and answers "how long is it" -- which is
// what *drawing* it needs. A block whose start is fractional and whose length
// is rounded has a right edge that jumps a whole frame at a time while its left
// edge slides, and trimming the front then looks like it is not sub-frame at
// all even though it is. Reported from use, on the first crop anybody did.
//
// The two differ by less than a frame and that is the whole of it: one is a
// count and the other is a measurement.
std::size_t audibleFrames(const AudioClip& clip, const AudioPlacement& placement, int fps);
double audibleFrameSpan(const AudioClip& clip, const AudioPlacement& placement, int fps);

}  // namespace animage
