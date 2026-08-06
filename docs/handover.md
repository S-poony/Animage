# Handover

Written at the end of the first build, for whoever picks this up — including a
later me with none of the context. It covers what exists, what is deliberately
not here, the traps that cost the most time, and what I would do next.

The French documents in [fr/](fr/) are still the specification. This file only
records what happened when it was built.

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
| M5 | Save, open, Save As, autosave, New, and PNG export. EXR is not written. |

A project is a folder: `scene.json` in text, and one file per cel beside it.
`serialise.h` decides the structure and `celfile.h` the pixels — both in `core`,
both testable without a window — while `project_files.h` in the application adds
the compressor and the folder around them. Saving builds alongside and swaps at
the end, so an interrupted save leaves the last good project where it was;
opening builds a whole document before adopting it, so a project that will not
open cannot take the open one down with it.

**Saving is incremental, and autosave rests on that.** A cel's revision is
bumped by every write to it, undo included, so a `project::SaveState` — a folder
and the revisions written to it — is enough to know which files in that folder
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

**Export writes 16-bit PNG**, a folder per layer with
`{track}_{layer}_{frame:04}.png` inside, plus an optional `composite/` of the
flattened picture, all over the canvas rectangle. Hidden layers are not written
at all, so the per-layer sequences and the flattened one agree about what the
shot contains. It is lossy and knowingly so — the arithmetic is in
`export_sequence.h` — and **EXR is the named next step** rather than a decision
still open: PNG is right where the destination expects PNG, and the format list
is easier to add to than the layout is to change.

One thing about export is worth reading before touching it. **A CTG layer's
fill is a cache, and the canvas only builds it for the frame on screen**,
because compositing is not allowed to start a max-flow. So a project straight
off disk has no fills at all, and an export that composited only what was
cached wrote blank colour sequences and said nothing about it — which is why
`exporting::write` takes the document by mutable reference and solves what is
missing. The price is that exporting a coloured shot for the first time pays one
max-flow per CTG layer per distinct drawing. It still solves where it stands and
at the interactive budget, which is now the *only* place that budget still binds
a final answer — see "what I would do next".

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

**Every drawing is judged, so the flags mean something.** After a solve, the
worst mark's `spread` — how much region it won for each pixel of itself — says
whether it filled a shape or only itself. `auditCtgFills` runs that over the
whole track coarsely, keeping a few bytes per drawing and no tiles, in 67 ms for
twelve 1080p drawings cold and nothing at all when none has moved. It is run a
quarter of a second after the edits stop, and on opening a project, because a
flag exists to say *which drawings to go to*: one that appears only once you are
looking at a drawing has told you nothing you did not just find out.

The flag is raised only for carried marks, and that is the point rather than a
hedge. A mark disagreeing with the solver on the drawing in front of you is
something you can watch happen; the same mark carried to the ninety drawings
after it is not. It does not wait for scribbles that move, which is the right way
round: carrying a mark unchanged under line art that has moved is precisely how
it lands wrong.

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
drawings is very ordinary. Past it, one of two things happens, and only one of
them is caught:

- With a mark in the next region too, the two contest the overlap, somebody
  loses, `spread` collapses to about 1 and the drawing is flagged. Every time,
  at exactly the displacement where it goes wrong.
- With **nothing** in the next region, the carried mark simply takes it —
  uncontested majority is a formality — and `spread` *rises*, 6.3 to 12.8,
  because the mark did fill a region and the number cannot ask which one. **The
  flag does not merely miss this case, it endorses it.** That is the design
  note's "wrong region of about the right size", and it is the one failure
  nothing here can see.

Moving the marks removes both: 400 px of movement, `spread` unchanged at 7.70,
and the neighbouring region goes from 100% wrong to none. **Where it fails is by
locking on** — matching ink is ambiguous when ink repeats, and a box with a wall
down the middle moved 200 px matched its far wall to the divider. The fill is
then exactly as wrong as carrying unchanged, which is the floor this cannot go
below, and the flag catches that one.

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
`celBounds` took the bounding box of a cel's tiles, and erasing empties a tile
without releasing it — so the solve region went on describing a mark that was no
longer there. That rectangle picks the solve resolution, so a stray scribble made
and rubbed out left every later solve on that drawing permanently coarser than
before the scribble existed. Invisibly: the region is not something you can see.
Reported as "erasing does not put the canvas back", and two better-sounding
theories were measured and dropped first — eraser residue, which the hard label
write makes impossible, and the largest-first solve order, which is deterministic
given the same seeds.

**The confidence signal the design notes propose does not work, and the
measurement is the only thing that says so.** Scoring a mark by the fraction of
it the solver labelled with the mark's own colour comes out at exactly 1 across
every case in `test_ctg`: a seed is only overruled when severing it beats
isolating it, which needs a mark that is nearly all edge. It is kept in
`CtgFill::confidence` and documented as a dead end so it is not derived a third
time. What separates is `spread`, and by a wide margin — 8.3, 17, 23, 65, 188 for
marks that landed properly, 1.82 for the tightest legitimate one, 1.00 for a mark
carried off its shape. It is not an invented quantity either: a cut that
encircles the scribble is exactly what `fr/lazybrush-et-calques-ctg.md` §5 calls
**Raccourcis**, so the flag names a failure the research had already named.

**And both numbers have to come off the solver's labels, never the finished
fill.** A mark wins its own pixels in the fill whatever the solver decided, so
read back from the fill every mark is perfectly placed, always. There is a test
pinning it, because it is the mistake the next person will make.

**A flag read from a cache only reports where you have been.** The timeline's
flags were taken from `ctgFillFor`, and a fill exists only for a drawing somebody
has visited — so they lit up behind you as you played through a shot. That is not
a weaker version of the feature, it is the absence of it: the flag exists to say
which drawings to go to. The verdicts are a separate, unevictable store precisely
because the timeline needs all of them at once and the fills it would need are
megabytes each.

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

## How to work on it

Everything in `src/core/` is free of Qt and can be tested headlessly. Everything
in `src/app/` is Qt. Keep that line: it is why the model has real tests — and it
is why the CTG solver, threads and all, is in `core` rather than in the window
that uses it. The hard part of running a max-flow somewhere else is the queue,
the superseding and the cancelling, and all of that is testable without a
display; what is left in the widget is a timer and a map.

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
dirty and emits `colourChanged`; `MainWindow` refreshes the timeline flags, the
layer panel and the status bar from that one signal.

The whole-track audit is the same path with `want_tiles` false and a lower
priority, and it keeps a `CtgVerdict` per drawing rather than a picture. The
compositor draws whatever fill is in the cache and never starts a solve —
`Document::ctgFillFor` is const for exactly that reason.

```bash
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH   # MSYS2 UCRT64, from PowerShell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/bench_composite     # timings, not a test -- including a whole CTG solve
./build/tests/bench_zoom -platform offscreen [dir]   # the whole display path
./build/tests/bench_save          # save, incremental save, open
./build/tests/bench_carry         # how far a mark survives being carried
```

`bench_carry` is a measurement rather than a stopwatch, and it is the one to run
before changing anything about carrying marks. It moves a shape a known amount
per drawing, marks only the first, and reports what the fill did on the rest:
how much of the region took the colour, how much of the world outside it did,
what `spread` said about it, and how far the solve decided the drawing had
moved. Every case runs twice, with the marks left where they were drawn and with
them following the line art, so the two are read side by side.

`bench_save` reports a full save, a full re-save, an incremental save with
nothing changed and one with a single drawing touched. The last is what autosave
actually costs and is the number to watch: if it starts tracking the size of the
shot rather than the size of the change, something has stopped carrying files
forward.

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

1. **Export solves on the interface thread, and it is now the only thing that
   does.** `exporting::write` calls `ctgFill` directly, so a coloured shot's
   first export pays a capped max-flow per drawing with the window frozen behind
   a progress dialog. Everything it needs is built: hand the jobs to
   `canvas_->colourSolver()` at `kFullSolveBudget`, collect them as they land,
   and the progress dialog becomes honest as well as unfrozen. It also fixes a
   quality bug nobody has reported yet — an exported fill is capped where the
   one on screen is not.
2. **EXR export**, the one piece of M5 deliberately left out. 16-bit PNG throws
   pixels away, so a lossless deliverable needs it; `tinyexr` is a single BSD
   header and the format list in `export_sequence.h` is where it goes.
   Everything around it — the layout, the naming, the canvas rectangle, the fill
   solving, the progress and cancellation — already exists and is tested, so this
   is a writer and a radio button.
3. **Rung three of scribbles that move**: one transform per *region* rather than
   one per drawing, from the previous fill's regions. Read
   [scribbles-through-time.md](scribbles-through-time.md) first — rungs one and
   two are built and measured, and the note now records both what they buy and
   the one way rung two fails, which is by locking onto the wrong alignment when
   the ink repeats. Rung four is the paper written for this exact problem
   (Sýkora, Dingliana & Collins, NPAR 2009) and is what to read before designing
   anything past three.
4. **Free the tiles that erasing has emptied.** A tile whose pixels are all
   cleared stays in the grid forever — it is written to saved projects and
   counted in memory. `Tile::isFullyTransparent` already exists and `celBounds`
   already ignores such tiles, so the correctness problem is gone and only the
   waste is left. The traps are the undo journal, which records tile snapshots
   by (cel, coord), and the tiles copy-on-write shares between cels.
5. **A better "wrong region" test than `spread`.** What is flagged today is "this
   mark filled nothing but itself", which is a fact. What is not caught is a mark
   landing in the *wrong region of about the right size* — and that is no longer
   a suspicion, it is measured: `bench_carry` produces it on demand, and `spread`
   does not merely miss it, it **rises**, because the mark did fill a region and
   the number cannot ask which one. "Wrong" only exists by reference to the
   drawing the mark came from, so it needs a correspondence between regions on
   two drawings, which is what item 3 would produce. Every proxy tried on paper —
   area ratio, region overlap — misfires on fast movement, which is exactly when
   carrying is most likely to be wrong *and* most likely to be right. Do not ship
   a second flag that cries wolf often enough to teach somebody to ignore both;
   wait until item 3 gives it something real to check against.
6. **GPU compositing**, if `bench_composite` says it is worth it at real
   drawing sizes rather than at the sizes tested here.
7. **The rest of the open issues**: several tracks (#1, which the timeline and
   the model are both ready for and no interface exposes), deleting every layer
   of a drawing (#2), an eraser cursor (#4), brush-resize feedback (#5), a
   non-modal colour panel (the parked half of #8), and an "overwrite drawings"
   checkbox (#9).

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
