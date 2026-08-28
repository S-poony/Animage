// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_peaks.h"

#include <algorithm>
#include <cmath>

namespace animage {

AudioPeaks peaksOf(const AudioClip& clip, int per_bucket) {
    AudioPeaks peaks;
    if (per_bucket <= 0 || clip.channels <= 0 || clip.empty()) return peaks;

    peaks.per_bucket = per_bucket;
    const std::size_t frames = clip.frames();
    const std::size_t channels = static_cast<std::size_t>(clip.channels);
    const std::size_t count =
        (frames + static_cast<std::size_t>(per_bucket) - 1) / static_cast<std::size_t>(per_bucket);
    peaks.buckets.assign(count, 0.0f);

    // One pass, taking the largest magnitude across every channel. Loudest of
    // the channels rather than their average: a line recorded onto one side of
    // a stereo file is still a line, and averaging would draw it at half the
    // height it is.
    for (std::size_t frame = 0; frame < frames; ++frame) {
        float loudest = 0.0f;
        const float* at = clip.samples.data() + frame * channels;
        for (std::size_t c = 0; c < channels; ++c) loudest = std::max(loudest, std::fabs(at[c]));
        float& bucket = peaks.buckets[frame / static_cast<std::size_t>(per_bucket)];
        bucket = std::max(bucket, loudest);
    }

    for (float value : peaks.buckets) peaks.loudest = std::max(peaks.loudest, value);
    return peaks;
}

float loudnessBetween(const AudioPeaks& peaks, std::int64_t from, std::int64_t to) {
    if (peaks.empty() || peaks.loudest <= 0.0f) return 0.0f;

    const std::int64_t count = static_cast<std::int64_t>(peaks.buckets.size());
    // Off either end is silence and not an error: a row draws columns that are
    // past the sound, and the answer there is that there is nothing there.
    std::int64_t first = from / peaks.per_bucket;
    std::int64_t last = (to - 1) / peaks.per_bucket;
    if (from < 0) first = 0;
    if (last >= count) last = count - 1;
    if (last < first || first >= count || last < 0) return 0.0f;

    // **At least the bucket it starts in**, so a range narrower than one bucket
    // still reads something. A column of the timeline is wider than a bucket by
    // construction -- see peaksOf -- but a caller need not be a column.
    float loudest = 0.0f;
    for (std::int64_t i = first; i <= last; ++i)
        loudest = std::max(loudest, peaks.buckets[static_cast<std::size_t>(i)]);
    return std::min(1.0f, loudest / peaks.loudest);
}

}  // namespace animage
