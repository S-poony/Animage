# Scribbles through time

Design notes for two things, in this order:

1. **A scribble stays** from one drawing to the next until the user changes it.
2. **A scribble moves** to follow the animation.

Nothing here is built. Written straight after the single-scribble change, while
the solver was still in hand, because a good deal of what follows is a
consequence of decisions already made rather than free choices.

The first is small, is not blocked by anything, and is most of the plan's "onion
fill" hypothesis. The second is a research problem with a cheap first rung.

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

Order of attack, cheapest first:

1. **Carry unchanged.** Part 1, and nothing else. On twos and threes it may
   simply be enough — measure how often before building anything else, because
   the answer decides whether the rest is worth it.
2. **One translation for the whole drawing**, from coarse correlation of the two
   barriers. Cel animation mostly translates between consecutive drawings, so
   this buys a lot for very little.
3. **One transform per region**, from the previous fill's regions. Translation
   first; affine only if translation measurably is not enough.
4. **As-rigid-as-possible registration** — Sýkora, Dingliana & Collins, NPAR
   2009, the direct sequel to LazyBrush and built for this. Read it before
   designing anything past 3.
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

- **Forward only, or backwards too?** Forward is simpler and matches how a shot
  gets coloured. Backwards would let you colour any drawing and have it apply to
  the whole run, which is sometimes what you want and never what you expect.
  Probably: forward by default, with an explicit "apply back from here".
- **Override granularity.** Per (image, layer) is proposed here and needs no
  fork. Per *scribble* is finer and more useful — change one region's colour on
  one drawing without detaching the rest — and needs the vector fork.
- **Does the transform belong to the drawing or to the inheritance?** If drawing
  *N+2* inherits from *N*, is its transform relative to *N* or composed through
  *N+1*? Composed drifts; relative needs a registration over a longer interval.
  Measure before choosing.
