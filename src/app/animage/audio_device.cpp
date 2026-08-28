// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_device.h"

#ifdef ANIMAGE_HAVE_AUDIO
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#endif

bool AudioDevice::available() {
#ifdef ANIMAGE_HAVE_AUDIO
    return true;
#else
    return false;
#endif
}

#ifndef ANIMAGE_HAVE_AUDIO

std::unique_ptr<AudioDevice> AudioDevice::open(int, int, Fill, QString* trouble) {
    if (trouble)
        *trouble = QStringLiteral(
            "This build has no audio support: Qt Multimedia was not found when it was "
            "configured.");
    return nullptr;
}

#else

namespace {

// What a device may ask for in one go, in frames, and therefore how much
// scratch the Int16 path holds.
//
// **A callback may not allocate**, so the buffer it converts through is sized
// once here rather than grown when a driver asks for more than expected. A
// device that does ask for more gets a short read and is called again, which
// `QIODevice` allows for a sequential device and which costs one extra call.
// 16384 frames is a third of a second at 48 kHz, against the 250 ms buffer the
// spike measured.
constexpr qint64 kScratchFrames = 16384;

// The source the sink pulls from.
//
// **Everything here runs on the device's thread.** `readData` is on a deadline:
// samples not returned in time are an underrun, heard as a click. So it does no
// allocation, takes no lock, and calls exactly one thing -- the `Fill` it was
// given, whose own contract is in audio_device.h.
class Pump : public QIODevice {
public:
    Pump(AudioDevice::Fill fill, int channels, QAudioFormat::SampleFormat format)
        : fill_(std::move(fill)), channels_(std::max(1, channels)), format_(format) {
        if (format_ != QAudioFormat::Float)
            scratch_.resize(static_cast<std::size_t>(kScratchFrames) *
                            static_cast<std::size_t>(channels_));
    }

    qint64 readData(char* data, qint64 max) override {
        const int width =
            format_ == QAudioFormat::Float ? int(sizeof(float)) : int(sizeof(qint16));
        const qint64 per_frame = qint64(channels_) * width;
        qint64 frames = max / per_frame;
        if (frames <= 0) return 0;

        if (format_ == QAudioFormat::Float) {
            // Straight into the device's own buffer: the format the renderer
            // works in is the format the driver wants, so there is nothing to
            // convert and nothing to copy through.
            float* out = reinterpret_cast<float*>(data);
            fillOrSilence(out, frames);
        } else {
            frames = std::min(frames, kScratchFrames);
            float* out = scratch_.data();
            fillOrSilence(out, frames);
            qint16* to = reinterpret_cast<qint16*>(data);
            const qint64 values = frames * channels_;
            // 32767 and not 32768 on the way *out*, where the decoder divides
            // by 32768 on the way in. The two are not inconsistent: coming in,
            // the negative end is what the range is; going out, 1.0 times 32768
            // is one past the positive end and wraps to full-scale negative.
            for (qint64 i = 0; i < values; ++i)
                to[i] = static_cast<qint16>(
                    std::lround(std::clamp(out[i], -1.0f, 1.0f) * 32767.0f));
        }
        return frames * per_frame;
    }

    // Never written to. A device a sink pulls from is read-only, and saying so
    // is what stops Qt asking.
    qint64 writeData(const char*, qint64) override { return 0; }

    bool isSequential() const override { return true; }

    // **Without this the sink pulls nothing at all**, and there is no error
    // anywhere. `QIODevice::read` consults `bytesAvailable()` before it will
    // call `readData`, and the base class answers 0 for a sequential device --
    // so a source with unlimited samples to offer looks empty. Found by
    // `tests/audio_probe` printing zeroes; see docs/audio-spike.md.
    qint64 bytesAvailable() const override {
        return (1 << 20) + QIODevice::bytesAvailable();
    }

private:
    void fillOrSilence(float* out, qint64 frames) {
        const std::size_t asked = static_cast<std::size_t>(frames);
        const std::size_t filled = fill_ ? fill_(out, asked) : 0;
        if (filled >= asked) return;
        // A caller with nothing to say may answer short, and what a device gets
        // for the rest must be silence rather than whatever the buffer held
        // last time round -- which comes out as a stutter of old audio.
        std::fill(out + filled * static_cast<std::size_t>(channels_),
                  out + asked * static_cast<std::size_t>(channels_), 0.0f);
    }

    AudioDevice::Fill fill_;
    int channels_ = 2;
    QAudioFormat::SampleFormat format_ = QAudioFormat::Float;
    std::vector<float> scratch_;
};

class QtAudioDevice : public AudioDevice {
public:
    QtAudioDevice(const QAudioDevice& output, const QAudioFormat& format, Fill fill)
        : rate_(format.sampleRate()), channels_(format.channelCount()) {
        // The pump before the sink, and destroyed after it: the sink pulls on
        // the pump, so a pump that went away first would be pulled on by a
        // thread that is still running. Member order below is what enforces
        // that, and `stop()` in the destructor is what makes it certain.
        pump_ = std::make_unique<Pump>(std::move(fill), channels_, format.sampleFormat());
        pump_->open(QIODevice::ReadOnly);
        sink_ = std::make_unique<QAudioSink>(output, format);
        sink_->start(pump_.get());
    }

    ~QtAudioDevice() override { stop(); }

    bool opened() const { return sink_ && sink_->error() == QAudio::NoError; }
    QString error() const {
        return sink_ ? QStringLiteral("the audio output would not start (Qt error %1)")
                           .arg(int(sink_->error()))
                     : QStringLiteral("no audio output was opened");
    }

    int rate() const override { return rate_; }
    int channels() const override { return channels_; }

    std::int64_t playedFrames() const override {
        if (!sink_ || rate_ <= 0) return 0;
        // Microseconds to frames in one step. Through whole milliseconds it
        // would lose up to 48 frames a reading, which is a fortieth of a
        // picture frame thrown away on every tick -- the same rounding
        // `slotForPlayedFrames` exists to avoid, arriving one function earlier.
        return sink_->processedUSecs() * std::int64_t(rate_) / 1000000;
    }

    void stop() override {
        if (sink_) sink_->stop();
    }

private:
    std::unique_ptr<Pump> pump_;
    std::unique_ptr<QAudioSink> sink_;
    int rate_ = 0;
    int channels_ = 0;
};

}  // namespace

std::unique_ptr<AudioDevice> AudioDevice::open(int rate, int channels, Fill fill,
                                               QString* trouble) {
    const auto say = [trouble](const QString& what) {
        if (trouble) *trouble = what;
        return std::unique_ptr<AudioDevice>();
    };

    const QAudioDevice output = QMediaDevices::defaultAudioOutput();
    if (output.isNull())
        return say(QStringLiteral("This computer has no audio output."));

    QAudioFormat format;
    format.setSampleRate(rate > 0 ? rate : 48000);
    format.setChannelCount(channels > 0 ? channels : 2);
    format.setSampleFormat(QAudioFormat::Float);

    // **The request is the clip's rate, and the fallbacks are the driver's.**
    // Asking for the rate a file decoded to is what makes the ordinary case an
    // exact sample-for-sample read; where a driver refuses it, `renderAudio`
    // resamples to whatever was agreed instead, so a refusal costs quality
    // rather than sound.
    if (!output.isFormatSupported(format)) {
        QAudioFormat theirs = output.preferredFormat();
        theirs.setSampleFormat(QAudioFormat::Float);
        if (output.isFormatSupported(theirs)) {
            format = theirs;
        } else {
            format = output.preferredFormat();
            if (format.sampleFormat() != QAudioFormat::Float &&
                format.sampleFormat() != QAudioFormat::Int16)
                return say(QStringLiteral(
                    "This computer's audio output uses a sample format Animage cannot "
                    "write."));
        }
    }

    auto device = std::make_unique<QtAudioDevice>(output, format, std::move(fill));
    if (!device->opened()) return say(device->error());
    return device;
}

#endif
