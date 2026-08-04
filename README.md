# Animage

A 2D animation program for hand-drawn work, built around two bets:

**Layers belong to the track, timing belongs to the image.** A layer is added
to every image at once, and an image that is held for three frames holds *all* of
its layers for three frames. This is deliberately unlike TVPaint and Toon Boom,
where each layer carries its own exposure — desynchronised exposures are a
classic source of production bugs. If two things need independent timing, they
belong in separate tracks.

A **track** is one stack of layers with its own time. The **timeline** is the
scene's shared time axis and the panel that shows it: a scene has several tracks
and one timeline. (Only one track exists so far — see
[docs/handover.md](docs/handover.md).)

**A colour layer stores scribbles, not pixels.** The CTG layer is an
implementation of LazyBrush (Sýkora et al., Eurographics 2009): you scrawl a
rough mark inside a region and a max-flow/min-cut finds the best possible
boundary, tolerating gaps in the line art. Because only the scribbles are
stored, the fill regenerates whenever the drawing or the scribble changes.

Status: **prototype**. You can draw, animate and colour; you cannot save. M0
through M4 of [the plan](docs/fr/plan-de-prototype.md) exist, M5 does not.

If you are picking this up, read [docs/handover.md](docs/handover.md) first: it
records what was built, where it deliberately departs from the plan, and the
mistakes that cost the most time.

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
| `1` / `0` | actual size / fit the canvas |
| `Shift+0` | fit the drawing, including whatever ran off the edge |
| `Ctrl+Z`, `Ctrl+Shift+Z` | undo, redo |
| `Alt`+right-drag | brush size |
| `Alt`+click | pick the colour under the pointer (taken where you let go) |
| Hold `Z` and drag | scrubby zoom |

The eyedropper is `Alt`+click on the drawing rather than the colour dialog's
"pick screen colour", which cannot work with a stylus: Qt routes pen input as a
tablet event and discards the mouse messages Windows promotes from it, so the
dialog never hears the click. Sampling the document is better regardless — it
reads the colour that was stored rather than what the monitor was showing after
sRGB encoding, the zoom filter and the onion skin.

**The canvas.** The outlined rectangle is what will be exported; everything
outside it is veiled. You can draw out there and nothing is clipped — roughs run
off the edge, and the tile model has no edges at all — but what is outside the
canvas is not in the picture, so a colour fill stops at the frame line. Set the
size under Edit > Scene settings, as an aspect ratio and a resolution or as a
width and a height in pixels; each is kept true to the other.

In the timeline: drag the ruler to scrub, drag the right edge of a card to
change how long the drawing is held, and drag the body of a numbered card to
reorder it. Held frames carry no number and cannot be picked up -- they are the
same drawing still showing, not a thing of their own, and they travel with it.

**Colour layers.** "Add colour layer" makes a layer that holds scribbles rather
than colour, at the bottom of the pile — it is cut against the line art and
belongs under it. There is no scribble tool: scrawl roughly inside a region with the
ordinary brush and the whole region takes that colour, gaps in the line art
included. One scribble fills one shape; what is outside it stays uncoloured until
you scribble there too. What is stored is the scrawl, not the fill, so moving a
scribble recolours the region and redrawing the line art re-cuts it.

Holes in the line are bridged up to a stated width — 32 image pixels by default,
`CtgSettings::gap_tolerance_pixels` — and past that the fill escapes, which is
the honest failure rather than a wrong answer: the line has stopped being a
boundary. That width is the price the cut pays to run along the edge of the
picture, so it means the same thing whatever the drawing is solved at.

Every ordinary layer becomes a barrier for it automatically. Cutting against a
rough as well as a clean closes gaps that leak from either alone. The **Marks**
column beside a colour layer shows the scribbles instead of the fill.

The layer panel on the right adds, removes, reorders, hides and fades layers.
Layers belong to the track rather than to the image, which is the point of
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
