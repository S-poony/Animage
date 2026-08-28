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
| [Duplicating a track](#duplicating-a-track) | every id inside it is new, and one of them would not have looked wrong |
| [Restacking by dragging](#restacking-by-dragging) | layers and tracks, one gesture, and where the panel had been scrolling to |
| [Naming a track or a layer](#naming-a-track-or-a-layer) | renaming a row where it is, and what a name is allowed to be |
| [Colour through time](#colour-through-time) | a mark carried to a drawing that has none |
| [Colour through time, part two](#colour-through-time-part-two) | and moved to where that drawing went |
| [Colour through time, part three](#colour-through-time-part-three) | one number became a field, and what four shots said about it |
| [What the push step is allowed to see](#what-the-push-step-is-allowed-to-see) | why rung four sheared a rectangle, and what it cost to stop it |
| [**The one constant in rung four**](#the-one-constant-in-rung-four-and-what-bounds-it) | `kAlongKeep`: the number to turn, and the two failures that bound it |
| [A mark the program moved, and did not land](#a-mark-the-program-moved-and-did-not-land) | dropping a stray, and why no benchmark here could see one |
| [**Scoring a rung against a hand**](#scoring-a-rung-against-a-hand) | `bench_hand`: a shot somebody coloured, as the thing to beat |
| [What a track does past its last drawing](#what-a-track-does-past-its-last-drawing) | holds, shows, and the difference |
| [What the keyboard does, and when](#what-the-keyboard-does-and-when) | the shortcut table, the first mode, and changing a key |
| [A straight line](#a-straight-line) | one key, and a stroke that writes nothing until it is let go |
| [Moving a drawing](#moving-a-drawing) | the transform tool |
| [The lasso](#the-lasso) | and what a selection is here |
| [Copy, cut and paste](#copy-cut-and-paste) | which is a float from the clipboard |
| [What a transform costs](#what-a-transform-costs) | measured, then made to cost less |
| [Transforming a layer through time](#transforming-a-layer-through-time) | every drawing at once, and the two numbers that decided its shape |
| [**Importing a picture**](#importing-a-picture) | a layer with no cels, and the one gesture that stores instead of baking |
| [**Importing a sequence**](#importing-a-sequence) | which frame a drawing shows, a bounded cache, and a decode the paint asks for |
| [**Importing a soundtrack**](#importing-a-soundtrack) | its own list, two selections, a row you can move and crop, and the one line lipsync rests on |
| [What a pan costs](#what-a-pan-costs) | the onion skin rebuilt from nothing every 64 pixels, and the cache that scrolls instead |
| [What a commit does to a line](#what-a-commit-does-to-a-line) | one filter chosen on the wrong quantity, and what it did to a rim |
| [What a commit is allowed to cost](#what-a-commit-is-allowed-to-cost) | a budget in tiles, and a box that goes red before Enter does anything |
| [What the pointer says](#what-the-pointer-says) | one place deciding it, in the canvas and in the timeline |
| [**Looking at the interface**](#looking-at-the-interface) | `shots`: a picture of the program, per situation, and yours to add to |
| [**Asking Qt a question directly**](#asking-qt-a-question-directly) | `dock_probe`: plain Qt with docks in it, for "ours or theirs?" |
| [**What taking Qt Multimedia costs**](#what-taking-qt-multimedia-costs) | the audio spike: all three packagers bundled it, and scrubbing needs none of it |
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
library and tests only` is it working. Every core test passes under it. Two
things to know: the binaries only run inside that shell, because MSVC links ASan
dynamically and `clang_rt.asan_dynamic-x86_64.dll` lives beside the compiler — a
test dying instantly with `0xC0000135` is a missing DLL and not a bug you just
wrote. And MSVC has no UBSan, so undefined behaviour is still the Linux job's to
catch. The `sanitizers (windows, core)` job in `ci.yml` runs exactly this, with
its own Developer Command Prompt rather than an action's — and with
`-DANIMAGE_REQUIRE_SANITIZERS=ON`, which is the flag that turns the quiet
fallback above into a refusal. Both sanitizer jobs pass it. A run that cannot
link the sanitizer it is named for now fails to configure instead of going green
having proved nothing, which was issue #80's real worry.

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
on a CTG layer so the mark is a hard label rather than paint, opens a command
directly (`Document::beginCommand`, closed in `endStroke` — a stroke outlives the
statement that starts it, so not the `ScopedCommand` that cut, paste and
transform use), and hands the stroke to `Brush`.

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
any colour that has gone stale and computes none of it. If the onion skin has
moved or changed, `rebuildOnion`/`paintOnion` refresh its own screen-sized buffer
first, because the sRGB loop reads one onion entry per cache entry. Then either
the whole cached region or the accumulated dirty rectangle goes through
`refreshRegion`. With no onion that is one `Compositor::compositeScene` into
`scratch_`; with one, the layer stack is split at `onionSplit` and composited in
two pieces — `scratch_` above the drawn layer, `under_` below it, which is issue
#77. Either way the conversion loop then merges paper, `under_`, onion skin and
drawing to sRGB in `display_` across a short-lived thread pool. That conversion
loop is the larger half of a refresh by a wide margin — see the traps. Finally
`display_` is blitted through one `QRectF`, nearest-neighbour only above 3×
magnification, and the canvas frame is drawn over it.

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

A worker runs `solveCtgJob`: estimate how far the marks' drawing has moved since
they were made (`estimateCtgWarp`, matching the two drawings' ink coarse-to-fine
with `estimateCtgShift` underneath; the answer can be a per-mark field, not just
one translation), move the marks through it once (`ctgCarriedMarks`), flatten the
ink into a barrier (`ctgBarrier`), read the moved marks into seeds, and run the
max-flow (`solveLazyBrush` over `GridFlow`). What it keeps is the labelling
itself — one label per solved cell, the palette, and the marks it was solved
from — and not a picture of it. A 16 ms poll on the canvas collects the result,
puts it in the cache, marks everything dirty and emits `colourChanged`;
`MainWindow` refreshes the timeline, the layer panel and the status bar from
that one signal.

**The fill has no pixels, so somebody has to work them out.** `ctgFillPixel`
is the reference and what tests read; `ctgFillSpan` is what the compositor
calls, a run of a row at a time, and `ctgFillExtent` says which part of a row
can hold an answer at all — which is how a fill gets back the shortcut an absent
tile gives a grid, and it is not optional: without it a colour layer costs the
area of the viewport rather than the area of the fill. All three are pure
functions of what the fill stores, because `compositeGrids` runs its bands on
several threads over one pass list and every one of them reads the same fill.

**Nothing bounds a fill.** Not the canvas, which used to: a shape running off
the frame is coloured out there too, under the veil, and a ball animating
off-screen keeps its colour instead of losing it at the frame line. What bounds
a *solve* is the drawn bounds of the marks and the ink plus a tile of margin,
and a cell budget that coarsens it until it fits; the labels are extended
outwards from there by clamping, which is exact because there is no line art
outside the drawn area for a cut to pass through. Export is unaffected and
always was — it composites the canvas. The cost is that ink far off the frame
coarsens the whole drawing, which is issue #61.

The compositor draws whatever fill is in the cache and never starts a solve —
`Document::ctgFillFor` is const for exactly that reason. A colour layer reaches
it as a `LayerPass` carrying a fill rather than a grid, which is the only pass
that is not a picture.

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

The swap is `swapIntoPlace`, and its rule is that **nothing on any path out of
it deletes the only copy of anything**. On every failure either `folder` holds a
complete project or the error names every folder that does. That is issue
[#42](https://github.com/S-poony/Animage/issues/42): the restore used to be
attempted and its answer discarded, so when it failed too the project sat under
`folder.replaced-<ms>` with nothing naming it, and the tidy-up then deleted the
copy that had just been written — the only copy of the work being saved. Both
are kept now, and the message says where they are.

The kept copy is moved out of the scratch name before anything else, and that
part is not cosmetic: the scratch path is named after the *process*, so it is
the same path for every save in a session, and the next save's first act is to
clear it. A rescue copy left there would be deleted two minutes later by
autosave, without a word.

`encodeCel` drops fully transparent tiles, sorts the coordinates so an unchanged
drawing encodes to identical bytes, and writes only the occupied span of each
row — which is the difference between handing the compressor 3.3 MB and handing
it 457 MB. Those are the compressor's *input*, not file sizes: on disk the same
shot is 6.1 MB against 12.0 MB, and
[why our own formats](why-our-own-formats.md) tabulates both columns. Worth
keeping straight, because quoting the input as a file size makes the format look
about 150× better than it is.

**In.** `load` reads `scene.json` into a **document of its own**, fills every cel
it names, and only then assigns over the open document. A project with one bad
cel in it therefore cannot leave you with half of it and none of what you had.

A cel that cannot be read costs that drawing rather than the project — issue
[#41](https://github.com/S-poony/Animage/issues/41) — but **only for a caller
that passes a `Damage*`**. Without one, `load` refuses exactly as it always did,
and that is the design and not a leftover: an empty cel is indistinguishable
from one that was erased on purpose, so opening what could be read is only safe
where somebody is going to *say* which drawings are gone. A missing cel file
counts the same as a damaged one, because a project is a folder and a sync that
brings back all of it but one file is at least as likely. A `scene.json` that
will not parse still refuses outright; there is no document without it.

**The window opens a damaged project as a rescued copy**, beside the original
and named `rescued_<name>`, and the damaged folder is never written to. That is
what stands between the animator and the worst outcome here: a project opened
with holes in it is still pointing at a folder, and autosave fires two minutes
later. Writing back would overwrite a damaged cel — which may still hold most of
a drawing, and which a more forgiving reader could one day recover something
from — with the empty cel that stood in for it. The rescued copy is written
immediately rather than left to the first autosave tick, because a document
straight off disk has nothing to autosave and would otherwise not be on disk at
all until somebody drew something.

The save state from a damaged load is thrown away rather than carried. A cel
that could not be read is recorded at the revision of the empty cel standing in
for it, so the state claims a file matches pixels that file has never held, and
carrying it forward would propagate the damage instead of noticing it.
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

## Duplicating a track

**Track ▸ Duplicate track**, and the copy lands directly under the one it came
from. It follows the highlight like the rest of that menu, so on a soundtrack
row it duplicates the soundtrack — which is also, today, **the only way a scene
gets a second one**. The model has been a list since audio arrived and the row
loop has always walked it; what was missing was any way to put another in, and
duplicating the one you have is the obvious one.

**Every id inside a track is new, and that is the whole of the work.** A track's
insides are named by ids that mean something only within it, and three of them
point at each other:

| | names | so a copy must |
|---|---|---|
| `Track::slots` | image ids | be rebuilt from the new ones, holds and all |
| `Image::cels`, `Image::source_frames` | layer ids | be re-keyed onto the new layers |
| `Layer::ctg_sources` | the line-art layers a colour layer is cut against | point at the copy's own line art |

**The last one is the one that would not look wrong.** A copy whose colour
layers still named the original's line art draws perfectly until somebody
redraws one of the two tracks, and then the wrong fill re-cuts. It is what
`aDuplicatedColourLayerIsCutAgainstItsOwnTracksLineArt` pins, and taking the
remap out reddens that test and nothing else — which is exactly what makes it
worth having.

Drawing numbers are **kept rather than renumbered**: a number is unique within a
track and this is a whole track, so the copy's cards read the same as the
original's. Cels are copied rather than shared, which is what makes it a
duplicate and not a second view, and is the one part of it that costs memory —
the same cost `duplicateImage` pays, once per drawing.

A soundtrack's copy is cheap by comparison: a file name and four numbers, no
cels and no ids inside it. `Document::duplicateAudioTrack` does not copy the
decoded samples, because `core` does not decode — `MainWindow` hands the clip
across afterwards rather than paying for a second decode of a file it already
has in memory.

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

### What the layer panel says it is showing

The dock is titled "Layers" and the column beneath it was headed "Layer": the
same word twice and no information. What was missing was whose layers these are.
Another track's layers can be named exactly the same — every track starts with a
"layer 1" — so a panel that names neither is a panel you can edit the wrong track
in without noticing.

The fact was not absent from the window. The status bar had it all along, eight
readings along and the width of the window away from the rows it qualifies, and
the timeline shows it as a highlighted row. Both are far from the decision, and
far from the decision is the same as absent for something you have to remember to
check. So the header carries the current track's name now, and the two read as a
pair: the dock says what the panel is, the header says whose.

`rebuildLayerList` is where it is set, and that is the whole of why this is one
line rather than a hunt: it is the function every path that changes the current
track ends in — clicking a row in the timeline through `setCurrentTrack`, Add and
Delete track, undo, Open, New — and it is also where a *rename* of the track
arrives by both of its routes, the gutter editor through `documentChanged` and
Track ▸ Rename track through `refreshEverything`.

Two things were checked by looking rather than assumed, with a `shots` situation
added and deleted again:

- **A long name does not shove the canvas sideways.** That is not a hypothetical
  worry in this panel — the Colour layer box appearing once took the dock from
  274 px to 292 and moved the canvas eighteen pixels. It does not happen here
  because column 0 is `QHeaderView::Stretch`, so it takes the width it is given
  rather than asking for more. A name at the sixty-character cap left the dock
  and the canvas exactly where they were.
- **The second column is usually not there at all.** "Marks" is
  `ResizeToContents` and a track with no colour layer has nothing to put in it,
  so most of the time that strip holds one word. Which is why replacing it cost
  nothing.

The tooltip says `Layers of "name"` rather than repeating the name alone,
because it has two jobs: a name too long for the column is elided, and a bare
name in a header could be read as one more layer rather than as the track the
layers belong to.

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

**A rename can be given up rather than finished**, which is what Escape does and
what a window on its way out does: `LayerList::abandonRename` and
`TimelineWidget::abandonRename` put the editor away and leave the name where it
was. `~MainWindow` calls both, and has to — an editor still open when the window
is destroyed finishes itself on the way down, against a document that has already
been destroyed. That is [a trap](#the-traps) with a section of its own, and it is
the one thing to read before changing anything about when these editors close.

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

**Which editor is the live one is asked of `LayerList`, not of `findChild`.**
Closing one only `deleteLater`s it, so it stays a child of the viewport until the
event loop next runs and `findChild<QLineEdit*>` keeps answering with it — so a
test that opened a second rename without settling the first finished the *old*
editor and left the new one open. `finishRenameForTesting` uses the pointer the
delegate reported instead. A test that wants the editor gone rather than merely
closed still has to settle it; `settleEditors` is `processEvents` followed by
`sendPostedEvents(nullptr, DeferredDelete)`, because plain `processEvents` does
not deliver that one.

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
The shift lives in `Document::ctgShiftAt` — `ctgCarryAt` since part three turned
the number into a field — written by every solve, read by the
compositor's marks pass and by `celForWriting`. Any fourth thing that shows
marks must read it too.

And it has to be written before it can be read: two callers were skipping the
solve for a layer showing its marks, on the grounds that its fill would not be
drawn. See "what a view that skips the solve also skips" below.

## Colour through time, part three

**A carried mark's position stopped being a number and became a field, and the
field is the paper's lattice.** Rung four — the as-rigid-as-possible lattice of
[scribbles-through-time.md](scribbles-through-time.md) — is built and is **the
default**. Rung three, one translation per region, and rung two, one for the
whole drawing, are still there and still reachable: `CtgSettings::carry` exists
so that a benchmark can ask three rungs the same question about the same
drawings, because what a rung is worth is the difference between it and the one
below it. Nothing else sets it. There is no environment variable and no
interface for choosing a rung; `ANIMAGE_CARRY` existed while the choice was open
and came out with the choice. See "rung four, and what it took to make it the
paper's" below.

**The marks are carried once now, and that is the load-bearing part.** Part two
records three readers each applying one shift their own way and two of them
getting it wrong. A field is harder to apply than a number, so it is applied in
one place — `ctgCarriedMarks` — and everything downstream reads pixels that are
already where they are used. `CtgFill::marks` are carried marks; the warp beside
them is reported and never subtracted. That change landed on its own, with
`bench_carry` identical line for line, because the plumbing was the risk and the
estimator was not.

Two costs came with it. A warp is kept per drawing in a map nothing evicts, so
it is cropped to the marks and stored no finer than 32 px — a seam between two
regions that moved differently is that wide, and the majority rule is what makes
that affordable. And the *pixels* a non-uniform warp produces are memoised in a
small bounded store (`Document::ctgCarriedMarksAt`), because compositing happens
per repaint and warping does not. That memo is checked against the source cel's
**revision** and not only against the warp: a mark rubbed out on the drawing
being carried *from* has to reach the screen before anything re-solves.

**What rung three is, in one paragraph.** The drawing the marks were made on is
cut coarsely — its own line art, its own marks, a max-flow inside the estimate
because the job has no document and the fill of an unvisited drawing is not in
the cache — and then split into connected pieces of one label. Two shapes
scribbled the same colour are one label and must not be one region, or the box
round both is the box round the drawing. Each piece is asked where it went, over
its own box, starting from what the whole drawing did. It costs 41 ms against
rung two's 7, beside a 97 ms first solve and a 1553 ms refined one.

**The bound on a region's search is the whole of whether it works.** A region may
depart from the drawing's answer by half its own *shorter* side, and both halves
of that are measured. Half a region's width is where a carried mark stops
holding its region — rung one's own measurement, and a property of the majority
rule rather than of any estimator. The shorter side is where a region's ink
starts repeating: given the longer one, the two halves of `bench_carry`'s
divided box matched each other, 150 px out, and the right half took the left
half's colour on every drawing.

**And a bound that holds only at the top of a pyramid is not a bound.** See "what
a coarse level decides that the fine levels never revisit" below.

### What a hand says about a rung

Three shots disagree with each other, usefully, and the disagreement is the
finding. **The way a shot is scribbled and the rung that carries it are one
thing.** The same shot coloured under one rung and then another ranks them in
opposite orders, which is why it has now been coloured four times:

The shot is coloured **four times over**, by the same person, each pass with a
different rung running while the marks were placed. That is the instrument, and
it exists because of the sentence above: a rung scored on marks placed for a
different rung is measuring the marks.

`to fix` is regions whose majority flipped, `same`/`wrong`/`none` are pixels the
hand gave a colour to, and `worst` is the lowest `same` any single drawing
scored — which is the column that says whether a method ever loses a drawing
outright.

| | to fix | same | wrong | none | worst |
|---|---|---|---|---|---|
| **colour 1**, scribbled for rung two | | | | | |
| left where drawn | 10 of 52 | 86.6% | 0.8% | 12.5% | 5.5% |
| rung two | 11 of 52 | 92.0% | 0.6% | 7.4% | 61.1% |
| rung three | 9 of 52 | 86.3% | 0.5% | 13.2% | 11.8% |
| rung four | 10 of 52 | 91.8% | 0.4% | 7.9% | 62.8% |
| **colour 2**, scribbled for rung three | | | | | |
| left where drawn | 7 of 53 | 91.1% | 2.0% | 6.9% | 63.1% |
| rung two | 10 of 53 | 93.0% | 2.6% | 4.4% | 67.8% |
| rung three | 9 of 53 | 90.0% | 0.7% | 9.3% | 24.3% |
| rung four | 14 of 53 | 91.2% | 1.8% | 7.0% | 65.6% |
| **colour 3**, scribbled for rung three | | | | | |
| left where drawn | 11 of 50 | 85.9% | 0.7% | 13.4% | 3.2% |
| rung two | 14 of 50 | 89.3% | 0.5% | 10.2% | 11.1% |
| rung three | 9 of 50 | 83.7% | 1.0% | 15.2% | 9.6% |
| rung four | 11 of 50 | 93.3% | 1.3% | 5.3% | 80.1% |
| **colour 4**, scribbled for rung four | | | | | |
| left where drawn | 13 of 60 | 87.8% | 0.7% | 11.5% | 4.5% |
| rung two | 14 of 60 | 92.7% | 0.8% | 6.6% | 65.7% |
| rung three | 16 of 60 | 87.6% | 0.5% | 11.8% | 10.0% |
| rung four | 14 of 60 | 92.9% | 0.6% | 6.5% | 68.2% |

**Read the two columns that disagree, because they are the finding.** Rung four
wins `same` and `none` on all four colourings and never loses a drawing
outright. Rung three wins `to fix` on three of four — and it wins it *because*
of the drawings it loses outright: a body that came back with no colour is one
scribble to nudge, so a wipeout counts as one region while rung four's smaller
scattered errors count as several. Both readings are honest and they point
opposite ways.

**The person who coloured it says the two are a wash**, and colouring the shot
took about five minutes under each against about seven for the earlier passes.
So the benchmark and the hand agree, including about not being able to choose.

**And the only number here that measures what any of this is for: the same shot,
timed by the person colouring it, went from 7 min 03 s under rung two to 2 min
44 s.** Every other measurement in this file is a proxy for that one. Roughly
half of it is the step to a rung that carries marks properly, which the five
minutes above already records; the rest arrived with rung four becoming fit to
be the default — the shear fixed, the estimate at half its old cost, and strays
no longer needing to be found and rubbed out. It is one person on one shot with
a stopwatch and it is worth more than the tables around it, because it is the
quantity the tables are trying to predict.
What is left to decide between them is not accuracy:

- rung four is the paper's, and it is the only rung that can be right about two
  things that moved differently — `two-circles` is a reported failure rung three
  **cannot** fix, because it starts from rung two's whole-drawing answer and is
  bounded near it, so when that answer is an alias rung three goes with it;
- rung three costs 41 ms against rung four's 195, both off the interface thread;
- rung three's failures are whole drawings and rung four's are patches, which is
  a question about which kind of mistake is easier to find.

**And rung three's own score is worth knowing about before choosing.** Given the
paper's difference instead of `agreement` — [#68](https://github.com/S-poony/Animage/issues/68),
measured and not shipped — rung three loses the wipeouts and gains the scattered
errors, arriving at 14 · 93.6% · 1.1% · 5.3% on colour 3 against rung four's
11 · 93.3% · 1.3% · 5.3%. It converges on rung four rather than beating it,
which is the argument that the two are one decision and not two.

**Neither of the two numbers settles it, and both are reported.** Pixels answer
"how much of the picture is wrong", which is not what a colourist pays — a whole
body that came back with no colour is one scribble to nudge, and three small
areas each taking a neighbour's colour is three. So there is a count of regions
that would have to be touched beside it. That one has its own cutoff and misses
a drawing that was 9.4% wrong without any region's majority flipping. Two ways
of being wrong about the question, kept side by side, and the pictures are
better than either.

### Rung four, and what it took to make it the paper's

`estimateCtgLattice`, and it needed no plumbing at all: a rung is now an
estimator and nothing else. The paper's, with four things taken from it — the
closed-form rigid fit, rigidity decreasing from 256 rounds to 32 over the run, a
stopping rule that reads the distance from the rest pose rather than the match
score, and the sum of absolute differences it matches blocks by. Two things not:
it matches blurred ink coverage rather than pixels, and it runs on the reduced
grid, both because our drawings are line art.

Embedding the lattice **on the drawing rather than on its bounding box** is the
paper's too, and skipping it changes the answer rather than the cost: a blank
node is never pushed, so a lattice over a sparse sheet is a rigid frame nailed
round everything that moves.

On two shapes that no single translation can describe it is decisive — 56.5% of
a drawing in the wrong colour becomes none of it.

**The fourth of those was taken late, and it was worth 146 px.** The push step
scored agreement — a sum of products, maximised — because that is what the rungs
below it score. Registered against a drawing and *itself*, where the only honest
answer is that nothing moved, the lattice drifted 146 px. It drifts none now,
and the regions a colourist would have to fix went from 19 of 52 to 10 on the
first colouring and 22 of 53 to 14 on the second, at about 170 ms against 526.

**That drift was not the aperture problem, which is what this document and
[#66](https://github.com/S-poony/Animage/issues/66) both used to say it was**
(closed, and superseded by #67 for the score and #69 for the shear). Under a difference, a node on a straight line *ties*
along that line — and the push step scores the position in hand first and only
displaces it on a strictly better score, so a tie is not a reason to move. Drift
needs a measure that actively prefers somewhere else, and an unnormalised sum of
products is one: see "why a sum of products is not a score for a block" below.
The aperture problem is real. It was not what the number was.

**On the shot the two rungs are a wash, and that is worth keeping in view.** On
the colouring made for rung three, rung four agrees with the hand slightly more
often — 91.2% against 90.0% — and leaves more regions to fix, 14 against 9. What
the pictures show is that the two fail on *different drawings*, badly, and
neither dominates: on drawing 9 of that colouring rung three colours almost
nothing and rung four is indistinguishable from the hand, and on drawing 10 rung
three is nearly right and rung four puts a whole leg in the foot's pink. The
person who coloured it found them equally quick. So the shot cannot choose
between them, and did not have to:

**Rung four is not the default, and there is now one reason rather than a
judgement: it comes apart on a closed loop of straight edges.** A rectangle
translated twenty pixels — about as ordinary as motion gets, and something every
rung below it gets right — produces a field running from −314 px to +58 px. That
is [#69](https://github.com/S-poony/Animage/issues/69), and a probe of twelve
shapes says exactly where the edge of it is:

| | rung four says | the field |
|---|---|---|
| one straight line, moved 20 | 17, 0 | uniform |
| two parallel walls, moved 20 | 17, 0 | uniform |
| L shape, moved 20 | 13, −7 | tight |
| C shape (three walls), moved 20 | 11, −6 | tight |
| ring, moved 20 | 20, 0 | uniform |
| filled disc, shrunk 10 | −1, −3 | tight |
| small 60 px box, moved 10 | 5, −4 | tight |
| box with a cross inside, moved 20 | 4, −15 | tight |
| **box, moved 20** | **−93, −116** | **x [−314, 58]** |
| **filled box, moved 20** | **−54, −69** | **x [−269, 55]** |
| **diamond outline, moved 20** | **47, −97** | **x [−104, 142]** |

**Closing the loop is what breaks it.** Three walls of a box are fine and four
are not; the same shape turned 45° is just as bad, so it is not axis alignment.
**Filling it does not help** — the filled box fails too — while a filled *disc*
is fine and a box with a cross scribbled through it is fine. So it is not line
art against filled art either, which is what the issue assumed when it was
opened.

What the working cases have and the failing ones lack is **something within a
block's reach that pins both directions at once**: curvature on the ring and the
disc, a free end on the L and the C, an interior feature on the crossed box, and
on the 60 px box corners that are never further off than a block can see. A long
straight run has none of that, and a closed loop of them can shear while every
local rigid fit stays satisfied — each wall slides freely along itself and the
loop has no interior tying opposite walls together.

**That one is the aperture problem, and it is a different thing from the 146 px
above.** The drift was the score, which is why registering a drawing against
itself now answers zero; this is the geometry, which a zero-drift score does not
reach. Both sentences are needed and neither replaces the other.

That was the scope of rung four for as long as it sheared: **good on drawn
character art, unreliable on anything geometric.** It is fixed — see "what the
push step is allowed to see" below — and the warning that came with it is worth
keeping anyway, because it is general: the four hand colourings said nothing
about the defect at all, since a cat has no straight walls, and every fixture in
`tests/` that is not a hand-drawn shot is made of boxes. A shot of a person's
drawings and a bench of synthetic shapes fail to notice different things, which
is why `bench_shapes` exists beside `bench_hand` rather than instead of it.

### What the push step is allowed to see

**Rung four sheared a rectangle because the push step moved nodes along
directions it could not see, and the regularisation could not take it back
fast enough.** That is [#69](https://github.com/S-poony/Animage/issues/69), and
the trace that found it is worth describing because the fix follows from it
directly.

Per step, on a 180 px box, the push step injected about **+6.5 cells of spread
every step, at every rigidity, from the first step to the last** — and what
varied was how much the regularisation removed: −6.9 at 239 inner rounds,
−6.5 at 135, −4.9 at 101, −3.1 at 32. While the net was zero the lattice sat
still and the match cost was flat. Below about 140 rounds removal lost, the
error compounded, and the loop hit its step cap still diverging, so the answer
was wherever the runaway had got to. Two things cut the removal rate — the
rigidity ramp and the size of the lattice, since the regularisation is a local
averaging and scatter across 43 nodes takes far more rounds to diffuse out than
across 28 — and one thing set the injection rate, which did not vary at all.

**So the removal side cannot be bought.** More rounds cost time quadratically,
cost the flexibility the method exists for, and still lose on a big enough
drawing: pinning rigidity at its maximum for the whole run left a 400 px box
diverging from step one. The fix had to be on the injection side.

**What a node is allowed to contribute is now split against the surface that
chose it.** The search has already scored its whole window and thrown it away;
nine more block differences round the winner give the curvature there. The small
eigenvalue names the direction the surface is flat along — a valley — and the
ratio of the two eigenvalues says how much of a pit rather than a valley it is.
The across-valley component is taken whole; the along-valley component is scaled
by that ratio. **The ratio and not either eigenvalue**, because a block's
absolute curvature scales with the ink under it where their ratio does not; and
a scaling and not a cutoff, because a threshold has been tried and rejected
three times here and a cliff is what was wrong with each.

Measured, the separation is not subtle: **84% of a box's pushed nodes sit in a
valley against 15% of a ring's**, mean ratio 0.056 against 0.269 — and the ring
stops moving entirely after three steps where the box never stops.

`bench_shapes` went from 334, 326, 320, 289 and 185 pixels wrong on five rows to
nothing worse than 34 on any row, with every box in single figures.

**Three constants moved with it, and two of them were wrong before.**

- *The stopping quantity.* The paper averages the distance from the rest pose
  over the points of the embedding lattice; this averaged over every node in the
  bounding grid, including ones in no square that are never pushed and never
  regularised. The rule reads a change, so the constant they contribute cancels
  — but the divisor does not, and it made the cutoff mean something two to eight
  times looser depending on how much blank paper the drawing sat on. That is
  [#71](https://github.com/S-poony/Animage/issues/71), fixed here rather than
  after, because taking less of the push slows convergence and a rule calibrated
  against a diluted quantity then started stopping shapes before they arrived.
- *The rigidity ramp.* It was `stepped / (kSteps - 1)`, which ties the schedule
  to the cap — so raising the cap to let a slow shape converge also stretched
  the ramp, and a lattice that should have been at 32 smoothing rounds by step 40
  was still doing 211. The paper ramps over its **first fifty steps** whatever
  the run length. Untying them was worth 26% of the running time *and* the best
  colouring scores of any rung, which is the signal that it was a defect and not
  a tuning: cost and quality do not usually move the same way.
- *The along weight.* Bounded from both sides and the bounds are measured: below
  a third of the along motion kept, the diamond returns to the hundreds, because
  keeping more of what compounds is what compounds; above it, limbs suffer — a
  leg is two long parallel edges that slide along their own length, which is the
  one shape whose real motion lies along the direction being suppressed.

**And it got faster rather than slower.** The paper recommends the early
termination of Li and Salari by name in §3.1: the best block position stays put
for most iterations while every other shift scores much higher, so almost every
comparison is one that is going to lose, and a sum that has already passed the
best in hand can stop where it is. The search only ever asks "is this strictly
better", so **no answer changes** — `bench_shapes` is identical value for value
— and the estimate went from 204 ms to 80 ms, which is half what rung four cost
before any of this work.

**One warning to carry forward.** Two rounds of measurement in this work were
taken against a stale binary: a benchmark left running holds a lock on its own
executable, the relink fails, and a build checked by grepping its output for
"error" does not notice, because ninja says `FAILED:`. Two runs reported the
pre-change numbers to the digit while a bench that had relinked reported the new
ones. **Check a build by its exit code, and never measure while one is running.**

### The one constant in rung four, and what bounds it

**`kAlongKeep`, in `src/core/ctg_job.cpp`. If rung four is misbehaving and you
are looking for a number to turn, this is the number, and this section is the
measurement that put it where it is.**

It is the power the curvature ratio is raised to before scaling the
along-valley half of a push step's displacement -- see "what the push step is
allowed to see" above for what that means and why it exists. Read it as *how
much of the motion a node cannot see is kept anyway*: at 0 all of it, at 1
almost none of it.

**It is bounded on both sides by a different failure, and both bounds are
measured**, which is the only reason it is defensible at all:

| | `bench_shapes` | what it costs |
|---|---|---|
| 0.25 and below | diamond **98**, and worse below | the shear comes back — keeping more of what compounds is what compounds |
| **0.35** | diamond 34, straight line 4 | shipped |
| 0.5 | diamond 29, straight line 6 | limbs suffer |

and on the four hand colourings, 0.35 against 0.5 is 7·11·10·12 regions to fix
against 7·12·10·12, with **pixels taking a wrong colour 0.9 / 2.0 / 1.4 / 0.8
against 2.0 / 2.1 / 1.4 / 1.4**. That last row is why it is 0.35 and not 0.5:
the shot's owner asked for an uncoloured region over a confidently wrong one,
and the wrong-colour column is the one that measures the thing they did not
want.

**What the two bounds are about, in one line each.** Below it the diamond
shears, because along-valley motion is exactly what accumulated into the #69
runaway. Above it a *limb* suffers, because a leg is two long parallel edges
that slide along their own length between drawings — so its real motion lies
along the direction being suppressed, and suppressing too much of it leaves the
leg behind while its foot's colour walks up it.

**Three warnings before you turn it.**

- **It was very nearly fitted to a bug.** 0.35 was first chosen over 0.5
  because 0.5 put a leg in its foot's pink, 11.2% of one drawing in a wrong
  colour. That leg was suffering from a rigidity ramp tied to the wrong
  constant, not from the exponent. With the ramp fixed the comparison had to be
  run again, and 0.35 won on different and much narrower grounds. **A constant
  justified by a measurement taken under a defect has no justification.**
- **`bench_shapes` alone will mislead you here.** The diamond and the straight
  line move in opposite directions across this constant, and neither of them is
  a drawing. The colourings are what decided it, and they can only be read four
  at once — this session twice saw a change look clean on one colouring and be
  a regression on another.
- **The family is `pow(ratio, k)` and nothing about that is sacred.** It is a
  smooth monotone map chosen because a wrong value degrades gradually where a
  threshold has a cliff, and cliffs are what made the three rejected thresholds
  in "the traps" wrong. If you find a better-motivated shape — a shrinkage with
  a noise scale in it, say — the thing to preserve is smoothness, not this
  particular curve.

### A mark the program moved, and did not land

**A carried mark that wins no region is dropped now rather than left where it
fell.** [#73](https://github.com/S-poony/Animage/issues/73), reported from using
the program rather than found by a bench — which matters, because **no
measurement in this tree could see it**.

A stray does not merely fail to colour a region. It colours its own pixels,
because `ctgFillPixel` consults the mark before the labels — a scribble is a
statement about the pixels it covers. So a mark carried off its shape leaves a
small patch of a wrong colour on the drawing, and somebody has to find it and
rub it out.

**Small is worse than large here, and that inverts the usual reading.** Measured
over the four hand colourings before the fix: a stray colours at worst 119 to
295 image pixels, an eleven-pixel square, and usually far less. A stray the size
of a limb gets noticed and fixed. One this size survives into the render. The
argument for dropping them is not about area — the area is negligible and always
was.

And it is not rare: **15 to 20 per cent of the regions a mark stands in were won
by nothing**, about one stray every other drawing.

**Why no bench caught it, which is the part worth keeping.** `countFixes` only
counts a region once it is at least 24×24 image pixels, and a stray is smaller
than that, so it was never a region to fix. The pixel percentages are over
millions of pixels, so a few hundred never moved them at a tenth of a per cent.
Dropping every stray in the shot changed `to fix`, `same`, `wrong` and `none` by
**exactly nothing** — 7, 11, 10 and 12 regions before and after, to the digit.
The instrument that found it is a person using the program, and the instrument
that now watches it had to be built for it: `bench_hand` reports strays as a
rate, the pixels they colour, and the worst single one.

**The unit is a connected run of labels, not a mark.** `CtgFill::spread` is per
palette colour and reduced to one number for a whole fill, so it cannot say
which mark was stranded, and two scribbles sharing a colour average each other
out. A run of labels can: barely larger than the mark inside it means that mark
won nothing, and two same-coloured scribbles landing in one region leave a run
holding both, which is correctly not a stray.

**Only a carried mark, never one drawn here, and that distinction is the whole
licence for doing it.** A scribble a person made is honoured whatever the solver
decided — there are tests pinning that and they are right. A scribble the
program moved here is a guess the program made, and a guess that won nothing is
one it may withdraw. The first version of this ignored the difference and three
tests caught it in the first run. `CtgFill::inherited` is the distinction and it
was already there.

**Both halves have to go.** Clearing the labels alone leaves the mark painting
its own pixels; clearing the marks alone leaves the labels colouring the cells
around it. And the marks are rebuilt rather than edited: the grid is shared
copy-on-write with the cel it came from, so writing through it would rub the
mark off the drawing somebody made it on.

**It cost nothing and it needed no persistence.** 106.7 s against 108.4 on the
four colourings, every gate unchanged, and `two-circles` went from two strays
colouring 644 px to none. Persistence was considered and is not needed here:
`celSourceFor` walks back to the drawing that *owns* the marks, so each drawing
carries from the owner rather than from the previous carried result. There is no
chain for an error to compound along, and nothing for a dropped mark to stay
dropped in.

**What it cannot reach.** A mark that landed in the *wrong* region rather than
in no region — a leg taking its foot's colour. That one won plenty, just not the
right thing, and `spread` measures it *higher* than a correct placement rather
than lower. It is a different defect, it wants a correspondence between regions
on two drawings, and #73 records what is known about it.

## Scoring a rung against a hand

`bench_hand` is the instrument the rungs above are argued with, and it is a
different kind of measurement from everything else in `tests/`. Every other
bench moves a shape this repository drew. Two drawings by a person are never the
same shape twice, and the failure rung two was reported for — drawings that
drift and change size — is one no fixture here can produce.

So it opens a shot somebody coloured, and for each drawing that owns marks:
solves it with them, which is the answer to beat; clears them so the drawing
inherits from the one before it; solves again once per rung; and compares the
two **fills**. Where a person put a mark is not a fact about anything — what
they looked at and accepted is the picture. Then it puts the marks back, undoing
to a recorded depth rather than counting undos, because an edit that changes
nothing records no command and counting them is how a bench quietly pops
somebody else's step.

Four shots are in the tree, and they are for different questions:

| | |
|---|---|
| `chatquimarche-coloured.animage` | a shot coloured drawing by drawing, four times over, each pass scribbled with a different rung running. Answers "is this better" — and, because the four passes disagree, "better for whom" |
| `two-circles.animage` | two circles moving up either side of the frame. Answers "what exactly is broken" |
| `still-and-moving-circles.animage` | one circle held still and one moving up past it. Answers "how far is too far" |
| `chatquimarche.animage` | the same shot before it was coloured, for `bench_session` |

The circles are the ones to reach for first when something is wrong, and both
pairs were reported rather than constructed. Two shapes the same size means
sliding the drawing sideways puts one on the other, which is the ambiguity every
failure so far has turned out to be — and being five drawings of two circles
each, they run in seconds and every number in them can be checked by eye.

**The two pairs are not the same question, and the difference is the point of
having both.** In `two-circles` both circles move, so the drawing has evidence
everywhere and the failure is that one translation cannot describe two motions:
rung two slides everything 780 px and puts one circle's mark on the other. In
`still-and-moving-circles` one of them does not move at all, and the moving one
goes further in a drawing than a node can see — so half the drawing has good
evidence, half has none, and the answer is confident about the wrong half. Its
layers say which is which: `colour 1` is what you get having coloured only the
first drawing, and `corrected` is the answer. That is
[#72](https://github.com/S-poony/Animage/issues/72), and rung four is already
the best of the three rungs on it — it leaves the moving circle uncoloured
where rungs two and three fill it with the *other* circle's colour, which is
the better failure and still not the right answer.

`--pictures DIR` writes the fills out, named `layer-drawing-method`, with the
colourist's beside them. On a shot with two colour layers both are scored, which
is how the same drawings under two different ways of scribbling are compared.

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
was an explicit scene length, and it is what
[a shot can now be told how long it is](#what-a-track-does-past-its-last-drawing)
below is about: `Scene::fixed_length` and `Scene::length` exist, so a cycling
track that is the longest no longer has nothing to cycle over.

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
- **It follows the focus, and nothing else** — `QApplication::focusChanged`, and
  `isTypingInto` on whatever now holds the keyboard.

That second point was learned twice, which is the part worth reading. It was
first reported by the two rename editors, each telling the window when it opened
and when it closed, and that was wrong in two ways at once.

It needed a *count* rather than a flag, because two editors overlap for a moment
— opening a rename in the layer panel is what takes the focus off an open one in
the timeline, so the second is created before the first is told it has finished,
and a flag would have handed the keyboard back with an editor still on screen.

And it covered renaming and nothing else. **So Return in the brush size box was
still Play**: the box never got the key, the value was never applied, and the
animation started playing instead. Reported from use, long after the rename it
was built for was working. The onion count and the five numbers on the transform
bar all had it too, and none of them had ever been thought about — because the
mechanism had been wired to the instance that prompted it rather than to the rule
it was an instance of.

The focus answers both. It is single-valued, so the overlap that needed counting
cannot arise: whoever has the keyboard *now* is the answer. And it knows nothing
about renaming, so a field added tomorrow is covered by having been added. The
two editors no longer report anything — `LayerList::renaming` and
`TimelineWidget::renamingChanged` are gone, and the ninety lines of comment
explaining when each of them fired went with them.

**The question was already answered three paragraphs down.** The Space and Z
filter asks `isTypingInto(QApplication::focusWidget())`, and has since it was
fixed — the same question, in the same file, for the other half of the same
problem. Nobody joined them up, and the cost of not joining them up was a field
that could not be typed into.

One consequence for teardown: `~MainWindow` now disconnects qApp from itself
beside removing the event filter, because taking the children apart moves the
focus and the connection would otherwise deliver into a window that is halfway
gone. See [the traps](#the-traps).

**And the other half of it: getting the keyboard back out of a field.** The
canvas (`StrongFocus`) and the timeline (`ClickFocus`) are the only two widgets
in this window that take focus on a click. Every other one is deliberately
`Qt::NoFocus` — the layer panel so that Space keeps panning instead of toggling a
visibility tick, the buttons and swatches so that they cannot take the pen — and
the unnoticed cost of that decision is the same fact from the other side: **a
widget that will not take the keyboard cannot take it away either.** So a number
being typed into kept the keyboard through a click on the opacity slider, on a
swatch, on the empty part of the layer panel; and a rename kept it through a
click on another row, leaving an editor open on one layer while a different one
was selected. Both reported from use, and measured before being believed:

| the click lands on | the keyboard afterwards |
|---|---|
| opacity slider | stayed in the field |
| layer panel, on a row | canvas — by a side effect in the selection path |
| layer panel, empty space | stayed in the field |
| canvas | canvas |

It went unnoticed for as long as it did because it cost nothing visible until the
shortcut mode started following the focus. After that, a field that would not let
go left **every shortcut switched off**, silently, until something that takes
focus was clicked. That is the whole of why it was worth fixing rather than
living with.

`MainWindow::takeTheKeyboardBackFrom`, called from the same application-wide
filter, is the rule: *a left press on anything but the field itself hands the
keyboard to the canvas.* No focus policy changed, because changing those is what
would bring Space back to the tick boxes; the click is what changed, and only
when there is a field to take the keyboard from.

**Read that function's walk before changing it.** It asks about the *parent
chain* of what was pressed and not about the widget itself, and that is not
defensive coding — a spin box holds the focus for the line edit inside it through
a focus proxy, so the line edit a click actually lands on reports `NoFocus`.
Asking only about the widget under the pointer took the keyboard off one number
field and handed it to the canvas on the way to another, which is this same bug
in the opposite direction. It was written that way first and the test caught it.

**And the filter that forwards Space and Z was eating every space anybody
typed.** It has been wrong since it was written, and nothing noticed while the
only places to type were dialogs nobody put a space into. Renaming in place is
typing into the main window, and a track renamed "rough pass" arrived called
"roughpass". It asks `isTypingInto(QApplication::focusWidget())` now — the focus
widget and not `watched`, because a key goes to whatever holds the keyboard and
this filter sees it again at every parent it propagates to. It was already wrong
for the Rename track dialog and for every spin box on the transform bar.

**And both of its guards were being asked again on the way out.** They answer
about the state *now*, and the state changes while a key is held down: Alt+Tab is
Alt going down, so the Space release on the way back carried Alt, hit the guard
that exists for Ctrl+Z, and was dropped — with its press already delivered. The
canvas went on believing Space was held, so every pen press panned instead of
drawing, for the rest of the session, with nothing saying why. Opening a rename
editor between a press and its release did the same through the other guard. The
filter remembers which keys it forwarded a press for and forwards their release
unconditionally; a release whose press it never sent still takes the old path, so
the bare-release case that was already tested is unchanged.

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
`compositeScene` takes a `SubstitutedLayer`, whose null `tiles` means "do not
draw this layer at all" — and drawn on top through a `QTransform`, so the source
region looks empty because it is not being drawn, not because anything was
erased. (This used to say `compositeScene` takes a "lifted layer id". It does
not and never did: `lifted` is the grid a transform picked up, on
`SelectionSplit` and `LiveTransform`, and the two are unrelated.) `Cel::replaceTiles` is what commits, journalling
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

### Alt scales about the middle, and takes the key off the eyedropper

Asked for later, and it is the gesture every drawing program has: hold Alt while
dragging a handle and the box grows both ways at once instead of pinning the
handle opposite. What it is for is registration — a drawing lined up on
something underneath it stays lined up while it is made bigger, which a scale
about the far corner does not do.

**It is one `repivot` and nothing else in the arithmetic.** The pivot was already
a number on `Transform` rather than a case in the maths, so the whole of
"symmetrical" is that the pivot is the middle of the box instead of the handle
opposite: `arm` is measured from the pivot, so from the middle it is half as long
and the same expression asks for half the factor. Nothing about corners against
edges, flips or rotation had to be touched, which is the pivot-as-a-number
decision paying for itself.

**Read afresh on every move, not decided at the press.** `continueTransformDrag`
rebuilds from `grab_values_` each time, so taking the key up or letting it go
part way through a drag changes the box under the same press — which is how the
key behaves everywhere else it does this. It costs the drag nothing to allow,
and a modifier you have to have thought of before pressing the button is one
nobody discovers.

**And it does not wait for the pointer to move.** What the key changes is the
pivot, so nothing on screen would move until the hand did, and a key whose effect
arrives on the next jog of the mouse reads as a key that did not work. The
key-down and key-up handlers re-run the drag from where the pointer already is.
Shift, which constrains a rotation to fifteen degrees, still waits for the next
move; it is the older of the two and nobody has complained, but it is the same
question and would be answered the same way.

**And that is what found the menu bar.** Reported straight away: Alt made the
box symmetrical on the way down and *ended the drag* on the way up. `QMenuBar`
watches for a bare Alt — it arms on the press and takes the keyboard on the
release — and the keyboard leaving this widget is a focus-out, which is
`abandonGesture`, which ends every gesture in progress including the one under
the button that is still held down.

The fix is in `focusOutEvent`: **a drag keeps the keyboard, and takes it back if
something helps itself to it.** Losing the focus is what puts the menu bar into
keyboard mode and losing it again is what takes it back out, so one `setFocus`
undoes the whole thing, and the key release still arrives here in the same
delivery — which is what lets the box go back to scaling about the corner at
once rather than at the next move.

Two things about it are worth having written down. **Accepting the key instead
does not work**, and that was measured rather than reasoned about: the menu bar
sees the press through a filter that runs before this widget is reached, so
there is nothing here to withhold from it. And **the condition is `grab_` and
not `transform_`** — `grab_` is set by a press and cleared by the release, so
what it says is "a button is down and the release is coming here". A transform
merely sitting on screen has no gesture to protect and lets the keyboard go like
anything else, and the window going away is still the whole of `abandonGesture`
through `changeEvent`, because there the release genuinely is not coming and a
drag left running would scale with no button held.

**The test for it needs three things that are each easy to leave out**, and the
first version had two of them wrong and passed against the broken code. It needs
the real window, because a bare `CanvasWidget` has no menu bar under it. It needs
a genuine Alt *press* and not only a release, because the press is what arms the
filter. And it needs no mouse event between the two, because a mouse event is one
of the things that disarms it — which is also why the bug wants the hand held
still, and why answering the key without waiting for a move is exactly what made
holding still the natural thing to do.

**The eyedropper gives the key up during a transform**, which was the user's
call and the right one: there is no colour in a transform to pick up, so nothing
is lost, and `Alt` cannot mean two things at once. Two places had to agree about
that — `pressAt`, so a press with Alt down starts the drag rather than sampling a
pixel, and `pointingAt`, so the pointer does not offer a pipette over a box that
would not use it. That is the rule the pointer exists for, and it is exactly the
sort of pair that goes out of step: they are the same order of tests in the same
order for that reason, and the handle cursor over an Alt-held box is what says
so.

**And the shortcut table stopped saying otherwise.** `PickColour` was `kAlways`
and is `kNormal` now, for the reason the straight line already was: one key, two
modes, two meanings, and the row is about the one the brush is under.

The panel had never listed the other meaning of either key — Shift's constraint
was as missing as Alt's symmetry — and it lists both now, as two `Kind::Held`
rows in the transform group rather than with the other held keys. The group
heading is what says *when* they mean this, which is the whole reason a second
row on the same key is not a contradiction: `shareAMode` is what keeps the pair
from reading as a collision, and it is the same mechanism that lets Return be
Play and also be Apply.

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

**An empty selection cannot exist.** A loop enclosing no ink is the same as no
selection — there is nothing to lift — but "no selection" also means "transform
everything", so a stray loop over blank paper would quietly become a
whole-drawing transform.

The rule is `dropSelectionIfItCatchesNothing`, and it is asked wherever the loop
or what sits under it changes: when a loop is finished, and when another layer is
chosen with one still up. Changing layer used to be the gap — a loop carried onto
a layer it covered no ink on stayed, and there was then a state that had to be
explained to whoever pressed Backspace in it. Applying the one rule in the one
place removed the state and the explanation together, which is the reason it is
one function and not a test repeated at each site.

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

**The loop is cleared by changing frame, survives changing layer where it still
catches something, and is cleared by a transform that commits.** The first two
are the design's; the third is not in it — after a commit the loop describes
where those pixels *were*, and keeping it would offer a second transform of a
shape that has moved out from under it.

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
colour layer is a label nobody meant. `refuseHere` is the single list every
operation that *carries a mark from one place to another* checks — transform,
cut, copy, paste — because "refuse where the brush refuses" is one list and not
four.

**And the kind is the only thing on it the brush does not check**, which is why
it is two lists rather than one. `refuseToEditHere` is the brush's own — no
drawing, no layer, locked, hidden — and `refuseHere` is that plus the kind.
`beginStroke` and `eraseSelection` ask the first; the four above ask the second.

Erasing is the case that decides the split. Issue
[#43](https://github.com/S-poony/Animage/issues/43) reported Backspace as a
fifth operation missing from the shared list and proposed adding it whole, kind
and all — and the kind is exactly the part it must not have. What a colour layer
holds is scribbles and not lines, which is what the other four have to care
about: they carry marks from one place to another and the two kinds are not the
same thing to carry. Erasing carries nothing anywhere. It takes a mark away,
which is what the eraser already does on that layer, so a Backspace that refused
there would be stricter than the tool sitting next to it.

**Every one of the five answers `Refusal` and not `bool`**, and that is the other
half of the same issue. `eraseSelection` returned a `bool` its action discarded,
so on a locked layer Ctrl+X named the reason and Backspace did nothing at all
with nothing said. An operation that silently does nothing is a bug as far as
anybody holding the pen is concerned — the same rule as the status bar saying why
the brush will not draw past the end of a track.

**But a refusal the status line is already showing is not repeated.** `sayCannot`
in `main_window.cpp` is the one place the five report from, and it stays quiet
about `NoDrawing`: past the end of a track the line permanently reads "you cannot
draw past the end of a track", and a temporary message *hides* the line while it
is up — so saying the same thing in other words covers over the words that were
already saying it. That is also why `eraseSelection` asks where before it asks
what: the frame change that took the playhead past the end already cleared the
loop, so testing the loop first answered "nothing is selected", which is true and
is not the reason.

**And the shorter the vocabulary the better.** The first cut of this gave erase a
refusal of its own for "there is ink on the layer, but not under the loop" —
reachable only by carrying a loop onto another layer, which
[an empty selection cannot exist](#the-lasso) now prevents outright. A rule
applied consistently is worth more than a message explaining the state it would
have left.

**A refused pen-down is answered by the pointer, not by a message.**
`beginStroke` consults the list and reports nothing, which is right — a stroke
has no status bar of its own and a banner under every refused dab would be
worse than the silence. For a while nothing else answered either: the status
line covered only past-the-end and `pointingAt` never asked about the layer, so
a crosshair sat over a layer that would take no mark and the pen left nothing
behind. That was issue
[#62](https://github.com/S-poony/Animage/issues/62).

`pointingAt` asks the refusal list last, after the gestures, the held keys and
the lasso, and answers `Pointing::Nothing` — the enumerator kept when a
transform stopped being able to reach it, on the stated grounds that it is
still right for anywhere else that acquires one. This is that somewhere else,
and the arrow it maps to already meant "this is not a place to draw". Nothing
new was invented; the design had left the slot open.

The order matters and is the order a press resolves in. Navigation outranks the
layer, because a locked layer that swallowed Space would be saying something
false about what a press does. So does the lasso: a loop can be drawn on a
locked or hidden layer and copied off one, since selecting is not editing and
whatever the selection is handed to says its own no.

**And the status line names which of the three it is**, for the reason it
already gave for past-the-end — a brush that does nothing is otherwise a bug —
but only when the past-the-end phrase is not already saying it. Two phrases for
one fault is one too many, which is `sayCannot`'s rule as well.

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

## Transforming a layer through time

Issue [#25](https://github.com/S-poony/Animage/issues/25): moving, turning or
scaling **one layer across every drawing in it**, the way layer opacity already
applies across time. The row under "drawing" in the taxonomy at the top of
[lasso-and-transform.md](lasso-and-transform.md). What it does, for somebody
using it rather than changing it, is
[in the manual](manual.md#transforming-a-whole-layer-through-time).

**It bakes, and the issue argued for the opposite.** #25 was written for a
stored affine on the `Layer`, applied at composite time — free, lossless,
adjustable afterwards, undoing through `LayerListOp` without touching the tile
journal at all. That is a genuinely better shape in every respect except one:
a transformed layer stops being a plain grid, so everything that reads a layer's
pixels has to go through the matrix — the brush, the eyedropper, `ctgBarrier`
(an ink layer that is a CTG source has to be flattened *through* its transform
or the barrier stops matching the drawing it is cut against), `celBounds`,
fit-to-drawing and export. And it needs an answer to "what happens when you draw
on one", which #25 gave as "the first stroke bakes it anyway".

Baking on Apply leaves all of that alone. `compositeScene` is still a flat list
of untransformed grids, `scene.json` gained nothing, `LayerPass` was not
widened, and the drawing question does not arise because there is never a
transformed layer to draw on. **The price is one resample per drawing and a
command that journals the layer twice over**, and both are in `bench_transform`.
The trade was made deliberately by the person whose drawings they are.

### The two doors, and why there is no switch

The Transform tool takes the drawing in front of you. **"Transform layer through
time"**, in the layer panel beside Add and Remove, takes the layer. Which door
you came through is fixed for the gesture and the bar says which — a label, not
a control.

That is `lasso-and-transform.md`'s own argument one gesture further along. It
refused a persistent scope checkbox because one that is off by default is a trap
*because* it is off by default: you forget it is on in the one session where it
is, and an ordinary drag then rewrites the whole shot. A switch on the transform
bar is the same trap with a shorter fuse — it would let you change what a
gesture already on screen is about. Two entry points and no switch is also the
shape the tool already had: there is no "transform selection" button because the
tool is the button, and this is the same sentence about a layer.

The button is **greyed out with a tooltip that says which reason it is**, rather
than absent: a control that comes and goes as you move between layers is one
nobody can find twice, and "why can I not do this here" is the question a
disabled control exists to answer. The order of reasons is `refuseHere`'s order,
and that is not cosmetic — the layer kind is asked ahead of the lock, because
unlocking a colour layer would not make this work and naming the lock would send
somebody to fix the wrong thing.

**A reason missing from that list is worse than one in the wrong place**, and
two were missing. `beginLayerTransform` refuses where there is no drawing at
this frame — past a track's last drawing there is no cel to float — and where
nothing has been drawn on the layer at all; the button knew about neither, so it
stayed pressable and the answer arrived as a status line that goes away instead
of a tooltip that is there when you look for it. They are also the two that
change on a different signal from the rest: the frame one moves with the
playhead, so `refreshLayerFlags` asks again; the ink one arrives with the first
stroke, so `documentChanged` does. Cheap in both cases — `layerDrawings` counts
cels, never pixels. `test_canvas` asserts each reason's wording, which is the
half of this a screenshot cannot show.

Locked layers refuse. Worth knowing because [importing.md](importing.md) plans
an imported modelsheet as a *locked* layer placed with this very box, so #31
will have to unlock to place or argue for an exception.

### The box is the union, and the rest of the layer is under it

**The box is every drawing's painted bounds united, not the one you are looking
at.** A box taken from whichever drawing the playhead happened to be on would
put the playhead into the arithmetic: turning the layer from the first drawing
and from the twentieth would give different answers, because the pivot is the
middle of the box. It also means there is a box at all on a frame where this
layer is empty, which on a layer that starts late is the first frame you would
try it from.

**And every other drawing is under the float at low opacity, moving with it.**
What is being placed is the layer, so there is no other way to see whether it
has landed. Three things about that picture:

- **One picture, not one blit per drawing.** They all move by the same matrix,
  so what has to come out of this is a single image: forty separate low-opacity
  blits would pile up into black everywhere the character stayed still, which is
  most of a held drawing. It is `compositeGrids` that makes it, handed the
  drawings as forty passes. The first version merged the grids into one with
  `mergeOver` and composited that — the same picture arrived at the expensive
  way, because `mergeOver` alpha-blends every overlapping tile at full tile
  resolution and by the third drawing almost every tile overlaps. Forty drawings
  of an HD character measured **817 ms**, on the interface thread, with nothing
  on screen to say why — the same freeze the bake at the other end of the
  gesture is so careful to announce, at the moment the button is pressed.
  Through the compositor it is **59 ms**: it works at the step the picture is
  actually drawn at rather than at tile resolution, and it splits by rows across
  every core.
- **Their own colours, not a tint.** The onion skin throws a ghost's colour away
  and tints it warm or cool because what it is saying is *when* that drawing is.
  These are all one layer at one moment, so there is no when to say, and what is
  worth seeing is what will actually land.
- **The same rectangle and step as the float**, so both go through one matrix and
  cannot drift apart.
- **In `Document::layerDrawings`' order, which is sorted.** `Track::images` is an
  `unordered_map`: its walk order is not the timeline's and is not stable
  between two runs of the same program. Merged in that order the ghost picture
  came out different every time it was drawn, because `mergeOver` alpha-composites
  and which drawing wins a shared pixel depends on which came last. There is no
  "nearest" among these to put in front — they are all one layer at one moment —
  so the order is fixed rather than meaningful. Fixed is the whole requirement.

**The box is green.** Two doors into one gesture that write amounts two orders of
magnitude apart should not look identical, and the ghosts do not say it on their
own — a layer of two drawings barely has ghosts. Red still beats green: whether
it can be committed at all is the more urgent of the two things that colour
carries.

### And it survives the playhead, which the Transform tool does not

`setFrame` commits a live transform, and for the tool that is right: a float
that follows you to another drawing is a paste onto the wrong drawing waiting to
happen, and there is nothing useful it could mean out there.

**A whole-layer transform inherited that rule and should never have.** The
reasoning is entirely about a float belonging to one drawing, and it inverts for
a gesture whose whole claim is that every drawing moves together — scrubbing to
see how the layer sits on a later drawing is part of placing it, not the end of
placing it. What it did instead was bake: a click on the timeline rewrote every
drawing in the layer, seconds of work, no Apply pressed, and nothing anywhere
that would have led you to expect it. The most natural thing to do with the
gesture was the one thing that ended it.

So the gesture survives, and the playhead changes exactly one thing about it:
which drawing is the float and which are the ghosts. `rebindLayerTransform`
re-splits the list and rebuilds the two pictures. The box, the pivot and the
five numbers do not move — they are about the layer, and the union of every
drawing's bounds is what makes that true whatever frame you are standing on.
`a-layer-moved-through-time-from-another-drawing` in `shots` is the picture of
it, and `test_canvas` asserts both halves: that the layer transform survives and
that an ordinary one still commits.

Changing *track* or *layer* still commits, and those keep their reason: a
transform is of one layer of one track, so leaving either says you are done.

### And the onion skin has to leave the layer out

Otherwise a neighbouring drawing is on screen twice — once still, where the
document still has it, and once moving under the float — and the two are in the
same place only until the first drag. `Compositor::composite` and
`compositeLayers` took a `SubstitutedLayer` for this, forwarding to the
`collectPasses` parameter that already implemented the omit case; no new logic
in `core` at all.

**It is carried on `OnionState` and not by setting `onion_dirty_` at the three
places that change it**, and that distinction is the whole reason the field
exists. Starting a layer transform, committing one and cancelling one each
change what the ghosts are made of while no drawing has moved, so nothing else
in the state would notice — and the first version of this did set the flag by
hand at all three. That is invalidation by signalling, which is the thing
[what refreshAll does not refresh](#what-refreshall-does-not-refresh-and-the-buffer-nobody-was-invalidating)
already records a bug from: there are twenty-six calls to `refreshAll`, and a
rule every future one has to remember is a rule that gets forgotten.

### What it costs, and the bound that had to change

`bench_transform` grew a whole-layer row, and it decided two things.

| a layer of 40 | PAL | HD | 4K |
|---|---|---|---|
| rotated 7° | 375 ms | 1225 ms | 3959 ms |
| retained by the history | 260 MB | 880 MB | 2960 MB |

**The history number is the one to know.** One command now retains a whole
layer, so at HD and above a single bake is past the 512 MB budget on its own and
the history drops everything older to stay inside it. The bake itself always
undoes — the newest command is never dropped — but the rest of the session's
history goes with it. That is inherent to baking rather than a fault: undo has
to hold the old pixels, and there is no cheaper correct answer.

**It is inherent to a bake that *lands*, and that distinction was missing.** A
bake that runs out of memory undoes itself (below) — and the trim had already
happened by then, at the moment the command closed. So a failed bake dropped the
session's undo history to make room for a command it then threw away, and said
"nothing has changed" over the top of it. Measured on a fixture with a small
budget: fourteen commands before, none after, and the first drawing no longer
reachable by Ctrl+Z. On a real HD layer the numbers in that table say the trim
fires every time.

So `endCommand` skips the trim while `defer_trim_` is set, `transformLayer` sets
it for the length of its own command, and trims itself once it knows the
outcome: on the way out if the bake landed, not at all if it was rescued. The
redo stack is held aside and put back the same way — a bake that changed nothing
must not be the thing that took away a redo. `transformLayer` is the only caller
that sets it and it is cleared before that function returns.

**And the commit ceiling had to stop being a total.** It was one budget across
the whole bake at first, which is the truthful-sounding shape and was wrong
immediately: a plain seven degrees on a forty-drawing 4K layer wants about
23,700 destination tiles against a ceiling of 16,384, so the feature did not
work at all on the shots it is most wanted for. The message it produced was
wrong three ways over — nothing was scaled, it was not a drawing, and scaling
down would not have helped — which is
[#65](https://github.com/S-poony/Animage/issues/65)'s fourth bullet made
ordinary.

What actually has no bound is the **scale**: the bar goes to 10000%, a handle
drag is bounded only by where the pointer can reach, and the destination grows
as the square. The number of drawings is not that — forty cels at 100% ask for
about what the layer already holds, and the layer is in memory now or there
would be nothing to look at. So the ceiling is `kLayerGrowth` times what is
there, or the single-drawing budget, whichever is larger.

`kLayerGrowth` is **three, and it was two until the tests said what the count
actually does**. What the ceiling bounds is the *counted* destination, and that
count grows every source tile's footprint by a whole tile on purpose. On a large
drawing that margin is a rim and a rotation counts near 1.2; on a small one the
rim is most of the answer and the same rotation counts near 1.9. At two, whether
you could turn your layer depended on how big the drawings were, which is not a
rule anybody could hold in their head. Three clears a rotation at either size
and still refuses a scale to 200%, which is four times the pixels before the
margin — so the two cases it has to tell apart stay on opposite sides of it.

**And asking the ceiling had to stop being a per-move question.** Everything
above is asked again on every pointer move of a drag, because the answer is what
turns the box red. For one drawing that is 0.3 ms and was measured; for a layer
it was **10.9 ms a move** on a forty-drawing shot of ordinary line art — on the
interface thread, once per tablet event, on a gesture the pen is holding.

Almost all of it was `drawnBounds`, which asks every tile whether it is fully
transparent and so reads pixels until it finds one. Sixteen milliseconds for
forty drawings, and *none of it depends on the five numbers being dragged*. So
`LayerFootprint` gathers it when the gesture starts — the grids, each one's
drawn bounds, and the occupied-tile count — and the drag reads the same answer
back on every move: **0.17 ms**. The footprint owns copies of the grids rather
than pointing at the document's, which costs a hash map of shared tile handles
and buys a thing a live gesture can hold without a dangling pointer being
possible.

The exact count underneath it also stopped hashing. `destinationTileCount` marks
a dense bitmap over the box the destination lands in, where that box is small
enough to address, and keeps the hash set for the ones that are not. Same
coordinates, same answers — 24,000 random transforms across five grid shapes
were compared against the old implementation to be sure — and it helps the
single-drawing path too.

### The wait, and what is on screen during it

Seconds, on a long shot, with the interface thread held the whole time. Two
things have to be true through it, and neither was at first.

**The status bar has to say what is happening before it happens.** Reported: a
bake said nothing until it was over. The message goes up on
`layerTransformStarted`, which is emitted *before* the work rather than after
it, and the listener calls `statusBar()->repaint()` — not `update`, which would
queue a paint that runs after the wait it was meant to explain, so the message
would appear and vanish in the same frame having described nothing; and not
`processEvents`, which would paint but would also deliver input into the middle
of a gesture that is halfway through committing itself.

**And the float has to stay up.** `applyTransform` clears `transform_` before it
commits, because committing repaints and a repaint that still saw a live
transform would draw the float over the pixels it had just become. That is right
for one drawing, where the commit is over before anything could paint, and wrong
for a layer: the document has not changed yet, so what is still true is exactly
what was on screen before Apply, and a repaint arriving with the transform
already gone would draw the layer omitted and nothing over it. A blank canvas,
for seconds, which reads as work lost rather than work happening. So the layer
path resets it *after* the bake instead.

Nothing repaints the canvas during a bake today, so that one survived either way
and would have gone on surviving until something did. What found it was
`a-layer-being-baked` in `shots`, which photographs the window from inside the
wait — and grabbing the window *is* that something. Worth remembering as the
shape of thing that harness catches: not a wrong pixel, but a state nobody would
think to look at because it is not supposed to last long enough to see.

### Running out of memory, and why that is a rescue rather than a crash

The ceiling is a bound and not a promise. A bake's peak is the new tiles plus
the old ones the journal is keeping for undo — about four times the layer at the
ceiling — and a large enough layer can still exhaust a machine.

Everywhere else in this program an allocation failure on the interface thread is
a crash, and here it would be a crash **with the layer half rewritten inside an
open command**. So `Document::transformLayer` catches the `bad_alloc`, puts back
every drawing it had already written, and says so. The one other place that
swallows a `bad_alloc` is `CtgSolver`, and its comment says exactly why it could
afford to: nothing in the document is half written when a solve throws. This is
the opposite case, which is why it undoes rather than merely shrugging.

Three details, each of which would be a bug without it:

- **The catch is inside the command's own scope.** Letting the exception out
  would unwind through `~ScopedCommand`, and an exception escaping a destructor
  during unwinding is a `terminate` rather than an error.
- **The rollback is guarded on the history stamp**, not on a depth and not
  unconditionally. A failure before the first write leaves an empty command,
  which `endCommand` does not push at all — and undoing then would undo whatever
  the person did *before* the bake. A depth would not do either: the history
  trims from the bottom, so it can gain a command and lose one in the same breath
  and read the same.
- **The rescued command is dropped from the redo stack.** `undo` moves a command
  across rather than discarding it, and a redo of a bake that ran out of memory
  would put the layer back into the state the rescue had just taken it out of.
- **The history is not trimmed for it, and the redo stack it found is put
  back.** See "what it costs" above: closing the command was spending the
  session's undo on a command that was about to be thrown away. This is the one
  of the four that was found by reading rather than by using, and it is the one
  that made the refusal message untrue.

All four are pinned by tests, through a hook on `Document`, because running out
of memory for real is not something a test can arrange: a machine with room to
swap succeeds and is merely slow, and one without it takes the test process down
along with the assertion. The hook is a cost worth paying — the rescue is what
licenses bounding the growth instead of the total, and an untested rescue is one
that stops working quietly. The hook is taken by the next call to
`transformLayer` whatever that call does, so one armed before an identity cannot
lie in wait and go off on a real bake later.

## Importing a picture

**File ▸ Import ▸ Image.** A picture comes in as a `LayerKind::Reference`
layer: one that holds **no cels at all** and whose pixels are derived from the
imported file and memoised. Why it is that rather than an ordinary layer with a
drawing in it, and what a sequence and a video will be, is
[importing.md](importing.md). This is what was built and what it cost.

Read the section above this one first if you have not. Everything here is a
contrast with it: a layer transform **bakes**, and this is the one gesture in the
program that does not.

### Where an imported picture's pixels come from, in order

A sixth path, in the register of [how the program fits
together](#how-the-program-fits-together), because like the others it is not
written down in any one file it passes through.

**In.** `MainWindow::importImageFrom` decodes the file
(`image_import::decode` — QImage, colour-converted to sRGB and linearised into
half), makes a new track with one `Reference` layer and one drawing,
`TrackEnd::HoldLast`, and records the file under a name inside the project. The
picture is installed with `Document::setReferenceFrame`, which is **not an
edit**: no cel, no journal entry, no undo step. The menu item is a file dialog
and a recap in front of that function, and nothing else — which is what lets
`shots` and the tests drive an import without answering a dialog.

**Out.** `Compositor`'s `collectPasses` is the one place in the program that
resolves a layer to pixels, and the reference branch sits beside the colour
layer's and has the same shape: ask the document, draw what is there, start
nothing. What comes back is an ordinary `TileGrid`, so **nothing below
`collectPasses` knows this kind exists** — `compositeGrids`, the export's
per-layer path and the eyedropper all work with no changes at all.

**Kept.** `refreshEverything` calls `MainWindow::refreshReferenceFrames`, which
re-derives any layer whose picture is missing or was made at a placement the
layer no longer has. Cheap when there is nothing to do, which is nearly always:
the test is a hash lookup per drawing.

**On disk.** `imports/` in the project folder, carried forward by every save.
See the trap below, which is the one thing here that would have gone wrong
silently.

### A drawing carries a picture two ways, and a copy has to take both

`Image::cels` is how a raster layer has something at a drawing. `source_frames`
is how a *reference* layer does — an entry naming which frame of the imported
file this drawing shows. They sit beside each other on the `Image` and answer
the same question for two kinds of layer.

`copyOfImage` took only the first, so **a duplicated drawing came back without
its imported picture**. On a track that is nothing but an import the whole
drawing was blank, which is what it was reported as: *"Duplicate drawing fails
silently on an import."* It had not failed. It had succeeded and handed back an
empty frame.

**The mixed track is the case worth knowing about**, and it is the one that hid:
a track can hold a raster layer and a reference layer together, and there half
the drawing came through. It reads as having worked, and the imported half is
gone.

Nothing is refcounted in the fix, unlike a cel: the entry is an int naming a
frame of a file the layer already lists, and two drawings pointing at the same
frame of the same file is the ordinary case — it is what holding an imported
frame a second time means. `copyOfImage` is the only place in the program that
builds a copy of an `Image`, which is why this was one hole and not several.

### It stores where the picture goes, and does not bake it

The section above spends its length on why a layer transform bakes: a stored
affine would force everything that reads a layer's pixels through a matrix —
the brush, the eyedropper, `ctgBarrier`, `celBounds`, fit-to-drawing, export.

**That argument is entirely about layers whose pixels are the truth.** A
reference layer's are derived, so its placement is applied in the *derive step*
— decode, colour-convert, tile, `transformTiles`, cache — and what reaches
`collectPasses` is a plain, already-placed grid. `compositeScene` is still a flat
list of untransformed grids and `LayerPass` is still not widened. The thing #25
refused is still refused.

What it buys is that **the loss never compounds**: adjusting a placement
re-derives from the original file, so a picture nudged, scaled and nudged again
has been resampled once, from the bytes that came off disk. A baked one would be
a resample of a resample of a resample, and would look fine until the third
adjustment.

**The box opens at the placement the layer already has**, and that is not only a
courtesy. `Transform` holds `scale_x` and `scale_y` separately, so two of them do
not compose into a third — rotate, then scale non-uniformly, and the result is a
shear the struct cannot express. Absolute numbers that the drag *edits* avoid
composition altogether. A design that started the box at the identity and
composed onto the stored value would have hit that wall after the arithmetic was
written.

**The cache is keyed on the placement, and absent beats stale.** A frame derived
at an earlier placement is not a slightly-out-of-date picture, it is a picture of
where the import used to be — and it would go on being drawn, convincingly, until
something happened to refresh it. `Document::referenceFrameFor` therefore takes
the placement and reports a mismatch as *nothing here*. Same lesson as [why a
cache key of cel revisions serves wrong fills, not slow
ones](#why-a-cache-key-of-cel-revisions-serves-wrong-fills-not-slow-ones), from
the other end.

### What the box's colour means, which changed

**Blue is what you pointed at — your selection, or the drawing in front of you.
Green is more than that.**

It used to be blue for the tool and green for the layer button, on the grounds
that the two doors "write amounts two orders of magnitude apart". That reason
does not survive this feature in either direction: a placement writes *nothing*
and can still move forty drawings, and a one-drawing layer came through the layer
door and moved exactly the drawing in front of you — the blue case wearing green.

So the rule is about scope, and the cost signal it used to carry is left to red,
which is the urgent one anyway.

**What the colour deliberately does not carry is whether the lasso applies.**
That already announces itself and better: a gesture that ignores a loop clears
it, so the loop visibly goes. One signal per fact — and the drawing count has no
other signal, since a layer of two drawings barely has ghosts.

### A layer of one drawing is the Transform tool's job

`beginLayerTransform` hands over to `beginTransform` when the layer has one
drawing, and **that is what gave the lasso back**. The whole-layer path clears
the selection because a loop describes a shape on this drawing and nothing at all
on the other forty; with one drawing there are no other forty, and refusing the
lasso there while honouring it in the tool was a difference with no reason behind
it.

Everything else already agreed — the box is that drawing's bounds either way,
Apply writes one cel either way — and the ordinary path does it without the
bake's rescue, its deferred trim or its history cost.

Not for a reference layer, whatever the drawing count: there is no cel for the
tool to lift, and a placement is a property of the whole file. **There is no
lasso on an import for that reason and not because of the drawing count**, which
is a different sentence and needs to stay one.

### The Transform tool works on an import, and the doors do not disagree

`chooseTransformTool` routes a reference layer to the placement. Both doors mean
one thing when there is one picture, and refusing with a message pointing at the
other button would be a rule with no consequence behind it.

"Two doors and no switch" is untouched by this: what that refuses is a control
that changes the scope of a gesture *already on screen*, and this is decided
before there is one.

### Four labels that would have been false

Worth listing because each was true before this feature and silently stopped
being, which is the shape of thing this file exists to catch:

| | said | says |
|---|---|---|
| the transform bar | "Whole layer" | "Placing — nothing is written" |
| the panel button | "Transform layer through time" | "Place this picture" |
| Apply's tooltip | "Bake it into the drawing" | "Put the picture down here. Nothing is written" |
| `refuseToEditHere`'s comment | "The layer kind is not here" | which kind is, and why the rest are not |

The last one is the important one. That comment explained a deliberate decision —
the brush puts scribbles on a colour layer, so nothing on that list has to care
what kind of mark it is carrying — and a reference layer is the first kind the
brush itself must refuse, because there is nowhere to put a mark rather than
because of what kind of mark it would be. The reasoning is intact and still
decides the colour layer; the comment now says which is which.

Apply's tooltip is composed from the keyed-tooltip table like every other, so it
still names the right key after a rebinding. `setKeyedTipText` is how a tooltip
that changes with the gesture reaches into that table rather than around it.

### What it costs, and where the next line is

Nothing measurable yet, and that is the point: an imported picture contributes
**no cels**, so it costs a save nothing, costs the undo history nothing, and adds
nothing to `totalTileCount`. `an-imported-picture-placed-and-applied` in `shots`
photographs `tiles 0  undo 2 (0 MB)` after a placement, which is the whole claim
in one line.

**The re-derive runs on the interface thread**, from `refreshEverything`. That is
affordable for a still and will not be for a sequence: a decode you can feel on a
300 dpi scan is a decode per frame on an animatic. That is the line where this
grows a worker and the request path becomes `requestCtgFills`' — paint asks, a
worker computes, a poll installs — which is what [importing.md](importing.md)
already specifies and what `CtgFillCache` is the template for, bound and
generation counter included.

### What is not built

- **Video.** A sequence with a decoder in front of it and no new storage at all:
  extracted to frames once, at import, so `QMediaPlayer` never reaches the paint
  path. The shape is settled in [importing.md](importing.md).
- **Playing a soundtrack.** Importing one is built and so is its row — see
  [importing a soundtrack](#importing-a-soundtrack) — and the deployment spike
  is done ([what taking Qt Multimedia
  costs](#what-taking-qt-multimedia-costs)). What is left is `AudioDevice` and
  the scrub.
- **Convert to drawings**, which is the way back — an import cannot be a CTG
  barrier, so colouring imported line art needs it. Whole layer, on a popup, and
  it is `Document::transformLayer`'s loop with a decode where the resample is:
  one command, the `bad_alloc` rescue, the deferred trim, the redo stack held
  aside. All four are bugs if omitted and all four were found the expensive way
  once already.
- **Telling a reference layer from an ordinary one in the panel**, which is
  [#84](https://github.com/S-poony/Animage/issues/84) and is smaller than it
  sounds: `layerLabel` and `applyLayerFlag` already do exactly this for colour
  layers.

## Importing a sequence

**File ▸ Import ▸ Image sequence.** The still was deliberately the smallest
instance of this and the shape did not change: the same `LayerKind::Reference`,
the same no cels, the same derived-and-memoised pixels. What a sequence added is
three things one frame never needed, and each of them turned out to be a
different kind of problem.

### Which frame a drawing shows, and why position cannot answer it

`Image` has a second sparse map beside `cels`: `LayerId → source frame index`,
absent meaning the layer is empty at this drawing exactly as a missing cel does.
`Layer::reference_source` became `reference_sources`, a list, and **a still is a
list of one** — making the single picture the one thing that is not a sequence
would have meant answering placement, export, save cost and colouring twice for
two features a user thinks of as one.

**It is not a retiming feature and it is not provenance.** Without it, "which
frame of the source does this drawing show" has to be derived from where the
drawing sits, and position moves: add a hold and two drawings share a slot
index, delete a frame and everything after it shifts. The very first hold breaks
it, and adding the field afterwards would be a migration of every project with
an import in it. It is not keyed on `Image::number`, which
[track.h](../src/core/track.h) says is reused after a deletion.

`setSourceFrame` is an edit — journaled, undone, saved — unlike everything else
in that part of `Document`, because the pixels are derived and cost a decode to
lose while this is a fact only the file remembers. It rides on `ImageOp`, which
already swaps a whole `Image` and fixes the cel refcounts on both sides.

### A bound, and the rule that is not obvious

`ReferenceCache` is `CtgFillCache` with the words changed, and the resemblance
is the point rather than a coincidence: both are derived data, bounded in bytes,
kept on the `Document` rather than on the thing they describe, precisely because
losing an entry costs a rebuild and nothing else.

**A lookup renews an entry and a store does not.** That is the whole rule. A
scrub goes back and forth over a handful of frames, so the frames being *looked
at* are the ones that must survive; renewing on store would hold whatever was
decoded most recently, which during a scrub is exactly the frame you are
leaving. `aLookupIsWhatKeepsAFrameResident` drives it against a budget small
enough to fill, which is the only reason the bound is settable at all.

**Absent still beats stale.** An entry records the placement it was derived at
and a lookup at any other reports *nothing here* — a frame derived under an old
placement is not slightly out of date, it is a picture of where the import used
to be, and it would go on being drawn convincingly.

**And eviction may only happen where the document may be edited**, which does
not look like an invariant and so is written where the class is: `LayerPass`
holds raw pointers into these grids and `compositeGrids` reads them from several
threads.

The budget is 512 MB and is **not measured**. The arithmetic is on the constant
so it can be argued with.

### Where an imported picture's pixels come from, in order — and what changed

The still's version of this path had the derive step running from
`refreshEverything`. **That could not survive a sequence, and the reason is a
fact about the program rather than about cost:** a frame change goes
`TimelineWidget::setCurrentSlot` → `onSlotChanged` → `canvas_->setFrame` and
never touches `refreshEverything` at all. A still did not care, because every
drawing of its track showed the same picture and the only thing that changed it
was a placement, which does arrive there. A sequence shows a different frame at
every drawing, so scrubbing one would have shown frame 1 for ever.

So the ask moved to the paint, which is the one thing that reliably happens when
what is on screen changes — the shape [importing.md](importing.md) specified and
the one the colour layer already uses. `refreshEverything` now touches imports
not at all, and the absence has a comment on it saying so.

`ReferenceDecoder` is `CtgSolver` with a decode in it. Its header lists the
three places they differ rather than pretending they do not: a job names a path
instead of a document, nothing is abandoned mid-decode, and there is one kind of
question so a request is identified by the drawing and the layer alone. It takes
**newest first** off its queue, which is the opposite of the solver and is right
here — a queue that backed up during a scrub is a list of frames already dragged
past.

**The canvas still never works out a path.** Where an import lives depends on
whether the project has been saved and into which folder, which is MainWindow
state that moves on the interface thread — so the canvas is given a locator it
calls once, on the interface thread, at the moment a job is queued, and never
inspects what comes back.

### Three things that were wrong, and what each one taught

**A frame that will not read is remembered as an empty picture.** Not tidiness:
the paint asks for whatever is not in the cache, so a failed decode that left
nothing behind would be asked for again on the next paint and every sixteen
milliseconds after that, for as long as the window is open — a decode a frame,
on a worker, for ever. `aFileThatWillNotReadIsAskedForOnce` pins it.

**Leaving a frame is not a reason to cancel a decode, and copying the colour's
rule said it was.** A paint drops stale requests and then asks, so at
twenty-four frames a second every decode was called off forty-one milliseconds
after it started — and a frame that takes longer than that never finished.
Reported as a libpng warning repeating during playback; the warning was the
file's, and the *repetition* was every pass decoding every frame and throwing
all of them away. The import was never visible during a take at all.

The colour's reason does not transfer either. A `CtgJob` carries copies of the
tile grids it will solve from, so a queue of them that fills faster than it
drains is real memory; a decode job is a path and nine numbers. And **a decoded
frame is worth having even for a drawing you have left** — it is the frame you
will be on again next time round, and the cache it lands in is bounded.

**The colour had the same bug and it was worse there**, because `abandoned()` is
checked *inside* the max-flow: the solve gave up partway and produced nothing,
so no fill ever completed during playback and the cache never accumulated. Found
here, fixed there — [#85](https://github.com/S-poony/Animage/issues/85), and
written up in [the answer that was called off a moment before it
landed](#the-answer-that-was-called-off-a-moment-before-it-landed), which is
worth reading for the part that generalises: the benchmark that should have
caught it was warming the thing it was measuring.

### What it costs, measured

`bench_import` exists because a report arrived that nothing here could answer,
and it takes a folder so it measures the real files rather than made-up ones —
what a PNG costs depends on what wrote it.

It found the tiling loop paying three `std::pow` per pixel through
`srgbToLinear`, and a hash-map lookup per pixel as well. Six million
transcendentals for an HD frame: **145 ms of tiling against 21 ms of actually
reading the PNG.** A 65536-entry table — every 16-bit value there is, which is
every input that can arrive once the image has been widened — and the tile
lookup hoisted to once per tile per row take that to **35 ms**.

**Every pixel is bit for bit what it was**, and that is required rather than
nice: the table is a memo of `color.h`'s function and not a second version of
it, because the derive step must be deterministic — a frame that is evicted and
decoded again has to come back the same or the picture changes while somebody
scrubs over it.

On the 4000×2250 sequence that was reported: 94 ms a frame, of which **70 is
libpng reading a 3.7 MB file and is not ours**. Those frames are mostly
transparent, so each is 18 tiles rather than 576 — all 151 come to 339 MB
against the budget, and every pass after the first is cached.

### Saying so, which is not optional at these speeds

An import whose frames are not decoded draws nothing — correctly, since
compositing may not start a decode — so a take that runs before they are in
shows the playhead advancing over a blank canvas. **That is indistinguishable
from the program being broken**, and it was reported as exactly that.

The status bar gains `loading imported pictures: 43 of 151`, its own permanent
widget beside the playback rate and in the same red. Three decisions in it:

- **Shown while a decode is outstanding, not while frames are merely missing.** A
  frame is only asked for when it is on screen, so a sequence somebody scrubbed
  part of sits at 40 of 151 with nothing happening — and a number that does not
  move is worse than none, being exactly what stuck looks like.
- **The denominator is the whole sequence**, because during a take every frame
  is visited and the climb is the progress being waited on. "How many are being
  worked out right now" would read 1 throughout.
- **`ReferenceCache::has` exists because asking is not using.** The count asks
  about every frame at once, and going through `find` would renew all of them on
  every status update — flattening the eviction order that keeps a scrub's own
  frames resident, from the one place whose whole job is to report and change
  nothing.

### What the dialog asks, and the one thing it will not offer

Order is **numeric and not correctable**: the last run of digits in the name,
padding not part of the group, files that surround their number differently kept
as separate runs, and a name with no digits at all put last. So there is no list
to drag rows about in — and what replaces it is the recap *saying what the rule
did*, which is this program's house rule for input it will not refuse and will
not silently pick over.

Three things it says rather than offers, because none is a choice: a new track,
one drawing per file on 1s, and `TrackEnd::Nothing`. That last is deliberately
not the still's `HoldLast` — a modelsheet is meant to stay up for the whole shot
and an animatic is a stretch of timing that ends where it ends.

Two it offers: a start frame defaulting to 1, and half size — which is a
*placement* of 50% and not a separate mechanism, so it is undoable, adjustable
afterwards through Place this picture, and cheaper rather than dearer, the
derive step applying the scale so a frame caches a quarter of the tiles.

A file that will not read is kept in the list rather than dropped, because
position in that list is what each drawing points at: removing one would move
every frame after it onto the wrong picture. Same reason a missing number leaves
a gap.

**And it does not ask which track to land in.** That was settled the other way
in [importing.md](importing.md) and reversed on the user's call: the argument
was that `ctg_sources` resolve inside the track, so a colour layer could not cut
against an import that landed elsewhere — and it never asked where the colour
layer is. `addColourLayer` acts on whichever track is current and an import
makes its track current, so the whole chain sits inside one track. What that
gives up is named there rather than dropped.

### What is not built

- **Convert to drawings**, which is the way back — an import cannot be a CTG
  barrier, so colouring imported line art needs it. Whole layer, on a popup, and
  it is `Document::transformLayer`'s loop with a decode where the resample is:
  one command, the `bad_alloc` rescue, the deferred trim, the redo stack held
  aside. All four are bugs if omitted and all four were found the expensive way
  once already.
- **Telling a reference layer from an ordinary one in the panel**, which is
  [#84](https://github.com/S-poony/Animage/issues/84) and is smaller than it
  sounds: `layerLabel` and `applyLayerFlag` already do exactly this for colour
  layers.

## Importing a soundtrack

**File ▸ Import ▸ Audio.** A sound comes in, is decoded, is copied into the
project, and gets a row in the timeline it can be moved and cropped in. **It
does not play yet** — the device that would make a noise is built and so is the
mixing that feeds it, and nothing has been wired to the playhead. What that
leaves is set out at the end of this section.

Why audio is not a `Track`, what scrubbing is for and what the playback clock
has to be derived from is [importing.md](importing.md). What taking Qt
Multimedia cost, measured before a line of this was written, is
[audio-spike.md](audio-spike.md). This is what was built.

### Audio is its own list, and that is what keeps everything else meaning what it meant

`Scene::audio_tracks` sits beside `Scene::tracks` rather than being a kind of
one. The specification has it that way and the code makes it sharper: `Track`
carries layers, slots, an image map, drawing numbers, `overwrite_drawings`,
`TrackEnd`, `blend`, `celSourceFor` and `nearestWithCel`, and audio answers *not
applicable* to every one. About twenty places walk `scene.tracks`; a kind flag
would put a guard in all of them.

`theSceneCarriesAudioTracksBesideItsTracksAndNotAmongThem` asserts the property
that argument rests on rather than assuming it — adding a soundtrack leaves
`scene.tracks` empty, so every one of those loops is untouched.

**It does not enter `shotFrames`.** A shot's length is what the drawings make it
or what the scene was told; a soundtrack running long is reference, and a scene
that grew when one was imported would be taking the shot's length from the wrong
thing. What it *does* enter is `Document::timelineFrames` — see below.

### The one line that will make lipsync right, built before anything can play it

`slotForPlayedFrames` derives the picture's slot from how much audio has come
out of the device, replacing a slot derived from the system clock. Three of the
four ways two clocks come apart stop existing rather than being separately
corrected: the device's output latency is already inside the count, a loop seam
wraps both together because there is only one number, and an interface stall
cannot reach a number that is not counted on that thread.

**It takes the sample count as an argument, and that is a requirement rather
than a style.** The runners have no audio device, so anything opening one there
fails or hangs — which means the arithmetic the whole of lipsync rests on is
exactly the part that could never be tested if it lived inside something owning
a `QAudioSink`. `test_audio` drives it with a fake and pins the loop seam and
the stall case on every platform on every push, with no hardware at all. The
precedent is `exporting::Solve`.

Four things in it that are not obvious, each of which was a bug avoided rather
than a style choice:

- **`played * fps / rate` in one step.** Going through whole milliseconds
  truncates a fortieth of a frame away on every tick.
- **A count that goes backwards is clamped.** A driver misreporting across a
  restart would otherwise turn a small negative into a colossal slot through the
  unsigned arithmetic — a picture on a random frame, which is much harder to
  recognise as a fault than a picture that has stopped.
- **`sampleForSlot` floors rather than truncates.** Truncation rounds towards
  zero, so a slot a fraction *before* the sound would come back as sample 0:
  audible, on a frame that should be silent, and only on the negative side.
- **A negative index says how far before the sound the slot is**, rather than
  being a sentinel. A caller plays silence until it reaches zero and then reads
  on, which is what a sound placed halfway into a frame needs.

### It decodes on the way in, where a picture does not

A sequence points its drawings at files and lets the paint ask for them, because
two hundred decodes on the interface thread is not a thing to do. A soundtrack
cannot: what would ask for it later is a device callback that must never wait on
a disk. So `audio_import::decode` reads the whole file before the track exists —
one file, tens of milliseconds — which is also what lets the recap say how long
the sound is.

It runs a **nested event loop**, because `QAudioDecoder` is asynchronous by
construction and there is no call that returns samples. The header says why that
is safe here and where it would not be. Three things in it are bugs if dropped:

- **Every buffer is read at its own format.** Setting a format is a request; a
  backend may hand back its own, and reading Int16 bytes as Float is a scream
  through somebody's headphones.
- **A silence timeout.** A backend that fails in a way it has no error for
  simply stops emitting, and the loop would otherwise spin for ever behind a
  modal dialog — which from the outside is the program hanging on a file
  somebody double-clicked.
- **Int16 divides by 32768, not 32767**, or a full-scale negative sample comes
  out past −1.0 and clips on the way to the device.

The decoded `AudioClip` is **derived data on the `Document`**, like a reference
frame's tiles and for the same bargain: losing it costs a decode of a file
sitting in the project folder. It is unbounded unlike the reference cache,
because a shot's worth of PCM is single-digit megabytes where one HD picture
frame is 17. `refreshAudioSamples` re-decodes after a load and nowhere else.

### The timeline reaches the sound; the shot is asked before it does

Two different questions, and the split is what makes both answers right.

`Document::timelineFrames` is the scene's own answer widened by any decoded
soundtrack. **It is on the Document because the Scene cannot answer it** — a
clip is derived data held here, so this is the one object that knows both the
tracks and the sound. The canvas and `stepFrame` moved onto it too, or the
playhead would be in two places at once: the strip showing it out over the sound
and the canvas showing an earlier frame.

The **shot** is a separate matter, and importing asks. Playback derives its slot
from `Scene::shotFrames`, so a one-second soundtrack in a shot of one drawing
played one frame and stopped — widening the timeline lets the playhead be
*dragged* over the sound and does nothing for Play. So the import dialog offers
to make the shot reach the end of the sound, and the rule is:

> **The box appears when it would change something, and is ticked only when
> nothing has decided the length yet.**

No box when the sound fits; ticked when no length is fixed; offered and unticked
when one is — saying how long a shot is is a decision, and an import has no
business overruling one already made. It never *shortens* the shot, whatever
offset is picked: the box says "reach the end of the sound", and a shot that
shrank would take drawings out of the export.

`scene.h` already named animating to a soundtrack as the case `fixed_length`
exists for, which is worth knowing before arguing with any of this.

### Two selections where the timeline had one

`MainWindow::track_` is read by five things — the canvas, the layer panel, the
Track menu, the drawing buttons and the status bar — and every one of them wants
a real `Track`. Clicking an audio row through the old path would have handed
`findTrack` an id that is not one: nothing to point at, the brush stops working,
and nothing says why.

So the timeline has a **narrow selection** (`track()`, only ever a drawing
track) and a **wide one** (`highlightedAudio()`, which may name something that
is not a `Track` at all and therefore may not be read by anything needing one).
Clicking a soundtrack row moves the highlight and leaves the brush where it was.

**Three things a soundtrack row walks into**, found by auditing every place that
assumed a row is a track:

- The paint loop dereferenced `trackAt` without asking. Every other call site
  already guarded — `trackAt` has always answered null past the end, and what
  changed is only that there is now something *after* the end.
- Restacking was bounded by `rowCount()`. A drawing row dropped below the
  soundtracks would hand `moveTrack` an index past the list it indexes, so it is
  bounded by `drawingRowCount()` now.
- `renameAt` would have opened an editor over a soundtrack's name and written
  the result to a track, which is the exact confusion the two selections exist
  to prevent. It renames soundtracks now, and what makes that safe is
  `renaming_audio_`: which list the id belongs to is settled when the editor
  opens rather than guessed when it closes.

#### What the two selections owed the interface, and did not pay until it was reported

The split above is right and stays. What was wrong is that **the interface drew
it as one thing and the Track menu read the wrong half of it**, and both were
reported from use in the same breath — as *"you can't delete an audio track; it
would be simpler if you could only select one track at a time"*.

That conclusion is the natural one to draw from what was on screen. A drawing
row was painted current whenever `track_` named it, and a soundtrack row was
painted current whenever `audio_row_` named it, in the same colour — so clicking
a soundtrack lit **two rows at once**, which is what two selections look like.

Collapsing them would cost more than it saves, and the cost falls on the exact
gesture a lipsync shot is made of: with one selection, clicking a soundtrack row
to nudge the sound half a frame puts the brush away — no track for the canvas,
an empty layer panel, dead drawing buttons — and getting it back means clicking
a drawing row again. Every single time you touch the sound.

So the two facts stay and are drawn as two facts:

> **The fill means "pointed at". The washed-back fill means "the brush lives
> here".** One row has the first at any moment.

- While a soundtrack is highlighted, the drawing track's gutter is the highlight
  colour at a third of its strength and its name drops back to ordinary text.
  It is the same colour washed back rather than a new one, so it is right in a
  dark theme for the reason everything else in this palette is.
- **A stroke landing on the canvas takes the highlight back**, through
  `clearAudioHighlight`. Drawing is the unambiguous statement that you are done
  with the sound, and without it a soundtrack clicked once stays lit for the
  rest of the session while every stroke lands somewhere else.
- **The Track menu acts on the row you are pointed at.** Rename and Delete
  follow the highlight; "Overwrite drawings" and "Past the last drawing" grey
  out while a soundtrack is highlighted, because neither means anything for one
  and both would otherwise act on a row nobody pointed at. `highlightChanged`
  is what tells the menu, and it exists because `trackChanged` cannot: clicking
  a soundtrack row deliberately leaves the current track where it was.

**A submenu whose every item is greyed still opens**, which is a thing worth
knowing because it looks fixed and is not. "Past the last drawing" had its three
items disabled on a soundtrack row and went on opening to offer them — a menu
that offers a choice and then refuses all of it says *there is something here
you may not have*, where one that does not open says *there is nothing here*.
The second is the true one. `QMenu::menuAction()` is what has to be disabled,
and holding the submenu as well as its items is the only reason `end_menu_`
exists.

#### The ruler is pinned, and the option that looked better was the same picture

Same report, same cause: **soundtracks are under every drawing row**, so a scene
with a few tracks in it is one you scroll down to reach the sound — and the
ruler is where scrubbing happens, which is the only way to hear that sound. A
ruler that scrolled away with the rows took the scrub band off the screen
exactly when it was wanted.

It is pinned **inside** the scrolled widget: `setRulerTop` is the scroll area's
vertical position, the band is painted last so it covers what slides under it,
and every gesture asks `inRuler` before it asks which row it is on.

**The alternative was to lift it out of the scroll area entirely**, into a strip
of its own above the viewport, and that was argued for here on the grounds that
a pinned band inside the widget *hides content*. **That was wrong and it is
worth writing down why**, because it is a plausible-sounding mistake. Taking the
ruler out reserves its 18 pixels above the viewport; leaving it in spends the
same 18 covering the top of it. Either way the row at the top of the view is cut
off by the same amount and comes out from under by scrolling. Neither costs the
dock any height — `syncTimelineHeight` moves it by *row differences*, and the
ruler is part of the fixed surround in both. Same picture, one sixth of the
code, and the end-of-shot line stays one `drawLine` through both bands instead
of being split across two widgets.

The general shape of the error: **two designs that differ in which side of a
boundary a fixed cost sits on are the same design.** What would have made the
split worth it is a fact about the *content* — something the band must never be
able to cover — and there is none here.

**Renaming a soundtrack is allowed, and the objection to it is answered rather
than dismissed.** It was raised that a user should perhaps not be able to rename
an import at all. The thing that makes it safe is that an `AudioTrack` carries
two separate strings: `name`, a label, and `source`, the file in the project's
`audio/` folder, which nothing here touches. What renaming genuinely costs is
the row's last visible link to the file it came from — so the row has a tooltip
saying which file that is. Two takes both imported as `dialogue` is the ordinary
case the rename exists for.

### The row: one shape carrying three facts

Painted by `paintEvent` and hit-tested in `mousePressEvent`, **not a `QSlider`**
— a widget placed on a row disables that row's own hit testing, which this file
already records for the rename editor and the layer panel already paid for.

The block is where the sound sits; its *height* is the level, so at the bottom
it is silent and no separate mute is needed; its *ends* are where the crop is.
The extent stays visible at any level, because a row that vanished when it was
silenced would be a row nobody could grab to bring back.

**And its top edge is the waveform**, which was left out of the first cut and is
in now. [importing.md](importing.md) put it out on the grounds that a labelled
bar is enough to *place* a sound and peaks are a second derived thing to build
before anything is audible. That was right, and it expired the day scrubbing
worked: a bar says where the sound is, and somebody reading a track needs to see
where the syllables are.

**It is the block's own top edge and not a picture laid over it**, which is what
keeps all three sentences above true. The whole shape is scaled by the gain, so
turning the sound down flattens the syllables with it and at the bottom there is
a flat line — which is what silent looks like, and is the same statement the
height was already making. The alternative, a centred waveform with a level bar
behind it, would have taken the level off the shape and needed a second thing to
carry it.

Two decisions in it that are not obvious:

- **Normalised to the file's own loudest moment, not to full scale.** A dialogue
  take recorded at a sensible level is a low ripple against full scale, with no
  syllables in it — and a waveform that cannot be read has not earned its row.
  So the shape says *where the sound is* and the block's height says *how loud
  it will be*. What that costs is that two takes recorded at different levels
  look equally loud.
- **A quiet passage draws a one-pixel line rather than a gap.** Without the
  floor the row breaks into islands, which reads as a sound that is not there
  rather than a sound that is soft — and the gaps are also where somebody has to
  grab to move the block.

**And the level got a line of its own back, which the waveform had taken away.**
Before the shape came from the sound, the top of the fill *was* the level and
there was nothing else it could be. With a waveform the top of the fill is the
loudest syllable in view and everywhere else it is lower — so the number the
drag is setting had no edge left. The line sits where a flat block would have
ended: the waveform touches it at the file's loudest moment and stays under it
everywhere else. Reported from use, on the first row anybody dragged after the
waveform landed.

`AudioPeaks` in `core/audio_peaks.h` is what it draws from: one bucket per 64
frames of audio, built when a file decodes and thrown away with the samples.
Derived from derived data, never written to a project, about 30 KB for a
ten-second take. **The bucket is narrower than a pixel column by construction** —
a cell is 26 pixels and a frame at 24 fps is 2000 samples at 48 kHz, so a column
is about 77 — which is what stops the row drawing a shape it invented between
two of them. It is rectified rather than signed, because a shape rising from the
bottom needs one number per column and not a pair.

A trap worth keeping, from the situation that photographs it: **a tone at one
level draws as a flat-topped block**, which is exactly what this row looked like
before. `writeTone` in `tests/shots.cpp` shapes its amplitude into six bursts
for that reason — a shot taken with a flat tone would have come back green
having shown nothing.

### Three gestures, one of them decided rather than chosen

Dragging an end crops. Dragging the body sideways moves the sound along the
shot. Dragging it up and down sets the level.

The ends are unambiguous and start on the press. The other two share a press and
are told apart by **which way the pointer goes first** — the same way this file
already tells a drawing drag (along a row) from a track restack (across one),
except that both of these start inside the row so the threshold decides rather
than the side of the gutter.

**A press that never moves opens no command and applies nothing.** Selecting a
soundtrack must not change it, which the first version got wrong by setting the
level to wherever the click landed.

**Nothing rounds to a frame**, on the user's call, and there is no snap
modifier. 1/24 of a second is 42 ms, which is most of the way to a syllable, so
a sound placed to the nearest frame is not placed at all. A pixel is 1/26 of a
frame at this cell width, which is the precision the gesture actually has.

Every drag is computed *from* the placement as it was when the press landed,
never by accumulating deltas — the same reason the transform box holds absolute
numbers. Accumulating makes the result depend on how many mouse events arrived,
which is not something a person can aim at.

### Two units, and neither is the other one's

**The offset is in frames and the trim is in seconds.** That is not an
inconsistency waiting to be tidied: it is what keeps both numbers correct after
somebody changes the scene's frame rate.

| | is a fact about | so a rate change |
|---|---|---|
| `offset_frames` | the shot — you placed the sound so a consonant lands on the drawing at frame 12 | leaves it on that drawing |
| `trim_start_seconds`, `trim_end_seconds` | the sound — "start 0.3 seconds into the take" | leaves it pointing at the same moment of audio |

In the other units each would drift, and the drift would be invisible until
somebody wondered why their lipsync had moved.

**Cropping the front moves the in-point and the offset together**, so the audio
under every remaining frame is the audio that was there before. Moving only the
trim would slide the whole take earlier, which is a different gesture and not
this one. `croppingTheFrontMovesTheReadHeadAndNotTheSound` pins it by asserting
that a given slot hears the same sample before and after a crop.

The crop is **non-destructive by construction**: two numbers, no samples
touched. It undoes by putting them back and can be taken out to the whole take
at any point. It is bounded so a frame of audio always survives, because a sound
trimmed to nothing draws no block and a row with no block is one nobody can take
hold of.

### The device is a seam, and what goes through it is a value

Two files, built before anything uses them, and both of them are shaped by the
same fact: **the thing that plays a sound runs on another thread.**

`audio_device.h` is the seam [importing.md](importing.md) asks for — open at
rate R, receive a callback asking for N frames, report what has come out, stop.
It is the only place in the program that knows what a `QAudioSink` is. Three
things in it are decisions rather than shape:

- **It is opened once and kept open.** The spike measured `QAudioSink::start()`
  at 335 ms, and a scrub is a burst of sound on every frame the playhead is
  dragged past. A device opened per burst would be silent for eight frames and
  then say something about the ninth. So the device stays and the *content*
  changes underneath it, which is why the callback is handed in once and must
  answer with silence rather than with nothing.
- **`playedFrames` is `processedUSecs()` as it comes**, with no
  buffer-in-flight subtraction, because the spike measured that it counts audio
  played *out*. Microseconds to frames in one step, for the reason
  `slotForPlayedFrames` does its own arithmetic in one step.
- **The rate asked for is the clip's**, so the ordinary case is an exact
  sample-for-sample read; a driver that refuses it says what it will take
  instead, and the renderer resamples. A refusal costs quality, not sound.

`core/audio_render.h` is what the callback calls. `renderAudio` turns an
`AudioProgram` — the soundtracks, their placements, a rate, a start slot, and
optionally a loop length and a burst length — into interleaved floats, and it is
a **pure function of its arguments**, which is the same requirement
`slotForPlayedFrames` is built to and for the same reason: a runner with no
sound card can still check the loop seam, the sub-frame offset, the trim and the
resampling. `test_audio_render` does, with no hardware.

Two things in it that are not obvious:

- **A program holds its clips by shared pointer, and that is about the thread
  rather than about sharing.** `Document::audioSamplesFor` answers a raw pointer
  into a map that an import or an undo may rehash, which is fine on the
  interface thread and fatal on a device's. `Document::sharedAudioSamplesFor` is
  the one accessor built for the other thread, and taking a share costs one
  atomic increment and no samples.
- **The interpolation is what makes the matched-rate case right**, not only the
  resampled one. An index that should land exactly on sample N arrives as N
  minus a rounding; interpolating gives sample N back, and taking the nearer of
  the two neighbours gives N − 1. Swapping it for nearest-neighbour reddens
  tests that have no resampling in them at all, which is how this was found.

### On disk

`audio/` in the project folder, carried forward by every save exactly as
`imports/` is and for the identical reason: nothing in the document can rebuild
a sound. The carry-forward is now **one function run twice** rather than a loop
written twice, which is this repository's own rule about extracting when the
second caller arrives.

`imports/` and `audio/` are two namespaces on purpose — a picture and a sound
may both be called `take-3.x` without either shadowing the other.

**The format version went to 4**, in two steps and for two reasons, both of them
the same standard: bump when an older build would get it *wrong*, not merely
when it would not understand.

- **3 is soundtracks.** A build that does not know `audio_tracks` drops the key
  and autosaves over the project without it — and unlike a cel there is nothing
  in the document to write it back from, so the file would sit in `audio/`
  orphaned, with the placement that timed the shot to it gone.
- **4 is the trim and the fractional offset.** A build reading `offset_frames`
  as an integer puts a sound placed at frame 12.4 back on frame 12 — 17 ms,
  which is where a consonant lives — and drops the in and out points entirely,
  playing whole takes where a line had been cropped out of one.

A project with no sound writes no `audio_tracks` key and an untrimmed sound
writes no trim, so every file that existed before each step is the same bytes it
was.

### What it costs

Nothing a save can see: the samples are derived and are never written. In memory
it is about 384 KB a second of decoded float, which is what the import recap says
— **beside the size of the file**, because a 799 KB mp3 announcing 7.5 MB reads
as the program having gone wrong and does not stop reading that way until the two
numbers are shown together. That was reported from use on the first file
somebody imported that was not a WAV.

### What is not built

- **The scrub**, which is the first thing that will make a noise: on each frame
  change while dragging, about one frame's worth of sound from that position.
  `AudioDevice` and `renderAudio` are both built and are what it is made of —
  see above — so what is missing is the part that decides *when* a burst
  happens and holds the device open while somebody is working.
- **Synchronised playback**, which is `slotForPlayedFrames` wired into
  `onPlaybackTick` in place of the wall clock. Both halves are built and tested;
  what is missing is a device that is running, to ask for the sample count.
- **More than one soundtrack** — the model is a list and the row loop walks it,
  so what is missing is only the interface for a second one.

### And what a video will not share with this

Worth saying because it looks like it should. A video is [extracted to frames at
import](importing.md#video-is-a-sequence-with-a-decoder-in-front), so its row is
a **track row with cards**, not a block: moving it in time is a slot operation
and cropping it is a question of which drawings exist. Neither reaches
`AudioPlacement`, and a video does not want sub-frame placement at all.

So the reuse point, if there is one, is the *gesture* code in `TimelineWidget` —
drag the body to move, drag the ends to crop — and not the data. Pre-shaping the
model for a second caller whose shape is different would have been reuse in name
only, against this repository's own rule of extracting when the second caller
arrives.

## What a pan costs

Reported as the program going heavy after a while at work — panning, with
drawing still fine. It was the onion skin, and it read as "after a while"
because onion skin costs nothing until there are neighbouring drawings to show.

The cache reaches 64 screen pixels past the viewport, so a drag leaves it every
64 pixels of travel. When it did, the whole cached region was composited again —
and because a rebuild sets `onion_dirty_`, the onion buffer was rebuilt from
nothing beside it: a composite of each neighbouring drawing over the whole
region, then a pass over the whole buffer per ghost to tint it. The neighbouring
drawings had not changed. Only the region had moved, by 64 pixels out of a
viewport.

| a pan step, two ghosts | before | after |
|---|---|---|
| the onion's composite | 9.65 ms | 1.42 ms |
| the onion's tint | 4.69 ms | 1.18 ms |
| the scene composite | 3.77 ms | 0.65 ms |
| linear → sRGB | 6.98 ms | 2.66 ms |
| Qt's blit and the overlays | 1.46 ms | 1.51 ms |
| **the whole paint** | **29.28 ms** | **7.61 ms** |

Three changes, and the third is the one that matters.

**`Framebuffer::resize` stopped emptying the buffer.** `compositeGrids` calls
`resize` and then `clear`, so every composite in the program wrote a
viewport-sized buffer to zero *twice* before compositing anything into it — 20 MB
written twice at 1642×777 entries, once per ghost as well as once for the scene.
`resizeCleared` is the one that still does both, in a single pass, for the
callers that want an empty buffer of a given size.

**The tint loop is split across threads**, the way the sRGB conversion beside it
and the compositor under it already were. It was the last loop in the display
path still running on one.

**And the cache scrolls rather than being rebuilt.** The sampling grid is
anchored at the image origin and not at the cached region — `refreshRegion`
already depended on that to make a partial refresh land on the entries a full
one would produce — so entry *e* covers the same image pixels wherever the view
has panned to. The entries the old cached region and the new one share are
therefore copyable as bytes, and only the strips that are new have to be
composited. The onion buffer is scrolled in lockstep, and has to be: the sRGB
conversion reads one onion entry per cache entry, so a display entry carried
across without its ghost would pair a drawing with a neighbour from the wrong
place.

Two things fell out of that. `pending_dirty_` is a short list rather than one
rectangle, because a scroll exposes an L and the union of an L is very nearly
the whole viewport — which is the cost the scroll exists to avoid; past six
regions they are merged after all. And `rebuildOnion` split in two:
`collectGhosts` decides which neighbours are shown, which a pan does not change,
and `paintOnion` puts them into one region of a buffer that already exists,
emptying that region and no more of it.

**A zoom keeps the old path in full.** It changes the step, so an entry stops
covering what it covered and nothing in either buffer means anything; there is
nothing to carry and it all gets composited again.

**And the one time the buffers are handed back.** Keeping them is the whole
point above, so releasing them is on the onion *setting* being turned off and on
nothing else — `CanvasWidget::setOnion`. There are now two of them: the ghosts,
and the part of the layer stack they sit over, which issue #77 added when the
onion moved out from under the whole document. Both are screen-sized, so
together they are around 50 MB at an ordinary window and near 300 MB at 4K
maximised, and `Framebuffer::resize(0, 0)` gives none of it back — shrinking a
vector never reduces its capacity. `Framebuffer::release` does, and
`turningTheOnionOffGivesItsMemoryBack` in `tests/test_render.cpp` pins both
halves: that switching off frees, and that a frame which merely *shows* no
ghosts does not. The second half is the one that matters, because
`collectGhosts` comes back empty during playback and on any frame with no
neighbour — releasing there would hand back a screen-sized allocation and ask
for it again on the next frame, once per frame while scrubbing, which is exactly
the cost this section exists to have removed.

What pins it is `panning leaves the same pixels a full recomposite would` in
`tests/test_render.cpp`, which pans through the scrolling path and compares the
result to `refreshAll` on the same view, pixel for pixel, at three zooms and in
both directions, with onion skin on. It is worth keeping honest: the way this
fails is a drawing that looks right until you pan and then shows a band of
somewhere else, and no stopwatch would notice. Moving the copy's source column
by one pixel fails it on twelve checks.

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

## What a commit is allowed to cost

Issue #40, and the shape of it is the one this file keeps running into: **the
preview and the commit are different orders of work, and only one of them is on
screen.**

`transformTiles` rasterises every destination tile with anything under it, and
nothing bounded how many that was. The scale has no ceiling on either route in
— the bar's own field goes to 10000%, and a handle drag is bounded only by
where the pointer can reach — while the destination grows as its square. So a
full-frame HD drawing at the top of the field asks for something like 137 GB and
several minutes, and the program dies partway through it.

What made it a trap rather than an obvious mistake is that **the float previews
through `buildTransformPicture`, which is bounded absolutely** — one composite
of at most 2048 pixels on its longest side, blitted through a `QTransform`. That
picture looks exactly as good at a scale that cannot be committed as at one that
can. Nothing on screen was a function of the thing that was about to go wrong.

**The guard is a budget on the commit, and the answer is on screen before Enter
is pressed.** `kCommitTileBudget` is 16384 destination tiles — two gigabytes,
four times the history's whole default budget, about a scale of ten on a
full-frame HD drawing and thirty-four on a small lasso. `commitFitsInBudget`
answers whether a commit would stay inside it; the box, its handles and its knob
are red for as long as the answer is no, and `applyTransform` refuses rather
than rasterising.

**The status bar and not the box is what carries it, and that is not a
fallback.** An over-budget box is at least sixteen thousand pixels across
whatever shape it is, so at 5% -- the furthest the canvas zooms out -- its
nearest edge is still off the viewport. Red works while you are *dragging*,
because the corner under the pointer is on screen by definition, and at the
smallest over-budget sizes zoomed right out. It shows nothing at all when a
number is typed into the field, which is the other route in. So the line goes up
when the ceiling is crossed and comes down when it is crossed back, with no
timeout in between: it describes a state, and a timed message says the box is
red after it has gone blue again.

**Except where there is no box left**, which is one refusal and only one: a
layer bake that ran out of memory has already put the layer back and taken its
own transform down, so nothing will ever call `syncTransformFields` again and
the standing message would stand for the rest of the session. The handler asks
`transformIsLive()` rather than asking which reason it is — what decides it is
whether there is still something on screen the message is about — and gives the
other case a timeout.

Four things about it were decisions rather than mechanics.

- **It counts from the source, not from the destination.** The destination is
  the thing with no bound, so walking it costs whatever it costs — 64 ms at
  10000% on an HD drawing, and four times that at 20000%. Walking the occupied
  source tiles instead is a walk over tiles that already exist, and stopping the
  moment the count passes the budget makes the worst case *the budget*: the same
  answer in the same fraction of a millisecond at 200% as at 10000%. A question
  the interface asks on every move of a drag can only be shaped like that one.
  There is also an `O(1)` box test in front of it, so most of a drag never counts
  a tile at all.
- **It is conservative, and in the direction that matters.** A destination tile
  is wanted if the *axis-aligned box* of its inverse-mapped square reaches
  occupied source, and that box is wider than the square it came from — so each
  source tile's footprint is grown before it is counted, and the guard says no
  slightly sooner than the resampler would say yes. `test_transform` asserts the
  half that matters against the resampler itself rather than against a second
  copy of the arithmetic: whatever fits, `transformTiles` then stays inside.
- **Nothing is clipped, and that is the point.** The canvas is unbounded on
  purpose. A commit that quietly cropped what it wrote would be a worse bug than
  the one being fixed, so a transform that will not fit is refused whole and the
  drawing goes anywhere it likes at a scale that does.
- **A refusal leaves the transform live, except where something is leaving.**
  Pressing Enter on a box that is too large writes nothing and keeps the float
  up, because scaling it back down and pressing Enter again is the whole of the
  remedy. But six callers commit *because you are leaving* — changing frame,
  track or layer, picking another tool, pasting, closing the document — and none
  of them can carry a float out or be left half done. Those go through
  `settleTransform`, which commits or, failing that, puts the drawing back where
  it was picked up from.

**What the status bar was saying about this, and now is not.** `undo N (X MB)`
counts `Command::retainedBytes`, which is the tiles a command *displaced* — and
a transform's new grid lives in the cel, uncounted. So the commit that creates
388 MB reports 20 MB, and that 388 MB appears only after the *next* commit
displaces it. The number is one command behind on exactly the quantity this bug
is about, which is worth knowing before it is used to judge a report.

**What is not covered.** The budget is a constant with the arithmetic beside it,
and it is the arithmetic that should be argued with: somebody working at 4K who
is refused a scale they had a reason for is the report that moves it, the way
the history budget's number is meant to move. Nothing warns *before* a gesture
that it is heading for the ceiling — the box goes red when it crosses, and that
is all. And the drag's own arithmetic is untouched: it is absolute rather than
accumulating (`scale = (wanted · arm) / |arm|²`, measured against the fixed
`live.bounds`), so a second drag replaces the first rather than multiplying it,
and there was never a runaway there to clamp.

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
- *Drawn cursors* for the things the system has no glyph for: rotation, the
  eyedropper on Alt, and the eraser. The first bitmap cursors in the program.
  Light under dark, the rule the transform box already follows, because a cursor
  crosses paper and ink by definition. The crosshair joined them later for a
  different reason — see [the crosshair](#the-crosshair-is-drawn-here-too).
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

### The crosshair is drawn here too

Added after the rest, and it breaks the rule the other three were chosen by:
**the system has a crosshair, and it is drawn here anyway.** The reason given
was that `Qt::CrossCursor` is too big and too thick to draw under — on Windows
it is a thirty-two pixel cross of three-pixel line, and what it covers is the
line art you are aiming at. `buildCrossCursor` is eleven pixels across and one
thick.

**One pixel wide cannot be stroked.** A one-pixel pen through antialiasing lands
as two grey ones wherever it is not exactly on a pixel boundary, so the glyph is
set a pixel at a time and the pale edge grown around what was marked, rather
than painted underneath it the way the rotate, eraser and eyedropper glyphs are.
That difference is the method, not a preference: at eleven pixels the
antialiasing *is* the glyph. Curved glyphs drawn the same way were tried
alongside it — a rope loop for the lasso, a rubber for the eraser — and rejected
as looking worse than the stroked versions they would have replaced. Straight
lines on the pixel grid survive that treatment; curves do not.

**What it cost elsewhere: the shape assertions stopped discriminating.** Every
drawn cursor answers `Qt::BitmapCursor`, so every test that named
`Qt::CrossCursor` names `BitmapCursor` now — and where a test used the shape to
tell the brush from the eraser, it no longer can. The `pointingOf` check beside
each one is what separates them, which is why those were always asserted in
pairs.

**`LatencyCanvas` still sets `Qt::CrossCursor`** and is deliberately untouched:
it is the M0 latency harness, where the pointer is the thing being measured
against a crosshair the widget paints itself.

The eraser and the lasso were left alone. Showing the eraser's rubber *and* a
crosshair together was drawn up and set aside, not refused — it is a bigger
bitmap than any cursor here uses, and whether Windows draws a 44-pixel cursor at
44 pixels is unverified.

## Looking at the interface

Issue #28, and the shortest section here that is worth anything: `tests/shots.cpp`
drives the real `MainWindow` through a list of named situations and writes one
PNG each.

```bash
./build/tests/shots            every situation, into build/shots/
./build/tests/shots --list     their names and what each is for
./build/tests/shots transform  only the ones whose name says transform
./build/tests/shots -platform windows   the style the program really runs
```

**These are pictures of the Qt on this machine, not of the Qt anybody
downloads**, and that is the one trap in it. Two gaps hide in there and only one
has a flag. The default run is offscreen, which has no native style, so Qt gives
it *fusion* while the program runs *windows11* — `-platform windows` closes that
one. The Qt *version* no flag closes: `shots` links against whatever MSYS2 has,
`ci.yml` decides what a download gets per platform, and a control's appearance is
Qt's to change between versions. Every run prints its Qt, its platform and its
style on the first line; read that before anything else when a shot and a report
disagree. See [three explanations for a bug nobody here had](#three-explanations-for-a-bug-nobody-here-had),
which cost a session and three published conclusions that were all wrong.

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

**It is also good for questions that are not about pixels, which was not the
plan and is worth knowing.** A `Stage` is a real `MainWindow`, built, shown,
settled and then *destroyed* — so it is the cheapest way to put the program into
a named state and watch what happens on the way out. Chasing the teardown bug in
[the traps](#the-traps) needed exactly that: a situation four lines long left the
keyboard in the brush size box, and `shots the-keyboard-left-in-the-brush-size-box`
built and destroyed that window in about two seconds, against twenty-three for a
run of `test_canvas`. That measurement is what established the second instance of
the bug was harmless rather than fatal. The picture it wrote was never opened.
The honest limit: it asserts nothing, so it tells you what happens and a test
still has to be the thing that keeps it happening.

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

## Asking Qt a question directly

`tests/dock_probe.cpp` is a plain Qt main window with two docks, a central
widget and a status bar, and **none of this program in it**. It is the tool for
the question that comes up every time something goes wrong with a panel: *is
this ours or is it Qt's?*

```bash
./build/tests/dock_probe              a window to drag panels around by hand
./build/tests/dock_probe --bench      try the cures against a forged fault
./build/tests/dock_probe --native-frame  let the window manager decorate a
                                      floating panel, as stock Qt does
```

It answered [#54](https://github.com/S-poony/Animage/issues/54) in a single run
— plain Qt froze its layout exactly as Animage did — and that one fact turned
the work from "find our bug" into "find the Qt release that has it", which was
a different and much shorter job. Before it existed the same question had been
answered by *reasoning*, twice, and wrong both times; see the traps.

Like `shots` it is **not a test and must not become one**: nothing asserts,
`ctest` never runs it, and it needs a real display and in places a real hand.
Unlike `shots` it links Qt directly rather than `animage_ui`, and that is the
whole point — a reproducer containing our code proves nothing about Qt.

`tests/window_probe.cpp` is **the other half of the same question**, and the two
are meant to be run side by side. It opens the real `MainWindow` on screen and
prints every dock's size on every change, against the size it was at the previous
reading and against the hint and the minimum it could be falling back to. Do the
same drag in both and the answer is a subtraction:

```bash
./build/tests/window_probe            the real window, logging every dock change
```

A separate binary and **not a flag on `dock_probe`**, because one binary cannot
be both free of Animage and made of it, and a flag would have quietly destroyed
the property that makes the first one worth having. They share about forty lines
of printing, which is the price of that and a good price.

Two things it is built around, both of which cost a run to learn. Add a couple of
**tracks before starting**: with one track the timeline dock sits exactly on its
own size hint, so a Qt that re-fits a layout to its hints takes nothing and the
run reports a real fault as absent — the rows are what put height above the hint,
and height above the hint is what there is to lose. And it watches
`dockLocationChanged` as well as `topLevelChanged`, because **a dock changing
side never floats**: drag a panel from the right edge straight to the left and
the second signal is the only one there is.

**A floating panel wears a Qt title bar by default**, and that is not a
decoration choice: a native title bar is non-client area, Windows Ink sends no
non-client press for a pen, and so a stylus cannot drag a floating panel back
in *at all* — issue #50, and the reason `FloatingDockFrame` exists. A probe a
pen cannot re-dock cannot answer any question about re-docking, which is most
of them; that was found by a reporter who could not finish a run, because the
flag then reached only the side dock and left the bottom one unpressable.
`--native-frame` restores Qt's own default for the #50 question itself.

Three things it does that are worth knowing before writing another one:

- **It reads Qt's private state without needing a private symbol.** With
  `Qt6WidgetsPrivate` present it prints `QMainWindowLayout`'s drag bookkeeping —
  `savedState`, `currentGapPos`, `pluggingWidget`, `movingSeparator` — whenever
  any of it changes. `QMainWindowLayoutState::isValid()` is `rect.isValid()` and
  `rect` is a public member, so reading the field works where calling the method
  would not link. The private package is optional and asked for **in `tests/`**,
  never in the root `find_package`; without it the probe still builds and still
  shows the geometry, which is the symptom.
- **It turns on Qt's own logging** — `qt.widgets.dockwidgets` — and folds it into
  the same log as the measurements, in order. Qt saying *"will be unplugged with
  size"* next to our own sample of the flag is what made #54 legible.
- **It prints to a file**, because a Qt program built `WIN32` has no console.

**And the technique it exists to make cheap is forging.** A synthetic drag sent
to a `QDockWidget` leaves Qt's state machine half finished and lies — so the
drag has to be a hand. But once a hand has shown that the fault *is* one piece
of state, that state can be set directly and a dozen candidate fixes tried by
machine in a second each. That is what `--bench` does, and it is the reason #54
has a table of eight cures rather than an argument about one.

## What taking Qt Multimedia costs

**Measured, before a line of audio code.** [importing.md](importing.md) puts a
deployment spike before all of it and calls it the highest-risk item in the
note: if `windeployqt` does not bundle the FFmpeg backend, that is a disaster to
find out after the audio layer exists. It was run, and the full record with the
numbers is [audio-spike.md](audio-spike.md).

Four things from it are worth having here, because each one is a thing somebody
would otherwise assume.

- **All three deployment tools bundled the backend with no help at all.**
  `windeployqt`, `macdeployqt` and `linuxdeploy-plugin-qt` each read it out of
  `animage`'s import table along with its FFmpeg libraries. The expensive
  outcome did not arrive.
- **It costs about 20 MB on every platform and nothing at startup.**
  `Qt6Multimedia` imports no FFmpeg; the backend is a plugin loaded lazily on
  first use of the media stack, and startup timed through the full window build
  is 29 ms either way.
- **Scrub audio needs none of that 20 MB.** With `plugins/multimedia` deleted
  outright, Qt keeps `QMediaDevices`, `QAudioDevice`, `QAudioSink` and
  `QAudioSource` — the raw audio path is native inside `Qt6Multimedia` itself.
  The payload buys `QAudioDecoder` and `QMediaPlayer`: a compressed audio file,
  and video. So if the backend is ever a problem somewhere, the half that
  matters degrades to WAV rather than disappearing.
- **`QAudioSink::processedUSecs()` counts audio played out**, so `playedMs()` is
  that number as it comes. That was the first of the note's two open
  measurements; the second belongs to video and is untouched.

`tests/audio_probe.cpp` is the instrument, in the register of the section above
— built, never run by `ctest`, and there for the first machine whose driver
disagrees with those numbers. `src/app/animage/audio_check.*` was the other half
and is **gone**, as its own header said it would be: the deployment tools only
look at `animage`, so the spike had to be inside the application to ask them
anything, and once it had asked there was nothing left for it to do. What
replaced it is `audio_device.h`, which is a seam and not a report. What that
cost the packaging steps in CI is in
[audio-spike.md](audio-spike.md#what-comes-out-again).

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
| Qt | whatever MSYS2 has today — 6.11 at the time of writing | pinned per platform in `ci.yml`: Windows 6.10.3, Linux and macOS 6.8.\* ([#82](https://github.com/S-poony/Animage/issues/82)) |
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
- **Reading a fill has to be a pure function of what the fill stores.** The
  compositor's bands all read the same `CtgFill` at once, and a fill has no
  pixels — so `ctgFillPixel`, `ctgFillSpan` and `ctgFillExtent` work the answer
  out rather than looking it up. That rules out the obvious first design,
  materialising a tile on first touch and keeping it, which would want a lock on
  the path the whole thing exists to make faster.
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

The section is far too long to read through, and nobody does. Every trap is its
own heading, so this is the list to scan when you are about to touch something
and want to know what has already gone wrong there. Add a row when you add a
trap.

| | |
|---|---|
| [Which rectangle counts the columns, and which sizes the buffer](#which-rectangle-counts-the-columns-and-which-sizes-the-buffer) | Offset layer at reduced zoom indexes one past the framebuffer |
| [What a missing pen release takes down with it](#what-a-missing-pen-release-takes-down-with-it) | A lost release strands the depth counter; undo and autosave stop |
| [What is left of a window when its children are destroyed](#what-is-left-of-a-window-when-its-children-are-destroyed) | Members die before children, so late callbacks reach freed memory |
| [What lighter and darker carry through, and what they cannot lift](#what-lighter-and-darker-carry-through-and-what-they-cannot-lift) | Bending a palette role keeps its alpha and cannot lift black |
| [Why an application-wide event filter sees its own events twice](#why-an-application-wide-event-filter-sees-its-own-events-twice) | Forwarded and propagated keys re-enter the filter; held keys recurse |
| [How many wrong theories a bug is worth](#how-many-wrong-theories-a-bug-is-worth) | After the second wrong theory, stop and instrument |
| [The background seeds that failed, and the rim that cannot be bought](#the-background-seeds-that-failed-and-the-rim-that-cannot-be-bought) | Soft seed and border price both fail; a hard rim works |
| [Why regenerating the fill whenever it looks stale costs a max-flow per dab](#why-regenerating-the-fill-whenever-it-looks-stale-costs-a-max-flow-per-dab) | Solve the fill on pen-up, not on cache staleness |
| [Where the fill solve runs, and the resolution that paid for it](#where-the-fill-solve-runs-and-the-resolution-that-paid-for-it) | A synchronous solve capped quality; it is threaded now |
| [What a widget on a list row takes over, including the row's own tick](#what-a-widget-on-a-list-row-takes-over-including-the-rows-own-tick) | setItemWidget swallows presses and kills the visibility tick |
| [What a fill with no absent tile stops getting for free](#what-a-fill-with-no-absent-tile-stops-getting-for-free) | A lazy fill costs the viewport, not the fill, unless it is told where to look |
| [What a stroke's dirty rectangle misses when the whole fill is resolved](#what-a-strokes-dirty-rectangle-misses-when-the-whole-fill-is-resolved) | A regenerated fill changes far from the pen; dirty everything |
| [Which strokes count as drawing, and the one the solve guard missed](#which-strokes-count-as-drawing-and-the-one-the-solve-guard-missed) | Inking the line art must defer the fill solve too |
| [What point-sampling the barrier does to a two-pixel line](#what-point-sampling-the-barrier-does-to-a-two-pixel-line) | A coarse step perforates line art and the fill escapes |
| [What a band counted in coarse rows really costs](#what-a-band-counted-in-coarse-rows-really-costs) | Coarse rows times step is image rows; band the barrier in bytes |
| [What a threshold meant before the thing under it changed](#what-a-threshold-meant-before-the-thing-under-it-changed) | A constant calibrated against one reduction survives the reduction changing |
| [What made saving slow, and why skipping unchanged cels would not have helped](#what-made-saving-slow-and-why-skipping-unchanged-cels-would-not-have-helped) | Tiles were 92.6% zeros; store each row's occupied span |
| [Why an ever-true tablet flag leaves the mouse unable to draw](#why-an-ever-true-tablet-flag-leaves-the-mouse-unable-to-draw) | Pen-seen must be a time window, not a permanent flag |
| [What was never timed, and what the benchmark stopped anyone timing](#what-was-never-timed-and-what-the-benchmark-stopped-anyone-timing) | Compositing was tuned unmeasured; the benchmark hid the larger loop |
| [Why sharpness changed at 70.7%, and the cache cap nobody attributed it to](#why-sharpness-changed-at-707-and-the-cache-cap-nobody-attributed-it-to) | A memory cap, not zoom, chose the sharpness threshold |
| [Why a cache sized by the drawing asks for half a gigabyte at 5% zoom](#why-a-cache-sized-by-the-drawing-asks-for-half-a-gigabyte-at-5-zoom) | Size caches and margins in screen terms, never image terms |
| [Why the step boundary kept moving, and the screen-pixel cache that removed it](#why-the-step-boundary-kept-moving-and-the-screen-pixel-cache-that-removed-it) | An integer step must jump somewhere; screen-pixel entries remove it |
| [Why the max-flow keeps its trees, and how an orphan adopts its own descendant](#why-the-max-flow-keeps-its-trees-and-how-an-orphan-adopts-its-own-descendant) | Keep search trees; cut the stale parent link before searching |
| [Why an edit that matched nothing still built and passed](#why-an-edit-that-matched-nothing-still-built-and-passed) | A silent no-op edit; build green, the button simply absent |
| [Who promotes a pen's taps, and whether a double click arrives](#who-promotes-a-pens-taps-and-whether-a-double-click-arrives) | Pen double clicks depend on the promoter; count presses instead |
| [The native frame a floating dock gets, and the press a pen never sends](#the-native-frame-a-floating-dock-gets-and-the-press-a-pen-never-sends) | A floating dock's title bar is non-client; Ink sends no press |
| [Why a rebuilt title bar matched every metric and still looked wrong](#why-a-rebuilt-title-bar-matched-every-metric-and-still-looked-wrong) | Paint the title bar with the style, do not assemble it |
| [What size a title-bar button's glyph is really drawn at](#what-size-a-title-bar-buttons-glyph-is-really-drawn-at) | The glyph is fitted inside the button; every metric reports a bound |
| [The flag a drag that ends outside the window leaves set](#the-flag-a-drag-that-ends-outside-the-window-leaves-set) | A dock dragged out freezes the layout; Qt 6.11.1 only |
| [What asking for a private Qt component at the top level switches off](#what-asking-for-a-private-qt-component-at-the-top-level-switches-off) | A root find_package failure skips every GUI target, silently |
| [Why fitting the canvas on a maximise cannot trust either event alone](#why-fitting-the-canvas-on-a-maximise-cannot-trust-either-event-alone) | A maximise reframe needs both the state change and the resize |
| [The nine-character band where an export makes the folder and no frames](#the-nine-character-band-where-an-export-makes-the-folder-and-no-frames) | Names of 247-255 characters export partially, destroying the last run |
| [What `findChild` hands back after the view has closed its editor](#what-findchild-hands-back-after-the-view-has-closed-its-editor) | A pending deferred delete leaves a dead editor findable |
| [Where the first dock-width reading was taken, and why the fix shipped twice](#where-the-first-dock-width-reading-was-taken-and-why-the-fix-shipped-twice) | Both measurements landed on the same side of the growth |
| [What emptying the fill cache does not reach while a solve is in flight](#what-emptying-the-fill-cache-does-not-reach-while-a-solve-is-in-flight) | Async solves outlive invalidation without a generation count |
| [What went stale when the solve stopped finishing in the same call stack](#what-went-stale-when-the-solve-stopped-finishing-in-the-same-call-stack) | Refreshes that piggybacked on a synchronous re-solve now lag |
| [What a view that skips the solve also skips](#what-a-view-that-skips-the-solve-also-skips) | No solve means no warp, and no warp reads as "the marks did not move" |
| [What the lattice fallback was being compared against](#what-the-lattice-fallback-was-being-compared-against) | Neither measure decides it; whether rest saw anything decides which measure |
| [The tests that construct the bug, and go red when it is fixed](#the-tests-that-construct-the-bug-and-go-red-when-it-is-fixed) | A whole fixture reddening can mean the feature works |
| [Why a cache key of cel revisions serves wrong fills, not slow ones](#why-a-cache-key-of-cel-revisions-serves-wrong-fills-not-slow-ones) | Revisions collide freely, so a shared scribble key swaps answers |
| [Why an erased scribble left every later solve on that drawing coarser](#why-an-erased-scribble-left-every-later-solve-on-that-drawing-coarser) | Emptied tiles kept the bounds that pick solve resolution |
| [Why the proposed confidence score reads 1 on every case](#why-the-proposed-confidence-score-reads-1-on-every-case) | Confidence is constant; spread separates but carries no verdict |
| [Why the carried-mark flag was removed rather than tuned](#why-the-carried-mark-flag-was-removed-rather-than-tuned) | Spread is an amount, not a correspondence; no threshold works |
| [What a stylesheet with no type selector also paints](#what-a-stylesheet-with-no-type-selector-also-paints) | Swatch styling leaks into its own tooltip, invisible to grab() |
| [How long a `QTreeWidgetItem` pointer stays valid once a rebuild is queued](#how-long-a-qtreewidgetitem-pointer-stays-valid-once-a-rebuild-is-queued) | A queued list rebuild deletes rows other code still holds |
| [What the screenshot showed, and what the run had never set up](#what-the-screenshot-showed-and-what-the-run-had-never-set-up) | Screenshot review needs the harness itself verified first |
| [The gap coordinates that built an open box out of plausible numbers](#the-gap-coordinates-that-built-an-open-box-out-of-plausible-numbers) | drawGappedBox takes coordinates, not offsets |
| [Four tool states in three booleans, and the pair a handler half-cleared](#four-tool-states-in-three-booleans-and-the-pair-a-handler-half-cleared) | Mutually exclusive state in separate flags drifts apart |
| [What the tests for a failed swap do and do not prove](#what-the-tests-for-a-failed-swap-do-and-do-not-prove) | The recovery is tested; the rename that triggers it never was |
| [Every route that changes the input to a differencing function](#every-route-that-changes-the-input-to-a-differencing-function) | `syncTimelineHeight` takes a difference, so a load has to announce itself |
| [A tablet gesture is not one device's](#a-tablet-gesture-is-not-one-devices) | The barrel button presses as a mouse and the drag arrives as tablet moves |
| [Three explanations for a bug nobody here had](#three-explanations-for-a-bug-nobody-here-had) | #75 was Qt 6.8's, and every instrument was running 6.11 |
| [What a save deletes that the document cannot write again](#what-a-save-deletes-that-the-document-cannot-write-again) | The swap replaces every entry; an imported file has no second copy |
| [A `shots` situation that presses an id nothing bound](#a-shots-situation-that-presses-an-id-nothing-bound) | `press` on an id with no QAction does nothing, silently |
| [Which Qt classes answer "what are you" and which only answer "are you this"](#which-qt-classes-answer-what-are-you-and-which-only-answer-are-you-this) | `QColorSpace` compares against a named space and never names one |


### Which rectangle counts the columns, and which sizes the buffer
**Two rectangles of the same width can span a different number of entries, and
sizing a buffer from one while indexing it from the other is a heap overflow.**
This one shipped too, and it is a *write*, so on a build with a sanitizer it
aborts and on one without it corrupts whatever the allocator put next.

`compositeGrids` sizes its framebuffer and its accumulator from the region being
drawn, then — for a layer drawn away from where its pixels are stored, which is a
colour layer showing carried marks — built the column plan from the region being
*read*, which is the same rectangle moved by the shift. The sample grid is
anchored at the image origin, so those two have different phases, and
`SampleStep::entryAt` floors: `entryAt(p) - entryAt(region.x - shift)` and
`entryAt(p + shift) - entryAt(region.x)` are not the same number. The first was
being used to index buffers sized by the second, and at some phases it is one
larger — one past the end, on every row of every repaint, at any zoom below 100%.

**The fix is to compute the columns in the space the buffers are counted in**,
which bounds the index by construction rather than by a check somebody has to
remember. Worth noting how it hid: the *spill* write a few lines below was
already guarded against exactly this, `column + 1 >= columns`, so the code looked
like it had been thought about. The unguarded write was the primary one.

It hid from the tests for a plainer reason. Both halves were covered — the offset
path has tests, and so does the reduction — but nothing put them together, so no
test ever ran a reducing `SampleStep` against a non-zero `LayerPass::offset`. The
new one sweeps ratios and offsets and asserts the one thing that cannot be wrong:
a pixel inside the region has to appear somewhere in the picture. It is confined
to the ratios where the box reads every image pixel, because above those the
lattice legitimately drops a one-pixel mark — with no offset at all, so that is
the reduction working rather than this bug.


### What a missing pen release takes down with it
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


### What is left of a window when its children are destroyed
**A window is destroyed from the top down, and everything it owns is destroyed
after it — so a child reporting to its parent during teardown is reporting to
something that has stopped existing.** This one shipped, and it turned the
`sanitizers`, `windows` and `macos` jobs red from run #61 onwards. Issue #51.

The order is the whole bug. `~MainWindow`'s body runs; then every member of
`MainWindow` is destroyed — `doc_`, `keyed_actions_`, `typing_editors_` among
them; and only then does `~QWidget` destroy the children, the layer panel and the
timeline included. **Destroying a rename editor is what finishes a rename**, so a
name still being typed into when the window went away committed itself from down
there. `LayerList::renamed` called `MainWindow::renameLayer`, which renames a
layer in a `Document` that had already been destroyed; `LayerList::renaming` and
`TimelineWidget::renamingChanged` — both since removed, see
[what the keyboard does](#what-the-keyboard-does-and-when) — called
`MainWindow::setTyping`, which walks a `keyed_actions_` whose buckets had already
been freed. Linux's UBSan named it —
*member call on address which does not point to an object of type `MainWindow`* —
and Windows and macOS called it a segfault. Both routes were instrumented and
both fire.

**It is also the best candidate yet for [#49](https://github.com/S-poony/Animage/issues/49)**,
the single unreproduced heap corruption. `renameLayer` does not merely read the
destroyed document: freed memory still holds the old bytes, so `findTrack`
answers with a plausible track and the call goes all the way through to
`Document::updateLayer` — a *write* into a destroyed `Document`, on a path
`test_canvas` runs every time. Instrumented on the broken destructor it reached
the write on five runs out of five, and one of those five died at `0xC0000005`
on a build with no sanitizer at all. That is the shape #49 describes: no output,
no assertion, a dead process at whatever unrelated allocation came next. Not
proof — #49's own call site was never captured, and its rate was under one in
twenty-five where this is far higher — but a freed-heap write on a path that
binary takes every run is a better candidate than anything proposed there.

**A rename editor was the first child found doing this, and it was not the only
one.** A `QAbstractSpinBox` reports through `editingFinished`, which fires on
losing focus — and a window closed with the keyboard in the brush size box runs
`[this] { canvas_->setFocus(); }` from inside `~QWidget`, reading a `canvas_`
that has already been destroyed. Five connections have that shape: `radius_`,
`onion_`, and three in the transform bar. Measured, it fires. It was *harmless* —
the canvas happens to still be alive at that moment, and `setFocus` on a live
widget does no damage — but harmless by luck, in a lambda one edit away from
touching the document.

**Why nothing said so, which is the part to remember.** `connect(x, sig, this,
&MainWindow::slot)` makes Qt do a `static_cast<MainWindow*>(receiver)`, and that
downcast is what UBSan reported at `qobjectdefs_impl.h:570`. `connect(x, sig,
this, [this] { ... })` stores a functor and never downcasts, so nothing is
flagged and only the *body* can be caught, and only if it does something ASan can
see. In `main_window.cpp` that is 28 connections the sanitizer can see and 10 it
cannot. **A green `sanitizers` job is not evidence of absence for this shape.**

**The fix is to shut the window down in `~MainWindow`'s body**, which is the last
moment the object is still a `MainWindow` and everything it owns is still there.
Three steps, each one the reason the next is safe:

1. `LayerList::abandonRename` and `TimelineWidget::abandonRename` **give an open
   rename up**. First, because the other two steps close the editor and an editor
   closed any other way *commits* — so without this, closing a window would
   silently apply a half-typed name.
2. `clearFocus()` on whatever holds the keyboard, so **everything that reports on
   losing focus reports now**, to a window that is entirely intact. A field that
   has lost focus does not lose it twice.
3. **Then the children are destroyed**, in reverse order of construction — the
   same rule C++ uses for members, and for the same reason: the canvas is built
   first and is what the later widgets refer back to, so it goes last. After
   this there is nothing left for `~QWidget` to destroy, so the moment in which a
   child is alive and this window's members are not **no longer exists**.

That third step is what makes the first two belt-and-braces rather than
load-bearing, and it is why this is written as a rule about teardown and not as
two bug fixes. Instrumented across a run of `test_canvas`: three calls arrive in
step 1, two in step 2, and **nothing at all in step 3 or after the body**. The
member pointers are set to null after the sweep, so the `if (canvas_)` questions
this class already asks stay truthful for the rest of the destruction.

**Cutting the wires instead does not work, and it is worse than doing nothing.**
This is the obvious fix, it was tried first, and it is worth writing down because
it fails in a way nothing points at. Clearing `layer_list_->renamed` in the
destructor does stop that one call — but an empty callback is exactly the case
where the delegate's `setModelData` falls through to Qt's own, and Qt's writes the
typed name into the model, and the model emits `itemChanged`, which is connected
to `MainWindow::onLayerItemChanged` — the same destroyed document, reached by a
third route, and *that* one writes to it. Instrumented over one run of
`test_canvas`, clearing the two callbacks turns three late calls into nine, six of
them through `onLayerItemChanged`. Measured on the maintainer's build it was six
runs out of six dead of `0xC0000374`.

`aWindowIsDestroyedSafelyFromAnyState` in `test_canvas.cpp` is what keeps this
from coming back: one window per interruption — the keyboard in a toolbar field,
the keyboard in the transform bar with a transform live, a stroke the pen has not
been lifted from, a colour solve still on its worker — each destroyed on the
spot. **It asserts almost nothing itself**, and says so: the checks are that each
state was really reached, so a case that has quietly stopped setting anything up
cannot pass by doing nothing, and what happens after each window dies is the
sanitizers' to judge. Adding a case is three lines, which is the only reason
anybody will add one.

Two smaller things fell out of chasing it, both worth knowing:

- **`layer_list_` is not dangling in `~MainWindow`.** That was the standing
  suspicion, because the destructor appeared to corrupt the heap by writing to
  it. It does not: instrumented, `layer_list_` equals `findChild<QTreeWidget*>()`
  and has a live parent in every window the tests destroy.
- **A closed editor is only `deleteLater`d, so `findChild<QLineEdit*>` goes on
  answering with it** until the event loop next runs. A second rename opened
  before the first editor had been collected therefore closed the *old* one and
  left the live one open — which is how one of the two tests came to destroy a
  window mid-rename in the first place. `LayerList` holds the live editor now,
  told to it by the delegate's `createEditor`.


### What lighter and darker carry through, and what they cannot lift
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
worth reading twice. Qt 6.8 on Windows 11 — which is what the Windows build shipped
against at the time, and what Linux and macOS still ship against — hands `Window`
over as `#00000000`: transparent, and *black underneath*. Made opaque that is pure black,
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
Qt's business and moves between Qt versions. Local was Qt 6.11 from MSYS2 and CI
built against 6.8, which is the entire difference between the two pictures.
Windows is pinned to 6.10 now, which narrows that particular pair without closing
it; Linux and macOS are still built against 6.8
([#82](https://github.com/S-poony/Animage/issues/82)), and the palette a download
reads is still not a thing this repository can pin.

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


### Why an application-wide event filter sees its own events twice
**Two crashes, one cause, and I caused it.** Space and Z are held modifiers, not
shortcuts, so they are forwarded to the canvas by an application-wide event
filter. An application filter also sees the events it sends itself — and sees
them again when an unaccepted key propagates to a parent widget. Both routes
lead straight back in. It needs a re-entrancy flag, and the canvas must *accept*
those keys including auto-repeats, or holding one past the repeat delay
recurses until the stack runs out. See `MainWindow::eventFilter`.


### How many wrong theories a bug is worth
**Guessing cost more than instrumenting, three times now.** The first crash took
two wrong theories before a test that sent real key events found it in one run.
The second took four, and was answered in one round trip once
`crash_report.cpp` existed. The lesson is written into the commits because it
will happen again: after the second wrong theory, stop and instrument.

**And then it happened again, with the rule already written down here.**
[#50](https://github.com/S-poony/Animage/issues/50) — a pen cannot drag a
floating panel back — cost four wrong theories and two whole implementations
that were written, tested, reported broken and deleted. A `QDockWidget` title bar
was replaced with one driven by hand, on a premise that was never checked; then
an event filter was written to give the pen events it was already receiving. Both
were reverted. What answered it was an afternoon's logging in the real
application: which widget receives each event, where the pointer is, and what the
window manager thinks it owns.

Three things made the guessing feel safe, and each is worth recognising:

- **A test that is genuinely falsified can still prove nothing.** The event
  filter had a test that failed when the fix was disabled — and the fix was inert
  on real hardware, because offscreen *Qt* promotes tablet events while on
  Windows the *platform* does. See the double-click trap above for
  `platformSynthesizesMouse`, which is the switch. **An offscreen test of pen
  input answers a question about Qt, never a question about a pen.**
- **A synthetic mouse drag is not a drag.** Sent to a `QDockWidget` it leaves
  Qt's own drag state machine half finished, and the wreckage looks exactly like
  a layout bug. Two conclusions were drawn from one, and both were wrong; the
  real layout fault is narrower and is
  [#54](https://github.com/S-poony/Animage/issues/54) — which turned out to be
  Qt's rather than ours, and is worked around rather than fixed. Its own entry
  below has the mechanism.
- **A screenshot shows a thing exists, not that it works.** The replaced title
  bar was checked by looking at a picture of it. The close button in that picture
  could not be clicked.

The reporter's own observation is what finally cracked it — *it works if I never
lift the pen* — which turned a vague "cannot put it back" into a precise "cannot
start a gesture on a floating panel". **Ask what the neighbouring gesture does.**
Half the wrong theories would have died at the first question.


### The background seeds that failed, and the rim that cannot be bought
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


### Why regenerating the fill whenever it looks stale costs a max-flow per dab
**Solving on a stale cache is not the same as solving when asked.** Every dab
bumps the cel's revision, so regenerating a CTG fill whenever it looked stale
meant a max-flow per dab. The cost was invisible for a while because a separate
bug meant the result was never drawn; fixing the repaint made it obvious. The
solve now happens once, when the pen lifts, and the scribble itself is shown
during the stroke.


### Where the fill solve runs, and the resolution that paid for it
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


### What a widget on a list row takes over, including the row's own tick
**A widget on a list row disables that row's own tick.** The show-scribbles box
was a `QCheckBox` in a widget set on the layer's row with `setItemWidget`. Qt
treats an index widget as a persistent editor, so it routes the press into the
widget instead of to the delegate — and the row's *visibility* tick silently
stopped working, meaning no colour layer could be hidden at all. The panel is
two real check columns now. If you are tempted to put a control on a row, don't.


### The scrollbar a size hint does not admit to, and where it takes its height
**`QScrollArea::sizeHint` adds the horizontal scrollbar's height only when the
policy is `ScrollBarAlwaysOn`.** With `ScrollBarAsNeeded` — which is what you
want, and what the timeline has — the hint describes a scroll area that will
never grow a slider, and anything sized from that hint has no room for one. When
the slider then appears it takes its height out of the *viewport*, which is
[#26](https://github.com/S-poony/Animage/issues/26): a shot wider than the window
put the pan slider across the bottom of the track row. Measured on a 1400 px
window, one track: viewport 68 px for a 64 px strip with no slider, 54 px for the
same strip with one — so ten pixels of the row outside the viewport, plus a
*vertical* scrollbar to scroll a single track up and down, which is the same
shortfall arriving as its own second symptom.

`ReservingScrollArea` in `main_window.cpp` adds it back, and asks the scrollbar
for its height rather than writing 14 down — that number is the style's and the
screen's.

**Reserved always, and the alternative is the interesting part.** Asking for the
height only when the slider appears would be exact in both states, and it would
also move the dock while somebody is working: a timeline deliberately dragged
down to one row would spring taller the moment the shot outgrew the window. That
is the complaint `timeline_rows_shown_` exists to prevent, arriving through a new
door. So the height is spent once, where the dock opens, and nothing moves it
afterwards. The cost is about ten pixels of canvas on a shot short enough that no
slider ever appears.

**And the test is single-sided on purpose.** It makes the shot long, checks the
slider is showing, and asserts the viewport still holds the strip. Not a
before-and-after: "the dock before the shot got long" is a reading on the far
side of the thing that goes wrong, and see
[where the first dock-width reading was taken](#where-the-first-dock-width-reading-was-taken-and-why-the-fix-shipped-twice)
for what that costs.


### What a fill with no absent tile stops getting for free
**The compositor's oldest shortcut is a property of tiles, not of layers.** An
area a layer left empty has no tile there, and the whole run is skipped on one
pointer test before a channel is read — which is why a 66-tile drawing and a
2425-tile one refresh in the same time. A fill that works its colour out per
pixel has no absent tile to skip on, and the first version of one duly cost the
area of the *viewport* where the tiles cost the area of the fill: the coloured
frame went from 14.6 ms to 37.6 ms at HD, and four tracks from 96 frames shown
to 89.

The fix is not a faster loop, it is giving the shortcut back. `ctgFillExtent`
answers which samples of a row can be anything but transparent — three
rectangles, no per-sample work — and the compositor asks it before it asks for
anything else. Two facts make it exact rather than a guess: `outside_is_clear`,
which is a fact about the labels that were computed and not an estimate of them,
and the marks' own drawn bounds, cached on the fill because working them out is
a scan of every mark pixel and it is wanted per row.

**And a run-length is worth nothing at the resolution that matters.** The other
half of the same regression: a run of pixels sharing one solved cell is `step`
long, and a 1080p drawing solves at `step` 1, so the run is one pixel and what
is left is a clamp, two divisions and a colour decoded from a key, per pixel.
The row is cut into its two clamped ends and the interior between them, so the
clamps are paid twice a row rather than twice a pixel, and the palette is
decoded once at solve time. Both were found by running `bench_playback`, which
is the whole reason the plan measures before it changes anything.


### What a stroke's dirty rectangle misses when the whole fill is resolved
**Solving globally and repainting locally is a bug that looks like a feature.**
A regenerated CTG fill changes colour across whole regions, nowhere near the
pen. Marking only the stroke's own rectangle dirty left the new fill beside the
stroke and the old one everywhere else, while hiding and showing the layer
repainted the lot — so the same operation appeared to have two behaviours. Any
path that regenerates a fill has to mark everything dirty.


### What refreshAll does not refresh, and the buffer nobody was invalidating
**The onion buffer is derived state and nothing was deriving it again.** The
ghosts are composited through the same layer flags as the drawing in front of
them and out of the same cels, so switching a layer off, or undoing a stroke
made on a neighbouring drawing, changes what they should look like. `refreshAll`
marks the display cache dirty and the onion buffer is not the display cache, so
the ghosts went on showing a layer that was no longer there until a frame change
happened to rebuild them. Reported as "the onion doesn't update when you toggle
a layer".

It had been that way from the start and was hidden by an accident: every cache
rebuild set `onion_dirty_`, so panning past the margin rebuilt the ghosts and
cleared the staleness. Scrolling the cache took that away, and a latent fault
became a visible one — which is the useful half of the story, because the
accident was never the mechanism and the bug was never in the pan.

Fixed by comparison rather than by signalling: `OnionState` holds the ghost
list, the track's layer list and the cel revisions behind each ghost, and the
buffer is rebuilt when it stops matching. There are twenty-six calls to
`refreshAll`, and a rule that every future one has to remember to invalidate the
onion is a rule that will be forgotten. The layer list is held whole and
compared with a defaulted `operator==` for the same reason — a field added to
`Layer` joins the comparison without anybody doing anything. Asked only when the
whole cache is being composited again, which every path that can change the
ghosts does, so it stays off the per-dab path entirely.


### Which strokes count as drawing, and the one the solve guard missed
**The per-dab solve came back through the other door.** The guard was "is a
scribble being drawn", which covers drawing on the colour layer and misses
drawing on the line art it is cut against — so inking over a filled drawing ran
a max-flow per dab. The condition is any stroke at all, and the solve belongs at
the end of it.


### What point-sampling the barrier does to a two-pixel line
**Never point-sample the barrier.** `ctgBarrier` used the compositor's `step`
argument, which samples every nth pixel. Line art is thin: at a coarse step a
two-pixel line becomes a dotted line, the barrier acquires holes that are not in
the drawing, and the fill pours out through its own outline. Composite at full
resolution and reduce by taking the *most* covered pixel in each block — too
solid costs a little gap tolerance, too thin costs the whole fill.


### What a band counted in coarse rows really costs
**A band counted in the coarse unit is `step` times bigger than it reads as.**
`ctgBarrier` composites at full resolution and reduces, so it works a band at a
time to keep the framebuffer small — and the band was counted in *coarse* rows.
Thirty-two coarse rows is thirty-two times `step` image rows, so the buffer went
on growing with the drawing much as it would have with no banding at all, and
the comment above it claimed the opposite.

The worst caller is `estimateCtgShift`, whose grid is a fixed ninety-six cells
across however large the drawing is, so its `step` grows without limit. On a
16384-wide one that is a 1.4 GB framebuffer, asked for twice per solve. On a
worker thread with nothing to catch it, the `bad_alloc` left the thread function
and `std::terminate` took the program — no dialog, no crash report, nothing
saved, and up to two minutes of drawing with it.

**Two things were wrong and only one of them was the arithmetic.** Bounding the
band in image rows rather than coarse rows is the obvious repair and it is not
enough on its own: the buffer is rows times *width*, and the width is the whole
drawing, so pinning the height at a thousand rows still leaves hundreds of
megabytes — and makes every ordinary case thirty-two times worse, because at
`step` 1 the band was thirty-two rows and would become a thousand. Bytes is the
quantity that was meant all along, and writing it in bytes is what stops the
constant doing two jobs.

**And a band does not have to line up with a coarse row.** That is what lets the
floor be one image row rather than one coarse row — `step` times smaller, and
the difference between a bound and a smaller unbounded thing. The reduction
accumulates with `min` into an array that starts at 1.0, so a coarse row
finished by two bands is the same answer as one finished by one; that was always
true and nothing had leaned on it. `test_ctg` reads the same ink over a narrow
region and a wide one and requires them to agree exactly. That is the only part
of this a test can reach: how tall a band was is not observable from outside, so
raising the budget far enough would leave the test passing and testing nothing.

**The catch is a backstop and it is not the fix.** `CtgSolver::run` now treats a
`bad_alloc` out of a solve as that solve failing. Everything a solve touches is
its own — the job is a copy, the fill is derived, no part of the document is
half written — so giving up on one costs a recompute and the session lives. It
is narrow on purpose: anything else escaping a solve is a bug in the solve and
should still be loud. What it does not do is make a huge allocation safe. On a
machine with room to swap it succeeds and the answer simply takes minutes, which
no `catch` can see. The caller is then holding a question with no answer coming;
it re-asks the next time the drawing changes, and until then shows the fill it
already had. `CtgSolver::failedCount` is the only trace it leaves.

**What is left is the time.** The band is 4 MB now and measures it: peak working
set over a 16384-wide barrier at step 171 is 4 MB above where it started, where
the formula for the old one says 1.4 GB. What that did not buy is speed. The
barrier composited every pixel of the region at full resolution whatever the
step, so an empty region cost the same as a drawn one — about 0.4 s a call at
that size, three calls a solve. That was recorded here for a while as "not this
bug and not fixed", and both halves of it are fixed now, in phase 2 of
[colour without a canvas](colour-without-a-canvas.md). They are separate
findings and each is worth having on its own.

**The barrier composites only where some source has a tile.** Exact rather than
an approximation, which is what makes it small: bare paper composites to fully
transparent, which is a coverage of zero — the identity for the max the barrier
reduces with and for the sum the correlation reduces with alike. Skipped in both
directions and not only by band, because a band test alone buys everything on
two patches stacked one above the other, nothing at all on two side by side, and
nothing on a long diagonal. Measured: over four times as much paper as the
drawing needs, compositing everywhere costs 3.5x and compositing where the ink is
costs 1.3x.

**And the correlation stopped borrowing the barrier's reducer.** The shift
estimate built both level zeroes with `ctgBarrier` and inherited a reduction that
takes the *most* covered pixel in a cell, while every level above it was built by
averaging — and `halve` said why in its own comment, naming the barrier's rule as
the opposite one. A barrier must not lose a thin line, because a hole in it is a
fill pouring out. A correlation wants the ink to weigh what there is of it, so
that half a line under a cell counts half; taking the most makes any cell holding
any ink read as solid, which at the step the search uses is most of the drawing.
The reduction is an argument now (`InkReduce`), and coverage rather than
intensity is the quantity both share.

That changed what the search finds, which was the point of it rather than a risk
run by it. On `bench_carry` the answer moved on eight rows, always by exactly one
cell of the search's own grid, six times towards the true shift and once away.
The row it was for is a shape carried 400 px: matched at 252 before and losing
both its regions outright, matched at 396 now with the left one fully coloured.
Coverage, leak and spread are otherwise unchanged. The numbers are in
[the colour benchmarks](colour-baseline.md).


### What a threshold meant before the thing under it changed
**A constant is calibrated against the code it reads, and changing that code
silently recalibrates it.** `estimateCtgShift` guards itself with "nothing to
match": sum the level-zero ink and give up below one. Level zero used to be
built by the barrier's reduction, which takes the *most* covered pixel in a
cell -- so a cell holding any ink read about 1, the sum was the number of inked
cells, and a threshold of one meant "no cell has ink in it". Level zero is
averaged now, which is right and is what every level above it always did. The
same cell then reads `ink / step^2`, the same sum is the ink divided by a cell's
area, and the same constant quietly became **"fewer than step² pixels of ink"**
-- eleven thousand of them at a step of 107.

Nothing noticed, because `step` is the region's longer side over ninety-six and
the region was clipped to the canvas: 1920 wide caps it at twenty, where the
old meaning and the new one differ by a factor nobody could see. Unclipping the
region for [colour without a canvas](colour-without-a-canvas.md) removed the
cap, and a whole drawing's worth of line art in a ten-thousand-pixel-wide region
then counted as nothing: no shift estimated, marks silently stopped following
the line art, and there was nothing on screen to say so.

**The fix is a unit, not a number.** Multiply back by the cell's area and the
threshold is in image pixels of ink, which is the same quantity at every step.
The rule generalises: a threshold on a reduced quantity has to name the unit it
is counted in, or the next change to the reduction moves it.

**The same function had the same shape of bug twice**, and the second one is not
about a reduction at all. The step was taken from the region's *longer* side
alone, so a region much wider than it is tall left the short axis with fewer
than the four cells the guard demands, and the estimate was abandoned outright
-- at twenty-four to one, which the canvas clip had made unreachable and the
drawn bounds of a sheet make ordinary. The step is now the coarsest of what the
long side asks for and what leaves the short side a grid, with a floor so the
long axis cannot grow without bound in exchange: paying for the short axis in
step is paying for the long one in cells, and the top-level search is offsets
times cells, which is about the fourth power of the grid. A sliver is the one
shape where both cannot be had, and giving up there is the honest answer rather
than the accidental one.

**And two wrong attributions before the right one**, which is worth as much as
the bug. The first reproduction used a small shift, which the search quantises
to zero at a large step whatever the reduction does -- so the guard looked
guilty and was not being reached. The second used two shapes far apart, where
the answer really does change with the reduction, but because the averaged
correlation weighs a small dense shape against a large sparse one differently --
not because of the guard at all. Only a case with one shape, a shift large
enough to survive quantisation, and a region widened by nothing but its own
argument isolates it. See
[how many wrong theories a bug is worth](#how-many-wrong-theories-a-bug-is-worth):
each theory was tested and each was wrong in a way that looked like the answer.


### What made saving slow, and why skipping unchanged cels would not have helped
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


### Why an ever-true tablet flag leaves the mouse unable to draw
**A counter is not a state.** Mouse events promoted from the pen were recognised
by "has this canvas ever seen a tablet event", which is true forever after the
first time the pen came near — so touching the tablet once left the mouse unable
to draw for the rest of the session, silently. It is a time window now.


### What was never timed, and what the benchmark stopped anyone timing
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


### Why sharpness changed at 70.7%, and the cache cap nobody attributed it to
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


### Why a cache sized by the drawing asks for half a gigabyte at 5% zoom
**Caches must be sized by the window, not by the drawing.** The composite cache
was sized from the visible *image* area, so at 5% zoom it asked for half a
gigabyte, and its margin was measured in image pixels, so it grew as you zoomed
in. Both are fixed; the shape of the mistake is worth remembering.


### Why the step boundary kept moving, and the screen-pixel cache that removed it
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


### Why the max-flow keeps its trees, and how an orphan adopts its own descendant
**The max-flow needs its trees kept.** Rebuilding the search trees per
augmenting path is Edmonds-Karp in disguise: correct, and 214 seconds on a
megapixel instead of 1.3. And repairing trees has one trap — an orphan that
still points at its old parent can adopt its own descendant, since the
candidate's chain runs back through it and still appears rooted. Cut the link
before searching. See `gridflow.cpp`.


### Why an edit that matched nothing still built and passed
**A green build proves nothing about the interface.** An edit that was meant to
add the "Add colour layer" button matched nothing, silently. The build passed,
all tests passed, and the button was simply absent. Screenshots caught it, and
caught a mis-encoded character and a fresh document with three undoable setup
steps. Look at the thing.


### Who promotes a pen's taps, and whether a double click arrives
**Whether a pen produces a double click depends on who promotes, and it is not
Qt's decision.** This entry said flatly that a pen produces no double click. That
is wrong, and the correction is worth more than the original claim was.

Qt turns a mouse's second press into `QEvent::MouseButtonDblClick` and never
delivers it as a press, so a widget that wants a double click watches for that
event and is done — with a mouse. A tablet event nobody accepts is promoted to
mouse events, but *which code does the promoting* is chosen by
`QWindowSystemInterfacePrivate::TabletEvent::platformSynthesizesMouse`. When it
is true — and the Windows plugin sets it true — Qt keeps out entirely and
whatever the platform sends is what arrives. When false, Qt promotes itself. The
two differ:

- **Qt's own promotion does produce a double click**, and it replaces the second
  press rather than following it: press, release, `MouseButtonDblClick`, release.
  It pairs taps using `touchDoubleTapDistance` (10 px) and not
  `mouseDoubleClickDistance` (5 px), sharing the one `mouseDoubleClickInterval`
  (400 ms). Measured — see below.
- **Windows Ink's promotion did not.** Reported with a real pen: two taps arrived
  as two ordinary presses, `QAbstractItemView`'s `DoubleClicked` trigger never
  fired, and double-clicking a name to rename it did nothing at all while the
  same click with a mouse worked on the same machine.

**How that was measured, because it settles a claim this file also got wrong.**
The promotion boundary was written up as untestable, on the grounds that it
happens in the platform layer below anything a test drives. What is untestable is
the way the tests are written: they construct a `QTabletEvent` and send it
straight to a widget, which skips promotion entirely.
`QWindowSystemInterface::handleTabletEvent` injects *at* the boundary instead, so
Qt's hit-testing, promotion and double-click detection all run above it. That is
what produced the sequence above. What it still cannot reach is Windows Ink —
injecting at Qt's boundary means Windows never sees a pen and so cannot promote,
which is why the Windows half of this rests on a hand report and not a test.

`DoubleTap` counts presses instead. Anything else in this program that wants a
double click needs the same. The two routes cannot both fire for one gesture, but
not for the reason this entry used to give: it is not that a pen never produces a
double click, it is that a pen never produces a double click *and* a second
press. Where Qt promotes, `DoubleTap` simply never sees a second press and the
`DoubleClicked` trigger has already done the job.

One edge follows from that and is not handled: on the promoting-by-Qt path the
first press arms `DoubleTap` and the second never arrives, so it stays armed, and
a third tap inside the interval and the distance would read as the second of a
gesture that already finished. It needs three taps in 400 ms within 10 px and it
costs a spurious rename.

Two consequences worth carrying: a `QMouseEvent` built by hand
carries **timestamp 0**, so every synthetic press in a test is the second of a
double tap at the same place unless the test says otherwise (`sendTap`); and a
tap counted this way should be matched to a *smaller* target than a mouse's
double click, since the second press is an ordinary press and everything else on
the row is still listening — the layer panel takes it on the name only, because
the visibility tick is in the same column and flicking a layer off and on is the
commonest gesture in the panel.


### The native frame a floating dock gets, and the press a pen never sends
**A floating panel's title bar belongs to the window manager, and a pen cannot
press it.** This is [#50](https://github.com/S-poony/Animage/issues/50), and it
is the *other* half of the pen story: not how an event is promoted, but whether
Qt is given one at all.

A floating `QDockWidget` on Windows is handed a **native window frame** —
`windowFlags` comes back `0xa00340b`, with no `Qt::FramelessWindowHint` in it.
Its title bar is therefore non-client area, and Qt only ever hears about it
through `QEvent::NonClientAreaMouseButtonPress` and its siblings. That is the
path a *mouse* re-docks a panel through, and it works. **Windows Ink generates no
non-client press for a pen** — measured with a real stylus: hovering that title
bar produces a stream of `NonClientAreaMouseMove` and a press never arrives, so
`QDockWidget`'s drag is never entered.

Everything else about the pen and the docks is fine, which is what made this hard
to see. Dragging a *docked* panel works, because its title bar is Qt's own and
inside the main window. Dragging one out and back in a single unbroken gesture
works, because the drag began in client area. Only a fresh press on an
already-floating panel fails.

**The only lever is `setTitleBarWidget`.** Setting `Qt::FramelessWindowHint`
directly is discarded — measured, `windowFlags()` comes back unchanged — because
Qt decides native decoration for itself and stands down only when a title bar
widget is supplied. `FloatingDockFrame` supplies one while a panel floats and
takes it away when it docks, so the ordinary window is untouched.

Two things about that widget matter more than what it looks like. It handles
**no** mouse or tablet events, so Qt still runs the drag, the drop preview and
the dock areas — the previous attempt overrode them and lost all three. And it is
applied only once **nothing is held down**, because `topLevelChanged(true)`
arrives mid-drag and changing the decoration recreates the window, which would
break the one gesture that already worked.


### What a floating panel's decoration is paid for with, if nobody says otherwise
**Putting a title bar on a floating panel takes the height out of what is inside
it.** This is [#57](https://github.com/S-poony/Animage/issues/57), and it is the
bill for #50 arriving somewhere nobody was looking.

Until `FloatingDockFrame` acts, a panel that has just been torn off wears a
*native* frame — so Qt hides its own title bar and the whole of the window's
height is contents. `setTitleBarWidget` then gives it a title bar and a frame it
did not have, and nothing tells the window it must be bigger, so Qt takes them
out of the contents. Measured on the reported case: the timeline dock tears off
at 120 px tall, its size hint becomes 150 the moment the decoration goes on — 24
px of title bar and 3 px of frame a side — and the window stays at 120. Thirty
pixels out of the strip, which is most of a 46 px row.

**The reported condition is what identifies it**: it only happens to a panel
nobody has resized. A dock dragged taller carries height above its own hint and
the decoration comes out of that; a dock left where it opened sits exactly on its
hint and has nothing but its contents to give. Any theory that does not explain
why resizing it first makes the fault go away is the wrong theory — that
condition ruled out "it falls back to its hint", which was two days of the
obvious answer.

The cure is to grow the window by the difference between the two size hints,
taken across the `setTitleBarWidget` call. Not by adding up a title bar height
and a frame width: both are the style's, one is doubled and the other is not, and
reading Qt's own arithmetic off its hint is right in both directions and in both
dimensions.

**None of it is reachable offscreen**, which is why it survived a suite that
tests `FloatingDockFrame` directly. Offscreen there is no native frame, so Qt
never hides its title bar, so the hint does not move when ours goes on and the
fix is a no-op — measured, 135 before and 135 after. `shots` cannot see it and
neither can a test. `tests/window_probe.cpp` is what saw it, on a real window
with a real hand, and it now marks any dock smaller than its own hint as
**SQUEEZED** so the next one is a glance rather than a subtraction.


### Why a rebuilt title bar matched every metric and still looked wrong
**A title bar is a drawn thing, not a row of widgets** — and this cost six
attempts, all of them spent matching numbers. `DockTitleStrip` paints itself with
`CE_DockWidgetTitle`, the same call `QDockWidget` makes, so the background, the
frame and the elided title come out right without any of it being restated. The
version before it was a `QLabel` and a `QToolButton` in a layout, and every
measurable thing about it was eventually made to match — strip height, button
box, icon, icon resolution — while it still looked wrong, because nothing was
painting the background at all. The reporter is who noticed: *the title and the
cross are in a rectangle when docked and in nothing when floating.*


### What size a title-bar button's glyph is really drawn at
**And `iconSize()` on a title-bar button is an upper bound, not the drawn size.**
The last of those attempts had every number identical to Qt's and still drew a
cross half again too big. The rule Qt follows is that the glyph is fitted to the
button's *inside*: the button is 20 px, `PM_DockWidgetTitleBarButtonMargin` is 5
a side, so the icon is drawn at 10 — whose ink is 9 px across, which is exactly
what Qt paints. `iconSize()` reads 16 and is never the answer. Every metric
reached for instead of this one — `PM_SmallIconSize`, `PM_ButtonIconSize`,
`QDockWidgetTitleButton::sizeHint`'s formula, `SE_DockWidgetCloseButton` —
reports the bound rather than the drawing.

The shot `panels-the-close-button-itself` is what settled it and is worth
reaching for before any more arithmetic: it magnifies Qt's button and ours side
by side and prints the bounding box of the *ink* in each. Every metric can agree
while the painted glyph does not, and only the picture says so.


### The flag a drag that ends outside the window leaves set
**Dragging a panel out of the window stops the layout running, and it is Qt's
bug rather than ours.** This is
[#54](https://github.com/S-poony/Animage/issues/54), and it is worth reading
whole, because almost everything that made it hard was about *how it was
answered* rather than about the answer.

`QMainWindowLayout::setGeometry` begins:

```cpp
    if (savedState.isValid() || (restoredState && isInApplyState))
        return;
```

`savedState` is the copy the layout takes in `unplug` when a dock is pulled out
at the *start* of a drag. A drag that ends with the panel outside the window
never clears it, so every child of the window keeps the geometry it had — the
canvas stays wider than the window, and the status bar sits above the bottom
with unpainted window behind it. What gets noticed is a **blank strip** where the
panel was, which is that seen through the canvas's `WA_OpaquePaintEvent`: the
canvas is not covering the region and nothing else paints it either. Docking the
panel again clears the flag, because docking is a `plug`, which is the whole
reason it presents as intermittent rather than as a window that is simply broken.

**It reproduces in plain Qt** — one `QMainWindow`, a central widget, a status bar
and two stock docks, none of this program in it, Qt 6.11.1, plain mouse. That is
what makes it upstream, and it is the experiment the issue had been sitting on.

**And upstream had already found it.** It is
[QTBUG-147209](https://bugreports.qt.io/browse/QTBUG-147209), fixed on
2026-06-03 by `e9a22af5ab7f`, whose message is this bug in our own words —
*"Saved state was not cleared when a dock widget is dragged. This caused an early
return in `QMainWindowLayout::setGeometry()` and subsequently in main window
children not receiving resize events."* Three lines of Qt say the whole story:

| Qt | what `endDrag` does with the flag | |
|---|---|---|
| 6.11.0 | `mwLayout->restore();` — a defaulted bool, clearing | correct |
| **6.11.1** | `mwLayout->restore(QInternal::KeepSavedState);` | **the bug** |
| 6.11.2 | `mwLayout->restore(QInternal::ClearSavedState);` | fixed |

So it is a regression with a narrow blast radius: a commit called "Code cleanup
in QMainWindowLayout" replaced a defaulted `bool keepSavedState` with a
`QInternal::SaveStateRule` enum, and one call site got the wrong enumerator.
**Nothing that ships has ever had it**, and that is now a pin rather than an
accident: `ci.yml` builds Windows against 6.10.3 and Linux and macOS against 6.8,
and this is a 6.11.1 bug. So it was only ever visible to somebody building locally
against MSYS2's Qt while it sat on 6.11.1. **A Windows pin of 6.11.1 would ship it
for the first time**, which is what the version check below is for, and is one
reason that pin is exact rather than `6.11.*`.

`MainWindow::wakeLayout` is therefore scoped to exactly that release, through
`QLibraryInfo::version()` rather than `QT_VERSION`, because Qt is a DLL here and
the build that compiled the check need not be the build that runs it. **When the
oldest Qt anyone builds this with is 6.11.2, the whole thing can be deleted**,
signal and all.

**Two hours of the search would have been saved by checking the version first.**
The Qt source was read from `qt/qtbase` at branch `6.11` — which is *ahead* of
the 6.11.1 tag — so it showed the fixed code while the binary on the desk ran the
broken code, and the measurements and the source disagreed for no visible reason.
Nothing was wrong with either. **Read the source at the tag you are running**, and
when a measurement contradicts the source, suspect the version before suspecting
the measurement.

**What the guess got wrong is the interesting half.** The issue reasoned that
Qt still believed a drag was in progress. It does not. Measured, in order: the
mouse release *is* delivered as an ordinary client-area event, `endDrag` *does*
run, the mouse grab *is* dropped, the space the panel left *is* reclaimed — and
the flag stays set. `currentGapPos`, `pluggingWidget` and `movingSeparator` are
all clear throughout. It is one flag and not a wedged state machine, and the
difference matters: a wedged machine wants poking, one flag wants clearing.

**`restoreState(saveState())` is the cure, and it is measured rather than
chosen.** `MainWindow::wakeLayout` does it on the moment the drag settles, on
the one Qt that needs it. Six
other candidates were applied to the frozen state and each left it frozen:
hiding and showing the dock, `setDockOptions`, `addDockWidget` again,
`invalidate()`, `setFloating` off and on, and `setTitleBarWidget` — which is what
#50 does, so **#50 does not cure this** and the two fixes are independent.

A seventh does work and is unusable: a mouse press and release on a dock
separator, with no move in between, makes Qt clear the flag itself through
`endSeparatorMove`. It is surgical and it needs a separator to exist, and when
every panel is floating there is none.

#### Three things about how this was answered

**`shots` cannot see it, and that is not a failure of `shots`** — a conclusion
Qt's own maintainers reached independently, since the commit fixing it says
*"The behavior can't be autotested, because a gradual resize of the main window
is prone to flakiness."* The state cannot
be reached from code: `setFloating(true)` never enters Qt's drag, so it freezes
nothing, and a synthetic drag sent to a `QDockWidget` leaves Qt's own machine
half finished and produces convincing symptoms that are not this one — the trap
[#50](https://github.com/S-poony/Animage/issues/50) already records. A real hand
is the only way in, and `QWidget::grab()` forces a repaint on top of that, so
even a frozen window photographs clean. **The geometry is wrong; the painting is
not.** Before reaching for `shots` on anything about docks, ask whether the state
can be reached without a hand.

**What answered it was a standalone plain-Qt program**, which is kept:
`tests/dock_probe.cpp`, and it has [a section of its
own](#asking-qt-a-question-directly). Reach for it before reasoning about
anything a dock does. It answers questions about *Qt*, which is a class of
question this program keeps needing and cannot ask from inside itself, and the
answer it gave here — plain Qt does this too — turned the work into a much
shorter one.

**Forging the broken state is what made the cures testable by machine.** The hand
test established that the whole fault is one flag with everything else already
cleaned up — so `savedState.rect = layoutState.rect` reproduces it exactly, with
no drag. Eight cures were then tried in one run of a program, in about a second,
where each would otherwise have cost a hand-made drag. **Measure the state once
by hand, then forge it.**

#### And a lesson about believing a bug report, including your own

The first hand test of the fix came back with four complaints: a panel landing
below the pointer, the timeline losing height, a panel not keeping its width
across a side change, and resizing gone laggy. All four read as regressions.

Two builds settled it — the fix and today's `main`, side by side in one folder,
same four gestures each. **Three of the four happen without the fix**, and are now
[#55](https://github.com/S-poony/Animage/issues/55),
[#56](https://github.com/S-poony/Animage/issues/56) and
[#57](https://github.com/S-poony/Animage/issues/57); the fourth did not reproduce
at all and was a machine having a bad minute. Nothing in the fix caused any of
them.

The cost of not doing that was already being paid: eight lines had been written
to save and restore the floating panel's geometry around the round trip, to fix
the panel landing below the pointer. They fixed nothing, because the round trip
does not move the panel — checked afterwards in the bench, with Qt's decoration
and with the frameless one #50 gives it. They are deleted, and the reasoning is
in the comment on `wakeLayout` so nobody writes them again.

**A regression report is a claim about a difference, so it needs both sides.**
Building the other side costs one `git stash` and one incremental build here, and
it is cheaper than the change it stops you making.


### What asking for a private Qt component at the top level switches off
**A component added to the top-level `find_package(Qt6 ...)` can silently turn
the whole application off.** `test_pen_promotion` needs a private Qt header, so
`GuiPrivate` was added to the `COMPONENTS` list in the root `CMakeLists.txt`.
Qt installs that ship no private headers — which is what CI has — then fail that
one `find_package` *entirely*, `Qt6_FOUND` comes back false, and the
`if(Qt6_FOUND)` around `src/app` skips every GUI target.

**What makes it a trap is how quietly it goes wrong.** Configure succeeds. The
build succeeds. `ctest` reports *100% tests passed* — of ten, because the seven
that need a window were never built. Nothing says "the application was not
compiled" until packaging fails at the very end with `Could not find app
bundle`, `Cannot find path animage.exe`, and `Could not find Qt modules to
deploy`: three platforms, three unrelated-sounding messages, none of them naming
the cause. The one line that does is `-- Qt 6 not found: building the core
library and tests only`, sitting in the middle of a successful configure log.

So a private module is asked for **in `tests/` and as its own package**:

```cmake
find_package(Qt6GuiPrivate QUIET)
if(Qt6GuiPrivate_FOUND)
  ...
else()
  message(STATUS "Qt private headers not found: skipping test_pen_promotion")
endif()
```

Its own package rather than another `COMPONENTS` list, so a failure cannot
clobber `Qt6_FOUND` for anything configured after it. A test that cannot be
built is a test that does not run; an application that cannot be built is a
broken release, and the two must not share a switch.

**The general rule: nothing the application needs to build may depend on a
private Qt header, and nothing may be added to the root `find_package` that is
not available everywhere the program is released.** To check a change here
without waiting for CI, configure with the private package disabled and confirm
the app target survives:

```
cmake -S . -B build-check -DCMAKE_DISABLE_FIND_PACKAGE_Qt6GuiPrivate=ON
```

It should print the skip line, still find Qt, and still build `animage`.


### Why fitting the canvas on a maximise cannot trust either event alone
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


### The nine-character band where an export makes the folder and no frames
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


### What `findChild` hands back after the view has closed its editor
**A closed editor is still a child of the view.** An item view releases its
editor with `deleteLater`, so `findChild<QLineEdit*>` hands back the dead one
until the deferred deletes run. A rename test typed into a closed editor and
passed — a rename that goes nowhere leaves the name alone exactly as a refused
rename does, so the assertion was true and meant nothing. `settleEditors` sends
the pending `DeferredDelete` events before anything looks for an editor.


### Where the first dock-width reading was taken, and why the fix shipped twice
**A before-and-after test is only a test if "before" is before.** The layer dock
grew by eighteen pixels when a colour layer was selected, shoving the canvas
sideways. The test written for it read the width *after* the box was showing and
then checked it did not shrink on the way out — both readings on the far side of
the growth, so it asserted that the bug did not un-happen, passed, and shipped
it. Twice, because the first fix was reported as done on the strength of that
green test. Measuring a quantity twice on the same side of the event you care
about will agree with itself perfectly and say nothing. Related: the dock is
sized from the panel's *preferred* width, so a minimum does not hold it still.


### What emptying the fill cache does not reach while a solve is in flight
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


### What went stale when the solve stopped finishing in the same call stack
**Anything that rode on a re-solve was riding on it being synchronous.** The
layer panel's tooltip was refreshed by the solve that changing the colour
sources happens to trigger. Invisible while the solve finished inside the paint
that started it; a stale panel the moment it did not. What a row says about a
layer is about the layer, so it is said when the layer changes. Expect more of
these: anything that was correct only because two things happened in the same
call stack.


### What a view that skips the solve also skips
**Skipping a solve because its picture will not be drawn skips everything else
that solve produces.** A colour layer with the Marks column ticked draws its
scribbles instead of its fill, so `requestCtgFills` left it out, and the
export's `needsFill` did the same, both saying the fill would be computed and
then not used. True — and where a carried mark ended up is worked out inside the
same solve and nowhere else. With no solve, `ctgCarryAt` answers with the
default it gives before anything has solved a drawing, and that default is
indistinguishable from "the marks did not move". So the one view whose whole job
is to show what the solver saw was the only view that could not, and drew a
carried mark where it was made instead of where it is used.

Reported as marks that do not carry when you tick the box. The picture was the
smaller half: the first stroke on a carrying drawing copies what it was
*showing*, so drawing while looking at the marks wrote the wrong position into
the cel, where unticking the box does not reach it and no re-solve will undo it.

Both places solve now, and marks view costs a solve like every other view.
Part two's rule was that a derived value which changes what is drawn has to be
reachable by everything that draws it; this is its other half — **the thing that
derives it has to actually run, and a view is not entitled to switch it off.**
`aLayerShowingItsMarksIsStillSolved` in `test_canvas` constructs it.


### The answer that was called off a moment before it landed
**A queue that never catches up may be a queue where nothing ever finishes**,
and the two look identical from outside. `dropStaleColourRequests` cancelled a
fill the moment its drawing went off screen, with a reason on it: playing a
coloured shot asks twenty-four questions a second against solves taking a tenth
of one, and a queue that fills faster than it drains never catches up. A paint
drops and *then* asks, so at 24 fps every solve was called off 41 ms after it
started — and `abandoned()` is checked inside the max-flow, so it gave up
partway and produced nothing at all. Playing a coloured shot never coloured
anything, however long anybody watched. Issue
[#85](https://github.com/S-poony/Animage/issues/85).

**The same line, copied, did the same thing to imported pictures**, where it
was found first: an animatic played as a blank canvas for the same reason, and
said so through a libpng warning repeating once per file per pass. Two
subsystems, one sentence, and neither report described the cause.

Three things to take from it.

**The stated reason was measurable and nobody had measured it.** The queue is
bounded by one entry per drawing per layer, because a repeat about the same
drawing supersedes — the shot, not the take — and a job's grids are handles to
cels that exist regardless. So it could not fill faster than it drained in the
way the comment claimed. What it could do, and did, was never finish.

**An answer for a drawing you have left is not a wasted answer.** It is the
drawing you will be on again next time round the loop, and the cache it lands in
is bounded, so keeping it cannot cost more than the bound. Judgements were
already kept on exactly that reasoning — being about the drawings you are not
looking at is the whole of what they are for — and it took a report to notice
that fills are the same.

**And the benchmark that should have caught it was measuring the other
question.** `bench_playback` calls `presolveColour`, which walks the shot and
waits for every fill *before* the timed pass, so it plays a shot whose colour is
entirely on hand. That is a fair question — does a solved fill survive to
playback — and it is not how a shot is coloured: you scribble the first drawing
and let `ctg_inherit` carry the marks, so every drawing after it has never been
looked at and pressing Play is the first thing that ever asks. `coldColourPasses`
plays without presolving and reports what is on hand after each pass, which is
where the bug is a table rather than an opinion:

| | before | after |
|---|---|---|
| pass 1 | 1 of 48 | 6 of 48 |
| pass 2 | 1 of 48 | 11 of 48 |
| pass 3 | 1 of 48 | 16 of 48 |
| pass 4 | 1 of 48 | 21 of 48 |

The one is the drawing that was scribbled. **A benchmark that warms the thing it
is about measures the warm case**, and that is worth checking of every fixture
here before trusting a row of it.

Two things the fix needed beyond the deletion, both about not swamping the
solver with work nobody is waiting for. **A take does not climb the ladder** —
the coarse answer is a tenth of a second and the fine one is a second and a
half, and asking for the second as soon as the first lands is forty-eight fine
solves nobody asked for. An animator watching a take is judging motion and where
the colour went, which is the argument
[playback-resolution.md](playback-resolution.md) already makes one subsystem
over. And **playback asks at `Whenever`**, so a stroke made after the take jumps
a queue two seconds long rather than joining the back of it.

### What the lattice fallback was being compared against
**A floor between two answers is a measurement, and this one was being taken in
the units of the wrong question.** When the rest run moves no node,
`estimateCtgLattice` starts a second run from rung two's answer and keeps it
only if it matches better. That floor was a sum of block differences, and on two
reported shots it threw the right answer away: the last drawing of each was the
only one left uncoloured, its marks sitting on the paper a shape had moved off.

**A difference cannot answer it.** It charges a wrong alignment twice — for the
ink each drawing puts where the other has none — and charges covering nothing
once. Two drawings of a moving shape never coincide, so "stay on the blank
paper" is a live answer and not a corner case: rest scored 410.5 against the
fallback's 406.2 on one drawing, and 212.7 against 221.2 on another. This is
part two's lesson about `agreement` arriving two rungs up, in a place where the
premise above `blockDifference` — a block covers the same samples at every
offset, so nothing shrinks — is not true. Two *poses of the whole lattice* put
the same source over quite different amounts of the target's ink.

**And agreement cannot answer it either**, which is the half worth writing down,
because swapping the measure is the obvious fix and it is wrong. An alias agrees
*better* than the truth — the sweep above `agreement` is exactly that
measurement — so scoring the floor on agreement put 819 px of movement onto
`bench_carry`'s two shapes with one of them standing still, and lost both
colours on a row that had been keeping one.

**What decides is not which measure but which question, and the rest pose's own
agreement says which.** Reaching the fallback already means no node moved. If
the rest pose agrees with *nothing* — zero, exactly, which is what it was on
every one of the nine fallbacks across the two reported shots — then there is
nothing under the lattice to have an opinion with. That is the "no evidence"
`moved` was standing in for, said directly, and then anything the fallback finds
beats nothing and a difference must not be consulted, because what it would be
scoring is how cheaply the source sits on bare paper. Above zero something is
under the lattice and stayed put, and the original difference floor is right.

Both halves are load-bearing and each one alone is a regression, in opposite
directions. All five benches are identical to before —
`bench_carry`, `bench_shapes`, and `bench_hand` on all three projects.

Two things this cost that are worth having straight. **The benches could not
have caught the bug and cannot defend the fix on their own**: every fixture in
`tests/` moved a shape that still overlapped itself, so nothing reached this
branch at all, and all five were byte-identical across a change that was
wrong. `tests/projects/moved-clear-of-itself.animage` is in the tree for that
reason and `oneScribbleReachesTheEndOfTheShot` reads it. And **restoring a file
with `Move-Item` keeps its old timestamp**, so ninja skips it and the next run
measures the build you thought you had replaced — which is how the first fix
looked verified and was not. Touch it, or check the object is newer.


### The tests that construct the bug, and go red when it is fixed
**A test that constructs a failure will be repaired by the fix for it.** Several
tests build a mark that lands in the wrong place, and moving marks with the
drawing makes them land right — so they went red on a change that was working
perfectly. They now turn the moving off and say why. The tell is a *whole
fixture* going green-to-red on a feature that is supposed to improve exactly
that case; read what the test was for before believing the failure.


### Why a cache key of cel revisions serves wrong fills, not slow ones
**A cache key made of revisions was going to lie, not thrash.** The CTG fill
cache was keyed on the cel holding the scribbles, which was a bijection for
exactly as long as one drawing had one scribble cel. The design notes predicted a
shared slot would thrash; it would have served wrong fills, because `inputs` is
mixed from cel revisions and revisions collide freely — **every cel in a project
straight off disk is at revision 1**. Two drawings inheriting one scribble with
equally-worn line art would have swapped answers. The key is `(drawing, layer)`,
and `inputs` names the scribble *cel* and not only its revision, because
reordering changes which cel is read and moves no revision anywhere.


### What a coarse level decides that the fine levels never revisit

A coarse-to-fine search picks its peak at the top of the pyramid and every level
below only refines that choice. The top level is the one least able to make it:
at a dozen cells across, two shapes a hundred pixels apart are one smudge.

On drawing 2 of `tests/projects/two-circles.animage` it answered -84 px, which
scores 3.73 at the finest level, while +84 px scores 4.08 and was never looked
at. Not a near miss — the better answer was never a candidate. The top level
keeps several of its peaks now, separated by more than the refinement window or
they converge on the same place, and each is carried down separately; the one
still best at the finest level wins. It cannot answer worse than before, because
the peak the old search started from is always among the candidates.

Two things this cost that are worth having straight. **A bound applied at the
top of a pyramid is not a bound**: every level below refines by two of its own
cells, which is 2^level of the finest ones, so a region allowed to depart 75 px
departed 114. It has to be clamped at every level. And **a better search is not
the same as better answers** — with the alias no longer missed by luck, one row
of `bench_carry` got worse, because the search now finds the highest-scoring
alignment and on that row the highest-scoring alignment is wrong. Which is the
next entry.

### Why the wrong alignment is sometimes the better answer

The obvious thing to blame when a carried mark lands on the wrong shape is the
score. `agreement` is a sum of ink times ink, so ink landing on blank paper
earns nothing and *costs* nothing: covering one shape exactly and abandoning
another ought to beat covering both partially for the wrong reason.

It does beat it, and that is not why. On two-circles drawing 3, where the
estimate slides 780 px sideways and puts one circle's mark on the other, the
alias wins on every criterion tried:

| | alias (780 px) | honest (−72 px) |
|---|---|---|
| agreement | **5.181** | 4.606 |
| intersection over union | **0.350** | 0.219 |
| normalised cross-correlation | **0.636** | 0.440 |
| worse of the two coverages | **0.527** | 0.413 |
| source ink left unmatched | **8.72** | 33.18 |

A sweep over every shift picks it under each of them. The circles move most of
their own width between drawings, so lining each up with itself overlaps badly
and lining one up with the other is nearly exact — the data supports the alias.

So the model is wrong and the measure is not. One translation cannot describe
two things that moved differently, and asked to anyway it reports whichever
single thing it can explain best, which has nothing to do with which thing the
marks are on. A ratio test does not save it either: on the drawing before, the
*right* answer wins by ×1.09, and here the wrong one wins by ×1.13. This is
written above `agreement` in the source as well, because that is where somebody
about to change it will be standing.

### Why an erased scribble left every later solve on that drawing coarser
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


### Why the proposed confidence score reads 1 on every case
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


### Why the carried-mark flag was removed rather than tuned
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


### Why a sum of products is not a score for a block
**A sum of products is only a fair score where both sides are the same size and
the overlap does not move with the shift.** Rung two is that case, and the note
above `agreement` is the measurement for it: a whole drawing against a whole
drawing, where a difference charges a wrong alignment twice and charges sliding
off the edge once. That rule was then carried into two places where its premise
is absent, and in one of them it was worth 146 px of invented movement.

Rung four's push step matches a fixed-size block that covers the same number of
samples at every offset. Nothing shrinks, so nothing penalises a wrong answer —
and what is left is a quantity **largest wherever the target has the most ink**,
whatever shape it is in. Every node was pulled towards the nearest dense thing.
The two measures disagree about blank paper and blank paper is most of a
drawing: under a difference, blank against blank is a perfect score and bare
paper says "there should be nothing here", where under a sum of products blank
against blank and blank against ink both score zero and bare paper says nothing
at all. The paper minimises a sum of absolute differences (§3.1, equation 1) and
so does this now.

Rung three's region search is the same shape of mistake one rung down and is
**still there**, because both attempts to fix it were worse. It masks its source
to one region and leaves its target whole — deliberately, and that is what makes
it a region's question — so a small masked source slides over a large unmasked
target and prefers wherever that target is densest. Two replacements were
measured and neither survived the second colouring:

| rung three, whole shot | colour 1 | colour 2 |
|---|---|---|
| `agreement` and the reach bound | 9 to fix · 0.5% wrong | **9 to fix · 0.7% wrong** |
| divided by the target under the mask | 9 to fix · 0.6% wrong | 10 to fix · **8.1%** wrong |
| a difference under the mask | 14 to fix · 0.7% wrong | 15 to fix · 2.3% wrong |

Normalising fails in a way worth knowing before anybody tries it a third time:
dividing by what is under the mask makes a region that landed on **nearly blank
paper** score well, because a small numerator over a small denominator is large.
On `colour 2` drawing 15 it took the picture from 1.0% in a wrong colour to
85.9%.

What that says is that `agreement` **and the reach bound together** are doing a
job neither half does alone — the bound is holding back a score that prefers the
wrong place — so the next attempt is not a third score. That is
[#68](https://github.com/S-poony/Animage/issues/68).

And the reason both attempts looked like improvements first: they were measured
on `colour 1`, the colouring made while rung two was running. **A rung scored on
marks placed for a different rung is measuring the marks**, and this is that
sentence catching something on its way in rather than after.


### What a comment goes on claiming after you replace the design under it
**The design was changed, the comment above it was not, and the comment was the
thing that read as correct.** `estimateCtgLattice` runs the lattice twice and
the first version chose between the two runs by their match score. That was
measured and was wrong — on two-circles the aliased translation genuinely
matches better, so scoring picked it — so the choice became "use the second run
only when the first one moved no node at all". The code was rewritten. The
fifteen lines above it went on saying "both are run and the one that matches
better is kept", and, a paragraph later, "it cannot answer worse than either
start alone".

Neither sentence was true any more, and the second one was a promise the code
had stopped keeping: the fallback replaced the first run unconditionally, so a
drawing that had correctly found nothing to do could be handed rung two's alias
instead. `LatticeFit::cost` was still computed on every call, with its own
justification attached, and read by nobody.

An outside review found it in one pass by reading the comment against the code —
which is the whole argument for having one. Two things about this worth keeping:
**a comment that survives the design it described does not decay into being
vague, it decays into being false**, and it is the most confident thing in the
file while it does so. And **the leftover of a design is usually still visible**:
a field computed and never read is where the removed rule used to be, so an
unused member is a place to go looking rather than a tidiness question.

The fix kept the cost after all, as a floor rather than as the choice — a
fallback that matches worse than the run it replaces is not an improvement —
which is a third thing: the measurement that killed the old design did not kill
the quantity it was measured on.

The floor then turned out to need a second quantity beside it, and this section
came within one paragraph of repeating its own lesson: the sentence above was
written when the cost was the whole rule, and it stopped being the whole rule.
See "what the lattice fallback was being compared against".

### What a default member initialiser reaches that you did not mean it to
**`ANIMAGE_CARRY` was the default of `CtgSettings::carry`, so it reached the
tests.** It was added as temporary scaffolding, so that a person could run the
program on one rung and then another and look at the difference — the one thing
no benchmark can do. Written as `Carry carry = carryFromEnvironment();` it also
became the default of every `CtgSettings{}` in the process, and `test_ctg`,
`bench_carry` and `bench_composite` all build one.

The failure needs nobody to make a mistake. The handover tells the reader to set
the variable and run the program; PowerShell keeps it set for the rest of that
shell; the next `run-tests.bat` in the same window then asserts rung two's
semantics against whichever rung was named, and `bench_composite` prints a line
labelled "the same per region" that is measuring rung four. Nothing fails
loudly; the numbers are just about a different thing than they say.

It is the rule in [scribbles-through-time.md](scribbles-through-time.md)'s
"things not to do", arriving from an unexpected direction: *a flag that tells the
user is fine; a rule that changes behaviour behind them is not.* The variable and
`applicationCtgSettings` are both gone now, with the choice they existed for, so
the compiled default is what everything gets.

**And the same trap fired again from the other side the moment that default
changed.** `bench_composite` timed a line labelled "the same per region" through
a bare `CtgSettings{}` — which was rung three while the default was rung three,
and became rung four the instant the default did. It read 78 ms against the
lattice's 78 ms and would have gone on being read as rung three's cost. It names
the rung it wants now. **A measurement that names a rung has to ask for it**:
inheriting a default is fine for behaviour and never fine for a label, because a
label is a claim and the default is free to move underneath it.


### What a stylesheet with no type selector also paints
**A Qt stylesheet with no type selector styles the widget's tooltip too.** A
swatch styled `background:#c00` handed its own colour to its own tooltip, and the
slashed "no colour" swatch drew a red streak through the text of its. Name the
type: `QPushButton { ... }`. Reported by the user; an offscreen `grab()` does not
contain the tooltip, so it cannot be caught by screenshot.


### How long a `QTreeWidgetItem` pointer stays valid once a rebuild is queued
**A queued signal that rebuilds a list deletes rows out from under whoever holds
one.** Reporting a finished solve by calling `rebuildLayerList` crashed
`test_canvas`: a solve runs inside a paint, so the report has to be queued, and
the rebuild clears the tree. Every `QTreeWidgetItem*` had quietly become valid
only until the next event-loop turn. A finished solve changes two words and a
colour, so it changes two words and a colour, in place. gdb found it in one run
after two wrong theories — the same lesson as the two crashes above, learned
again.


### What the screenshot showed, and what the run had never set up
**Look at the thing, and check the harness is looking at it.** A screenshot
caught the "None" control being a second red-slashed swatch beside the one that
already showed a red slash — two identical patches, one a readout and one a
control, with nothing to say which. The same screenshot run had silently failed
to add the colour layer it was testing, because it looked for a `QAction` where
the button is a `QPushButton`, so the first two pictures were of nothing at all.


### The gap coordinates that built an open box out of plausible numbers
**A test fixture can build a shape that is not the shape you meant.**
`drawGappedBox` takes the gap as coordinates, and handing it two outside the box
gives a bottom wall running six hundred pixels past the corner — not a closed
shape at all. The printed numbers looked perfectly reasonable.


### Four tool states in three booleans, and the pair a handler half-cleared
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

### What the tests for a failed swap do and do not prove
**A seam that lets a test watch the failure is not the same as having seen the
failure.** `swapIntoPlace` takes the rename as an argument so a test can hand it
one that refuses on the second call. Everything else in those tests is real —
real folders, real projects in them, and what is asserted afterwards is what is
actually on disk. So the *recovery* is genuinely tested: the previous project
goes back, or both copies are kept and both are named, and a rescue copy is
moved out of the scratch path.

**What is reasoned and not reproduced is the trigger.** Nobody has made Windows
refuse that rename. The argument is that the first rename moved `folder` away,
so the second fails if anything recreated or locked the path in between — a
cloud-sync client watching the folder, an antivirus handle — and that the
restore then fails the same way. That is plausible and is why the code guards
it; it is not an observation. If it turns out the second rename cannot fail
while the first succeeds, these tests still pass and are still testing nothing
that happens.

**And the one thing no test here covers is whether anybody reads the message.**
An autosave failure is a status-bar line for eight seconds, deliberately — a
dialog every two minutes would be worse than the fault it reports. But this
message now carries two paths and an instruction, which is not what a line that
disappears is for, and this is the one failure where the project is not at its
path at all. Left as it is rather than changed quietly; it is a decision about
interrupting somebody who is drawing, and it belongs to whoever owns that.

### Why an eraser passing a stroke chewed it, and a pan left the damage behind
**A box filter whose answer depends on the rectangle it was asked for is not a
filter, it is a function of the caller.** That is
[#64](https://github.com/S-poony/Animage/issues/64), which has the repro, the
measurements and the zoom table; what is here is what they taught.

`planColumns` widened its first sample's block back to the edge of the region it
was handed and clipped the last one to the far edge, normalising by the weight
that produced, and the boxed row loops did the same down the rows. Snapping to
the sample grid — which the cache does, and which its comments lean on — makes a
partial refresh land on the same entry *indices* as a full one; it does not make
it read the same *samples*. That was the gap, and it is the kind that hides
behind a true statement.

The canvas composites one dirty rectangle per dab while the pen is down and the
whole cached region when it lifts, so every dab wrote a line of wrong entries
around itself and the lift wiped the lot. **The eraser is where it is visible and
not because the eraser is special**: it is three times the brush's radius and it
changes nothing underneath, so what you see move is unambiguously the renderer.
Drawing does the same around its own new ink, where nobody was looking — and a
pan does it with no lift to follow, which is the more serious half and the one
nobody reported.

**The zoom signature was the whole diagnosis.** Below 100% the cache is reduced;
at 100% and above the ratio clamps to 1 and the reducing path is never entered,
so there is nothing to be wrong. The report said "strongest at 100%, stops at
120%", which sounds like it contradicts that and does not: **the status bar prints
zoom to no decimal places**, so anything from 99.5% to 100.4% says "100%", and
the wheel steps by 1.2, so the next stop above is 120%. A reported zoom is a
range and a reported *step* is a ratio; read a symptom that names a percentage
with both in mind.

The fix is that a block reaching past either end of the region is read past
either end, and whatever lands in an entry nobody asked for is dropped rather
than folded into the entry that is there. `blendLayerRowsBoxed` and
`blendFillRowsBoxed` no longer take a region at all, which is the part worth
keeping: the invariant is structural now rather than a rule someone has to
remember. `boxSampleStride` clamps to one entry for the same reason — it changes
nothing today, and it is what the three loops that assume it cannot check.

**And the same fault had a second door, which only looking for it found.** A
moved pass — the one kind there is, a colour layer showing carried marks — took
its rows from `entryAt(region.y - offset.y)`, the read row of the region's
corner, indexed as though it were the drawn one. That is the row half of exactly
the mistake `planColumns` had already been fixed for on the columns: `entryAt`
floors, so the read grid and the drawn grid do not step together. The rows carry
`offset.y` now and re-anchor to nothing, which is what the columns already do.

What pins it is one assertion made against every kind of source the reduction
has: sweep dab-sized bands over the picture and require each entry to equal what
the whole-region composite produced, exactly. `test_brush.cpp` does it for an
ordinary layer, over the origin and left of and above it; `test_ctg.cpp` does it
for the two the first fix nearly missed — the moved pass, at offsets that are
deliberately not whole entries, and the fill, which is a different reading of the
same idea and so a separate piece of code that can rot on its own. Each was built
against the reduction it replaces and each went red there.

`aSampleBlockIsNeverLongerThanAnEntry` is the odd one out and is the only guard
here that passes on the code it was written against. It asserts the arithmetic
the three loops assume and cannot check — that a block of pixels never reaches a
third entry — because that one is a constant away from being false and would fail
by quietly averaging slightly wrong rather than by going red. Confirmed by
raising the budget and removing the clamp, where it does go red, and says which
block and which entry.

**What the second door says about the first.** The eraser fault was found,
diagnosed and fixed, and that fix was measured clean across forty-five zooms
before anything looked at the pass with an offset in it — which had the same
fault, a worse version of it, and was not covered by the sweep that said the fix
worked, because the sweep only ever composited what its own fixture drew. **A
measurement that says a fault is gone has said it about the paths it exercised
and about nothing else**, and the path most likely to be missed is the one with
an extra parameter, because that is the one a straightforward fixture does not
build.

### Every route that changes the input to a differencing function
**`syncTimelineHeight` does not size the timeline dock, it moves it by the rows
that came or went** — deliberately, because measuring the wrapping round the
strip was wrong twice. The cost of working in differences is that every route
that changes the track count has to say so, and one did not:
[#74](https://github.com/S-poony/Animage/issues/74), where opening a
three-track project into a one-track window left the strip at the height for one
row. `afterProjectLoaded` rebinds the canvas, the timeline and the panels and
never called it.

**What made it look mysterious is what pointed at the cause.** The report was
"Ctrl+Z puts it back up", which sounds like undo knowing something about docks.
It does not: undo goes through `refreshEverything`, which is the next thing in
the program that calls `syncTimelineHeight` at all. It was still holding
`timeline_rows_shown_ = 1` from before the load, so it saw a difference of two
rows and spent it. **A fix that arrives on an unrelated action is a function that
was never called on the path that needed it.**

**And the same shape caught the test that was written for it.** Two
`Add track` triggers back to back grow the dock by *one* row, not two: both ask
for "the height you are now, plus a row", and the height has not moved between
them because the layout has not run. The test read 179 where it wanted 225 and
looked for a while like the fix overshooting. `processEvents` between the
triggers is the whole of it — but the lesson is that a differencing call and a
deferred layout are two ways of saying the same thing, and a test that drives the
first has to pump the second.

### A tablet gesture is not one device's
**The pen's barrel button reaches Qt as a *mouse* right press, and the drag that
follows it arrives as tablet moves.** So "Alt and the right button, dragged
sideways" -- the brush resize -- is one gesture spread across two event streams,
and it has to stay that way: the hand doing it is holding the pen in the air,
not touching the tablet with it. Any rule of the form "a gesture belongs to the
device that started it, and only that device may drive it" breaks the gesture
outright. That rule was designed, written down and shown to the maintainer before
it was found to be wrong, which is the cheapest place to find it.

**What must not cross is the *end*.** The release that closes a gesture is the
release of the press that opened it, and nothing else. Resting the nib on the
tablet in the middle of an Alt-and-barrel resize used to be read as a fresh press
-- which, with Alt still held, is the eyedropper -- and the nib lifting used to
be read as the end of the resize the other hand was still holding.
[#76](https://github.com/S-poony/Animage/issues/76). So `navigation_opened_`
records whether a tablet press or a mouse press opened the gesture, presses from
the other stream are swallowed, and only the opener's release ends it. Moves are
deliberately not gated at all.

**Classified by the event that arrived and never by the physical device**, for
exactly the barrel's reason: it is the pen, and it presses and releases as a
mouse. A check of "is this really the pen" would get this one backwards.

**And the one thing the swallowing must never eat is a release that would close
an open stroke.** `CanvasWidget::abandonGesture`'s comment has the cost:
`Document::beginCommand` nests by depth and commits at zero, so a stroke that
never ends leaves the depth at one for the rest of the session -- nothing reaches
the undo stack again, autosave defers for ever, and no colour layer solves. All
of it silent. That is why the guards are on the *press* and on `endNavigation`,
and why `releaseAt` still runs on a release from either stream.

**What a synthetic test can and cannot say here.** `tests/test_canvas.cpp` drives
the whole gesture with real `QMouseEvent`s and `QTabletEvent`s and is red without
the fix, which genuinely covers the routing. What it cannot say is what a real
pen sends -- that belongs to a tablet driver and a Windows Ink setting.
`tests/pen_probe.cpp` is for that: the real window, logging every pen and mouse
event the canvas is offered and what the canvas made of each. It writes one
synthetic event through its own filter first, so an empty log means the probe is
broken rather than the pen.

### What a save deletes that the document cannot write again
**A save builds a new folder and renames it into place, so every directory entry
in the project is replaced on every save** — and anything the build step did not
put into the new folder is gone. That is fine for a cel: its pixels are in the
document, so a save can always write it out again. It is not fine for an
imported file, because what the document holds is a **name**.

Nothing about getting this wrong announces itself at the time. The import looks
right, the save reports success, and the picture is missing the next time the
project is opened — or two minutes later, when autosave has fired and written
the folder without it.

So `ProjectIO::save` carries `imports/` forward, looking in three places in
order: the folder the last successful save wrote them to (`SaveState::folder`,
which is where they are for every save after the first, Save As included); the
path an import came from, for a project that has never been saved; and the
target folder, for a re-save whose state was lost. A name in none of them fails
the save **loudly**, while the original is still wherever the person imported it
from — a save that quietly dropped it would be discovered somewhere else, later,
by somebody who no longer has the file.

`anImportSurvivesSavingAndOpening` in `test_canvas` saves, saves again over the
same folder, does a Save As, **deletes the original picture** and reopens. The
deletion is the half that matters: without it the test passes on a project that
is still leaning on a path outside itself.

The general shape: **anything the project folder holds that the document cannot
regenerate has to be carried across the swap by name.** Cels are the exception
here, not the rule, and they are the only thing that was ever in that folder
before.


### A `shots` situation that presses an id nothing bound
**`Stage::press` takes a shortcut id, looks up the `QAction` the window made for
it, and does nothing at all if there is none** — silently, because an id with no
action is indistinguishable from an action that ran and changed nothing.

`Id::TransformApply` is such an id. Apply is a button on the transform bar and
its key is handled elsewhere; `makeAction` is never called for it. So
`s.press(Id::TransformApply)` is a no-op, and a situation that used it
photographed the transform still live — a picture that looks like a placement
that failed to commit, which is a bug you can spend a while looking for in the
committing code.

Use `s.choose("Apply")`, which finds the button by its label and clicks it.

**The general rule is the one this harness already has written down**: a shot is
only worth what the run behind it set up, and a situation that quietly did less
than it says is worse than no situation, because the picture is evidence. Before
believing a shot that shows something *not* happening, check that the step which
was supposed to make it happen actually ran. See [what the screenshot showed, and
what the run had never set
up](#what-the-screenshot-showed-and-what-the-run-had-never-set-up), which is the
same lesson one layer out.


### Which Qt classes answer "what are you" and which only answer "are you this"
**`QColorSpace` has no accessor that hands back which named colour space it is.**
There is `QColorSpace::NamedColorSpace` and there is a constructor taking one,
but a space read from a file is a set of primaries and a transfer function and
usually matches none of them exactly — so Qt offers equality against a named one
and not a name.

Naming a file's colour space for a message therefore means comparing against the
handful that are worth naming and falling back to `description()`. That is what
`image_import`'s `nameOfSpace` does, and it is worth a comment there because
"ask it which one it is" is the obvious thing to reach for and compiles into a
different error every Qt version.

Small, and here because it is the cheap end of a habit worth having: a Qt class
that models something continuous will let you *test* a value and often will not
*tell* you one.

### Three explanations for a bug nobody here had
**Issue #75 was a bug in Qt 6.8, and every instrument pointed at it was running
Qt 6.11.** Nothing in this repository ever caused it, nothing here ever fixed it,
and three separate mechanisms were published for it before anybody asked which Qt
the reporter's binary was built against.

The fault is [QTBUG-140649](https://qt-project.atlassian.net/browse/QTBUG-140649).
Qt's *windows11* style cut a `QSlider`'s handle flat at both ends of its travel;
the fix landed in **6.10.1** and 6.11.0 and was never picked to 6.8. A local
Windows build links against whatever MSYS2 has, which was 6.11 throughout. The
download was built against **6.8.3**, because `ci.yml` pinned `6.8.*` and 6.8.3 is
the newest 6.8 an open-source user can obtain. So the program on the maintainer's
desk did not have the bug, and the program the maintainer downloaded did.

**What was published, in order, and all of it wrong:**

1. *The style paints a handle bigger than the `PM_SliderLength` it publishes and
   `QWidget` clips it.* A workaround followed — four pixels added to the handle,
   the groove pulled in to match, the constant "bounded" at 2, 4 and 6 by reading
   magnified crops. Written into a commit, a header and a comment on #75.
2. *Retraction: there is no overflow at all.* A standalone Qt reproducer and
   per-pixel maps found the handle painting exactly inside its rect at every
   position and every scale factor. True — of Qt 6.11, which is what the
   reproducer was built against, and which had already been fixed.
3. *Installing any `QProxyStyle` changes which handle Qt draws, and `QWidget::grab()`
   renders some controls differently from the screen.* Both invented to explain
   why the fix appeared to work and why no instrument could see it. Written into a
   commit, a header, #81, and this file.

**Every one of those was reasoned from a picture of Qt 6.11.** Agreement between
instruments that share a version is worth nothing, and there was nothing wrong
with any of them: `shots` drew a whole handle because on 6.11 the handle *is*
whole. The `grab()` claim in 3 is withdrawn — nothing has ever shown a grab
drawing a control the screen does not.

**What ended it took ten minutes.** The maintainer downloaded the release, opened
it, and saw the fault still there with the workaround compiled in. One binary that
users actually run, looked at once.

**The reporter's screenshot was correct evidence the entire time.** It showed a
17x18 handle in a six-row groove where the local build draws a twelve-pixel ring
in a four-row groove. That difference was noticed and explained away twice — first
as *an older Qt*, which was the right answer, dismissed because the MSYS2 package
on the desk had been installed three weeks earlier; then as a hover or pressed
state, which the reporter denied. The right answer was raised and refuted with a
fact about the wrong machine. **Nobody asked which Qt built the binary in the
picture**, and that question is one line of `ci.yml`.

**So the rule.** When a report and your instruments disagree, the first question
is not *what is my instrument missing*. It is **are we running the same program** —
and [the same source, two different pictures](#the-same-source-two-different-pictures)
is that question in a form answerable in a minute. That section already existed
when #75 was filed, written after the same version gap produced a white-on-white
timeline. It was not opened.

`shots` prints its Qt, its platform and its style on the first line of every run.
`ci.yml` says what a download gets, per platform. If those two differ, the
difference is a suspect before anything in this repository is.

Windows now builds against **6.10.3**, which carries the fix. Not 6.11, and the
reason is worth knowing before anyone tries: `install-qt-action` fetches through
aqtinstall, whose newest release cannot read the repository layout Qt uses for
6.11, so the pin's ceiling is the installer rather than Qt. 6.11.2 was pushed
first and the job died before it compiled a line. `ci.yml` carries the two
commands that tell the difference.

So the gap is narrowed and not closed: the download runs 6.10, the desk runs 6.11.
Linux and macOS still build against 6.8.3, which is a version that can no longer
receive a fix — [#82](https://github.com/S-poony/Animage/issues/82).

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

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH   # MSYS2 UCRT64, from PowerShell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/bench_composite     # timings, not a test -- including a whole CTG solve
./build/tests/bench_zoom -platform offscreen [dir]   # the whole display path
./build/tests/bench_session -platform offscreen [--project FOLDER]   # does a session get heavy?
./build/tests/bench_save          # save, incremental save, open
./build/tests/bench_carry         # how far a mark survives being carried
./build/tests/bench_shapes        # what an estimator does to a family of shapes
./build/tests/bench_hand -platform offscreen [--project FOLDER] [--pictures DIR]  # against a hand
./build/tests/bench_transform     # what moving a drawing -- or a whole layer -- costs, and what it costs the history
./build/tests/bench_playback -platform offscreen   # what playback drops, coloured and not
./build/tests/bench_import -platform offscreen [dir]   # what one imported frame costs to decode
./build/tests/shots [--list] [name]   # pictures of the interface, one per situation
./build/tests/dock_probe [--bench]    # plain Qt with docks: is a panel fault Qt's?
./build/tests/window_probe            # the same readings from the real window: is it ours?
./build/tests/carry_probe FOLDER -platform offscreen   # why did the colour not reach that drawing?
```

`carry_probe` is for one report and it is a common one: "I scribbled the first
drawing and the colour does not reach that one." It walks a project drawing by
drawing through a real `CanvasWidget`, so the ladder and the budgets are the
ones a person gets, and prints per drawing which drawing the marks came from,
what the drawing's ink and the marks' bounds are, whether those two overlap at
all, and what each rung said on its own. The line to read first is
`ON THE DRAWING` against `ON BARE PAPER`; the rungs beside it say which of them
gave up, which is the whole question once you know one did.

`shots` is the one that is not a number. It drives the real window through a
list of named situations and writes a PNG each, into `build/shots/` unless told
otherwise; `--list` says what they are and a bare word runs only the ones whose
name matches, so looking at one thing costs one picture rather than thirty. It
runs offscreen without being asked to. **Add situations to it freely** — nothing
depends on any of them being there, which is the point. See
[looking at the interface](#looking-at-the-interface).

`bench_carry` is a measurement rather than a stopwatch, and it is the one to run
before changing anything about carrying marks. It moves a shape a known amount
per drawing, marks only the first, and reports what the fill did on the rest:
how much of the region took the colour, how much of the world outside it did,
what `spread` said about it, and how far the solve decided the drawing had moved. Every case runs twice, with the marks left where they were drawn and with
them following the line art, so the two are read side by side.

`bench_shapes` is the newest and the one that would have caught the most. Every
other fixture in `tests/` moves **one** shape — `bench_carry` moves a box,
`bench_hand` opens a shot somebody coloured — so between them they asked four
rungs the same question about a rectangle for a year, and none of them noticed
that rung four cannot follow a rectangle at all. This one moves fifteen shapes
by the same known amount and prints how far the worst cell of each answer is
from the truth, which is the number that decides whether a carried mark lands.

Its value is not the rows, it is the **differences between neighbouring rows**.
Three walls of a box against four; sixty pixels across against three hundred; a
cross through the middle or nothing there; the same shape turned 45°. Each pair
differs in one property, so a failure says what it is a property *of* — which is
the thing no single fixture can tell you, and which took ten minutes here after
a week of not knowing. Add shapes to it freely, in pairs that differ in one
thing.

`bench_hand` is the other measurement of carrying and it asks the question
`bench_carry` cannot: not "how far does a mark survive being moved a known
amount", but "does this agree with somebody who coloured the shot". Every rung
is scored against the same hand on the same drawings, so what it reports is a
difference between rungs rather than a level. Read
[scoring a rung against a hand](#scoring-a-rung-against-a-hand) before reading
its numbers — particularly the part about the marks and the rung being one
thing, which is what stops the table meaning what it looks like it means. With
`--pictures DIR` it writes the fills out with the line art drawn over them,
which is the only form in which the question it is asking can be judged by eye.

**And the instrument it could not be:** which rung a colourist would rather have
is a question about which failure they would rather find, and rungs three and
four fail on different drawings. No benchmark answers that, so for a while the
program could be run on one rung or the other from two batch files beside
`run-animage.bat`, reading `ANIMAGE_CARRY` once at startup. The shot was
coloured four times over, the person who coloured it reported the two a wash on
speed and preferred rung four's failure — an uncoloured region over a
confidently wrong one — and with that answered the scaffolding came out.
`run-animage.bat` runs what ships.

`bench_save` reports a full save, a full re-save, an incremental save with
nothing changed and one with a single drawing touched. The last is what autosave
actually costs and is the number to watch: if it starts tracking the size of the
shot rather than the size of the change, something has stopped carrying files
forward.

`bench_session` is the one that asks whether the program gets heavier the longer
it is used, which is a question none of the others can ask: they all measure a
document that was built a moment ago. It draws thousands of strokes, one command
each, and re-times the same pan drag as it goes, then drops the history and
times it once more — which is what closing and reopening a project does. Give it
`--project` and it does all of that on a real folder rather than a synthetic
scene, and `--onion`, `--lasso`, `--transforms` and `--zoom` to hold one of the
other things a session accumulates while the pan is timed. It is the bench to
reach for when something is slow *now* and was not an hour ago.

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

**And the colour cache holds about half a shot.** It used to hold a good deal
less: a fill was a picture of the canvas at full resolution, the budget worked
out at about 2000 tiles, and 62 of 192 fills survived four tracks of 48 against
**20 of 48** at 4K. A fill is now the labelling rather than a picture of it —
about a quarter of the memory — so on the same budget the four-track shot keeps
127 of 192 and the 4K one 40 of 48. What playback shows is still whichever fills
are there, and the shortfall is still real. The solves it provokes are counted
rather than timed. Worth knowing before item 4 is read as the answer to it: the
max-flow is staying on the CPU, so a GPU compositor does not touch this.

The numbers, run by run, are in
[the colour benchmarks](colour-baseline.md), which is where the plan below
gates itself.

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
| Rung three, and rung four beside it | "colour through time", part three |
| Freeing emptied tiles | "a rectangle built from tile coordinates remembers what you erased" |
| Lasso and transform, all four phases | "moving a drawing" through "what a transform costs" |
| The shortcut table, and all of #14 | "what the keyboard does, and when" |
| One place deciding the pointer (#27), the eraser (#4), the resize ring (#5) | "what the pointer says" |
| A screenshot target (#28) | "looking at the interface" |
| Capping the undo history (#23) | "what the history is allowed to cost" |
| Importing a single image, and placing it | "importing a picture" |
| Importing an image sequence | "importing a sequence" |

1. **The rest of importing**, which is the thing in flight and the only entry
   here with a design note of its own: [importing.md](importing.md) settles the
   shape, and "importing a picture" and "importing a sequence" above record
   what is built.

   **Audio is next, and that is a decision rather than the order falling out.**
   This list used to run video-then-audio, because a video is the sequence with
   one thing added and building it second means building nothing twice. That
   argument is about the *pictures*, and audio shares none of that machinery —
   so the tie was broken the way [importing.md](importing.md) breaks it, on what
   the shot is for: a lipsync shot is the thing the note is written around, and
   video is reference for it.

   - **Making the sound audible**, which is all that is left of audio. The
     model, the sync arithmetic, the import, the format, the row and moving and
     cropping the sound in it are built — see [importing a
     soundtrack](#importing-a-soundtrack) — and its deployment spike is done and
     cost none of what it was feared to ([what taking Qt Multimedia
     costs](#what-taking-qt-multimedia-costs)).

     **`AudioDevice` is built**, and so is `renderAudio`, which is what a
     device callback calls — see [the device is a seam, and what goes through
     it is a value](#the-device-is-a-seam-and-what-goes-through-it-is-a-value).
     The spike came out with it.

     What is left is **the scrub**: on each frame change while dragging, play
     about one frame's worth from that position. Everything it reads from
     exists — `sampleForSlot` answers where in the file to start, trim and
     fractional offset included, and `renderAudio` does the reading — so what
     it needs is the part that decides when a burst happens and holds a device
     open while somebody is working.

     After that, **synchronised playback** is one line —
     `slotForPlayedFrames` in place of the wall clock in `onPlaybackTick`.
     Both halves of that line are built and tested. What is missing is a device
     that is running, to ask for the sample count.

     One thing to know before starting: `processedUSecs()` counts audio
     **played out** of the device, measured, so `AudioDevice::playedFrames()`
     uses it as it comes with nothing subtracted.
   - **A video**, which is a sequence with a decoder in front and no new
     storage: extracted to frames once, at import, so the decoder never reaches
     the paint path. The one open measurement in the whole note belongs to it —
     whether `QMediaPlayer` at 1× extracts every frame — and it is the only
     question left whose answer could change what gets built.
   - **Convert to drawings**, the way back from a reference layer, without which
     imported line art cannot be coloured at all.

     **It expires a colour, and this is where that is written down.** A
     reference layer's row in the layer panel is drawn in the theme's disabled
     grey — see `applyLayerFlag` — and what that grey says is "nothing here can
     be acted on", which is exactly true today and stops being true the day this
     lands. Revisit it then rather than inheriting it.

2. **TIFF export**, which is the half of the format list still missing. It is
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

3. **Rung four is the default and the queue moved on.** What it took is in
   "colour through time, part three" and "what the push step is allowed to see";
   the defect that held it up was [#69](https://github.com/S-poony/Animage/issues/69)
   and it is fixed. Two things it left behind, both reported from use rather
   than found by a bench:
   [#72](https://github.com/S-poony/Animage/issues/72), a mark that stops
   following a shape which moved further than the search window can see, and
   [#73](https://github.com/S-poony/Animage/issues/73), dropping a carried mark
   that lands where it fills nothing instead of carrying the mistake on. #72 is
   the one that limits what the rung can do; #73 is the one that would make its
   failures cheaper to live with.

   Rung three's own score is the same mistake one rung down and is
   [#68](https://github.com/S-poony/Animage/issues/68). Two fixes were measured
   and both were worse; see "why a sum of products is not a score for a block".
   It matters less the moment rung four is the default, which is a reason to
   settle that first rather than to do them together.

4. **A flag that means something.** There was one, built on `spread`, and it came
   out — see "the flag that had to come out". Anything that replaces it has to
   clear a bar the old one did not: "wrong" only exists by reference to the
   drawing a mark came from, so it needs a correspondence between regions on two
   drawings. **Rung three now produces exactly that** — `markRegions` cuts the
   source drawing into the pieces the marks own, and each piece is told where it
   went — so the thing this was waiting for exists. It still has to be computed
   for drawings nobody has opened, which is what the audit did and what
   `CtgSolver`'s second priority is still there for.
5. **GPU compositing**, if `bench_playback` says it is worth it — not
   `bench_composite`, which watches the half that is not the problem. What it
   says today is that HD is comfortable at any track count and 4K drops between
   a quarter and two fifths of its frames, so this is a 4K deliverable and not a
   general one. It does not answer the coloured case at all: the max-flow stays
   on the CPU, and what breaks there is the fill cache, not the compositing.
6. **The rest of the open issues.** Transforming a layer across time
   ([#25](https://github.com/S-poony/Animage/issues/25)) is done, and it did
   *not* want `LayerPass` widened from an offset to an affine, which is what
   this entry used to say it needed. It bakes instead — see
   [transforming a layer through time](#transforming-a-layer-through-time) —
   so `compositeScene` is still a flat list of untransformed grids and the
   widening is still nobody's job. What it left behind is #65's fourth bullet,
   which it made worse before making it better.

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
