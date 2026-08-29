# Importing

Written before anything was built, in the register of
[scribbles-through-time.md](scribbles-through-time.md): what is being decided,
why, and what would change each decision. Anything still **not settled** is
marked, and the unsettled ones are collected again at the end so nobody has to
hunt for them.

The two large questions the first draft left open — which audio library, and
what shape a sequence is stored in — have both been answered, and the answer to
the first changed the answer to the second. They are settled in place below
rather than in an appendix, with what decided them.

> **Three of the four imports are built, and audio is done.** *(Since this was
> written: the sound is audible — scrubbing and synchronised playback both work
> — and the row has a waveform, which this note puts out of the first cut below.
> All three are recorded in the handover; this note is left as it was, being a
> plan.)* A
> single image imports and can be placed, so can a sequence, and a soundtrack
> imports, saves, and has a row it can be moved and cropped in — it does not yet
> make a noise. This note stays a plan and is not rewritten into a description:
> what was built is recorded in ["importing a
> picture"](handover.md#importing-a-picture), ["importing a
> sequence"](handover.md#importing-a-sequence) and ["importing a
> soundtrack"](handover.md#importing-a-soundtrack), which is where a reader who
> wants the code should start. Read this one for *why*, and those for *what*.
>
> What taking Qt Multimedia actually cost — measured before a line of audio was
> written, because this note said to — is [audio-spike.md](audio-spike.md).
>
> Three things this note predicted are worth knowing before trusting the rest of
> it, because they are the evidence its remaining predictions rest on. The
> compositor really did cost three lines: `collectPasses` is the one place that
> resolves a layer to pixels and it already had the branch shape. A stored
> placement really is free — a picture moved and scaled shows `tiles 0` and
> `undo 2 (0 MB)`, which is the claim under "placement is stored" made visible.
> And a sequence really was the still with three things added and nothing
> reshaped.
>
> **Two it got wrong**, both in the cheap direction. The still is a reference
> layer, not ordinary cels — fixed above. And the decode moving off the
> interface thread is described here as an optimisation a sequence forces; it is
> not, it is the only route by which a sequence can notice the playhead has
> moved at all, because a frame change never calls `refreshEverything`.
>
> **And one thing it said would need measuring turned out to matter more than
> the thing it was measured for.** `bench_import` exists now; the decode it
> found was four times slower than it needed to be, and that was our own tiling
> loop rather than anything about a file. See the section on benchmarking.
>
> **[Convert to drawings](#convert-to-drawings) is built, and two of the things
> this note says about it below are wrong.** They are left standing, because
> what a plan predicted is worth having beside what happened, and each is
> corrected in place where it is said. In short: converting costs the undo
> history *nothing*, not most of it — a bake displaces tiles and a conversion
> writes into cels that did not exist. And this note's line about the files
> ceasing to be read turned out to have a consequence it did not follow through:
> the save carried forward only files something still named, so converting and
> saving deleted the scan from the project, and undoing then left a project that
> would not save at all. Fixed on the user's call by keeping the files. Both are
> written up in [converting an import to
> drawings](handover.md#converting-an-import-to-drawings).

The French documents in [fr/](fr/) are still the specification. This note fills
in something the specification reserved a place for and deferred:
[modele-de-donnees.md](fr/modele-de-donnees.md) has `Scene { audio_tracks:
[AudioTrack] }`, and [plan-de-prototype.md](fr/plan-de-prototype.md) puts `son`
out of the prototype's scope. That reservation turns out to have been the right
call twice over, and this note leans on it.

| | |
|---|---|
| [What it is for](#what-it-is-for) | the shot somebody is trying to make |
| [The order of work](#the-order-of-work) | audio, then a still, then a sequence, then a video |
| [Audio is not a track](#audio-is-not-a-track) | and the specification already said so |
| [Scrubbing comes first](#scrubbing-comes-first) | and it dodges the hard part entirely |
| [The playback clock](#the-playback-clock) | four ways two clocks come apart, worst first |
| [Which library](#which-library) | Qt Multimedia, and what taking it costs |
| [Video export, and what Qt gives free](#video-export-and-what-qt-gives-free) | cheap, not free, and not before 6.8 |
| [A single image](#a-single-image) | the modelsheet, which is a reference layer too |
| [Colours, which have to survive](#colours-which-have-to-survive) | the good news, and the real threat |
| [How a sequence is stored](#how-a-sequence-is-stored) | a reference layer, and why the compositor needs nothing |
| [Video is a sequence with a decoder in front](#video-is-a-sequence-with-a-decoder-in-front) | extracted once, at import |
| [What reference-only gives up, and the way back](#what-reference-only-gives-up-and-the-way-back) | convert to drawings, whole layer, on a popup |
| [The menu, and what each dialog asks](#the-menu-and-what-each-dialog-asks) | |
| [When the frame rate changes](#when-the-frame-rate-changes) | both directions, and both warn |
| [Where an import lands](#where-an-import-lands) | bottom, in import order |
| [What the export says](#what-the-export-says) | a recap, which fixes something already broken |
| [The project folder and the file format](#the-project-folder-and-the-file-format) | |
| [**What was built that this note did not plan**](#what-was-built-that-this-note-did-not-plan) | the shot's length, and moving and cropping a sound |
| [**What the spike measured**](#what-the-spike-measured) | and the two things in this note it corrects |
| [**What the handover already knows about this**](#what-the-handover-already-knows-about-this) | the traps this note would otherwise walk into |
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

- **Audio quality is not a goal**, but the reason has changed and the old one
  should not be left standing. It used to be that the program cannot export
  video and has no muxer, so imported audio could never leave the program at
  all. Taking Qt Multimedia takes a muxer with it — see [video
  export](#video-export-and-what-qt-gives-free) — so that argument is gone. What
  is left is narrower and still enough: what would carry the audio out is a
  *preview* file, watched to check timing, and nothing about a preview is worth
  trading the scrub for.
- **Frame accuracy is the goal.** The whole of lipsync is deciding which frame a
  sound is on. An error that is constant and small still ruins it, because it
  biases every judgement in the same direction.
- **Imports are reference.** This is the user's call and it is what makes most of
  the cheap options available. See [what reference-only gives
  up](#what-reference-only-gives-up-and-the-way-back), which is not nothing.

## The order of work

1. ~~**Audio**~~ — **built as far as it can go without a device.** The model,
   the sync arithmetic, the import, the format, the row, and moving and cropping
   the sound in it. What is left is `AudioDevice` and the scrub. See
   ["importing a soundtrack"](handover.md#importing-a-soundtrack).
2. ~~**A single image**~~ — **built**, and it did build most of 3: the layer
   kind, the derive step, the cache, the placement, the format and the
   `imports/` folder all exist. See
   ["importing a picture"](handover.md#importing-a-picture).
3. ~~**An image sequence**~~ — **built**, and all three of the things one frame
   never needed are in: the per-image source frame map, a bound on what is
   resident, and a decode off the interface thread. The third was not an
   optimisation to reach for later and the reason turned out to be sharper than
   this note guessed — a frame change never calls `refreshEverything` at all, so
   there was no route by which a sequence could notice the playhead had moved.
   See ["importing a sequence"](handover.md#importing-a-sequence).
4. **A video**, which is 3 with a decoder in front of it and no new storage at
   all.
5. **Video export**, last and deliberately so. It is the only item here that
   writes a file for somebody else, and none of the other four is waiting on it.

Nothing in 1 blocks 2. The order of 2, 3 and 4 is not a preference: each is the
one before it with one thing added, so taking them out of order means building
the same machinery twice.

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

*A manual sync offset, in milliseconds — **deferred**, on the user's call.*
Whatever survives the subtraction is a constant per machine and per driver, and
the honest way to remove it is to let the user null it out. That is still true;
it is simply not worth a control until a machine turns out to need one.

Deferring it costs nothing structurally, and that is why it is safe to defer.
The calibration describes the *machine*, so it is a preference and does not go
in `scene.json` — which means adding it later touches no project file and is not
a format change. It is also **not** the same field as an audio track's placement
offset, which is in frames and belongs to the shot; that one is not deferred,
because a project carried to another computer has to carry it.

What deferring does cost: if `playedMs()` turns out to over- or under-report on
some driver, there is nothing the user can do about it but say so. That is
acceptable while the subtraction is honest, and the way to find out whether it
is honest is to measure it — see [which library](#which-library).

*The arithmetic must be a pure function of "samples played".* GitHub's runners
have no audio device — anything opening an output there fails or hangs. So the
slot calculation takes a sample count as an argument and a test drives it with a
fake, pinning the loop seam and the stall case with no hardware at all. The
precedent is `exporting::Solve`: the thing that needs a resource is passed in, so
the logic can be tested without it.

## Which library

**Settled: Qt Multimedia.** Video import is wanted, and that is what decides it —
exactly as the first draft of this note predicted it would. A live-action
reference for a character animator is an ordinary request, an animatic that
arrived as an `.mp4` is an ordinary Tuesday, and a storyboard mode editing
several scenes would want a player too. Take Qt Multimedia for any of those and
the audio module comes with it, at which point vendoring an audio-only library
was a detour.

The case for vendoring miniaudio was real and is recorded below rather than
deleted, because what it was weighing is what the cost of this decision still
is.

### What it buys beyond the decision

Three things, none of which was the reason and all of which are worth having.

- **`QAudioDecoder` closes the codec gap.** This was named as the one thing that
  would change the answer other than video: miniaudio does not decode AAC/M4A or
  Opus, and a director sending an `.m4a` off a phone is ordinary. Qt decodes all
  of them, so the "explain and ask for a WAV" conversation does not have to
  happen.
- **`QAudioSink` in pull mode is the shape [scrubbing](#scrubbing-comes-first)
  asked for** — a device that calls you for samples, rather than a media-player
  object with a play position.
- **`QMediaPlayer` is what a storyboard mode would want**, and it is the same
  module. That is not a reason to build one, but it is a reason not to have to
  revisit this.

### What it costs, and none of it has changed

- `pacman -S mingw-w64-ucrt-x86_64-qt6-multimedia` locally.
- `modules: qtmultimedia` on **four** Qt install steps in
  [ci.yml](../.github/workflows/ci.yml) — three build platforms and the sanitize
  job.
- Three deployment tools that must now find and bundle a backend:
  `linuxdeploy-plugin-qt`, `macdeployqt`, `windeployqt`. Qt Multimedia's default
  backend is FFmpeg, shipped as a plugin with its own bundled shared libraries.

That last line is the zlib story again, and the handover already wrote the
lesson: *"X is a dependency of Y, so X is present" confuses being linked into
with being available to link against, and a bundled library is exactly the case
where those come apart.*

**So the deployment spike comes before any audio code is written.** A
hello-world that opens a sink, built and packaged through all three tools on all
three platforms. If `windeployqt` does not bundle the FFmpeg plugin correctly,
that is a fact worth having on day one and a disaster to discover after the
audio layer exists. The precedent for how to ask is `dock_probe` — see [asking Qt
a question directly](handover.md#asking-qt-a-question-directly) — which exists
for exactly this class of question: is this ours or theirs, answered by the
smallest program that can tell.

### The one line in CMakeLists that can turn the application off

**`Multimedia` must not be added to the root `find_package(Qt6 COMPONENTS ...)`.**

This is not a style preference, it is a trap the handover already paid for: [what
asking for a private Qt component at the top level switches
off](handover.md#what-asking-for-a-private-qt-component-at-the-top-level-switches-off).
A component in that list which the installed Qt does not have fails the whole
`find_package`, `Qt6_FOUND` comes back false, and the `if(Qt6_FOUND)` around
`src/app` skips **every GUI target** — while configure, build and `ctest` all
report success.

Every Qt this project currently builds against is such an install. There is no
`Qt6Multimedia` in MSYS2 here today and none on the CI runners, so the first
commit that adds it to the root list would turn the application off on every
machine at once, and say so only in one status line in the middle of a
successful log. Ask for it as its own package, the way `Qt6GuiPrivate` is asked
for in `tests/`, and let a missing module disable the audio feature rather than
the program.

### The seam is still wanted, and now for one reason rather than two

It used to be insurance against picking the wrong library. That reason is gone;
the other one is not, and it was always the stronger of the two.

**The sync arithmetic must be a pure function of "samples played".** GitHub's
runners have no audio device, so anything opening an output there fails or
hangs. The slot calculation therefore takes a sample count as an argument and a
test drives it with a fake, pinning the loop seam and the stall case with no
hardware at all. The precedent is `exporting::Solve`: the thing that needs a
resource is passed in, so the logic can be tested without it.

That requirement alone produces the `AudioDevice` header — open at rate R,
receive a callback asking for N frames, report frames consumed, stop. Keeping
Qt's types out of `MainWindow` and `TimelineWidget` then costs nothing extra,
and it is the version of this decision that could otherwise be regretted.

**And there is one number to measure before trusting any of it.**
`QAudioSink::processedUSecs()` is the obvious source for `playedMs()`, and
whether it counts audio *handed to* the sink or audio *played out of* it decides
whether the buffer-in-flight subtraction is still needed. It is the number the
deferred [manual sync offset](#the-playback-clock) is standing on.

**Measure it, in the deployment spike, before writing the sync arithmetic.** Play
a file and ask whether the number ever reports more audio than there has been
time to play. It cannot, if it counts audio that has come out; it will, if it
counts audio handed over. Ten lines, once, and then it is known.

Neither answer is more likely than the other and this note does not guess at one,
because a guess written here is an invitation to skip the measurement. What is
worth knowing is what the measurement is *for*: getting this wrong does not fail,
it leans — the picture sits a fixed fraction of a frame away from the sound, on
every frame, invisibly. That is exactly the error [the playback
clock](#the-playback-clock) exists to remove, arriving through the one number
meant to remove it.

### What was weighed against it, kept because it is the cost

`third_party/` already holds tinyexr and miniz — two single-file libraries
vendored with a written argument, precisely to avoid per-platform install steps
and per-platform bytes. miniaudio is the same shape: one header, public domain /
MIT-0 and so GPL-compatible, speaking WASAPI, CoreAudio, ALSA and PulseAudio
with no system dependency, with WAV, FLAC and MP3 decoders built in.

Against Qt Multimedia that was: no new Qt module, no change to four CI steps, no
new burden on three deployment tools, no bundled FFmpeg, and a data callback
exactly where `playedMs()` and scrub audio both want to live. **Every one of
those is still true, and each one is now a cost this project has agreed to
pay.** They are listed here so that the bill is not a surprise, and so that a
future reader who finds video import abandoned knows what the audio layer could
go back to.

What would *not* have changed it either way: the platform APIs, which are the
safe part. WASAPI has been the Windows audio API since Vista and has never been
deprecated; CoreAudio's output path has been stable for twenty years. Linux is
where the churn lives — OSS → ALSA → PulseAudio → PipeWire — but PipeWire ships
`pipewire-pulse` and `pipewire-alsa` as the compatibility path, and the failure
mode if that eroded is worse latency, not silence.

## Video export, and what Qt gives free

**Not free, but cheap — and not before Qt 6.8.** It is not a priority: it comes
after import, and this section exists so that it is easy when it comes rather
than to argue for doing it now.

### What the API actually is

`QMediaRecorder` encodes and muxes, but its input is a `QMediaCaptureSession` —
which historically meant a camera, a microphone or a screen grab. There was no
supported way to hand it frames the program generated itself. **Qt 6.8 added
`QVideoFrameInput` and `QAudioBufferInput`**, which are exactly that: push a
`QVideoFrame` per composited frame with a start and end time in microseconds,
throttled by a `readyToSendVideoFrame` signal, and push the soundtrack the same
way.

So the new code is: composite each frame as the export already does, wrap the
`Framebuffer` as a `QVideoFrame`, push, handle the backpressure, wait for
finalisation. A couple of hundred lines and a dialog. The muxing — which is the
part that would otherwise mean FFmpeg by hand — comes with it.

### What it does not give

- **Encoder control.** `QMediaFormat` picks a container and a codec; quality is
  a five-step enum or an explicit bitrate. No CRF, no preset, no profile.
- **Anything but 8-bit 4:2:0** out of H.264/H.265 in practice. Chroma
  subsampling on flat colour with hard edges is close to the worst case for what
  this program draws: coloured line art will fringe. That is fine for a file
  somebody watches to check timing and wrong for anything called a master.
- **Certainty across platforms.** The FFmpeg backend ships as a plugin with its
  own bundled libraries, and which encoders are present varies. Check on all
  three rather than assuming.

**So it is the convenience deliverable**, the way TIFF is the compatibility one
and EXR is the lossless one — the handover's queue makes that distinction for
TIFF and it is worth making once here too, because it is what stops the same
argument happening twice. Everything downstream already takes an image sequence.

### Two constraints that have to be decided before it is built

**The Qt floor rises to 6.8, with no headroom under it.**
[CMakeLists.txt](../CMakeLists.txt) asks for 6.5 today, and `ci.yml` pins Linux
and macOS at `6.8.*` — which is the version `QVideoFrameInput` arrives in.
Video export therefore has a hard minimum sitting exactly on what CI installs.
Nothing to do about it now except know it: **do not lower the CI pin below 6.8,
and do not raise the root `find_package` minimum for any other reason without
noticing this.**

**Shipping an H.264 encoder is a decision, not a side effect.** Qt Multimedia
under GPLv3/LGPLv3 is fine for this project. The bundled FFmpeg — and, for
software H.264, OpenH264 — add third-party licence texts to ship from
`packaging/`, and the patent position around distributing an H.264 encoder is
something a GPL project should settle deliberately. Free-software projects ship
one routinely; that is a reason it is defensible, not a reason it is automatic.

### What to do now so it is easy later

Nothing in the import work needs changing for it. Two habits are enough:

- **The export already composites every frame at canvas size**, through
  `exporting::write`. A recorder is another `exporting::Format`-shaped decision
  over the same loop, not a second traversal of the scene. Keep it that way.
- **Audio can now leave the program**, which the [placement
  offset](#the-menu-and-what-each-dialog-asks) in frames is what positions. That
  is arithmetic on the shot and is unaffected by any device latency, so the
  deferred calibration never enters a muxed file. Worth writing down because the
  two offsets look alike and only one of them is ever a deliverable.

And one sentence the import dialog says today has to change when this ships:
**"audio is not exported"** stops being true. Until then it is true and it stays.

## A single image

**Settled, and it changed: a still is a reference layer too.** The first draft
made it an ordinary `Track` with an ordinary `Layer` and one `Image` whose cel
holds the decoded pixels, on the grounds that one drawing is cheap however it is
stored. That is still true about the memory and is no longer the deciding
question.

**What decided it is coherence, on the user's call.** Once a sequence is a
reference layer — see [how a sequence is stored](#how-a-sequence-is-stored) —
making the still the one thing that is not means answering *placement, locking,
export, save cost, colouring* and *what happens when you draw on it* twice, with
two different answers, for two features a user thinks of as one. A still is a
sequence of one frame, and there is no cheaper way to say that than to make it
one.

It costs one thing, and it is the cheap case of it: tracing over a modelsheet
means converting it to drawings first — one command, on one drawing. See [the
way back](#what-reference-only-gives-up-and-the-way-back).

So:

- `TrackEnd::HoldLast` is **required**, or the modelsheet appears on frame 0 and
  nowhere else. It already exists.
- **The layer is not locked, and does not need to be.** The first draft locked
  it, which was right when it was an ordinary raster layer whose only protection
  was the lock. A reference layer refuses the brush on its *kind* instead — which
  means a kind check has to be added to
  [`refuseToEditHere`](../src/app/animage/canvas_widget.cpp), which deliberately
  has none today. See [the traps](#what-the-handover-already-knows-about-this),
  because that is a design decision being reversed and it has a comment
  explaining it.

  **The new check goes ahead of the lock**, following the order `refuseHere`
  already uses and for the same reason it gives: *unlocking a colour layer would
  not make a cut work on it, so naming the lock would send somebody to fix the
  wrong thing.* Locking an import as well would be worse than redundant — the
  refusal reports the first reason it finds, so a locked reference layer answers
  *"this layer is locked"* to somebody who then unlocks it and is no better off.
  The kind is the true reason and it is the one that has to be said.
- Layer opacity already works, which is the whole of what a reference needs.
- **Placement is stored, not baked**, and that is the answer to the clause the
  first draft left open. It is set out under [how a sequence is
  stored](#how-a-sequence-is-stored), because it is the same mechanism for one
  frame and for two hundred.

**The cost, once:** a tile is 128×128 RGBA half = exactly 128 KB. A 300 dpi A4
scan (2480×3508) is 20×28 = 560 tiles = **70 MB**. An HD still is 15×9 = 135
tiles = **17 MB**. That is fine for a thing you import one of — and under the
reference shape it is a cache entry rather than a cel, so it is fine for a thing
you import one of *and never save the tiles of*.

**A visible modelsheet is exported**, and that is accepted rather than
overlooked — see [what the export says](#what-the-export-says). Hiding the layer
keeps it out, because hidden layers are not written. Note that hiding removes the
compositing cost and **not** the memory.

An "import at half size" option is worth having and should be named that rather
than "compress", because that is what it does: a quarter of the tiles, and right
for a reference you look at rather than export. Under the reference shape it is
not a separate feature at all — it is a placement of 50%, applied where every
other placement is applied, and the box filter that serves it is the one
`transformTiles` already uses for reduction.
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

**All of that now happens in the derive step and not once at import**, which
changes two things about it. It has to be **deterministic**, because a frame
that is decoded, evicted and decoded again must come back the same or the
picture changes while you scrub over it — that is not a quality question, it is a
correctness one. And it has to be **cheap enough to be on the decode path**: the
`Format_RGBA64` detour exists to stop an 8-bit conversion banding, and it costs
four bytes a channel across a whole frame every time a frame is rebuilt. If that
turns out to dominate a scrub, the answer is to widen the cache rather than to
skip the conversion.

## How a sequence is stored

**Settled: a reference layer, with no cel** — shape 3 of the three the first
draft weighed. What settled it was not the benchmark it asked for. It was video.

The first draft framed this as "cheapest to build and closes a door" against
"more machinery and keeps it open", and said it should be decided deliberately.
Three things decided it, in this order.

### Video removes shape 1 from the list

An HD frame is 17 MB of tiles. **Ten seconds of HD video at 24 fps is 240 frames
= 4.15 GB resident**, permanently, and a full encode of all of it on the first
save.

And a second ceiling sits under that one, which nothing in the first draft of
this note mentioned: **the undo history is bounded at 512 MB** — see [what the
history is allowed to cost](handover.md#what-the-history-is-allowed-to-cost).
Merely holding the frames does not spend that, but any command that displaces
them journals what it displaced, so a single operation over such a layer is
eight times the whole budget and drops every older command in the session to
make room. The layer bake already hits this and says so — a forty-drawing HD
layer retains 880 MB — and a ten-second video is six times the drawings.

Ordinary cels were defensible for a hundred-frame PNG sequence and are not a
candidate once a video can be imported. That is new information rather than a
change of mind: the first draft named video as the thing that would decide the
audio library and did not notice it also decides this.

### Shape 3's compositor cost was overstated, and this was checked

The first draft said shape 3 costs "one new path in the compositor, which is
real and is the substantial one". **That is true if the cache holds decoded
pixels, and false if it holds a `TileGrid`** — which it should, because
[`compositeGrids`](../src/core/compositor.cpp) already takes `const TileGrid*`
and cannot tell where one came from.

There is exactly one place in the program that resolves a layer to pixels —
`collectPasses` — and it already has the branch this needs, for the colour layer:

```cpp
if (layer->kind == LayerKind::Ctg) {
    ...
    else if (const CtgFill* fill = doc.ctgFillFor(track_id, image_id, *it))
        passes.push_back({nullptr, layer, {}, fill});
    continue;
}
```

A reference layer is the same three lines with a `const TileGrid*` coming back
instead of a `const CtgFill*` — and it inherits the rule the CTG branch already
states in a comment beside it: *"If no fill has been built yet the layer simply
does not draw — compositing is not the place to start a max-flow."* **Substitute
"decode" for "max-flow" and that is exactly the rule this needs**, for the same
reason and with the same consequence: the paint finishes with whatever is in the
cache, and asks for what was missing.

Two things fall out of it, neither of which had to be designed:

- **The per-layer export is free.** `compositeLayers` resolves through
  `collectPasses` too, so "what does a per-layer export write for a layer with no
  cels" — an open question in the first draft — answers itself: what the
  compositor draws, at canvas size, through the path that already exists.
- **A reference grid keeps the absent-tile shortcut.** It is a real `TileGrid`,
  so a mostly-empty reference costs its own area and not the viewport's. That is
  the thing a fill had to have specially given back to it; see [what a fill with
  no absent tile stops getting for
  free](handover.md#what-a-fill-with-no-absent-tile-stops-getting-for-free).

### Shape 2's promotion is better as a command than as a state machine

Shape 2 — derived cels with the file as the truth — bought the ability to draw
on an import, and paid for it with eviction, rebuild, a stable `revision()` for a
cel that can be rebuilt, and a promotion state machine keyed on the first stroke.

**Every one of those exists to make the first stroke silently turn a derived cel
into a real one.** Make it an explicit command instead and they all go away,
while what they were buying stays. See [the way
back](#what-reference-only-gives-up-and-the-way-back), which is the user's call
and is the whole of shape 2's argument, bought for a dialog.

### What the shape is

**A reference layer has no cels. Its pixels are derived from a file, and what is
derived is a `TileGrid` in a bounded cache on `Document`.**

The precedent is exact and it is not an analogy: `CtgFillCache` is derived data,
bounded, kept in `Document` rather than on the `Cel`, *precisely because losing
it costs a rebuild and nothing else* — which is the sentence
[ctg_fill.h](../src/core/ctg_fill.h) already uses about itself. Read that class
before writing this one. It is close to a template:

| `CtgFillCache` does | and a reference cache wants |
|---|---|
| bounded in **bytes**, not entries | the same — frames differ in size by 4× between HD and 4K |
| LRU touched by *lookup*, not by store | the same, and for the same reason: scrubbing back and forth must not evict what is being scrubbed over |
| a `generation()` counter for answers in flight | the same — see [the traps](#what-the-handover-already-knows-about-this) |
| eviction costs a recompute and nothing else | eviction costs a decode and nothing else |

And the request path is the one the colour layer already uses:
`CanvasWidget::paintEvent` asks for what is missing and computes none of it; a
worker decodes; a poll installs the answer and refreshes. `requestCtgFills` is
the model, and the reason it is the right one here is that **video decode cannot
happen on the interface thread** and neither can a 70 MB scan.

**One new invariant, and it has to be written down because it does not look like
one:** the reference cache may only be evicted where the document may be edited.
`LayerPass` holds raw pointers into it and `compositeGrids` reads them from
several threads. That is the same rule the document already has — it just does
not read as "editing the document" to whoever writes the eviction.

### Placement is stored, and that is what answers #25

This is the clause the first draft flagged and could not answer: *what is being
placed came off disk at a known size, and resampling it into cels on placement is
a decision this note should make rather than inherit.*

[`Document::transformLayer`](../src/core/document.cpp) bakes, and
[#25](https://github.com/S-poony/Animage/issues/25) argued for the opposite and
lost for one reason: a stored affine would force *everything that reads a
layer's pixels* through a matrix — the brush, the eyedropper, `ctgBarrier`,
`celBounds`, fit-to-drawing, export. See [transforming a layer through
time](handover.md#transforming-a-layer-through-time).

**That argument is entirely about layers whose pixels are the truth.** A
reference layer's pixels are derived, so its placement can be applied *in the
derive step* — decode, colour-convert, linearise, tile, `transformTiles`, cache —
and what reaches `collectPasses` is a plain, already-placed, untransformed grid.
`compositeScene` stays a flat list of untransformed grids. `LayerPass` is not
widened. **The thing #25 refused is still refused.**

Three consequences, all good:

- **Loss never compounds.** Nudging a reference twice re-derives twice from the
  original file. A baked import would resample a resample, and for something you
  position while animating and then adjust again, that is the difference that
  matters.
- **Scaling down costs less, not more.** A 4K source placed at 25% caches a
  quarter-size grid, because the derive is what applies the scale. ("Import at
  half size" is therefore not a feature — it is a placement.)
- **The locked-layer clause dissolves.** The first draft asked whether placing an
  import means unlock-place-relock, or an exception. Neither: the rule stays
  *lock refuses what writes pixels*, a reference layer's placement writes none,
  and imports do not land locked anyway.

**One green box, two Applies** — bake on an ordinary layer, store on a reference
one — with the *layer kind* deciding and the bar saying which. That is the "two
doors and no switch" argument holding rather than being bent: what is refused
there is a control that lets you change what a live gesture is about, and this is
not one.

### One field the model needs, and it is not a retiming feature

`Image` needs a second sparse map beside
[`cels`](../src/core/image.h): `LayerId → source frame index`. Absent means empty
here, exactly as it does for `cels`, and it survives reordering and deletion for
the same reason `cels` does.

**Retiming an import is not a priority — but this field is not what makes
retiming possible, it is what makes the mapping survive an ordinary edit.**
Without it, "which frame of the source does this drawing show" has to be derived
from position, and position moves: add a hold and two drawings share a slot
index; delete a frame and everything after it shifts. The very first hold breaks
it, and retrofitting the field afterwards is a migration of every project that
has an import in it.

**Do not key it on `Image::number`.** [track.h](../src/core/track.h) is explicit
that nothing is keyed on that number and that it is reused after a deletion — so
keying a picture on it would silently re-point another drawing's frame. That is
the same warning the drawing-number counter earned, pointing the other way.

With the field present, retiming an import is not a feature to build so much as
one that is not prevented, and the two traps to know are recorded in [what the
handover already knows about
this](#what-the-handover-already-knows-about-this).

### What is still worth benchmarking, and what is not

`bench_import` was proposed to size a door that is now closed, and most of what
it was for went with it. **It exists anyway, and it was worth more than this
section expected** — not because it sized anything, but because a report arrived
that nothing could otherwise answer, and it turned two guesses with different
fixes into a number. It takes a folder, so it measures the files somebody
actually reported about; what a PNG costs depends on what wrote it.

- ~~**Decode time per frame, per format**~~ — **measured, and the first answer
  was about us.** The tiling loop was paying three `std::pow` per pixel and a
  hash lookup per pixel: 145 ms against 21 ms of actually reading an HD PNG. A
  table and a hoist took it to 35. What is left on a large frame is the file
  reader, which is not ours.
- **What a cache bound should be**, in bytes, against a realistic scrub. Still
  open: 512 MB is a number with arithmetic behind it and no measurement.
- **Time to convert a layer to drawings**, because that one *does* write cels and
  is bounded by the history budget.

Encode time per imported frame against a drawn one — the first number the draft
asked for — is no longer interesting, because a reference layer's frames are
never encoded into cels at all.

## Video is a sequence with a decoder in front

**Settled: a video is extracted to frames once, at import.** Not decoded to
frames in memory, and not played live as a layer.

### Why not live

A `QMediaPlayer` inside the compositor is a second timebase, which is precisely
the trap [the playback clock](#the-playback-clock) spends four ways describing.
And `QMediaPlayer::setPosition` is not a frame-exact random-access decoder: it
seeks approximately and asynchronously, so getting frame N means getting
something near frame N. For a program whose stated purpose is deciding *which
frame* a thing is on, an off-by-one-or-two reference is worse than no reference,
because it is wrong quietly.

### What extraction buys

- **Video and sequence become one storage shape**, one cache, one export answer,
  one everything downstream. The decoder is a front end and touches nothing else.
- **Frame accuracy is exact and permanent.** There is no seek behaviour left to
  fight, ever.
- **The project opens on a machine without the codec**, which a self-contained
  project folder ought to mean and would not otherwise.
- **Qt Multimedia never reaches the paint path or the playback path.** It runs
  once, in a worker, at import. The seam [which library](#which-library) asks for
  is preserved for free rather than by discipline.

### What it costs, and the mitigation

Disk, and a wait at import. Three decisions follow:

- **Extract as JPEG.** The source was already lossy, so nothing that matters is
  lost — and it is the difference between roughly 600 MB and roughly 70 MB for
  ten seconds of HD. An imported PNG sequence is kept exactly as it arrived; only
  video-derived frames are written as JPEG, because only they came from a lossy
  source already.
- **Keep the video file too.** It is usually smaller than its own extracted
  frames, and it is what makes re-extraction at a different rate possible. See
  [when the frame rate changes](#when-the-frame-rate-changes), which needs it.
- **The dialog asks for in and out points**, defaulting to the whole file. This
  is the mitigation for the case that hurts — a two-minute animatic is 2880
  frames — and it is what people want anyway: you import the part of the animatic
  that is your shot, not the whole board.

### Which file is the truth, for a video

Worth setting out on its own, because everywhere else in this note the answer is
obvious and here it is not — and the whole of the section below only makes sense
once it is clear.

**For an image sequence the imported files are the truth**, from the first day to
the last. The tiles are derived from them, dropping a tile costs a decode, and
re-deriving is exact however many times it happens. There is nothing here to
worry about.

**For a video the extracted frames are the truth, and the video file is not.**
That is the asymmetry. A video cannot be read at a frame you name — seeking is
approximate, which is [why it is not played
live](#why-not-live) — so extraction is not a cache being filled from a source
that stays authoritative. **It is a one-way conversion**, from something the
program cannot address into something it can, and afterwards the picture is made
from the result and never from the `.mp4` again.

So a frame missed during extraction is missing from the picture, and nothing
re-derives it, because re-deriving would mean running the extraction again and
nothing does that on its own. The video is kept beside the frames — so the frame
is not gone from the disk, and running the extraction again is at least
*possible* — but that is a fact about the folder and not a mechanism.

**Converting to drawings is downstream of all of this and is not where the risk
is:** it copies whatever the picture already is, holes included.

### Slots are addressed by time, not by file number

**Frames are placed where their presentation timestamp says, and never by their
position in the extracted list.** This is not a defensive measure; it is what
conforming a rate *is*. A 25 fps source in a 24 fps scene means slot *i* is the
source frame nearest time *i*/24, and there is no way to express that by counting
files. See [when the frame rate changes](#when-the-frame-rate-changes), which is
the same mechanism read from the other end.

One property falls out of it and is worth knowing, though it is not a reason to
do it: a frame that is missing for any reason leaves a *gap* rather than shifting
everything after it, and a gap is filled by holding the frame before — which is a
hold, and the timeline is made of those.

### Getting a complete extraction

**Why this is a question at all: Qt Multimedia gives us a player, not a video
decoder.** `QAudioDecoder` decodes a file to buffers on demand; there is no
public equivalent for video, so the only route to frames is `QMediaPlayer`
feeding a `QVideoSink`. A player is **entitled** to drop frames — its job is the
right picture at the right wall-clock moment, and skipping when it cannot keep up
is correct behaviour for that job. The risk is not that decoding is unreliable;
it is that this is a playback tool held by the wrong end.

**Entitled to is not the same as likely to, and the two should not be confused
here.** Decoding HD at 1× is a small fraction of one core against a 41 ms frame
period. The expectation is that nothing drops at all.

So two things, both of which are cheap and neither of which is a recovery
mechanism:

- **Extract at 1×, and never let the sink be the bottleneck.** A player driven
  fast has to choose what to skip; at normal rate it has a whole frame period per
  frame and no reason to skip anything. The frame handler must not encode a JPEG
  while the decoder waits on it — copy the frame, put it on a bounded queue,
  return, and let a worker write the files. Bounded, because a queue of
  decoder-owned frames is a way to exhaust the decoder's own buffers. The cost is
  that extracting *n* seconds takes *n* seconds, which [in and out
  points](#video) keep to the length of the shot.
- **Count what arrived.** A video declares its duration and rate, so the number
  to expect is known before extraction starts. Comparing costs nothing and it is
  what makes a shortfall a fact rather than a suspicion.

**And nothing else, until there is something to design for.** What an import
should do about a short extraction — refuse, import with the gaps, retry — is not
decided here, deliberately: the right answer depends on whether shortfalls happen
at all, how many frames, and whether they are transient or systematic, and none of
those is known. Choosing a recovery before knowing the failure is how machinery
gets built for a case that never arrives. **The measurement decides it**, and
until then the count is an assertion that fails loudly.

If it turns out to drop regularly, the answer is not a retry button — it is that
`QMediaPlayer` is the wrong tool, and the route is FFmpeg directly. That is
genuinely awkward: Qt Multimedia already *bundles* FFmpeg for its own backend and
does not expose it, so this would be a second copy of a library already in the
build. Worth knowing as the shape of the bad outcome, and worth nothing at all
until it is measured.

## What reference-only gives up, and the way back

Stated plainly so that it is a decision and not a discovery.

**`ctg_sources` are resolved inside the track.** [ctg.cpp](../src/core/ctg.cpp)
looks each one up with `line->findLayer(source)` and reads it with
`record->celFor(source)` against an `Image` in that same track. So a CTG barrier
must be a cel-bearing layer in the same track as the colour layer.

One consequence, and it is the whole of what reference-only gives up: **a
reference layer has no cel and cannot be a CTG barrier at all.** So **importing
scanned line art and colouring it with LazyBrush does not work, until it is
converted.** For an application whose headline is a colour solver, that is a
real thing to give up, and it is why the way back is not optional.

This used to list a second consequence — that an import landing in a new track
cannot be a barrier for a colour layer in a different one — and drew a feature
out of it. The sentence is true and the feature was not needed: the colour layer
goes in the import's track. See [landing in an existing
track](#landing-in-an-existing-track), which is now a record of a decision
reversed rather than an argument for one.

### Convert to drawings

**Settled: it converts the whole layer, and it is offered by a popup that
appears when you try to draw on a reference layer.** Both halves are the user's
call and both are worth the reasons being recorded.

*Whole layer, not per drawing.* Per drawing is cheaper on the history and is what
shape 2's promotion would have done — and it is the wrong thing to show somebody.
A reference layer that is drawings on some frames and reference on others is a
state nobody asked for and nobody can see, and the first question it produces is
"why can I draw here and not there". Converting the layer is one answer to one
question.

*Offered on the attempt to draw, not only from a menu.* The refusal is where the
question actually gets asked. But it should be **both**: a command in the layer
panel beside "Transform layer through time", greyed out with a tooltip when it
does not apply, because that is the pattern the handover argues for — *"a control
that comes and goes as you move between layers is one nobody can find twice, and
'why can I not do this here' is the question a disabled control exists to
answer."* The popup is discoverability; the button is findability; they are not
alternatives.

> **The button was built, used, and taken out again on the user's call.** What
> the argument above missed is that nobody goes looking for this: converting is
> not a thing anybody sets out to do, it is what you find out you need when a
> stroke will not land — and the refusal is already there. A permanent control
> for it was a fourth button in a four-button panel, greyed on every layer but
> one, teaching a concept the program can raise for itself at the only moment it
> is wanted.
>
> What replaced it is the offer reaching **four gestures instead of one**: the
> brush, and copy, cut and paste, all of which want a cel an import has not got.
> See [no button for converting](handover.md#no-button-for-converting-and-where-the-offer-went-instead).

**What the popup says, and the one thing to get right about it.** The user's
instinct was that it should say whether the conversion is lossless, depending on
whether the reference was scaled or rotated. That is the right thing to key on
and the wrong tense to say it in.

The conversion writes the cached grid into cels, and the cached grid *is* what is
on screen. **So the conversion never loses anything** — any resampling already
happened in the derive step. What is actually lost is the *future*: once
converted, the pixels are the truth, so re-placing the layer bakes on top of what
is already baked, and from then on loss compounds. So:

- at 1:1, with no rotation: *"Every pixel is kept exactly."*
- scaled or rotated: *"The drawings will hold what you see now. Placing it again
  afterwards will resample what is already resampled — place it first if you have
  not."*

**And it must not claim more than that.** A JPEG or a video frame was lossy
before it reached the program, and "every pixel is kept exactly" is a statement
about *this step* and not about the artwork. The popup says what converting does;
it is not entitled to say the drawing matches what somebody scanned.

The popup also has to say the cost, because it is large and it is not
recoverable: the frame count, the memory, and that **it will clear the rest of
the undo history**. A 240-frame HD layer is 4 GB in one command against a 512 MB
budget. The conversion itself always undoes — the newest command is never
dropped — but everything older goes. That is inherent to writing cels rather than
a fault, and it is the same sentence [transforming a layer through
time](handover.md#what-it-costs-and-the-bound-that-had-to-change) already had to
write about itself.

> **Wrong, and the popup was written to say it before anybody measured.** It
> costs the history *nothing*. The history's budget counts the tiles a command
> **displaced** and is keeping alive for undo — a bake displaces every tile of
> every drawing, where a conversion writes into cels that did not exist and
> displaces none. The 4 GB is real and is in the document; it is simply not in
> the history, and nothing older is dropped. The mistake was reasoning from the
> bake by analogy instead of from what `Command::retainedBytes` counts.
> Measured, and pinned in `test_transform`.
>
> **And the sentence below about the files has a consequence this note did not
> follow through.** A save carries forward the imports something still *names*,
> and a converted layer names none — so converting and saving took the scan out
> of the project folder, with no symptom at the time, and undoing then left a
> layer naming files that existed nowhere and a project that would not save at
> all. Fixed on the user's call by keeping the files rather than by explaining
> the loss, which costs an imports folder that only ever grows. Both in
> [converting an import to
> drawings](handover.md#converting-an-import-to-drawings).

**Sometimes it refuses, and that is not a gap to be closed later.** Whole-layer
conversion serves the case it is for — scanned line art, tens of drawings, which
is what colouring an import means — and cannot serve a two-hundred-frame video,
because nothing can: those pixels do not fit in memory and colouring them was
never the point. The refusal says the number and says the layer is too long,
rather than starting and failing partway. Reimporting a shorter range is the
answer, and the [in and out points](#video) are what make it a small one.

**And it is the same code.** `Document::transformLayer` is: walk every drawing of
a layer, write a new `TileGrid` into each, inside one `ScopedCommand`, with the
`bad_alloc` rescue, the deferred trim and the redo stack held aside. Convert-to-
drawings is that loop with `decodedFrame(source, n)` where `transformTiles(old,
t)` is. **All four of those details are bugs if they are omitted, and all four
were found the expensive way once already** — see [running out of memory, and why
that is a rescue rather than a
crash](handover.md#running-out-of-memory-and-why-that-is-a-rescue-rather-than-a-crash).
Extract the loop when the second caller arrives; do not write it twice.

It needs the bound asked before the work as well, in the shape of
`commitFitsInBudget`, so that a conversion which cannot fit refuses with a reason
instead of failing in the middle.

### Landing in an existing track

**Reversed, on the user's call: an import always makes its own track, and there
is no combo box.** This section said the dialog asks and that it was in the
first cut, on the grounds that `ctg_sources` resolve inside the track — so a
character's colour layer could not cut against an import that landed anywhere
else.

**The step that argument skips is that the colour layer can be added to the
import's own track.** `addColourLayer` acts on whichever track is current, and
an import makes its track current; the source list it builds offers every raster
layer *of that track*, so a converted import appears in it. Import, convert, add
a colour layer there, and the whole chain sits inside one track with nothing
crossing between two. Nothing in the model had to change and nothing new had to
be built — it was already the shorter route, and the combo box was machinery for
a problem that only exists if you insist the colour layer was there first.

What the reversal gives up is the case where it *was* there first, and it is
worth naming so that this is a decision rather than an oversight: **a track you
have already built cannot cut against a scan imported afterwards.** You would
want that scan as a layer in that track, and it will arrive in one of its own.
That is real. It is not the workflow the argument was made for — scanning line
art and colouring it means the import *is* the drawings, not a second opinion
about drawings that already exist. The way out is unchanged and still unscoped:
an operation that moves a layer from one track to another.

## The menu, and what each dialog asks

Four items under **File ▸ Import**, arriving in this order:

- **Audio…**
- **Image…**
- **Image sequence…**
- **Video…**

A still and a sequence are separate items because they arrive at different times
and do different things, not because the file picker cannot tell them apart.
Video is separate again because it asks two questions neither of the others does.
Nothing for projects or PSDs.

**Drag and drop onto the timeline is how people will actually do this.** Not in
the first cut, but each import should be a function the drop handler can call.

### Image sequence

- **Order is numeric and not correctable.** `frame10.png` never goes before
  `frame9.png` and nobody has ever wanted it to. Files with no number, or two
  numbering schemes in one selection, are *said* rather than guessed at — the
  house rule is to let the input in and explain it, not to silently pick.
- **Where:** a new track, always. See [landing in an existing
  track](#landing-in-an-existing-track), which argued for a combo box here and
  was reversed — a colour layer added to the import's own track reaches the
  converted drawings without one.
- **When:** a start frame, defaulting to 1. The default is what anybody wants
  and the box costs nothing over hard-coding it, which is the only reason it is
  here rather than deferred.
- **Exposure:** on 1s. An image sequence has no frame rate of its own; inventing
  one is inventing information. This is the sentence video does *not* inherit —
  see below.
- **Past the last frame: nothing.** `TrackEnd::Nothing`, which is the default
  for an animation and the opposite of what a still gets. That is not an
  inconsistency to tidy up later: a modelsheet is meant to stay up for the whole
  shot, and an animatic is a stretch of timing that ends where it ends. Both are
  the track's own setting afterwards.
- **Size:** the transform box, validated by the user, and stored rather than
  baked.
- **Import at half size:** offered, off by default. It is a placement of 50% and
  not a separate mechanism.
- **The recap:** frame count and what it will cost, before it happens.

### Image

The same, minus order and exposure. One drawing, `TrackEnd::HoldLast`.

### Video

Everything the sequence dialog asks, plus two questions that are the whole
reason it is its own item:

- **In and out points**, defaulting to the whole file. A two-minute animatic is
  2880 frames on disk; the shot is thirty of them.
- **Frame rate.** The video has one and the scene has one. Conform to the
  scene's, by default and loudly — see [when the frame rate
  changes](#when-the-frame-rate-changes). This is not the sequence's "inventing
  one is inventing information": a video's rate is information, and using it is
  the opposite of inventing.

And the recap says what extraction will cost in **disk** as well as memory,
because that is the number this import has and the others do not.

### Audio

The file, and a placement offset in frames. And one sentence the dialog has to
say, because it is otherwise found out much later: **audio is not exported.**
That sentence stops being true the day [video
export](#video-export-and-what-qt-gives-free) ships, and until then it stays.

## When the frame rate changes

**Both directions warn, and this is a decision rather than a nicety.** A
conformed video is the one import whose picture is right and whose *timing* can
silently become wrong, and timing is what the import was for.

**On import, when the video's rate is not the scene's.** Say both numbers, say
which one is being conformed to, and say what that does — at 25 into 24, one
frame in twenty-five is dropped; at 24 into 25, one is repeated. Conforming is
right, because getting *time* right is what a lipsync or animatic reference is
for, and a 4% drift over a ten-second shot is two and a half frames by the end.
But it is not free and it should not be silent.

**On changing the scene's rate, when a conformed video is already in it.** This
is the direction that is easy to miss and worse when it happens: the import was
correct when it was made, nothing about it has changed, and it is now wrong. The
warning fires from `SceneSettingsDialog`, names the imports affected, and offers
to re-conform them.

**Re-conforming is cheap, and that is why it can be offered rather than only
warned about.** The extracted frames are already on disk and the source video is
kept beside them; re-conforming rewrites the slot-to-source-frame mapping and
touches no pixels at all. It is the [one field the model
needs](#one-field-the-model-needs-and-it-is-not-a-retiming-feature) doing the job
it exists for.

**Which means the source rate has to be stored.** One number per imported video,
recorded at import: the rate the frames were extracted at. Without it the second
warning cannot be asked — "did this used to match?" has no answer — and
re-conforming has nothing to conform from.

This is the one import-provenance field this note argues *for*, and it is worth
saying why, because [where an import lands](#where-an-import-lands) argues
against the whole category. The objection there is to a field that records where
something came from and then decides nothing: *"a stored counter that no longer
decides anything is exactly the thing somebody re-wires by accident later"*. This
one decides two things every time it is read — whether the warning fires, and
what re-conforming conforms from. It is not provenance; it is an input.

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

**Two fields elsewhere in this note look like exceptions and are not**, and it is
worth saying which so that the rule stays usable rather than being quietly
abandoned. A reference layer stores its source and its placement, and a conformed
video stores the rate it was extracted at. Neither records *provenance* — each is
read every time the picture is built, and the program stops working without it.
What the rule above refuses is a field that says where something came from and
then decides nothing, because that is the one that attracts *do not export
imported tracks, lock imported tracks, relink, re-import*. **The test is whether
removing the field breaks something today**, and for these two it does.

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

**And an export has to decode every frame of a reference layer, not the ones
that have been looked at.** The cache holds what somebody scrubbed over; an
export writes the whole shot, so a 240-frame reference is 240 decodes even if
three of them were ever on screen. That is not a new problem, it is an old one
with a second instance: [export_sequence.h](../src/app/animage/export_sequence.h)
already explains that the export must run the colour solves for frames nobody
visited, *because* a compositor is not allowed to start one — and says an export
that composited only what was cached would write blanks without saying anything.
A reference layer is the same sentence with "decode" in place of "solve", and it
wants the same two things: the export drives the work itself, and the progress
bar counts it. A decode is milliseconds where a solve is a second and a half, but
240 of them is still a stretch of a bar that would otherwise sit still.

## The project folder and the file format

A project stays **a self-contained folder**. Nothing here introduces a reference
to a file outside it, because a project that breaks when it is moved would be the
first thing in this format that does.

```
the-shot.animage/
  scene.json
  cels/cel-000007.acel
  audio/dialogue.wav          the imported file, copied in
  imports/bg_0001.png         an imported sequence, as it arrived
  imports/animatic.mp4        an imported video, kept
  imports/animatic_0001.jpg   ...and the frames extracted from it
```

**`imports/` is not redundant any more, and that is the change.** The first
draft kept the source files while they were redundant, as the cheap decision that
preserved the option. Under the reference shape they are not an option, they are
where the picture comes from: a reference layer has no cels, so if the file is
gone there is nothing to draw. Which raises the stakes on one line of [not in
scope](#not-in-scope) — relinking a moved source file stays out, and it stays out
because the folder is self-contained and nothing can move.

**What is derived and is not stored**, all of it rebuilt on demand:

- decoded PCM — decode at load; a ten-second file is tens of milliseconds
- waveform peaks
- **every reference frame's tiles.** This is the large one: a 240-frame import
  adds nothing whatever to what a save writes, because there are no cels.

**`kSceneFormatVersion` goes to 2.** Not because an older build would misread an
`audio_tracks` key, but because it would *ignore* it and then autosave over the
project two minutes later without it. Silent data loss, and the version gate is
what stops it. That is exactly the standard [project_io.h](../src/app/animage/project_io.h)
sets: bump when old builds would get it wrong.

The same gate covers everything else here, and it is worth listing what an older
build would get wrong so that nobody is tempted to let one of them through:
a `LayerKind` it does not know reads as raster, so an import would come back as
an **empty layer that silently saves over the reference**; the per-image source
frame map would be dropped; and a conformed video's source rate would be lost, so
[the frame-rate warning](#when-the-frame-rate-changes) could never fire again.
Three ways to lose work quietly, behind one number.

The incremental save is unaffected and cheaply so. A converted import's cels have
revisions like any others, so after the first write they are carried forward as
hard links and cost nothing; an unconverted one has no cels to carry.

## What was built that this note did not plan

Two things, both on the user's call and both worth recording here rather than
only in the handover, because each one answers a question this note asks and
answers differently.

**The shot can be told to reach the sound.** This note has audio never touching
the shot's length, and that is right as far as it goes — a soundtrack running
long is reference, and a scene that grew when one was imported would take the
shot's length from the wrong thing. What it missed is that
[`onPlaybackTick`](#the-playback-clock) derives its slot from `shotFrames`: a
one-second soundtrack in a shot of one drawing plays *one frame* and stops, so
the feature this whole note is written around does not work. Widening the
timeline lets the playhead be dragged over the sound and does nothing for Play.

So the import dialog offers it, with a rule: **the box appears when it would
change something, and is ticked only when nothing has decided the length yet.**
`scene.h` already named animating to a soundtrack as the case `fixed_length`
exists for, which is the part this note could have found and did not.

**A soundtrack can be moved and cropped in its row, finer than a frame.** Not in
this note at all. Dragging the block sideways moves the sound; dragging its ends
crops it, non-destructively, by moving two numbers and touching no samples. The
placement is fractional because 1/24 of a second is 42 ms — most of the way to a
syllable — so a sound placed to the nearest frame is not placed at all.

That last point is a correction to something this note *does* say. [Audio is not
a track](#audio-is-not-a-track) observes that "the axis is free" in a soundtrack
row, meaning a vertical drag collides with nothing. Both axes turned out to be
wanted: sideways moves, up and down is the level, and the ends crop.

**None of it is shared with video, and that is a decision.** A video is
[extracted to frames at import](#video-is-a-sequence-with-a-decoder-in-front),
so its row is a track row with cards — moving it is a slot operation, cropping
it is which drawings exist, and it wants no sub-frame placement. The reuse point
would be the gesture code and not the data, so nothing has been pre-shaped for
it.

## What the spike measured

**The deployment spike has been run, and the record is
[audio-spike.md](audio-spike.md).** It is kept out of this note rather than
folded into it, for the reason given at the top: this stays a plan and is not
rewritten into a description. What belongs here is only what it *changes* about
the plan, which is two things and a confirmation.

**It confirmed the expensive worry was unfounded.** All three deployment tools
bundled the FFmpeg backend from `animage`'s import table with no help, and a
downloaded Windows package loads it on a machine that never had Qt. The cost is
about 20 MB per platform and nothing measurable at startup. Every line of [what
it costs, and none of it has changed](#what-it-costs-and-none-of-it-has-changed)
is still a real bill; none of it is a risk any more.

**It corrects two things in this note.**

*The first is small and mechanical.* [Which library](#which-library) says
`modules: qtmultimedia` goes on **four** Qt install steps. The file has **two**
`install-qt-action` blocks, one of which the build matrix runs three times. The
Windows core-only sanitizer installs no Qt deliberately and must not start.

*The second is not small.* [The open questions](#the-open-questions) asks
whether `processedUSecs()` counts audio handed over or played out, and its
stated test — *"whether the number ever reports more audio than there has been
time to play"* — **is the wrong meter and answers wrongly.** There is no instant
to measure real time from: the stream starts inside `QAudioSink::start()`, which
takes a third of a second, so the number sits a constant few tens of
milliseconds ahead of any timer started around that call and reads as "handed
over" on a device that plainly is not. The answer is **played out**, decided by
comparing what the sink was handed against what it reports, which has no start
instant in it at all.

**And it found a seam this note does not draw.** [Scrubbing comes
first](#scrubbing-comes-first) is right that it is the higher-value half — and
it turns out to be the half that needs none of the FFmpeg payload.
`QAudioSink`, `QAudioDevice` and `QMediaDevices` are native inside
`Qt6Multimedia`; delete the backend plugin entirely and scrubbing still works,
measured. What the 20 MB buys is `QAudioDecoder` — the codec gap this note takes
Qt Multimedia partly to close — and `QMediaPlayer`, which is all of video. That
is not an argument against paying it. It is worth knowing which line item is
which, because it means the feature the program is *for* does not depend on the
part of the bill that could go wrong on some platform.

**One measurement is untouched**: whether `QMediaPlayer` at 1× extracts every
frame. It belongs to video rather than audio, and it is still the only question
in this note whose answer could change what gets built.

## What the handover already knows about this

Everything below is already written down in
[handover.md](handover.md) and none of it was reachable from this note. They are
here because each one is a thing this feature walks into, and the handover's own
rule for its trap list is that you scan it *before* touching something rather
than after.

| what it is | why it is this note's problem |
|---|---|
| [What a missing pen release takes down with it](handover.md#what-a-missing-pen-release-takes-down-with-it) | the convert popup is raised by a pen that is still down |
| [What the history is allowed to cost](handover.md#what-the-history-is-allowed-to-cost) | 512 MB, and converting a layer is past it on its own |
| [Running out of memory, and why that is a rescue rather than a crash](handover.md#running-out-of-memory-and-why-that-is-a-rescue-rather-than-a-crash) | the four details convert-to-drawings inherits, each a bug if dropped |
| [What asking for a private Qt component at the top level switches off](handover.md#what-asking-for-a-private-qt-component-at-the-top-level-switches-off) | `Multimedia` in the root `find_package` turns the application off |
| [What emptying the fill cache does not reach while a solve is in flight](handover.md#what-emptying-the-fill-cache-does-not-reach-while-a-solve-is-in-flight) | a decode in flight when the placement changes |
| [What went stale when the solve stopped finishing in the same call stack](handover.md#what-went-stale-when-the-solve-stopped-finishing-in-the-same-call-stack) | anything that was correct because the decode used to be synchronous |
| [Why a cache key of cel revisions serves wrong fills, not slow ones](handover.md#why-a-cache-key-of-cel-revisions-serves-wrong-fills-not-slow-ones) | a wrong cache key here serves the wrong frame, not a slow one |
| [Every route that changes the input to a differencing function](handover.md#every-route-that-changes-the-input-to-a-differencing-function) | an import adds a track, and audio adds a row |
| [What a comment goes on claiming after you replace the design under it](handover.md#what-a-comment-goes-on-claiming-after-you-replace-the-design-under-it) | `refuseToEditHere` says in a comment that it does not check the kind |
| [Looking at the interface](handover.md#looking-at-the-interface) | four dialogs, a popup and a new row, none of which a green build can see |
| [The same source, two different pictures](handover.md#the-same-source-two-different-pictures) | the backend a download has is not the backend on your desk |

Five of them need more than a row, and one thing that is not a trap at all is at
the end because it means a piece of this does not have to be built.

**The convert popup must not be raised from `tabletEvent`.** *"Opening any dialog
with the pen down was enough"* is how the handover puts it, and the popup this
note asks for is opened by exactly that: a pen coming down on a reference layer.
The classic failure is smaller here than it looks — a refused stroke opens no
command, so there is no depth counter to strand — but the release is still
swallowed by the dialog, and a dialog appearing under a pen that is still on the
tablet is bad to use quite apart from being risky. Record the refusal on the
press and raise the popup on the release. The general shape is `abandonGesture`'s:
a press-to-release gesture needs a third way out.

**The decode cache needs a generation counter from the first version, not from
the first bug.** What a decoded frame depends on is not all in its key: the
placement is on the layer, the source list is on the import, and neither moves
anything the key can see. The way both say "that is all wrong now" is by emptying
the cache — which reaches the shelf and not the answers in the air, and a decode
started before a placement changed will land after it, match, and be installed as
current. The fill cache learned this the expensive way when its solve stopped
finishing in the call that started it. Copy `CtgFillCache::generation()`; do not
rediscover it.

**And a generation is not a document identity, which is the one thing it looks
like it is.** It counts how many times *that* cache has been emptied, so it
answers "was the shelf cleared under me" and nothing else. Every project loaded
from disk arrives at the same count, because `loadScene` empties the cache
exactly once — so two projects opened one after the other are both at
generation one, and the questions asked about the first are indistinguishable
from current when the second arrives. Ids do not save you either: they come from
a counter that restarts per document, so the small ones collide as a matter of
course, and an answer about drawing 3 of the old project is installed against
drawing 3 of the new one and composited. Replacing the document is a *statement*
rather than a comparison: `CanvasWidget::forgetImports`, called from
`afterProjectLoaded`, which is the one funnel every replacement of `doc_` goes
through. Anything that keeps a question across a document swap needs the same
treatment — `MainWindow::document_epoch_` is the number for the ones that cannot
simply be dropped.

**And a wrong key here is worse than a slow one.** The lesson from the fill cache
is that a key which is a bijection today stops being one quietly: *"every cel in
a project straight off disk is at revision 1"*. A reference frame's key has to
name the source, the frame index **and** the placement it was derived under —
because two frames derived at different scales are different pictures that would
otherwise share a key, and what you get is not a slow scrub but the wrong frame
on screen.

**An import has to call `syncTimelineHeight`.** It does not size the timeline
dock, it moves it by the rows that came or went, so every route that changes the
track count has to say so — and [#74](https://github.com/S-poony/Animage/issues/74)
is what happens when one does not: a load that added two tracks left the strip at
the height for one, and the symptom was that *Ctrl+Z put it back*, because undo
was the next thing that called it. An import adds a track. An audio import adds a
row that is not a track at all, which is a case that function has never seen.

**One comment goes stale the moment a reference layer exists**, and it is a
comment that explains a deliberate decision rather than a detail.
`refuseToEditHere` — the brush's list — says: *"The layer kind is not here: the
brush puts scribbles on a colour layer, the eraser rubs them out again... Nothing
on this list moves a mark from one place to another, so nothing on it has to care
which kind of mark it is."* A reference layer is the first kind the brush itself
must refuse, so that list gains its first kind check and that paragraph stops
being true. Rewrite it in the same commit. The handover's own entry on this is
about a comment that survived the design under it being replaced, and it is worth
reading before writing the replacement.

**One thing comes free and is worth knowing so nobody builds it twice.**
`refuseToEditHere` is what `Pointing` consults, so the moment a reference layer
refuses the brush, **the cursor already says so** — before the pen is anywhere
near the tablet, with no new code. That is [what the pointer
says](handover.md#what-the-pointer-says) paying for itself, and it means the
popup is the second thing that tells you rather than the first.

## Not in scope

More than one audio track, volume automation, waveform rendering in the first cut
(a labelled bar is enough to place a sound), track reading and phoneme breakdown,
and relinking a moved source file.

Two things have moved off this list and one has moved onto it.

**Video import is in.** It was "a question, not a plan" and it is now a plan; it
is what settled [which library](#which-library) and, through that,
[how a sequence is stored](#how-a-sequence-is-stored).

**Video export and muxing are in, and are last.** Not in the first cut and not in
the third, but no longer out — see [video export](#video-export-and-what-qt-gives-free),
which exists so that the work is cheap when it is reached and so that nothing
built before then makes it expensive.

**A manual audio sync calibration is out**, on the user's call, and out is
reversible here in a way most of this list is not: it is a preference rather than
a field in `scene.json`, so adding it later touches no project. See [the playback
clock](#the-playback-clock).

## The open questions

**All three of the first draft's are answered**, and they are kept here with
their answers rather than deleted, because what decided each one is the part
worth having.

1. ~~**Which audio library**~~ — **Qt Multimedia.** Video import is wanted, which
   is exactly the branch the question named as decisive. The sub-question — does
   "import a video" mean decode-to-frames-once or play-it-live — is answered
   *neither*, and closer to the first: [extract to frames once, at
   import](#video-is-a-sequence-with-a-decoder-in-front), so the decoder never
   reaches the paint path.
2. ~~**Which shape a sequence is stored in**~~ — **a reference layer, with no
   cel.** Not settled by `bench_import`, which was sizing a door that video had
   already closed: ten seconds of HD is 4.15 GB of tiles against a 512 MB history
   budget. Two things the first draft had wrong in the cheap direction: the
   compositor needs nothing at all if the cache holds a `TileGrid`, and shape 2's
   promotion is better as a command than a state machine.
3. ~~**Whether giving up colouring imported line art is intended**~~ — **no, and
   one of the two answers it proposed turned out to be enough.** [Convert to
   drawings](#convert-to-drawings) is an explicit command over the whole layer,
   offered by a popup when you try to draw on a reference layer. The second
   answer — letting an import land in an existing track — was taken and then
   [reversed](#landing-in-an-existing-track): the colour layer can be added to
   the import's own track, so nothing has to cross between two.

**What is left is two things to measure, and this note does not guess at either.**
A guess written down here would be read later as a decision somebody took, and it
would be an invitation to skip the measurement that was supposed to replace it.
So each is stated as the question, the test, and what each answer means:

**1. Does `QAudioSink::processedUSecs()` count audio handed to the device, or
audio played out of it?**

*Test:* play a file and watch whether the number ever reports more audio than
there has been time to play.
*If handed over:* `playedMs()` subtracts the audio still queued.
*If played out:* use it as it comes.
Either way it is one line, because the arithmetic is a pure function of a sample
count. See [which library](#which-library).

**2. Does `QMediaPlayer` at 1×, with a sink that never blocks, extract every
frame?**

*Test:* extract clips of known length and compare the count against the frame
count each file declares.
*If nothing drops*, which is what is expected: video import costs a wait and
nothing else, and the count stays as an assertion.
*If something drops:* the numbers are what say whether it is a stray frame or a
pattern, and only then is there anything to design — see [getting a complete
extraction](#getting-a-complete-extraction), which deliberately does not decide
it in advance.
*If it drops regularly:* `QMediaPlayer` is the wrong tool and the route is FFmpeg
directly — the one genuinely expensive outcome in this note.

Both belong in the same spike as the deployment check, and that spike comes
before any audio code. Neither blocks starting, but the second is the only
question here whose answer could change what gets built.
