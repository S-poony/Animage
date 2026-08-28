// SPDX-License-Identifier: GPL-3.0-or-later
//
// A plain Qt program that opens an audio output and watches one number, with
// none of Animage in it.
//
// **It exists to answer the first of the two measurements
// docs/importing.md ends on**, and it is written the way that note asks for:
// the question, the test, and what each answer means, with no guess written
// down anywhere -- a guess here would be read later as a decision somebody
// took, and it would be an invitation to skip the measurement.
//
// > **Does `QAudioSink::processedUSecs()` count audio handed to the device, or
// > audio played out of it?**
// >
// > *Test:* play a file and watch whether the number ever reports more audio
// > than there has been time to play.
// > *If handed over:* `playedMs()` subtracts the audio still queued.
// > *If played out:* use it as it comes.
//
// The whole of the playback clock stands on that number. `onPlaybackTick`
// derives the picture's slot from the system clock today, and the one-line
// change that makes lipsync right is to derive it from what the sound card has
// actually played instead. Get the meaning of this number wrong and nothing
// fails: the picture sits a fixed fraction of a frame away from the sound, on
// every frame, invisibly -- which is exactly the error deriving from the device
// was supposed to remove, arriving through the one number meant to remove it.
//
// It is not a test and must not become one, for `dock_probe`'s reasons.
// Nothing asserts and `ctest` never runs it: GitHub's runners have no audio
// device, so anything opening an output there fails or hangs. That is the same
// fact that makes the sync arithmetic take a sample count as an argument, so a
// fake can drive it with no hardware at all.
//
// ```
// ./build/tests/audio_probe                 two seconds of a tone, a table
// ./build/tests/audio_probe --seconds 5     longer, for a slower machine
// ./build/tests/audio_probe --silent        the same readings, nothing audible
// ./build/tests/audio_probe --list          the outputs this machine has
// ./build/tests/audio_probe --device 2      one of them instead of the default
// ./build/tests/audio_probe --decode f.mp3  decode a file twice, say what it said
// ```
//
// `--decode` is here for a different question from the rest of the file, and it
// is the question this probe exists for in general: **is a message coming out
// of a decode ours or the decoder's?** It reads the same file twice through a
// plain `QAudioDecoder` with none of Animage anywhere near it, and prints what
// came out each time. A message that appears on both passes belongs to the file
// and the codec; one that appears only on the second belongs to whatever
// happened in between, which would be ours.
//
// The default output is whatever the desktop last pointed at, which on a
// machine with a monitor plugged in over HDMI is often the monitor. A buffer
// size is the driver's and not Qt's, so `--device` is how a second reading gets
// taken without changing anything in the sound control panel.
//
// --- If you are an agent reading this, this file is yours --------------------
//
// The same licence `tests/shots.cpp` and `tests/dock_probe.cpp` carry, and for
// the same reason. Change it, add the reading you need, delete the one in your
// way. Nothing depends on any of it. Two things worth keeping: it must stay
// **free of Animage**, because a reading taken through our code answers a
// question about our code, and it must stay a probe rather than a test.
//
// --- How the reading is taken ------------------------------------------------
//
// A tone rather than a file, and that is a departure from the note's wording
// worth defending. What the test needs is to know *exactly* how many bytes Qt
// has taken and when, and a generator knows that by construction -- the sink
// pulls from a `QIODevice` we wrote, so `readData` is the meter. A decoded file
// would add `QAudioDecoder`, a buffer to run out of, and a second thing that
// could be wrong, to measure a number that has nothing to do with where the
// samples came from.
//
// **Three clocks are printed side by side and the answer is which two agree.**
//
// | | what it is |
// |---|---|
// | `wall` | `QElapsedTimer` from just before `start()`. Real time. |
// | `handed` | bytes `readData` has given Qt, in ms. What the sink has *taken*. |
// | `processed` | `processedUSecs()`, in ms. The number under test. |
//
// A sink primes itself: the first `readData` asks for a whole buffer before a
// microsecond of it is audible. So at the first reading `handed` is already
// tens of milliseconds and `wall` is nearly nothing, and the two answers are
// far apart from the very first line rather than after a drift:
//
// - **`processed` tracks `handed`** -- the two stay within a few milliseconds
//   of each other. It counts audio handed over, and `playedMs()` must subtract
//   what is still queued.
// - **`processed` tracks `wall`** -- and so lags `handed` by about a buffer. It
//   counts audio played out, and `playedMs()` is this number as it comes.
//
// **The column to decide on is `handed-processed`, not `ahead`**, and that is a
// correction to the note's own wording rather than a detail. "Whether the number
// ever reports more audio than there has been time to play" is the right idea
// and the wrong meter, because there is no instant to measure `wall` from: the
// stream starts somewhere *inside* `start()`, which takes a third of a second
// on this machine, so `processed` sits a constant few tens of milliseconds
// ahead of any timer started around it. Read literally, that constant answers
// "handed over" on a device that plainly is not -- which is how the first run
// of this probe reported it.
//
// So the two are told apart by whether `handed - processed` is about zero or
// about a buffer. That has no start instant in it at all, and both numbers are
// counted from the same one.

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QIODevice>
#include <QMediaDevices>
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace {

constexpr int kRate = 48000;
constexpr int kChannels = 2;
constexpr double kToneHz = 440.0;

// What the generator claims it can supply, in bytes. Any large number does: it
// is an offer rather than an allocation, and the sink asks for what its own
// buffer has room for.
constexpr qint64 kOffered = 1 << 20;

// The source the sink pulls from, and the meter for how much it has pulled.
//
// **`readData` must be cheap and must never block.** It runs on Qt's audio
// thread, and the whole point of a pull-mode sink is that it asks for samples
// when it wants them; a slow answer here is an underrun, which would show up in
// the table as a stall and be read as the number under test misbehaving. A sine
// is three floating-point operations.
class Tone : public QIODevice {
public:
    explicit Tone(bool silent) : silent_(silent) {}

    // Bytes handed to Qt so far. Read from the interface thread while the audio
    // thread writes it, which is what makes it atomic rather than a plain
    // counter -- a torn read here would be a wrong reading in the table, and a
    // wrong reading is the only thing this program produces.
    qint64 handed() const { return handed_.load(std::memory_order_relaxed); }

    qint64 readData(char* data, qint64 max) override {
        const qint64 frames = max / qint64(kChannels * sizeof(float));
        float* out = reinterpret_cast<float*>(data);
        for (qint64 i = 0; i < frames; ++i) {
            const float v = silent_ ? 0.0f
                                    : 0.2f * float(std::sin(2.0 * std::numbers::pi * kToneHz
                                                            * double(phase_) / double(kRate)));
            for (int c = 0; c < kChannels; ++c) *out++ = v;
            ++phase_;
        }
        const qint64 wrote = frames * qint64(kChannels * sizeof(float));
        handed_.fetch_add(wrote, std::memory_order_relaxed);
        return wrote;
    }

    // Never written to. A `QIODevice` that a sink pulls from is read-only, and
    // saying so is what stops Qt asking.
    qint64 writeData(const char*, qint64) override { return 0; }

    // A generator has no end, which is what a sink wants: `atEnd()` returning
    // true is how a pull-mode sink learns to stop, and this one is stopped by
    // the clock instead.
    bool isSequential() const override { return true; }

    // **Without this the sink pulls nothing at all**, and the table reads zero
    // for ever with no error raised anywhere. `QIODevice::read` consults
    // `bytesAvailable()` before it will call `readData`, and the base class
    // answers 0 for a sequential device -- so a generator with infinite data to
    // offer looks empty. Qt's own audio output example overrides it for the
    // same reason. Found here by the probe printing zeroes, which is the sort
    // of thing a probe is for.
    qint64 bytesAvailable() const override {
        return kOffered + QIODevice::bytesAvailable();
    }

private:
    bool silent_ = false;
    qint64 phase_ = 0;
    std::atomic<qint64> handed_{0};
};

// Reads a file through a plain QAudioDecoder and reports what happened,
// including anything Qt or the backend said while it was happening.
//
// Deliberately not audio_import::decode. That one is ours, and a reading taken
// through our code cannot answer a question about whose message this is -- the
// same reason dock_probe links Qt directly rather than animage_ui.
struct DecodeReading {
    bool ok = false;
    int rate = 0;
    int channels = 0;
    qint64 frames = 0;
    QStringList said;
    QString error;
};

QStringList* g_said = nullptr;
QtMessageHandler g_previous_handler = nullptr;

void captureSaid(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (g_said) g_said->append(msg);
    if (g_previous_handler) g_previous_handler(type, ctx, msg);
}

DecodeReading readOnce(const QString& path) {
    DecodeReading out;
    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));

    QEventLoop loop;
    QTimer silence;
    silence.setSingleShot(true);
    silence.setInterval(15000);
    QObject::connect(&silence, &QTimer::timeout, &loop, [&] {
        out.error = QStringLiteral("stopped responding");
        loop.quit();
    });
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        silence.start();
        const QAudioBuffer buffer = decoder.read();
        if (!buffer.isValid()) return;
        if (out.rate == 0) {
            out.rate = buffer.format().sampleRate();
            out.channels = buffer.format().channelCount();
        }
        out.frames += buffer.frameCount();
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, [&] { loop.quit(); });
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop,
                     [&](QAudioDecoder::Error) {
                         out.error = decoder.errorString();
                         loop.quit();
                     });

    g_said = &out.said;
    g_previous_handler = qInstallMessageHandler(captureSaid);
    silence.start();
    decoder.start();
    loop.exec();
    decoder.stop();
    qInstallMessageHandler(g_previous_handler);
    g_said = nullptr;

    out.ok = out.frames > 0;
    return out;
}

void reportDecode(const QString& path) {
    const QFileInfo info(path);
    std::printf("file     : %s\n", info.fileName().toUtf8().constData());
    std::printf("bytes    : %lld\n", (long long)info.size());

    for (int pass = 1; pass <= 2; ++pass) {
        const DecodeReading r = readOnce(path);
        std::printf("\n--- pass %d ---\n", pass);
        std::printf("  ok      : %s\n", r.ok ? "yes" : "no");
        std::printf("  format  : %d Hz, %d ch\n", r.rate, r.channels);
        std::printf("  frames  : %lld", (long long)r.frames);
        if (r.rate > 0)
            std::printf("  (%.3f seconds)", double(r.frames) / double(r.rate));
        std::printf("\n");
        const double megabytes =
            double(r.frames) * std::max(1, r.channels) * 4.0 / (1024.0 * 1024.0);
        std::printf("  decoded : %.2f MB of float, from %.2f MB of file\n", megabytes,
                    double(info.size()) / (1024.0 * 1024.0));
        if (!r.error.isEmpty())
            std::printf("  error   : %s\n", r.error.toUtf8().constData());
        if (r.said.isEmpty()) {
            std::printf("  said    : nothing\n");
        } else {
            for (const QString& one : r.said)
                std::printf("  said    : %s\n", one.toUtf8().constData());
        }
    }
    std::printf(
        "\nA message on BOTH passes belongs to the file and the codec. One on only the\n"
        "second belongs to whatever happened in between, which would be ours.\n");
}

double msOfBytes(qint64 bytes) {
    return 1000.0 * double(bytes) / double(kRate * kChannels * sizeof(float));
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    int seconds = 2;
    int device = -1;
    QString decode_path;
    bool silent = false;
    bool list = false;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--silent") silent = true;
        else if (args[i] == "--list") list = true;
        else if (args[i] == "--seconds" && i + 1 < args.size()) seconds = args[++i].toInt();
        else if (args[i] == "--device" && i + 1 < args.size()) device = args[++i].toInt();
        else if (args[i] == "--decode" && i + 1 < args.size()) decode_path = args[++i];
    }

    if (!decode_path.isEmpty()) {
        reportDecode(decode_path);
        return 0;
    }

    const QList<QAudioDevice> outs = QMediaDevices::audioOutputs();
    if (list) {
        const QAudioDevice def = QMediaDevices::defaultAudioOutput();
        for (int i = 0; i < outs.size(); ++i)
            std::printf("%2d %s %s\n", i, outs[i].id() == def.id() ? "*" : " ",
                        outs[i].description().toUtf8().constData());
        return 0;
    }

    const QAudioDevice out = (device >= 0 && device < outs.size())
                                 ? outs[device]
                                 : QMediaDevices::defaultAudioOutput();
    std::printf("device   : %s\n", out.description().toUtf8().constData());
    std::printf("null     : %s\n", out.isNull() ? "yes -- there is no audio output here" : "no");
    if (out.isNull()) {
        std::printf("\nNothing to measure. This is what a CI runner looks like.\n");
        return 1;
    }

    QAudioFormat fmt;
    fmt.setSampleRate(kRate);
    fmt.setChannelCount(kChannels);
    fmt.setSampleFormat(QAudioFormat::Float);
    if (!out.isFormatSupported(fmt)) {
        std::printf("\n48 kHz stereo float is not supported here; using the preferred format.\n");
        fmt = out.preferredFormat();
    }
    std::printf("format   : %d Hz, %d ch, sample format %d\n", fmt.sampleRate(),
                fmt.channelCount(), int(fmt.sampleFormat()));

    QAudioSink sink(out, fmt);
    Tone tone(silent);
    tone.open(QIODevice::ReadOnly);

    // **The clock is restarted once `start()` has returned**, and what opening
    // the device cost is reported on its own line rather than folded in.
    // Opening a stream is hundreds of milliseconds on some drivers, and
    // counting that as time the audio has been playing would put `wall` that
    // far ahead of everything else -- answering the question wrongly, in the
    // direction that looks reassuring.
    QElapsedTimer wall;
    wall.start();
    sink.start(&tone);
    const qint64 open_ms = wall.elapsed();
    wall.restart();
    std::printf("opening  : %lld ms\n", (long long)open_ms);
    std::printf("buffer   : %lld bytes = %.1f ms\n", (long long)sink.bufferSize(),
                msOfBytes(sink.bufferSize()));
    std::printf("\n  wall      handed    processed     ahead      handed-processed\n");
    std::printf("  (ms)      (ms)      (ms)          (ms)        (ms)\n");

    // Two statistics, and both are minima for reasons worth stating because
    // neither is obvious and an average would be wrong in both.
    //
    // `handed - processed` says **which number is being counted**. A minimum,
    // because the gap sawtooths -- it dips every time the sink refills -- and a
    // `processedUSecs` counting what it was handed would sit at the bottom of
    // that dip permanently rather than passing through it.
    //
    // `processed - wall` says **whether it runs at real time**, read as the
    // first half against the second. A minimum again, and this one is about the
    // meter rather than the thing measured: `w` and `p` are two reads that are
    // not one, so the thread being preempted between them lands entirely in
    // `p - w` and can only ever inflate it. On this desktop three readings in
    // eight hundred come out most of a frame high that way. The noise has a
    // sign, so the minimum is the reading with the least of it in -- where an
    // average, or first-against-last, is at the mercy of where the blips fell.
    double ahead_lo = 1e9, ahead_hi = -1e9;
    double gap_lo = 1e9, gap_hi = -1e9;
    double early_lo = 1e9, late_lo = 1e9;
    const double halfway = seconds * 500.0;
    QTimer sampler;
    sampler.setInterval(10);
    QObject::connect(&sampler, &QTimer::timeout, [&] {
        const double w = double(wall.elapsed());
        const double h = msOfBytes(tone.handed());
        const double p = double(sink.processedUSecs()) / 1000.0;
        ahead_lo = std::min(ahead_lo, p - w);
        ahead_hi = std::max(ahead_hi, p - w);
        (w < halfway ? early_lo : late_lo) = std::min(w < halfway ? early_lo : late_lo, p - w);
        gap_lo = std::min(gap_lo, h - p);
        gap_hi = std::max(gap_hi, h - p);
        std::printf("  %-9.0f %-9.1f %-13.1f %+-11.1f %+.1f\n", w, h, p, p - w, h - p);
        std::fflush(stdout);
        if (w >= seconds * 1000) QCoreApplication::quit();
    });
    sampler.start();

    const int rc = app.exec();
    sink.stop();

    // The verdict, and the two numbers it is read off. Printed rather than
    // decided, because what the note wants recorded is the measurement -- the
    // one-line consequence is written down where the arithmetic is.
    const double buffer_ms = msOfBytes(sink.bufferSize());
    const double drift = late_lo - early_lo;
    std::printf("\nprocessed - wall  : %+.1f first half, %+.1f second half"
                "  (drift %+.1f, jitter %.1f)\n",
                early_lo, late_lo, drift, ahead_hi - ahead_lo);
    std::printf("handed - processed: %+.1f to %+.1f ms  (buffer %.1f)\n", gap_lo, gap_hi,
                buffer_ms);
    std::printf("one frame at 24 fps:  41.7 ms\n\n");

    if (gap_lo < buffer_ms / 4.0)
        std::printf("handed - processed goes to nearly nothing, so processedUSecs counts audio\n"
                    "HANDED TO the device. playedMs() must subtract what is still queued.\n");
    else
        std::printf("handed - processed never falls below most of a buffer, so processedUSecs\n"
                    "counts audio PLAYED OUT of the device. playedMs() uses it as it comes.\n");

    // Said separately because it is a different fact and a reader wants both:
    // whether the number runs at real time at all. **Drift is the halves
    // against each other and not the spread**, and that is not a softer test --
    // see where the two minima are taken. A rate difference moves the offset
    // between the halves; sampling noise widens the spread and leaves them
    // where they were.
    if (std::abs(drift) < 5.0)
        std::printf("\nIts offset from the wall clock ends where it started, so it advances at\n"
                    "real time rather than in buffer-sized jumps. The offset itself is where\n"
                    "the timer was started and not an error in the number.\n");
    else
        std::printf("\nIts offset from the wall clock moved %+.1f ms over the run, so the two\n"
                    "are running at different rates. Read the table before trusting the line\n"
                    "above.\n", drift);
    return rc;
}
