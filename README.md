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
and one timeline. (Only one track exists so far — see
[docs/handover.md](docs/handover.md).)

**A colour layer stores scribbles, not pixels.** The CTG layer is an
implementation of LazyBrush (Sýkora et al., Eurographics 2009): you scrawl a
rough mark inside a region and a max-flow/min-cut finds the best possible
boundary, tolerating gaps in the line art. Because only the scribbles are
stored, the fill regenerates whenever the drawing or the scribble changes.

Status: **prototype**. You can draw, animate, colour, save and export. M0 through
M5 of [the plan](docs/fr/plan-de-prototype.md) exist; export writes 16-bit PNG
and not yet EXR. Colour carries from drawing to drawing and moves with the
drawing when it does — see below.

A project is a folder — `scene.json` beside one file per cel — under File ▸
Open, Save and Save As. Cel pixels are stored losslessly, bit for bit as the
half-floats they are in memory, because a 16-bit PNG cannot hold one without
throwing pixels away. See [docs/handover.md](docs/handover.md).

If you are picking this up, read [docs/handover.md](docs/handover.md) first: it
records what was built, where it deliberately departs from the plan, and the
mistakes that cost the most time.
[docs/scribbles-through-time.md](docs/scribbles-through-time.md) designs the two
halves of colouring across time — carrying scribbles from drawing to drawing and
moving them with the animation. Both are built as far as one translation per
drawing; the rungs past that are still research. Where building either half
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

## Building

Requires a C++20 compiler, CMake 3.21+, and (for the application, not the core
library) Qt 6.5+.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The build denies compiler warnings and runs the tests under ASan and UBSan by
default, so a warning or a memory error fails the build or the tests rather
than being noticed later. `-DANIMAGE_WERROR=OFF` and `-DANIMAGE_SANITIZE=OFF`
turn either off (the packaged CI builds use the latter, so released binaries
do not carry a sanitizer runtime).

The core library `animage_core` has no Qt dependency and no external
dependencies at all. If Qt 6 is not found, the GUI targets are skipped and the
core library and its tests still build.

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
| `src/app/animage/project_io.*` | The one place a project folder meets the disk: scene.json and the cels, Qt's JSON and zlib included. |
| `src/app/latency/` | M0: the pen latency harness. |
| `tests/` | Unit tests, for the core and for the application's save and load. |
| `docs/fr/` | Original design documents. |

## Running it

Double-click `run-animage.bat`, or:

```bash
cmake --build build --target animage && ./build/src/app/animage
```

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
| `Alt`+click | pick the colour under the pointer (follows the pointer, taken where you let go) |
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

Holes in the line are bridged however wide they are — the fill follows the ink
where there is ink and jumps where there is not, so a shape drawn with a fifth of
its outline missing still fills from one scribble. There is no gap setting to
tune. What limits it is the size of your scribble: giving a scribble up is what
the boundary is measured against, so **a bigger scribble bridges a bigger hole**.

The outside of a shape is left uncoloured. Scribbling a colour out there does not
fill the background — the edge of the picture is background and cannot be
overruled, so the mark keeps roughly its own pixels. Carry a scribble off the
edge of the picture and the region it is in fills to that edge.

**A scribble wins the pixels it covers**, whatever the solver decided. The
solver's job is the pixels you said nothing about, so a mark is a manual
touch-up for anything the fill got wrong — dab on the spot and it takes that
colour. The marks are invisible wherever the fill agreed with them, because a
scribble carries the colour of the label it produces, so what you see is the
disagreement and nothing else.

The **None** swatch beside the colour scribbles *no colour at all*: the region it
wins is left empty, and the spots it covers have their colour taken back off
them. It is offered on colour layers only, where a mark is a label rather than
paint.

**Colour carries from drawing to drawing.** A drawing with no marks of its own
shows the nearest coloured drawing's, so colouring the first drawing of a run
colours the run; scribbling on a drawing takes it over from there, and the
drawings after it follow that one instead. Clearing a drawing's marks puts it
back to carrying. Nothing is copied and nothing is stored — it is resolved as it
is read, so reordering and deleting drawings change what follows what for free.

**And the marks move with the drawing.** Where a mark is carried to a drawing it
was not made on, it is shifted by however far the line art moved between the
two, measured from the drawings themselves and stored nowhere. Left where it was
drawn a carried mark holds its region only while the drawing has moved less than
about half that region's width — which between two drawings is not much — and
past that the region takes the wrong colour or none. It is one shift for the
whole drawing, so a shot where two things move apart is a shot where it is right
about one of them; that is what the switch is for.

The **Colour layer** box in the layer panel is where this is set: which layers
the fill is cut against, whether it carries at all, whether it carries forwards,
backwards or to whichever coloured drawing is nearer, and whether carried marks
move with the drawing. Cutting against a rough as well as a clean closes gaps
that leak from either alone, which is why several sources is the default. The
**Marks** column shows the scribbles instead of the fill.

**The fill is worked out beside the interface, not inside it.** A max-flow over
a 1080p drawing takes about a second and a half, so it happens on another
thread: the status bar says "colouring..." while it does, the last answer stays
on screen until the new one lands, and nothing waits. A coarse answer arrives
about a tenth of a second after the pen lifts and a full-resolution one replaces
it, so a stroke costs the coarse one and pausing is what buys the rest.

**The timeline says where the colour came from.** A blue bar under a drawing's
number means its colour was carried there rather than drawn there, and an arrow
before the colour layer's name says the same about the drawing you are standing
on, with the drawing it came from in the tooltip. Both are a walk over the
drawings and cost nothing, so they are true everywhere whether or not you have
been there.

There was a warning beside them — an orange corner for carried marks that had
landed on nothing — and it was taken out because it fired on drawings whose
colour was perfectly good. What it was measuring, and why the measurement cannot
carry a flag, is in [docs/handover.md](docs/handover.md).

The layer panel on the right adds, removes, reorders, hides and fades layers.
Layers belong to the track rather than to the image, which is the point of the
whole model: adding a layer touches no drawing, and a drawing held over five
frames holds every one of its layers for those five frames.

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
