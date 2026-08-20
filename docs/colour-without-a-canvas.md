# Colour without a canvas

A plan, not a description.

**Phases 0 to 3 are built.** Phase 4 is not, and was not skipped for lack of
time: the measurement it was always waiting on now exists and says the cost it
removes is small at any separation a person is likely to draw. The reasoning is
on [#61](https://github.com/S-poony/Animage/issues/61); what every phase
measured is in [the colour benchmarks](colour-baseline.md), run by run.

Three issues, one goal: **the colour layer stops being bounded by the canvas.**
[#59](https://github.com/S-poony/Animage/issues/59) is the shape — the fill is
the last derived thing that is still a rectangle.
[#60](https://github.com/S-poony/Animage/issues/60) and the barrier beside it
are the cost — the last two places that pay for the area of a box rather than
for the ink in it. [#61](https://github.com/S-poony/Animage/issues/61) is the
quality, and it stops being optional the moment #59 lands: the canvas clip is
what has been quietly protecting the solve's resolution from a sparse sheet, and
this plan removes it.

Read [why our own formats](why-our-own-formats.md) for why the drawing has no
edges, and [scribbles through time](scribbles-through-time.md) for what a mark
is. The traps this crosses are indexed in [handover.md](handover.md#the-traps);
the ones that matter here are named where they bite.

## What is true today

`Scene::canvas()` is `{0, 0, width, height}`, always at the origin
([scene.h:39](../src/core/scene.h)), and it reaches the solve as `CtgJob::canvas`
([ctg.cpp:98](../src/core/ctg.cpp)). Inside `solveCtgJob` it is `filled`:

| [ctg_job.cpp](../src/core/ctg_job.cpp) | what it does |
|---|---|
| 330 | empty canvas, empty fill |
| 361 | clips the shift estimate's ink area |
| 375 | clips `region`, the area actually solved |
| 439 | `built.region = filled` |
| 550-554 | the paint-out loop, one write per canvas pixel |
| 598 | clips the scribble override |

**The labelling never sees it.** The barrier, the seeds, the unseverable rim,
the max-flow, `confidence` and `spread` all work on `region`, which is drawn
bounds plus a tile of margin. The solve is already canvas-free; the *fill* is
not, and neither is the clip that decides what gets solved at all.

Two of the six are free for the taking. `built.region` is read by no production
code — only `test_ctg` (408, 452-455, 475, 534-535, 943, 953, 1002), where it
stands in for "the canvas". And the clip at 375 throws away ink the model holds:
nothing on the write path clips a stroke to the canvas, and the canvas frame is
drawn precisely because drawing past it is meant to be ordinary
([canvas_widget.cpp:1227](../src/app/animage/canvas_widget.cpp)).

Two costs are still the area of a rectangle rather than the ink in it, and both
are `ctgBarrier`, which composites at full resolution and reduces:

- Twice per solve for `estimateCtgShift`
  ([ctg_job.cpp:240-241](../src/core/ctg_job.cpp)), which is #60.
- Once per solve for the solve's own barrier
  ([ctg_job.cpp:400](../src/core/ctg_job.cpp)). #60 leaves this one alone and the
  handover records it as "not this bug and not fixed". **After #59 it becomes
  the bug**, because `region` is no longer capped by the canvas, so the one
  remaining full-resolution composite grows without limit on a sparse sheet.

## The order, and why

Four phases after a measurement. Each ends somewhere the program is shippable,
and each has a gate that is a measurement rather than an opinion.

| | | gate |
|---|---|---|
| 0 | measure what is there | `bench_carry`, `bench_composite`, `bench_playback` before anything |
| 1 | the fill stops being a picture (#59, first half) | the accessor equals the baked tiles, pixel for pixel |
| 2a | the barrier stops paying for empty paper | the barrier array is identical, byte for byte |
| 2b | the correlation stops borrowing its reducer (#60) | `bench_carry`, on a branch, no worse |
| 3 | the canvas leaves the job (#59, second half) | off-canvas ink solves and colours |
| 4 | the grid stops being uniform (#61) | a dense drawing's grid is unchanged; two patches both solve at step 1 |

Phases 0 to 3 ran their gates and are built. Phase 4 is on #61, unbuilt and with
the arithmetic that decides it written down there.

Phase 1 before phase 3 is the whole of the safety here: while the canvas clip is
still in place, the lazy fill has to answer *exactly* what the baked tiles
answered, and that is checkable against the thing it replaces. Once the clip goes
there is nothing to check against, because the answer is deliberately different.

Phase 2a before phase 3 is so that the clip is only removed once the barrier can
afford it. 2b is independent of all of this and could go anywhere; it is here
because it shares a function with 2a and because it is the one piece that might
not land at all.

## Phase 0 — measure first

Run `bench_carry`, `bench_composite` and `bench_playback`, and keep the output.
Every later gate reads against these.

## Phase 1 — the fill stops being a picture

**`CtgFill` keeps the answer instead of the picture.** The solve already holds
it: `solved.labels` over the `solved` rectangle at `step`, plus the palette.

```
struct CtgFill {
    std::vector<std::int16_t> labels;    // -1 where nothing reached
    std::vector<std::uint32_t> palette;  // label -> scribble key
    PixelRect solved;
    int step = 1;

    TileGrid marks;         // the scribbles, as they were solved from
    CtgShift carried_by;    // and how far they were moved to read them
    ...
};
```

`TileGrid tiles` and `PixelRect region` both go. `region` first and on its own —
one line and a handful of test assertions re-pointed at `solved` or at a
rectangle of their own, no design in it, and it shortens everything after.

**Two bytes a label, not one.** Two bytes is 32767 colours, which no drawing can
reach: a mark is written hard, so each stroke lays one flat colour and a drawing
would need thirty-two thousand distinct ones. One byte would be 127 and would
need a rule for what happens at 128 — a cap that fails is a cap somebody has to
design a failure for, and the memory it saves is not needed. See the cache
below.

**The marks travel with the fill, and the shift with them.** #59 proposes
reaching for `Document::ctgShiftAt` at composite time. Carrying both on the fill
is better and is barely more: a `TileGrid` copy is handles, and a run of
drawings inheriting one scribble cel shares one set of pixels. It means reading
a fill needs nothing but the fill — no document, no lookup, nothing to forget —
and the handover's rule that a derived value which changes what is drawn has to
be reachable by everything that draws it is then satisfied by construction
rather than by a fourth reader remembering to ask.

**The accessor is `solvedIndex`, moved and evaluated per pixel asked for.** Its
justification is already canvas-free
([ctg_job.cpp:313-328](../src/core/ctg_job.cpp)): outside the drawn area there is
no line art, so everything out there is one connected stretch of blank paper; a
cut cannot pass through it and it can only take one label; whatever label reaches
the region's border is the label of everything beyond it. Nothing in that
argument names a rectangle. The canvas was only ever where somebody stopped
writing.

Two functions, in `core` and Qt-free:

```
Rgba ctgFillPixel(const CtgFill&, int x, int y);
void ctgFillSpan(const CtgFill&, int y, int first_x, int stride, int count, Rgba* out);
```

`ctgFillPixel` is the reference and the thing tests read. `ctgFillSpan` is what
the compositor uses and is where the constant is won: the coarse row is worked
out once, a run of `step` image pixels is one label and therefore one colour, and
the marks are then overwritten a tile at a time with the tile lookup hoisted
across the run — the same shape `blendLayerRows` already uses, and for the same
reason.

**The scribble override moves here, through the same shift as the seeding.** Not
a detail: the two are the same statement about the same mark, so a seed read in
one place and an override painted in another puts the mark's own pixels
somewhere the solver never saw it
([ctg_job.cpp:583-589](../src/core/ctg_job.cpp)). Both now read `fill.marks`
through `fill.carried_by`, in one function.

> **And this is the invariant most at risk in the whole plan, so it gets a test
> of its own.** *A mark shows its own pixels whatever the solver decided, at full
> resolution, however coarse the solve was.* It is what lets somebody scribble
> into a region too narrow for the solve to spread through and still see the
> colour they asked for, and it is what leaves a mark visible on a drawing whose
> line art has gone — a ball that vanishes mid-canvas takes the fill with it and
> leaves the scribble. That guarantee is currently a consequence of the override
> being baked last, at full resolution, into the tiles
> ([ctg_job.cpp:601-607](../src/core/ctg_job.cpp)). Moving it to composite time is
> exactly the change that could lose it — most plausibly by the coarse label run
> overwriting the mark rather than the other way round, or by the mark being
> sampled on the reducing path's lattice and falling between two samples. So:
> a `test_ctg` case with a mark on a drawing that has no line art at all, and one
> in a region narrower than `step`, both asserting the mark's own pixels come
> back at full resolution.

**One precomputed fact buys back the absent-tile shortcut.** Today an area the
fill left transparent has no tile and the compositor skips it before reading a
channel; that is the property the whole compositor is built on. A lazy fill has
no absent tile. But outside `solved` every answer comes from the clamped ring,
and on an ordinary drawing that ring is the background — label -1, transparent.
So the solve records one bool: *is every label on the clamped ring -1*. When it
is, everything outside `solved` is transparent and `ctgFillSpan` says so in one
test rather than per pixel. It is exact and not an approximation: it is a fact
about the labels that were computed.

**The compositor learns a second kind of source.** `LayerPass` gains
`const CtgFill* fill`, and `collectPasses` pushes that instead of `&fill->tiles`
([compositor.cpp:451](../src/core/compositor.cpp)). `blendLayerRows` and
`blendLayerRowsBoxed` get twins that fill a scratch row from `ctgFillSpan`
instead of walking tiles, and are otherwise the same arithmetic — layer opacity
included, since a colour layer has one like any other.

> **The trap here does not apply, and it is worth saying why rather than being
> careful in the dark.** [Which rectangle counts the columns, and which sizes the
> buffer](handover.md#which-rectangle-counts-the-columns-and-which-sizes-the-buffer)
> was a one-past-the-end *write* on the reducing path with a non-zero
> `LayerPass::offset`. A lazy fill is drawn where it is — the shift is applied
> inside the accessor, when the marks are read — so a fill pass carries
> `offset = 0` and reuses the ordinary column plan unchanged. The corner is not
> entered rather than being guarded against.

> **And one constraint neither issue mentions.** `compositeGrids` runs its bands
> on several threads over one pass list, so a fill is read concurrently by all of
> them. The accessor has to be a pure function of what the fill stores. That
> rules out the obvious first design — materialise tiles on first touch and keep
> them — which would need a lock on the path that is being made faster.

**The cache is re-counted in bytes.** `CtgFillCache` budgets in
`fill.tiles.tileCount()` and there are no tiles. Bytes is the quantity that was
meant, which is the same lesson as [what a band counted in coarse rows really
costs](handover.md#what-a-band-counted-in-coarse-rows-really-costs). The
footprint is the labels and the palette; the marks are *not* counted, because
they are handles shared with the cel and with every other drawing inheriting it,
so charging the cache for them would charge it for memory it usually does not
cause.

The arithmetic, which is the part to argue with rather than the constant:

| | today | after |
|---|---|---|
| a 1080p fill | 135 tiles, ~17 MB | ~2.07M labels, ~4 MB |
| a 4K fill, solved at half | ~540 tiles, ~68 MB | ~4.2M labels, ~8 MB |
| what 256 MB holds | ~15 drawings at 1080p | ~64 at 1080p, ~32 at 4K |

That is aimed straight at what `bench_playback` reports: 48 of 48 fills survive
an HD shot today, 62 of 192 across four tracks, and 20 of 48 at 4K. The budget
stays at 256 MB and buys about four times as much of a shot. It is still a budget
and it will still express itself as a threshold somewhere — the somewhere is how
far back along the timeline you can jump before a fill is solved again.

**`want_tiles` stops meaning what it says.** There is no paint-out loop to skip,
so it becomes "keep the labels, or only the verdict". Worth keeping — it is the
shape a whole-track pass needs, and `CtgSolver`'s second priority is still there
for one — but the comments at `ctg_job.h:137` and in `ctg.h` claim a saving that
no longer exists.

### The gate

Keep `CtgFill::tiles` and the paint-out loop for exactly one commit, and assert
`ctgFillPixel(fill, x, y) == fill.tiles.pixel(x, y)` over the canvas across the
`test_ctg` fixtures. Green, then delete the loop and the field.

This is the strongest check available anywhere in the plan and it costs one
commit's worth of dead code. #59 says the colour assertions "should hold pixel
for pixel"; this is the difference between that being a hope and being a thing
that ran.

Then `bench_composite` and `bench_playback` again, and read the coloured rows. If
the coloured frame at HD gets worse, `ctgFillSpan` is doing too much work per
pixel and wants a better run-length, not a rethink.

## Phase 2 — the ink stops paying for the paper

Two changes to one function, and they are independent of each other.

### 2a — composite only where there is something to composite

`ctgBarrier` works a band at a time and composites every pixel of every band,
whether or not any source has a tile there. So an empty region costs the same as
a drawn one — about 0.4 s a call on a large drawing, three calls a solve — and
`compositeGrids` writes each band twice before reading anything, once in
`resize`, which assigns every element, and once in `clear`.

**Skip what has no tile under it.** This is exact rather than an approximation,
and the reason is worth stating plainly because it is what makes the change
small: `intensity` starts at 1.0, which is bare paper, and a run of pixels with
no tile under it composites to fully transparent, which reduces to exactly 1.0.
Skipping it and compositing it produce the same array. A tile that exists but
holds nothing is composited anyway and costs nothing but time.

**Skipped in both directions, not only by band.** A whole-band test alone buys
everything on two patches stacked vertically and nothing at all on two side by
side, since every row then has ink somewhere in it — and nothing on a long
diagonal, which is an ordinary thing to draw. So within a band, composite the
runs of columns where some source has a tile in that band's tile rows, and leave
the rest. That is a loop over tile columns, and it is still `compositeGrids`
doing the flattening: the `over` blending of several sources and the antialiased
rim that lets a boundary sit *inside* a line stay in the one place that already
knows how to do them.

The cost then follows the ink rather than the box round it, which is what makes
phase 3 affordable.

### 2b — the correlation stops borrowing the barrier's reducer (#60)

`estimateCtgShift` builds both level-zeroes with `ctgBarrier` and inherits its
reduction, which takes the **most** covered pixel in a block. Every level above
it is built with `halve`, which **averages** — and which names the barrier's rule
as the opposite one in its own comment
([ctg_job.cpp:164-168](../src/core/ctg_job.cpp)): a barrier must not lose a thin
line, because a hole in it is a fill pouring out; a correlation wants the ink to
weigh what there is of it, so that half a line under a cell counts half.

Only one of them can be right for a correlation, and the code argues for
averaging. So the reduction becomes an argument:

```
enum class InkReduce { Most, Mean };
std::vector<float> ctgInkCoverage(const std::vector<TileGrid>& sources,
                                  const PixelRect& region, int step, InkReduce);
```

Coverage rather than intensity — 0 is bare paper — so `ctgBarrier` is
`1 - ctgInkCoverage(..., Most)` and `estimateCtgShift` stops flipping it back.
Both reducers decompose across bands, which is what the banding needs: `Most`
accumulates with `max` into an array of zeros, and `Mean` accumulates a sum and
divides by the cell's own pixel count at the end, so a cell finished by two bands
is the same answer as one finished by one. Cells no band touched stay at zero,
which is correct for both.

**This changes what the search finds**, most at large `step`, where a `max`
reduction makes any cell containing any ink read as solid ink. It is not a
refactor and the measurement is the point of it.

### The gate

**For 2a: identical output, and then a stopwatch.** The barrier array must be
unchanged, byte for byte, on every `test_ctg` fixture. `test_ctg` already reads
the same ink over a narrow region and a wide one and requires them to agree
exactly; that test carries over and a new one compares against the unskipped
implementation directly while both exist. Then `bench_composite`, which times a
whole solve.

**For 2b: `bench_carry`, the whole table.** Coverage, leak, `spread` and the
shift reported per drawing, with marks carried and with them left where they were
drawn. `test_ctg:1491-1499` calls `estimateCtgShift` against known shifts and is
the fast check.

Speed is not the risk in 2b — there is no version of it that is slower. What is
at risk is *where carried marks land*. **2b goes on a branch and does not reach
`main` while any of those numbers is worse than phase 0 recorded**, and a worse
number is a thing to explain rather than to tune around: it would say the `max`
reduction was doing something for the correlation that nobody wrote down, and
that should be written down before it is changed. 2a has no such risk and can go
straight in.

## Phase 3 — the canvas leaves the job

`CtgJob::canvas` is deleted and `filled` with it.

- `region` is the drawn bounds of the shifted marks and the sources, plus a tile
  of margin, and nothing clips it. The empty-region early-out stays: nothing
  drawn is still an empty fill.
- The shift estimate's `area` is the union of both drawings' bounds, unclipped.
- `ctgInputsFor` stops mixing the canvas width and height
  ([ctg.cpp:76-78](../src/core/ctg.cpp)), so resizing the canvas stops throwing
  away every fill in the document.
- `Scene`'s comment says the canvas is "what a colour fill is bounded by" and
  `kMaxCanvasSide`'s says "the CTG solve and the composite are both bounded by
  the canvas". Both become false. The composite still is, at export.

**What a user sees change.** Colour goes past the frame: a shape running off the
edge is coloured out there too, under the veil, and a ball animating off-screen
keeps its colour instead of losing it at the frame line. Export is unaffected — it
composites the canvas and always did.

**And what it costs.** The box round the ink is what picks the solve's
resolution, so ink far off the frame now coarsens the whole drawing. Time and
memory do not run away — the cell budget still caps both, and after phase 2a the
barrier costs what the ink costs — so what is lost is sharpness and nothing else.
That is #61, and it is why #61 is in this plan rather than waiting for somebody
to measure that people draw two things far apart.

### The gate

`theFillCoversTheCanvasAndStopsThere` is rewritten to say the opposite, and
deliberately: a box that runs off the right-hand edge is now coloured out there.
The canvas-resize-invalidates-the-cache assertion goes with the hash entry it was
pinning. New: a mark drawn entirely outside the canvas colours a shape entirely
outside the canvas, which today does nothing at all.

## Phase 4 — the grid stops being uniform

`solveCtgJob` solves `region` at a uniform `step`, raised until the cell count
fits the budget. So the resolution the drawing is solved at is set by the *box*
round the drawing rather than by the drawing, and after phase 3 that box is
whatever you drew on.

**The cheap fix is a gap cap, and #61 is right that it must not be built.**
Clustering the ink and solving a box per cluster makes a clustering radius a hard
gap cap: ink further than `R` apart goes into different sub-problems, so no cut
across that gap is representable and no scribble of any size bridges it. That is
the priced border wearing a different hat, and [there is no value that avoids
it](handover.md#the-background-seeds-that-failed-and-the-rim-that-cannot-be-bought).
Three more objections behind it, in #61.

**What to build instead is a grid whose spacing is uneven**, and it is not a
compromise for being smaller than the mesh #61 sketches. It is the best answer
available to anything that keeps `GridFlow`, and keeping `GridFlow` is a
performance decision about the common case rather than a convenience — see
[what this is not](#what-this-is-not-and-when-to-reach-past-it) below.

Keep the grid **rectangular and four-neighbour** and let only the *spacing* vary:
column `i` covers image columns `[X[i], X[i+1])` and row `j` covers
`[Y[j], Y[j+1])`. `GridFlow` is untouched. `LazyBrushProblem` is still a dense
`width x height` grid. Connectivity is preserved exactly, so there is no gap cap
and no new knob, and resolution is lost only where there was nothing to resolve.

- **Choosing the spacing.** A tile-column is occupied if any source or mark tile
  covers it — the profile is a walk over tile coordinates and costs nothing.
  Occupied tile-columns are cut at the uniform step. The same for rows. On a
  drawing that fills its frame every column is occupied and the grid is exactly
  today's.
- **A run of empty columns is graded, not collapsed.** Widths go
  `1, 2, 4, 8, ... 8, 4, 2, 1` times the step across the gap rather than becoming
  one enormous cell. That costs about `2*log2(gap)` cells — twenty-seven for a
  gap of ten thousand, which is nothing — and it is what stops three things going
  wrong at once. A cut cannot pass *through* a node, so a single merged cell
  makes cutting across a wide blank corridor unrepresentable and snaps the colour
  boundary to the corridor's end; grading leaves the middle of the gap with cells
  to cut between, coarse ones. It keeps neighbouring cells within a factor of two
  of each other rather than reaching aspect ratios of thousands to one. And gap
  tolerance survives quantisation exactly, because with edge costs scaling by
  shared length, crossing a coarse region costs the same total length as crossing
  a fine one — `n < lambda*|S|` is preserved, which is the one thing all of this
  must not break.
- **Every anisotropic cell is provably blank**, which is what makes the previous
  point safe. If a tile exists at column `c` and row `r`, then both are occupied,
  so cell `(c, r)` is fine in both axes. A cell that is coarse in either
  direction contains no tiles at all — so the stretched cells only ever sit where
  there is nothing to see, and none of the staircase artefacts anisotropy usually
  brings can land anywhere visible.
- **The capacities are where the bug would be**, and #61 says so. `k` is the
  perimeter and has to be counted in a nominal unit rather than in cells; a
  seed's strength is `lambda*K` per *pixel* of scribble, so a cell's terminal
  capacity scales with its area; and a boundary's cost scales with the shared
  edge length between two cells. A uniform grid has to reproduce today's numbers
  exactly, and that is a test rather than a hope.
- **Reading the fill needs a cell lookup**, since `solvedIndex` currently divides
  by `step`. A binary search over the boundary array, hoisted per span rather
  than per pixel — eleven steps at the largest grid the budget allows.

### The gate

Two tests and a measurement.

- **A dense drawing's grid is exactly today's.** Everything occupied means every
  boundary falls on the uniform step, so nothing about an ordinary drawing moves.
  This is what confines any capacity error to sparse sheets, where `test_ctg` and
  `bench_carry` are then the check.
- **Two patches far apart both solve at step 1.** 500x500 each, 10000 px apart: a
  uniform grid is 110M cells and is solved at step 6; the uneven one is about
  1000x1000 plus twenty-seven graded columns, and is solved at full resolution.
- `bench_carry` and `bench_composite` unchanged on the dense cases.

### What this is not, and when to reach past it

Written down because "why not the obvious better thing" is the question this
will be asked, and the answer is a ranking rather than a preference.

**Why uneven spacing is the best answer and not the cheap one.** Any scheme that
keeps `GridFlow`'s implicit four-neighbour grid *must* be separable. The grid is
a rectangular lattice indexed `j*W+i` with neighbours at `±1` and `±W`; if cells
are to tile the region and adjacency is to match geometry, cell `(i,j)` has to be
`[X_i, X_i+1) x [Y_j, Y_j+1)`. Uneven `X` and `Y` is the whole of the freedom
there is. So this is optimal in its class, and the class is the one where an
ordinary drawing stays as fast as it is today.

**Where it is genuinely worse than optimal.** Separability degrades as `N^2` for
`N` clusters sharing neither rows nor columns, against `N` for a true adaptive
mesh: ten scattered patches of 200 cells each is about 4M cells here against
400k there, a factor of ten. For one to three clusters — a character, a prop, a
background element — it is within a small constant of optimal. Whether a drawing
ever has ten is the measurement #61 says gates this work, and nobody has made it.

**The general adaptive mesh** is what #61 sketches and it is the next rung, not
the answer this replaces. It is optimal in cell count always. It costs
`GridFlow`'s implicit grid, `LazyBrushProblem` ceasing to be a grid,
`labelUncontestedRegions`' flood fill needing neighbour lists — and a constant
factor on *every dense drawing*, which is the common case, unless two solvers are
kept. Note that it needs the *same* area-and-edge-length capacity scaling this
does, so building the uneven grid first derisks the hardest part of it and leaves
an exact identity to test it against. Going straight there introduces the
capacity arithmetic and the adjacency change together, with nothing to check
either against.

**Banded refinement** — solve coarse, then re-solve at full resolution only in a
band around the cut — is orthogonal to all of this and is possibly the larger
prize, because it ignores clusters entirely and puts resolution where the
boundary actually is. It is what would let a 4K drawing solve at full resolution.
It is also **approximate**: a coarse cut that misses a feature cannot be
recovered by refining around it locally. What this project has repeatedly decided
it fears is a confident wrong answer rather than a slow one, so it wants its own
issue and its own measurement, and it does not replace any of the above.

## What is deliberately not done

- **No clustering, no gap cap, no border price.** See phase 4 and #61. This
  codebase has now refused the same shape three times and the reason is written
  down each time.
- **The fill is still never stored.** It is derived, and saving or propagating it
  would be storing the one thing the layer exists in order not to store.
- **No GPU.** The max-flow stays on the CPU, so nothing here waits on item 4 of
  the handover's queue and nothing there answers this.
- **Nothing tells the user the fill is coarse.** `CtgFill::step` knows, and after
  phase 3 there are drawings where it will be large for a reason a person could
  act on. The transferable half of
  [#40](https://github.com/S-poony/Animage/issues/40) is that nothing on screen
  was a function of the thing about to go wrong — but a message is interface
  scope and belongs to whoever designs it. Worth an issue once phase 4 says how
  often it would fire.
