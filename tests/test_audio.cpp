// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sync arithmetic, driven by a fake sample count.
//
// **This is the whole reason those functions take a number instead of owning a
// device.** GitHub's runners have no audio output, so anything that opens one
// there fails or hangs -- and the arithmetic the whole of lipsync rests on is
// exactly the part that must not go untested. Handing the count in means the
// loop seam and the stall case can be pinned with no hardware at all, on every
// platform, on every push. See docs/importing.md, "the playback clock".
//
// Nothing here opens an audio device or links Qt.

#include <cstdint>

#include "audio_track.h"
#include "scene.h"
#include "testing.h"

using namespace animage;

namespace {

// 48 kHz at 24 fps: 2000 frames of audio per frame of picture, which is a whole
// number and keeps every expectation below readable. Rates that do not divide
// evenly get their own test.
constexpr int kRate = 48000;
constexpr int kFps = 24;
constexpr std::int64_t kPerFrame = kRate / kFps;

void theSlotAdvancesWithTheAudioAndNotWithTheClock() {
    TEST("the slot advances with the audio and not with the clock");
    // Nothing played, nothing advanced.
    CHECK_EQ(slotForPlayedFrames(0, 0, kRate, kFps, 100), std::size_t(0));

    // One frame's worth of audio is one frame of picture.
    CHECK_EQ(slotForPlayedFrames(0, kPerFrame, kRate, kFps, 100), std::size_t(1));
    CHECK_EQ(slotForPlayedFrames(0, 10 * kPerFrame, kRate, kFps, 100), std::size_t(10));

    // A sample short of the boundary is still the frame before it. This is the
    // whole of frame accuracy: the picture changes when the sound crosses into
    // the frame, not when it is nearly there.
    CHECK_EQ(slotForPlayedFrames(0, kPerFrame - 1, kRate, kFps, 100), std::size_t(0));
    CHECK_EQ(slotForPlayedFrames(0, 2 * kPerFrame - 1, kRate, kFps, 100), std::size_t(1));

    // And it starts from wherever Play was pressed.
    CHECK_EQ(slotForPlayedFrames(30, 5 * kPerFrame, kRate, kFps, 100), std::size_t(35));
}

// The seam is where the first draft of this design expected to have to correct
// itself twice -- once for the picture wrapping and once for the sound. It does
// not, and this is what says so: there is one number, so both wrap on it.
void theLoopSeamIsOneNumberAndNotTwo() {
    TEST("the loop seam is one number and not two that have to agree");
    const std::size_t count = 10;

    // Round the loop exactly: back to where it started, not to 10.
    CHECK_EQ(slotForPlayedFrames(0, 10 * kPerFrame, kRate, kFps, count), std::size_t(0));

    // And on round the second and the fortieth, with no accumulated error --
    // which is the failure the note predicted for a design that restarted the
    // audio each time round: "a frame out after forty passes of a three-second
    // shot, which is an ordinary amount of looping for lipsync".
    CHECK_EQ(slotForPlayedFrames(0, 13 * kPerFrame, kRate, kFps, count), std::size_t(3));
    CHECK_EQ(slotForPlayedFrames(0, 403 * kPerFrame, kRate, kFps, count), std::size_t(3));

    // Starting mid-shot wraps at the same place.
    CHECK_EQ(slotForPlayedFrames(7, 5 * kPerFrame, kRate, kFps, count), std::size_t(2));
}

// docs/playback-resolution.md measured 4K dropping between a quarter and two
// fifths of its frames. A stall does not only drop paints -- the 1 ms tick does
// not fire either. What must survive it is that the picture *catches up* rather
// than running slow, which is the good property the wall clock already had and
// which this must not lose.
void aStallIsCaughtUpWithRatherThanRunSlow() {
    TEST("a stall is caught up with rather than run slow");
    // Nothing was asked for 500 ms -- twelve frames' worth at 24 fps -- and
    // then the question is asked again. The answer is where the sound is, not
    // twelve frames after wherever the last answer was.
    CHECK_EQ(slotForPlayedFrames(0, 12 * kPerFrame, kRate, kFps, 1000), std::size_t(12));

    // The point being that this function has no memory of its own answers. Two
    // calls with the same count give the same slot however much time passed
    // between them, and a call that skipped ten of them lands where the audio
    // is. That is what makes the picture jump and catch up instead of drifting.
    CHECK_EQ(slotForPlayedFrames(0, 12 * kPerFrame, kRate, kFps, 1000),
             slotForPlayedFrames(0, 12 * kPerFrame, kRate, kFps, 1000));
}

void aRateThatDoesNotDivideEvenlyTruncatesRatherThanRounding() {
    TEST("a rate that does not divide evenly truncates rather than rounds");
    // 44.1 kHz at 24 fps is 1837.5 audio frames per picture frame, so every
    // other boundary lands mid-sample. Truncation is right: a slot must not
    // advance until the sound has actually reached it.
    const int rate = 44100;
    CHECK_EQ(slotForPlayedFrames(0, 1837, rate, kFps, 100), std::size_t(0));
    CHECK_EQ(slotForPlayedFrames(0, 1838, rate, kFps, 100), std::size_t(1));
    CHECK_EQ(slotForPlayedFrames(0, 3674, rate, kFps, 100), std::size_t(1));
    CHECK_EQ(slotForPlayedFrames(0, 3675, rate, kFps, 100), std::size_t(2));

    // Ten seconds in, the error has not accumulated: 240 frames of picture is
    // 441000 samples exactly.
    CHECK_EQ(slotForPlayedFrames(0, 441000, rate, kFps, 1000), std::size_t(240));
}

// A driver reporting a position that goes backwards -- across a restart, say --
// must stall the picture rather than throw it somewhere random. Without the
// clamp the unsigned arithmetic turns a small negative into an enormous
// positive, and a picture on a wrong frame is much harder to recognise as a
// fault than a picture that is not moving.
void aDeviceReportingBackwardsDoesNotThrowThePictureAcrossTheShot() {
    TEST("a device reporting backwards stalls the picture rather than throwing it");
    CHECK_EQ(slotForPlayedFrames(5, -1, kRate, kFps, 100), std::size_t(5));
    CHECK_EQ(slotForPlayedFrames(5, -1000000, kRate, kFps, 100), std::size_t(5));
}

void anEmptyShotAndNonsenseRatesAnswerRatherThanDivideByZero() {
    TEST("an empty shot and nonsense rates answer rather than divide by zero");
    CHECK_EQ(slotForPlayedFrames(0, 48000, kRate, kFps, 0), std::size_t(0));
    CHECK_EQ(slotForPlayedFrames(3, 48000, 0, kFps, 100), std::size_t(3));
    CHECK_EQ(slotForPlayedFrames(3, 48000, kRate, 0, 100), std::size_t(3));
    // A start slot past the end still lands inside the shot.
    CHECK_EQ(slotForPlayedFrames(250, 0, 0, kFps, 100), std::size_t(50));
}

void whichSampleIsHeardAtASlot() {
    TEST("which sample of the file is heard at a slot");
    AudioPlacement at_zero;
    CHECK_EQ(sampleForSlot(0, at_zero, kRate, kFps), std::int64_t(0));
    CHECK_EQ(sampleForSlot(1, at_zero, kRate, kFps), kPerFrame);
    CHECK_EQ(sampleForSlot(24, at_zero, kRate, kFps), std::int64_t(kRate));

    // Placed at frame 10, the file's first sample is heard on frame 10.
    AudioPlacement at_ten;
    at_ten.offset_frames = 10.0;
    CHECK_EQ(sampleForSlot(10, at_ten, kRate, kFps), std::int64_t(0));
    CHECK_EQ(sampleForSlot(11, at_ten, kRate, kFps), kPerFrame);

    // **Before it starts the number says by how much**, rather than reporting a
    // sentinel. A caller feeding a device plays silence until the index reaches
    // zero and then reads on, which is what a sound placed part-way into a
    // frame needs -- half of that frame is silence and half of it is sound.
    CHECK_EQ(sampleForSlot(9, at_ten, kRate, kFps), -kPerFrame);
    CHECK_EQ(sampleForSlot(0, at_ten, kRate, kFps), -10 * kPerFrame);
}

// The whole reason the offset is a double. 1/24 of a second is 42 ms, which is
// most of the way to a syllable, so a sound placed to the nearest frame is not
// placed at all.
void aSoundCanSitBetweenTwoFrames() {
    TEST("a sound placed between two frames is heard between them");
    AudioPlacement half;
    half.offset_frames = 10.5;

    // Frame 10 begins half a frame before the sound does.
    CHECK_EQ(sampleForSlot(10, half, kRate, kFps), -kPerFrame / 2);
    // Frame 11 is half a frame into it.
    CHECK_EQ(sampleForSlot(11, half, kRate, kFps), kPerFrame / 2);

    // And the fraction survives being small. A tenth of a frame at 24 fps is
    // 4 ms, which is the order of precision a drag actually has.
    AudioPlacement nudged;
    nudged.offset_frames = 10.1;
    CHECK_EQ(sampleForSlot(10, nudged, kRate, kFps), -kPerFrame / 10);
}

// Floor and not truncate. Truncation rounds towards zero, so a slot a fraction
// *before* the sound would come back as sample 0 -- audible, on a frame that
// should be silent, and only on the negative side. Nobody finds that by ear.
void theSampleIndexIsFlooredAndNotTruncated() {
    TEST("a slot just before the sound reads as before it, not as its first sample");
    AudioPlacement just_after;
    just_after.offset_frames = 0.5;
    const std::int64_t at_zero = sampleForSlot(0, just_after, kRate, kFps);
    CHECK(at_zero < 0);
    CHECK_EQ(at_zero, -kPerFrame / 2);
}

void aSoundtrackCanStartBeforeTheShotDoes() {
    TEST("a soundtrack can start before the shot does");
    AudioPlacement early;
    early.offset_frames = -5.0;
    CHECK_EQ(sampleForSlot(0, early, kRate, kFps), 5 * kPerFrame);
    CHECK_EQ(sampleForSlot(5, early, kRate, kFps), 10 * kPerFrame);
}

// --- cropping, which moves two numbers and no samples ----------------------

void croppingTheFrontMovesTheReadHeadAndNotTheSound() {
    TEST("cropping the front of a sound leaves the rest where it was");
    // A sound at frame 10, cropped by a quarter of a second. Cropping the front
    // means the audio under every remaining frame is the audio that was there
    // before -- so the in-point and the offset move together.
    AudioPlacement cropped;
    cropped.offset_frames = 10.0 + 0.25 * kFps;  // the block's front moved right
    cropped.trim_start_seconds = 0.25;

    // At the new front, a quarter of a second into the file.
    CHECK_EQ(sampleForSlot(16, cropped, kRate, kFps), std::int64_t(0.25 * kRate));

    // And the frame that was showing a given sample before the crop is showing
    // the same sample after it, which is the whole claim.
    AudioPlacement whole;
    whole.offset_frames = 10.0;
    CHECK_EQ(sampleForSlot(20, whole, kRate, kFps), sampleForSlot(20, cropped, kRate, kFps));
}

void croppingIsTwoNumbersAndNoSamples() {
    TEST("a crop changes what is audible without touching the clip");
    AudioClip clip;
    clip.rate = kRate;
    clip.channels = 1;
    clip.samples.assign(kRate * 2, 0.5f);  // two seconds
    const std::size_t before = clip.samples.size();

    AudioPlacement whole;
    CHECK_NEAR(audibleSeconds(clip, whole), 2.0, 1e-9);
    CHECK_EQ(audibleFrames(clip, whole, kFps), std::size_t(48));
    CHECK_EQ(lastAudibleSample(clip, whole), std::int64_t(kRate * 2));

    AudioPlacement cropped;
    cropped.trim_start_seconds = 0.5;
    cropped.trim_end_seconds = 0.25;
    CHECK_NEAR(audibleSeconds(clip, cropped), 1.25, 1e-9);
    CHECK_EQ(audibleFrames(clip, cropped, kFps), std::size_t(30));
    // The out-point counts from the end of the file, and the in-point does not
    // enter it: one names a sample and the other names where reading starts.
    CHECK_EQ(lastAudibleSample(clip, cropped), std::int64_t(kRate * 2 - kRate / 4));

    // **Non-destructive**, which is the whole reason to do it this way: the
    // samples are untouched, so the crop undoes by putting two numbers back.
    CHECK_EQ(clip.samples.size(), before);
}

void aLengthThatEndsPartWayIntoAFrameStillUsesThatFrame() {
    TEST("a sound ending a tenth of the way into a frame still occupies it");
    AudioClip clip;
    clip.rate = kRate;
    clip.channels = 1;
    // A second and a tenth of a frame.
    clip.samples.assign(kRate + kPerFrame / 10, 0.5f);
    // Rounded up, or the row would stop short of what you can hear.
    CHECK_EQ(audibleFrames(clip, AudioPlacement{}, kFps), std::size_t(25));
}

void theSceneCarriesAudioTracksBesideItsTracksAndNotAmongThem() {
    TEST("the scene carries audio tracks beside its tracks and not among them");
    Scene scene;
    CHECK(scene.audio_tracks.empty());

    AudioTrack sound;
    sound.id = 1;
    sound.name = "dialogue";
    sound.source = "dialogue.wav";
    sound.placement.offset_frames = 12;
    scene.audio_tracks.push_back(sound);

    // The one property the whole "audio is not a track" argument rests on:
    // adding a soundtrack leaves every loop over scene.tracks meaning exactly
    // what it meant. There are about twenty of them.
    CHECK_EQ(scene.tracks.size(), std::size_t(0));
    CHECK_EQ(scene.audio_tracks.size(), std::size_t(1));

    // And it does not lengthen the shot. What a soundtrack is worth is judged
    // against the drawings; a scene whose length came from its audio would grow
    // every time somebody imported a reference that ran long.
    CHECK_EQ(scene.shotFrames(), std::size_t(0));

    const AudioTrack* found = scene.findAudioTrack(1);
    CHECK(found != nullptr);
    CHECK_EQ(found->name, std::string("dialogue"));
    CHECK(scene.findAudioTrack(2) == nullptr);

    // An id from the same counter as a drawing track's, and never confusable
    // for one, because the two are looked up in different lists by different
    // functions. See TimelineWidget's two selections.
    CHECK(scene.findTrack(1) == nullptr);
}

void gainIsWhatYouWillHearAndTheBarHeightIsTheSameNumber() {
    TEST("gain is what you will hear, and the bar height is the same number");
    AudioTrack sound;
    CHECK_NEAR(sound.placement.gain, 1.0, 1e-12);
    sound.placement.gain = 0.0;  // silent at the bottom, so no separate mute is needed
    CHECK_NEAR(sound.placement.gain, 0.0, 1e-12);
}

}  // namespace

int main() {
    theSlotAdvancesWithTheAudioAndNotWithTheClock();
    theLoopSeamIsOneNumberAndNotTwo();
    aStallIsCaughtUpWithRatherThanRunSlow();
    aRateThatDoesNotDivideEvenlyTruncatesRatherThanRounding();
    aDeviceReportingBackwardsDoesNotThrowThePictureAcrossTheShot();
    anEmptyShotAndNonsenseRatesAnswerRatherThanDivideByZero();
    whichSampleIsHeardAtASlot();
    aSoundCanSitBetweenTwoFrames();
    theSampleIndexIsFlooredAndNotTruncated();
    aSoundtrackCanStartBeforeTheShotDoes();
    croppingTheFrontMovesTheReadHeadAndNotTheSound();
    croppingIsTwoNumbersAndNoSamples();
    aLengthThatEndsPartWayIntoAFrameStillUsesThatFrame();
    theSceneCarriesAudioTracksBesideItsTracksAndNotAmongThem();
    gainIsWhatYouWillHearAndTheBarHeightIsTheSameNumber();
    return testing::summarise("audio");
}
