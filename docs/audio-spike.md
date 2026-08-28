# The audio spike

[importing.md](importing.md) puts one piece of work before any audio code is
written, and calls it the highest-risk item in the note:

> So the deployment spike comes before any audio code is written. A
> hello-world that opens a sink, built and packaged through all three tools on
> all three platforms. If `windeployqt` does not bundle the FFmpeg plugin
> correctly, that is a fact worth having on day one and a disaster to discover
> after the audio layer exists.

**This is what it found.** It is a record of measurements and not a plan: read
[importing.md](importing.md) for what audio is *for* and what shape it will
take, and this for what taking Qt Multimedia actually costs, because two of the
things that note is weighing turn out to be different from what it assumed.

Nothing here is built. `src/app/animage/audio_check.*` and the two lines it adds
to `main.cpp` are the spike itself and **come out again** — see [what comes out
again](#what-comes-out-again). `tests/audio_probe.cpp` stays, as `dock_probe`
stays: it is an instrument, and the next machine that disagrees with these
numbers is the reason to keep it.

| | |
|---|---|
| [The short version](#the-short-version) | three questions, three answers |
| [What `processedUSecs` counts](#what-processedusecs-counts) | played out, and the test that says otherwise |
| [**What the backend is for**](#what-the-backend-is-for-which-is-less-than-the-bill) | scrubbing needs none of it |
| [What the three packagers did](#what-the-three-packagers-did) | all three, unprompted |
| [What it costs](#what-it-costs) | bigger yes, slower no, harder no |
| [The licence, which got cheaper](#the-licence-which-got-cheaper) | Qt's FFmpeg is LGPL, not GPL |
| [**The machine that will want a calibration**](#the-machine-that-will-want-a-calibration) | it is Bluetooth, and it is not hypothetical |
| [What is still not known](#what-is-still-not-known) | including the one that could change what gets built |
| [What comes out again](#what-comes-out-again) | |
| [Found while looking for something else](#found-while-looking-for-something-else) | three of them, one a real defect |

## The short version

| the question | the answer |
|---|---|
| Do the three deployment tools bundle a Qt Multimedia backend? | **All three do, with no help.** `windeployqt`, `macdeployqt` and `linuxdeploy-plugin-qt` each found the plugin and its FFmpeg libraries from the import table alone. |
| Does a *downloaded* package find that backend on a machine that never had Qt? | **Yes**, tested the only way that counts — the Windows zip off the CI artifact, unpacked on a machine with no MSVC Qt, run with `PATH` stripped to `C:\Windows\system32;C:\Windows`. |
| Does `QAudioSink::processedUSecs()` count audio handed to the device, or played out of it? | **Played out.** `playedMs()` is that number as it comes, with nothing subtracted. |

And one nobody asked, which is the most useful thing here: **scrub audio needs
none of the FFmpeg payload at all.**

## What `processedUSecs` counts

The first of the two measurements [importing.md](importing.md) ends on, and the
whole playback clock stands on it. Getting it wrong does not fail — it *leans*,
and the picture sits a fixed fraction of a frame away from the sound on every
frame, invisibly.

`tests/audio_probe.cpp` opens a sink at 48 kHz stereo float, feeds it a
generated tone from a `QIODevice` it can meter, and prints three clocks beside
each other every 10 ms. Eight seconds on the machine it was run on:

```
opening  : 335 ms
buffer   : 96000 bytes = 250.0 ms

processed - wall  : +37.6 first half, +37.6 second half  (drift +0.0, jitter 10.0)
handed - processed: +250.0 to +292.0 ms  (buffer 250.0)
```

`handed - processed` never falls below most of a buffer, and `processed - wall`
ends where it started. A number counting what it had been *handed* would sit at
the bottom of that gap permanently and move in buffer-sized jumps; this one
tracks real time. So: **played out**, and `playedMs()` needs no
buffer-in-flight subtraction.

### The note's own test names the wrong meter

Worth writing down because it is the sort of thing that gets a measurement
skipped rather than corrected. [importing.md](importing.md) says:

> *Test:* play a file and watch whether the number ever reports more audio than
> there has been time to play.

Right idea, wrong meter — **there is no instant to measure real time from.** The
stream starts somewhere *inside* `QAudioSink::start()`, which takes 335 ms here,
so `processedUSecs` sits a constant few tens of milliseconds ahead of any timer
started around that call. Read literally, that constant says "handed over" about
a device that plainly is not, and that is exactly how the first run of the probe
reported it.

`handed - processed` has no start instant in it and both its terms are counted
from the same one. That is the column to decide on.

### A second device says the same thing, and settles what the offset is

Run again on a Bluetooth speaker rather than the monitor's HDMI audio:

```
opening  : 555 ms
processed - wall  : +24.8 first half, +25.8 second half  (drift +1.0, jitter 13.0)
handed - processed: +250.0 to +292.0 ms  (buffer 250.0)
```

Same verdict, and the useful part is what *changed*: the constant offset went
from +37.6 to +24.8 while everything else stayed. **That is the offset being an
artefact of where the timer was started and not a property of the number** — the
claim made above from one device, now with a second one behind it. `handed -
processed` is identical to the tenth of a millisecond, because it is a fact
about the buffer and not about the device.

Sound was confirmed audible on this one by ear, which is the one thing no check
here can do.

### And a pull-mode `QIODevice` must override `bytesAvailable`

Without it the sink pulls **nothing**, for ever, and raises nothing at all:
`QIODevice::read` consults `bytesAvailable()` before it will call `readData`,
and the base class answers 0 for a sequential device — so a generator with
infinite data to offer looks empty. The first run of the probe printed a table
of zeroes with no error anywhere. Qt's own audio output example overrides it for
the same reason.

## What the backend is for, which is less than the bill

**The single most useful thing the spike found, and it was not what it was
looking for.** Delete `plugins/multimedia` outright and Qt says, in as many
words:

> No QtMultimedia backends found. Only QMediaDevices, QAudioDevice,
> QSoundEffect, QAudioSink, and QAudioSource are available.

That list is the whole of what scrub audio needs. `audio_probe` was run against
exactly that — a plugin tree with the backend removed — and opened the device,
played, and reported the same numbers to the tenth of a millisecond. The raw
audio path is native inside `Qt6Multimedia` itself (WASAPI, CoreAudio,
ALSA/PulseAudio); the backend plugin is not in it.

So the FFmpeg payload — which is where every megabyte and all three packaging
tools' difficulty live — buys exactly two things:

| needs the backend | does not |
|---|---|
| `QAudioDecoder`: mp3, m4a, opus, anything compressed | `QAudioSink`, and so the whole of scrubbing |
| `QMediaPlayer`, and so all of video import | `QMediaDevices`, and so device enumeration |

**[importing.md](importing.md) does not draw this seam**, and it changes what
its cost argument is weighing. That note takes Qt Multimedia on the strength of
video and treats the audio module as arriving free alongside it. The truth is
the other way round for the half being built first: [scrubbing comes
first](importing.md#scrubbing-comes-first) — *"the higher-value half of audio
import for this program's stated purpose"* — and scrubbing is available for one
1.2 MB library. Every megabyte after that is bought by a director's `.m4a` and
by video.

This is not an argument for shipping without the backend. `QAudioDecoder`
closing the codec gap is named in that note as one of the three things Qt
Multimedia buys, and *"the 'explain and ask for a WAV' conversation does not
have to happen"* is worth 20 MB. It is an argument for knowing which line item
is which, because if the backend ever turns out to be a problem on some
platform, **the feature that matters degrades to WAV rather than disappearing.**

## What the three packagers did

CI, on the branch, dispatched with no pull request and nothing merged. Each
packaging step is followed by a step that says what it bundled, what it weighed,
and what the packaged binary reports when it runs — the third being the one that
cannot be answered by looking, since a plugin in the right folder and a plugin
that loads are different facts.

| | bundled | backend loaded |
|---|---|---|
| **Linux**, `linuxdeploy-plugin-qt` | `libQt6Multimedia.so.6`, five FFmpeg `.so`s, `plugins/multimedia/libffmpegmediaplugin.so` | yes |
| **macOS**, `macdeployqt` | `QtMultimedia.framework`, `libavcodec/avformat/avutil`, `PlugIns/multimedia/libffmpegmediaplugin.dylib` | yes |
| **Windows**, `windeployqt` | `Qt6Multimedia.dll`, five FFmpeg DLLs, `multimedia/ffmpegmediaplugin.dll` | yes |

**None of them needed telling.** They read the import table of `animage`, found
Qt Multimedia in it, and pulled the backend and its libraries in behind it. The
disaster this spike existed to catch early does not happen.

### Which is why the spike had to be in the application

The three tools are run over `animage` and nothing else, and they work by
reading what a binary imports. **A probe under `tests/` that links Qt Multimedia
beside the application teaches them nothing** — with nothing in `animage`
importing the module, every one of them correctly bundles nothing, and the run
comes back green having asked no question at all. That is why
`audio_check.cpp` is in `src/app` rather than beside `audio_probe`, and it is
the whole reason the spike has two halves instead of one.

### And the download is the reading that counts

CI runs `--audio-check` on the binary it just built, on the machine that just
installed Qt. Useful, and not the same reading: [the same source, two different
pictures](handover.md#the-same-source-two-different-pictures) is already written
down here about exactly this. So the Windows zip was fetched from the artifact,
unpacked on a machine that has never had MSVC Qt on it, and run with the `PATH`
stripped back to `C:\Windows\system32;C:\Windows`:

```
audio: built against Qt Multimedia
  Qt 6.10.3, plugins at .../unpacked/Animage/plugins
  outputs: 1
    DELL S2721HS (Intel(R) Display Audio)
  qt said: Using Qt multimedia with FFmpeg version 7.1.3 LGPL version 2.1 or later
  VERDICT: a backend loaded and found outputs. Audio works here.
```

Windows is the platform where this could be tested most honestly, because it is
the one where a package built by CI could be run on a machine that shares
nothing with the runner. **Linux and macOS have not had this test**, only the
runner's own — see [what is still not
known](#what-is-still-not-known).

## What it costs

### Bigger: yes, and this is the whole of the bill

Artifact sizes, spike branch against `main` at `d09c2658`, measured the same way
on both:

| | before | after | |
|---|---|---|---|
| Linux AppImage | 36.8 MB | **55.7 MB** | +18.9 |
| macOS zip | 20.3 MB | **39.9 MB** | +19.6 |
| Windows zip | 13.5 MB | **22.6 MB** | +9.1 |

Windows is cheaper because a zip of DLLs compresses where an AppImage and a
`.app` are already-compressed containers, not because it ships less: the Windows
stage is 51.9 MB unpacked, of which the multimedia part is 19.7 MB. **The real
number is about 20 MB on every platform**, and `avcodec` is two thirds of it.

### Slower: no, and measured rather than assumed

`Qt6Multimedia.dll` imports no FFmpeg library at all — the backend is a plugin,
and Qt loads it lazily on first use of the media stack. So the 20 MB is paged in
when something asks for audio and not before.

Startup was timed through the full window build, five runs each, on the same
machine:

```
without Multimedia   29  30  29  30 ms
with Multimedia      29  29  29  29 ms
```

Nothing. And nothing in the [playback clock](importing.md#the-playback-clock) or
the paint path changes: the design keeps the decoder off both, and this spike
gives no reason to revisit that.

### Harder to ship: no

This was the expensive outcome the spike was insurance against, and it did not
arrive. No deployment tool needed a flag, an environment variable, or a manual
copy. No CI step needed anything beyond `modules: qtmultimedia` on the Qt
install.

**Two places, not four.** [importing.md](importing.md) says *"`modules:
qtmultimedia` on **four** Qt install steps"*; the file has **two**
`install-qt-action` blocks, one of which the build matrix runs three times. The
Windows core-only sanitizer installs no Qt deliberately — `find_package(Qt6)`
failing is what keeps that job small — and must not start.

**And the trap the note warns about is real and was avoided by construction.**
`Multimedia` is asked for as its own package in `src/app/CMakeLists.txt` and
never in the root `find_package`. Checked rather than asserted: a build
configured with `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6Multimedia=ON` still builds
`animage`, still configures `tests/`, and answers *"not built"* to
`--audio-check`. See [what asking for a private Qt component at the top level
switches off](handover.md#what-asking-for-a-private-qt-component-at-the-top-level-switches-off),
which is this mistake already paid for once.

## The licence, which got cheaper

[importing.md](importing.md) treats shipping an H.264 encoder as a decision a
GPL project should settle deliberately, and it is right to. But the position is
lighter than that section assumes, and the difference is *whose* FFmpeg.

**Qt's official binaries ship an LGPL 2.1 FFmpeg.** All three CI packages report
it themselves: *"Using Qt multimedia with FFmpeg version 7.1 LGPL version 2.1 or
later"*. That is a build configured without the GPL-only encoders — no x264, no
x265.

**MSYS2's is a GPL 3 build**, and a local Windows build says so: *"FFmpeg
version 8.1.2 GPL version 3 or later"*. Compatible with this project either way,
since Animage is GPL-3.0-or-later — but it is a different set of third-party
licence texts, and a contributor's local build and the shipped one are not the
same artifact.

Neither of those is a decision this note takes. What it records is that **the
encoder question does not arise from importing at all** — it arrives with
[video export](importing.md#video-export-and-what-qt-gives-free), which is last
and is where it should be argued.

## The machine that will want a calibration

[importing.md](importing.md) defers a manual sync offset in milliseconds, on the
user's call, and is explicit about what deferring costs:

> What deferring does cost: if `playedMs()` turns out to over- or under-report
> on some driver, there is nothing the user can do about it but say so.

**That machine has a name, and it is any Bluetooth output.** A2DP puts an encode,
a radio hop and a decode between the operating system and the speaker, and it is
ordinarily 100–200 ms — **two and a half to five frames at 24 fps.** That is not
the sub-frame bias [the playback clock](importing.md#the-playback-clock) is
written to remove; it is a desync somebody would see.

**This is reasoning and not a measurement, and the difference matters here.**
What was measured is that `processedUSecs` tracks real time on a Bluetooth
device exactly as it does on HDMI — same rate, same gap, a different constant.
What is *inferred* is that the number cannot include the transport delay,
because what reports it is the operating system's mixer position and the radio
hop is downstream of that. Very likely, and not established: the offset a timer
sees is contaminated by where the timer started, which is precisely the trap
this document already had to correct once.

**What would settle it** is a comparison rather than an absolute: play the same
file through HDMI and through Bluetooth, and ask whether `playedMs()` differs by
the transport delay or by nothing. If by nothing, the delay is invisible to the
number and a calibration is the only way to null it.

Nothing to do now — the deferral is the right call and is cheap to reverse,
being a preference rather than a field in `scene.json`. What this adds is that
the case is ordinary rather than hypothetical, so **the first person to report
that the sound is late is likely to be on Bluetooth**, and that is worth asking
before looking anywhere else.

## What is still not known

Three things, and the first is the only one that could change what gets built.

**1. Does `QMediaPlayer` at 1× extract every frame?** Untouched. It is the
second of [importing.md](importing.md)'s two measurements and it belongs to
video, which is not what is being built next. It stays open, and it stays the
one question here whose answer could make video expensive.

**2. Does a downloaded Linux or macOS build find its backend?** Only the
runner's own copy has been asked, and the runner has Qt installed. Windows had
the honest test because a Windows machine was available to run the download on.
Neither of the others is expected to fail — both bundled the plugin and both
loaded it — but "expected" is what [the same source, two different
pictures](handover.md#the-same-source-two-different-pictures) exists to warn
about, and the test costs one download on a machine of each kind.

**3. Whether sound is audible from a *downloaded* build.** A local build has
been heard — `run-audio-probe.bat`, on two devices, by ear. The packaged
binaries prove the backend loads and the device is found, but `--audio-check`
enumerates and does not play, so nobody has heard a downloaded build make a
noise. It wants one person, once, per platform, and the probe is not in the
package to do it with — which is a thing to fix on the day the audio layer gives
the application something audible of its own.

## What comes out again

`audio_check.h`, `audio_check.cpp`, their line in `src/app/CMakeLists.txt`'s
source list, and the `--audio-check` branch in `main.cpp`. The header says so at
the top so that nobody inherits it as a feature.

What replaces them is the `AudioDevice` seam [importing.md](importing.md) asks
for — open at rate R, receive a callback asking for N frames, report frames
consumed, stop — which is a different shape and a different purpose: keeping
Qt's types out of `MainWindow` and `TimelineWidget`, rather than reporting on
them.

**What stays** is `tests/audio_probe.cpp`, the `find_package(Qt6Multimedia)`
block, and `modules: qtmultimedia` in CI. The probe stays for `dock_probe`'s
reason: it is the instrument, and the first machine whose driver disagrees with
the numbers above is exactly when somebody will want to re-run it rather than
rebuild it. It is built and **never run by `ctest`** — the runners have no audio
device, so opening an output there fails or hangs, which is the same fact that
makes the sync arithmetic take a sample count as an argument.

## Found while looking for something else

**A real defect, and the branch's own.** The first CI run this branch had ever
had failed on macOS, at `sequence_import_dialog.cpp:159`: a lambda capturing a
`this` it never uses, which Apple Clang makes fatal under `-Werror` through
`-Wunused-lambda-capture` while GCC and MSVC say nothing. **The sequence import
dialog had never compiled on macOS.** Nothing to do with audio; found because
the spike was the first thing to put the branch through CI, and fixed in
`c96ed10`.

**A second dispatch cancels the first.** The workflow's `concurrency` group is
`ci-${{ github.ref }}` with `cancel-in-progress: true`, so dispatching a second
run on the same branch kills the one already going — including its packaging
steps, which is where the answers were. Obvious afterwards. Wait for a run, or
expect to lose it.

**MSYS2's `windeployqt` does not produce a runnable folder, and never did.** A
staged local build fails to start with `STATUS_DLL_NOT_FOUND` with or without
Multimedia: `libstdc++-6`, `libgcc_s_seh-1`, `zlib1`, ICU, HarfBuzz and a dozen
others are simply not copied, because `windeployqt` knows Qt's dependencies and
not MinGW's. Multimedia makes it worse in kind — MSYS2's FFmpeg is a
kitchen-sink distro build that pulls x264, x265, aom, dav1d, libjxl, librsvg,
GnuTLS and about forty more — but the folder did not work beforehand either.
**This is not a shipping path and nothing in the repository uses it**; CI builds
Windows with MSVC and Qt's own binaries. Recorded so that nobody measures the
cost of audio with this and gets a number that means nothing.
