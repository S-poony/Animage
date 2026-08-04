# Handover

Written at the end of the first build, for whoever picks this up — including a
later me with none of the context. It covers what exists, what is deliberately
not here, the traps that cost the most time, and what I would do next.

The French documents in [fr/](fr/) are still the specification. This file only
records what happened when it was built.

## Where it got to

M0 through M4 exist. **M5 does not: nothing can be saved.** That is the largest
gap and the obvious next job — a session's work is lost when the window closes.

| | |
|---|---|
| M0 | Pen latency measured at ~15 ms, passed. [m0-latency.md](m0-latency.md) |
| M1 | Data model, copy-on-write tiles, undo. All five plan tests pass. |
| M2 | Canvas, pressure brush, eraser, layers. Compositing is on the CPU, not the GPU. |
| M3 | Timeline, holds, onion skin, playback, drawing during playback. |
| M4 | LazyBrush solver and the CTG layer, in the app and usable. |
| M5 | **Missing.** No save, no load, no export. |

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

**Caches must be sized by the window, not by the drawing.** The composite cache
was sized from the visible *image* area, so at 5% zoom it asked for half a
gigabyte, and its margin was measured in image pixels, so it grew as you zoomed
in. Both are fixed; the shape of the mistake is worth remembering.

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
```

`test_canvas` drives the real widgets offscreen and can send tablet events; it
is where interface bugs get caught. A crash writes `animage-crash.txt` beside
the executable, with a stack that `addr2line` decodes:

```bash
addr2line -e build/src/app/animage.exe -f -C 0x14000a130
```

Add the PE image base (`0x140000000`) to the offsets in the report.

## What I would do next

1. **Saving.** M5, and the largest gap by far. A folder with `scene.json` and a
   PNG per cel, per the plan. A CTG cel saves its scribbles; the fill is derived
   and should never be written. The canvas rectangle exists now
   (`Scene::canvas()`), so export has a size to write; before it, every frame
   would have come out as its own bounding box and no two the same.
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
