// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_import.h"

#ifdef ANIMAGE_HAVE_AUDIO
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstring>
#endif

namespace audio_import {

bool available() {
#ifdef ANIMAGE_HAVE_AUDIO
    return true;
#else
    return false;
#endif
}

#ifndef ANIMAGE_HAVE_AUDIO

Decoded decode(const QString&) {
    Decoded out;
    out.trouble = QStringLiteral(
        "This build has no audio support: Qt Multimedia was not found when it was "
        "configured.");
    return out;
}

#else

namespace {

// How long to wait for a decoder that has stopped saying anything.
//
// **Not a performance bound -- a liveness one.** QAudioDecoder reports the end
// of a file with a signal, and a backend that fails in a way it has no error
// for simply stops emitting. Without this the nested event loop below spins for
// ever behind a modal dialog, which from the outside is the program hanging on
// a file somebody double-clicked. Generous, because a slow disk is not a fault.
constexpr int kSilenceTimeoutMs = 15000;

// Appends one buffer's samples, converted to float, whatever the decoder chose
// to hand over.
//
// **QAudioDecoder does not promise the format you asked for.** Setting a format
// is a request; a backend is entitled to hand back its own, and the FFmpeg one
// does for some inputs. Reading the buffer's *own* format on every buffer is
// what makes that a non-event rather than silent noise -- the classic version
// of this bug is reading Int16 bytes as Float and getting a scream.
void appendBuffer(const QAudioBuffer& buffer, animage::AudioClip& clip) {
    const QAudioFormat format = buffer.format();
    const int channels = format.channelCount();
    const int frames = buffer.frameCount();
    if (channels <= 0 || frames <= 0) return;

    const std::size_t before = clip.samples.size();
    clip.samples.resize(before + static_cast<std::size_t>(frames) * channels);
    float* out = clip.samples.data() + before;
    const int values = frames * channels;

    switch (format.sampleFormat()) {
        case QAudioFormat::Float: {
            const float* in = buffer.constData<float>();
            std::memcpy(out, in, static_cast<std::size_t>(values) * sizeof(float));
            break;
        }
        case QAudioFormat::Int16: {
            const qint16* in = buffer.constData<qint16>();
            // 32768 and not 32767: the negative end is what the range is, and
            // dividing by the positive one lets a full-scale negative sample
            // come out past -1.0 and clip on the way to the device.
            for (int i = 0; i < values; ++i) out[i] = float(in[i]) / 32768.0f;
            break;
        }
        case QAudioFormat::Int32: {
            const qint32* in = buffer.constData<qint32>();
            for (int i = 0; i < values; ++i) out[i] = float(double(in[i]) / 2147483648.0);
            break;
        }
        case QAudioFormat::UInt8: {
            const quint8* in = buffer.constData<quint8>();
            for (int i = 0; i < values; ++i) out[i] = (float(in[i]) - 128.0f) / 128.0f;
            break;
        }
        default:
            // A format nothing here knows how to read. Dropping the buffer
            // rather than guessing at its layout: silence is recognisable and
            // reinterpreted bytes are a scream through somebody's headphones.
            clip.samples.resize(before);
            break;
    }
}

}  // namespace

Decoded decode(const QString& path) {
    Decoded out;

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        out.trouble = QStringLiteral("There is no file at %1.").arg(path);
        return out;
    }

    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(info.absoluteFilePath()));

    // Asked for, not relied on. appendBuffer reads what each buffer actually
    // says it is; this only tips the backend towards the shape that needs no
    // conversion at all.
    QAudioFormat wanted;
    wanted.setSampleFormat(QAudioFormat::Float);
    decoder.setAudioFormat(wanted);

    animage::AudioClip clip;
    QEventLoop loop;
    QString failure;
    bool capped = false;

    QTimer silence;
    silence.setSingleShot(true);
    silence.setInterval(kSilenceTimeoutMs);
    QObject::connect(&silence, &QTimer::timeout, &loop, [&] {
        failure = QStringLiteral(
            "The decoder stopped responding. The file may be damaged, or in a format "
            "this build cannot read.");
        loop.quit();
    });

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        silence.start();
        const QAudioBuffer buffer = decoder.read();
        if (!buffer.isValid()) return;
        if (clip.rate == 0) {
            clip.rate = buffer.format().sampleRate();
            clip.channels = buffer.format().channelCount();
        }
        // A buffer whose shape changed mid-file. Nothing sensible can be
        // appended to what came before, so the read stops here with what it
        // has, and says so rather than interleaving two different pictures of
        // the same sound.
        if (buffer.format().sampleRate() != clip.rate ||
            buffer.format().channelCount() != clip.channels) {
            failure = QStringLiteral(
                "The file changes sample rate or channel count partway through, which "
                "this cannot read. Converting it to a plain WAV first will work.");
            loop.quit();
            return;
        }
        appendBuffer(buffer, clip);
        if (clip.frames() >= static_cast<std::size_t>(kMaxFrames)) {
            capped = true;
            loop.quit();
        }
    });

    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, [&] { loop.quit(); });
    // qOverload, because QAudioDecoder has a signal and a getter both called
    // `error` and taking the address of the name alone is ambiguous. The
    // signal is the one with a parameter.
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error),
                     &loop, [&](QAudioDecoder::Error) {
                         // Qt's own sentence. It names the codec or the container
                         // where it can, and inventing a friendlier one here would
                         // be throwing away the only detail there is.
                         failure = decoder.errorString();
                         if (failure.isEmpty())
                             failure = QStringLiteral("The decoder would not read this file.");
                         loop.quit();
                     });

    silence.start();
    decoder.start();
    loop.exec();
    decoder.stop();

    if (!failure.isEmpty() && clip.empty()) {
        out.trouble = failure;
        return out;
    }
    if (clip.empty()) {
        out.trouble = QStringLiteral(
            "Nothing came out of that file. It may hold no audio -- a video with no "
            "soundtrack reads this way.");
        return out;
    }
    if (clip.rate <= 0 || clip.channels <= 0) {
        out.trouble = QStringLiteral("The decoder gave no sample rate for that file.");
        return out;
    }

    out.ok = true;
    out.clip = std::move(clip);
    // Partial reads are a success with a sentence, not a failure: what came out
    // is real audio and refusing it would help nobody.
    if (capped) {
        out.trouble = QStringLiteral("Only the first ten minutes were read.");
    } else if (!failure.isEmpty()) {
        out.trouble = QStringLiteral("The file ended early: %1").arg(failure);
    }
    return out;
}

#endif  // ANIMAGE_HAVE_AUDIO

}  // namespace audio_import
