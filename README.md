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

Everything under [docs/](docs/), and whether it describes something that exists:

| | |
|---|---|
| [handover.md](docs/handover.md) | how the program fits together, what was built and what it cost, and the traps. Start at its own table of contents. **Built** |
| [why-our-own-formats.md](docs/why-our-own-formats.md) | why the project file and the cel format are its own, measured against the alternatives. **Built** |
| [scribbles-through-time.md](docs/scribbles-through-time.md) | a scribble staying from one drawing to the next, and moving to follow the animation. **Built** as far as one translation per drawing; the rungs past that are research |
| [lasso-and-transform.md](docs/lasso-and-transform.md) | selecting part of a drawing, moving, rotating and scaling it, and the clipboard. **Built** |
| [m0-latency.md](docs/m0-latency.md) | the pen-to-pixel latency gate, and how it is measured. **Built** |
| [importing.md](docs/importing.md) | bringing in image sequences and audio. Two decisions still open: which audio library, and how an imported sequence is stored. **Not built** |
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

## Running it

Double-click `run-animage.bat`, or:

```bash
cmake --build build --target animage && ./build/src/app/animage
```

| | |
|---|---|
| Draw | pen, or left mouse |
| `Shift`-drag | a straight line, at any angle: where the pen lands to where it lifts |
| `B` / `E` | brush / eraser (turning the stylus over also erases) |
| `L` | lasso: loop round part of the drawing |
| `T` | transform: move, turn or resize the selection, or the whole drawing |
| Backspace | erase what is selected (`Delete` still deletes the drawing) |
| `Ctrl+X` / `C` / `V` | cut, copy, paste — the selection, or the whole drawing |
| Enter, Escape, arrows | during a transform: apply, cancel, nudge (`Shift` for ten) |
| `[` / `]` | smaller / larger |
| Wheel | zoom about the pointer |
| Space-drag, middle-drag | pan |
| `1` / `0` | actual size / fit the canvas |
| `F` | fit the drawing, including whatever ran off the edge |
| `H` | hide the panels, and bring them back |
| Maximise / restore | fits the canvas, since the window it was framed for has gone |
| `Ctrl+Z`, `Ctrl+Shift+Z` | undo, redo |
| `Alt`+right-drag | brush size |
| `Alt`+click | pick the colour under the pointer (follows the pointer, taken where you let go) |
| Hold `Z` and drag | scrubby zoom |

**A straight line is any straight line.** Hold `Shift` when the pen goes down and
the stroke runs from where it landed to where it lifts, at whatever angle the
hand chose — it is not snapped to the horizontal, the vertical or a diagonal,
because a drawing has edges at every angle and a constraint that only knows three
of them is a constraint you have to work around. The path in between is thrown
away: wander wherever you like and the mark is the line.

Nothing is written until the pen lifts, so what you see meanwhile is a thin band
where the mark will go, and letting go of it is what puts ink on the drawing.
That means the line is one undo step, a straight line held through a frame change
lands whole on the drawing you end up on, and nothing is left behind on the one
you aimed from. Shift is read when the pen lands and the whole gesture keeps that
answer: reaching for it half way through cannot straighten a stroke that is
already on the paper.

The eraser is the same gesture. During a transform `Shift` means the
fifteen-degree constraint instead, which is the same key doing the analogous job
for the tool that has the pen.

**The lasso does not clip the brush.** You can draw anywhere whether or not
something is selected, and drawing outside a selection is not blocked, masked or
warned about — which is the largest difference from every other program with a
lasso in it. What a selection is here is an argument to three operations,
transform, erase and (later) copy, so it needs no mode and no panel, it is not
saved with the project, and an ordinary click clears it. It is cleared by
changing frame and survives changing layer: a loop is geometry, so re-lifting it
from another layer of the same drawing means something, and carrying it to
another drawing is how you transform the wrong thing.

**Transform** takes the selection, or the whole drawing on the layer you are on
if there is none. Corner handles resize both ways and edge handles one way, the
round knob above the box turns it (so does dragging just outside a corner),
dragging anywhere else moves it — inside the box or well away from it, which is
what you need when the thing being moved is a thin line or is being lined up
against the drawing underneath. `Shift` constrains a rotation to fifteen degrees
and a move to an axis, and the
numeric fields on the bar are the way to place something exactly, or to move a
box whose handles have gone off screen. Nothing is written until you apply, so
cancelling leaves no undo step; moving by a whole number of pixels does not
resample, so registration nudges never soften a line. Colour layers are excluded:
a mark there is a label rather than paint, and interpolating one invents colours.

**Flip X and Flip Y** are on the same bar, and they mirror about the middle of the
box. A flip is a state of the transform rather than something that happens to the
drawing — press it twice and you are exactly where you started — and it is exact:
mirroring moves the pixels without resampling them, so a flipped drawing is the
drawing, to the bit. That is why a handle dragged past its anchor squashes to
nothing instead of flipping: a mirror made out of a −1 scale would go through the
resampler, and a blurred mirror half a pixel out of place is not something
anything on screen would tell you about.

**A paste is a transform that came from the clipboard.** It lands at the
coordinates it was copied from — you paste to re-register something, not to drop
it wherever the view happens to be — and it arrives as a float you can place
before applying it, so nothing is written until you press Enter. The clipboard is
the program's own and not the system one: a 16-bit-per-channel image handed to
another program would lose precision, and it would be a different feature.

**All of those keys can be changed**, under Edit > Keyboard shortcuts. The list
is grouped and searchable, nothing changes until you press Apply, and Apply waits
until no two shortcuts that are ever live at the same time collide — it names
what has hit what rather than refusing the keys as you type them. Only what you
change is written down, to `shortcuts.json` in the platform's per-user
configuration directory — `%LOCALAPPDATA%\Animage\Animage` on Windows — so a
default improved in a later version still reaches you everywhere you left one
alone. Every tooltip in the program says
which key its control is on, and says the one it is on now.

Four things in that panel cannot be changed and are listed anyway. `Space`, `Z`,
`Alt` and `Shift` are *held* while you click or drag rather than pressed, which a
shortcut cannot express. Two of them still take their key, so an action rebound
onto `Space` would take panning away with nothing to say it had; `Alt` and
`Shift` take nothing from anybody and are listed because the panel is where you
go to find out what the keyboard does, and an answer without the eyedropper or
the straight line in it is the wrong answer.

Fitting the drawing is `F` and not the `Shift+0` it used to be. On a keyboard
whose digit row is the shifted face of another row — AZERTY, for one — typing
`0` at all means holding Shift, so `0` and `Shift+0` are one chord; Qt answers an
ambiguous shortcut by cycling between the candidates rather than by complaining,
which presents as the wrong thing happening every other press. That pair is one
of the two things the panel refuses, and it is the one nobody sees coming: they
are genuinely different sequences, and a check for duplicates passes them both.

The eyedropper is `Alt`+click on the drawing rather than the colour dialog's
"pick screen colour", which cannot work with a stylus — see **what the pen can
and cannot reach** below. Sampling the document is better regardless: it reads
the colour that was stored rather than what the monitor was showing after sRGB
encoding, the zoom filter and the onion skin.

**What the pen can and cannot reach** is worth knowing, because three things that
each look like the pen being broken are one mechanism. Qt's widgets are built for
a mouse, and a pen reaches them by promotion: Qt turns a tablet event into a
mouse event, but only when nothing accepted the tablet event, and it sends that
mouse event to whatever sits under the tip. Almost everything in the window is
fine — buttons, menus, sliders, the layer panel, dragging a layer to restack it —
because the widget under the tip is the widget the gesture meant.

What is not fine is anything that works by *grabbing* the mouse, which is a
widget saying "send me the pointer wherever it goes, until I say stop". A grab is
a promise about a mouse, and the pen never made it: its events keep going to
whatever is under the tip. So:

- The colour dialog's **pick screen colour** grabs the pointer to follow it
  across the screen. With a pen it never hears the click. Hence `Alt`+click.
- A **modal dialog** is the same fact from the other side: Qt withholds mouse
  events from a window a dialog has blocked, but tablet events go by what is
  under the tip regardless, so the pen used to draw on the canvas behind an open
  dialog. That one is fixed.

**Panels can be torn off and dragged back with a pen**, and getting there found
a second mechanism worth knowing about. A floating panel used to be given a
*native* window frame, which means its title bar belonged to Windows rather than
to Qt — and Windows Ink never reports a press on one. The panel could be dragged
out, because that gesture starts on a docked title bar, and then could not be
picked up again, because a fresh press on the floating one reached nothing at
all. Floating panels now wear a title bar Qt draws, which a pen can press like
any other part of a window. See
[#50](https://github.com/S-poony/Animage/issues/50).

**Whether a pen produces a double click depends on the platform**, which is worth
knowing if you are reading the code. Where Qt does the promoting it sends one,
and it is generous about it — two taps pair if they land within 10 px of each
other rather than the 5 px a mouse gets, because a hand holding a stylus is not a
hand holding a mouse. Where Windows Ink does it, two taps arrived as two ordinary
presses and no double click at all. Double-tapping a name in the layer panel or
the timeline to rename it works either way, because those two count the taps
themselves.

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

**Tracks.** One row each, under one ruler and one playhead. Click a row to work
on that track: the layer panel, the brush and every timing button follow it,
while the canvas goes on showing all of them. The Track menu adds, renames and
deletes them; a new track arrives at the bottom of the stack, with a layer and a
drawing so there is something to draw on.

Drag a track's name up or down the strip on the left to restack it — the top row
is the front of the picture, so that is how a background gets behind a character.
Hovering a name says what the track is and what it does with a drawing put down
on it. Double-click a name to rename it there, and a layer's name in the panel
the same way; Enter keeps the new name and Escape leaves the old one.

Tracks need not be the same length. What a track shows once the playhead is past
its last drawing is set under Track ▸ Past the last drawing: nothing (the
default), hold the last drawing, or cycle — and the status bar says which, for
the track you are on. That is about the picture, so
it applies to the flattened `composite/` export and not to the per-layer
sequences: **a layer's folder is as long as its own track.** A background drawn
once exports one frame, and downstream you import the still rather than a
sequence. It does mean layer folders can differ in length.

**How long the shot is.** By default, as long as the longest track. Tick *Fixed
scene length* under Edit ▸ Scene settings and the number beside it is the shot
instead, in frames, with the duration in seconds shown under it.

Fixed, the boundary is a red line down the timeline with a grip in the ruler, and
you can drag it. A track is allowed to run past it: those frames are washed out
in the timeline, the status bar says *outside the shot*, and they are not played
and not exported until you move the boundary. Nothing is thrown away -- you can
still scrub to them and draw on them -- so cutting a shot short is not the same
as deleting the end of it. Adding a drawing never moves the boundary; the scene
says how long the shot is, not the tracks.

**Overwrite drawings**, in the Track menu, is per track and on by default. On,
the shot is a fixed length and a new drawing lands on the playhead and takes over
the rest of the hold it lands in: a drawing held 11 frames with the playhead on
frame 4 keeps 3, and the new one takes the other 8. Off, adding a drawing puts it
in after the whole hold and the shot gets a frame longer. Duplicating
does the same, and so does dragging a card -- it takes over the rest of the hold
it is dropped on, and the frames it left are absorbed by the drawing beside them,
so the length never changes. It never takes a drawing's last frame, so nothing is
wiped out by putting something down: standing on the first frame of a hold the
new drawing starts one frame later, and a hold of one frame has nothing to spare
at all, so there the track goes back to getting longer.

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
it, so a stroke costs the coarse one and pausing is what buys the rest. Export
solves the same way, so the window keeps drawing while it does — and at the
resolution the drawing was made at rather than the interactive cap, which is
what the screen gets only after you have paused on a drawing.

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

**Exporting.** File ▸ Export sequences asks what to write — a sequence per
layer, the flattened picture, or both — in which format, and what to call the
export, then where to put it. The name is a folder of its own, so a shot's dozen sequence folders
arrive together instead of loose among whatever else was in the directory you
picked; it starts as the project's own name. Inside are the frames over the canvas
rectangle, a folder per layer:

```
the-shot/
  track-1_ink/     track-1_ink_0001.png     track-1_ink_0002.png  ...
  track-1_colour/  track-1_colour_0001.png  ...
  composite/       composite_0001.png       ...
```

The underscore separates the track from the layer from the frame number and
nothing else in a name is allowed to be one, so a layer called "layer 1" is
`layer-1` and the last number is always the frame. Hidden layers are not
written at all.

Name a layer whatever you like, up to sixty characters: punctuation and spaces
become hyphens, and accented and non-Latin letters are kept as they are. The one
thing to know is that this makes two names into one folder — `rough 1` and
`rough-1` both give `rough-1` — so if two layers of a track would collide, the
export stops before writing anything and tells you which two to rename. It
refuses rather than inventing a name, because a folder quietly called
`rough-1-2` is not one you would go looking for.

**16-bit PNG or EXR, and they are not the same picture.** PNG converts on
purpose — sRGB, unpremultiplied — and throws away about a third of what a
drawing holds; it is right where the destination expects PNG. EXR converts
nothing: half-float, linear light, premultiplied alpha, exactly the pixels the
compositor produced. So a frame written both ways holds different numbers, and
comparing them channel by channel will suggest one is broken when neither is. Colouring is solved for drawings nobody has opened — otherwise a
project straight off disk would export blank colour sequences — and it is solved
off the interface thread, so the progress dialog moves and Cancel answers.

**Exporting again over an old export replaces it**, after asking, rather than
writing in among it. Merging is the dangerous one: re-export a shot you have
since cut short and the old export's later frames sit after the new ones,
reading downstream as a perfectly well-formed sequence of the wrong length.
Cancelling halfway would splice two shots together at the seam. A folder that is
*not* an export — the project folder itself, most obviously — is never offered
for deletion; it asks for another name instead.

The layer panel on the right adds, removes, hides and fades layers, and layers
are restacked by dragging a row up or down the list — the top row is the top of
the stack. Layers belong to the track rather than to the image, which is the
point of the whole model: adding a layer touches no drawing, and a drawing held
over five frames holds every one of its layers for those five frames.

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
