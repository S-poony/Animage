// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_player.h"

#include <algorithm>
#include <utility>

#include "audio_device.h"

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() { close(); }

bool AudioPlayer::open(int rate, int channels, QString* trouble) {
    return openWith(
        [](int r, int c, AudioDevice::Fill fill, QString* said) {
            return AudioDevice::open(r, c, std::move(fill), said);
        },
        rate, channels, trouble);
}

bool AudioPlayer::openWith(const Opener& opener, int rate, int channels, QString* trouble) {
    close();

    // The callback holds a share of the state and nothing else -- not `this`.
    // Its own place in the program is a local it carries, so the only thing two
    // threads meet over is `Shared`, and this object can be moved about or
    // asked questions while the device runs without any of that reaching the
    // audio thread.
    auto shared = shared_;
    auto fill = [shared, seen = std::uint64_t{0}, cursor = std::int64_t{0}](
                    float* out, std::size_t frames) mutable -> std::size_t {
        std::shared_ptr<const animage::AudioProgram> program;
        {
            std::lock_guard<std::mutex> guard(shared->lock);
            program = shared->program;
            if (shared->epoch != seen) {
                seen = shared->epoch;
                cursor = 0;
            }
            // Counted here, inside the same lock that read the program, and
            // before a sample has been written. That pairing is what makes
            // `base` exact: whichever of the two threads takes the lock first,
            // this call's frames land on the correct side of a publish.
            shared->handed += static_cast<std::int64_t>(frames);
        }
        if (!program) return 0;  // the device makes the rest silent
        animage::renderAudio(*program, cursor, out, frames);
        cursor += static_cast<std::int64_t>(frames);
        return frames;
    };

    QString said;
    device_ = opener(rate, channels, std::move(fill), &said);
    if (!device_) {
        if (trouble) *trouble = said;
        return false;
    }
    asked_rate_ = rate;

    // A device that has just opened has nothing to say and no history. Anything
    // left over from the last one would be read against a count that restarted.
    std::lock_guard<std::mutex> guard(shared_->lock);
    shared_->program.reset();
    ++shared_->epoch;
    shared_->handed = 0;
    shared_->base = 0;
    return true;
}

void AudioPlayer::close() {
    // The device first: `stop()` does not return until the callback has, which
    // is what makes it safe to drop the program it was reading.
    device_.reset();
    asked_rate_ = 0;
    std::lock_guard<std::mutex> guard(shared_->lock);
    shared_->program.reset();
    shared_->handed = 0;
    shared_->base = 0;
}

bool AudioPlayer::onTheRightOutput() const {
    if (!device_) return false;
    if (!device_->healthy()) return false;
    const QString now = AudioDevice::defaultOutputId();
    // An empty answer means the machine has no default at all, which is not a
    // reason to throw away a device that is still playing.
    return now.isEmpty() || now == device_->outputId();
}

int AudioPlayer::rate() const { return device_ ? device_->rate() : 0; }
int AudioPlayer::channels() const { return device_ ? device_->channels() : 0; }

void AudioPlayer::play(std::shared_ptr<const animage::AudioProgram> program) {
    std::lock_guard<std::mutex> guard(shared_->lock);
    shared_->program = std::move(program);
    ++shared_->epoch;
    shared_->base = shared_->handed;
}

std::int64_t AudioPlayer::playedFrames() const {
    if (!device_) return 0;
    const std::int64_t played = device_->playedFrames();
    std::int64_t base = 0;
    {
        std::lock_guard<std::mutex> guard(shared_->lock);
        base = shared_->base;
    }
    // Negative until the program's first sample is out of the buffer, and a
    // caller wants "none of it yet" rather than a number pointing backwards.
    return std::max<std::int64_t>(0, played - base);
}
