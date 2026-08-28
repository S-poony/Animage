// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio_track.h"

namespace animage {

// How loud a soundtrack is, sample by sample, at a resolution a row can draw.
//
// **Derived data twice over**, and that is why it is a separate thing from the
// clip rather than a field on it: an `AudioClip` is what a decode produced, and
// this is what somebody worked out from it afterwards. It follows the clip's
// bargain exactly -- losing it costs arithmetic over samples that are already
// in memory, and it is never written to a project.
//
// It exists so that a paint does not walk half a million floats. A row is
// repainted on every playhead move, and the whole point of a waveform is that
// it is there while you drag.
//
// **Rectified, not signed.** The row draws the sound as a shape rising from the
// bottom, because the height of that shape is already the level and dragging it
// is already how the level is set -- so what a column needs is one number, "how
// loud is it here", and not a pair. A centred waveform would be the other way
// of drawing this and would have to give up saying the level with the same
// shape.
struct AudioPeaks {
    // How many frames of audio each bucket covers.
    int per_bucket = 0;

    // The loudest sample in each bucket, as a magnitude.
    std::vector<float> buckets;

    // The loudest sample in the whole file, which is what the row divides by.
    //
    // **Normalised on purpose, and it is a real choice.** Drawn against full
    // scale, an ordinary dialogue take recorded at a sensible level is a low
    // ripple with no shape in it -- and a waveform whose syllables cannot be
    // told apart is a waveform that has not earned its row. Normalising means
    // the row says *where the sound is* rather than *how loud the file is*,
    // which is the question somebody reading a track is asking. How loud it
    // will be is the block's height, and that is a different number.
    float loudest = 0.0f;

    bool empty() const { return buckets.empty() || per_bucket <= 0; }
};

// A bucket per `per_bucket` frames of audio.
//
// The default is small enough that a pixel of the timeline is always more than
// one bucket: a cell is 26 pixels and a frame at 24 fps is 2000 samples at
// 48 kHz, so a pixel is about 77 samples. At 64 the shape a column draws is
// never invented from a bucket wider than the column.
AudioPeaks peaksOf(const AudioClip& clip, int per_bucket = 64);

// The loudest magnitude between two sample positions, relative to the loudest
// in the file -- so 0 is silence and 1 is the peak of the take.
//
// `from` and `to` are frames of audio and may run off either end of the file,
// which is not an error: a row draws columns that are past the sound, and what
// is there is silence.
float loudnessBetween(const AudioPeaks& peaks, std::int64_t from, std::int64_t to);

}  // namespace animage
