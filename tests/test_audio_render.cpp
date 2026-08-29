// SPDX-License-Identifier: GPL-3.0-or-later
//
// What is read out of a decoded clip: the samples a device gets handed, and the
// shape a timeline row draws.
//
// **The companion to test_audio.cpp and the same argument.** That one pins the
// picture's side of the arithmetic -- which slot the playhead should be on --
// and this one pins the sound's: which samples come out, in what order, at what
// level, and what happens at the seam. Both of them exist because GitHub's
// runners have no audio output, so anything that opens one there fails or
// hangs, and neither the loop seam nor a sub-frame offset is a thing anybody
// finds by listening on the one machine that has speakers.
//
// Nothing here opens an audio device or links Qt.

#include <cstdint>
#include <memory>
#include <vector>

#include "audio_peaks.h"
#include "audio_render.h"
#include "testing.h"

using namespace animage;

namespace {

// 48 kHz at 24 fps: 2000 device frames per frame of picture, a whole number,
// which is what keeps every expectation below readable. Rates that do not
// divide evenly get their own test.
constexpr int kRate = 48000;
constexpr int kFps = 24;
constexpr std::int64_t kPerFrame = kRate / kFps;

// A sample whose value says which sample it is. Both terms are dyadic, so they
// are exact in a float and a comparison can be tight rather than generous --
// a test that has to allow a percent cannot tell resampling from a wrong index.
float valueAt(std::int64_t frame, int channel) {
    return static_cast<float>(frame) / 131072.0f + static_cast<float>(channel) / 16.0f;
}

std::shared_ptr<const AudioClip> ramp(int rate, int channels, std::int64_t frames) {
    auto clip = std::make_shared<AudioClip>();
    clip->rate = rate;
    clip->channels = channels;
    clip->samples.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels));
    for (std::int64_t f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c)
            clip->samples[static_cast<std::size_t>(f * channels + c)] = valueAt(f, c);
    return clip;
}

AudioProgram oneSound(std::shared_ptr<const AudioClip> clip, AudioPlacement placement,
                      int channels = 1) {
    AudioProgram program;
    program.sources.push_back({std::move(clip), placement});
    program.rate = kRate;
    program.channels = channels;
    program.fps = kFps;
    return program;
}

std::vector<float> render(const AudioProgram& program, std::int64_t from, std::size_t frames) {
    std::vector<float> out(frames * static_cast<std::size_t>(std::max(1, program.channels)),
                           // Filled with something that is not silence, so that a
                           // renderer which writes nothing fails rather than passing
                           // on a buffer that happened to be zero.
                           7.0f);
    renderAudio(program, from, out.data(), frames);
    return out;
}

// --- reading a file at the rate it was decoded at ---------------------------

void aClipAtTheDeviceRateIsReadSampleForSample() {
    TEST("a clip at the device's own rate is read sample for sample");
    const AudioProgram program = oneSound(ramp(kRate, 1, 48000), AudioPlacement{});

    const std::vector<float> first = render(program, 0, 8);
    for (int i = 0; i < 8; ++i) CHECK_NEAR(first[i], valueAt(i, 0), 1e-7);

    // **`from` is a position, not a cursor.** Asking for the same stretch twice
    // gives the same samples, which is what lets a device be re-pointed at a
    // sample count rather than having to be told what it has already had.
    const std::vector<float> later = render(program, 5000, 8);
    for (int i = 0; i < 8; ++i) CHECK_NEAR(later[i], valueAt(5000 + i, 0), 1e-7);
}

void splittingACallInTwoChangesNothing() {
    TEST("a call split in two gives the same samples as one call");
    const AudioProgram program = oneSound(ramp(kRate, 2, 48000), AudioPlacement{}, 2);
    const std::vector<float> whole = render(program, 1234, 100);
    const std::vector<float> front = render(program, 1234, 40);
    const std::vector<float> back = render(program, 1274, 60);
    for (std::size_t i = 0; i < 80; ++i) CHECK_NEAR(whole[i], front[i], 1e-7);
    for (std::size_t i = 0; i < 120; ++i) CHECK_NEAR(whole[80 + i], back[i], 1e-7);
}

// --- where the sound is, and where it is not --------------------------------

void nothingIsHeardBeforeTheSoundStarts() {
    TEST("nothing is heard before the sound starts");
    AudioPlacement at_ten;
    at_ten.offset_frames = 10.0;
    const AudioProgram program = oneSound(ramp(kRate, 1, 48000), at_ten);

    // Frame 10 is 20000 device frames in. The sample before it is silence and
    // the one on it is the file's first.
    const std::vector<float> across = render(program, 10 * kPerFrame - 4, 8);
    for (int i = 0; i < 4; ++i) CHECK_NEAR(across[i], 0.0f, 1e-9);
    for (int i = 0; i < 4; ++i) CHECK_NEAR(across[4 + i], valueAt(i, 0), 1e-7);
}

// The whole reason the offset is a double: 1/24 of a second is 42 ms, which is
// most of the way to a syllable.
void aSoundPlacedHalfWayIntoAFrameStartsHalfWayIntoIt() {
    TEST("a sound placed between two frames starts between them");
    AudioPlacement half;
    half.offset_frames = 10.5;
    const AudioProgram program = oneSound(ramp(kRate, 1, 48000), half);

    const std::int64_t starts = 10 * kPerFrame + kPerFrame / 2;
    const std::vector<float> across = render(program, starts - 2, 4);
    CHECK_NEAR(across[0], 0.0f, 1e-9);
    CHECK_NEAR(across[1], 0.0f, 1e-9);
    CHECK_NEAR(across[2], valueAt(0, 0), 1e-7);
    CHECK_NEAR(across[3], valueAt(1, 0), 1e-7);
}

void aSoundtrackThatStartsBeforeTheShotIsAlreadyRunningAtFrameZero() {
    TEST("a soundtrack that starts before the shot is already running at frame 0");
    AudioPlacement early;
    early.offset_frames = -5.0;
    const AudioProgram program = oneSound(ramp(kRate, 1, 48000), early);
    const std::vector<float> at_start = render(program, 0, 4);
    for (int i = 0; i < 4; ++i) CHECK_NEAR(at_start[i], valueAt(5 * kPerFrame + i, 0), 1e-7);
}

void pastTheOutPointIsSilenceAndNotAnError() {
    TEST("past the out-point is silence");
    AudioPlacement cropped;
    cropped.trim_end_seconds = 0.25;  // a second-long file, a quarter taken off the back
    const AudioProgram program = oneSound(ramp(kRate, 1, kRate), cropped);

    const std::vector<float> across = render(program, 36000 - 2, 4);
    CHECK_NEAR(across[0], valueAt(35998, 0), 1e-7);
    CHECK_NEAR(across[1], valueAt(35999, 0), 1e-7);
    CHECK_NEAR(across[2], 0.0f, 1e-9);
    CHECK_NEAR(across[3], 0.0f, 1e-9);
}

// The property croppingTheFrontMovesTheReadHeadAndNotTheSound pins for the
// index, asserted here on the samples themselves: the audio under a given frame
// is the audio that was there before the crop.
void croppingTheFrontLeavesTheAudioUnderAFrameWhereItWas() {
    TEST("cropping the front leaves the audio under a frame where it was");
    const auto clip = ramp(kRate, 1, kRate);

    AudioPlacement whole;
    whole.offset_frames = 10.0;
    AudioPlacement cropped;
    cropped.offset_frames = 10.0 + 0.25 * kFps;  // the block's front moved right
    cropped.trim_start_seconds = 0.25;

    const std::vector<float> before = render(oneSound(clip, whole), 20 * kPerFrame, 4);
    const std::vector<float> after = render(oneSound(clip, cropped), 20 * kPerFrame, 4);
    for (int i = 0; i < 4; ++i) CHECK_NEAR(before[i], after[i], 1e-7);
}

// --- the loop seam ----------------------------------------------------------

// The seam is the case docs/importing.md expected to have to correct twice --
// once for the picture and once for the sound. It does not: the position wraps,
// so the sound wraps on the same number the picture does.
void theLoopSeamLandsOnASampleAndNotOnAFrameBoundary() {
    TEST("the loop seam lands on a sample");
    AudioProgram program = oneSound(ramp(kRate, 1, 48000), AudioPlacement{});
    program.loop_slots = 10;  // a ten-frame shot

    const std::int64_t seam = 10 * kPerFrame;
    const std::vector<float> across = render(program, seam - 2, 4);
    CHECK_NEAR(across[0], valueAt(seam - 2, 0), 1e-7);
    CHECK_NEAR(across[1], valueAt(seam - 1, 0), 1e-7);
    // And straight back to the top of the shot, with no gap and no repeat.
    CHECK_NEAR(across[2], valueAt(0, 0), 1e-7);
    CHECK_NEAR(across[3], valueAt(1, 0), 1e-7);
}

void aShotShorterThanItsSoundtrackNeverReachesTheRest() {
    TEST("a shot shorter than its soundtrack never reaches the rest of it");
    AudioProgram program = oneSound(ramp(kRate, 1, 48000), AudioPlacement{});
    program.loop_slots = 4;  // four frames of picture over a second of sound
    // Nothing past 8000 samples is ever heard, however long it plays for.
    const std::vector<float> late = render(program, 100 * 4 * kPerFrame, 4);
    for (int i = 0; i < 4; ++i) CHECK_NEAR(late[i], valueAt(i, 0), 1e-7);
}

void startingPartWayThroughTheShotStartsPartWayThroughTheSound() {
    TEST("playing from frame 6 starts six frames into the sound");
    AudioProgram program = oneSound(ramp(kRate, 1, 48000), AudioPlacement{});
    program.start_slot = 6;
    const std::vector<float> out = render(program, 0, 4);
    for (int i = 0; i < 4; ++i) CHECK_NEAR(out[i], valueAt(6 * kPerFrame + i, 0), 1e-7);
}

// --- a burst, which is what a scrub is --------------------------------------

void aBurstStopsAtItsLengthAndRampsAtBothEnds() {
    TEST("a burst stops at its length and ramps at both ends");
    AudioProgram program = oneSound(ramp(kRate, 1, 48000), AudioPlacement{});
    program.length = kPerFrame;
    program.fade = 96;  // two milliseconds

    // It opens from nothing rather than stepping the speaker cone.
    const std::vector<float> opening = render(program, 0, 4);
    CHECK_NEAR(opening[0], 0.0f, 1e-9);
    CHECK(opening[1] < valueAt(1, 0));
    CHECK(opening[1] > 0.0f);

    // In the middle it is the file, untouched.
    const std::vector<float> middle = render(program, 1000, 4);
    for (int i = 0; i < 4; ++i) CHECK_NEAR(middle[i], valueAt(1000 + i, 0), 1e-7);

    // And it closes the same way, then stops.
    const std::vector<float> closing = render(program, kPerFrame - 2, 4);
    CHECK(closing[0] < valueAt(kPerFrame - 2, 0));
    CHECK(closing[0] > 0.0f);
    CHECK_NEAR(closing[2], 0.0f, 1e-9);
    CHECK_NEAR(closing[3], 0.0f, 1e-9);
}

// --- what the level does ----------------------------------------------------

void gainIsWhatYouHearAndZeroIsSilent() {
    TEST("gain is what you hear, and zero is silent");
    AudioPlacement half;
    half.gain = 0.5;
    const std::vector<float> quiet = render(oneSound(ramp(kRate, 1, 48000), half), 100, 4);
    for (int i = 0; i < 4; ++i) CHECK_NEAR(quiet[i], valueAt(100 + i, 0) * 0.5f, 1e-7);

    AudioPlacement down;
    down.gain = 0.0;
    const std::vector<float> none = render(oneSound(ramp(kRate, 1, 48000), down), 100, 4);
    for (int i = 0; i < 4; ++i) CHECK_NEAR(none[i], 0.0f, 1e-9);
}

// --- rates and channels that do not match the device ------------------------

void aClipAtHalfTheDeviceRateIsReadAtHalfSpeed() {
    TEST("a 24 kHz clip on a 48 kHz device is read at half speed");
    const AudioProgram program = oneSound(ramp(kRate / 2, 1, 24000), AudioPlacement{});
    const std::vector<float> out = render(program, 0, 6);
    // Every other output sample is a sample of the file; the ones between are
    // the midpoint, because linear and not nearest.
    CHECK_NEAR(out[0], valueAt(0, 0), 1e-7);
    CHECK_NEAR(out[1], (valueAt(0, 0) + valueAt(1, 0)) / 2.0f, 1e-7);
    CHECK_NEAR(out[2], valueAt(1, 0), 1e-7);
    CHECK_NEAR(out[3], (valueAt(1, 0) + valueAt(2, 0)) / 2.0f, 1e-7);
    CHECK_NEAR(out[4], valueAt(2, 0), 1e-7);
}

void aMonoFileIsHeardFromBothSpeakers() {
    TEST("a mono file is heard from both speakers");
    AudioProgram program = oneSound(ramp(kRate, 1, 48000), AudioPlacement{}, 2);
    const std::vector<float> out = render(program, 500, 3);
    for (int i = 0; i < 3; ++i) {
        CHECK_NEAR(out[i * 2 + 0], valueAt(500 + i, 0), 1e-7);
        CHECK_NEAR(out[i * 2 + 1], valueAt(500 + i, 0), 1e-7);
    }
}

void aStereoFileKeepsItsSides() {
    TEST("a stereo file keeps its sides");
    AudioProgram program = oneSound(ramp(kRate, 2, 48000), AudioPlacement{}, 2);
    const std::vector<float> out = render(program, 500, 3);
    for (int i = 0; i < 3; ++i) {
        CHECK_NEAR(out[i * 2 + 0], valueAt(500 + i, 0), 1e-7);
        CHECK_NEAR(out[i * 2 + 1], valueAt(500 + i, 1), 1e-7);
    }
}

// Dialogue lives in the centre channel of a surround mix, so a downmix that
// took left and right would drop the one thing a lipsync reference is for.
void moreChannelsThanTheDeviceHasAreAveragedAndNotTruncated() {
    TEST("a file with more channels than the device is averaged, not truncated");
    AudioProgram program = oneSound(ramp(kRate, 4, 48000), AudioPlacement{}, 2);
    const std::vector<float> out = render(program, 500, 2);
    for (int i = 0; i < 2; ++i) {
        const float mixed = (valueAt(500 + i, 0) + valueAt(500 + i, 1) + valueAt(500 + i, 2) +
                             valueAt(500 + i, 3)) /
                            4.0f;
        CHECK_NEAR(out[i * 2 + 0], mixed, 1e-6);
        CHECK_NEAR(out[i * 2 + 1], mixed, 1e-6);
    }
}

// --- nothing to play, which must still write something ----------------------

// A device buffer that is left alone holds whatever was in it last time round,
// which comes out as a stutter of old audio rather than as nothing.
void nothingToPlayIsSilenceAndNotAnUntouchedBuffer() {
    TEST("nothing to play writes silence rather than leaving the buffer alone");
    AudioProgram empty;
    empty.rate = kRate;
    empty.channels = 2;
    empty.fps = kFps;
    const std::vector<float> nothing = render(empty, 0, 4);
    for (float v : nothing) CHECK_NEAR(v, 0.0f, 1e-9);

    // A soundtrack whose file has not decoded yet is the same case: the row
    // exists, the samples do not.
    AudioProgram undecoded = oneSound(nullptr, AudioPlacement{}, 2);
    const std::vector<float> quiet = render(undecoded, 0, 4);
    for (float v : quiet) CHECK_NEAR(v, 0.0f, 1e-9);
}

void nonsenseRatesAnswerRatherThanDivideByZero() {
    TEST("nonsense rates answer rather than dividing by zero");
    AudioProgram program = oneSound(ramp(kRate, 1, 48000), AudioPlacement{});
    program.rate = 0;
    for (float v : render(program, 0, 4)) CHECK_NEAR(v, 0.0f, 1e-9);
    program.rate = kRate;
    program.fps = 0;
    for (float v : render(program, 0, 4)) CHECK_NEAR(v, 0.0f, 1e-9);
}

// --- two soundtracks, which the model already allows ------------------------

void twoSoundtracksAreSummedAndCannotPushEachOtherPastFullScale() {
    TEST("two soundtracks are summed, and the sum cannot leave the range");
    auto loud = std::make_shared<AudioClip>();
    loud->rate = kRate;
    loud->channels = 1;
    loud->samples.assign(1000, 0.75f);

    AudioProgram program;
    program.sources.push_back({loud, AudioPlacement{}});
    program.sources.push_back({loud, AudioPlacement{}});
    program.rate = kRate;
    program.channels = 1;
    program.fps = kFps;

    // 1.5 would wrap on some drivers, which is a bang rather than distortion.
    for (float v : render(program, 0, 4)) CHECK_NEAR(v, 1.0f, 1e-6);
}

// --- the shape a row draws --------------------------------------------------

// A clip whose loudness is known bucket by bucket: four blocks of 64 frames at
// four levels, which is exactly one bucket each at the default size.
std::shared_ptr<const AudioClip> steps(const std::vector<float>& levels, int channels = 1) {
    auto clip = std::make_shared<AudioClip>();
    clip->rate = kRate;
    clip->channels = channels;
    for (float level : levels)
        for (int f = 0; f < 64; ++f)
            for (int c = 0; c < channels; ++c) clip->samples.push_back(c == 0 ? level : 0.0f);
    return clip;
}

void aBucketHoldsTheLoudestSampleInIt() {
    TEST("a bucket holds the loudest sample in it");
    const AudioPeaks peaks = peaksOf(*steps({0.25f, 1.0f, 0.5f, 0.0f}));
    CHECK_EQ(peaks.per_bucket, 64);
    CHECK_EQ(peaks.buckets.size(), std::size_t(4));
    CHECK_NEAR(peaks.buckets[0], 0.25f, 1e-6);
    CHECK_NEAR(peaks.buckets[1], 1.0f, 1e-6);
    CHECK_NEAR(peaks.buckets[2], 0.5f, 1e-6);
    CHECK_NEAR(peaks.buckets[3], 0.0f, 1e-6);
    CHECK_NEAR(peaks.loudest, 1.0f, 1e-6);
}

// **The magnitude and not the value.** A waveform drawn from signed samples
// would be a row of half-height shapes wherever the sound happened to swing
// negative, which is most of it.
void theLoudnessIsAMagnitude() {
    TEST("loudness is a magnitude, so a negative sample is as loud as a positive one");
    const AudioPeaks peaks = peaksOf(*steps({-0.8f, 0.4f}));
    CHECK_NEAR(peaks.buckets[0], 0.8f, 1e-6);
    CHECK_NEAR(peaks.loudest, 0.8f, 1e-6);
}

// A line recorded onto one side of a stereo file is still a line. Averaging the
// channels would draw it at half the height it is.
void aChannelOnItsOwnIsAsLoudAsTheFile() {
    TEST("a sound on one channel is drawn at its own height, not at half of it");
    const AudioPeaks peaks = peaksOf(*steps({1.0f}, 2));
    CHECK_NEAR(peaks.buckets[0], 1.0f, 1e-6);
}

// Normalised to the file's own peak, which is the choice this makes: a quiet
// dialogue take drawn against full scale is a ripple with no syllables in it.
void loudnessIsRelativeToTheLoudestThingInTheFile() {
    TEST("loudness is relative to the loudest thing in the file");
    const AudioPeaks peaks = peaksOf(*steps({0.1f, 0.2f}));
    CHECK_NEAR(loudnessBetween(peaks, 0, 64), 0.5f, 1e-6);
    CHECK_NEAR(loudnessBetween(peaks, 64, 128), 1.0f, 1e-6);
    // Across both, the louder one wins -- a column narrower than a syllable
    // must not average it away.
    CHECK_NEAR(loudnessBetween(peaks, 0, 128), 1.0f, 1e-6);
}

// A row draws columns that are past the sound at both ends, and what is there
// is silence rather than an error.
void offEitherEndIsSilence() {
    TEST("off either end of the file is silence");
    const AudioPeaks peaks = peaksOf(*steps({1.0f, 1.0f}));
    CHECK_NEAR(loudnessBetween(peaks, -5000, -4000), 0.0f, 1e-6);
    CHECK_NEAR(loudnessBetween(peaks, 100000, 200000), 0.0f, 1e-6);
    // Straddling the front still reads the part that is there.
    CHECK_NEAR(loudnessBetween(peaks, -100, 64), 1.0f, 1e-6);
}

void aSilentFileAnswersRatherThanDividingByZero() {
    TEST("a silent file answers rather than dividing by zero");
    const AudioPeaks peaks = peaksOf(*steps({0.0f, 0.0f}));
    CHECK_NEAR(peaks.loudest, 0.0f, 1e-9);
    CHECK_NEAR(loudnessBetween(peaks, 0, 128), 0.0f, 1e-9);

    // And so does a clip nothing has decoded into.
    const AudioPeaks none = peaksOf(AudioClip{});
    CHECK(none.empty());
    CHECK_NEAR(loudnessBetween(none, 0, 128), 0.0f, 1e-9);
}

// A bucket has to be narrower than a column of the timeline, or the row draws a
// shape it invented between two of them. A cell is 26 pixels and a frame at
// 24 fps is 2000 samples at 48 kHz, so a column is about 77.
void aBucketIsNarrowerThanAColumnOfTheTimeline() {
    TEST("a bucket is narrower than a column of the timeline");
    const AudioPeaks peaks = peaksOf(*steps({1.0f}));
    const double samples_per_column = double(kRate) / (double(kFps) * 26.0);
    CHECK(double(peaks.per_bucket) < samples_per_column);
}

}  // namespace

int main() {
    aClipAtTheDeviceRateIsReadSampleForSample();
    splittingACallInTwoChangesNothing();
    nothingIsHeardBeforeTheSoundStarts();
    aSoundPlacedHalfWayIntoAFrameStartsHalfWayIntoIt();
    aSoundtrackThatStartsBeforeTheShotIsAlreadyRunningAtFrameZero();
    pastTheOutPointIsSilenceAndNotAnError();
    croppingTheFrontLeavesTheAudioUnderAFrameWhereItWas();
    theLoopSeamLandsOnASampleAndNotOnAFrameBoundary();
    aShotShorterThanItsSoundtrackNeverReachesTheRest();
    startingPartWayThroughTheShotStartsPartWayThroughTheSound();
    aBurstStopsAtItsLengthAndRampsAtBothEnds();
    gainIsWhatYouHearAndZeroIsSilent();
    aClipAtHalfTheDeviceRateIsReadAtHalfSpeed();
    aMonoFileIsHeardFromBothSpeakers();
    aStereoFileKeepsItsSides();
    moreChannelsThanTheDeviceHasAreAveragedAndNotTruncated();
    nothingToPlayIsSilenceAndNotAnUntouchedBuffer();
    nonsenseRatesAnswerRatherThanDivideByZero();
    twoSoundtracksAreSummedAndCannotPushEachOtherPastFullScale();
    aBucketHoldsTheLoudestSampleInIt();
    theLoudnessIsAMagnitude();
    aChannelOnItsOwnIsAsLoudAsTheFile();
    loudnessIsRelativeToTheLoudestThingInTheFile();
    offEitherEndIsSilence();
    aSilentFileAnswersRatherThanDividingByZero();
    aBucketIsNarrowerThanAColumnOfTheTimeline();
    return testing::summarise("audio render");
}
