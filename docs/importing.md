# Importing

Written before anything is built, in the register of
[scribbles-through-time.md](scribbles-through-time.md): what is being decided,
why, and what would change each decision. Several things here are **not
settled**, and they are marked. The unsettled ones are collected again at the
end so nobody has to hunt for them.

The French documents in [fr/](fr/) are still the specification. This note fills
in something the specification reserved a place for and deferred:
[modele-de-donnees.md](fr/modele-de-donnees.md) has `Scene { audio_tracks:
[AudioTrack] }`, and [plan-de-prototype.md](fr/plan-de-prototype.md) puts `son`
out of the prototype's scope. That reservation turns out to have been the right
call twice over, and this note leans on it.

| | |
|---|---|
| [What it is for](#what-it-is-for) | the shot somebody is trying to make |
| [The order of work](#the-order-of-work) | audio, then a still, then a sequence |
| [Audio is not a track](#audio-is-not-a-track) | and the specification already said so |
| [Scrubbing comes first](#scrubbing-comes-first) | and it dodges the hard part entirely |
| [The playback clock](#the-playback-clock) | four ways two clocks come apart, worst first |
| [**Which audio library**](#which-audio-library-not-settled) | **not settled** — video is what decides it |
| [A single image](#a-single-image) | the modelsheet, and the cheap case |
| [Colours, which have to survive](#colours-which-have-to-survive) | the good news, and the real threat |
| [**Sequences: three shapes**](#sequences-three-shapes-not-settled) | **not settled** — and what a benchmark would settle |
| [What reference-only gives up](#what-reference-only-gives-up) | and the way back |
| [The menu, and what each dialog asks](#the-menu-and-what-each-dialog-asks) | |
| [Where an import lands](#where-an-import-lands) | bottom, in import order |
| [What the export says](#what-the-export-says) | a recap, which fixes something already broken |
| [The project folder and the file format](#the-project-folder-and-the-file-format) | |
| [Not in scope](#not-in-scope) | |
| [The open questions](#the-open-questions) | collected |

## What it is for

One shot, described plainly, because every decision below is answerable from it.

An animator has a soundtrack from a director and needs to make a **lipsync
shot**. They import the audio, drag the playhead back and forth over a syllable
until they find the frame the consonant lands on, and animate against it. Beside
that they have a **modelsheet** — one scanned or exported image, kept visible
while they draw, so the character stays on model. Sometimes they have a
**sequence** of images as reference: an animatic, a rendered background pass,
somebody else's pencil test.

Three things follow from that and they are worth stating before anything else:

- **Audio quality is not a goal.** The program cannot export video and has no
  muxer, so imported audio is a reference to animate against and never a
  deliverable. Nothing here should be traded for fidelity.
- **Frame accuracy is the goal.** The whole of lipsync is deciding which frame a
  sound is on. An error that is constant and small still ruins it, because it
  biases every judgement in the same direction.
- **Imports are reference.** This is the user's call and it is what makes most of
  the cheap options available. See [what reference-only gives
  up](#what-reference-only-gives-up), which is not nothing.

## The order of work

1. **Audio**, because it is what the shot is for.
2. **A single image**, because a modelsheet is one drawing and one drawing is
   cheap however it is stored.
3. **An image sequence**, because it is the only one where the storage question
   is real, and it should be decided by a benchmark rather than by argument.

Nothing in 1 blocks 2, and 3 is deliberately last.

## Audio is not a track

An `AudioTrack` is its own list on the `Scene`, exactly as
[modele-de-donnees.md](fr/modele-de-donnees.md) has it. It is **not** a `Track`
with a kind flag.

The specification is authoritative, so that would be enough. The code makes it
sharper:

- `Track` carries layers, slots, an image map, drawing numbers,
  `overwrite_drawings`, `TrackEnd`, `blend`, `celSourceFor`, `nearestWithCel`.
  Audio answers "not applicable" to every one of them.
- About twenty places walk `scene.tracks` — the compositor, the canvas twice,
  the export three times, `project_io`, `command.cpp`, `scene.h` itself, the
  timeline. A `TrackKind` puts a guard in all of them, and this codebase's own
  recorded lesson is that policy spread over call sites rots. Look at how hard
  [track.h](../src/core/track.h) works to keep layer-kind policy in one
  function: *"Nothing here knows about layer kinds... see
  Document::ctgScribblesAt, which is the only one that should."*
- A second list is empty in every project that exists, and every loop that
  exists keeps meaning exactly what it means today.

**The interface unifies what the model separates.** An audio track is a row in
the timeline, under every drawing row, because it has no compositing order and
letting it be dragged into the middle of the stack would imply a depth it does
not have.

**There is no fake layer.** An audio track has no layers, and giving it a
one-row layer list so that a volume slider has somewhere to live would mean
every `currentLayer()` call site handling a layer that is not one. The panel
*area* is shared and the content differs — the same shape as the colour-layer
box that already appears under the list when a CTG layer is selected.

**Volume is a horizontal bar across the row, dragged up and down.** Its height
in the row *is* the level, and the waveform is scaled to it, so the row shows
what you will hear rather than describing it. At the bottom it is silent, so no
separate mute is needed.

The axis is free, which is worth noticing rather than discovering later. In a
drawing row a horizontal drag moves a drawing and a vertical drag in the gutter
restacks the track; an audio row has no cards to pick up, so a vertical drag
inside it collides with nothing.

That bar must be **painted by `TimelineWidget::paintEvent` and hit-tested in
`mousePressEvent`** — not a child `QSlider`. The handover records the trap from
two directions already: a widget placed on a row disables that row's own hit
testing, which is why track names are a gutter rather than a control per row,
and the layer panel hit the same thing. A real slider there would work, and
everything else in that row would stop responding.

### The two selections

`MainWindow::track_` is the `TrackId` of the track being edited. Five things
read it: the canvas (which drawing the brush writes into), the layer panel
(whose layers are listed), the Track menu, the drawing buttons, and the status
bar. Today, clicking **any** timeline row emits `trackChanged` →
`setCurrentTrack(id)` and all five re-point.

An audio row clicked through that path hands over an id that is not a `Track`.
`findTrack` returns null and the canvas, the panel and the menus have nothing to
point at — the brush stops working, and nothing says why.

So `TimelineWidget` needs **two selections where it has one**:

| | |
|---|---|
| which row is highlighted | may be an audio row; drives the properties panel |
| which drawing track is current | only ever a real `Track`; unchanged by clicking audio |

Clicking the audio row shows its gain bar and leaves the brush exactly where it
was. This is small, and it is the kind of thing that is very annoying to
retrofit once four widgets assume one selection.

## Scrubbing comes first

Reading a track means dragging the playhead back and forth across a syllable
until you can hear which frame the consonant is on. That is **scrub audio**: on
each frame change while dragging, play about one frame's worth of samples from
that position.

It is the higher-value half of audio import for this program's stated purpose,
and it has a property that decides the whole build order: **scrubbing has no
clock problem at all.** There is no second timebase to drift against and no
reference to compare with, so output latency of a few tens of milliseconds is
imperceptible — you drag, you hear roughly what is there.

**So the first cut is: import → store → decode to PCM in RAM → waveform →
scrub.** It ships the thing that matters most and contains none of the
synchronisation work below. Synchronised playback is then a separable second
piece that can be argued and measured on its own.

It also fixes the shape of the audio layer: scrubbing wants the decoded samples
resident and a device you can push arbitrary samples into on demand. That is a
callback-driven device, not a media-player object with a play position.

Memory is not a consideration here. 48 kHz stereo is 192 KB/s as 16-bit and
384 KB/s as float; a ten-second shot is single-digit megabytes, against 17 MB for
one HD image frame.

## The playback clock

This is the trap, and it is worth writing out because the reason usually given
for it is the smallest of the four.

`onPlaybackTick` derives the picture's position from one thing:

```cpp
const qint64 elapsed = playback_clock_.elapsed();
const std::size_t slot = (playback_start_slot_ + elapsed * fps / 1000) % count;
```

A pure function of the system monotonic clock. Add the sound card's clock beside
it and there are four ways they come apart.

**1. Fixed output latency. Tens of milliseconds, always present, and the one
that ruins lipsync.** Samples handed to a device are not heard when they are
handed over: they sit in the ring buffer, then the OS mixer's, then the
hardware. Shared-mode WASAPI is typically 10–30 ms, PulseAudio 20–50 and
sometimes worse, CoreAudio around 10. Start the wall clock and the audio on the
same line and **the picture runs ahead of the sound by that whole amount,
permanently.**

One frame at 24 fps is 41.7 ms, so a 30 ms buffer is 0.7 of a frame and a 60 ms
one is 1.4. That direction — sound lagging picture — is the better-tolerated one
perceptually, but tolerance is the wrong question. The question is whether the
mouth opens on the right frame, and a *systematic* offset biases every placement
the same way. The shot would read correctly here and be a frame and a half late
everywhere else.

**2. Loop seams.** The slot loops by `% count` and the clock never restarts.
Audio has to be wrapped or restarted each time round. Restart it and the buffer
re-primes, so the offset calibrated on the first pass is a different offset on
the second. Wrap it sample-accurately and the seam is right, but the picture
wrapped at a slot boundary that was not the same instant, so the two walk apart
by a fraction of a frame per loop — a frame out after forty passes of a
three-second shot, which is an ordinary amount of looping for lipsync.

**3. Interface stalls, which already exist.**
[playback-resolution.md](playback-resolution.md) measured 4K dropping between a
quarter and two fifths of its frames. A stall does not only drop paints: the 1 ms
tick does not fire, so `setCurrentSlot` is not called. The wall clock is still
right, so the picture jumps and catches up — the good property the code already
has. Audio, fed from its own callback, does not stall at all. Picture and sound
survive a stall **only if both are anchored outside the interface thread**, which
today they are.

**4. Crystal drift, and for this program it is negligible.** Sound cards differ
from the system clock by tens of ppm. 100 ppm over a ten-second shot is 1 ms —
0.024 of a frame. Over a five-minute looping session it is 30 ms, still under a
frame. This is the reason textbooks give for "audio must be the master clock",
and here it is the least of the four.

**The conclusion is narrower and more useful than the slogan.** Derive the slot
from a position the audio device reports — not because of drift, but because it
makes 1, 2 and 3 come out right *by construction* rather than as three
separately-discovered corrections. The change is one line:

```cpp
const qint64 elapsed = audio_ ? audio_->playedMs() : playback_clock_.elapsed();
```

where `playedMs()` is samples consumed ÷ rate, minus the buffer still in flight.
With no audio in the scene, nothing changes at all.

**Two things follow, and both should be built in from the start.**

*A manual sync offset, in milliseconds.* Whatever survives the subtraction is a
constant per machine and per driver, and the honest way to remove it is to let
the user null it out. Note this is **not** the same field as an audio track's
placement offset, which is in frames and belongs to the shot: the calibration
describes the machine, so it is a preference and does not go in `scene.json`.
Two fields, because they are two different kinds of thing and a project carried
to another computer must not carry the first one with it.

*The arithmetic must be a pure function of "samples played".* GitHub's runners
have no audio device — anything opening an output there fails or hangs. So the
slot calculation takes a sample count as an argument and a test drives it with a
fake, pinning the loop seam and the stall case with no hardware at all. The
precedent is `exporting::Solve`: the thing that needs a resource is passed in, so
the logic can be tested without it.

## Which audio library (not settled)

**This is genuinely open, and video is what decides it.**

### What was checked

Qt Multimedia is **not installed anywhere in this project's world today**.
Locally, `C:\msys64\ucrt64\lib\cmake` has `Qt6Core`, `Qt6Gui`, `Qt6Widgets`,
`Qt6Network` and the Labs QML modules, and no `Qt6Multimedia`. In
[ci.yml](../.github/workflows/ci.yml), `jurplel/install-qt-action@v4` is called
with a version and no `modules:`, which installs qtbase only.

So taking Qt Multimedia costs: a `pacman -S
mingw-w64-ucrt-x86_64-qt6-multimedia` locally; `modules: qtmultimedia` on **four**
Qt install steps (three build platforms and the sanitize job); and three
deployment tools that must now find and bundle a backend —
`linuxdeploy-plugin-qt`, `macdeployqt`, `windeployqt` — for a module whose
default backend is FFmpeg, shipped as a plugin with its own bundled shared
libraries.

That last line is the zlib story again, and the handover already wrote the
lesson: *"X is a dependency of Y, so X is present" confuses being linked into
with being available to link against, and a bundled library is exactly the case
where those come apart.*

### The case for vendoring miniaudio

`third_party/` already holds tinyexr and miniz — two single-file libraries
vendored with a written argument, precisely to avoid per-platform install steps
and per-platform bytes. miniaudio is the same shape: one header, public domain /
MIT-0 and so GPL-compatible, speaking WASAPI, CoreAudio, ALSA and PulseAudio
with no system dependency, with WAV, FLAC and MP3 decoders built in.

Against Qt Multimedia that is: no new Qt module, no change to four CI steps, no
new burden on three deployment tools, no bundled FFmpeg, MP3 without a
backend-dependent codec, and a data callback that is exactly where `playedMs()`
and scrub audio both want to live.

### The case for Qt Multimedia, which is video

**Importing a video is something people will want**, and it is the branch where
vendoring an audio-only library is the wrong call. A live-action reference for a
character animator is an ordinary request; so is dropping in an animatic that
arrived as an `.mp4` rather than as frames. Video needs a media framework, and
the realistic ones are Qt Multimedia or FFmpeg directly. Take Qt Multimedia for
video and the audio module comes with it, at which point miniaudio was a
detour.

**The sub-question that changes the answer, and which is also not settled:**
does "import a video" mean *decode it to frames once, at import* — in which case
it is a decoder problem and lands in whatever storage shape sequences use — or
*play it live as a layer*, which is a player problem and settles the question in
Qt Multimedia's favour immediately? These are very different features that share
a menu item.

### What is settled about it

Two things, whichever way it goes.

**The decoder and the device are separable, and only the device is awkward to
replace.** A hybrid is legitimate: decode through one thing, play through
another.

**The seam matters more than the choice.** What is needed from an audio device
is small — open at rate R, receive a callback asking for N frames, report frames
consumed, stop. Behind about fifty lines of an `AudioDevice` header, swapping one
implementation for the other is a day's work. The version of this decision that
could be regretted is the one where the library's types reach into `MainWindow`
and `TimelineWidget`. Since the sync arithmetic has to be a pure function anyway,
the seam is being asked for regardless.

**What would change it, other than video:** a codec. miniaudio does not decode
AAC/M4A or Opus, and a director sending an `.m4a` off a phone is an ordinary
Tuesday. That is a gap rather than obsolescence — the answers are to explain and
ask for a WAV, to hook up a decoder, or to decode through Qt while playing
through miniaudio.

**What would *not* change it:** the platform APIs, which are the safe part.
WASAPI has been the Windows audio API since Vista and has never been deprecated;
CoreAudio's output path has been stable for twenty years. Linux is where the
churn lives — OSS → ALSA → PulseAudio → PipeWire — but PipeWire ships
`pipewire-pulse` and `pipewire-alsa` as the compatibility path, and the failure
mode if that eroded is worse latency, not silence. Nor does maintenance: the
library would be public domain and sitting in the tree, so "abandoned" means
"ours", over a surface small enough to own.

## A single image

**Settled, and it is the cheap case.** An imported still is an ordinary `Track`
with one ordinary `Layer` and one `Image`, its cel holding the decoded pixels.

- `TrackEnd::HoldLast` is **required**, or the modelsheet appears on frame 0 and
  nowhere else. It already exists.
- The layer is **locked** by default — a property, not a kind, and
  `beginStroke` already refuses on it — and can be unlocked.
- Layer opacity already works, which is the whole of what a reference needs.
- Placement is the transform box, validated by the user, as with any other
  transform. That means this feature waits on transforming a layer across time
  ([#25](https://github.com/S-poony/Animage/issues/25)); before then, place at
  1:1 with the top-left at the origin, since the canvas is the only rectangle in
  the model and drawing outside it is already allowed.

  > **Update: #25 is built, and it has one clause this has to answer.** The box
  > is "Transform layer through time" in the layer panel, and it is green rather
  > than blue — see "transforming a layer through time" in handover.md. But it
  > **refuses a locked layer**, and the line above says an imported modelsheet
  > lands locked. So placing an import is either unlock, place, relock, or an
  > exception argued for here. It also *bakes* rather than storing a transform,
  > which for an import is a different question than it was for drawn work: what
  > is being placed came off disk at a known size, and resampling it into cels on
  > placement is a decision this note should make rather than inherit.

**The cost, once:** a tile is 128×128 RGBA half = exactly 128 KB. A 300 dpi A4
scan (2480×3508) is 20×28 = 560 tiles = **70 MB**. An HD still is 15×9 = 135
tiles = **17 MB**. That is fine for a thing you import one of.

**A visible modelsheet is exported**, and that is accepted rather than
overlooked — see [what the export says](#what-the-export-says). Hiding the layer
keeps it out, because hidden layers are not written. Note that hiding removes the
compositing cost and **not** the memory.

An "import at half size" option is worth having and should be named that rather
than "compress", because that is what it does: a quarter of the tiles, and right
for a reference you look at rather than export.

## Colours, which have to survive

An artist importing a palette image and eyedropping from it is a stated use, so
this has to be right.

**The good news: an 8-bit sRGB import is exactly lossless.** Half spends its
precision relatively — finely near zero, coarsely near one — which is the whole
argument in [why-our-own-formats.md](why-our-own-formats.md) for why half beats
16-bit integer. All 256 sRGB values land on distinct halves and come back to the
same integers. `srgbToLinear` in [color.h](../src/core/color.h) is already the
function. Nothing has to be traded here; exactness is free.

**The real threat is ICC profiles.** A palette exported from Photoshop or
Procreate is often Display P3 or Adobe RGB. Assume sRGB and the numbers get
reinterpreted against the wrong primaries — every swatch arrives a different
colour than the artist chose, silently, which is the worst way for this to be
wrong. So: read `QImage::colorSpace()`, and when it is not sRGB convert to
`QImage::Format_RGBA64` **first** — converting an 8-bit image in place bands —
then `convertToColorSpace(QColorSpace::SRgb)`, then linearise into half.

Two smaller notes. A PNG with alpha is premultiplied on the way in, so
eyedropping a half-transparent swatch gives a darker colour than the swatch
looks; opaque images, which palettes and modelsheets are, are unaffected. And
JPEG's chroma subsampling happened before the file reached us.

**A round-trip test is cheap and worth pinning:** export a frame to PNG, import
it back, compare within the known quantisation. It fails loudly if anyone
touches the conversion.

## Sequences: three shapes (not settled)

**This is the other genuinely open decision, and a benchmark should settle it
rather than an argument.**

Two facts frame it. A sequence is the only import where the numbers are real: an
HD frame is 17 MB of tiles, so 100 frames is **1.7 GB** resident, and 4K is 68 MB
a frame. And **the first save after an import is a first-class cost, not a
footnote** — `bench_save` measured 3047 ms for 96 drawings, but those are *line
art*: 14 thin strokes and 4 scribbles per drawing, so `encodeCel` keeps only
narrow row spans and throws most of every tile away. An imported frame has every
span full. The ratio is large and unmeasured, and it is the first number
`bench_import` should report.

### Shape 1 — ordinary cels

What a drawn frame is. Everything works: brush, transform, CTG over the top,
onion skin, export, undo.

*Costs:* 17 MB per HD frame resident, permanently, and a full encode of every
frame on the first save.

### Shape 2 — derived cels, with the file as the truth

The source file lives in the project folder; the tiles are built from it and are
a **bounded cache that can be dropped and rebuilt**. The save writes the source
file, not the cels.

This is not a new idea in this codebase — it is `CtgFillCache` exactly: derived
data, bounded, kept in `Document` rather than on the `Cel` precisely because
losing it costs a rebuild and nothing else. And "what happens when you draw on
it" already has a worked answer here: the first mark on an inheriting CTG
drawing copies what it was showing and edits the copy. Same move — the first
stroke promotes a derived cel to a real one, and from then on it saves normally.

*Buys:* the save cost collapses to copying a file. Resident memory becomes
whatever the cache is allowed to be, rather than the whole sequence. Drawing and
colouring stay possible.

*Costs:* eviction and rebuild machinery; a wrinkle where a rebuilt cel needs a
stable `revision()`, since the CTG input hash is keyed on it; and the source
files sit in the project folder alongside whatever cels have been promoted.

### Shape 3 — a reference layer, with no cel at all

The compositor reads decoded pixels — or the compressed bytes, decoded on demand
— and there is no `Cel`. Cheapest of the three in both memory and save time: a
2 MB JPEG stays 2 MB rather than becoming 17 MB of tiles, and a save is a file
copy.

*What it does not cost, having been checked rather than assumed.* An earlier
draft of this note said a new kind "splits every path that touches pixels — the
compositor, the eyedropper, the transform's `liftThrough`, `paintedBounds`,
`celForWriting`". That was wrong, in both directions:

- **The eyedropper is free.** `CanvasWidget::pickColourAt` composites a 1×1
  rectangle through `compositeScene` and reads the pixel back, across every
  track. It samples the *picture*, so it works on anything the compositor can
  draw, with no new code at all.
- **`refuseHere` already branches on the layer kind** — `if (layer->kind ==
  LayerKind::Ctg) return Refusal::ColourLayer;` — and it is the single list that
  copy, cut, paste, transform and the brush all consult. A reference kind is one
  more line in a function that exists for precisely this.
- **And the paths behind that gate are never reached.** `liftForTransform`,
  `paintedBounds` and `celForWriting` sit downstream of the refusal, so they need
  nothing.

*What it actually costs:* **one new path in the compositor**, which is real and
is the substantial one. A decision about what a per-layer export writes for a
layer with no cels — the source frames, nothing, or a render. And a decode per
frame per paint if the bytes are kept compressed, which is affordable for JPEG
and not for PNG.

*And what it forecloses* is the subject of the next section, which is the honest
cost and is not an implementation one.

### Where this stands

**On build cost alone, shape 3 is plausibly the cheapest of the three, and this
note should not pretend otherwise.** Shape 2 needs eviction, rebuild, a stable
`revision()` for a cel that can be rebuilt, and a promotion state machine. Shape
3 needs none of those: there is no cache to bound, nothing to invalidate, and no
second state a cel can be in. Set against that, its compositor path is one
function.

The argument for shape 2 is **optionality, not economy** — it keeps drawing,
colouring and per-frame transforms possible on an imported sequence. The
argument against it is that optionality is being paid for in machinery, and the
user has now said twice that a sequence is reference and does not need any of
it. In real work you position a reference once, with a layer-level transform
([#25](https://github.com/S-poony/Animage/issues/25), already on the queue), and
never touch its pixels again — which is a use case shape 3 serves completely.

So this is a real choice between "cheapest to build and closes a door" and
"more machinery and keeps it open", and it should be made deliberately rather
than defaulted into. What `bench_import` contributes is the size of the door:
full encode time per imported frame against a drawn one, resident tile bytes,
and paint cost at a realistic cache size. If shape 1's numbers turn out
tolerable, the door is cheap and shape 2 is not needed either.

**One decision is cheap and should be made now, whichever shape wins: keep the
source files in the project folder from day one, even while they are redundant.**
That is what preserves the option. Without it, moving from shape 1 to either of
the others is a migration; with it, it is a change to one class — and shapes 2
and 3 both need those files anyway, since under both of them the file is what
the picture is made from. The price is disk — a 100-frame
PNG sequence sitting beside the cels it produced — which for a single image is
nothing and for sequences is another number for the benchmark.

## What reference-only gives up

Stated plainly so that it is a decision and not a discovery.

**`ctg_sources` are resolved inside the track.** [ctg.cpp](../src/core/ctg.cpp)
looks each one up with `line->findLayer(source)` and reads it with
`record->celFor(source)` against an `Image` in that same track. So a CTG barrier
must be a cel-bearing layer in the same track as the colour layer.

Two consequences:

- A **reference layer with no cel cannot be a CTG barrier at all** (shape 3).
- Even with cels, an import that always lands in a **new track** cannot be a
  barrier for a colour layer in a different one.

Which together mean: **importing scanned line art and colouring it with
LazyBrush does not work.** For an application whose headline is a colour solver,
that is a real thing to give up, and it is worth being sure.

**The way back, if it is wanted, is a command rather than a mode.** "Convert to
drawings" bakes a reference track into ordinary cels — paying the memory and the
save cost, on demand, at the moment somebody actually wants to colour it. That
keeps the default cheap, keeps the door open, and asks nobody to decide at import
time. Under shape 2 it is nearly free to implement: it is the promotion that
already has to exist.

**Not settled:** whether an import should also be able to land *as a layer in an
existing track* rather than only as a new track. That is the other half of the
answer, and it is a small addition to the import dialog rather than a change to
the model.

## The menu, and what each dialog asks

Three items under **File ▸ Import**, arriving in this order:

- **Audio…**
- **Image…**
- **Image sequence…**

A still and a sequence are separate items because they arrive at different times
and do different things, not because the file picker cannot tell them apart.
Nothing for projects, PSDs or (yet) video.

**Drag and drop onto the timeline is how people will actually do this.** Not in
the first cut, but each import should be a function the drop handler can call.

### Image sequence

- **Order is numeric and not correctable.** `frame10.png` never goes before
  `frame9.png` and nobody has ever wanted it to. Files with no number, or two
  numbering schemes in one selection, are *said* rather than guessed at — the
  house rule is to let the input in and explain it, not to silently pick.
- **Where:** a new track.
- **Exposure:** on 1s. An image sequence has no frame rate of its own; inventing
  one is inventing information.
- **Size:** the transform box, validated by the user.
- **Import at half size:** offered, off by default.
- **The recap:** frame count and what it will cost, before it happens.

### Image

The same, minus order and exposure. One drawing, `TrackEnd::HoldLast`.

### Audio

The file, and a placement offset in frames. And one sentence the dialog has to
say, because it is otherwise found out much later: **audio is not exported.**

## Where an import lands

Settled, and it needs nothing stored.

**An import lands at the bottom of the stack**, in the order it was imported.
Audio rows sit below every drawing row, which is free: audio is a separate list
rather than a position in the stack, so there is nothing to enforce and nothing
that can put it in the wrong place.

**Stills and sequences are not ordered against each other.** That was considered
and dropped on the user's call, and dropping it is what keeps this cheap: any
rule of the form "a sequence goes above a still" needs a track to remember what
it was imported as, and this repository has already written down why to be wary
of such a field. The drawing-number counter was taken out of the struct *and* out
of `scene.json` on the grounds that *"a stored counter that no longer decides
anything is exactly the thing somebody re-wires by accident later"* — and what
would try to attach itself to an import-provenance field is easy to predict: do
not export imported tracks, lock imported tracks, relink, re-import.

So no track records where it came from, and the stack after several imports is
whatever the import order made it.

**One residual case, and dragging is the answer.** A drawing track created
*after* an import also lands at the bottom, so it ends up below that import.
Enforcing otherwise would need the same field. Restacking by dragging already
works, it is one gesture, and this only arises in a shot that mixes imports with
tracks made afterwards.

## What the export says

The export dialog gains a **recap of what is about to be written**: each
sequence, its frame count, its file count, and the total.

This is not only about a modelsheet ending up in the render. It fixes something
already broken and already documented — the handover records that **layer folders
can be different lengths**, `character_ink/` with ten frames beside
`background_ink/` with two, and says in as many words that *nothing in the export
announces it*. The recap is where that announcement belongs. Once imported
sequences exist it also answers "why is my export 4 GB" before the export rather
than after.

## The project folder and the file format

A project stays **a self-contained folder**. Nothing here introduces a reference
to a file outside it, because a project that breaks when it is moved would be the
first thing in this format that does.

```
the-shot.animage/
  scene.json
  cels/cel-000007.acel
  audio/dialogue.wav        the imported file, copied in
  imports/bg_0001.png       the imported sources, kept
```

**Decoded PCM is derived and is not stored** — decode at load; a ten-second file
is tens of milliseconds. Waveform peaks are derived too.

**`kSceneFormatVersion` goes to 2.** Not because an older build would misread an
`audio_tracks` key, but because it would *ignore* it and then autosave over the
project two minutes later without it. Silent data loss, and the version gate is
what stops it. That is exactly the standard [project_io.h](../src/app/animage/project_io.h)
sets: bump when old builds would get it wrong.

The incremental save works in favour of imports either way: an imported cel's
revision never changes, so after the first write it is carried forward as a hard
link and costs nothing.

## Not in scope

Video (see above — it is a question, not a plan), audio export or muxing, more
than one audio track, volume automation, waveform rendering in the first cut (a
labelled bar is enough to place a sound), track reading and phoneme breakdown,
and relinking a moved source file.

## The open questions

1. **Which audio library**, decided mainly by whether video import is on the
   road — and, if it is, by whether "import a video" means *decode to frames
   once* or *play it live as a layer*. Those are different features sharing a
   menu item.
2. **Which shape a sequence is stored in** — ordinary cels, derived cels with the
   file as the truth, or a reference layer with none. Shape 3 is probably the
   cheapest to build and closes a door; shape 2 keeps it open and pays in
   machinery. `bench_import` sizes the door; keeping the source files from day
   one preserves the option under any of the three.
3. **Whether giving up colouring imported line art is intended**, given that CTG
   barriers are per-track and a reference layer has no cel to be one. If not, the
   answers are "convert to drawings" as an explicit command, or letting an import
   land as a layer in an existing track.
