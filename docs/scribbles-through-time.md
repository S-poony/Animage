# Scribbles through time

Design notes for two things, in this order:

1. **A scribble stays** from one drawing to the next until the user changes it.
2. **A scribble moves** to follow the animation.

**Part 1 is built. Part 2 is built to its fourth rung, and the third is the one
that runs.** Rung 3 — one translation per region — is on by default. Rung 4 —
the paper's as-rigid-as-possible lattice — is built, measured and **off**, but
the margin is much narrower than it was: most of what kept it there was a match
score it should never have had, and with the paper's own score it now wins some
of the drawings rung 3 loses and loses some rung 3 wins. Which is better is a
question for somebody colouring a shot with each. Rung 5 is still research.

Rung 3 has moved since this was written, because the fill it would read has: see
the note under [estimating the transform](#estimating-the-transform). This
document was written straight after the single-scribble change, while the solver
was still in hand, because a good deal of it is a consequence of decisions
already made rather than free choices. Where building either part contradicted
it, the original text is kept and the correction is marked **Built:** or
**Measured:** underneath — a design note that quietly agrees with whatever
happened is no use to anybody reading it before doing the rest.

> **Since built, and it is the sentence to read before any of the rest.** Every
> rung above the second failed the same way and it took three shots to see it:
> **the way a shot is scribbled and the rung that carries it are one thing, not
> two.** The same shot coloured twice, once with marks placed for rung 2 and
> once for rung 3, ranks the two rungs in opposite orders. So a rung cannot be
> scored on marks placed for a different one, and "which is better" is not a
> question with an answer until somebody says which way they scribble. See
> [what a hand says about a rung](handover.md#colour-through-time-part-three).

The first was small, was not blocked by anything, and is most of the plan's
"onion fill" hypothesis. The second is a research problem with a cheap first
rung.

## Why this is cheaper here than in TVPaint

Two properties of the model do real work, and both are worth knowing before
reading anything else.

**Layers belong to the track and timing belongs to the image.** So a CTG layer
and the line art it is cut against are always exposure-synchronised: they are
held for the same frames because they are held by the same image. "The previous
drawing" is therefore unambiguous. In TVPaint and Toon Boom each layer carries
its own exposure, so the previous drawing of the colour layer and the previous
drawing of the line art need not be the same moment, and every feature like this
has to say which it means. Here the question does not arise.

**The fill is derived and never stored.** Only scribbles have to survive; the
fill regenerates. Nothing needs migrating, and a mistake costs a recompute.

## Part 1 — scribbles that stay

### The model change

Sparse absence already means something for a raster layer: the layer is empty on
that drawing. For a CTG layer it should mean **inherited**.

Reading the scribble cel for image *N*: if `Image::celFor(layer)` is absent, walk
back through `track.slots` from *N* and take the first earlier image that has
one. One function beside `Document::celAt`, and `ctgFill` uses it instead of
`record->celFor(layer_id)`.

Walk `slots`, not `images` — `slots` is the order in time, `images` is a hash —
and over *distinct* ImageIds, so a drawing held for five frames is one step back,
not five. `Track::distinctNeighbours` already does exactly this walk for onion
skin; use it rather than writing a second one.

### What "unless the user changes them" means

The first stroke on a CTG layer at image *N* copies the inherited cel into *N*
and edits the copy. Copy-on-write makes that a copy of tile handles, not of
pixels — the same mechanism that makes `duplicateImage` cheap. From then on *N*
is what *N+1* inherits.

The whole edit is in `Document::celForWriting`, which today creates an *empty*
cel when one is absent. For a CTG layer it should create one seeded from the
inherited cel. Two consequences fall straight out:

- Erasing an inherited mark works normally, because you are editing your own
  copy of it. No "delete inherited scribble" concept is needed.
- "Revert to inherited" is `clearCel` — it already restores sparse absence, and
  absence now means inherited. It needs a menu item and nothing else.

### The cache bug this will introduce

`ctg_cache_` is `unordered_map<CelId, CtgFill>`, keyed by the **scribble cel id**
(`document.h`, `Document::ctgFillFor`). Today one image has one scribble cel, so
that key is a bijection. Under inheritance, *N* images share one cel and the key
collides.

It will not serve a wrong fill — `inputs` mixes the *source* cels' revisions,
which differ per image, so a collision invalidates rather than lies. It will
thrash: one cache slot fought over by every drawing in the run, re-solving on
every frame change, and onion skin re-solving per neighbour per frame. On a
capped 512×512 solve that is about 120 ms each.

Key it by `(ImageId, LayerId)`. This is the change most likely to be missed,
because nothing fails — it just gets slow, and slowly.

> **Built:** the key is `(ImageId, LayerId)`, but the reasoning above is wrong in
> the direction that matters. It *would* have served wrong fills. Source cel
> revisions do not reliably differ per image: a revision counts writes to one
> cel, so two drawings inked with the same number of strokes sit at the same
> number, and **every cel in a project straight off disk is at revision 1**. Two
> drawings inheriting one scribble with equally-worn line art would have swapped
> answers silently.
>
> For the same reason `inputs` now mixes the scribble *cel's id* and not only its
> revision. Reordering drawings changes which cel is read and moves no revision
> anywhere, so a key made of revisions alone goes on serving the colour from
> whichever drawing used to precede this one. There is a test with the two
> scribbles drawn identically, so only their identity tells them apart.
>
> The cache also had to be bounded. A fill covered the canvas — 135 tiles at
> 1080p, about 17 MB — and before inheritance it took scribbles of your own to
> get one. Every drawing has one now, so playing a coloured shot through once was
> a gigabyte. Least recently used; evicting costs a recompute, which is the whole
> reason the layer stores marks instead of pixels.
>
> **Since built:** a fill is no longer a picture and the budget is no longer
> counted in tiles. It is the labelling — about 4 MB at 1080p — and the budget is
> a quarter of a gigabyte of those. Same budget, four times as much of a shot.

### Undo, deletion, reordering

- Creating the cel on first edit is a `CelAssignOp`. Already the mechanism; use
  it rather than inventing one.
- Deleting the drawing that owned a scribble leaves later drawings inheriting
  from whatever now precedes them. That falls out of resolving at read time. The
  cel itself survives as long as the history references it, because the undo
  stack counts as a reference and cel ids are never reused — both already tested.
- Reordering drawings changes who inherits from whom, correctly and for free.

**Do not store a "parent scribble" pointer per image.** Every reorder and every
deletion would invalidate it, and the bugs would be intermittent. Resolve at read
time; it is a short walk over an array.

### What it buys beyond convenience

This is most of the plan's second hypothesis — "onion fill", one soft scribble
covering several superimposed intervals at once, which the design notes expect to
be the argument for the whole program. With inheritance, colouring the first
drawing of a run colours the run. That is the hypothesis, tested by using it.

### Showing it

An inherited scribble has to look inherited or nobody will trust it. The timeline
already draws the distinction that matters — a numbered card is a drawing, an
unnumbered one is the same drawing still showing — and this is the same idea one
layer down. The minimum is a per-drawing indication of "own scribbles" against
"inherited", in the timeline card or the layer panel's Marks column.

Cheap, and load-bearing: the feature is invisible when it works, so the only way
to know it is working is to be told.

> **Built:** both. A blue bar under the number on the timeline card where the
> colour was carried rather than drawn, and an arrow before the layer's name in
> the panel with the drawing it came from in the tooltip. The two say the same
> thing at different scales — the timeline across time, the panel about the
> drawing you are standing on.
>
> Note what this half does *not* wait for: whether marks were carried is a walk
> over the slots and costs nothing, so it is drawn for every drawing always,
> where the flag beside it needs a solve. The two are shown together and are
> computed nothing alike.

### Tests worth writing

- Image 3 with no scribble cel solves using image 1's scribbles.
- Drawing on image 3 leaves 1 and 2 untouched, and 4 onwards follow 3.
- Clearing image 3's override returns it to inheriting.
- Deleting image 1 leaves 3 inheriting from whatever now precedes it.
- Reordering drawings changes inheritance, without touching a cel.
- Adding a CTG layer to a 500-image track still allocates nothing. This is an
  existing invariant with a test behind it; inheritance must not quietly break
  it by materialising anything.
- Solving image 2, then 3, then 2 again does not re-solve. This is the cache
  key, and it is the only test that would have caught it.

## Part 2 — scribbles that move

### The registration barely has to work

A soft scribble obeys the rule of majority: the region takes the colour with the
greater share of its pixels inside. So a propagated scribble needs only **most of
its pixels in the right region** — not an accurate alignment. That is a far
weaker requirement than registration for compositing, and it is why the cheap
methods are worth exhausting before the expensive ones. It is also what the
paper's own patch pasting (§5) relies on.

### A constraint the single-scribble change added

Gap tolerance is now `n < λ·|S|`: the widest hole a fill will bridge is
proportional to the **area** of the scribble. That is new, and it changes the
economics of the obvious first method.

Any propagation that shrinks a scribble to be safe — erode it so it cannot
overhang a line — pays for the shrinking in gap-bridging power, on exactly the
drawings where the artist was least careful. So when measuring erode-and-carry,
measure both things: whether the carried scribble lands in the right region, and
whether it still bridges the holes it needs to. The second will bite first.

Corollary: prefer moving a scribble whole to shrinking it in place.

### The cheapest thing that could work: a transform, not new geometry

Scribbles are raster tiles today. The plan originally specified vector strokes
(`Scribble { colour, hard, strokes }`); the implementation deliberately went
raster, because there is no scribble tool — you draw with the ordinary brush, and
that is a better design than the plan sketched. Everything past "carry it
unchanged" usually wants per-region entities with a transform, which is a
data-model fork and expensive to reverse.

There is a middle path that needs no fork. Keep the raster cel and add **an
affine transform per (image, layer)**, applied when an *inherited* scribble is
read. `ctgFill` samples scribbles with `scribbles->pixel(...)` while building the
seed grid; sampling through an inverse transform is a small, local change in one
loop. The transform is six floats, undoable like any other layer property, and
nothing about storage moves.

> **Built, and not stored.** The local change in one loop is exactly what it
> turned out to be — two of them, the seeding and the override. But the
> transform is worked out inside the solve from the two drawings' line art
> rather than kept anywhere, because a stored one would be a second piece of
> derived data to keep in step with drawings that move, and it would have to be
> invalidated by everything that already invalidates the fill. It is a
> translation in whole pixels rather than an affine in six floats: a mark needs
> most of its pixels in the right region and nothing finer, so anything more
> precise is accuracy nothing reads.

That buys the whole of one-transform-per-drawing without committing to the fork.
Take the fork when per-*region* motion is genuinely needed — and take it
knowingly, because it is the expensive decision in this document.

### Estimating the transform

The previous drawing's solved fill already gives regions for nothing: it is a
tile grid of labels, one per colour. For each region, take its bounding box and
search translations that minimise a distance between drawing *N*'s ink and
*N+1*'s ink over that box.

`ctgBarrier` already produces exactly the ink-coverage raster both sides need,
at whatever resolution the solve is running at, and it already downsamples
conservatively. Reuse it; do not write a second rasteriser.

> **Since built, and rung 3 starts somewhere else now.** The paragraph above is
> the right idea against the wrong two objects, because
> [colour without a canvas](colour-without-a-canvas.md) changed both of them.
> Read this before writing any of rung 3.
>
> **The previous drawing's fill is no longer a tile grid of labels.** It is
> `CtgFill::labels` — one `int16_t` per solved cell, row-major over
> `CtgFill::solved` at `CtgFill::step`, `-1` where nothing reached — with
> `CtgFill::palette` turning a label into a scribble key. That is *better* for
> this than what the paragraph assumed: regions are already the thing stored, so
> a per-region bounding box is a walk over the label array rather than a
> re-derivation from pixels. Remember the two conversions, because nothing else
> will remind you: a cell `(cx, cy)` covers image pixels
> `[solved.x + cx*step, solved.x + (cx+1)*step)`, and the answer outside
> `solved` is the label on the ring clamped one cell inwards, not a lookup.
>
> **`ctgBarrier` is no longer the raster to reuse.** The advice survives — do not
> write a second rasteriser — but the function to call is `ctgInkCoverage`, and
> the argument that matters is `InkReduce`. `ctgBarrier` is `1 - Mean`'s
> opposite: it reduces by the *most* covered pixel in a cell, which is what a
> barrier must do because a hole in it is a fill pouring out. A correlation
> wants `InkReduce::Mean`, so that half a line under a cell counts half. Rung 2
> was built the wrong way round on exactly this and it changed what the search
> found — see the plan's phase 2b for the measurement.
>
> **Nothing clips a region any more.** The solve is the drawn bounds of the marks
> and the ink plus a tile of margin, and that is all. For rung 3 this is the
> point rather than an obstacle: a per-region box is small even when the regions
> are far apart, so each gets a fine step of its own where one global translation
> gets the coarse step of the box round everything. That is the whole of the
> failure a user reported against rung 2 — draw something a long way off and the
> carrying of a mark somewhere else changes — and rung 3 is what fixes it, not
> rungs 4 or 5.
>
> **And two traps that are the same trap, both paid for in this function.** A
> threshold on a reduced quantity has to name the unit it is counted in: the
> "nothing to match" guard was a count of inked cells, the reduction changed
> under it, and it silently became "fewer than step² pixels of ink". And a step
> taken from a region's longer side alone leaves the shorter side with no grid:
> past about twenty-four to one the search abandoned itself outright. Both were
> unreachable while the region was clipped to the canvas and ordinary once it was
> not. Any new correlation that reduces to a grid inherits both. See
> [what a threshold meant before the thing under it changed](handover.md#what-a-threshold-meant-before-the-thing-under-it-changed).
>
> **A fill now carries the marks it was solved from and the shift they were read
> through**, as `CtgFill::marks` and `CtgFill::carried_by`. So the third rule
> below — everything that shows a mark has to be told where it went — is
> satisfied by construction for anything reading a fill. `Document::ctgShiftAt`
> is still there and is still what the Marks column reads, because showing the
> scribbles does not go through a fill.

Order of attack, cheapest first:

1. **Carry unchanged.** Part 1, and nothing else. On twos and threes it may
   simply be enough — measure how often before building anything else, because
   the answer decides whether the rest is worth it.

   > **Measured, in `bench_carry`.** A shape that moves a known amount per
   > drawing, a mark made on the first drawing only, and the fill read off each
   > drawing that inherits it.
   >
   > **The rule is half the width of the region.** A carried mark holds its
   > region while the majority of it is still inside — that is not a new rule,
   > it is the soft-scribble majority rule applied to a mark that has not moved
   > — so a mark scrawled across the middle of a region survives displacement of
   > about half that region's width and fails just past it. On a 150-wide region
   > it was right at 60 px and wrong at 80. Nothing degrades: it is the same
   > fill until it is a different one.
   >
   > **Which way it fails depends on the neighbour, and the number can only see
   > one of the two.** Where the next region has a mark of its own, the two
   > contest the overlap and the loser's region goes uncoloured or takes the
   > wrong colour — and `spread` collapses to about 1 at exactly the
   > displacement where it goes wrong. Where the next region has *no* mark of
   > its own, a carried mark overlapping it by any amount at all takes it:
   > uncontested, majority is a formality. 100% of the neighbouring region,
   > wrong, from the first drawing of motion — and `spread` **rises**, from 6.3
   > to 12.8, because the mark did win a region, just not the one it was asking
   > for. A flag was built on this number and has since been removed; see
   > docs/handover.md.
   >
   > That is the open question at the bottom of this document, measured: the
   > wrong region of about the right size is real, it is the ordinary
   > consequence of a wall sliding across a mark, and no quantity read off one
   > drawing can see it. It needs the correspondence that rung 2 has to build
   > anyway.
   >
   > **So it is worth building.** Half a region's width is a very ordinary
   > amount of movement between two drawings — a hand on twos crosses far more —
   > and the failure is not a soft degradation but a whole region taking the
   > wrong colour. What carrying unchanged does buy is everything that barely
   > moves: held drawings, backgrounds, and the slow parts of a shot, where it
   > is exactly right and costs nothing.
2. **One translation for the whole drawing**, from coarse correlation of the two
   barriers. Cel animation mostly translates between consecutive drawings, so
   this buys a lot for very little.

   > **Built, and it is the default.** `estimateCtgShift`, and it is as cheap as
   > this hoped: 19.7 ms on a 1920x1080 drawing against 129 ms for the coarse
   > solve it precedes, so a carried mark costs about a seventh more than one
   > that was drawn where it is.
   >
   > It is **derived and never stored**, which is a departure from the sketch
   > below and the same rule the fill already lives by. The job carries both
   > drawings' line art, so the shift can be worked out again whenever it is
   > wanted and thrown away with the fill it produced. A stored transform would
   > be a second derived thing to keep in step with drawings that move, and this
   > document is emphatic that propagated means provisional.
   >
   > **What it buys, measured in `bench_carry`.** Everything the first rung
   > failed at: a mark carried across 400 px of movement fills its shape with
   > `spread` unchanged at 7.70, where leaving it behind gave 0% coverage and a
   > flag. The wrong-region failure — a wall sliding across a mark, the
   > neighbour taking a colour that was never meant for it — goes from 100% of
   > the neighbouring region to none of it.
   >
   > **Where it fails is worth knowing.** It is a global translation found by
   > matching ink, so line art that repeats gives it more than one good answer:
   > a box with a wall down the middle, moved 200 px, matched its far wall to
   > the divider and reported 49. The fill is then exactly as wrong as carrying
   > unchanged, which is the floor this cannot go below.
   >
   > And a translation cannot explain a change of *size*, which is what freehand
   > redrawing mostly is. On a reported project — five circles drawn in the same
   > place — the circles had really drifted 42–65 px upwards and shrunk by a
   > fifth, and the estimate came back 72–102 px: right direction, overstated,
   > because the best overlap of two rings of different radius is where they
   > touch rather than where they are concentric. A confidence gate was measured
   > and cannot separate that from a genuine move (×1.09–1.25 against ×1.02–1.11
   > for a real 20–40 px translation). Two rules came out of it and are in
   > `estimateCtgShift`: score agreement rather than difference, and blur before
   > matching so that a drawing is a shape rather than a line. Rungs 3 and above
   > are what a real answer looks like.
   >
   > Two rules fell out of building it. **The mark's own pixels move with its
   > seed**: a mark wins the pixels it covers whatever the solver decided, so a
   > seed read in one place and an override painted in another would leave a
   > stripe of colour across a region with every reason to be a different one.
   > And **the shift is part of what the fill depends on**, so the fill's key
   > has to mix the *source* drawing's line art as well as this one's — nothing
   > else in it mentions that drawing, and redrawing it moves the answer.
   >
   > A third, from the first bug report against it. **Everything that shows a
   > mark has to be told where it went.** The fill followed the drawing while
   > the Marks column drew the mark where it was made, and the first stroke on a
   > carrying drawing copied the marks unmoved — so touching the colour layer
   > anywhere undid the fill you were looking at. The shift is derived, so the
   > temptation is to work it out where it is wanted; it costs 20 ms, which is
   > nothing in a solve and impossible in a paint. It is kept in
   > `Document::ctgShiftAt`, written by every solve and read by everything else.
   >
   > And **the search window has to cover the whole area, not half of it**. It
   > was half the grid, so a shape that had moved most of its own width sat
   > outside the window and the search reported the best wrong answer with
   > nothing to say it had been looking in the wrong place. That is the failure
   > mode to fear in all of this: not a wrong answer, a confident one.
3. **One transform per region**, from the previous fill's regions. Translation
   first; affine only if translation measurably is not enough.

   > **Built, and it is the default.** Translation only; affine was never
   > reached for, because what a region has to answer is where its marks go and
   > that is whole pixels. `estimateCtgWarp`, and it costs 41 ms against rung
   > two's 7.
   >
   > **The regions are solved here rather than looked up.** The design note
   > above says to read them off the previous drawing's fill, and the job
   > carries no document — so the source drawing's line art and its marks, both
   > already in the job, are cut coarsely inside the estimate. That is a second
   > max-flow per solve and it is most of the 41 ms. It also removes a
   > dependency the note did not notice: the fill of a drawing nobody has
   > visited is not in the cache to be read.
   >
   > **A region is a connected piece of one label and not a label.** Two shapes
   > scribbled the same colour are one label, and the box round both is the box
   > round the drawing — which is rung two again, on exactly the drawings rung
   > three is for.
   >
   > **What bounds a region's search is the whole of whether it works**, and
   > both halves of the bound are measured rather than chosen. It may depart
   > from the whole drawing's answer by half its own *shorter* side: half a
   > region's width is where a carried mark stops holding its region, which is
   > rung one's own measurement, and the shorter side is where a region's ink
   > starts repeating. Given the longer side instead, the two halves of
   > `bench_carry`'s divided box matched each other and the right half took the
   > left half's colour on every drawing.
   >
   > A confidence margin instead of a bound was measured and **not** built:
   > wrong departures reached ×1.575 and the departures that are needed start at
   > ×1.607, so any threshold between them is a constant fitted to a fixture.
   >
   > **What it buys and what it costs, on a shot somebody coloured.** On the
   > drawings whose marks were placed with it running, nine better, four worse,
   > two level, and pixels taking the *wrong* colour go from 2.6% to 0.5% —
   > which is the failure that has to be hunted for, where a missing colour
   > announces itself. On the same shot coloured for rung two it is a net loss.
   > See the sentence at the top of this document.
4. **As-rigid-as-possible registration** — Sýkora, Dingliana & Collins, NPAR
   2009, the direct sequel to LazyBrush and built for this. Read it before
   designing anything past 3.

   > **Built, measured, and off by default.** `estimateCtgLattice`. Push every
   > lattice node on its own to where its neighbourhood matches best, then pull
   > the lattice back towards rigid, and repeat until it stops moving.
   >
   > It needed no plumbing, and that is worth knowing before the next rung:
   > `CtgWarp` has been a displacement field since rung three, so a rung is now
   > an estimator and nothing else. Everything that shows a mark reads pixels
   > that were already carried.
   >
   > **On two shapes that a single translation cannot describe it is decisive.**
   > `tests/projects/two-circles.animage`, which was reported rather than
   > constructed: rung two and rung three both slide the whole drawing 780 px
   > and put one circle's mark on the other, and rung four does not, because it
   > never picks a translation to be wrong about. 56.5% of a drawing in the
   > wrong colour becomes none of it.
   >
   > **On a real shot it did not beat rung three**, measured and confirmed by
   > hand. It fixed rung three's worst drawing and lost more than it gained
   > elsewhere.
   >
   > **And the reason had a number on it, and the number was not what it looked
   > like.** Registered against a drawing and *itself*, where the only honest
   > answer is that nothing moved, the lattice drifted 146 px — and that was
   > read here, and in #66, as the aperture problem: a node on a straight line
   > matches equally well anywhere along it, and line art is mostly straight
   > lines where the paper's examples are filled cartoon regions.
   >
   > **It was the score.** The push step maximised agreement — a sum of
   > products — where the paper minimises a sum of absolute differences
   > (§3.1, equation 1). The two disagree about blank paper, and blank paper is
   > most of a drawing: under a difference, blank against blank is a perfect
   > score and bare paper is evidence, where under a sum of products blank
   > against blank and blank against ink both score zero and what is left is a
   > quantity largest wherever the target has the *most* ink. Every node was
   > pulled towards the nearest dense thing.
   >
   > Under the paper's measure the drift is not smaller, it is **zero** — a node
   > on a straight line *ties* along that line, and the search scores the
   > position in hand first and only displaces it on a strictly better score, so
   > a tie is not a reason to move. The regions a colourist would have to fix
   > went from 19 of 52 to 10 on the first colouring and 22 of 53 to 14 on the
   > second, and the whole estimate costs 195 ms against 526 because the lattice
   > settles rather than thrashing. two-circles is unmoved.
   >
   > The aperture problem is real and this does not solve it. It was not what
   > the number was, and the lesson underneath is the one worth carrying to
   > rung five: **a sum of products is only a fair score where both sides are
   > the same size and the overlap does not move with the shift.** Rung two is
   > that case. A block is not, and neither is rung three's masked region — see
   > [#68](https://github.com/S-poony/Animage/issues/68), where two attempts to
   > fix it were measured and both were worse.
   >
   > **Rung four is still not the default**, and the reason is now honest rather
   > than a bug. On the shot coloured for rung three it agrees slightly more
   > often and leaves more regions to fix, and the two fail on *different*
   > drawings: rung three's worst is one rung four gets exactly right, and rung
   > four puts a whole leg in a foot's colour where rung three is nearly right.
   > Which failure a colourist would rather have is not a question a benchmark
   > can answer, so `ANIMAGE_CARRY` exists, temporarily, to let one be chosen by
   > hand.

5. **Region-graph matching.** Regions with adjacency and area from *N*, matched
   against an over-segmentation of *N+1*. Robust to large motion, brittle to
   topology change — a limb crossing a body merges two regions and the match is
   gone.

### Propagated means provisional

Never written, discarded the moment the user draws, shown differently from a
scribble somebody made. Ideally the same mechanism as part 1, because
inheritance already *is* "provisional until edited" — if the two end up as
separate concepts, one of them is wrong.

### A confidence signal, nearly free

The failure mode to fear is not a wrong fill, it is a *quietly* wrong one: a
propagated scribble landing half in the wrong region still produces a confident
answer, because majority rule always has an opinion.

After a solve, for each scribble compute the fraction of its pixels that ended up
inside its own label's region. That number is already sitting there at solve
time. Below some fraction, flag the drawing in the timeline. It tells a colourist
which frames to look at, and it is the difference between automation that gets
used and automation that gets switched off.

Worth building early — with part 2's first rung, not after it.

> **Built, and then removed.** It was built with part 1 rather than part 2,
> because carrying a mark unchanged under line art that has moved is precisely
> how it lands wrong — motion is what would *reduce* that. It fired on drawings
> whose colour was good and it is gone; the numbers below are what remains, and
> docs/handover.md records why neither of them can carry a flag.
>
> The fraction proposed here comes out at exactly 1 in every case in `test_ctg`.
> A seed is only overruled when severing it beats isolating it, and that needs a
> mark which is nearly all edge, so in practice the solver honours whatever it
> can see. It is kept as `CtgFill::confidence` and marked a dead end.
>
> What works is scoring a mark by what it **won**: region area per pixel of
> itself, `CtgFill::spread`. A mark that filled a shape wins many times its own
> area; a mark carried onto blank paper wins nothing but itself, because with no
> line art to follow the cut simply hugs the seed. Measured — 8.3, 17, 23, 65,
> 188 for marks that landed properly, 1.82 for the tightest legitimate one, and
> 1.00 for a mark carried off its shape. The floor is 1.5 and it is measured, not
> derived.
>
> It is not an invented quantity, which is worth knowing: it detects the failure
> the research note already names. `fr/lazybrush-et-calques-ctg.md` §5 lists
> **Raccourcis** — "un scribble trop fin dans une région à long contour troué :
> la coupe encercle le scribble" — and a cut that encircles the scribble is
> exactly a spread of 1. The remedy given there is a wider brush, and that is
> still the remedy; what is new is being told which drawings need it.
>
> Two things this does not do, both worth knowing before part 2. It does not
> catch a mark landing in the **wrong region of about the right size** — see the
> open question below. And both numbers must be read off the solver's labels and
> never off the finished fill: a mark wins its own pixels in the fill whatever
> the solver decided, so from the fill every mark is perfectly placed, always.
>
> Judging a whole track is what makes it useful, and it is affordable because the
> verdict is not the fill. The audit solved coarsely and stopped after the
> labelling, keeping a few bytes a drawing: 67 ms for twelve 1080p drawings cold,
> nothing when none has moved. Flags read from the fill cache instead only
> appeared on drawings somebody had already visited, which is the feature
> inverted.
>
> The function that did this was called `auditCtgFills` and is **not in the tree**
> — it went with the flag it fed, and nothing else read it. Named in the past
> tense here on purpose: the paragraph above describes what was built and
> measured, not something to go looking for.

### Scheduling

Propagation multiplies solve cost by the number of drawings, against a solve that
is capped at 512×512 *because it runs where the interface is waiting*. So solving
off the interface thread — item 3 in the handover — is a prerequisite for part 2.

It is **not** a prerequisite for part 1, which only ever solves the drawing in
front of you. Do part 1 first and do not wait for the thread work.

## Things not to do

- **Do not store a parent pointer.** Resolve inheritance at read time.
- **Do not materialise inherited scribbles into every image.** It breaks "adding
  a layer touches no image", which has a test; and it makes "unless the user
  changes them" undecidable, because an inherited mark and a drawn one stop
  being distinguishable.
- **Do not propagate the fill.** It is derived. Propagating it would be storing
  the one thing the layer exists in order not to store.
- **Do not make any of this conditional** on colour count, on drawings looking
  "similar enough", or on a confidence threshold silently switching behaviour.
  This codebase has learned that lesson twice — a background conditional on there
  being one colour, and a border price that was a gap cap — and both times the
  condition moved the surprise rather than removing it. A flag that tells the
  user is fine; a rule that changes behaviour behind them is not.

## Open questions, for whoever picks this up

- ~~**Forward only, or backwards too?**~~ **Settled.** A per-layer choice of
  forwards, backwards, or whichever coloured drawing is nearer, defaulting to
  forwards. Backwards is for colouring the drawing in front of you — often the
  last of a run, because it is the one you were working on. Both ways is what
  fills the gaps between drawings already coloured, which neither of the other
  two does. None of them is guessed at: reaching in a direction you did not ask
  for is sometimes what you want and never what you expect. Carrying at all is
  also a switch, because a shot whose design changes every drawing gets nothing
  from it and has to go looking for the marks it carried.
- **What is "the wrong region"?** Still open, and it is the interesting one. The
  flag catches a mark that filled *nothing*, which is a fact about one drawing.
  It does not catch a mark that landed in the wrong region of about the right
  size, and that cannot be judged from one drawing at all: "wrong" only exists by
  reference to the drawing the mark came from, so it needs a correspondence
  between regions on two drawings — which is exactly what part 2 has to build
  anyway. Proxies considered and not built: region area ratio between source and
  target, and region overlap. Both misfire on fast movement, which is when
  carrying is most likely to be wrong *and* most likely to be right. A second
  flag that cries wolf teaches people to ignore the first one, so this waits for
  part 2 to give it something real to check against.

  **Since measured, and worse than it reads above.** `bench_carry` produces the
  case on demand: a wall sliding across a carried mark, with nothing marked on
  the far side. The neighbouring region takes the wrong colour completely, from
  the first drawing of movement, and `spread` goes *up* — 6.3 to 12.8 — because
  the mark did fill a region and the number cannot ask which one.

  That, and a snug mark in a small region measuring 1.96 against a floor of 1.5,
  is why the flag built on `spread` was taken out rather than tuned. The
  quantity is an amount and the question is a correspondence. This stays open,
  and it is now the whole of what a flag would need.
- **Override granularity.** Per (image, layer) is proposed here and needs no
  fork. Per *scribble* is finer and more useful — change one region's colour on
  one drawing without detaching the rest — and needs the vector fork.
- **Does the transform belong to the drawing or to the inheritance?** If drawing
  *N+2* inherits from *N*, is its transform relative to *N* or composed through
  *N+1*? Composed drifts; relative needs a registration over a longer interval.
  Measure before choosing.
