// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_render.h"

#include <algorithm>
#include <cmath>

namespace animage {

namespace {

// One source, with everything that does not change from frame to frame worked
// out once. The inner loop runs at the device's rate -- 48000 times a second
// per source -- so a division that could have been done here is 48000
// divisions that did not need doing.
struct Prepared {
    const AudioClip* clip = nullptr;
    double gain = 1.0;
    double offset_seconds = 0.0;
    double trim_start_seconds = 0.0;
    double clip_rate = 0.0;
    // One past the last sample the trim leaves audible. Past it is silence, not
    // an error: an out-point is a place to stop reading.
    std::int64_t last = 0;
};

float sampleOf(const AudioClip& clip, std::int64_t frame, int channel) {
    return clip.samples[static_cast<std::size_t>(frame) * static_cast<std::size_t>(clip.channels) +
                        static_cast<std::size_t>(channel)];
}

}  // namespace

void renderAudio(const AudioProgram& program, std::int64_t from, float* out, std::size_t frames) {
    const int channels = std::max(1, program.channels);

    // Silence first, and always. A device buffer this function leaves alone
    // holds whatever was in it last time round, which comes out as a stutter of
    // old audio rather than as nothing -- and every early return below is a
    // case where there is genuinely nothing to play.
    std::fill(out, out + frames * static_cast<std::size_t>(channels), 0.0f);
    if (program.silent() || frames == 0) return;

    const double rate = static_cast<double>(program.rate);
    const double fps = static_cast<double>(program.fps);

    std::vector<Prepared> ready;
    ready.reserve(program.sources.size());
    for (const AudioSource& source : program.sources) {
        if (!source.clip) continue;
        const AudioClip& clip = *source.clip;
        if (clip.rate <= 0 || clip.channels <= 0 || clip.empty()) continue;
        Prepared one;
        one.clip = &clip;
        one.gain = std::clamp(source.placement.gain, 0.0, 1.0);
        one.offset_seconds = source.placement.offset_frames / fps;
        one.trim_start_seconds = std::max(0.0, source.placement.trim_start_seconds);
        one.clip_rate = static_cast<double>(clip.rate);
        one.last = lastAudibleSample(clip, source.placement);
        if (one.last <= 0 || one.gain <= 0.0) continue;
        ready.push_back(one);
    }
    if (ready.empty()) return;

    // Where the first rendered sample sits in the shot, and how long the loop
    // is, both in seconds. Seconds rather than slots because a soundtrack does
    // not know about slots: the offset is the only thing in frames of picture
    // and it is turned into seconds once, here.
    const double start_seconds = static_cast<double>(program.start_slot) / fps;
    const double loop_seconds =
        program.loop_slots > 0 ? static_cast<double>(program.loop_slots) / fps : 0.0;

    for (std::size_t i = 0; i < frames; ++i) {
        const std::int64_t pos = from + static_cast<std::int64_t>(i);
        if (pos < 0) continue;
        if (program.length >= 0 && pos >= program.length) continue;

        // The burst's ramp. Two comparisons and a divide against a click on
        // every frame the playhead is dragged past -- see AudioProgram::fade.
        double envelope = 1.0;
        if (program.fade > 0) {
            const double in = static_cast<double>(pos) / static_cast<double>(program.fade);
            if (in < 1.0) envelope = std::max(0.0, in);
            if (program.length >= 0) {
                const double away = static_cast<double>(program.length - pos) /
                                    static_cast<double>(program.fade);
                if (away < 1.0) envelope = std::min(envelope, std::max(0.0, away));
            }
        }

        // The position in the shot, wrapped if this program loops. **The wrap
        // is on the position and not on a slot**, so a loop seam lands between
        // two samples exactly where the picture's `% count` lands, rather than
        // at the nearest frame boundary to it.
        double seconds = start_seconds + static_cast<double>(pos) / rate;
        if (loop_seconds > 0.0) {
            seconds = std::fmod(seconds, loop_seconds);
            if (seconds < 0.0) seconds += loop_seconds;
        }

        float* frame_out = out + i * static_cast<std::size_t>(channels);
        for (const Prepared& source : ready) {
            const double into = seconds - source.offset_seconds;
            // Before the sound begins. Not an error and not a sentinel: a shot
            // longer than its soundtrack is ordinary, and so is a soundtrack
            // placed part-way into one.
            if (into < 0.0) continue;

            // The trim moves the read head into the file and the offset moves
            // the file along the shot, so they add. Same arithmetic as
            // sampleForSlot, in seconds rather than at one slot.
            const double index = (into + source.trim_start_seconds) * source.clip_rate;
            if (index < 0.0 || index >= static_cast<double>(source.last)) continue;

            // Linear between two samples rather than the nearer of them. Where
            // the clip's rate is the device's, the fraction is zero or within a
            // rounding of it and the read is the sample itself; where they
            // differ, which is a 44.1 kHz file on a 48 kHz output,
            // nearest-neighbour is audible as a rasp on speech and this is not.
            //
            // **And it is what makes the matched-rate case right at all**,
            // which is not obvious and is why swapping it for nearest reddens
            // tests that have no resampling in them. An index that should land
            // exactly on sample N arrives as N minus a rounding, so `at` is
            // N - 1 with a fraction of almost one; interpolating gives sample N
            // back, and taking the nearer of the two would give N - 1.
            const std::int64_t at = static_cast<std::int64_t>(index);
            const double frac = index - static_cast<double>(at);
            const std::int64_t next = std::min(at + 1, source.last - 1);
            const AudioClip& clip = *source.clip;
            const double weight = source.gain * envelope;

            if (clip.channels > channels) {
                // More channels in the file than the device has. **Averaged
                // rather than truncated to the first two**: dialogue lives in
                // the centre channel of a 5.1 mix, and taking left and right
                // would drop the one thing a lipsync reference is for.
                double sum = 0.0;
                for (int c = 0; c < clip.channels; ++c) {
                    const double a = sampleOf(clip, at, c);
                    const double b = sampleOf(clip, next, c);
                    sum += a + (b - a) * frac;
                }
                const float value =
                    static_cast<float>(sum / static_cast<double>(clip.channels) * weight);
                for (int c = 0; c < channels; ++c) frame_out[c] += value;
            } else {
                // Fewer or the same. A mono file is heard from both speakers
                // rather than from one, which is what the last channel
                // repeating amounts to.
                for (int c = 0; c < channels; ++c) {
                    const int from_channel = std::min(c, clip.channels - 1);
                    const double a = sampleOf(clip, at, from_channel);
                    const double b = sampleOf(clip, next, from_channel);
                    frame_out[c] += static_cast<float>((a + (b - a) * frac) * weight);
                }
            }
        }
    }

    // Clamped, because several soundtracks at full level sum past full scale
    // and what a driver does with a sample outside [-1, 1] is its own business
    // -- on some it wraps, which is not quiet distortion but a bang. One pass
    // over the buffer, after the mix rather than inside it, so that a sum which
    // goes over and comes back is not clipped twice.
    const std::size_t values = frames * static_cast<std::size_t>(channels);
    for (std::size_t i = 0; i < values; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

}  // namespace animage
