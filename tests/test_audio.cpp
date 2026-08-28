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
    CHECK_EQ(sampleForSlot(0, 0, kRate, kFps), std::int64_t(0));
    CHECK_EQ(sampleForSlot(1, 0, kRate, kFps), kPerFrame);
    CHECK_EQ(sampleForSlot(24, 0, kRate, kFps), std::int64_t(kRate));

    // Placed at frame 10, the file's first sample is heard on frame 10.
    CHECK_EQ(sampleForSlot(10, 10, kRate, kFps), std::int64_t(0));
    CHECK_EQ(sampleForSlot(11, 10, kRate, kFps), kPerFrame);

    // Before it starts there is nothing to hear, and saying so with a negative
    // beats a bool a caller can forget to look at.
    CHECK_EQ(sampleForSlot(9, 10, kRate, kFps), std::int64_t(-1));
    CHECK_EQ(sampleForSlot(0, 10, kRate, kFps), std::int64_t(-1));
}

// A soundtrack that starts before the shot does is ordinary: a line of dialogue
// with a breath in front of it, the word on frame 1 and the breath falling off
// the start. The subtraction has to be done widened, or slot 0 against an
// offset of -5 becomes a sample index near eighteen quintillion.
void aSoundtrackCanStartBeforeTheShotDoes() {
    TEST("a soundtrack can start before the shot does");
    CHECK_EQ(sampleForSlot(0, -5, kRate, kFps), 5 * kPerFrame);
    CHECK_EQ(sampleForSlot(5, -5, kRate, kFps), 10 * kPerFrame);
}

void theSceneCarriesAudioTracksBesideItsTracksAndNotAmongThem() {
    TEST("the scene carries audio tracks beside its tracks and not among them");
    Scene scene;
    CHECK(scene.audio_tracks.empty());

    AudioTrack sound;
    sound.id = 1;
    sound.name = "dialogue";
    sound.source = "dialogue.wav";
    sound.offset_frames = 12;
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
    CHECK_NEAR(sound.gain, 1.0, 1e-12);
    sound.gain = 0.0;  // silent at the bottom, so no separate mute is needed
    CHECK_NEAR(sound.gain, 0.0, 1e-12);
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
    aSoundtrackCanStartBeforeTheShotDoes();
    theSceneCarriesAudioTracksBesideItsTracksAndNotAmongThem();
    gainIsWhatYouWillHearAndTheBarHeightIsTheSameNumber();
    return testing::summarise("audio");
}
