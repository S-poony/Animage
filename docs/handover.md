# Handover

Written at the end of the first build, for whoever picks this up — including a
later me with none of the context. It covers what exists, what is deliberately
not here, the traps that cost the most time, and what I would do next.

The French documents in [fr/](fr/) are still the specification. This file only
records what happened when it was built.

## Where it got to

M0 through M4 exist. **M5 is half done: a project saves and opens; nothing
exports yet.** A session's work survives the window closing, which it did not
before.

| | |
|---|---|
| M0 | Pen latency measured at ~15 ms, passed. [m0-latency.md](m0-latency.md) |
| M1 | Data model, copy-on-write tiles, undo. All five plan tests pass. |
| M2 | Canvas, pressure brush, eraser, layers. Compositing is on the CPU, not the GPU. |
| M3 | Timeline, holds, onion skin, playback, drawing during playback. |
| M4 | LazyBrush solver and the CTG layer, in the app and usable. |
| M5 | **Partly.** Save, open and Save As work. No export, no autosave, no New. |

A project is a folder: `scene.json` in text, and one file per cel beside it.
`serialise.h` decides the structure and `celfile.h` the pixels — both in `core`,
both testable without a window — while `project_files.h` in the application adds
the compressor and the folder around them. Saving builds alongside and swaps at
the end, so an interrupted save leaves the last good project where it was;
opening builds a whole document before adopting it, so a project that will not
open cannot take the open one down with it.

Since the first build, the model also grew a **canvas**: `Scene::canvas()`, the
rectangle that will be exported, set under Edit ▸ Scene settings. Before it
there was no such thing as "the picture" — tiles are sparse and their
coordinates signed, so the drawing surface has no edges at all, which is
deliberate and stays true. Drawing outside the canvas is still allowed; what is
out there simply is not in the picture, which is why a colour fill stops at the
frame.

And **`Timeline` is now `Track`** throughout, including the French
specification. A `Track` is one stack of layers with its own time; the timeline
is the scene's shared time axis and the panel that shows it. A scene has several
tracks and one timeline. If you find the old name anywhere, it meant the track.

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
copy-on-write tiles make a snapshot for a render thread nearly free.

**There is no scribble tool**, and there should not be one — this was the
user's call and it is a better design than the plan sketches. A CTG layer is
the mode: draw on it with the ordinary brush and what you draw is a scribble.

**Cels are not saved as PNG.** The plan says a PNG per cel; a 16-bit PNG cannot
hold a half-float. Half spends its precision relatively — finely near zero,
coarsely near one — while integers are evenly spaced, so of the 15362 half values
in [0,1] a 16-bit image keeps 7169, and some non-zero values quantise to zero.
sRGB-encoding first keeps 10871, which is better and still lossy. A save that
loses pixels is not a save. The format is ours instead, storing the same bits the
tiles hold — see `celfile.h`, which documents the layout so a drawing is
recoverable with nothing but zlib. PNG remains the right thing for *export*,
where a conversion is expected and the destination is another program.

**Pen prediction is ruled out permanently.** Not deferred — refused. It hides
latency rather than removing it, and pays with accuracy at the ends of strokes.
Do not propose it.

## The traps

These are the things that cost hours, in the order they hurt.

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

**The solve runs where the interface is waiting, so it is capped.** A max-flow
grows faster than its region — about 1.3 s for a megapixel — and on a large
drawing an unbounded one does not take a while, it stops the program. The
resolution is now reduced until the solve fits in roughly 512x512 whatever it
was asked for. That is a real loss of quality on a big canvas, and the honest
fix is to solve on a background thread and refine, which nobody has written yet.
It is the first thing to do after saving if colouring is being used seriously.

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

## How to work on it

Everything in `src/core/` is free of Qt and can be tested headlessly. Everything
in `src/app/` is Qt. Keep that line: it is why the model has real tests.

```bash
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH   # MSYS2 UCRT64, from PowerShell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/bench_composite     # timings, not a test
./build/tests/bench_zoom -platform offscreen [dir]   # the whole display path
```

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

## What I would do next

1. **Finish M5**, in this order, because each one wants the last.
   - **Incremental saving.** A save re-encodes every cel, which is about three
     seconds for ninety-six drawings whether or not anything changed. Fine when
     you asked for it; not fine for autosave, which would pay it every time it
     fired. No format change is needed — cels are separate files and
     `Cel::revision()` already says which ones moved — but it wants a small piece
     of per-project state that the window has to hold, so it belongs before
     autosave rather than after.
   - **Autosave**, writing into the project folder. That was a deliberate choice
     and it has a consequence: the disk is then always current, so there is
     nothing for an unsaved-changes warning to warn about, and "quit without
     saving to throw away a ruined drawing" stops working. The window title
     carries a `*` in the meantime, which is the only signal until autosave
     exists.
   - **New**, which waits on autosave: discarding an untitled document is only
     safe to offer once the alternative is not silent loss. It should feel like
     launching the application, plus the Scene settings dialog opening.
   - **Export.** A sequence per layer, `{track}_{layer}_{frame:04}`, over the
     canvas rectangle. One decision is open and is easier made before the dialog
     exists than after: 16-bit PNG cannot hold a half-float without throwing
     pixels away — the same arithmetic that decided the save format — so a
     lossless deliverable means EXR. PNG is still right where the destination
     expects PNG and a conversion is understood.
2. **Scribbles through time.** A CTG cel with no scribbles should fall back to
   the nearest earlier drawing's rather than being empty: colour once, carry
   forward, and a new scribble overrides from there. This is also most of the
   plan's "onion fill" hypothesis, which the layer model makes nearly free and
   which the notes expect to be the selling point. Designed out in
   [scribbles-through-time.md](scribbles-through-time.md), together with the
   harder half — scribbles that move to follow the animation. The carry-forward
   is small and is not blocked by anything; the motion needs item 3 first.
3. **Solve the CTG fill off the interface thread.** It is capped at about
   512x512 today purely so it cannot freeze the program, which costs real
   quality on a large drawing. Solving in the background, coarse first and
   refining, removes both the cap and the wait. The copy-on-write tiles already
   make the snapshot a background thread would need almost free.
4. **GPU compositing**, if `bench_composite` says it is worth it at real
   drawing sizes rather than at the sizes tested here.

## Two things to be careful of

**The undo model rests on cel ids never being reused.** Deleting a drawing and
then undoing a stroke made on a drawing that shared its cel only works because
of that, and because the history counts as a reference on a cel. Both are
tested; neither is obvious from reading the code in one place.

**Layers belong to the track and timing belongs to the image.** This is the
project's central bet and the code depends on it everywhere: adding a layer must
touch no image, and holding a drawing must allocate nothing. There are tests
pinning both. If either starts failing, something has misunderstood the model.

**"Track" and "timeline" are not the same word.** A `Track` is one stack of
layers with its own time; the timeline is the scene's shared time axis and the
panel that shows it. The struct was called `Timeline` until the two meanings
were separated — if you find the old name anywhere, it means the track.
