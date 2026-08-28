// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_track.h"

#include <algorithm>

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

std::int64_t sampleForSlot(std::size_t slot, int offset_frames, int rate, int fps) {
    if (rate <= 0 || fps <= 0) return -1;

    // Signed all the way through. `slot` is unsigned and `offset_frames` can be
    // negative, so subtracting one from the other in the wrong order is how a
    // soundtrack that starts before frame 0 becomes a sample index near
    // 18 quintillion. Widen first, subtract second.
    const std::int64_t into = static_cast<std::int64_t>(slot) - offset_frames;
    if (into < 0) return -1;
    return into * rate / fps;
}

}  // namespace animage
