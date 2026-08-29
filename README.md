# Animage

[![CI](https://github.com/S-poony/Animage/actions/workflows/ci.yml/badge.svg)](https://github.com/S-poony/Animage/actions/workflows/ci.yml)

A 2D animation program for hand-drawn work, built around two bets:

**Layers belong to the track, timing belongs to the image.** A layer is added
to every image at once, and an image that is held for three frames holds *all* of
its layers for three frames. This is deliberately unlike TVPaint and Toon Boom,
where each layer carries its own exposure — desynchronised exposures are a
classic source of production bugs. If two things need independent timing, they
belong in separate tracks.

A **track** is one stack of layers with its own time. The **timeline** is the
scene's shared time axis and the panel that shows it: a scene has several tracks
and one timeline. The timeline shows one row per track under a single playhead,
tracks stack as flat groups with track 1 on top, and the canvas shows all of
them while you draw on one. Tracks need not be the same length.

**A colour layer stores scribbles, not pixels.** The CTG layer is an
implementation of LazyBrush (Sýkora et al., Eurographics 2009): you scrawl a
rough mark inside a region and a max-flow/min-cut finds the best possible
boundary, tolerating gaps in the line art. Because only the scribbles are
stored, the fill regenerates whenever the drawing or the scribble changes.

Status: **prototype**. You can draw, animate, colour, save and export. M0 through
M5 of [the plan](docs/fr/plan-de-prototype.md) exist; export writes 16-bit PNG
or lossless half-float EXR, with TIFF the named next format for the pipelines
that ask for it. Colour carries from drawing to drawing and moves with the
drawing when it does — see [the manual](docs/manual.md).

A project is a folder — `scene.json` beside one file per cel — under File ▸
Open, Save and Save As. Cel pixels are stored losslessly, bit for bit as the
half-floats they are in memory, because a 16-bit PNG cannot hold one without
throwing pixels away. See [docs/handover.md](docs/handover.md).

If you are picking this up, read [docs/handover.md](docs/handover.md) first: it
records what was built, where it deliberately departs from the plan, and the
mistakes that cost the most time.
[docs/scribbles-through-time.md](docs/scribbles-through-time.md) designs the two
halves of colouring across time — carrying scribbles from drawing to drawing and
moving them with the animation. Both are built, the second as far as one
translation per *region*, with the as-rigid-as-possible registration past it
built, measured and switched off. Where building either half
contradicted the design, the note keeps the original text and marks the
correction underneath, which is the interesting part to read.

## Design documents

The original design documents are in French and are authoritative — the code
follows them, not the other way round. They live in [docs/fr](docs/fr/).
Everything else in the repository is in English.

If you are wondering why this repository has its own image format — a reasonable
first question, and usually a sign of something gone wrong —
[docs/why-our-own-formats.md](docs/why-our-own-formats.md) answers it, with the
measurements, the alternatives that were rejected and why, and what would change
the decision. It also records the hand-written JSON reader that used to sit
beside it, why the argument for that one looked identical and was not, and why
removing it was right.

Everything under [docs/](docs/), and whether it describes something that exists:

| | |
|---|---|
| [manual.md](docs/manual.md) | what every tool, key and panel does — the reference for using the program. **Built** |
| [handover.md](docs/handover.md) | how the program fits together, what was built and what it cost, and the traps. Start at its own table of contents. **Built** |
| [why-our-own-formats.md](docs/why-our-own-formats.md) | why the project file and the cel format are its own, measured against the alternatives. **Built** |
| [scribbles-through-time.md](docs/scribbles-through-time.md) | a scribble staying from one drawing to the next, and moving to follow the animation. **Built** to one translation per region; the lattice past it is built and off by default, close enough to be a choice rather than a gap, and the rung past that is research |
| [lasso-and-transform.md](docs/lasso-and-transform.md) | selecting part of a drawing, moving, rotating and scaling it, and the clipboard. **Built** |
| [m0-latency.md](docs/m0-latency.md) | the pen-to-pixel latency gate, and how it is measured. **Built** |
| [importing.md](docs/importing.md) | bringing in pictures, image sequences, video and audio. Its two open decisions are settled — Qt Multimedia, and a reference layer with no cels. **Built** except for video files import and export |
| [audio-spike.md](docs/audio-spike.md) | what taking Qt Multimedia costs, measured before any audio code: what the three packaging tools bundled, what it weighs, and the one number the playback clock stands on. **A record of measurements**, not a plan |
| [playback-resolution.md](docs/playback-resolution.md) | giving up resolution to hold the frame rate, and when. **Not built** |

As the paragraph above says of one of them: a design note written before the
thing was built is kept afterwards, with the original text left alone and the
correction marked underneath. So a document that opens "nothing here is built"
may well be describing something that now is, and the corrections are the
interesting part. The column above says which is which, so that finding out does
not mean reading the whole note first.

## Download

Every push to `main` is built and tested on Linux, Windows and macOS, and the
binaries are published to the
[**latest build**](https://github.com/S-poony/Animage/releases/tag/latest).
Nothing needs to be installed alongside them — Qt travels with the download.

| | |
|---|---|
| Windows | `Animage-windows-x64.zip` — unzip and run `animage.exe`. |
| macOS | `Animage-macos-universal.zip` — unzip, then **right-click ▸ Open** the first time. Needs macOS 13.3 or newer. |
| Linux | `Animage-linux-x86_64.AppImage` — `chmod +x` it and run it. |

These are prototype builds from the tip of `main`, not stable releases, and they
are unsigned. macOS will refuse a double-click and offer no way past it;
right-click ▸ Open gives you the button that says yes. Windows may show a
SmartScreen warning, behind *More info ▸ Run anyway*. Signing costs money per
year from both Apple and a Windows certificate authority, and is worth paying
when there is something worth shipping.

What every tool, key and panel does — the straight line, the lasso, transform,
tracks, colour layers, export — is in **[the manual](docs/manual.md)**.

## Building

Requires a C++20 compiler, CMake 3.21+, and (for the application, not the core
library) Qt 6.5+. Nothing else: the EXR writer's compressor is vendored rather
than linked, so there is no zlib to install on any platform.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The build denies compiler warnings and asks for ASan and UBSan by default, so a
warning or a memory error fails the build or the tests rather than being noticed
later. `-DANIMAGE_WERROR=OFF` and `-DANIMAGE_SANITIZE=OFF` turn either off (the
packaged CI builds use the latter, so released binaries do not carry a sanitizer
runtime).

The sanitizers are a request and not a guarantee: a toolchain that ships neither
runtime — MSYS2's UCRT64 GCC is one, so this is the normal case on Windows — is
warned about once at configure time and then builds without them. There are three
outcomes, so it takes two variables in `build/CMakeCache.txt` to tell them apart:
`ANIMAGE_ASAN_UBSAN_OK` set means both, empty with `ANIMAGE_UBSAN_OK` set means
UBSan alone, and both empty means neither. (Under MSVC the ASan probe is
`ANIMAGE_ASAN_OK`.) Reading only the first cannot distinguish "UBSan only" from
"no sanitizer at all", which is the distinction worth having.

On Windows the core library can still be sanitized, because it needs no Qt and
MSVC has AddressSanitizer. From a Developer Command Prompt:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

`-- Sanitizers: AddressSanitizer` and `-- Qt 6 not found: building the core
library and tests only` means it worked. The test binaries run only inside that
shell — MSVC links ASan dynamically, and a test exiting instantly with
`0xC0000135` is the missing runtime DLL rather than anything you wrote. CI runs
this on every push, alongside the Linux job that covers the whole program under
both sanitizers.

The core library `animage_core` has no Qt dependency and no external
dependencies at all. If Qt 6 is not found, the GUI targets are skipped and the
core library and its tests still build.

Beside the tests are the benchmarks and `shots`, which are built and never run
by `ctest`. `shots` drives the real window through a list of named situations
and writes a picture of each, because every interface bug this project has
recorded was caught by looking and none by a green build. It is meant to be
added to — see [docs/handover.md](docs/handover.md).

```bash
cmake --build build --target shots && ./build/tests/shots --list
```

`dock_probe` is beside it and answers a different question. It is a plain Qt
window with docks in it and **none of this program**, for deciding whether a
fault in a panel is ours or Qt's — which it settled for
[#54](https://github.com/S-poony/Animage/issues/54) in one run, after the same
question had been answered by reasoning twice and wrong both times.

```bash
cmake --build build --target dock_probe && ./build/tests/dock_probe
```

`window_probe` is the other half of that question: the same readings taken from
the real window, so that doing one drag in each says whether a dock fault is ours
or Qt's by subtraction.

```bash
cmake --build build --target window_probe && ./build/tests/window_probe
```

Both need a real hand — `setFloating` does not enter Qt's drag machinery, so a
dock fault cannot be reached from code. Double-click `run-dock-probes.bat` to
build both and open them one after the other, with the drags to try written on
the way in.

### On Windows with MSYS2

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base
```

Then run the CMake commands above from a UCRT64 shell.

## Layout

| Path | Contents |
|---|---|
| `src/core/` | Data model, tiles, brush, compositor, undo, colour. Pure C++20, no Qt. |
| `src/app/animage/` | The application. |
| `src/app/animage/project_io.*` | The one place a project folder meets the disk: scene.json and the cels, Qt's JSON and compression included. |
| `src/app/latency/` | M0: the pen latency harness. |
| `tests/` | Unit tests, for the core and for the application's save and load. Plus the benchmarks, `shots.cpp` — pictures of the interface, one per named situation — and the two dock probes — `dock_probe.cpp`, plain Qt with docks in it, beside `window_probe.cpp`, the real window, for telling our bugs from Qt's. |
| `third_party/` | tinyexr (BSD-3), a header included only by `exr_writer.cpp`, and miniz (MIT), its own translation unit in `animage_ui`. Both vendored. |
| `packaging/` | The desktop entry, and the icon. `animage.af` is the mark as drawn, `animage.svg` is exported from it by hand, and `make-icons.py` renders the PNG, `.ico` and `.icns` beside them. |
| `docs/fr/` | Original design documents. |

## Measuring pen latency

M0 is the gate before everything else, and it needs a human with a camera.
Build and run `animage_m0_latency`, then follow
[docs/m0-latency.md](docs/m0-latency.md). Pass is under 25 ms, pen tip to
photons — not the number the HUD shows.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).

Note for contributors: this choice makes distribution through the iOS App Store
impossible. The prototype targets desktop only, so the question is deferred
rather than answered — see
[docs/fr/tablettes-couleur-licence.md](docs/fr/tablettes-couleur-licence.md).
