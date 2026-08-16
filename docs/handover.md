# Handover

Written at the end of the first build, for whoever picks this up — including a
later me with none of the context. It covers what exists, what is deliberately
not here, the traps that cost the most time, and what I would do next.

The French documents in [fr/](fr/) are still the specification. This file only
records what happened when it was built.

**Start with "how the program fits together"** if you are picking this up to
change something. The rest of the file is a record — what was built, in the order
it was built, and what each thing cost — and it is worth reading, but it is not
the shape of the program. Those five maps are.

| | |
|---|---|
| [Where it got to](#where-it-got-to) | what exists, milestone by milestone |
| [**How the program fits together**](#how-the-program-fits-together) | five paths traced end to end, each across several files |
| [Several tracks, and a track that overwrites](#several-tracks-and-a-track-that-overwrites) | what a second track changed, and what it broke |
| [Restacking by dragging](#restacking-by-dragging) | layers and tracks, one gesture, and where the panel had been scrolling to |
| [Naming a track or a layer](#naming-a-track-or-a-layer) | renaming a row where it is, and what a name is allowed to be |
| [Colour through time](#colour-through-time) | a mark carried to a drawing that has none |
| [Colour through time, part two](#colour-through-time-part-two) | and moved to where that drawing went |
| [What a track does past its last drawing](#what-a-track-does-past-its-last-drawing) | holds, shows, and the difference |
| [What the keyboard does, and when](#what-the-keyboard-does-and-when) | the shortcut table, the first mode, and changing a key |
| [A straight line](#a-straight-line) | one key, and a stroke that writes nothing until it is let go |
| [Moving a drawing](#moving-a-drawing) | the transform tool |
| [The lasso](#the-lasso) | and what a selection is here |
| [Copy, cut and paste](#copy-cut-and-paste) | which is a float from the clipboard |
| [What a transform costs](#what-a-transform-costs) | measured, then made to cost less |
| [What a commit does to a line](#what-a-commit-does-to-a-line) | one filter chosen on the wrong quantity, and what it did to a rim |
| [What the pointer says](#what-the-pointer-says) | one place deciding it, in the canvas and in the timeline |
| [**Looking at the interface**](#looking-at-the-interface) | `shots`: a picture of the program, per situation, and yours to add to |
| [**The same source, two different pictures**](#the-same-source-two-different-pictures) | what a downloaded build does not share with yours, and where to look first |
| [What the history is allowed to cost](#what-the-history-is-allowed-to-cost) | a budget in bytes, and a save marker that had to stop counting steps |
| [What is not what the plan asked for](#what-is-not-what-the-plan-asked-for) | deliberate departures, each reversible |
| [**The traps**](#the-traps) | the things that cost hours, worst first |
| [How to work on it](#how-to-work-on-it) | build, test, what each benchmark is for, and where the icon comes from |
| [What I would do next](#what-i-would-do-next) | the queue |
| [Three things to be careful of](#three-things-to-be-careful-of) | the two bets everything rests on, and one word |

## Where it got to

M0 through M4 exist, and **M5 is done**: a project saves, opens, saves itself,
starts over and exports. A session's work survives the window closing, which it
did not before — and now survives not thinking about it at all.

| | |
|---|---|
| M0 | Pen latency measured at ~15 ms, passed. [m0-latency.md](m0-latency.md) |
| M1 | Data model, copy-on-write tiles, undo. All five plan tests pass. |
| M2 | Canvas, pressure brush, eraser, layers. Compositing is on the CPU, not the GPU. |
| M3 | Timeline, holds, onion skin, playback, drawing during playback. |
| M4 | LazyBrush solver and the CTG layer, in the app and usable. Solved on a worker thread. |
| M5 | Save, open, Save As, autosave, New, and export as 16-bit PNG or half EXR. |
| — | Several tracks in the interface, and an "overwrite drawings" setting per track. |

A project is a folder: `scene.json` in text, and one file per cel beside it.
All of it lives in the application now, in one place — `ProjectIO`, which is
the single class that turns a folder on disk into a document in memory and
back. `core` is the model and knows nothing of bytes on disk. Saving builds
alongside and swaps at the end, so an interrupted save leaves the last good
project where it was; opening builds a whole document before adopting it, so a
project that will not open cannot take the open one down with it.

`scene.json` is read and written with Qt's QJsonDocument — the hand-rolled
JSON reader is gone — and the readers are pinned by hostile-file tests
(`tests/test_hostile.cpp`) that replay the crashes and undefined behaviour
the old reader had, so they cannot come back. The build denies warnings, and the
tests ask for ASan and UBSan by default, so a memory error or an out-of-range
conversion fails the tests instead of shipping — **where the sanitizers exist**,
which on Windows they do not. MSYS2's UCRT64 GCC links neither runtime, the
probe in `CMakeLists.txt` fails, and the build carries on with none while
`ANIMAGE_SANITIZE` is still reported as `ON`. The configure step says so, once:

```
ANIMAGE_SANITIZE is ON but no sanitizer runtime links; tests will not
detect memory or UB errors
```

It scrolls past, and after that nothing distinguishes a sanitized build from an
unsanitized one — `build/CMakeCache.txt` holds the honest answer in
`ANIMAGE_ASAN_UBSAN_OK` and `ANIMAGE_UBSAN_OK`, both empty here. So a green local
`ctest` on Windows says less than this paragraph used to claim it did. Worth
knowing before trusting a clean run on a change that moves memory about.

**The core can be sanitized on Windows, and CI now does it on every push.** MSVC
has AddressSanitizer, and the core library needs no Qt — which is the whole
reason this is cheap, since MSYS2's Qt cannot link against MSVC and installing a
second Qt to sanitize the widgets would buy the half of the program that draws
rectangles. From a Developer Command Prompt:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

`-- Sanitizers: AddressSanitizer` and `-- Qt 6 not found: building the core
library and tests only` is it working. All ten core tests pass under it. Two
things to know: the binaries only run inside that shell, because MSVC links ASan
dynamically and `clang_rt.asan_dynamic-x86_64.dll` lives beside the compiler — a
test dying instantly with `0xC0000135` is a missing DLL and not a bug you just
wrote. And MSVC has no UBSan, so undefined behaviour is still the Linux job's to
catch. The `sanitizers (windows, core)` job in `ci.yml` runs exactly this.

**Saving is incremental, and autosave rests on that.** A cel's revision is
bumped by every write to it, undo included, so a `ProjectIO::SaveState` — a
folder and the revisions written to it — is enough to know which files in that folder
are still current. The ones that are get carried into the folder being built as
a hard link rather than as new bytes, which keeps the build-alongside-and-swap
exactly as it was while paying nothing for the drawings that did not move. On a
96-drawing shot a save went from 3047 ms to 126 ms with nothing changed and
140 ms with one drawing touched; a first save and a Save As are unchanged,
because neither has anything to carry.

The chain is link, then copy, then encode in full, so a filesystem that will not
link or a file that has gone missing makes a save *slower* and never wrong. That
matters more than it sounds: the state is a claim about the pixels and never a
promise about the disk, and a sync client is entitled to move the disk.

Autosave then fires every two minutes, writing over the project, deferred while
the pen is down or the animation is playing and silent when nothing has moved.
Closing the window writes too. That was the deliberate choice the plan describes
and it has the consequence it always had: the disk is always current, so
"quit without saving to throw away a ruined drawing" is gone, and undo within
the session is the only way back. The `*` in the title stayed, now meaning
"not yet autosaved" — it clears itself within two minutes and is the only sign
that the last few strokes are still only in memory.

One cost is worth writing down because it was raised and accepted rather than
missed. The swap replaces every directory entry in the project on every save,
hard links included, so a folder on Google Drive or Dropbox has its whole file
list touched each time even though almost no bytes changed. What is left of the
126 ms is exactly that — creating 192 entries and dropping 192 old ones, with no
pixel work at all. Writing in place instead would touch only the changed files
and sync far better, at the cost of the guarantee that an interrupted save
leaves the last good project alone. That trade is open, and the numbers to
re-argue it are in `bench_save`.

**New, Open and Close all go through one door.** `leaveCurrentDocument` is what
they call first, and it says the thing autosave implies: a project with a folder
is simply written, because leaving is not a way to discard. The single case
autosave cannot cover is a document that has never been saved anywhere — there
is no folder to write it to — so that is the only case that asks, and it is the
only place in the program where work can be lost by answering a question wrong.
New then rebuilds the same document the application starts with, including the
`clearHistory` that stops a fresh document arriving with three undoable setup
steps on the stack, and opens Scene settings.

**Export writes 16-bit PNG and EXR**, a folder per layer with
`{track}_{layer}_{frame:04}.{ext}` inside, plus an optional `composite/` of the
flattened picture, all over the canvas rectangle, and all inside a folder the
export dialog asks you to name — defaulting to the project's. Hidden layers are
not written at all, so the per-layer sequences and the flattened one agree about
what the shot contains.

**The two formats answer different questions and are not interchangeable.** PNG
converts on purpose — sRGB, unpremultiplied, 16-bit integer — and loses about a
third of the half values in [0,1] doing it; the arithmetic is in
`export_sequence.h`. EXR converts not at all: half, linear, premultiplied, which
is what a `Framebuffer` already holds. **So the same frame written both ways does
not contain the same numbers**, and anyone comparing them channel by channel will
conclude one is broken. That is why the dialog says so and not only this file.

The EXR writer is `exr_writer.cpp`, which is the only place tinyexr is compiled —
one vendored BSD-3 header in `third_party/`, included SYSTEM, and that one
translation unit built with warnings off so the rest of the tree keeps `-Werror`
meaning something.

**Where the compressor comes from is the part that bit.** tinyexr wants a
zlib-compatible API for ZIP, and the project had never linked zlib — the cel
format goes through Qt's `qCompress`. The first version used
`find_package(ZLIB REQUIRED)` on the stated grounds that "zlib is a transitive
dependency of Qt, so it cannot fail on a machine that has got this far". **That
was wrong, and CI said so within four minutes.** Qt bundles zlib internally and
ships neither the headers nor an importable target, so Linux passed (apt has it),
macOS passed (the SDK has it) and **Windows failed at configure** with
`Could NOT find ZLIB`, because the Windows job installs no dependencies at all
and never needed to.

The lesson is not about zlib. It is that "X is a dependency of Y, so X is
present" confuses *being linked into* with *being available to link against*, and
a bundled library is exactly the case where those come apart. The three platforms
disagreeing is what made it visible; a claim like that, verified only on the
machine it was written on, will hold there and nowhere else.

**miniz is vendored beside tinyexr instead** — MIT, from tinyexr's own `deps/`,
and `TINYEXR_USE_MINIZ` is what tinyexr expects by default anyway. That removes
the question on all three platforms with no per-platform install step, and it
makes the compressed bytes identical everywhere, which linking three different
system zlibs would not. It costs a second vendored library and a second licence
in the tree, and nothing measurable in time: the suite is 40 s either way.
`MINIZ_NO_ZLIB_COMPATIBLE_NAMES` is set, because tinyexr only calls the
`mz_`-prefixed entry points and the zlib-named aliases would otherwise define
`compress2` and `uncompress` into a process that may already have them from the
zlib Qt links — a collision that resolves silently and differently per platform.
Both files are compiled with warnings off, like the writer itself.

Three things about the writer that a round trip will not catch, which is why they
are written down. It reads the `Framebuffer` directly and builds no QImage —
`Rgba` is float32 and interleaved, EXR wants planar half, and Qt cannot write EXR
anyway. The channel planes are named A, B, G, R alphabetically, because a wrong
order produces a file that loads perfectly and shows a blue scribble; there is a
test with an orange one, and it was checked that mislabelling the planes turns it
red. And `requested_pixel_types` is HALF as well as `pixel_types` — a mismatch
there is where a lossless writer quietly becomes a converting one.

**What verified it, and what did not.** The round-trip test reads the file back
with tinyexr, which is the library that wrote it — measuring twice on the same
side of the event, and it proves less than it looks. The real check was
OpenEXR's own `exrheader` and `exrmaketiled` (installed locally, never linked),
which are a different implementation: the header reports four 16-bit
floating-point channels, ZIP in blocks of 16 scan lines, and `dataWindow` equal
to `displayWindow`, and re-tiling forces every pixel through OpenEXR's decoder.
If you change the writer, run those again rather than trusting the test.

There is a second test pinning that the EXR and the PNG are **the same
picture**, differing only by the conversions the PNG makes. Two things about it
are worth reading before changing it, because both were wrong first.

Its tolerance is 24 parts in 65535 and deliberately not zero. The PNG's numbers
come from the float32 the compositor works in; the EXR's have been through half,
because half is what it stores — so the EXR path quantises *before* the sRGB
curve and the curve then magnifies the step, about 15 parts near 0.9 against a
measured worst of 10. Demanding equality failed on 420 fully opaque pixels and
looked like a bug; it was arithmetic.

And the fixture sets the colour layer to **half opacity**, which is the whole
reason the test can fail at all. Without it every partly-covered pixel in the
scene belongs to the black line art — and black is the one colour where
premultiplied and straight agree, both being zero. The first version passed with
the unpremultiply deleted from it. If you write another test about premultiplied
alpha, the thing it needs is a translucent *coloured* region, and the way to know
you have one is to break the code and watch it go red.

### The EXR decisions, and what would change them

Argued out before the writer was written, and it was the right order: the
expensive half of a format is not the writing.

**A file per layer, not every layer as channels in one file per frame.** The
specification asks for `un dossier par calque` and the French documents are
authoritative, so channels-in-one-file is a deviation needing an argument rather
than a preference. It is also purely additive later — multi-channel would be a
third entry in `exporting::Format`, not a replacement — so nothing is
foreclosed. Blender reads plain EXR sequences perfectly well; what multilayer
buys there is convenience in the Image node, not capability. After Effects is
*specifically* awkward with multi-channel, needing the EXtractoR effect, and poor
with multi-part, while being perfectly happy with plain RGBA. **What would change
it:** somebody working in Blender and finding the folders annoying. Before
building it, check that Blender groups `layer.R`/`layer.G`/… into selectable
layers by testing a hand-made file; that is believed here rather than known.

**Linear and premultiplied, making neither conversion the PNG path makes.** Not
really a choice — it is what EXR's convention is, and the whole of why EXR is the
lossless option.

**No `chromaticities` attribute.** A wash in effect, since an absent one means
sRGB primaries by convention and the file makes that claim either way; it comes
down to whether we assert something the codebase has not decided. The working
space today is "linear light" and nothing narrower, and
`fr/tablettes-couleur-licence.md` contemplates Rec.2020 or ACEScg later. The same
ambiguity is already in the PNG path, which applies the sRGB transfer function
and assumes its primaries, so EXR does not introduce it. **What would change it:**
pinning the working space, at which point write it.

**ZIP compression.** Lossless, universally read, and line art is mostly
transparent so it crushes. Not DWAA, DWAB or B44, which are lossy and would
defeat the point of choosing EXR at all. A constant rather than a setting until
somebody asks.

**`dataWindow` equal to `displayWindow`, both the canvas.** EXR can store less
than the frame it declares, which would let an export keep only the drawn
bounding box while every frame stays logically identical — the sparse property
the cel format exists for, applied to export, and a real saving on typical line
art. It is not the default because a mismatch is handled badly by some readers,
After Effects historically among them, and because every frame being the canvas
rectangle is a promise the export tests pin. **What would change it:** wanting the
file sizes more than the compatibility. A more radical version keeps what runs
off the canvas, which is a question about what an export *is* rather than about a
format.

**Half channels, not float.** Exact match to storage, half the bytes, no
downside.

**Measure before threading the encode.** Solving is off the interface thread now,
so encoding is the largest thing left on it — but there is no `bench_export`, and
this file's own lesson is that a benchmark decides where you will look next.
Write that first and optimise second.

One behavioural difference to know: `toShort` clamps to [0,1] and EXR does not.
Nothing in a composited frame is out of range today — the transparent label's
negative light lives in scribble cels and the compositor resolves it — but if one
ever leaked, PNG would hide it and EXR would preserve it.

**An export replaces what was in the folder, and "overwrite" had to be made
true before it could be said.** The word was going to go on a confirmation
dialog over the merge that was already happening, and it would have been a lie —
`write` creates folders and writes files into them, and nothing clears what was
there. Same-named files are replaced and everything else stays, so re-exporting
a shot you have since cut short leaves the old export's later frames after the
new ones, which downstream is a well-formed sequence of the wrong length, and
cancelling halfway splices two shots at the seam. Both are silent and both look
right. The dialog would have been reassuring in exactly the cases that were
wrong.

So the folder is emptied first, and the price of that is a recursive delete
needing a guard. `exporting::occupantOf` answers Nothing, AnExport or
SomethingElse, and it is strict on purpose: an export is folders of frames named
after their folder and *nothing else whatever*, bar the junk a file browser
leaves (`.DS_Store` and friends), which is ignored and deleted with the rest.
SomethingElse is refused rather than weighed up — the project folder is the
obvious way to point `rm -r` at every drawing in the shot, and it is tested that
one is not mistaken for an export. Save's build-alongside-and-swap is still not
what export does; this only decides what the folder holds before it starts.

**The underscore in an exported name means one thing.** It separates the track
from the layer from the frame number, so every other character that is not a
letter or a digit — spaces, punctuation, and an underscore somebody typed —
becomes a hyphen, runs of them collapse to one, and the ends are trimmed. That
is what makes `track-1_layer-1_0007` readable: three fields, and the last number
is always the frame. It also decided the default track's name, which was `main`
and is now `track 1`: the model and the timeline both take several tracks
already and only the interface does not, so the first one may as well say which
number it is rather than being the one that never does.

**A name is only ever a folder name here**, and that bounds how much damage a
strange one can do. Cel files are `cel-000123.acel` — ids and never names — and a
name lives in `scene.json` as a JSON string, which `QJsonDocument` escapes and
the hostile-file tests pin. So nothing anybody types can hurt a project; the
whole question is what the export makes of it. Two things it made of it were
wrong, and both were reported after renaming got easy enough to do:

- **`sanitise` walked bytes and not characters.** A `std::string` here is UTF-8,
  so `isLetterOrNumber` was handed half a letter at a time: the first byte of `é`
  is a letter on its own and the second is not, and `décor` exported as
  `dÃ-cor`. Nothing failed — the export succeeded and the folder was gibberish —
  which is why it survived a green suite and a French specification. Accented and
  non-Latin letters are **kept** rather than reduced to ASCII: they are what
  somebody typed, all three platforms take them in a path, and the alternative is
  a transliteration table with no end to it.
- **Two names can be one folder, and that silently lost a layer.** Sanitising is
  many-to-one, so `rough 1` and `rough-1` are two names and one folder; identical
  names are not prevented anywhere either. Both wrote the same filenames into the
  same folder frame by frame, so the export succeeded, looked complete, and held
  one layer where two were asked for. It is refused now, naming both layers —
  `occupantOf`'s stance rather than a quiet `-2` suffix, and for the same reason:
  nobody works out that clash from a folder listing afterwards.

- **And a name can be too long to write, which also failed partway.** The
  measurements are in `name_limits.h`, and the short version is that a name goes
  into a path twice — folder and file stem — so the folder can be legal while the
  file inside it is not. Refused with the collision, for the same reason and in
  the same place.

**The refusal is asked for twice, and the second place is the one that matters.**
`write` checks before it creates anything, which is enough to make the *export*
safe and is not enough to make the operation safe: an export replaces what was in
the folder, so `MainWindow::exportSequences` **empties it first**. A refusal that
lived only inside `write` would therefore have thrown the previous export away in
order to produce nothing — the right answer arriving after the damage. So
`namesCollide` is on the header and is asked before the clearing, and `write`
asks again because it cannot assume its caller did. That ordering is the whole
reason the check is public; if the clearing ever moves, this moves with it.

Two more things about the collision, before changing the naming. **It can only
happen within one track**, because the underscore separates the fields: `a b`/`c`
and `a`/`b c` give `a-b_c` and `a_b-c`, and the separator being in a different
place is what keeps them apart. And **nothing can collide with `composite`**,
which has no underscore in it at all. Hidden layers are skipped, because they are
not written — a check over every layer rather than every *written* layer would
refuse exports that were never in danger.

What is *not* handled is a name Windows reserves for a device — `CON`, `NUL`,
`AUX`, `PRN`, `COM1` and friends. Those are letters, so they survive sanitising,
and the export then fails at `mkpath` with "cannot create CON". That is loud and
loses nothing — no frame is written and the message names the folder — so what is
wrong with it is only the explanation, and it is
[issue #34](https://github.com/S-poony/Animage/issues/34) rather than a fix here.

One thing about export is worth reading before touching it. **A CTG layer's
fill is a cache, and the canvas only builds it for the frame on screen**,
because compositing is not allowed to start a max-flow. So a project straight
off disk has no fills at all, and an export that composited only what was
cached wrote blank colour sequences and said nothing about it — which is why
`exporting::write` takes the document by mutable reference and solves what is
missing. The price is that exporting a coloured shot for the first time pays one
max-flow per CTG layer per distinct drawing.

**Those max-flows are on a worker now, and getting them there moved the loop
they were in.** `exporting::Solve` is a callback: `write` hands over a `CtgJob`
and installs the `CtgFill` that comes back, and `MainWindow` implements it with
a `CtgSolver` and a nested event loop, so the interface thread waits somewhere
it can still paint the progress dialog and answer Cancel. Off the interface
thread it can also ask for `kFullSolveBudget`, which fixes a quality bug nobody
had reported: an exported fill used to be capped where the one on screen was
not. Passing no `Solve` solves where the caller stands, capped — what the tests
do, and what the behaviour was.

Two things about that are worth knowing before changing it.

- **The solver is the export's own and not the canvas's**, which is not what
  this file used to suggest. Results are collected rather than delivered, so a
  `CtgSolver` has exactly one owner: everything it finishes goes to whoever
  calls `collect()`, and `CanvasWidget::collectColour` drops what the canvas did
  not ask for. Sharing one would mean a repaint during the export — which the
  progress dialog causes — quietly swallowing the answer the export was waiting
  for, and the wait would never end. One worker, so one max-flow at a time,
  which is also the memory bound.
- **Frames are written in slot order, every sequence at once**, rather than one
  whole sequence after another. That is what makes each drawing solve exactly
  once. Sequence by sequence, a colour layer's pass solves every drawing in the
  shot and the flattened pass asks for them all over again — by which time the
  bounded fill cache has evicted the early ones, so the same max-flows run
  twice. It is also what lets the solves be counted in advance, which is what
  the progress bar needs: a max-flow is a second and a half and a PNG is
  milliseconds, so a bar counting only files sits still through the whole of the
  slow half and then runs to the end.

Since the first build, the model also grew a **canvas**: `Scene::canvas()`, the
rectangle that will be exported, set under Edit ▸ Scene settings. Before it
there was no such thing as "the picture" — tiles are sparse and their
coordinates signed, so the drawing surface has no edges at all, which is
deliberate and stays true. Drawing outside the canvas is still allowed; what is
out there simply is not in the picture, which is why a colour fill stops at the
frame.

## How the program fits together

Five paths, each traced end to end. They are here rather than further down
because they are the thing worth having before changing anything, and because
none of them is written in any single file it passes through — that is exactly
why they are worth writing.

Each one names the functions in the order they run. Where a decision inside one
has a reason, the reason is elsewhere in this file; these are maps and not
arguments.

### Where a stroke becomes pixels on screen, in order

The most-crossed path in the program, and the one anything about tools, input or
rendering starts from.

**Down.** The pen arrives at `CanvasWidget::tabletEvent`, which asks five
questions before it draws anything: is a modal dialog up (leave the event alone —
it has a better claim on the pen); is there a *child widget* under the pointer
(the transform bar floats on the canvas, so the event must be left unaccepted for
Qt to synthesise the mouse event that a spin box listens for); is this a
navigation gesture (Space-drag, held Z, Alt and the right button); is Alt down
(the eyedropper); is a transform or the lasso live. Only then `beginStroke`.

`beginStroke` refuses where there is nothing to draw on — no cel, a locked layer,
a hidden one — then copies the brush's or the eraser's settings, overrides them
on a CTG layer so the mark is a hard label rather than paint, opens a
`ScopedCommand`, and hands the stroke to `Brush`.

**Into the document.** `Brush` asks `Document::celForWriting`, which creates the
cel on first use and records that so undo removes it, then `Cel::writableTile`
per tile touched — which copies the tile if anything else is sharing it, and
journals the tile it displaced *once per command*, not once per dab. Nothing
composites here. Each dab only calls `markDirty` with its rectangle and asks for
a repaint of it.

**Up, and onto the screen.** All the flattening happens in `paintEvent`, once,
however many edits arrived since the last one. `ensureCacheCoversView` settles
`cache_step_` — one cache entry per *screen* pixel, so the cache is the size of
the window at every zoom — and the region it covers. `requestCtgFills` asks for
any colour that has gone stale and computes none of it. Then either the whole
cached region or the accumulated dirty rectangle goes through `refreshRegion`,
which calls `Compositor::compositeScene` into `scratch_` and converts paper,
onion skin and drawing to sRGB in `display_` across a short-lived thread pool.
That conversion loop is the larger half of a refresh by a wide margin — see the
traps. Finally `display_` is blitted through one `QRectF`, nearest-neighbour only
above 3× magnification, and the canvas frame is drawn over it.

**The pen lifts.** `endStroke` closes the command — which is where
`Document::endCommand` drops the tiles the stroke emptied — refreshes everything
and emits `documentChanged`. The colour solve is asked for by the *next* paint,
never by the stroke.

The release is not the only way there, and it must not be: `abandonGesture` ends
the stroke the same way when the window stops being active or the keyboard
leaves the canvas, and `beginStroke` closes an open one before opening another.
See [the traps](#the-traps) for what one missed release used to cost.

Worth knowing before touching any of it: **nothing along this path sets the
cursor**. There is one function that decides what the pointer looks like, from
what is true rather than from where the code has got to — see
[what the pointer says](#what-the-pointer-says). It used to be nine `setCursor`
calls, three of them repeating the same held-key chain by hand.

### Where the colour comes from, in order

The one map I wanted when I picked this up, because the path crosses four files
and no single one of them says so.

A stroke ends, and `CanvasWidget::paintEvent` calls `requestCtgFills`. That
compares a hash of everything the fill depends on (`ctgInputsFor`) against the
fill already in `Document::ctgCache()`, and if they differ it takes a `CtgJob` —
a copy of the marks, the ink and the canvas, in shared tile handles — and hands
it to `CtgSolver`. Nothing is computed on the interface thread; the paint
finishes with whatever fill is in the cache, which is the last answer.

A worker runs `solveCtgJob`: estimate how far the drawing has moved since the
marks were made (`estimateCtgShift`), flatten the ink into a barrier
(`ctgBarrier`), read the marks through that shift into seeds, run the max-flow
(`solveLazyBrush` over `GridFlow`), and paint the labels back into tiles. A 16 ms
poll on the canvas collects the result, puts it in the cache, marks everything
dirty and emits `colourChanged`; `MainWindow` refreshes the timeline, the layer
panel and the status bar from that one signal.

The compositor draws whatever fill is in the cache and never starts a solve —
`Document::ctgFillFor` is const for exactly that reason.

### Where a transform's pixels go, in order

Nothing on this path writes the document until the last step, and that is the
whole design rather than an optimisation.

**Picking up.** The Transform tool calls `beginTransform`, which checks
`refuseHere` — the same list the brush checks, plus the layer kind — and then
`liftForTransform`. With a loop that is `rasterise` (an even-odd scanline fill
producing a coverage mask) followed by `liftThrough`, which splits the cel in two:
`lifted = src × c` and `remaining = src × (1 − c)`. With no loop everything is
lifted and nothing remains, which is what makes the two cases one code path. The
box is `paintedBounds` of the lifted half — every pixel, not every tile.

**Holding.** `buildTransformPicture` composites the lifted half *once*, through
`compositeGrids`, into an ARGB image bounded at 2048 on its longest side. Every
paint after that draws the scene with `SubstitutedLayer{layer, &remaining}` — so
the hole stands in the layer's own place in the stack rather than being painted
over the top of it — then a veil over everything, then that image blitted through
`from_picture * moving * to_widget`, then the box, the handles and the rotation
knob. A drag edits the five numbers in `Transform` and nothing else;
`endTransformDrag` puts the pivot back to the middle so the numeric fields keep
meaning one thing.

**Putting down.** `applyTransform` runs `transformTiles` — the exact block-copy
path for a whole-pixel translation, bilinear for magnification and rotation, a
box filter over the source footprint for reduction — then `mergeOver` to land it
on the half that stayed, then `Cel::replaceTiles`, all inside one
`ScopedCommand`. `replaceTiles` journals both sides, the tiles arriving and the
tiles going away, so undo restores the drawing exactly.

Cancel resets the state and leaves no undo entry, because nothing was ever
written. An identity writes nothing either — except for a paste, where landing
something at the coordinates it came from *is* the operation.

### Where a frame change goes, in order

Short, and worth having written down because getting it half right is silent.

**One thing owns the playhead:** `TimelineWidget::current_slot_`.
`setCurrentSlot` clamps it against `Scene::timelineFrames()` — everything
reachable, which can be longer than the shot — and emits `currentSlotChanged`.
`MainWindow::onSlotChanged` is the only listener that matters: it calls
`canvas_->setFrame`, refreshes the layer flags and syncs the status bar.

`CanvasWidget::setFrame` is where the consequences live. It commits a live
transform, clears the selection, rebinds a stroke that is still in progress onto
the new drawing inside the same command, re-reads which drawing this track
*holds* here — `Track::imageAtSlot`, not `imageShownAt`, because past the end
there is nothing to edit even when there is something to see — marks the onion
skin dirty and refreshes.

**And `MainWindow::refreshEverything` runs the other way**, pulling the canvas
back to `timeline_widget_->currentSlot()`. So anything that moves the canvas
without moving the timeline is undone by the next refresh. That is not
hypothetical: it is how a paste committed itself before it could be placed, since
a frame change is exactly what bakes a float. Move the two together.

Playback is the same entry point at twenty-four frames a second.

### Where a project goes to disk and comes back, in order

`ProjectIO` is the only class that turns a folder into a document and back;
`core` knows nothing about bytes on disk.

**Out.** Save, Save As, autosave and `leaveCurrentDocument` all end in
`ProjectIO::save(doc, folder, save_state_)`. It builds in a scratch folder beside
the target, pixels first — so a failure never leaves a `scene.json` pointing at a
cel that does not exist. A cel whose revision matches the `SaveState` from the
last save of *this same folder* is carried forward as a hard link instead of
re-encoded, which is what makes autosave affordable; a link that fails falls back
to writing the bytes, so the state is a hint and never a promise. Then
`scene.json`, then the swap: the old folder is moved aside, the new one renamed
into place, and only then is the old one deleted.

`encodeCel` drops fully transparent tiles, sorts the coordinates so an unchanged
drawing encodes to identical bytes, and writes only the occupied span of each
row — which is the difference between a 3 MB file and a 457 MB one.

**In.** `load` reads `scene.json` into a **document of its own**, fills every cel
it names, and only then assigns over the open document. A project with one bad
cel in it therefore cannot leave you with half of it and none of what you had.
`MainWindow::afterProjectLoaded` then rebinds everything that was holding ids
from the document that has just gone: the canvas, the timeline, the layer panel
and the status bar.

## Several tracks, and a track that overwrites

Issues #1 and #9, built together because the second needs somewhere to live and
the first is what gives it one.

**The model and the file always took several tracks; nothing could make a
second one.** `writeTrack`/`readTrack`, `celsReferencedBy` and the whole export
already looped over `scene.tracks` — so the round-trip half of #1 was a test to
write rather than a bug to find, and it passed the first time it ran. That is
worth saying plainly rather than quietly: the check was the deliverable, and
`aMultiTrackProjectComesBackWhole` is now what stops it rotting. It builds three
tracks of deliberately different shapes and puts each drawing's stroke at a
height no other track uses, so a cel that came back attached to the wrong drawing
shows up as a pixel in the wrong place and not merely as a missing one.

What was single-track was the interface: `MainWindow::track_`, a canvas that
composited one track, and a timeline that drew one strip.

**The canvas shows every track and you draw on one.** `Compositor::compositeScene`
resolves every track's layers into one flat `LayerPass` list and composites it
once — not several tracks composited apart and blended, which is what the export's
flattened pass does and what this deliberately does not. Flat is the definition:
tracks stack as groups, and a group you could blend separately would need its own
opacity applied to it. `Track::opacity` is stored and still not applied, and if it
ever is, `compositeScene` is the function that stops being a flat list.

The division that fell out of it is worth keeping: **what follows the picture and
what follows the track.** Compositing, the eyedropper and which CTG fills get
solved all follow the picture, because they answer for the pixel you are looking
at. The brush, the onion skin and "fit to drawing" follow the current track,
because they are about the drawing in your hand. Getting the colour solves onto
the first list mattered — a background track's fill is never asked for otherwise,
so it would have arrived coloured and gone blank the moment you selected the
character.

That also broke an assumption that was true while there was one track: "the
drawing on screen" is no longer one drawing. `dropStaleColourRequests` cancelled
anything whose image was not `image_`, which with several tracks cancels every
other track's solve on every paint. It asks `isShownNow` now, which is the same
question against all of them.

**Tracks are not all the same length, and `Track::imageAtSlot` is the only place
that knows it.** Past its last slot a track shows nothing. That is a policy and
not a fact — issue #20 wants it per track: show nothing, hold the last drawing,
or cycle — so everything that draws a track goes through that one function and
nothing else compares a slot against `slots.size()` and decides for itself. The
playhead, the timeline's length and playback are all `Scene::frameCount()`, the
longest track, so a shot does not end where the track you happen to be editing
does.

**"Overwrite drawings" is a property of the track, so the buttons do not know
about it.** `Document::addDrawing` and `duplicateDrawing` take the playhead's
slot and place the drawing the way the track says; `MainWindow` asks
`firstSlotOf` where it ended up rather than assuming, because under overwrite it
lands neither where the playhead was nor after the hold.

The rule is `Track::overwriteRangeAt`: the rest of the hold from the playhead
onwards, **never including the hold's first frame**. That exception is the whole
design and it was a decision, not an oversight — taking the rest of a hold from
its first frame takes the entire drawing, and pressing Insert is not a way to
delete one. It has a consequence that reaches further than it looks:

- A hold of one frame has no room at all. An insert there falls back to
  lengthening the track and a drop falls back to reordering, because those are
  the length-preserving things each of them can still do.
- **Nothing can retire a drawing, so nothing has to tidy one away.** The first
  version of this carried a `dropUnusedImages` that swept up drawings no slot
  showed any more. It could never fire: the run being landed in always keeps a
  frame, and the frames a moved drawing vacates are always taken by a drawing
  still in the track. It came out, and `overwritingNeverLosesADrawing` pins the
  property instead — because it is exactly what a later change to the rule would
  break silently.

**Read the run before you move anything, and the first drawing is the one that
cannot move.** Reported on `1...2....3.....`: nudging the first drawing one frame
swapped it with the second, leaving drawing 2 holding a single frame and drawing
1 holding everything up to drawing 3.

The cause is an ordering mistake. `moveDrawingOver` lifted the drawing out first,
handing the frames it was leaving to a neighbour — so the neighbour's run then
measured as *both* runs together, and "take the rest of the hold it lands in"
swallowed the lot. The run has to be read from the track as it stands.

**Which drawing it happened to is the interesting part, and it was the reporter
who worked it out rather than me.** The frames a drawing vacates go to the
drawing *before* it, and merging them backwards leaves the run's remainder from
any drop point exactly what it should be — so for every drawing with something in
front of it, lifting-out-first gave the right answer by luck. Only when the
drawing starts at slot 0 is there nothing in front, the frames go to the drawing
*after* instead, and the merged run then runs forwards through the neighbour.
That is the swap, and it can only ever have happened to the first drawing of a
track.

Worth recording because the first fix for it was too broad: it made every drop
inside a drawing's own hold do nothing, which is right for the first drawing and
wrong for all the others — it stopped drawings being nudged along their own holds
at all, which had always worked. Dragging along your own hold means "start here",
and the hold in front grows by what you gave up; the first drawing of a track has
no hold in front to grow, so it alone stays put. One case, not a rule.

The general shape is worth keeping: **an operation defined in terms of "the run
at X" cannot compute X from a strip it has already modified.** The intermediate
state has runs in it that were never really there — and it will agree with the
right answer often enough to look correct.

**Moving over a hold has two coordinate systems and neither derives from the
other.** `moveDrawing` takes a position *between* drawings, counted with the
drawing lifted out; `moveDrawingOver` takes the frame it was dropped *on*. One
function with two meanings for its argument would have been a bug waiting on
whichever caller guessed wrong, so there are two, and the widget picks — which it
must anyway, since the drop caret is a line in one case and a range of cells in
the other. Vacated frames go to the neighbour before them, or the one after when
the drawing left the very start.

**The timeline is rows now, one per track, under one ruler and one playhead.**
Two things about it. The track names are a gutter down the left rather than a
control on each row, because a widget on a row disables that row's own hit
testing — the same trap the layer panel already records. And the blue rim marking
the current frame is drawn on the current track's row only: there is one playhead
and the ruler says where it is, but the rim means "this is the frame you are
editing", and on every row it read as four selections the brush was not going to
touch. Reported on a screenshot, which is the only thing that would have caught
it.

**Overwrite is on by default**, which was asked for after the first build of it
and is the right way round: you block a shot out in holds and then break them
down, so a default that lengthens the shot on every breakdown means retiming it
by hand afterwards. `readTrack` falls back to *the track default* rather than to
what the build that wrote the file did, so a project saved before the setting
existed changes behaviour when it is opened. That is deliberate — a default that
depended on the age of the file would be an invisible difference between two
tracks that look identical — and it only touches projects from before the key
existed, because every file this build writes carries it.

**The timeline dock sizes itself to the number of tracks, and getting that right
took three goes.** It was a fixed 96 px, which suited exactly one row and hid the
second behind a scrollbar — and a scrollbar reads as "scrolled", not as "too
small". Pinning `minimumHeight` and `maximumHeight` to the wanted height fixed
that and welded the dock shut: it could no longer be dragged below whatever the
track count last asked for, and Qt will not shrink a dock merely because the
widget inside it wants less, so deleting a track left the panel at its old
height.

Two things fix it. The first is the distinction between *constraining* a size and
*asking* for one: the scroll area keeps a small floor and no ceiling, so the dock
drags freely both ways, and the height is requested through `resizeDocks` when
the row count changes. `timeline_rows_shown_` is what stops every refresh — there
is one per frame change — from shoving the dock back and undoing a drag the
moment you scrubbed.

The second is **asking for a difference rather than a total**, and it took two
wrong versions to get there. Adding up what wraps the strip — the scroll area's
frame, the horizontal scrollbar, the buttons above it, the dock's own title bar —
forgot a different piece each time, and every miss put the bottom row underneath
something. Then measuring the strip's own height to take a difference from,
which looks like the careful version and is worse: a resizable scroll area
stretches its widget to the viewport, so that height is the space *available* and
never the space *wanted*, and comparing the two answers "no growth needed" every
single time. What is left needs no measurement at all. A row is `kRowHeight` and
the wrapping costs the same whatever the row count is, so the dock is asked to be
exactly that much taller or shorter, and the first call takes the panel's own
sizeHint because it has nothing to take a difference from.

None of it was caught by a test. A harness prints the dock height as tracks are
added, deleted and dragged, and it is the only reason the second version — which
had passed a screenshot — was found to have stopped growing at all.

**A drawing number is now the lowest one free, and that reverses a decision this
file used to defend.** `Track::next_drawing_number` was a stored counter that
only went up, on the grounds that "a number that comes back means two drawings in
one scene answer to it". Reported as a bug and it is one: delete drawing 2, make
another, and it arrives as 3 with no 2 in the track at all, and a reworked scene
climbs into numbers that mean nothing.

The old grounds were about the wrong property. Nothing is keyed on the number —
it is a label on a timeline card and in one tooltip, and the identity is the
`ImageId`, which still is never reused. What has to hold is that two drawings do
not share a number *at one time*, and the lowest-free rule keeps that. What is
given up is that a number names the same drawing for ever, so a note written last
week about "drawing 2" may now point at a different one. That was the trade and
it was made deliberately.

Being derived is the other half of it: the counter is gone from the struct *and*
from `scene.json`, because a stored counter that no longer decides anything is
exactly the thing somebody re-wires by accident later. Undo gets it for free —
putting a drawing back puts its number back in use, and there is a test — where
the counter version would have had to record and restore it.

What did **not** change is that a drawing keeps the number it was born with.
Deriving the label from position instead renumbers a drawing the moment you drag
it, which is precisely when you need to know which one you are holding; that
decision stands.

And **`Timeline` is now `Track`** throughout, including the French
specification. A `Track` is one stack of layers with its own time; the timeline
is the scene's shared time axis and the panel that shows it. A scene has several
tracks and one timeline. If you find the old name anywhere, it meant the track.

## Restacking by dragging

Both stacks are dragged now — layers in the panel, tracks in the timeline — and
the Move up and Move down buttons are gone. The buttons were one click per
position, so a layer of ten going to the bottom was nine of them, and neither
button ever said what it was about to move past. Tracks had no way to be
restacked at all, which meant a background made second stayed in front of the
character for good.

**The two are the same shape and are deliberately not the same code.** A drop
lands on a *boundary* between rows, and both models count a destination in the
list with the dragged row already taken out of it — so `boundary - 1` going down
and `boundary` going up, and the two agree only in one direction. That
subtraction is the whole of the arithmetic and it is the thing that would be
wrong: it is `LayerList::destinationFor`, which is public and static so a test
can reach it, and the same three lines by hand in
`TimelineWidget::mouseReleaseEvent`. Sharing them would have meant a header
for one expression that each side states differently anyway — the timeline's
boundary comes from a y and a row height, the panel's from Qt's drop indicator.

**Qt's drag and drop cannot be driven by a test, and that decided how much of
this is Qt's.** A `QDropEvent` sent to the view, or to its viewport, reaches
neither handler: only the platform's drag manager delivers these, and the
offscreen platform has none. An hour went into finding that out, so it is
written down rather than left to be re-discovered.

What is left is tested from both ends and joined in the middle. The *start* is
ordinary mouse events and those do arrive: a press on a row and a three-pixel
move put the view in `DraggingState`, which is what `dragHasBegunForTesting`
reports and is the moment before the platform takes over — deliberately under
`QApplication::startDragDistance()`, because one pixel further runs `startDrag`,
which blocks in a drag loop offscreen has no way to finish. The *end* is
`LayerList::reordered` called directly, with the document and the panel checked
after it. In between is Qt's, plus `destinationFor`, which is pinned on its own.

The timeline's drag needs none of that: it is built out of ordinary mouse events
throughout, so it is tested end to end.

**The layer list refuses to reorder itself, and that is the point of
`LayerList` rather than an implementation detail.** `QTreeWidget`'s own drop
takes the item out and puts it back somewhere else, which leaves the panel
showing a stack the document has never heard of — until the next rebuild, which
would then silently undo the drag. So `dropEvent` is overridden, sets
`Qt::IgnoreAction` and changes nothing; the document moves the layer and the
list is rebuilt from it. IgnoreAction is load-bearing twice over:
`QAbstractItemView::startDrag` deletes the row it dragged when the drop comes
back `MoveAction`.

Two smaller things about it. The items have `Qt::ItemIsDropEnabled` *cleared*,
because a layer is not a place to put another layer — with it on, Qt offers a
third drop position, "onto this row", drawn as a box round the row, meaning
something a flat stack cannot do. And `LayerList` has no `Q_OBJECT`: it has no
signals of its own, a `std::function` is enough for one caller, and it keeps
`findChild<QTreeWidget*>` — which is how the tests and `shots` reach the panel —
finding it.

**Restacking a track is `Document::moveTrack`, and its undo is two numbers.**
`TrackOrderOp` holds the move that undoes it and flips itself on apply, like
every other op here. Deliberately not a pair of `TrackOp`s and not "swap the
whole track list": both would copy every `Image` record in the scene to record an
edit that touches no drawing at all. Nothing else moves — no cel refcount, no
slot, no image — so a track keeps its own time whatever it is stacked against,
and `restackingATrackMovesNoDrawing` pins that along with the inverse being exact
in both directions.

The handle is the name strip and not the row, which the gutter was already free
to be: there is no frame under it, so nothing is competing for the press. A press
there still selects the track, as it always did, and a drag only starts once the
pointer has moved `kDragThreshold` *vertically* — where a card needs the same
distance horizontally. Neither can start the other, because they begin on
different sides of the gutter.

**And the hand now means two things, which is still one thing.** The timeline's
cursor rule was "a hand means this drawing can be picked up" — see
[what the pointer says](#what-the-pointer-says), where it cost a bug to get
there. It is now "this can be picked up and carried somewhere else", which is
true of a drawing along its track and of a track up its stack, and false
everywhere else in the widget. The gutter was an arrow before and there is a test
that used to say so.

**The word "overwrite" is off the gutter.** It was under the track name in small
text, and it was reported as clutter: the setting is on by default, so it was on
nearly every row nearly all the time, which is how a label stops being read. It
is in the row's tooltip now, along with the name and the fact that the row can be
dragged — asked for rather than read past — and the status bar still says
`(overwrite)` about the track being edited. That reverses what the code used to
argue, which was that it "has to be visible from the row itself"; the argument
was right about the fact mattering and wrong about a permanent label being how
you make somebody notice one.

### Where the layer panel had been scrolling to

Issue #12, and it is worth reading before touching `rebuildLayerList`, because
the mechanism generalises to any panel that changes shape when its selection
does.

Reported as "the layer list waits a few seconds and then slowly scrolls to the
new colour layer". What was measured is the other half of that: **with 60 layers
the new colour layer was not scrolled to at all.** The list settled at scrollbar
value 29 where 44 was needed, immediately, and stayed there.

The cause is that `setCurrentItem` scrolls by itself, and it scrolls against the
panel *as it stands at that moment*. A colour layer goes to the bottom of the
stack, and selecting one makes the Colour layer box appear underneath the list —
which takes about half the list's height with it. So the scroll was worked out
for a list of 32 rows and applied to one of 17. Every ingredient is ordinary and
the combination is not: the row that most needs scrolling to is the one whose
selection shrinks the thing being scrolled.

`showCurrentLayer` is the fix and the interesting line in it is
`layout()->activate()`: it lays the panel out *now*, so the `scrollToItem` after
it is measured against the height the list actually ends up with. Without that
the call is a second guess at the same wrong number.

It is called from the three places that make or move a layer and not from
`rebuildLayerList`, which is deliberate — a rebuild runs on every frame change,
and a list that scrolled back to the current layer each time you scrubbed would
be a worse bug than the one being fixed. `setCurrentItem` is silent when the
current row has not changed, and that silence is what makes scrolling somewhere
to look at another layer survive a scrub.

The multi-second crawl in the report was never reproduced offscreen; the scroll
lands in one step either way now, and a `scrollTo` on a row that is already
visible does nothing, so whatever was nudging it has nothing left to nudge.

## Naming a track or a layer

Double-clicking a name edits it where it is — in the timeline's gutter and in the
layer panel — and Track ▸ Rename track still opens the dialog it always did. The
same rows this happens on are the ones you drag; see
[restacking by dragging](#restacking-by-dragging).

**Neither panel edits itself**, which is the rule `LayerList` already followed for
the drop and it matters here for the same reason: a row showing a name the
document has never heard of is undone by the next rebuild, silently. The editor
reports what was typed, the document is changed, and the panel is rebuilt from
it.

**The editor opens on the name, which is not the row's text.** A colour layer
showing marks made on another drawing has `← ` in front of its name — see
`MainWindow::layerLabel` — so an editor seeded from the row saves the arrow into
the name, and the next rename puts a second arrow in front of the first. It
cannot be fixed by keeping the plain name in `Qt::EditRole` either, because
`QTreeWidgetItem` maps `EditRole` and `DisplayRole` onto the same slot. The
delegate asks the panel through `LayerList::nameOf` instead.

Three smaller things about the layer panel's editor, each of which was wrong
first:

- **The delegate refuses column 1.** A `QTreeWidgetItem`'s flags belong to the
  item and not to the column, so `ItemIsEditable` makes the Marks column editable
  too — and that column holds a tick and no text at all. `createEditor` returns
  null there.
- **`editTriggers` is `DoubleClicked` and nothing else.** Qt's default set
  includes `SelectedClicked`, which starts an edit on a plain click on the row
  you are already on, and the row you are already on is the layer you are drawing
  on.
- **That an edit has begun is reported from the delegate's `createEditor`.**
  Something has to know, because the keyboard shortcuts are turned off while a
  name is being typed into — and the obvious place, `QAbstractItemView::edit`,
  does not mean what its name suggests: it returns true when the *delegate* has
  merely consumed the event, so ticking a visibility box counted as starting a
  rename and left the keyboard switched off with no editor to show for it. An
  editor being made is the honest moment; `closeEditor` is the other end, and is
  Qt's own, so a rename closed by anything at all says so.

**An empty name is refused and the old one kept**, in both panels and in the
dialog. A nameless track has no label on its row, and a nameless layer has no
folder of its own in an export.

**And a long one is stopped at sixty characters, by the field**, in all three
places — which is why Track ▸ Rename track builds its own `QInputDialog` instead
of calling `QInputDialog::getText`: the static helper offers no way to reach its
line edit, and a dialog that accepts a name the row beside it would refuse is two
rules for one thing. Sixty is far past anything a hundred-pixel gutter can show,
so it is a cap nobody meets, which is the only kind worth enforcing by refusing
the keystroke rather than explaining afterwards. Both numbers and the arithmetic
tying them together are in `name_limits.h`; the reason there is a *second* number
is in [the traps](#the-traps).

**What the keyboard does while a name is being typed into** is
[its own question](#what-the-keyboard-does-and-when), and it had a real bug in
it: Return is Play. **How a pen opens one of these** is
[a trap](#the-traps) rather than a feature — Qt does not turn two taps into a
double click.

**What a name is allowed to be: anything.** Nothing anybody types can hurt a
project — cel files are ids, and a name lives in `scene.json` as a JSON string
that `QJsonDocument` escapes. The whole question is what the *export* makes of
it, where a name becomes a folder name; that is under "the underscore in an
exported name means one thing" in [where it got to](#where-it-got-to), and the
short version is that accents survive, two different names can be one folder,
and the export refuses rather than quietly losing a layer.

**There are `shots` for both editors.** The layer one is worth having looked at
before believing anything about it: Qt's item editor is frameless and white on a
white list, so the field is invisible and the *selected name* is the only thing
saying one is open. That was checked before concluding the field was too narrow —
measuring said 234 px, which is the whole row.

### What a test of a rename can reach

Both gestures are driven end to end from the press — `MouseButtonPress`,
`Release`, `DblClick` for a mouse, and two timed presses with no double click
between them for a pen — because that is what a hand does and both widgets are
hit tested by hand.

What is driven at the seam is the *commit* of the layer editor. Qt answers
`Return` in an item view by posting a queued call inside the delegate, and a
synthetic key event never gets that call delivered offscreen. `Escape` is emitted
straight out of the same event filter and does work, which is what proved the
filter was installed at all and that the trouble was the hop rather than the
wiring. `finishRenameForTesting` calls the pair that hop would have made. The
timeline's editor needs none of it: an ordinary `QLineEdit` with an ordinary
`editingFinished`.

Neither seam is why Return failed in the real program. That was the `Play`
shortcut, and no test here would have caught it.

## Colour through time

**Part 1 of [scribbles-through-time.md](scribbles-through-time.md) is built, and
part 2 as far as its second rung** — see below. An
absent cel on a CTG layer used to mean the layer was empty there; it now means
*inherited*. Colour the first drawing of a run and the run is coloured, and a
drawing you scribble on takes over from there. It is resolved at read time by
walking back over distinct drawings — `Track::celSourceFor`, through
`Document::ctgScribblesAt`, which is the only place the "absence means inherited"
policy lives — so reordering and deleting change who inherits from whom for free
and touch no cel. The first mark on an inheriting drawing copies what it was
showing and edits the copy, which is why one stroke does not wipe the colour off
the drawing you were adding to.

It is a per-layer choice, not a fact: carrying can be switched off, and it can
run forwards, backwards, or to whichever coloured drawing is nearer. Forwards is
how a shot gets coloured; backwards is for colouring the drawing in front of you
— often the last of a run, because it is the one you were working on — and both
ways is what fills the gaps between drawings you have already coloured.

**A mark now wins the pixels it covers, whatever the solver decided.** The
solver's job is the pixels nobody said anything about, so a scribble is a manual
touch-up for what the min-cut missed, at no cost to the solver. It has to be over
the fill rather than under it, and a transparent scribble is what settles that:
under the fill, "no colour here" would be the one thing hidden. The marks are
self-effacing — a scribble carries the colour of the label it produces — so
wherever the fill agreed the override paints what was already there and you see
only the disagreement.

**Transparency is a label.** It cannot be alpha zero, because alpha zero already
means nothing was scribbled at all, so it is stored as a pixel that is
unmistakably present and unmistakably not a colour: opaque, with negative light
in every channel. A premultiplied colour lives in [0,1], so nothing can produce
it by accident and no colour is taken from the user to pay for the encoding; half
stores −1 exactly, so the file format learns nothing. It is a "None" position on
a two-way switch beside the colour swatch, offered only on a colour layer. The
alternative was a palette on the layer with the cel storing indices — the cleaner
model, and where per-colour settings will eventually have to go, but it changes
what a scribble cel holds and a mistake in it is a migration rather than a
recompute.

**A mark is written hard now.** Three places said a CTG stroke was a label and
not paint and the brush blended anyway, so the two only agreed in the middle of a
stroke; at its rim, and everywhere one colour crossed another, it left pixels
quantising to a third colour that competed for regions on its own account. See
`BrushSettings::label`. Two strokes crossing leave two labels, every alpha on the
cel is exactly 0 or 1, and erasing writes exact zeros.

**There was a flag on every drawing, and it had to come out.** It is worth
reading before building another one, because everything about it was right
except the part that mattered. See "the flag that had to come out" below.

Of the colour issues, #3, #6 and #7 are done and #8 is half done — the
transparent colour exists, the non-modal colour panel is deliberately parked. #6
was built the opposite way round from how it was written, for the transparency
reason above.

## Colour through time, part two

**A carried mark now moves to where the drawing went.** One translation for the
whole drawing, found by matching the two drawings' ink coarse-to-fine
(`estimateCtgShift`), applied wherever a mark is read on a drawing it was not
made on. It is on by default and it costs 19.7 ms against the 129 ms coarse
solve it precedes.

It is derived and never stored, which is a deliberate departure from the design
note's six-floats-per-drawing. The job already carries both drawings' line art,
so the shift is worked out inside the solve and thrown away with the fill; a
stored transform would be a second piece of derived data to keep in step with
drawings that move. Whole pixels, not an affine: a mark needs most of its pixels
in the right region and nothing finer.

**The reason it is the default is measured, in `bench_carry`.** Left where it
was drawn, a carried mark holds its region only while the drawing has moved less
than about half that region's width — that is the soft-scribble majority rule
applied to a mark that has not moved — and half a region's width between two
drawings is very ordinary. Past it, one of two things happens, and no quantity
read off one drawing can tell them apart:

- With a mark in the next region too, the two contest the overlap, somebody
  loses, and `spread` collapses to about 1 — at exactly the displacement where
  it goes wrong.
- With **nothing** in the next region, the carried mark simply takes it —
  uncontested majority is a formality — and `spread` *rises*, 6.3 to 12.8,
  because the mark did fill a region and the number cannot ask which one. That
  is the design note's "wrong region of about the right size", and it is why a
  flag on this number was worse than none. See "the flag that had to come out".

Moving the marks removes both: 400 px of movement, `spread` unchanged at 7.70,
and the neighbouring region goes from 100% wrong to none. **Where it fails is by
locking on** — matching ink is ambiguous when ink repeats, and a box with a wall
down the middle moved 200 px matched its far wall to the divider. The fill is
then exactly as wrong as carrying unchanged, which is the floor this cannot go
below.

**Two things about the score, both learned from one reported project.** It is
what a translation estimator lives or dies by, and the first version of it was
wrong in a way that only shows on real drawings.

*Score agreement, never difference.* A drawing is nearly all bare paper, so a
wrong alignment is charged twice — for the ink it puts where there is none, and
for the ink it fails to cover — while sliding the whole drawing off the edge is
charged once, for the ink left behind. Two circles a person drew in the same
place never coincide exactly, so "disappear entirely" scored better than "line
them up" and the search answered with the corner of its own window: 480 px of
movement for a circle that had not moved. Agreement cannot do that, because ink
pushed off the edge agrees with nothing. It is also the right question: what a
translation is for is putting ink on ink.

*Match a shape, not a line.* Line art is thin, and two thin lines either
coincide or they do not, so agreement between them is all or nothing and its
maximum can be nowhere near the truth. Two rings of different radius do not
overlap at all when concentric and overlap most when slid until they touch — so
the sharp-ink answer to "the same circle, drawn twice, a little smaller" is "it
moved by the difference of the radii". A three-tap blur at every level of the
pyramid is what stops that, and it is load-bearing rather than tidy: without it
a 400 px movement is found as 99 px and the fill is lost.

What is left is honest and is the limit of one translation. On that project the
circles really had drifted 42–65 px upwards and shrunk by a fifth, and the
estimate came back 72–102 px: the right direction, overstated, because a
translation is being asked to explain a change of size. A confidence gate was
measured and does not separate — a real 20–40 px translation of a box scores
×1.02 to ×1.11 against not moving, and those circles scored ×1.09 to ×1.25, so
any threshold that blocks the second blocks the first. Getting past this needs
rung three, not a better constant.

**Three things have to agree about where a mark ended up, and at first only one
of them did.** Reported as a bug and it was three: the fill followed the drawing
while the Marks column drew the mark where it was made, the first stroke on a
carrying drawing copied the marks unmoved and so undid the fill you could see,
and a shift larger than half the area went unfound. The last one is the
interesting one — the search window was half the grid, so a shape that had moved
most of its own width sat outside it and the search reported the best wrong
answer with nothing to say it had been looking in the wrong place. The window is
the whole grid now, which at these sizes costs nothing.

The other two are the same lesson from different directions: **a derived value
that changes what is drawn has to be reachable by everything that draws it.**
The shift lives in `Document::ctgShiftAt`, written by every solve, read by the
compositor's marks pass and by `celForWriting`. Any fourth thing that shows
marks must read it too.

## What a track does past its last drawing

Issue #20. Tracks share one timeline and are not obliged to be the same length,
so this is an ordinary question and not an edge case: a background drawn once
under a character animated over forty frames, or a four-drawing cycle for a walk.
`TrackEnd` is per track — show nothing, hold the last drawing, or cycle — and
Nothing is the default, which is what happened before there was a choice.

**The whole thing turns on one distinction: what a track *holds* against what it
*shows*.** `Track::imageAtSlot` is the contents — the slot's own drawing, kNoId
past the end. `Track::imageShownAt` is the picture, and past the end it is
whatever `end` says. They agree everywhere inside the track and differ only out
past it, and every question in the program is one or the other:

| holds (`imageAtSlot`) | shows (`imageShownAt`) |
|---|---|
| which drawing the brush edits | what the canvas composites |
| a layer's own export sequence | the flattened composite |
| | which CTG fills are worth solving |

**Only the flattened composite gets the end behaviour, and a layer's own sequence
stops where its track stops.** That was the user's call and the argument for it
is better than the one against: a layer sequence is a sequence of *that layer's
drawings*, so padding a two-frame background out to forty is twenty times the
bytes for no information, and downstream you import the still rather than the
sequence — backgrounds do not move. It has a consequence worth stating plainly
because nothing in the export announces it: **layer folders can be different
lengths.** `character_ink/` holds ten frames and `background_ink/` holds two, and
anyone dropping both onto a compositor timeline has to know the second is a still
and not a sequence that ran out.

That change reaches further than cycling. Per-layer sequences used to be padded
with *blank* frames out to the scene length — `fileCount` was `sequences ×
frames` — so any project with unequal track lengths now exports shorter folders
than it did, whether or not any track holds or cycles.

**It never makes the shot longer.** The timeline is as long as the longest track,
so a cycle fills frames that already exist. One consequence to know: a cycling
track that *is* the longest cycles over nothing at all, so a four-drawing walk in
a four-frame scene does nothing until something else is longer. The missing piece
is an explicit scene length, which does not exist yet.

**You can see a held or cycled drawing and not draw on it.** Reported as a
question — "why is the user allowed to draw past the track end? There are no cels
there" — and it was right. The first version let a stroke out there edit the
underlying drawing, on the grounds that a repeat is the same image showing again
exactly as a hold is. But the end behaviour had already been scoped to the
picture, and editing follows the contents; extending it to the brush contradicted
the distinction the whole feature is built on. Past the end there is no slot and
no cel, so there is nothing to edit, and the status bar says so — a brush that
silently does nothing is otherwise a bug.

**The timeline row stops at the last drawing, and that was the second attempt.**
The first drew the extended frames as faint dotted cells, so that the row would
not stop at four while the canvas kept drawing out to forty. It reads worse than
an empty row: a cell is a frame you can put a drawing on, so cells you cannot
click are a harder thing to explain than the absence of any. What a track does
out there wants saying once rather than repeated along the row, and how is
[issue #22](https://github.com/S-poony/Animage/issues/22) — still open, and the
shape of it is not decided. Whatever it turns out to be, it must not be a widget
placed on the row: see "a widget on a list row disables that row's own tick".

**The status bar says it for the track being edited**, which answers the half of
#22 that is about the current track and none of the half that is about seeing
all of them at once. It reads `track 1 (overwrite, hold past the last drawing)`.

Three decisions in that one phrase, all of them reported as wanting to be
different from how they were first built:

- **The whole sentence and not the bare word.** `hold` alone would sit four words
  from `held 5`, which is the current drawing's exposure — two unrelated meanings
  of one root on one line — and the word means nothing without what it is about.
- **On every track at every frame, including the default `nothing`.** Tracks are
  not the same length, so which of the three a track is doing decides what the
  shot looks like wherever that track runs out, and there is nowhere else it is
  written down. `overwrite` keeps the opposite rule — shown only when true —
  because it is on by default and is not expected to be changed, which is the
  same argument that took the word off the timeline gutter.
- **One word each in the menu.** `Past the last drawing ▸ Hold`, where the items
  used to repeat the submenu's own title back at it — "Hold the last drawing"
  under "Past the last drawing" is the same words twice and reads as though the
  two halves might mean different things.

**And the three words exist twice on purpose.** `project_io::endName` returns
`nothing`/`hold`/`cycle` too, and those three are the *file format*. Sharing one
function would mean that improving the wording here silently changed what
`scene.json` means. The format's three are pinned by the round-trip tests; the
interface's three are pinned by nothing but taste, and that is the point.

**A shot can now be told how long it is, and the length is a cap rather than a
floor.** That is the second design; the first one is worth recording because the
difference is instructive.

`Scene::fixed_length` and `Scene::length`, under Edit ▸ Scene settings as a
checkbox and a number, with the duration in seconds under it — frames are what an
exposure sheet counts in and seconds are what a brief is written in, and the
framerate is the only thing connecting them. Off by default, which is what
happened before it could be said. Two fields rather than a zero sentinel, because
"derived" and "sixty" are different kinds of answer.

**The first version made it a floor** — the shot was the larger of the length and
the longest track — on the grounds that a shot shorter than its own contents
would strand drawings past the end of the timeline where nothing could reach
them. That reasoning was sound and the conclusion was wrong: it made the setting
unable to do the one thing it is for, which is to say a shot is sixty frames when
a track runs to eighty. The answer is not to forbid the case but to keep the
drawings reachable, so:

| | |
|---|---|
| `Scene::shotFrames()` | the shot. What plays, and what is exported — the composite *and* every layer's own sequence. |
| `Scene::timelineFrames()` | everything reachable: the shot, or a track that runs past it. What the timeline draws and what scrubbing, stepping and the canvas clamp to. |

They differ only past a fixed boundary, and keeping them apart is the whole of
how a cap works — a `max()` hidden inside one function could not tell a caller
that plays from a caller that draws. Frames out there are washed over in the
timeline, the status bar says "outside the shot", and they are still editable.
Cutting a shot short must not mean destroying what is beyond the cut.

The boundary is drawn as a line down the panel with a grip in the ruler, and
dragging it fixes the length — you are saying where the shot ends, which is what
the setting means. The grip is in the ruler and nowhere else on purpose: the
ruler is the scene's own time, and it keeps the handle clear of the run edges in
the rows, which belong to a track and are sometimes at the same x.

**Nothing a track does may move it.** The scene sits above the tracks, so adding
a drawing lengthens the track and never the shot. A setting that edits itself
when you draw is not a setting.

A fixed length is also what makes a cycle worth having: without one, a
four-drawing walk is the longest track and cycles over nothing at all.

The solve counter in the export had to change with it. It used to skip repeats of
the drawing before, which is exact when each drawing is visited once and
contiguously — a cycling track comes back to the same four drawings every four
frames, so it now counts distinct drawings. Left alone it would have promised ten
times the max-flows it then ran, and the progress bar would have sprinted to the
end and stopped.

**Still open:** whether the end behaviour should also decide how playback repeats
— looped against a single pass — which was raised and deliberately not settled.

## What the keyboard does, and when

Phase 0 of [lasso-and-transform.md](lasso-and-transform.md), and the cheap half
of [#14](https://github.com/S-poony/Animage/issues/14). Every shortcut was a
`QKeySequence` literal at one of fifteen call sites in `buildActions`, all of
them `ApplicationShortcut` — which means they fire regardless of what the canvas
thinks it is doing. That is fine while the program has no modes and stops being
fine the moment it has one.

`shortcuts.h` is one table: id, label, default key, and **which modes the action
is live in**. `buildActions` reads it, `MainWindow::setShortcutMode` is the only
thing that ever calls `setEnabled` on one of them, and that is the whole of the
mechanism — a disabled `QAction` does not consume its shortcut, so turning Play
off is what frees Return for a transform to validate with and turning the frame
steps off is what frees the arrows to nudge with. Modality written as
`setEnabled` calls spread through the code that changes mode is how an action
ends up stuck disabled after some cancel path nobody tested.

**There is a third mode, `Typing`, and it exists for the same collision.** Return
is Play, and Return is also how a rename is finished, so pressing Enter to accept
a new layer name started playback instead. Reported. The answer was already
built: turning Play off is what frees the key, exactly as it does for a
transform.

It has no flag of its own in the table — `liveIn` answers false for every row in
it — because the answer for every row *is* the same one, and a bit per row would
be a decision that is not a decision. Two things about it are less obvious:

- **Coming out of it, the mode is asked for rather than assumed.** A transform
  can be live while a layer is renamed, and going back to `Normal` there would
  take the nudge keys away.
- **It is a count and not a flag.** Two editors overlap for a moment: opening a
  rename in the layer panel is what takes the focus off an open one in the
  timeline, so the second is created *before* the first is told it has finished,
  and a flag would hand the keyboard back with an editor still on screen.

**And the filter that forwards Space and Z was eating every space anybody
typed.** It has been wrong since it was written, and nothing noticed while the
only places to type were dialogs nobody put a space into. Renaming in place is
typing into the main window, and a track renamed "rough pass" arrived called
"roughpass". It asks `isTypingInto(QApplication::focusWidget())` now — the focus
widget and not `watched`, because a key goes to whatever holds the keyboard and
this filter sees it again at every parent it propagates to. It was already wrong
for the Rename track dialog and for every spin box on the transform bar.

Neither of those is reachable by a test here, and that is worth saying plainly: a
synthetic key event does not go through Qt's shortcut map, so no assertion in
`test_canvas` can see Play swallow a Return. What is pinned is one step in — that
`Play` is disabled while an editor is open and enabled again after every way of
closing one, and that a space reaches a focused field while still panning when
the canvas has the keyboard.

**And now the other half of #14: the keys can be changed.** `Bindings` is what
the keyboard is bound to *now* — the table's defaults with the user's changes
over the top — and `shortcuts::current()` is the one the program is running on.
`Edit > Keyboard shortcuts` edits a copy of it and installs it on Apply.

Four things about that are decisions rather than mechanics.

**Nothing changes until Apply, and Apply is the refusal.** The obvious version
rejects the chord as you type it, which can say "no" and cannot say *what you
have hit* — and the collision this exists to prevent is one nobody sees coming.
So the chord goes in, both rows go red, a sentence underneath names them, and
Apply stays grey until one of them moves. The user asked for it this way and it
is better than what was planned.

**Only what differs from a default is written down**, to `shortcuts.json` in the
platform's config directory, keyed by a stored name. That is what lets a default
improved in a later version reach everybody who never touched that action. It is
also why `Entry::name` must never be edited: it is what a rebinding is keyed by,
so changing one does not rename a binding, it silently discards it. `Id` stays
internal and can be renamed freely.

**The two collision rules moved out of the test and into the program.** A rule
that only a test knows can say the defaults are fine and nothing else, and the
dialog has to refuse on exactly the rules the table is pinned by. `test_shortcuts`
now asserts both that the defaults obey them and that the rules would notice if
they did not — the second half being the one that stops a rule that has quietly
stopped working from looking green forever.

**The keys a live transform borrows are rows now**, and the canvas asks
`activates(Id::TransformApply, event)` rather than naming `Qt::Key_Return`. This
is the part that would have been easy to skip and is load-bearing: those keys
belong to actions that have been *disabled*, so no QAction anywhere holds them,
and without a row for each, a rebinding could put Fit drawing on Left and take
the nudge away with nothing in the program able to notice. Shift for ten pixels
is read as "the binding, with a Shift it has not got", which is one rule instead
of four more rows.

`Space`, `Z` and `Alt` are rows too, and cannot be rebound: they are held for as
long as a click or a drag lasts, which `QAction` cannot express. Two reasons, and
the first one alone got the third of them left out of the first build. `Space`
and `Z` *consume* their key, and an application-wide shortcut wins it before the
canvas ever sees the press, so an action rebound onto `Space` would take panning
away silently — that is a conflict, and the table has to know about it. `Alt`
consumes nothing (the canvas watches it without accepting it, because it is the
menu bar's own key on Windows) and so can never be in a collision at all. It is
listed because the panel is where somebody goes to find out what the keyboard
does, and an answer with the eyedropper missing from it is the wrong answer.
Reported, and the correction is the point: "which keys could collide" and "which
keys are there" are two different questions, and the panel answers the second.

**Every tooltip that names a key is composed rather than typed.** They used to
end in literals — `(Ctrl+D)`, `(Enter)`, `(Delete)` — which were already only as
true as the last person to move a binding and are false the first time anybody
rebinds anything. `MainWindow::keyedTip` registers the sentence and the id, and
`syncTooltips` fills the key in and does it again after every Apply. A sentence
that names *other* keys says `%1` and lists them, for the same reason. Two tools
had no tooltip at all and now say what they are and what key they are on.

**The bug in #14 is not a duplicate binding and no table would have caught it.**
`Fit canvas` was `0` and `Fit drawing` was `Shift+0` — two genuinely different
sequences. They are one physical chord on a keyboard whose digit row is the
shifted face of another row, which on AZERTY it is: producing `0` at all means
holding Shift, so both bindings match, Qt calls the shortcut ambiguous, and it
*cycles between the candidates* rather than complaining. That is why it presents
as "sometimes the wrong thing happens" instead of as an error.

Fit drawing is `F` now. No digit would have been safe, so the fix is to leave the
digit alone rather than to pick another one, and `test_shortcuts` pins both
rules: within one mode no two actions share a key, and no two bindings differ
only by Shift on a key that is not a letter. The second one was checked by
putting `Shift+0` back and watching it go red.

Two things that rule has to be careful about, both found by writing it. Letters
are exempt and must be — the unshifted face of a letter key is that letter on
every layout, so `B` and `Shift+B` are two chords a hand can tell apart. And so
is everything outside printable ASCII: Qt lists the dedicated `Key_Save` among
the standard Save bindings and `Shift+Key_Save` among Save As's, which is a real
pair on a keyboard that has such a key and no chord at all on one that has not.

`[` and `]` were the other thing recorded here and are answered rather than
fixed: on AZERTY both need AltGr, which Windows reports as Ctrl+Alt. They work
and they are awkward, and being awkward is a rebinding question — so they are
named rows in the panel now (they used to have no label, being in no menu) and
they are the first two anybody will want to move.

**What a picture caught, and a green build did not.** The first build of the
panel had a Reset button on all thirty-odd rows, because they were made and then
hidden: a widget handed to a tree is one the view manages, and it shows it again
on the next relayout. They are made and unmade now, so a Reset exists only beside
a row that has something to undo. `shots` has the panel as it opens and the panel
with `Shift+0` typed back into Fit drawing, which is the collision this issue was
raised for, shown being refused — and a third of the search reaching into a
folded group, which is the half of the search that can break silently.

**One trap for anyone driving `QKeySequenceEdit` with synthetic events**, which
is the same shape as the one `Stage` records about held modifiers. It *clears the
field on the key press* — emitting `keySequenceChanged` with an empty sequence —
and settles only on the release. So a test that sends a press and reads the
binding is reading a field mid-edit and will report that the row was unbound. It
also means an edit abandoned after a bare modifier leaves the row unbound, which
is Qt's behaviour rather than ours: the field is empty and so is the binding, and
Reset is beside it.

## A straight line

Shift held when the pen lands makes the stroke a line from there to wherever it
lifts, at whatever angle. **Not snapped to the horizontal, the vertical or a
diagonal**, which was the ask and is the whole of it: a drawing has edges at
every angle, and a constraint that knows three of them is one you work around.

The interesting part is not the geometry, which is one call to `Brush::extend`.
It is that **a straight line writes nothing until the pen lifts**, and everything
else about the feature falls out of that.

The reason it has to is mechanical. The far end moves for as long as the gesture
lasts, so anything stamped before it settles would have to be taken back off —
and the brush has no way to lift a dab off a tile. It only lays them down. So
`beginStroke` opens the command and records the anchor and does *not* call
`brush_.begin`; `extendStroke` moves the far end and returns; `endStroke` is
where `begin` and one `extend` finally run, over the segment, in that order. The
command is open across all of it, so it is one undo step exactly as a free stroke
is.

Four things follow, and three of them are the answers to questions that would
otherwise have needed deciding one at a time:

- **Shift is read at the press and the gesture keeps that answer.** Not a key the
  stroke keeps consulting. Taking the constraint up half way would mean
  unstamping the squiggle already on the drawing, and letting go of it half way
  would mean a mark whose first half was never drawn. Neither is available, so
  the latch is not a simplification — it is the only thing the model can mean.
- **A frame change under a live line carries the whole line to the new drawing.**
  There is no brush to rebind, so `rebindStrokeToCurrentImage` returns early and
  the mark lands on whichever drawing is current when the pen lifts. That is
  deliberately *not* what a free stroke does — a free stroke leaves a piece of
  itself on every frame it passed over, which is how you sketch a moving point,
  and a line sliced into as many pieces as playback showed frames is nobody's
  gesture. It is also why deferring `brush_.begin` mattered rather than merely
  being tidy: stamping the anchor dab at the press would have left a dot on the
  drawing the line was aimed from, silently.
- **The far end's pressure is the last move's and not the release's.** A pen
  lifting reports pressure 0, so a line stamped from the release is a hairline
  that fades to nothing. Both ends are stored for exactly this.
- And the whole of what Shift changes is **where the dabs go**. Same spacing,
  same pressure interpolation, same everything: the mark a line leaves is bit for
  bit the mark those two endpoints leave freehand, and there is a test that says
  so by drawing both and comparing every pixel. It runs on the *pen* at two
  different pressures, because the mouse path reports 1.0 throughout and would
  have agreed with a wrong far-end pressure by accident — which is the defect
  above, and it was put back to watch the test go red.

**The band is a thin centre line and not the brush's width**, which was a choice
and not an omission. What is being aimed is an axis, and the width is not part of
what the gesture is deciding: it is the tool's, it is the same before and after,
and it is exactly as unannounced here as it is for a free stroke — the brush
cursor is a crosshair and you learn the radius by drawing. A filled band would
also be worst exactly where it is needed most, since a brush here goes to 400
pixels across and something that wide covers the drawing you are lining the mark
up against. It is light under dark like every other overlay here, and solid
rather than the lasso's dashes, which mean a selection boundary.

If that turns out to be wrong it will be reported as "the line landed fatter than
I expected", and the answer is a radius on the cursor for *every* stroke rather
than a band on this one — the complaint would not be about the constraint.

It is drawn on the same take-it-off-and-put-it-on-again plan as the tool ring
(`updateLinePreview` against `updateToolRing`) for the same reason: the far end
moves on every pen move, and repainting the whole widget for each one would
recomposite the viewport at the rate the hand is travelling. Both ends are held
in image coordinates, so a zoom mid-gesture moves the band with the drawing.

Unlike almost everything in [what the pointer says](#what-the-pointer-says),
**this one a picture can catch** — the canvas draws it, so `grab()` renders it,
where a cursor is never in the frame. `shots` has
`a-straight-line-being-aimed`: the band mid-gesture, over a circle rather than
over bare paper, with the detour the hand made leaving no trace.

**One thing deliberately left alone.** The pointer says nothing about Shift being
held — `pointingAt` answers from the held keys and this is not one of the keys it
asks about. Doing it properly means a drawn cursor, since Qt has no glyph for it,
and the two things that already say so are the band once the pen is down and the
shortcut panel before it. Worth revisiting if it is reported; not worth inventing
a glyph for speculatively.

Shift is a `Kind::Held` row in the shortcut table, `kNormal`, listed for Alt's
reason rather than Space's: it consumes nothing, so it can be in no collision at
all, and the panel is where somebody goes to find out what the keyboard does. It
is `kNormal` and not `kAlways` because during a transform the same key means the
fifteen-degree constraint — one key, two modes, two meanings, and the row is
about the one the brush is under.

## Moving a drawing

Phase 1 of [lasso-and-transform.md](lasso-and-transform.md): the Transform tool,
with no selection yet, so it takes the whole cel of the active layer. A live box
with handles, five numeric fields, Apply and Cancel, and the arrows to nudge with.

**Entering the tool is what starts it.** There is no "transform selection"
button, because the tool is the button — which is what makes "press it with
nothing selected and it boxes the whole drawing" fall out of the design instead
of being a special case in it.

**The document is not written until it is committed.** The obvious version
writes the hole immediately and puts the pixels back if you cancel, which means
Escape has to unwind a command and there is an undo entry for something that did
not happen. Instead the layer is left out of the composite —
`compositeScene` takes a `lifted` layer id — and drawn on top through a
`QTransform`, so the source region looks empty because it is not being drawn,
not because anything was erased. `Cel::replaceTiles` is what commits, journalling
both sides so undo puts the drawing back exactly; an identity writes nothing at
all, because picking a drawing up and putting it back is not an edit.

**Whole-pixel translation is a branch and not a lucky case of the general
path.** `Transform::isWholePixelTranslation` is asserted directly by a test,
because bit equality alone would not say which path ran — bilinear at an integer
offset lands one weight on one sample and is exact too. A drag rounds `dx` and
`dy` to whole pixels for the same reason: half a pixel of translation cannot be
aimed at and only resamples. The axis mirror
([#24](https://github.com/S-poony/Animage/issues/24)) is that same branch with a
sign, built beside it as `isAxisMirror` and never as a −1 scale through the
resampler.

**A reduction needs a filter as wide as the reduction, and it is load-bearing.**
A fixed four-neighbour read at a four-times reduction drops line art entirely —
measured by forcing that path and watching a one-pixel line vanish rather than
merely thin. What it argues for is a kernel scaled to the transform, which is not
the same claim as "box-filter when reducing"; the second is what was built, and
[what a commit does to a line](#what-a-commit-does-to-a-line) is what that cost.

**The box is the ink's bounds and not the tiles'.** `paintedBounds` reads every
pixel of every occupied tile; `drawnBounds` stops at the tile. Which one you want
is not a matter of taste — a solve region is choosing a resolution and wants the
cheap one, and a rectangle drawn on screen 128 pixels bigger than the drawing on
every side is a picture of the tile grid, which is an implementation detail
nobody asked to see. The first screenshot of this had the tile-aligned box in it
and nothing else would have caught it.

**The pivot belongs to the gesture and is put back between gestures.** Dragging
the top-left handle scales about the bottom-right corner, so the pivot moves
constantly and `repivot` absorbs the difference into the translation — otherwise
the drawing jumps the moment you touch a handle. At the end of every drag it goes
back to the middle of what was picked up, which is what keeps "rotation 30"
meaning thirty degrees about the middle whatever the last drag did.

**The bar floats over the canvas rather than taking a row above it.** It was a
`QToolBar` on a row of its own, and a toolbar is in the window's layout — so the
canvas lost the bar's height on the way into a transform and got it back on the
way out, which moved the drawing on screen at exactly the two moments you are
looking at where it has landed. It is a `QFrame` child of the canvas now,
centred along the top. What that costs is that nothing lays it out: it is placed
by hand when it appears and again from the canvas's resize, which the window's
event filter forwards. Reported after the first build of it.

**And that broke the pen on it, which is a trap worth writing down.** A
`QSpinBox` has no `tabletEvent`, so it ignores the pen and Qt propagates the
event to the parent — which, now that the bar is a child of the canvas, is the
canvas, and `tabletEvent` there accepted everything. Two things went wrong at
once: the press started a transform drag on the drawing underneath, and **Qt only
synthesises a mouse event for a tablet event that nobody accepted**, so the
buttons were not clickable with a pen at all. `childAt` on the event position is
the guard. Every other panel in the window is a sibling of the canvas rather than
a child, which is why none of them ever needed it — putting any control *on* the
canvas needs it again.

**The box has a rotation knob**, on a stem out from the middle of the top edge,
away from the centre so it stays outside the box however far the box has turned.
Round, where the eight that resize are square: two shapes for two operations, and
it is the only thing on the box saying that one of them turns the drawing rather
than stretching it. Rotation used to be available only from an invisible band
just outside a corner — the band stays, because it is where a hand reaches
without being told, but a gesture nobody can see is a gesture nobody uses, and
the numeric field was the only discoverable way to turn a drawing. Reported.

**A drag that starts anywhere moves it**, and not only one that starts inside the
box. `boxTargetAt` used to fall through to "nothing at all", so the drawing could
be picked up only from inside the rectangle drawn round it — which is exactly
where you cannot press when the thing being moved is a thin line, or when the
middle of the box is the drawing underneath that you are registering against.
Reported, and nothing was given up by it: the knob, the eight handles and the
rotate band round each corner are all tested before the fallback, so a corner
still scales and just outside one still rotates. The consequence is that a
transform now claims the whole canvas, which is what
[the pointer says](#what-the-pointer-says).

**Two scale fields, not one.** The note says "dx / dy / rotation / scale" and
also that edge handles scale one axis; those cannot both be true. Two fields was
the user's call. Letting the handle decide is what frees Shift to constrain
rotation to fifteen-degree steps and a move to an axis, which is worth more than
a modifier that switches between one axis and two.

**Scale is clamped positive rather than allowed through zero.** Dragging a handle
past its anchor squashes to nothing instead of flipping, because a flip has to be
the exact path and not a bilinear resample at −1, which carries a half-pixel
phase error and gives a blurred mirror that nothing complains about.

**And that is what the two Flip buttons are** — issue #24, and the reason the
clamp stays. `flip_x` and `flip_y` are two more numbers on the transform rather
than an operation on the drawing: pressing one twice is exactly where you
started, nothing is written until Apply like everything else on that bar, and the
buttons are checkable because what they show is which way round the drawing
currently is.

The sign is a `bool` and not a negative scale, which is a decision. The scale
stays a magnitude because that is what the bar's two per-cent fields show, and
"−100%" is not a number anybody types into one. `matrixOf` multiplies the sign in
and **that is the only place in the arithmetic that knows** — the resampler, the
bounds and the pivot all read the matrix, and the kernel already took the scale's
magnitude.

**`isAxisMirror` is the second exact path**, beside `isWholePixelTranslation`,
and `mirrorTiles` is `translated` with a sign: a permutation of pixels, copied as
raw halves rather than through `pixel`/`setPixel`, which would be a half → float →
half round trip per pixel on the one path whose whole claim is that it does not
touch the numbers. It carries a clause the translation does not need — twice the
pivot has to be a whole number, because a mirror maps a destination pixel centre
to `2·pivot + d − centre` and an axis a quarter of a pixel off maps centres into
gaps. The interface cannot produce one (the pivot is the middle of a whole-pixel
rectangle), and this is `core`, where "cannot happen" is worth being wrong about
cheaply.

`bench_transform` prints it beside the nudge, which is where the claim belongs:
59 ms against 43 at 4K, 17 against 12 at HD. The gap is that a mirrored run is
reversed, so it copies four halves at a time where `translated` copies a whole
row — the same order of cost, paid once on Apply, and nothing like the 200 ms a
rotation of the same drawing costs.

**Two things this broke, and both are pinned.** Leaving the flip out of
`isWholePixelTranslation` is the one that would have shipped: a mirror answers
yes to every other clause in it, so the commit takes the translation branch and
hands back a drawing nobody has flipped, silently. And the handle drag measures
against an arm that has to carry the sign — a mirrored box has its top-left
handle over on the right, so an unsigned arm asks for a negative factor and the
clamp that keeps scale positive turns that into the box collapsing to one per
cent the moment you touch it after flipping. Both were checked by putting the
defect back and watching the test go red.

**Leaving commits, and that was a decision.** Changing frame, changing layer,
changing track, picking another tool and leaving the document all bake it;
Escape and Ctrl+Z cancel it; the Clear button cancels it, because emptying the
layer throws those pixels away and baking them first would be a resample spent on
nothing. The note only settled the frame case — reaching for the brush meaning
"I have finished placing it" was the user's call, and it makes the tools a way
out as well as Apply.

**The preview and the commit do not agree exactly, on purpose.** The float is
one layer composited once into an ARGB image and blitted through a `QTransform`;
the real resample is paid once, on commit. Re-resampling half-float tiles on
every mouse move will not hold a frame. Two things about that image are worth
knowing. It is bounded absolutely — 2048 on its longest side — rather than by the
window, which is the one place in this program that is the right way round: what
is held is one layer of one drawing, its size is known when the gesture starts,
and rebuilding it as the view moves would recomposite on every pan of a gesture
whose whole point is to be looked at from several places. And it unpremultiplies
before the sRGB curve and premultiplies after, because Qt's premultiplied format
wants sRGB bytes scaled by alpha; applying the curve to an already-premultiplied
number leaves a rim of the wrong lightness round everything soft, which on line
art is the whole of the line.

**What is not covered.** Deleting the track or the layer under a live transform
goes through paths that do not settle it; `applyTransform` re-checks that the cel
is still there and silently drops the transform if it is not, rather than
enumerating call sites — a list of those to remember would be the same bug with
more steps. Phases 3 and 4 were not built when this was written and are now:
[copy, cut and paste](#copy-cut-and-paste) and
[what a transform costs](#what-a-transform-costs).

## The lasso

Phase 2. A loop drawn with the pen, in image coordinates, rasterised to a
coverage mask when one is needed. `src/core/selection.h` is Qt-free and
`test_selection` drives all of it headlessly, which is the whole reason the
polygon is in `core`.

**A selection does not clip the brush**, which is the decision the rest of the
feature falls out of. You can draw anywhere whether or not something is selected.
So a selection has no independent life: it is an argument to transform, copy and
erase, it has no mode, no panel and no place in the saved project, and a click
clears it. The status bar is the only thing that says one exists.

**Coverage, not a hard edge.** `lifted = src × c`, `remaining = src × (1 − c)`,
which is exact and costs nothing *because* the pixels are premultiplied — with
straight alpha each half would need its colour rescaling and the two would only
add back up approximately. There is a test that they add back up exactly.

**The lift is what makes the two cases one path.** `liftForTransform` returns
both halves; with no loop everything is lifted and nothing stays. The remaining
half then stands in for the layer through `SubstitutedLayer`, in the layer's own
place in the stack rather than painted over the top — over the top it would cover
the layers above it. That is a widening of the `lifted` layer id phase 1 added,
and it replaced it.

**A tile the loop does not reach is shared rather than copied**, so lassoing a
corner of a drawing costs the tiles under the loop and nothing else.

**What separates a click from a lasso is the drag threshold**, four *screen*
pixels, so it means the same thing at every zoom — and never a threshold on the
loop's area. A legitimate selection can be a single eyelash: long, thin, and
near-zero area.

**An empty lasso clears the selection and does not become select-all.** A loop
enclosing no ink is the same as no selection — there is nothing to lift — but "no
selection" also means "transform everything", so a stray loop over blank paper
would quietly become a whole-drawing transform. There is a test.

**Entering a transform dims what is not moving.** Selecting on one layer while
looking at a composite of every track is a real surprise: you loop around a
character and only the ink lifts. The veil goes under the float and over
everything else, which is what says which of the things on screen the gesture is
about.

**Backspace erases the selection and Delete still deletes the drawing.** The
natural expectation on Delete is "erase what I selected" and the existing binding
is "delete this drawing", which is a bad surprise in the dangerous direction;
making it depend on whether a selection exists is a bad surprise in the other.
Two keys, no mode.

**The loop is cleared by changing frame, survives changing layer, and is cleared
by a transform that commits.** The first two are the design's; the third is not
in it — after a commit the loop describes where those pixels *were*, and keeping
it would offer a second transform of a shape that has moved out from under it.

**Even-odd, so a loop that crosses itself has a hole in it**, which is what a
figure of eight looks like to anybody drawing one. Eight sub-rows per pixel row
and exact horizontal coverage: the horizontal half costs nothing and the vertical
half costs a factor, so the two are deliberately not symmetrical. Four sub-rows
was visibly banded on a near-horizontal edge and sixteen bought nothing.

## Copy, cut and paste

Phase 3, and it is small because **a paste is a float that came from the
clipboard instead of from the cel**. The lifting, the hole, the box and the
commit were all already built; what a paste adds is one line — the half that
stands in for the layer is the whole cel, because nothing was taken out of it.

**The clipboard is internal.** The system clipboard cannot carry half-float
precision or the CTG label encoding, and an image handed to another program is a
different feature with a different argument behind it.

**Copy with no selection copies the whole cel**, symmetrically with what the
Transform tool does. Cut is copy plus the hole, in one command.

**A paste lands at the coordinates it was copied from.** You paste to
re-register something, not to drop it wherever the view happens to be.

**An unmoved paste still writes, and an unmoved transform still does not.**
`LiveTransform::pasted` is the difference: landing something on the drawing at
the coordinates it came from is the operation, while picking the drawing's own
pixels up and putting them back is the absence of one and must cost neither a
resample nor an undo step.

**Blocked on the layer kind, never on a guess about the pixels.** A CTG cel
pasted onto a raster layer writes negative light as paint; raster paint onto a
colour layer is a label nobody meant. `refuseHere` is the single list all four
operations — transform, cut, copy, paste — check, because "refuse where the brush
refuses" is one list and not four.

**A paste must not call `refreshEverything`.** That puts the canvas back on the
timeline's frame, and a frame change is exactly what commits a float — so a paste
would bake itself before it could be placed. Only a cut refreshes, because only a
cut has written anything. Found by a test that changed frame behind the
timeline's back, which is a second thing worth remembering: the canvas and the
timeline have to be moved together or the next refresh puts one of them back.

## What a transform costs

Phase 4. `bench_transform` was written before anything was optimised, and it
found the thing nobody would have looked for: **the path that exists in order to
be free was the slowest one in the feature.**

`translated()` walked the *destination* asking the grid for each pixel — one hash
lookup and one half → float → half round trip each — so nudging a 4K drawing
three pixels took **289 ms** on the branch whose entire reason for existing is
that a registration nudge must never cost anything. It is block copies now: each
source tile lands across up to four destination tiles and each overlap is a run
of whole rows, which is correct without blending because a translation is
injective. A translation by a whole number of *tiles* re-keys the handles and
copies nothing at all.

Two other changes, both from the same run. Destination tiles with nothing under
them are skipped before a pixel is read — the inverse-mapped corners of a tile
bound where it can have come from, and line art is mostly paper. And the resample
is spread across a short-lived thread pool by destination tile, exactly the way
the compositor splits by rows, with one `GridReader` per worker because it holds
a cursor.

|  | before | after |
|---|---|---|
| nudge 4K, whole pixels | 289 ms | 43 ms |
| rotate HD 7° | 178 ms | 27 ms |
| rotate 4K 7° | 601 ms | 84 ms |
| scale 4K to 200% | 2334 ms | 369 ms |
| scale 4K to 25% | 39 ms | 7 ms |

What is left is honest. 43 ms to nudge a 4K drawing is mostly allocating and
zeroing six hundred fresh tiles — 78 MB — and no arrangement of the copy avoids
that while a tile is a dense array. Scaling up to 200% is four times the
destination pixels and costs four times as much; it is paid once, on commit, by
somebody who has just decided where the drawing goes.

The lift is 33 ms on 4K and single-threaded, and `paintedBounds` is 10 ms. Both
are paid once when a gesture *starts*, which is why neither was worth chasing —
but `paintedBounds` reads every pixel of every occupied tile, so it must not go
in a loop. The benchmark measured it as 0.00 ms at first because the result was
discarded and the whole call was optimised away; that is worth watching for in
any benchmark of a pure function.

**And the benchmark decides where the next problem will be.** Nothing here times
the preview — the `QTransform` blit per frame is Qt's and this cannot see it —
and nothing times a whole gesture end to end. If a transform ever feels heavy in
a way these numbers do not explain, that is where to look first.

Those numbers are the ones the box filter ran at. What replaced it costs more,
and [the next section](#what-a-commit-does-to-a-line) is why that was worth
paying.

## What a commit does to a line

Reported as "it looks good until you press Enter", and it was one line of the
resampler choosing between two filters on the wrong quantity.

`transformTiles` decided whether to interpolate or to average a block from the
axis-aligned box of a destination pixel's footprint in the source, `(|a| + |b|) /
2` per axis. For a rotation that is `(|cos θ| + |sin θ|) / 2`, which is **greater
than half a pixel at every angle that is not a multiple of ninety degrees** —
0.509 at one degree, 0.557 at seven, 0.707 at forty-five. So every rotation was
treated as a reduction and sent through a filter meant for one.

**The damage was not blur, which is what makes it worth writing down.** The block
was unweighted and its bounds were rounded outward to whole pixels, so it had no
sub-pixel response at all: whether it spanned two pixels or three flipped with
the fractional coordinate, and where an edge fell *inside* a pixel was rounded
away. Every anti-aliased rim the brush had laid down came back as stair-steps.
A filter that had merely been too soft would have been reported years later, if
at all; this was reported the first time somebody rotated something.

It also could not weigh what it read. Ink came out 5% light at nine tenths and
26% heavy at a quarter — the error changes sign because it is footprint rounding
and not a bias, which is why no test of the "does it darken" shape would have
found it.

**What is there now is one tent, and no second filter.** Its support along each
source axis is `max(1, 1/scale)` source pixels: one pixel when the drawing is
magnified or only turned, which is bilinear exactly, and `1/scale` when it
shrinks. The scale comes from the `Transform`'s own numbers and never from the
mapped footprint — that substitution is the whole of the fix.

It is separable along the *source* axes rather than the destination's because
`matrixOf` builds `R · S`: the scale sits next to the source, so the prefilter a
reduction wants is axis-aligned there, and a rotation, being rigid, wants none.
That is a property of this program's `Transform` being a similarity and not a
general affine, and it is the second thing that decision has paid for.

`boxSampleStride` went with it, and its absence is not an oversight. The
compositor caps its samples because a display cache is rebuilt on every pan; a
commit is paid once and kept forever, and those are different decisions however
much they look like one. A tent scaled to the reduction also reads about four
samples per *source* pixel whatever the scale, so nothing here grows without
bound the way a fixed footprint would.

|  | box filter | tent |
|---|---|---|
| nudge 4K, whole pixels | 43 ms | 43 ms |
| rotate 4K 7° | 84 ms | 100 ms |
| scale 4K to 200% | 369 ms | 362 ms |
| scale 4K to 25% | 7 ms | 59 ms |

The exact path and magnification are untouched, because neither ever entered the
branch — 150% looked identical before and after, and that is what said the branch
and not the arithmetic was at fault. A rotation costs a fifth more. A four-times
reduction costs eight times what it did, which is the honest price of reading the
block it is averaging instead of three samples across it.

**Two tests pin it, and both were watched going red against the old filter.**
That mattered more than usual here: the first version of the rotation test
asserted that the darkest pixel of the turned line was at least half opaque, and
it **passed against the filter it was written to catch** — a block average leaves
a third where it spans three pixels and a half where it spans two, the span
alternates with the phase, and two hundred pixels of line contain plenty of both.
What separates them is that the line is dark *all along*, so the assertion is on
the palest column and not on the darkest pixel.

**What found it was looking.** The numbers say a rotation loses about 45% of its
edge contrast, which is a figure nobody would act on; the magnified screenshot of
a committed arc beside a live one is unarguable. Both are in
[looking at the interface](#looking-at-the-interface).

## What the pointer says

Issues #27, #4 and #5, built together because all three wanted the same missing
piece first and none of them is worth much without it. Through a whole transform
the cursor was `CrossCursor`, and a press meant one of four things depending on
where it landed — move, scale one axis, scale both, rotate — or nothing at all.

**One enum and one function**, `CanvasWidget::Pointing` and `pointingAt`. It
answers from everything true at once — a gesture already under way, then the
held keys, then what is under the pointer, then which tool is up — and it is the
only thing in the file that calls `setCursor`. The nine calls it replaced were
the problem rather than a symptom of it: three of them wrote out the same
`space_held_ ? … : zoom_key_held_ ? …` chain by hand, which is how a cursor ends
up stuck as a closed hand after a path nobody tested. Same shape of fix as the
shortcut table, for the same reason.

**The order the question resolves in is the order a press resolves in**, and it
has to be. Mid-drag the answer is what was *grabbed* and not what is underneath:
a corner handle dragged past the opposite one leaves the pointer nowhere near
the box and the gesture is still a scale. Held keys come next because navigation
is available inside every tool — a box that appeared to swallow Space would be
saying something false about what a press does.

**The hit test is now one function asked by both halves.** `boxTargetAt` answers
"what would a press here grab", and `beginTransformDrag` is that plus the pivot
arithmetic. While they were two pieces of code there was nothing keeping the
promise and the press in step, and the case that made this worth doing is
exactly the one where they disagree by design: dragging *at* a corner scales,
dragging *just outside* one rotates.

**And there is no fourth outcome any more.** A press off the box used to do
nothing, which was the one of the four worth saying with a cursor from another
family, and `Pointing::Nothing` was it. Since a drag that starts anywhere
[moves the drawing](#moving-a-drawing), a transform claims the whole canvas and
the move cursor is shown across all of it. That looks like a loss of information
and is the rule being obeyed: **the pointer answers the same question as the
press under it**, and the press changed. The enumerator is kept rather than
deleted, unreachable while a transform is live, because it is still the right
answer for anywhere else that acquires one.

**The invisible band survives, and now says so.** That was the open design
question in #27 — a band nobody can see is a second unlabelled way to do
something the knob already does labelled — and the answer was to keep it and
change the cursor there, which was the user's call. It is where a hand reaches
without being told.

**Three mechanisms, not one, and knowing which is which is most of the work.**

- *System cursors* for the eight handles. Qt has diagonal, horizontal and
  vertical size cursors and they map straight on. What they name is the
  direction the **hand drags**, so it follows the box round as it turns: the top
  edge of a box rotated a quarter turn stretches sideways. The scale is
  deliberately *not* applied — a corner of a box squashed flat still points
  nearly sideways and still scales both axes, and a cursor describing the
  drawing's own axes would be describing the wrong thing.
- *Drawn cursors* for the three things the system has no glyph for: rotation,
  the eyedropper on Alt, and the eraser. The first bitmap cursors in the
  program. Light under dark, the rule the transform box already follows, because
  a cursor crosses paper and ink by definition.
- *A ring the canvas draws itself*, at the tool's radius, while a drag is
  setting that radius. Not a cursor and it could not be one: a brush here goes
  up to 400 pixels across and a cursor is a 32-pixel bitmap.

**What the widget draws arrives a frame after the pointer does, and that decides
which mechanism a thing belongs to.** The eraser was built first as a ring at
its radius following the pointer — which is what #4 and #5 sharing a mechanism
was going to mean — and it was reported straight back: the ring lags, and it sat
on top of the crosshair so one hand had two marks under it. Both faults are the
same fault. The pointer is moved by the hardware and the ring is painted by us,
one repaint later, so a ring chasing a pointer trails it at exactly the speed
the hand is moving; and a mark that is not the cursor is a second pointer.

So the rule is: **anything that must sit under a moving pointer has to be the
cursor.** The eraser is a drawn rubber in place of the crosshair, which is also
what makes it visible when the pen is turned over — read from hover now, not
from the press, since the whole complaint was that you found out by drawing.
The ring survives for the resize gesture alone, and it is fine there for exactly
the reason it failed for the eraser: it is anchored to where the drag began and
holds still while the pointer moves away from it. There is nothing to trail.
It is also the right picture for that gesture — the pointer is measuring a
distance out from that point and the circle is what the distance means.

**A screenshot cannot check almost any of this**, which was known before
starting and is why the tests assert `cursor().shape()` and the decision behind
it after moving the pointer somewhere. `QWidget::grab()` renders the widget and
never the pointer. Two consequences worth having written down:

- The ring is the exception, and deliberately: because the canvas draws it, a
  view taken during a resize **is a different picture** from the one before the
  radius moved, and there is a test that says so. It is the only piece of
  pointer feedback a picture can catch.
- A drawn cursor can still be looked at, through `QCursor::pixmap()`. Nothing
  asserts on it, but it is how the glyphs were checked and every one of them
  needed it: the first eyedropper was a thumb, the second was a magnifying glass
  — which in a program with a zoom on a held key is worse than no glyph at all —
  and neither is a thing any assertion here would have noticed.

**`beginNavigation` takes the event's modifiers now, not the machine's.** It
read `QGuiApplication::keyboardModifiers()`, which answers for whatever the
person at the keyboard is leaning on — so the resize gesture could not be driven
by a test at all, and #5 turned out to be a gesture with nothing watching it.
The same reasoning put `alt_held_` on the widget rather than a global query
behind the eyedropper cursor.

**And a hover-driven cursor needs the move handler to run with no button down**,
which during a transform it did not: `mouseMoveEvent` handed the event to
`continueTransformDrag`, which returns immediately when nothing is grabbed. So
the box knew perfectly well what a press would do and had no occasion to say it.
The pointer is now updated at the top of the handler, before anything decides to
swallow the event.

One thing measured rather than assumed: the cursor is pushed to the platform
only when the decision *changes*, not on every mouse move. `pointing_` is an
optional for exactly that — empty until the first answer, which is what lets the
constructor go through the same function instead of being a second place that
knows what a cursor is.

**And the timeline had the same disease, which is how it got found.** Reported
while this was being built: the pointer turned into a hand the moment it entered
the timeline. The cause is the shape rather than the pixel — the cursor was
decided at five points along `mouseMoveEvent`, with a `hovering_edge_` flag
remembering whether one of them had already fired, and the ruler claimed
`PointingHandCursor` because scrubbing is a click. The ruler is the first thing
the pointer crosses coming down from the canvas, so the whole strip read as a
hand before you had reached anything, and the hand that means *this drawing can
be picked up* was the same hand.

`TimelineWidget::cursorAt` is now the one place, in the same order as the
canvas's: a gesture under way, then the ruler, then what is under the pointer.
The ruler is an arrow with a split-arrow on the end-of-shot grip, and a hand
means one thing — this can be picked up and carried somewhere else. The flag is
gone, and with it `leaveEvent`, which existed only to undo it.

That rule was "a numbered card" until the gutter became the handle a track is
restacked by; it is two places now and still one meaning. See
[restacking by dragging](#restacking-by-dragging).

**And asking the same question in two places found a real bug under it.** The
first fix asked "is there a card here" the way `mousePressEvent` already did —
through `slotAt`, which **clamps** x to the last frame. Past the end of the
strip that answers "the last slot", so the whole width of the widget beyond the
drawings offered the last one: an open hand out there, and a drag that really
did pick that drawing up and move it. Reported, and only visible in a track of
**single-frame drawings** — with anything held, the clamp lands mid-run,
`runBounds(slot).first != slot`, and the wrong answer looks exactly like the
right one.

`cardAt` is the honest version and both the press and the pointer go through it.
The distinction it draws is worth keeping: **a clamp is right for the playhead
and wrong for hit-testing.** Clicking past the end still means the last frame,
because there is always a frame you are standing on — but "what is under the
pointer" has to be able to answer *nothing*, and a function that cannot say so
will invent something plausible instead.

## Looking at the interface

Issue #28, and the shortest section here that is worth anything: `tests/shots.cpp`
drives the real `MainWindow` through a list of named situations and writes one
PNG each.

```bash
./build/tests/shots            every situation, into build/shots/
./build/tests/shots --list     their names and what each is for
./build/tests/shots transform  only the ones whose name says transform
```

**Every interface bug in this file was caught by looking, and none by a green
build** — the mis-encoded character, the "Add colour layer" button an edit
silently failed to add, the two identical red swatches, the transform box drawn
128 pixels clear of the drawing because it was made from tile-aligned bounds,
the blue rim on every timeline row. The first thing it caught after it existed
was not an interface bug at all: a magnified crop of a committed rotation beside
a live one is what turned "the quality drops on Enter" from "a one-pixel line
loses 45% of its edge contrast", which is a figure nobody could act on, into
stair-steps nobody could argue with — and the shape of the picture is what said
the defect was a lost sub-pixel phase rather than blur, which is a different fix. See [what a commit does to a
line](#what-a-commit-does-to-a-line). What was missing was never the will to look
but the scaffolding: building lasso and transform meant writing a throwaway
screenshot function into `test_canvas.cpp`, building, looking and deleting it
again, four times. A cycle that costs a build and leaves debris in a test file
is a cycle nobody runs when they are nearly finished, which is exactly when it
is worth running.

**It is not a test and must not become one.** Nothing asserts. Golden-image
comparison is the tempting mistake: font rendering differs across platforms and
Qt versions, so it would go red on CI for reasons that are not bugs, and a red
CI that means nothing is worse than none. It belongs with the benchmarks and
carries their instruction — run it before and after anything that touches the
canvas.

**It is meant to be edited, including by whoever is only passing through.** Add
the situation you need, bend one that is nearly right, delete one that is in the
way. Nothing depends on any of them: no test reads them, there are no reference
images to keep in step, and `ctest` never runs the target. A situation added to
chase one bug and deleted afterwards costs a recompile of one file and leaves no
debris, which is the whole difference from the throwaway function it replaces.
The three-line shape is the part worth protecting — a situation nobody can add
in three lines is one nobody adds at the end of an afternoon, and the end of an
afternoon is when looking pays.

**A gesture is said one piece at a time, on any widget, and that is deliberate.**
`Stage::pressOn`, `dragTo` and `releaseOn` take the widget because the canvas is
not the only thing in the window with gestures on it — the timeline has four —
and the first situation that needed one built `QMouseEvent`s by hand inside the
lambda: fifteen lines where the contract is three, most of it plumbing with
nothing to do with what the picture was about. **Nothing releases for you**, and
that is the other half: the interesting pictures here are the ones taken
*during* a gesture. A drop caret exists only while a row is in hand, and so does
the range an overwriting drop would take over. A situation that wants the
mid-drag picture simply does not call `releaseOn`; one that wants the result
does.

**The cursors are the exception, and they are why the file is not just
`grab()`.** `QWidget::grab()` renders the widget and never the pointer, so the
rubber, the pipette and the rotation arrow appear in no screenshot of the canvas
at all — see [what the pointer says](#what-the-pointer-says). They are read off
the widget with `QCursor::pixmap()` after hovering what raises them, which
checks the half a glyph cannot: that the right one comes up in the right place.
Each is shown on paper and on ink, because the rule they follow is light under
dark and a cursor crosses both by definition.

**And the most useful thing it did was print rather than photograph**, which is
worth knowing before trusting the sentence above about what looks wrong. Issue
#12 was diagnosed by a throwaway situation that built sixty layers, added a
colour layer and printed the scrollbar every twenty milliseconds — the answer
was `value=29 max=44, at 13 ms, and never moves again`, and it is the whole of
the diagnosis. The picture of that same bug is a layer list scrolled to layer 32
of 61, which looks exactly like a layer list. So a situation is free to print,
and the thing it prints may be the deliverable; the picture it also writes is
sometimes only a formality.

**Writing it found one thing, and it was in the harness rather than in the
program** — worth recording because anyone driving this widget with synthetic
events will hit it. The canvas deliberately does not ask
`QGuiApplication::keyboardModifiers()`, which would answer about whichever keys
the person running the tests is leaning on; it reads Alt from the key event
*and* re-reads it from every mouse event afterwards, because a real window
system stamps the live modifier state on all of them. So a hover sent with
`Qt::NoModifier` silently un-holds Alt, and the eyedropper cursor was reported
absent when it was the harness that had let go. `Stage` holds the modifiers and
stamps them on, the way the window system does.

## The same source, two different pictures

This section exists because it was missing, and its absence cost most of an
afternoon. Somebody downloaded the Windows build, found the timeline drawn white
on white, checked that the release was built from the commit they had, and
reasonably concluded the difference had to be in the program. It was not. The
search went through the compositor, the half-float path, the threading in
`refreshRegion` and the sRGB conversion before reaching a widget that draws
rectangles — because nothing here said what a downloaded build does not share
with the one on your desk.

**It shares the source and nothing else.** The `latest` pre-release names its
commit in the release notes, so confirming that takes ten seconds and is worth
doing first — but it settles less than it looks like it settles. Everything below
still differs:

| | yours, per the README | the download |
|---|---|---|
| compiler | GCC, MSYS2 UCRT64 | MSVC 19, Visual Studio 2022 |
| Qt | whatever MSYS2 has today — 6.11 at the time of writing | 6.8.\*, pinned in `ci.yml` |
| sanitizers | none with MinGW; the probe fails, see [where it got to](#where-it-got-to) | off, deliberately |
| C runtime | UCRT via MinGW | MSVC's |
| Qt libraries | MSYS2's DLLs on `PATH` | whatever `windeployqt` copied |

**The Qt version is the one that changes what you see**, and it is the one nobody
suspects, because a version gap in a toolkit reads as a risk of crashes rather
than of colours. It is the opposite. Qt is where the system palette, the style
and the platform theme come from, so a widget that reads a palette role is
reading a value that this repository does not contain and cannot pin. Two Qt
versions on the *same machine under the same Windows theme* hand over different
palettes — that is not a bug in either of them, and it is the whole of the
white-on-white timeline. See the first entry under [the traps](#the-traps).

**So the question to ask first is: what does this widget read that is not in the
source?** A palette role, a style metric, a standard icon, a font. That set is
small, and it is where a difference between two builds of one commit almost has
to live. Ordinary program state is not a candidate — it came from the same code.

**And the way to answer it is to print, not to photograph.** `shots` runs the real
window and a situation may print whatever it likes; `the-timeline-palette` prints
every role the timeline reads with its alpha and then what the derivation makes
of each one. Five numbers ended a search that four hours of reading the
compositor had not. This is the same lesson as ["guessing cost more than
instrumenting"](#the-traps), one layer further out, and it earns its own entry
because the instrument that was missing was not a crash log — it was a way to see
a value that arrives from outside the program.

**What is still not pinned.** Nothing checks that the shipped build and a local
one agree about how anything looks. There are no reference images —
deliberately, and for the reasons set out in [looking at the
interface](#looking-at-the-interface) — and CI builds `shots` without ever
running it, so no picture of a released build exists anywhere.
[#33](https://github.com/S-poony/Animage/issues/33).

## What the history is allowed to cost

Issue #23. `undo_stack_` only ever grew — `clearHistory` and loading a project
emptied it and nothing else ever took anything off it — and a command retains
the tiles it displaced, as handles to tiles that would otherwise have been
freed. Nothing broke: the failure is memory over a long session, and it has a
second half that is easy to miss. `historyReferences` counts the history as a
reference on a cel, so deleting a drawing frees nothing at all while a step that
mentions it is still on the stack.

**The cap is in bytes and not in steps, because one command is worth forty of
another.** Measured rather than estimated, at the end of each size in
`bench_transform`:

| what one command retains | |
|---|---|
| a stroke | 0.25–0.75 MB, being the two to six tiles it crossed |
| a full-cel transform, PAL | 6.5 MB |
| the same, HD | 22 MB |
| the same, 4K | 74 MB |

A tile is 128×128 half RGBA, so 128 KB, and a transform replaces every tile the
cel has while `Cel::replaceTiles` journals both sides. "Keep the last hundred
steps" is therefore fifty megabytes of strokes or seven gigabytes of transforms,
which is one rule choosing two numbers that are nothing like each other. That is
the whole argument for counting bytes.

`Document::kDefaultHistoryBudget` is 512 MB — about a thousand strokes,
twenty-three HD transforms or six 4K ones — and `setHistoryBudget` moves it and
takes effect immediately, which is also how the tests reach it. Past the budget
the oldest commands go, oldest first, and then `collectGarbage` takes the cels
they were the last thing holding. That second step is the point rather than
housekeeping: those commands are exactly what was standing between the collector
and the drawings somebody had already deleted.

Four things about it were decisions rather than mechanics.

- **The newest command is never dropped**, even when it is larger than the whole
  budget on its own — which one 4K transform nearly is. What you have just done
  has to be undoable, and a cap that made the edit in front of you the one thing
  you could not take back would be a worse bug than the memory it saved.
- **Only the undo stack is trimmed.** Everything on the redo stack is *newer*
  than everything on the undo stack, it being the branch that was undone away
  from, so dropping from that end would take the recent work and keep the old.
- **The collector is not run for nothing.** `collectGarbage` is O(cels ×
  history) and this happens at the end of every stroke once the budget is full,
  so the cel ids the dropped commands mentioned are gathered first and the scan
  runs only if one of them is now orphaned. A dropped stroke on a drawing that
  still exists costs a handful of comparisons.
- **The ops are not weighed, and a step cap is what bounds them.** A command's
  ops copy layer lists, slot vectors and `Image` records — two to three orders
  of magnitude below a tile grid — and pricing them properly would mean a
  virtual on every `Op` estimating the size of containers it does not own. So
  `kHistoryStepCap` is 10 000 steps, which is deliberately *not* the budget: at
  half a megabyte a stroke the bytes bind ten times sooner, so it never ends a
  drawing session's history. It exists because a day of retiming and restacking
  records commands that hold no tile at all, and a byte budget that only sees
  pixels would let those grow without limit.

Freeing tiles that erasing had emptied was the other end of the same number and
is already done — `Document::endCommand` drops them, and a command that then
records no difference at all is not put on the stack to be counted. See
["a rectangle built from tile coordinates remembers what you erased"](#the-traps).

**And the save marker had to stop being a step count.** `MainWindow` asked
"changed since the last save" as `undoDepth() == saved_undo_depth_`, which a
capped history breaks outright: drop one step off the bottom, push one on the
top, and a document with a whole session in it reads as saved — so the title
loses its `*` and, far worse, autosave decides there is nothing to write. It was
already wrong in a smaller way before any of this, and had been from the start:
undo one step, draw a different one, and the count is back where it was over a
drawing that is not the one on disk.

It is `Document::historyStamp()` now — the stamp of the command on top of the
undo stack, 0 when there is none, handed out once and never reused. Undo and
redo walk the same stamps, so "undone back to where the last save stood" still
counts as unchanged, which was the good half of the depth and the only one.
`adifferentEditFromTheSameDepthIsUnsaved` pins it through the real window, and
the depth it asserts is *equal* is precisely what the old marker was reading.

The status bar says `undo 12 (34 MB)` rather than `undo 12`, because with a
budget in bytes the step count no longer says what the history costs, and the
megabytes are the only warning that the next stroke will drop the oldest one.

**What would change the number.** Six 4K transforms is thin, and somebody
working at that size who runs out of undo where they did not expect to is the
report that should raise it — it is one constant, and the arithmetic to argue
with is printed by `bench_transform`. What that report must not produce is a
step count, which is the thing this replaced.

## What is not what the plan asked for

Places where the built thing deliberately differs. Each was a judgement, and
each could reasonably be reversed.

**Compositing is on the CPU.** The plan says QRhi. A CPU compositor was written
first because it can be tested without a window, and it stayed because it turned
out fast enough once it was measured — 4.8 ms for four layers over a viewport,
after the two fixes described below. The GPU path is still the right end state;
the interface for it already exists in `Compositor`, and `bench_composite` is
what would show whether a rewrite paid.

**Input and playback share a thread.** The plan asks for them to be split now
rather than retrofitted. M0 measured the application's own share of latency at
0.03 ms, so there was no case for it yet. The retrofit is well supported:
copy-on-write tiles make a snapshot for a render thread nearly free — which is
no longer a claim, because the CTG solve is that retrofit and it cost almost
nothing. See `CtgJob`.

**What runs where.** Worth knowing before touching any of it, because it is
three different answers:

- The interface, the document and every write to it are on the main thread. The
  document is not locked anywhere and must not be read from another one.
- Compositing spreads its rows across a short-lived thread pool inside one call,
  and joins before returning. Nothing outlives the call.
- A CTG solve runs on a worker owned by `CtgSolver`, reads only its own `CtgJob`
  copy, and hands back a whole `CtgFill`. It is installed into the document by
  the interface thread on a 16 ms poll. A worker never touches a widget, and the
  solver's own wake-up callback is deliberately unused by the canvas for that
  reason.
- An export does the same, through its own `CtgSolver` and a nested event loop
  rather than a poll — it is a thing the user is waiting for, so it waits, and
  the loop is what keeps the progress dialog painting while it does. Every write
  to the document is still the interface thread's, made where the answer is
  collected. See "export writes 16-bit PNG".

**There is no scribble tool**, and there should not be one — this was the
user's call and it is a better design than the plan sketches. A CTG layer is
the mode: draw on it with the ordinary brush and what you draw is a scribble.

**Cels are not saved as PNG.** Argued out at length, with the measurements and
the rejected alternatives, in
[why-our-own-formats.md](why-our-own-formats.md) — it is the first thing anyone
asks about this repository. In brief: the plan says a PNG per cel; a 16-bit PNG cannot
hold a half-float. Half spends its precision relatively — finely near zero,
coarsely near one — while integers are evenly spaced, so of the 15362 half values
in [0,1] a 16-bit image keeps 7169, and some non-zero values quantise to zero.
sRGB-encoding first keeps 10871, which is better and still lossy. A save that
loses pixels is not a save. The format is ours instead, storing the same bits the
tiles hold — the layout is documented in `project_io.h` so a drawing is
recoverable with nothing but zlib. PNG remains the right thing for *export*,
where a conversion is expected and the destination is another program.

**Pen prediction is ruled out permanently.** Not deferred — refused. It hides
latency rather than removing it, and pays with accuracy at the ends of strokes.
Do not propose it.

## The traps

These are the things that cost hours, in the order they hurt.

**A gesture that opens a command on the press and closes it on the release will
one day not get the release, and the undo history dies silently for the rest of
the session.** This one shipped, and it is first because of how quiet it was.

`Document::beginCommand` nests by counting depth and `endCommand` only commits at
zero. So one stroke that never ended left the depth at one for good: every later
stroke went one, two, one and never reached zero, and **nothing was pushed onto
the undo stack again**. Ctrl+Z jumped back past everything drawn since. Three
more things failed with it, all without a word — autosave defers while a stroke
is in progress, so it deferred for ever; `journal_.take()` never ran, so tile
snapshots accumulated unbounded; and `requestCtgFills` returns early while
stroking, so no colour layer ever solved again.

The release goes missing more easily than it sounds. `tabletEvent` ignores every
event while a modal dialog is up, which is right — the dialog has a better claim
on the pen — but a release is one of those events, so opening any dialog with the
pen down was enough. Alt+Tab is the same shape. And because the canvas has both
`WA_TabletTracking` and mouse tracking, the stuck stroke then *drew on hover*: a
pointer moving across the canvas with nothing pressed, painting at full pressure.

**A press-to-release gesture needs a third way out, and the widget has to be told
when it is no longer the one finishing things.** `abandonGesture` ends whatever is
open the way the release would have, from `focusOutEvent` and from
`changeEvent` on `ActivationChange`; `beginStroke` closes an open stroke before
opening another, because the cost of missing one route is the whole session.
`TimelineWidget` had the identical leak in its two drag gestures and has the same
guard. The test that pins it fails three ways on the commit before it, and two of
those three are the history, not the flag.

**A palette role arrived with alpha on it, and the timeline vanished.** The
downloaded Windows build drew the timeline white on white — no cell outlines, no
tick marking a held frame — while the same commit built here drew it correctly.
`TimelineWidget::paletteFor` bends one palette role into another, lighter for a
dark theme and darker for a light one, and **`QColor::lighter` and `darker` work
in HSV and carry the alpha through untouched**. So `QPalette::Window` arriving
with any transparency in it did not make the row faint: it made the background,
the ruler, the gutter and every outline invisible at once, while the drawing
numbers and the playhead went on drawing, because those are roles used as they
come rather than bent.

**Forcing the alpha fixed it and shipped a worse bug**, which is the part of this
worth reading twice. The Qt the Windows build ships against hands `Window` over as
`#00000000` — transparent, and *black underneath*. Made opaque that is pure black,
`lightness()` reads 0, the theme is taken for a dark one, and `lighter()` scales
the HSV value, so it cannot lift a black: `lighter(180)` on black is black. Same
widget, same commit, one release later, a black slab instead of a white one. The
second bug was already known when the first was fixed — it had an issue open with
the measurements in it — and it was filed as a hypothetical about accessibility
themes rather than recognised as the other half of the value in hand.

So the lesson is not "force the alpha". It is that **`Window` was a role this
widget could not substantiate**, and the fix is that nothing structural is derived
from it any more. `Base` can be substantiated: it is what a cell is painted with,
it is what every text field in the application stands on, and a theme that gets it
wrong is broken in a way somebody has already reported. It arrives as `#ffffff` in
the build that draws this correctly and in the build that did not. `lighter` and
`darker` are gone with it, replaced by a step of a fixed fraction of the distance
to white or black — the separation is then the same wherever it starts, which is
what these numbers were always for.

Four things about it are worth more than the fix.

**A degenerate value is not a wrong value, and it fails twice.** `#00000000` is not
a colour that is slightly off; it is a role nobody set. Read for its alpha it
erased the row, and read for its brightness it inverted the theme. Anything
derived from a palette role should be asked whether the role can be *shown to be
meaningful*, not whether it happens to be in range — and "this is what the cells
are painted with" is that argument, where "this is the window colour" was not.

**The symptom pointed away from the cause.** A row with no outlines reads as a
missing `drawRect`, and the things that still drew — numbers, playhead, gutter —
read as proof that the paint event was fine. They were the clue: *everything that
survived was a palette role used raw, and everything that vanished was one that
had been bent.* That split is the whole diagnosis, and it is visible in the
screenshot before any code is opened.

**It is not a dark theme's problem and not a rare one.** This machine's Windows 11
theme hands over `WindowText` at `#e4000000` — alpha 228. The palette the
timeline reads is *already* one with transparency in it; which roles carry it is
Qt's business and moves between Qt versions. Local is Qt 6.11 from MSYS2 and CI
builds against 6.8, which is the entire difference between the two pictures.

**The suite is green through all of it, and always was.** All fifteen tests pass
with either bug present — they pin arithmetic, and this is a colour arriving from
outside the program. `tests/shots.cpp` carries the four situations that do see it:
`the-timeline-palette` prints every role with its alpha and then what is made of
it, and three beside it hold a palette still —
`a-timeline-whose-window-colour-has-alpha`,
`a-timeline-whose-window-colour-is-transparent-black` (the one that actually
ships) and `a-timeline-on-a-pure-black-theme`
([#32](https://github.com/S-poony/Animage/issues/32), Windows' High Contrast
Black). All three must look like the first. They are three lines each, and they
are the only way to see any of this without a build made against another Qt. If
the derivation is ever touched, run them.

**A picture would have been quicker than a release.** Each of these was found by
someone downloading a build, and each was then reproduced here in about a minute
by setting one palette role and taking a screenshot. The gap between "cannot
reproduce locally" and "reproduced locally in a minute" was entirely the idea of
*forging the input* rather than trying to obtain the environment. That is what
these situations are, and it is worth reaching for before the next release goes
out to find something.

**Two crashes, one cause, and I caused it.** Space and Z are held modifiers, not
shortcuts, so they are forwarded to the canvas by an application-wide event
filter. An application filter also sees the events it sends itself — and sees
them again when an unaccepted key propagates to a parent widget. Both routes
lead straight back in. It needs a re-entrancy flag, and the canvas must *accept*
those keys including auto-repeats, or holding one past the repeat delay
recurses until the stack runs out. See `MainWindow::eventFilter`.

**Guessing cost more than instrumenting, twice.** The first crash took two wrong
theories before a test that sent real key events found it in one run. The second
took four, and was answered in one round trip once `crash_report.cpp` existed.
The lesson is written into the commits because it will happen again: after the
second wrong theory, stop and instrument.

**An implicit background as a *seed* was tried and removed — then solved from
the other side.** A single scribble has nothing to be cut against, so it labelled
everything, and filling one shape took a second scribble for the world outside
it. Seeding a background round the rim fixes that and no strength for it works:
weak enough to lose to a real scribble is weak enough for a gap in the line to
defeat, and strong enough to hold a gapped shape is strong enough to overrule the
scribble the user drew.

The reason is worth keeping, because it says where *not* to look. The strength of
a soft seed is its area — severing one costs `λK` a pixel, which is exactly what
makes the majority rule work — so a rim seed's authority comes from the size of
the canvas rather than from anything the user meant. There is no good value for
that knob and there was never going to be one.

**A finite border *price* was then tried, and failed differently.** Charging `g`
per pixel for a boundary running along the edge is attractive — `g` comes out as
"the longest hole the fill will jump", a specification rather than a knob, and it
measured exactly: a hole of `n` cells needed `g = n + 1`. That is the flaw. Any
price makes "give the colour everything" an available labelling costing `g·K`,
against `n·K` to bridge, so **a finite border price is a gap cap exactly equal to
it**. It only filled shapes that were nearly closed, which a paint bucket nearly
does. There is no value that avoids this; do not go looking for one.

**What works is an unseverable rim**, and that is what is there now: the
outermost ring of the solved grid is a hard background seed. It cannot be bought
at any price, so *some* boundary separating scribble from rim must exist and the
cut simply picks the cheapest — along the outline where there is one, across the
holes where there is not, at any width. Measured against the two-scribble
arrangement it replaced, one scribble bridges strictly wider holes at every
scribble size. The tolerance it gives is `n < λ·|S|`: **scribble bigger to bridge
bigger holes**, which is a rule a person can act on.

Two consequences to know. A user's scribble displaces the rim seed where it is
drawn — that is the ordinary rule that your seed wins where you seeded, and it is
how a region running off the frame gets filled. And **scribbling a background
colour no longer fills the background**: the rim cannot be overruled, so a mark
out there keeps roughly its own pixels. Colouring the outside now wants an
explicit background colour on the layer rather than a scribble.

It also got faster, against expectation: `bench_composite` at 512x512 went 152 ms
(no background) → 417 ms (priced border) → 117 ms (hard rim). More cuts, far
smaller sub-problems — the paper's largest-first pruning finally has something to
prune with.

**Solving on a stale cache is not the same as solving when asked.** Every dab
bumps the cel's revision, so regenerating a CTG fill whenever it looked stale
meant a max-flow per dab. The cost was invisible for a while because a separate
bug meant the result was never drawn; fixing the repaint made it obvious. The
solve now happens once, when the pen lifts, and the scribble itself is shown
during the stroke.

**The solve used to run where the interface was waiting, so it was capped.** A
max-flow grows faster than its region — about 1.3 s for a megapixel — and on a
large drawing an unbounded one does not take a while, it stops the program, so
the resolution was reduced until the solve fitted in roughly 512x512 whatever it
was asked for. That is a real loss of quality on a big canvas, and the fix was
always to solve on a background thread and refine.

That is now what happens, and the cap is only the *first* answer: 127 ms coarse
so the colour follows the pen, then the same drawing solved at the size it was
drawn at, 1.7 s later at 1080p. The second bound is on memory rather than
patience — a max-flow keeps something like ninety bytes a cell, so four million
cells is a few hundred megabytes while it runs. Both numbers are in
`bench_composite`.

Two things that were easy to get wrong here. **A widget repaints many times a
second and the newest question wins**, so a paint that asks again for a solve
already running calls off the answer the last paint was waiting for — for ever.
The canvas records what it has asked about. And **leaving a drawing has to call
its solve off**: playback is twenty-four questions a second against answers
taking a tenth of one, and a queue that fills faster than it drains never
catches up.

**A widget on a list row disables that row's own tick.** The show-scribbles box
was a `QCheckBox` in a widget set on the layer's row with `setItemWidget`. Qt
treats an index widget as a persistent editor, so it routes the press into the
widget instead of to the delegate — and the row's *visibility* tick silently
stopped working, meaning no colour layer could be hidden at all. The panel is
two real check columns now. If you are tempted to put a control on a row, don't.

**Solving globally and repainting locally is a bug that looks like a feature.**
A regenerated CTG fill changes colour across whole regions, nowhere near the
pen. Marking only the stroke's own rectangle dirty left the new fill beside the
stroke and the old one everywhere else, while hiding and showing the layer
repainted the lot — so the same operation appeared to have two behaviours. Any
path that regenerates a fill has to mark everything dirty.

**The per-dab solve came back through the other door.** The guard was "is a
scribble being drawn", which covers drawing on the colour layer and misses
drawing on the line art it is cut against — so inking over a filled drawing ran
a max-flow per dab. The condition is any stroke at all, and the solve belongs at
the end of it.

**Never point-sample the barrier.** `ctgBarrier` used the compositor's `step`
argument, which samples every nth pixel. Line art is thin: at a coarse step a
two-pixel line becomes a dotted line, the barrier acquires holes that are not in
the drawing, and the fill pours out through its own outline. Composite at full
resolution and reduce by taking the *most* covered pixel in each block — too
solid costs a little gap tolerance, too thin costs the whole fill.

**The obvious optimisation was the wrong one, and only a measurement said so.**
Saving ninety-six drawings took 10.5 seconds and opening them 3.9, every time,
changed or not. The obvious fix is to skip cels that have not moved — which would
have left the *first* save and every *open* exactly as slow, because neither
repeats anything. Breaking the time down instead said 92.6% of the bytes handed
to the compressor were zero: tiles were written whole, so a three-pixel line
crossing a 128x128 tile contributed 128 KB of which almost none was ink. Storing
only each row's occupied span took save to 3.0 s and open to 1.6 s, and made the
files *smaller* — 12 MB to 6 MB — because deflate was no longer being asked to
re-derive emptiness we already knew about.

Two things to carry from it. **A repeated-work optimisation cannot help the first
run**, so if the first run is also slow the problem is somewhere else. And format
decisions are the ones to measure early: this was a change to the bytes on disk,
which is cheap while no project exists and a migration afterwards.

**A counter is not a state.** Mouse events promoted from the pen were recognised
by "has this canvas ever seen a tablet event", which is true forever after the
first time the pen came near — so touching the tablet once left the mouse unable
to draw for the rest of the session, silently. It is a time window now.

**Nothing was measured until it had been "optimised" three times.** Every lag
complaint traced to one operation — flattening the visible region — which had
never been timed. It was 27 ms for four layers, against a 16.7 ms frame. Two
things were wrong: it decoded all four channels of a pixel before asking whether
the pixel was there (line art is mostly empty), and it used one core for work
that is trivially parallel. Together, 5-6x. `bench_composite` exists so this
cannot quietly come back; run it before and after anything that touches
compositing.

**And then the same mistake, one function further out.** `bench_composite`
watches the compositor, so the compositor is what got optimised — twice. Nobody
timed the loop *after* it, which turns the flattened region into display pixels,
and that loop was the larger half by a factor of ten: 37 ms against the
compositor's 3-8, single-threaded, while the compositor had been parallel for
years. The tell was there to be read and nobody read it — a 66-tile drawing and
a 2425-tile drawing refreshed in the same time, because the work is per output
pixel and not per stroke. **A benchmark defines where you will look, so it also
defines where the next problem will be.** `bench_zoom` now times the whole path
through the real widget, which is the only reason this was found.

**Three rendering faults, one root: a memory limit making decisions nobody
attributed to it.** Reported as "strokes are jagged at 68% but crisp at 72%,
and it lags when zoomed out", which sounds like three unrelated complaints and
was in fact mostly one. `cache_step_` is meant to be `floor(1/zoom)` — sample
every pixel until there are more image pixels than screen pixels to hold them.
But the cache was also capped at twice the viewport, and the region wanted at
zoom `z` covers `viewport/z²`, so the cap bound first at exactly `z = 1/√2`.
**70.7% is 68 and 72's only interesting neighbour**, and nothing in the code
said so. Worse, the cap scales with the window, so the boundary moved: 50% in an
800x500 canvas, 60% at 1100x640, 70.7% on anything larger. The same percentage
meant different sharpness in different windows, which is exactly why the
relationship looked non-linear and unexplainable.

The lesson generalises past this bug. **A resource limit placed on one axis will
express itself as a threshold on every other axis, and it will not be labelled.**
If a quantity changes discontinuously and the constant that governs it is
nowhere near, look for a budget.

Two more, from the same investigation:

- **The blit chose its filter on `zoom_ < 1.0`.** Two errors in four tokens. The
  factor being applied is `cache_step_ * zoom_` — at step 2 and 70% zoom the
  cache is being *magnified* by 1.4 — so it asked about the wrong number. And
  the threshold was 1.0, which meant nearest-neighbour from 101% upwards, where
  it duplicates one pixel column in eleven and puts a staircase along every
  curve. Nearest is right when the pixels are the subject and wrong when they
  are not; the line is around 3x, not 1x.
- **Giving up the margin before giving up resolution is the wrong trade.** It
  reads as the careful choice — keep the picture sharp, drop the convenience —
  and between 60% and 72% zoom it ran the margin to zero, so the cache held the
  viewport and not one pixel more and *every* mouse move of a pan recomposited.
  A spent margin buys nothing back and costs again on the next move; a raised
  step costs sharpness once. The margin has a floor now.

`test_render` pins all of these as invariants rather than timings, because every
one of them was a decision the code made and a decision can be asserted exactly.
Reverting any single fix turns it red, which was checked rather than assumed.

**Caches must be sized by the window, not by the drawing.** The composite cache
was sized from the visible *image* area, so at 5% zoom it asked for half a
gigabyte, and its margin was measured in image pixels, so it grew as you zoomed
in. Both are fixed; the shape of the mistake is worth remembering.

**An integer cannot track a continuous quantity, so it will pick somewhere to
jump — and something else will pick where.** Issue #10 moved the step boundary
from 70.7% to 61% and left it there. It was still a boundary: `cache_step_` was
`floor(1/zoom)`, an integer, so sampling density had to halve discontinuously
*somewhere*, and because the cache was addressed in image pixels its size grew
as `viewport/zoom²` and needed a cap — which is what chose the somewhere. Two
symptoms, one cause, and neither goes away by tuning the cap.

Issue #11 is the fix: hold the cache at one entry per **screen** pixel rather
than per image pixel. The size then stops depending on zoom (1.59M entries at
every zoom below 100%, against 2.5M at 72% and 100k at 400%), the ratio is
`1/zoom` continuously with no cap to override it and no dependence on the
window, and there is no budget loop left in `ensureCacheCoversView` at all.
Three things fell out of it that are worth knowing:

- **A box filter on rounded boundaries is worse than the point sampling it
  replaces.** At 1.4 image pixels an entry, rounding the entry edges to whole
  pixels gives some entries two pixels and some one, and that alternation
  measured RMS 10.7 against a curve drawn at display size — where point
  sampling gave 6.7 and *weighting* the pixel an edge lands inside gives 2.1.
  The grid has to be continuous (`SampleStep` is 16.16 fixed point) and a
  sample that straddles a boundary has to be split in proportion. Half a filter
  is not half as good; it is worse than none.
- **A screen-resolution cache is only worth having if the blit is a copy.**
  Entry *e* starts at image `e/zoom`, which lands on a whole screen pixel
  exactly when `pan * zoom` is whole. Off that, Qt resamples the cache against
  itself at roughly 1:1 — pure blur, 4.2 RMS against 1.7 — for nothing. The pan
  is snapped to whole screen pixels now, which costs at most half a pixel of a
  gesture that was already made in screen pixels.
- **The measurement rounds too.** Two zooms in `bench_zoom`'s table looked
  mysteriously worse than their neighbours for a while, and it was the
  benchmark: a whole number of entries covers a fraction of a pixel less than
  the whole drawing, and the ground truth could only be drawn at a whole number
  of pixels, so the two slid a third of a pixel apart and the filter was
  charged for it. Before believing a number that only some rows are bad at,
  check what the row does differently.

The price is honest and is in `bench_zoom`: a scrubby zoom below about 62% now
recomposites on every move rather than only when crossing a step (1.7 ms → 11 ms
median, still inside a frame), and compositing far out costs more because the
block is genuinely being read rather than sampled once (at 10% zoom on a dense
four-layer drawing, 6 ms → 17 ms). Both buy the continuity; the first was
previously "free" only because you were looking at a cache built for a
different zoom, which is what jagged meant.

**The max-flow needs its trees kept.** Rebuilding the search trees per
augmenting path is Edmonds-Karp in disguise: correct, and 214 seconds on a
megapixel instead of 1.3. And repairing trees has one trap — an orphan that
still points at its old parent can adopt its own descendant, since the
candidate's chain runs back through it and still appears rooted. Cut the link
before searching. See `gridflow.cpp`.

**A green build proves nothing about the interface.** An edit that was meant to
add the "Add colour layer" button matched nothing, silently. The build passed,
all tests passed, and the button was simply absent. Screenshots caught it, and
caught a mis-encoded character and a fresh document with three undoable setup
steps. Look at the thing.

**A pen produces no double click.** Qt turns a mouse's second press into
`QEvent::MouseButtonDblClick` and never delivers it as a press, so a widget that
wants a double click watches for that event and is done — with a mouse. A tablet
event nobody accepts is promoted to a plain press and a plain release, by Windows
Ink or by Qt where the platform does not, and two taps arrive as two ordinary
presses with nothing marking them as a pair. So `QAbstractItemView`'s
`DoubleClicked` trigger never fires for a pen, and neither does any
`mouseDoubleClickEvent`. Double-clicking a name to rename it did nothing at all,
and worked perfectly with a mouse on the same machine.

`DoubleTap` counts them instead. Anything else in this program that wants a
double click needs one too, and the two routes are exclusive by construction,
which is what makes having both safe: a mouse arrives as `MouseButtonDblClick`
and never as a second press, a pen arrives as a second press and never as a
double click. Two consequences worth carrying: a `QMouseEvent` built by hand
carries **timestamp 0**, so every synthetic press in a test is the second of a
double tap at the same place unless the test says otherwise (`sendTap`); and a
tap counted this way should be matched to a *smaller* target than a mouse's
double click, since the second press is an ordinary press and everything else on
the row is still listening — the layer panel takes it on the name only, because
the visibility tick is in the same column and flicking a layer off and on is the
commonest gesture in the panel.

**A window state change and a widget resize arrive in either order.** Maximising
the window frames the canvas in it, and restoring frames it again — the canvas
keeps its zoom and pan across a resize and the pan is the image point at the
widget's *top left*, so a window made much bigger used to show the same drawing
at the same size in the same corner with new emptiness beside it. Deliberately
only those two and not every resize: the drawing surface has no edges, working
outside the canvas is ordinary here, and a view that snapped back whenever a dock
moved would take you off what you were drawing. Restoring is not symmetry for its
own sake either — a canvas fitted to a full screen is too big for the window that
comes back.

Four lines of code, and it took three attempts. The middle one is the
instructive one. Fitting on `QEvent::WindowStateChange` fits to the window
that is going away, because the widget has not been resized yet; a zero-delay
timer does not fix it, because the platform's resize is not guaranteed to have
arrived by the time the timer runs. So the next version asked `isMaximized()` on
the canvas's *resize* instead — and that does nothing at all in a real window,
where the widget is resized **before** the window state is updated, so the
question answers "no change" and no further resize ever comes.

Neither event can decide alone. The state change arms a reframe, every canvas
resize within `kReframeWindowMs` fits, and a queued call covers a maximise that
does not change the canvas's size. Fitting is idempotent, so fitting two or three
times through one maximise costs nothing; what has to hold is that the last fit
runs after the last resize, and it does whichever way round they arrive.

**And the offscreen platform picks one of those orders, so the test was green
for the whole of it.** Offscreen delivers the resize after the state change,
which is the order the broken version assumed. The test now also sends a resize
*after* the state change, and the check that actually settled it was
`shots -platform windows` against a real maximised window, reading the zoom back:
0.6575, against 0.6575 for a fit at that size. Anything about window state wants
verifying that way before it is believed.

**A path limit is per component, and a name that breaks it breaks it halfway.**
Windows allows 255 characters per path *component*, not per path — the old
260-character total does not bite, because Qt prefixes long paths internally, and
a 533-character path writes fine here. So the question for an exported name is
only ever how long one component is, and an exported name is a component twice
over: the folder, and the stem of every file in it,
`{track}_{layer}/{track}_{layer}_0007.png`.

That doubling is what makes it fail badly rather than cleanly. Measured, by
exporting at increasing lengths:

| sequence name | what happens |
|---|---|
| up to 246 | exports |
| **247 to 255** | **folder created, no frame in it can be written** |
| 256 and up | folder cannot be created either; fails cleanly |

The middle band is nine characters wide and nine is `_0001.png`. It is the worst
of the three outcomes: the sequences with shorter names are written and the long
one is not, so the export is *partial* — and since an export empties its folder
first, the previous one has gone as well. Both ends of the table are fine; only
the band matters, and it is invisible from either side of it.

The lesson generalises past this program: **a limit that a name can straddle
needs checking, not just handling.** A name that is far too long fails at the
first thing that touches the disk and is obvious. A name that is *just* too long
gets through the first thing and fails at the second, which is always later and
usually partway through the work.

**A closed editor is still a child of the view.** An item view releases its
editor with `deleteLater`, so `findChild<QLineEdit*>` hands back the dead one
until the deferred deletes run. A rename test typed into a closed editor and
passed — a rename that goes nowhere leaves the name alone exactly as a refused
rename does, so the assertion was true and meant nothing. `settleEditors` sends
the pending `DeferredDelete` events before anything looks for an editor.

**A before-and-after test is only a test if "before" is before.** The layer dock
grew by eighteen pixels when a colour layer was selected, shoving the canvas
sideways. The test written for it read the width *after* the box was showing and
then checked it did not shrink on the way out — both readings on the far side of
the growth, so it asserted that the bug did not un-happen, passed, and shipped
it. Twice, because the first fix was reported as done on the strength of that
green test. Measuring a quantity twice on the same side of the event you care
about will agree with itself perfectly and say nothing. Related: the dock is
sized from the panel's *preferred* width, so a minimum does not hold it still.

**An invalidation that empties a cache has to reach the answers in the air.**
Some of what a fill depends on is not in its key — which way marks are carried,
and a whole document being replaced by another whose drawings answer to the same
ids — and the way all of those say so is by emptying the fill cache. That was
enough for exactly as long as a solve finished inside the call that started it.
With the answer arriving later, a solve started before the emptying lands after
it, its hash still matches, and it is installed as though it were current. The
cache counts how many times it has been emptied and anything with a solve in
flight records that count, so every present *and future* way of saying "all of
that is wrong" invalidates both. A list of call sites to remember would have
been the same bug with more steps.

**Anything that rode on a re-solve was riding on it being synchronous.** The
layer panel's tooltip was refreshed by the solve that changing the colour
sources happens to trigger. Invisible while the solve finished inside the paint
that started it; a stale panel the moment it did not. What a row says about a
layer is about the layer, so it is said when the layer changes. Expect more of
these: anything that was correct only because two things happened in the same
call stack.

**A test that constructs a failure will be repaired by the fix for it.** Several
tests build a mark that lands in the wrong place, and moving marks with the
drawing makes them land right — so they went red on a change that was working
perfectly. They now turn the moving off and say why. The tell is a *whole
fixture* going green-to-red on a feature that is supposed to improve exactly
that case; read what the test was for before believing the failure.

**A cache key made of revisions was going to lie, not thrash.** The CTG fill
cache was keyed on the cel holding the scribbles, which was a bijection for
exactly as long as one drawing had one scribble cel. The design notes predicted a
shared slot would thrash; it would have served wrong fills, because `inputs` is
mixed from cel revisions and revisions collide freely — **every cel in a project
straight off disk is at revision 1**. Two drawings inheriting one scribble with
equally-worn line art would have swapped answers. The key is `(drawing, layer)`,
and `inputs` names the scribble *cel* and not only its revision, because
reordering changes which cel is read and moves no revision anywhere.

**A rectangle built from tile coordinates remembers what you erased.**
The bounds a cel derives from its tiles — `drawnBounds`, in `tile.h`, and called
`celBounds` in these notes and in issue #23 by a name it has never had in the
code — took the bounding box of a cel's tiles, and erasing empties a tile
without releasing it — so the solve region went on describing a mark that was no
longer there. That rectangle picks the solve resolution, so a stray scribble made
and rubbed out left every later solve on that drawing permanently coarser than
before the scribble existed. Invisibly: the region is not something you can see.
Reported as "erasing does not put the canvas back", and two better-sounding
theories were measured and dropped first — eraser residue, which the hard label
write makes impossible, and the largest-first solve order, which is deterministic
given the same seeds.

**And the grid lets go of it now**, which is the other half and was left for
later at the time. `Document::endCommand` drops every tile the command emptied,
so absent and fully transparent stop being two states that mean the same thing.
Three points about where it is done. It is at the *end* of the command rather
than at the write, because a stroke crosses one tile many times and only the
whole command knows when the writing stopped. Undo needs nothing added: the
journal already recorded what the tile held before the first write, and
`swapTile` is its own inverse whether or not either side is there. And it is
deliberately not a revision bump — no pixel changed, and bumping would throw
away a CTG fill that is still correct.

An entry that then records no difference at all is dropped from the command with
it, so rubbing out over blank paper is no longer an undo step that puts an empty
tile back. This is also the cheap end of [#23](https://github.com/S-poony/Animage/issues/23):
the tiles a command retains are what the history costs.

**The confidence signal the design notes propose does not work, and the
measurement is the only thing that says so.** Scoring a mark by the fraction of
it the solver labelled with the mark's own colour comes out at exactly 1 across
every case in `test_ctg`: a seed is only overruled when severing it beats
isolating it, which needs a mark that is nearly all edge. It is kept in
`CtgFill::confidence` and documented as a dead end so it is not derived a third
time. `spread` separates further — 8.3, 17, 23, 65, 188 for marks that landed
properly against 1.00 for a mark carried off its shape — and not far enough to
carry a flag either, which took a second round of measurements to establish. See
"the flag that had to come out" above. A cut that encircles the scribble is
exactly what `fr/lazybrush-et-calques-ctg.md` §5 calls **Raccourcis**, so the
number does name a failure the research had already named; naming it is not the
same as being able to detect it.

**And both numbers have to come off the solver's labels, never the finished
fill.** A mark wins its own pixels in the fill whatever the solver decided, so
read back from the fill every mark is perfectly placed, always. There is a test
pinning it, because it is the mistake the next person will make.

**The flag that had to come out.** The timeline flagged drawings whose carried
marks had filled nothing but themselves, from a whole-track audit that judged
every drawing coarsely so the flag could say "go and look at drawing 34" rather
than "you are standing on a bad one". It was reported as firing on drawings whose
colour was perfectly good, often enough to be worth less than nothing, and it was
removed rather than tuned. What is left is `CtgFill::spread`, the measurement,
which `bench_carry` reports.

Two things were measured before deciding, and neither found the reported case:
the coarse audit does not judge differently from the fine solve (45.2 against
62.1 on the same drawing, and the same verdict), and a mark that fills a small
region snugly — which is what "scribble bigger to bridge bigger holes" asks
people to draw — bottoms out at 1.96 against a floor of 1.5. Close, but not the
reported symptom.

What the measurements do say is that the number cannot carry a flag at all.
A mark that filled nothing measures exactly 1.00; a mark that snugly fills a
small region measures 1.96 and a thinner region would measure less; and a mark
that filled *the wrong region* measures **higher** than one that filled the right
one, because `spread` is an amount and not a correspondence. A threshold in a gap
half a unit wide, with a case on the wrong side of it that the number actively
rewards, is not a signal with a bad constant. It is the wrong quantity.

So the next attempt should not start from a threshold on one drawing. What
"wrong" means here only exists by reference to the drawing a mark came from —
the same correspondence rung three of
[scribbles-through-time.md](scribbles-through-time.md) has to build anyway — and
until that exists, no flag is better than one that cries wolf. That is not a
consolation: a flag people learn to ignore also teaches them to ignore the next
one.

The audit went with it, since nothing else read it. What survives is the shape of
it, in `CtgSolver`: a second priority for work nobody is waiting for, and a solve
that stops after the labelling and keeps no tiles. Both are tested and both are
exactly what a whole-track pass needs when there is something worth passing over.

One thing it taught that outlives it. **A flag read from a cache only reports
where you have been.** The first version took its flags from `ctgFillFor`, and a
fill exists only for a drawing somebody has visited — so they lit up behind you
as you played through a shot. That is not a weaker version of such a feature, it
is the absence of it, and any future one has to be computed for drawings nobody
has opened.

**A Qt stylesheet with no type selector styles the widget's tooltip too.** A
swatch styled `background:#c00` handed its own colour to its own tooltip, and the
slashed "no colour" swatch drew a red streak through the text of its. Name the
type: `QPushButton { ... }`. Reported by the user; an offscreen `grab()` does not
contain the tooltip, so it cannot be caught by screenshot.

**A queued signal that rebuilds a list deletes rows out from under whoever holds
one.** Reporting a finished solve by calling `rebuildLayerList` crashed
`test_canvas`: a solve runs inside a paint, so the report has to be queued, and
the rebuild clears the tree. Every `QTreeWidgetItem*` had quietly become valid
only until the next event-loop turn. A finished solve changes two words and a
colour, so it changes two words and a colour, in place. gdb found it in one run
after two wrong theories — the same lesson as the two crashes above, learned
again.

**Look at the thing, and check the harness is looking at it.** A screenshot
caught the "None" control being a second red-slashed swatch beside the one that
already showed a red slash — two identical patches, one a readout and one a
control, with nothing to say which. The same screenshot run had silently failed
to add the colour layer it was testing, because it looked for a `QAction` where
the button is a `QPushButton`, so the first two pictures were of nothing at all.

**A test fixture can build a shape that is not the shape you meant.**
`drawGappedBox` takes the gap as coordinates, and handing it two outside the box
gives a bottom wall running six hundred pixels past the corner — not a closed
shape at all. The printed numbers looked perfectly reasonable.

**Four tools spelled as three independent flags, and a handler that kept half of
a pair.** Which tool had the pen was two booleans and an optional — eight
combinations for four states — and the window kept them in step by writing the
same triple at three call sites, in two different orders. One had already
drifted: entering a transform put the lasso down and said nothing about the
eraser. Pressing E and then Ctrl+V therefore left a checked Transform button over
a live eraser, so Alt+right-drag went on resizing the eraser, the ring drew at
its radius, and the toolbar's size box showed its number until the transform
ended and the figure jumped.

Nothing worse happened only because all five dispatch sites test the transform
first — an ordering obligation carried by five call sites and enforced by
nothing. **State that is mutually exclusive belongs in one value, so that the
combinations nothing has an answer for cannot be written down at all.** The
navigation gestures were the same shape and had not yet bitten: three flags set
one at a time by a strict chain and cleared together, with "is one of them
running" spelled out at four call sites, two of which were re-deriving by hand
what `continueNavigation` already returns as a bool. Both are enums now, and the
test that pins the first one fails on the commit before it.

## How to work on it

Everything in `src/core/` is free of Qt and can be tested headlessly. Everything
in `src/app/` is Qt. Keep that line: it is why the model has real tests — and it
is why the CTG solver, threads and all, is in `core` rather than in the window
that uses it. The hard part of running a max-flow somewhere else is the queue,
the superseding and the cancelling, and all of that is testable without a
display; what is left in the widget is a timer and a map.

The five paths through it are in
[how the program fits together](#how-the-program-fits-together), near the top,
because they are what you want before changing anything rather than after.

```bash
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH   # MSYS2 UCRT64, from PowerShell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/bench_composite     # timings, not a test -- including a whole CTG solve
./build/tests/bench_zoom -platform offscreen [dir]   # the whole display path
./build/tests/bench_save          # save, incremental save, open
./build/tests/bench_carry         # how far a mark survives being carried
./build/tests/bench_transform     # what moving a drawing costs, and what it costs the history
./build/tests/bench_playback -platform offscreen   # what playback drops, coloured and not
./build/tests/shots [--list] [name]   # pictures of the interface, one per situation
```

`shots` is the one that is not a number. It drives the real window through a
list of named situations and writes a PNG each, into `build/shots/` unless told
otherwise; `--list` says what they are and a bare word runs only the ones whose
name matches, so looking at one thing costs one picture rather than twenty-one. It
runs offscreen without being asked to. **Add situations to it freely** — nothing
depends on any of them being there, which is the point. See
[looking at the interface](#looking-at-the-interface).

`bench_carry` is a measurement rather than a stopwatch, and it is the one to run
before changing anything about carrying marks. It moves a shape a known amount
per drawing, marks only the first, and reports what the fill did on the rest:
how much of the region took the colour, how much of the world outside it did,
what `spread` said about it, and how far the solve decided the drawing had moved. Every case runs twice, with the marks left where they were drawn and with
them following the line art, so the two are read side by side.

`bench_save` reports a full save, a full re-save, an incremental save with
nothing changed and one with a single drawing touched. The last is what autosave
actually costs and is the number to watch: if it starts tracking the size of the
shot rather than the size of the change, something has stopped carrying files
forward.

`bench_playback` is the one that reports a **count rather than a time**, and the
reason is the whole of why it exists. Playback works its slot out from a clock
(`onPlaybackTick`), so a paint that overruns does not make the take run slow — it
makes the frames underneath it never appear. A pan that stutters reports itself;
a playback dropping every third frame looks like the *drawing* is wrong, and the
animator goes and fixes a breakdown that was fine. Judging timing is the whole
purpose of playback, so the one thing that can quietly corrupt that judgement
should not be the one thing with no instrument on it.

Two things about it before changing it. It drives the whole `MainWindow` and not
the canvas — a playback frame is a slot change, `refreshLayerFlags` and
`syncStatus`, a *full-cache* repaint (`setFrame` calls `refreshAll`, so playback
never gets a partial one) and the playhead, and timing only the canvas would be
the `bench_composite` mistake one function further out. And it runs every case
twice, line art and coloured, because the timing is decided before the shot is
coloured: the uncoloured pass is the one that has to be fast, and the coloured
one is a shot being reviewed rather than judged. The coloured row prints its fill
coverage beside it, because what playback composites there is the fill — a
scribble with no closed region to win is cut close around itself by the hard rim
and fills almost nothing, and that fixture would report that colour is free.

**What it says today**, which is one finding and not the one the queue expected:

| | frame | shown at 24 fps |
|---|---|---|
| HD, 2 tracks, line art | 13.2 ms | 48 of 48 |
| HD, 4 tracks, 96 frames | 15.7 ms | 96 of 96 |
| 4K, 2 tracks, line art | 53.6 ms | **37 of 48** |
| 4K, 2 tracks, coloured | 68.7 ms | **30 of 48** |

One run, and the millisecond columns move by a few per cent between runs — the
4K line-art frame has measured anywhere from 52 to 59 ms. Read the differences
between rows and not the digits, which is also why the two claims below are
stated as ranges rather than as the ratios one run happens to give.

**Track count barely registers and output pixels are the whole story.** Two
tracks and 1612 tiles against four tracks and 6452 moves the canvas half by one
to three milliseconds, because `compositeScene` is one flat list and an empty
tile is skipped before a channel is read. The 4K canvas widget is 4.8x the HD
one's area and costs four to five times as much — the same per-output-pixel
property the traps record from the other end, where a 66-tile and a 2425-tile
drawing refreshed in the same time. So at 4K between a quarter and two fifths of
the frames never reach the screen, silently, in the mode where somebody is
judging timing.

What to do about it is issue
[#30](https://github.com/S-poony/Animage/issues/30), argued in
[playback-resolution.md](playback-resolution.md), which is a plan and not a
description: stop repainting a held frame first, which on twos removes the
problem outright and costs no sharpness; and then, for the shots on ones where a
hold cannot help, composite fewer entries while playing, earn the reduction from a
measurement rather than applying it unconditionally, and decide it at a loop
boundary so the picture never changes sharpness mid-take.

**And the colour cache cannot hold a shot.** A fill covers the canvas at full
resolution, so the bound works out at about 2000 tiles: 48 of 48 fills survive an
HD shot of 24 drawings, 62 of 192 survive four tracks of 48, and **20 of 48**
survive at 4K. What playback then shows is whichever fills are still there. The
solves it provokes are counted rather than timed, and the count is 6 in two
seconds at HD against **0 at 4K** — where the same mechanism demonstrably works,
so a 4K fill either never finishes or is superseded before it can. Either way the
colour does not arrive. That is worth knowing before item 4 is read as the answer
to it: the max-flow is staying on the CPU, so a GPU compositor does not touch
this.

`bench_zoom` drives the real `CanvasWidget` across the zoom range and reports,
per zoom, the step and margin it chose, what a full refresh costs against the
compositing inside it, and what a pan and a scrubby zoom cost per mouse move —
median *and* worst, because the median hides the whole effect. Given a directory
it also writes comparison sheets, including one rendered through the real widget.
Run it before and after anything that touches the canvas.

`test_canvas` drives the real widgets offscreen and can send tablet events; it
is where interface bugs get caught. A crash writes `animage-crash.txt` beside
the executable, with a stack that `addr2line` decodes:

```bash
addr2line -e build/src/app/animage.exe -f -C 0x14000a130
```

Add the PE image base (`0x140000000`) to the offsets in the report.

### The icon, and why the build does not draw it

The mark is drawn in `packaging/animage.af` and everything else in `packaging/`
comes out of it, in two steps, both by hand and both **committed rather than
generated during the build**:

```bash
# Only when the drawing changes. Otherwise the tree already has all of this.
#   1. open animage.af in Affinity Designer, export the artboard as animage.svg
#   2. then, and commit what it writes:
python packaging/make-icons.py
```

Nothing reads the `.af` — not the build, not `make-icons.py`, not the program.
The SVG is where the automated part starts and is sufficient on its own.

The `.af` is there because it is the file somebody would actually change the
logo in, and a project that ships only the export has lost the drawing. It is
closed and binary, so nothing reads it automatically, git cannot diff it, and
two people cannot edit it at once — which is the price of keeping the real
master rather than a flattened one. Anyone without Affinity edits the SVG and
says so in the commit, at which point the `.af` is behind and the next person to
open it needs to know that.

**The artwork runs past the artboard on every side, and that is the drawing and
not an accident.** The paths span roughly −923 to 5110 across a 3544-wide
square, so what the icon shows is a crop. The construction puts its vanishing
points outside the frame, which is what keeps the perspective sound and the
thing editable later; pulling the shapes inside to suit the smallest icon would
give that up. The price is paid at 16 pixels, in the title bar and nowhere else
that matters — the taskbar takes 24 or 32, and everything from 48 up reads
plainly. Judged legible enough at 16 and not worth a second export. Don't
"fix" the crop without knowing that it was looked at.

Step two is Inkscape, a deliberate dependency on a program a build machine is
not promised to have — which is precisely why it runs by hand and its output is
in the tree. Qt would have been the obvious renderer and cannot be used: `QtSvg`
implements SVG Tiny 1.2, which has no `<mask>`, and the mark has one. Rendered
through Qt the masked stroke silently disappears, so a build-time rasterisation
would have shipped a subtly wrong icon on the platforms nobody was looking at.

The icon then has to arrive in four unrelated places, and getting three of them
right leaves the fourth generically iconed:

| | reads | set by |
|---|---|---|
| The window, the dialogs, Alt-Tab | `:/icons/animage-*.png`, compiled in | `QApplication::setWindowIcon` in `main.cpp` |
| Explorer and the taskbar | `animage.exe` itself | `packaging/animage.rc`, an `RC` source on the target |
| Finder and the Dock | `Contents/Resources/animage.icns` | `MACOSX_BUNDLE_ICON_FILE`, plus the `.icns` as a bundle source |
| The Linux desktop | the icon theme, by the name in `animage.desktop` | the AppImage step in `ci.yml`, installing the SVG under `hicolor` |

The first two are separate on purpose and neither substitutes for the other: a
running window asks Qt what its icon is and never looks at the executable, and
Explorer reads the file without starting it. The PNG ladder is eight sizes
rasterised at the size each is used at, rather than one large image Qt scales
down, because 512 pixels squeezed into a title bar is a smear.

`enable_language(RC)` is in the top-level `CMakeLists.txt` and has to be: Visual
Studio generators enable it themselves, Ninja and MinGW do not, and without it
the `.rc` is handed to the C compiler.

## What I would do next

A queue, and only a queue. Things that were on it and are now built have moved
into sections of their own — this list kept growing entries that existed to say
they were finished, which made it longer to read and harder to trust. What has
come off it since the first build, with where the reasoning went:

| | |
|---|---|
| EXR export | "export writes 16-bit PNG and EXR", and the section after it |
| Several tracks (#1), overwrite (#9), track ends (#20) | the two sections on tracks |
| Carrying marks, and moving them (#3, #6, #7) | "colour through time", parts one and two |
| Freeing emptied tiles | "a rectangle built from tile coordinates remembers what you erased" |
| Lasso and transform, all four phases | "moving a drawing" through "what a transform costs" |
| The shortcut table, and all of #14 | "what the keyboard does, and when" |
| One place deciding the pointer (#27), the eraser (#4), the resize ring (#5) | "what the pointer says" |
| A screenshot target (#28) | "looking at the interface" |
| Capping the undo history (#23) | "what the history is allowed to cost" |

1. **TIFF export**, which is the half of the format list still missing. It is
   the **compatibility** deliverable and not the lossless one — EXR is the
   lossless one and is built — and keeping that straight is what stops it being
   argued about twice.

   It was raised that TIFF is the more common deliverable in 2D animation, and
   that is true -- TVPaint and Harmony both write it and scanned-drawing
   pipelines have used it for decades. Three things about it are easy to get
   wrong, all measured against what it *can* do rather than what it usually
   does:

   - A TIFF **can** hold our pixels losslessly -- `SampleFormat = 3` and
     `BitsPerSample = 16` is half. `why-our-own-formats.md` used to say
     otherwise and has been corrected.
   - But half-float TIFF is a thinly supported corner. Writing it produces files
     a lot of applications will not open, and writing 32-bit float instead to be
     safely readable doubles the bytes for data that is natively 16-bit and buys
     no precision whatever.
   - And it is not the cheap option it sounds like. TIFF needs libtiff or Qt's
     `qtimageformats` add-on, and Qt cannot write TIFF in this build --
     `QImageWriter::canWrite("tif")` is false. The nearly-free version is Qt
     writing *integer* TIFF, which is exactly as lossy as the PNG already
     written. The free TIFF is the lossy TIFF.

   So it is a third entry in `exporting::Format`, an extension in
   `extensionFor`, one more in `isFrameOf`'s list and an item in the dialog's
   combo box. The writer converts exactly as `toSrgb16` does; that is the point
   of it.

2. **Rung three of scribbles that move**: one transform per *region* rather than
   one per drawing, from the previous fill's regions. Read
   [scribbles-through-time.md](scribbles-through-time.md) first — rungs one and
   two are built and measured, and the note now records both what they buy and
   the one way rung two fails, which is by locking onto the wrong alignment when
   the ink repeats. Rung four is the paper written for this exact problem
   (Sýkora, Dingliana & Collins, NPAR 2009) and is what to read before designing
   anything past three.
3. **A flag that means something.** There was one, built on `spread`, and it came
   out — see "the flag that had to come out". Anything that replaces it has to
   clear a bar the old one did not: "wrong" only exists by reference to the
   drawing a mark came from, so it needs a correspondence between regions on two
   drawings, which is what item 2 would produce. Every proxy tried on paper —
   area ratio, region overlap — misfires on fast movement, which is exactly when
   carrying is most likely to be wrong *and* most likely to be right. And it has
   to be computed for drawings nobody has opened, which is what the audit did and
   what `CtgSolver`'s second priority is still there for.
4. **GPU compositing**, if `bench_playback` says it is worth it — not
   `bench_composite`, which watches the half that is not the problem. What it
   says today is that HD is comfortable at any track count and 4K drops between
   a quarter and two fifths of its frames, so this is a 4K deliverable and not a
   general one. It does not answer the coloured case at all: the max-flow stays
   on the CPU, and what breaks there is the fill cache, not the compositing.
5. **The rest of the open issues.** One came out of designing lasso and
   transform and is one small piece with the groundwork already under it:
   transforming a layer across time
   ([#25](https://github.com/S-poony/Animage/issues/25)), which wants `LayerPass`
   widened from an offset to an affine — the same widening the live preview
   already needed.

   The rest are older: showing a track's end behaviour on the track itself (#22),
   deleting every layer of a drawing (#2), a non-modal colour panel (the parked
   half of #8), and this file being hard to navigate
   ([#29](https://github.com/S-poony/Animage/issues/29)).

## Three things to be careful of

**The undo model rests on cel ids never being reused.** Deleting a drawing and
then undoing a stroke made on a drawing that shared its cel only works because
of that, and because the history counts as a reference on a cel. Both are
tested; neither is obvious from reading the code in one place. What is new is
that the history is finite: a step past the budget is dropped and its cels are
collected, so "undo can always bring it back" is true only as far back as
[the history is allowed to cost](#what-the-history-is-allowed-to-cost).

**Layers belong to the track and timing belongs to the image.** This is the
project's central bet and the code depends on it everywhere: adding a layer must
touch no image, and holding a drawing must allocate nothing. There are tests
pinning both. If either starts failing, something has misunderstood the model.

**"Track" and "timeline" are not the same word.** A `Track` is one stack of
layers with its own time; the timeline is the scene's shared time axis and the
panel that shows it. The struct was called `Timeline` until the two meanings
were separated — if you find the old name anywhere, it means the track.
