# Animage

A 2D animation program for hand-drawn work, built around two bets:

**Layers belong to the timeline, timing belongs to the image.** A layer is added
to every image at once, and an image that is held for three frames holds *all* of
its layers for three frames. This is deliberately unlike TVPaint and Toon Boom,
where each layer carries its own exposure — desynchronised exposures are a
classic source of production bugs. If two things need independent timing, they
belong in separate timelines.

**A colour layer stores scribbles, not pixels.** The CTG layer is an
implementation of LazyBrush (Sýkora et al., Eurographics 2009): you scrawl a
rough mark inside a region and a max-flow/min-cut finds the best possible
boundary, tolerating gaps in the line art. Because only the scribbles are
stored, the fill regenerates whenever the drawing or the scribble changes.

Status: **prototype**. Nothing here is usable for real work yet. See
[the milestone plan](docs/fr/plan-de-prototype.md).

## Design documents

The original design documents are in French and are authoritative — the code
follows them, not the other way round. They live in [docs/fr](docs/fr/).
Everything else in the repository is in English.

## Building

Requires a C++20 compiler, CMake 3.21+, and (for the application, not the core
library) Qt 6.5+.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The core library `animage_core` has no Qt dependency and no external
dependencies at all. If Qt 6 is not found, the GUI targets are skipped and the
core library and its tests still build.

### On Windows with MSYS2

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-boost
```

Then run the CMake commands above from a UCRT64 shell.

## Layout

| Path | Contents |
|---|---|
| `src/core/` | Data model, tiles, brush, compositor, undo, colour. Pure C++20, no Qt. |
| `src/app/animage/` | The application. |
| `src/app/latency/` | M0: the pen latency harness. |
| `tests/` | Unit tests for the core. |
| `docs/fr/` | Original design documents. |

## Running it

Double-click `run-animage.bat`, or:

```bash
cmake --build build --target animage && ./build/src/app/animage
```

Currently at M2: one image of one timeline, drawn on with a pressure brush.
There is no timeline UI and nothing can be saved yet.

| | |
|---|---|
| Draw | pen, or left mouse |
| `B` / `E` | brush / eraser (turning the stylus over also erases) |
| `[` / `]` | smaller / larger |
| Wheel | zoom about the pointer |
| Space-drag, middle-drag | pan |
| `1` / `0` | actual size / fit the drawing |
| `Ctrl+Z`, `Ctrl+Shift+Z` | undo, redo |

The layer panel on the right adds, removes, reorders, hides and fades layers.
Layers belong to the timeline rather than to the image, which is the point of
the whole model — with only one image visible that is not yet observable, and
it becomes so at M3.

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
