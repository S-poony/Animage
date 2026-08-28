// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading a soundtrack file, and what the import does with what comes out.
//
// **This one needs Qt Multimedia's backend and the two beside it do not**, which
// is why it is its own binary rather than more cases in `test_audio` or
// `test_serialise`. Those test the model and the format and run anywhere;
// `QAudioDecoder` is the one part of audio that a Qt without a media backend
// cannot do at all, and a test that cannot run is better skipped loudly than
// merged into one that could have run.
//
// It needs no audio *device*: decoding produces buffers and opens no output, so
// this runs on a machine with no sound card -- which every CI runner is. See
// docs/audio-spike.md, which is where that distinction was measured.
//
// The fixture is written here rather than committed. A WAV header is forty-four
// bytes and a test that builds its own input cannot be broken by somebody
// tidying up a binary in the repository.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

#include "audio_import.h"
#include "testing.h"

using namespace animage;

namespace {

void put32(QByteArray& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.append(char((v >> (8 * i)) & 0xff));
}
void put16(QByteArray& out, std::uint16_t v) {
    for (int i = 0; i < 2; ++i) out.append(char((v >> (8 * i)) & 0xff));
}

// A 16-bit PCM WAV of a sine, written by hand.
QByteArray makeWav(int rate, int channels, int frames, double hz) {
    QByteArray data;
    for (int i = 0; i < frames; ++i) {
        const double t = double(i) / rate;
        const auto v = std::int16_t(std::lround(0.5 * 32767.0 *
                                                std::sin(2.0 * std::numbers::pi * hz * t)));
        for (int c = 0; c < channels; ++c) put16(data, std::uint16_t(v));
    }

    const std::uint32_t byte_rate = std::uint32_t(rate) * channels * 2;
    QByteArray out;
    out.append("RIFF");
    put32(out, std::uint32_t(36 + data.size()));
    out.append("WAVEfmt ");
    put32(out, 16);                            // PCM header size
    put16(out, 1);                             // PCM
    put16(out, std::uint16_t(channels));
    put32(out, std::uint32_t(rate));
    put32(out, byte_rate);
    put16(out, std::uint16_t(channels * 2));   // block align
    put16(out, 16);                            // bits
    out.append("data");
    put32(out, std::uint32_t(data.size()));
    out.append(data);
    return out;
}

QString writeWav(const QDir& dir, const QString& name, const QByteArray& bytes) {
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return QString();
    file.write(bytes);
    file.close();
    return path;
}

void aWavComesBackWithItsRateItsChannelsAndItsLength(const QDir& dir) {
    TEST("a wav comes back with its rate, its channels and its length");
    const QString path = writeWav(dir, QStringLiteral("tone.wav"), makeWav(48000, 2, 24000, 440.0));
    CHECK(!path.isEmpty());

    const audio_import::Decoded decoded = audio_import::decode(path);
    if (!decoded.ok) {
        std::printf("    decoder said: %s\n", qPrintable(decoded.trouble));
    }
    CHECK(decoded.ok);
    if (!decoded.ok) return;

    CHECK_EQ(decoded.clip.rate, 48000);
    CHECK_EQ(decoded.clip.channels, 2);

    // Half a second at 48 kHz. Not exact: a decoder is entitled to hand back
    // whole buffers and some pad the last one, so this asks that the length is
    // right to within a buffer rather than to the sample. What must be exact is
    // the *rate*, because that is what the sync arithmetic divides by.
    const std::size_t frames = decoded.clip.frames();
    CHECK(frames >= 23000);
    CHECK(frames <= 25000);

    // Twelve frames of picture at 24 fps, which is what the recap says.
    CHECK_EQ(decoded.clip.framesAtFps(24), std::size_t{12});

    // The samples are audible rather than zeroes -- a decode that produced
    // silence would pass every check above.
    float peak = 0.0f;
    for (float s : decoded.clip.samples) peak = std::max(peak, std::abs(s));
    CHECK(peak > 0.2f);
    CHECK(peak <= 1.0f);
}

void amonoFileStaysMono(const QDir& dir) {
    TEST("a mono file stays mono rather than being widened on the way in");
    const QString path = writeWav(dir, QStringLiteral("mono.wav"), makeWav(44100, 1, 4410, 220.0));
    const audio_import::Decoded decoded = audio_import::decode(path);
    CHECK(decoded.ok);
    if (!decoded.ok) return;
    // Kept as it came. Converting here would be a second thing that could be
    // wrong about a file, and what opens the device asks the clip for its own
    // rate and channel count.
    CHECK_EQ(decoded.clip.channels, 1);
    CHECK_EQ(decoded.clip.rate, 44100);
}

void afileThatIsNotAudioIsRefusedWithASentence(const QDir& dir) {
    TEST("a file that is not audio is refused with something a person can read");
    const QString path = writeWav(dir, QStringLiteral("nonsense.wav"), QByteArray("not a wav", 9));
    const audio_import::Decoded decoded = audio_import::decode(path);
    CHECK(!decoded.ok);
    // A sentence and not a code. Decoding is the one place a file the user
    // picked fails for reasons that are the file's, so what comes back has to
    // be showable.
    CHECK(!decoded.trouble.isEmpty());
}

void afileThatIsNotThereSaysSoWithoutOpeningADecoder() {
    TEST("a missing file says so");
    const audio_import::Decoded decoded =
        audio_import::decode(QStringLiteral("/no/such/file/at/all.wav"));
    CHECK(!decoded.ok);
    CHECK(!decoded.trouble.isEmpty());
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("audio decode:\n");

    if (!audio_import::available()) {
        std::printf("  Qt Multimedia is not in this build: nothing to test.\n");
        return 0;
    }

    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("  could not make a temporary directory\n");
        return 1;
    }
    const QDir dir(temp.path());

    aWavComesBackWithItsRateItsChannelsAndItsLength(dir);
    amonoFileStaysMono(dir);
    afileThatIsNotAudioIsRefusedWithASentence(dir);
    afileThatIsNotThereSaysSoWithoutOpeningADecoder();
    return testing::summarise("audio decode");
}
