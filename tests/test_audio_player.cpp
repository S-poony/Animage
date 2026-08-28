// SPDX-License-Identifier: GPL-3.0-or-later
//
// The handover between the interface and a device, driven by a fake one.
//
// **What this pins is a subtraction that nobody can hear.** `AudioPlayer`
// counts two different things: frames it has handed to a device, and frames the
// device says have come out of the speaker. The gap between them is a buffer's
// worth -- a quarter of a second on the machine docs/audio-spike.md was
// measured on, which is six frames of picture at 24 fps. Get the subtraction
// wrong and nothing fails: the picture leans a fixed distance away from the
// sound, on every frame, which is the exact error the whole playback clock
// exists to remove.
//
// So it is checked the way the arithmetic below it is checked -- by handing the
// resource in. `AudioPlayer::openWith` takes an opener, a fake device answers
// `playedFrames()` on command, and the whole of this runs on a machine with no
// sound card. See audio_player.h, and audio_track.h for the same argument one
// layer down.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "audio_device.h"
#include "audio_player.h"
#include "audio_render.h"
#include "testing.h"

using namespace animage;

namespace {

constexpr int kRate = 48000;
constexpr int kFps = 24;

// A device that pulls when it is told to and plays out when it is told to.
//
// **The two are separate on purpose**, because on a real device they are: a
// driver takes a buffer's worth of samples long before any of them is audible,
// and everything this test is about lives in that gap.
class FakeDevice : public AudioDevice {
public:
    FakeDevice(Fill fill, int rate, int channels)
        : fill_(std::move(fill)), rate_(rate), channels_(channels) {}

    int rate() const override { return rate_; }
    int channels() const override { return channels_; }
    std::int64_t playedFrames() const override { return played_; }
    void stop() override { stopped_ = true; }

    // A fake is always on the output it opened and always running: what those
    // two answer for is a machine whose speakers move about, which is not a
    // thing this test is about and not a thing a fake can be.
    QString outputId() const override { return QStringLiteral("fake"); }
    bool healthy() const override { return !stopped_; }

    // The driver asking for samples.
    std::vector<float> pull(std::size_t frames) {
        std::vector<float> out(frames * static_cast<std::size_t>(channels_), 7.0f);
        const std::size_t filled = fill_ ? fill_(out.data(), frames) : 0;
        last_fill_ = filled;
        if (filled < frames)
            std::fill(out.begin() +
                          static_cast<std::ptrdiff_t>(filled * static_cast<std::size_t>(channels_)),
                      out.end(), 0.0f);
        return out;
    }

    // The speaker catching up.
    void playOut(std::int64_t frames) { played_ += frames; }

    std::size_t lastFill() const { return last_fill_; }
    bool stopped() const { return stopped_; }

private:
    Fill fill_;
    int rate_ = 0;
    int channels_ = 0;
    std::int64_t played_ = 0;
    std::size_t last_fill_ = 0;
    bool stopped_ = false;
};

std::shared_ptr<const AudioClip> ramp(std::int64_t frames) {
    auto clip = std::make_shared<AudioClip>();
    clip->rate = kRate;
    clip->channels = 1;
    clip->samples.resize(static_cast<std::size_t>(frames));
    for (std::int64_t f = 0; f < frames; ++f)
        clip->samples[static_cast<std::size_t>(f)] = static_cast<float>(f) / 131072.0f;
    return clip;
}

std::shared_ptr<const AudioProgram> programAt(std::size_t slot) {
    auto program = std::make_shared<AudioProgram>();
    program->sources.push_back({ramp(48000), AudioPlacement{}});
    program->rate = kRate;
    program->channels = 1;
    program->fps = kFps;
    program->start_slot = slot;
    return program;
}

// Opens a player onto a fake, and hands back the fake so a test can drive it.
FakeDevice* attach(AudioPlayer& player) {
    FakeDevice* raw = nullptr;
    const bool ok = player.openWith(
        [&raw](int rate, int channels, AudioDevice::Fill fill, QString*) {
            auto fake = std::make_unique<FakeDevice>(std::move(fill), rate, channels);
            raw = fake.get();
            return std::unique_ptr<AudioDevice>(std::move(fake));
        },
        kRate, 1);
    CHECK(ok);
    return raw;
}

// --- what comes out ---------------------------------------------------------

void withNothingPublishedThePlayerAnswersShortAndTheDeviceGoesQuiet() {
    TEST("with nothing to play the player answers short");
    AudioPlayer player;
    FakeDevice* device = attach(player);
    const std::vector<float> out = device->pull(64);
    CHECK_EQ(device->lastFill(), std::size_t(0));
    for (float v : out) CHECK_NEAR(v, 0.0f, 1e-9);
}

void aPublishedProgramIsReadFromItsBeginning() {
    TEST("a published program is read from its beginning, and then onwards");
    AudioPlayer player;
    FakeDevice* device = attach(player);
    const auto program = programAt(0);
    player.play(program);

    std::vector<float> expected(200);
    renderAudio(*program, 0, expected.data(), 200);

    const std::vector<float> first = device->pull(120);
    const std::vector<float> second = device->pull(80);
    for (std::size_t i = 0; i < 120; ++i) CHECK_NEAR(first[i], expected[i], 1e-7);
    for (std::size_t i = 0; i < 80; ++i) CHECK_NEAR(second[i], expected[120 + i], 1e-7);
}

// A scrub is a burst on every frame the playhead is dragged past, so this is
// the case that happens dozens of times a second while somebody reads a track.
void publishingAgainStartsTheNewOneAtItsBeginning() {
    TEST("publishing again starts the new program at its beginning");
    AudioPlayer player;
    FakeDevice* device = attach(player);
    player.play(programAt(0));
    device->pull(500);

    const auto next = programAt(40);
    player.play(next);
    std::vector<float> expected(64);
    renderAudio(*next, 0, expected.data(), 64);

    const std::vector<float> out = device->pull(64);
    for (std::size_t i = 0; i < 64; ++i) CHECK_NEAR(out[i], expected[i], 1e-7);
}

// --- the subtraction --------------------------------------------------------

void nothingHasBeenPlayedUntilTheDeviceHasPlayedIt() {
    TEST("a program has played nothing until the device has played it");
    AudioPlayer player;
    FakeDevice* device = attach(player);
    player.play(programAt(0));
    CHECK_EQ(player.playedFrames(), std::int64_t(0));

    // Handed over is not heard. A driver takes a buffer's worth before a
    // microsecond of it is audible.
    device->pull(1000);
    CHECK_EQ(player.playedFrames(), std::int64_t(0));

    device->playOut(400);
    CHECK_EQ(player.playedFrames(), std::int64_t(400));
}

// **The whole reason this class counts what it hands over.** A program
// published while a device is already loaded is not heard until everything in
// front of it has been -- a quarter of a second on the machine the spike ran
// on, which is six frames of picture. A count taken at the moment of publishing
// would say the sound had started when it had not.
void aProgramPublishedBehindAFullBufferWaitsForIt() {
    TEST("a program published behind a full buffer is not heard until the buffer is");
    AudioPlayer player;
    FakeDevice* device = attach(player);

    // A quarter of a second of the first program is already inside the device.
    player.play(programAt(0));
    device->pull(12000);

    player.play(programAt(40));
    CHECK_EQ(player.playedFrames(), std::int64_t(0));

    // The speaker is still working through what was in front of it.
    device->playOut(5000);
    CHECK_EQ(player.playedFrames(), std::int64_t(0));
    device->playOut(6999);
    CHECK_EQ(player.playedFrames(), std::int64_t(0));

    // And the moment it reaches the new program, the count starts.
    device->playOut(1);
    CHECK_EQ(player.playedFrames(), std::int64_t(0));
    device->playOut(250);
    CHECK_EQ(player.playedFrames(), std::int64_t(250));
}

// The same fact from the other side: frames handed to the device *after* a
// publish belong to the new program and must not be counted in front of it.
void framesHandedAfterAPublishBelongToTheNewProgram() {
    TEST("frames handed after a publish belong to the new program");
    AudioPlayer player;
    FakeDevice* device = attach(player);
    player.play(programAt(0));
    device->pull(2000);

    player.play(programAt(10));
    device->pull(2000);  // all of this is the new program

    device->playOut(2000);  // the old program finishes
    CHECK_EQ(player.playedFrames(), std::int64_t(0));
    device->playOut(1500);
    CHECK_EQ(player.playedFrames(), std::int64_t(1500));
}

void closingForgetsEverythingAndStopsTheDevice() {
    TEST("closing stops the device and forgets what it had counted");
    AudioPlayer player;
    FakeDevice* device = attach(player);
    player.play(programAt(0));
    device->pull(1000);
    device->playOut(1000);
    CHECK_EQ(player.playedFrames(), std::int64_t(1000));

    player.close();
    CHECK(!player.running());
    CHECK_EQ(player.playedFrames(), std::int64_t(0));

    // A second device starts from nothing rather than inheriting a count that
    // belonged to the first one.
    FakeDevice* second = attach(player);
    player.play(programAt(0));
    second->pull(100);
    second->playOut(100);
    CHECK_EQ(player.playedFrames(), std::int64_t(100));
}

void silenceLeavesTheDeviceOpenAndGoesQuiet() {
    TEST("silence leaves the device open and goes quiet");
    AudioPlayer player;
    FakeDevice* device = attach(player);
    player.play(programAt(0));
    device->pull(100);

    player.silence();
    const std::vector<float> out = device->pull(64);
    CHECK(player.running());
    CHECK_EQ(device->lastFill(), std::size_t(0));
    for (float v : out) CHECK_NEAR(v, 0.0f, 1e-9);
}

}  // namespace

int main() {
    withNothingPublishedThePlayerAnswersShortAndTheDeviceGoesQuiet();
    aPublishedProgramIsReadFromItsBeginning();
    publishingAgainStartsTheNewOneAtItsBeginning();
    nothingHasBeenPlayedUntilTheDeviceHasPlayedIt();
    aProgramPublishedBehindAFullBufferWaitsForIt();
    framesHandedAfterAPublishBelongToTheNewProgram();
    closingForgetsEverythingAndStopsTheDevice();
    silenceLeavesTheDeviceOpenAndGoesQuiet();
    return testing::summarise("audio player");
}
