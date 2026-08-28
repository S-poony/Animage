// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_track.h"

#include <algorithm>
#include <cmath>

namespace animage {

std::size_t slotForPlayedFrames(std::size_t start_slot, std::int64_t played, int rate, int fps,
                                std::size_t count) {
    if (count == 0) return 0;
    if (rate <= 0 || fps <= 0) return start_slot % count;

    // Clamped rather than trusted. `played` comes from a device driver, and a
    // driver that reports a position going backwards across a restart would
    // otherwise turn into a colossal positive slot through the unsigned
    // arithmetic below -- a picture that jumps to a random frame rather than
    // one that stalls, which is much harder to recognise as a fault.
    const std::int64_t frames = std::max<std::int64_t>(0, played) * fps / rate;

    // The modulo is what makes the loop seam right by construction. The picture
    // and the sound wrap on the same number because there is only one number:
    // wrapping is a property of this expression rather than two wraps that have
    // to be kept agreeing with each other.
    return (start_slot + static_cast<std::size_t>(frames)) % count;
}

std::int64_t sampleForSlot(std::size_t slot, const AudioPlacement& placement, int rate, int fps) {
    if (rate <= 0 || fps <= 0) return 0;

    // In doubles the whole way, because the offset is fractional and the whole
    // point of it being fractional is that the fraction survives. Doing this in
    // integers would round the placement back to the frame it was dragged off.
    const double into_frames = static_cast<double>(slot) - placement.offset_frames;
    const double into_seconds = into_frames / static_cast<double>(fps);

    // The trim moves the read head into the file; the offset moves the file
    // along the shot. They add rather than fighting, which is what makes a
    // trimmed sound stay where it was put instead of jumping when it is cropped.
    const double seconds = into_seconds + placement.trim_start_seconds;

    // Floor and not truncate. Truncation rounds towards zero, so a slot a
    // fraction *before* the sound would come back as sample 0 -- audible, on a
    // frame that should be silent, and only on the negative side. That is the
    // kind of asymmetry nobody finds by listening.
    return static_cast<std::int64_t>(std::floor(seconds * static_cast<double>(rate)));
}

std::int64_t lastAudibleSample(const AudioClip& clip, const AudioPlacement& placement) {
    if (clip.rate <= 0) return 0;
    const std::int64_t total = static_cast<std::int64_t>(clip.frames());
    const std::int64_t cut = static_cast<std::int64_t>(
        std::llround(placement.trim_end_seconds * static_cast<double>(clip.rate)));
    return std::max<std::int64_t>(0, total - std::max<std::int64_t>(0, cut));
}

double audibleSeconds(const AudioClip& clip, const AudioPlacement& placement) {
    if (clip.rate <= 0) return 0.0;
    const double whole = static_cast<double>(clip.frames()) / static_cast<double>(clip.rate);
    const double left =
        whole - std::max(0.0, placement.trim_start_seconds) - std::max(0.0, placement.trim_end_seconds);
    return std::max(0.0, left);
}

std::size_t audibleFrames(const AudioClip& clip, const AudioPlacement& placement, int fps) {
    if (fps <= 0) return 0;
    const double seconds = audibleSeconds(clip, placement);
    if (seconds <= 0.0) return 0;
    // Rounded up: a sound that ends a tenth of the way into a frame still makes
    // a noise on it, and a row that stopped short would be drawing less than
    // you can hear.
    return static_cast<std::size_t>(std::ceil(seconds * static_cast<double>(fps)));
}

}  // namespace animage
