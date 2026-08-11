# Lasso and transform

Design notes for selecting part of a drawing and moving, rotating or scaling it,
and for copying and pasting the result.

**Nothing here is built.** This was written before any of it, out of a
conversation that argued the shape of the feature before its code, and it is
written down for the same reason `scribbles-through-time.md` was: most of it is
a consequence of decisions the model has already made rather than a free choice,
and finding that out twice is expensive. Where building it contradicts this
document, keep the original text and mark the correction underneath. A design
note that quietly agrees with whatever happened is no use to anybody reading it
before doing the rest.

Two things are deliberately not here and have issues of their own: transforming
a whole layer across time ([#25](https://github.com/S-poony/Animage/issues/25)),
and flipping ([#24](https://github.com/S-poony/Animage/issues/24)).

> **Correction, written while building it.** All four phases are built, so
> "nothing here is built" is no longer true of any of it. What happened is in
> `handover.md` under "what the keyboard does, and when", "moving a drawing",
> "the lasso", "copy, cut and paste" and "what a transform costs"; corrections to
> specific claims below are marked where they belong. The document was right
> about nearly all of it, including the two things it warned would be expensive
> to find out twice: that a hard mask leaves a jagged rim, and that bilinear at a
> four-times reduction loses line art entirely. Both were checked by building the
> wrong version first and watching it fail.

## What a selection is here, and what it is not

**A selection does not clip the brush.** You can draw anywhere, whether or not
something is selected, and drawing outside a selection is not blocked, masked or
warned about. This is the single largest difference from every other program
with a lasso in it, and it is the decision the rest of the feature falls out of.

Because a selection here cannot restrict painting, it has no independent life. It
is an argument to three operations — transform, copy, erase — and nothing else.
So it wants no mode, no panel, no status of its own and no place in the saved
project, and it can be cleared by an ordinary click without anybody losing work
they cannot recreate in two seconds.

**That is what the choice buys, not an argument that the other choice is wrong.**
Restrictive selections are the norm everywhere else and they work perfectly well;
a selection that clips the brush is a genuinely useful tool, which is why every
painting program has one and why several have little else. What is being traded
is a capable tool for a feature with no ceremony around it. See "decisions taken"
for what would reverse it.

Concretely, a selection is:

- a closed loop of points **in image coordinates**, so it survives panning and
  zooming and means the same thing at any magnification;
- **about one layer** — the active one — even though it was drawn over a
  composite of every track;
- **not saved**, and gone when the project closes.

## Where a transform belongs: layer, track, scene

Three different features share one piece of arithmetic and are constantly
mistaken for each other. Naming them apart is what stops the scope question
coming back on every subsequent decision.

| | | |
|---|---|---|
| **drawing** | one layer, one drawing | this document |
| **layer** | one layer, every drawing — like layer opacity | [#25](https://github.com/S-poony/Animage/issues/25) |
| **track** | every layer of a track, every drawing — a peg | not built |
| **scene** | every track — a camera | not built |

What this document builds is the first row, and it **bakes pixels**: transforming
the drawing in front of you rewrites its cel, because that is what "move this
drawing" means. The row below it should not bake — see #25 for why, and for what
happens when you then draw on it.

The consequence worth stating plainly: **a transform always acts on exactly one
layer, the active one.** There is no "which layers" question anywhere in this
feature. Moving a character drawn on an ink layer and a rough layer is two
transforms, and the only way to make them identical is to type the same numbers
into both — which is one of the reasons the numeric fields are in the first phase
rather than being a nicety added later.

## Two tools and one contextual control

The interface is smaller than the feature sounds.

**Lasso** and **Transform** are tools, in the exclusive `QActionGroup` that
already holds Brush and Eraser. They are tools rather than buttons because they
compete with the brush for the pen, which is exactly what that group means.

There is **no "transform selection" button**, because the tool is the button.
Entering the Transform tool takes the current selection, or the whole cel if
there is none. That single rule is what makes "press it with nothing selected and
it boxes the whole drawing" fall out instead of being a special case.

**The scope control is not a persistent checkbox anywhere.** It belongs to the
transform that is happening, not to the track and not to the program, so it lives
on a bar that exists only while a transform is live and is gone the rest of the
time. A permanently visible checkbox that is off by default is a trap precisely
*because* it is off by default: you will forget it is on in the one session where
it is, and an ordinary drag will then rewrite the whole shot.

The bar holds: numeric dx / dy / rotation / scale, Apply, Cancel. Everything on
it follows the window's existing focus discipline — buttons `NoFocus`, numeric
fields `ClickFocus` with `editingFinished → canvas_->setFocus()`, copying
`radius_` and `onion_` — because that discipline is why the canvas has the
keyboard at all.

> **Correction: two scale fields, not one.** "dx / dy / rotation / scale" and
> "edge handles scale one axis" cannot both be true, and the second is the one
> the design leans on — letting the handle decide is what frees Shift. So the bar
> holds dx, dy, rotation, scale X and scale Y. The alternative was a single field
> that shows nothing meaningful whenever the two axes differ, which is a readout
> that sometimes cannot say what is happening.
>
> **Addition: picking another tool commits.** The document settled that changing
> frame commits and that looking never does, and said nothing about the tools.
> Reaching for the brush means you have finished placing the drawing, so Brush,
> Eraser and Lasso bake it — which makes the tools a way out as well as Apply.
> Changing layer and changing track commit for the same reason; Clear cancels,
> because emptying the layer throws those pixels away and baking them first would
> be a resample spent on nothing.

Cut, Copy, Paste, Select all and Deselect go in the Edit menu beside Undo and
Redo. Nothing goes in the Track menu.

## Phase 0 — the shortcut table

A prerequisite, not a nicety, and it is a down payment on
[#14](https://github.com/S-poony/Animage/issues/14).

A live transform is the first **mode** this program has. Return means Play
normally and Validate during a transform; the arrows mean step-frame normally and
nudge during a transform. Today every shortcut is a `QKeySequence` literal at one
of fifteen call sites in `buildActions`, all `ApplicationShortcut`, which means
they fire regardless of what the canvas thinks it is doing. Implementing modality
as `setEnabled` calls scattered through the transform code is how an action ends
up stuck disabled after some cancel path nobody tested.

So: one table — id, label, default key, which modes the action is live in — that
`buildActions` reads. No rebinding interface, no settings file, nothing
user-facing changes. Rebinding stays in #14.

What must go quiet while a transform is live: Play, Previous/Next frame,
Previous/Next drawing, Insert, Duplicate, Delete drawing, Hold longer, Hold
shorter. Undo and Redo are the exception — they are not disabled but
**redefined**: Ctrl+Z with a live transform cancels the transform.

A disabled `QAction` does not consume its shortcut, so disabling Play is what
frees Return; `play_button_` goes with it, and entering a transform stops
playback outright.

The test the table buys: no two actions live in the same mode share a key.

## Phase 1 — transform with no selection

The most useful two thirds of the feature, and it needs no polygon code at all.
Whole cel, active layer, live box, Apply and Cancel, numeric fields.

**Whole-pixel translation must bypass the resampler and be bit-exact.**
Registration nudges are the most common transform in animation by a wide margin
and they must never soften a line. `translated()` in `tile.h` already does
exactly this. Axis mirrors are the same branch with a sign, which is why #24 is
cheap later and why it must not be built on the general path.

**The box for "no selection" is the ink's bounds, not the canvas.** Which means
it comes from `celBounds` — the rectangle that famously goes on describing marks
you erased, because erasing empties a tile without releasing it. So freeing
emptied tiles ("what I would do next", item 3) is a prerequisite for the box
being in the right place, not a tidy-up to do afterwards.

**Handles are drawn at a fixed screen size**, and when the box is narrower than
about three of them they sit outside its edge rather than on it. Below a minimum
on-screen size the interior stops being a move target, because there is nothing
left to hit, and the move comes from the handles or the numeric fields. The
mirror case matters too: a whole-cel transform while zoomed in puts every handle
off screen, and then the numeric fields are the only grab there is.

**Corner handles scale uniformly, edge handles scale one axis.** Letting the
handle decide rather than a modifier is what frees Shift to constrain rotation to
fifteen-degree steps and moves to an axis, which is worth more.

## Phase 2 — the lasso

The selection is stored as a polygon and rasterised to a coverage mask when one
is needed. Polygon because it is cheap, because it can be re-rasterised at any
resolution, and because a scanline fill with an even-odd rule is sixty lines of
Qt-free code in `core` that a test can drive headlessly.

**Coverage, not a hard edge.** A hard mask cut through line art gives the lifted
content a jagged rim. With premultiplied pixels the honest version is exact and
costs nothing: `lifted = src × c`, `remaining = src × (1 − c)`. Premultiplication
is what makes that true, and it is one more thing that representation is paying
for.

**What counts as a click rather than a lasso is the ordinary drag threshold** —
about four **screen** pixels, so it means the same thing at every zoom — and not
a threshold on the selection's area. A legitimate selection can be a single
eyelash: long, thin, and near-zero area. A click clears the selection; a drag
makes one however small the loop.

Selecting on one layer while looking at a composite of every track is a real
surprise: you loop around a character and only the ink lifts. Entering the
transform should dim what is not moving.

## Phase 3 — copy, cut and paste

Cheap once phase 2 exists, because **a paste is a float that came from the
clipboard instead of from the cel**. That is the whole reason it comes third: the
lifting, the hole, the box and the commit are all already built.

A paste lands at the coordinates it was copied from — you paste to re-register
something, not to drop it wherever the view happens to be — and it lands in a
float that touches no pixel of the drawing until it is validated.

The clipboard is internal. The system clipboard cannot carry half-float
precision or the CTG label encoding, and an image handed to another program is a
different feature with a different argument behind it.

## The float, and why the document is not written until commit

During a live transform the source region has to look empty, and the moved pixels
have to be visible on top of it.

**Do not lift into the document.** The obvious version writes the hole
immediately and puts the pixels back if you cancel — which means Escape has to
unwind a command, and there is now an undo entry for a thing that did not happen.
Lift into a scratch grid instead, leave the document untouched, and let the
preview subtract the mask. The document is written exactly once, on commit,
inside one `ScopedCommand`. Escape leaves no undo entry because nothing happened.

`LayerPass` already carries an offset for the CTG shift; widening it to an affine
is what the preview draws through, and it is the same widening #25 needs.

**The preview and the commit will not agree exactly.** Re-resampling half-float
tiles on every mouse move will not hold a frame, so the preview is the existing
display cache blitted through a `QTransform` and the real resample is paid once,
on commit. The committed pixels therefore differ slightly from what you were
looking at while dragging. That is the same class of honesty as the PNG and the
EXR not containing the same numbers, and it should be documented rather than
chased.

## What a commit costs, and why looking must never commit

A commit resamples. Rotating five degrees ten times is visibly softer than
rotating fifty degrees once, because each commit puts half-float line art through
a filter again. So everything in the design pushes toward one commit per
intention: hold the matrix live, accumulate, bake once.

That is why **nothing about looking at the drawing may commit** — not panning,
not zooming, not fit-to-canvas, not the onion skin, not layer visibility, not the
passe-partout. Judging a placement means looking at it from another zoom, and a
transform that ends when you look at it is unusable exactly when it is working.
An early sketch of this feature had "any key or any action validates"; it was
refused for these two reasons together.

It also settles a cost that looked frightening and is not. Registration is
iterative — nudge, look, nudge again — and if each nudge were its own command it
would be a full-cel snapshot each time. Because looking does not commit, the
whole session is one command.

**Minification needs a real filter.** Bilinear at a four-times reduction drops
line art entirely: the same trap `ctgBarrier` records from the other end, and the
same lesson as the sample grid — half a filter is worse than none. Box-filter the
source footprint when scaling down, reusing the `SampleStep` reasoning rather
than inventing a second one.

Write `bench_transform` before optimising any of it. This repository's most
repeated lesson is that a benchmark decides where you will look next.

## Colour layers are out of scope, and what that costs

**Lasso and Transform are disabled on a CTG layer**, with the status bar saying
why — consistent with the brush already meaning something different there.

This is a real simplification and not only a restriction. A mark is a label:
alpha is exactly 0 or 1 and the colour is quantised to eight bits a channel to
form a key, so any interpolation recreates a bug that was already found and fixed
once, where a blended rim quantises to a third colour that competes for regions
on its own account. And the transparent label is `{-1,-1,-1,1}` while
`isTransparentScribble` tests `r < 0`, so interpolating a transparent mark
against a red one produces pixels that classify as transparent — colour silently
swallowed at every rim. Excluding CTG layers deletes the nearest-neighbour path,
the threshold-at-0.5 mask and that trap together.

What it costs is that transforming an ink layer always leaves its scribbles
behind — and how much that costs depends entirely on which transform it was.

**A translation costs nothing, and the machinery is on its side.** A scribble is
a seed and not a boundary: it has only to land mostly inside the region it names,
which is the whole reason for scribbling rather than filling. A drawing that
moved is precisely the case `ctg_follow_motion` and `estimateCtgShift` were built
for, and a translation is exactly the thing the estimator estimates — so nudging
a drawing to register it should be found, absorbed and re-solved correctly with
nobody doing anything. Even with the marks left where they were drawn,
`bench_carry` measures a mark holding its region until the drawing has moved
about half that region's width. Most transforms are small moves, and for those
this is not a compromise at all.

**Rotation and scale are where it degrades, and the failure is quieter than
staleness.** The transform bumps the cel's revision, so `ctgInputsFor` changes
and the fill *re-solves by itself*, from marks that did not move, with the
estimator returning a translation-only best guess at a change that was not a
translation. The colour actively re-lands, slightly wrong, with nothing
announcing it.

That is accepted rather than missed: colour is done on animation that is
finished, so transforming a drawing after colouring it means something has
already gone wrong. **No flag.** A number that judges one drawing cannot tell a
mark that filled the wrong region from one that filled the right one — that was
measured — and a flag people learn to ignore teaches them to ignore the next one.

## The traps

Written as traps because each one is a mistake that is available to make.

**An empty lasso must not become select-all.** A loop enclosing no non-transparent
pixels is the same as no selection — there is nothing to lift. But "no selection"
also means "transform everything", so a stray loop over blank paper would become a
whole-drawing transform. Clear the selection, and stop.

**Iterate distinct drawings, not slots.** Anything that walks a track transforming
cels will transform a five-frame hold five times. The export's solve counter
learned this exact lesson and the fix was the same.

**A transform applies to the whole hold.** One cel, several frames. Obvious once
said, and it will be reported.

**Two Enters, and that is correct.** With a numeric field focused, the first
Return commits the field and hands focus back to the canvas; the second commits
the transform. It is what the brush size box does today. Write it down so it is
not "fixed".

**A float is document state that is not in the document.** Autosave fires every
two minutes and writes the project; a float is not in it. A crash loses it.
Ctrl+Z during one cancels it rather than undoing the stroke before it. And
changing frame commits, because a float that follows you to another drawing is a
paste onto the wrong drawing waiting to happen — copy already follows you, which
is enough.

**Refuse where the brush refuses**, and for the same reasons: a locked layer, a
hidden layer, and past the end of the track, where there is no slot and no cel
and therefore nothing to edit. Transform is easy to forget here because it is not
the brush.

**Pasting a CTG cel onto a raster layer writes negative light as paint.** Block
it on the layer kind, not on a guess about the pixels.

**Backspace erases the selection; Delete still deletes the drawing.** The natural
expectation on Delete is "erase what I selected" and the existing binding is
"delete this drawing", which is a bad surprise in the dangerous direction. Making
it depend on whether a selection exists is a bad surprise in the other. Two keys,
no mode.

## Decisions taken, and what would reverse them

- **A selection does not clip the brush.** Everything else follows from it. What
  would change it: wanting to paint inside a shape without leaving it, which is a
  real thing people do — but it is mostly a thing *painters* do, and this is
  animation software. That is the whole of the argument, and it is an argument
  about who is holding the pen rather than about what a selection is. Clipping
  the brush is a second feature; it should be argued on its own rather than
  smuggled in as a property of selections.
- **One layer at a time.** Reversing it needs multi-selection in the layer panel
  and a decision about what a transform of several layers with different content
  even means; the numeric fields are the escape hatch until then.
- **The selection is cleared by changing frame and survives changing layer.** A
  loop is geometry in image space, so re-lifting it from another layer of the same
  drawing is meaningful; carrying it to another drawing is how you transform the
  wrong thing.
- **Ctrl+C with no selection copies the whole cel**, symmetrically with what the
  Transform tool does with no selection.
- **The clipboard is internal.**
- **Colour layers are excluded.** What would change it: someone wanting to move
  scribbles, which the inheritance mechanism largely already does for them.

## Order of work

Each phase is usable on its own and the order is dependency-driven, not
importance-driven.

0. The shortcut table, and the disable list.
1. Transform with no selection: whole cel, live box, numeric fields, exact
   whole-pixel translation, Apply and Cancel.
2. The lasso: polygon, coverage mask, lift, hole, dimming.
3. Copy, cut and paste as a float.
4. `bench_transform`, then optimise what it points at.

Then, separately and not here: transforming a layer across time
([#25](https://github.com/S-poony/Animage/issues/25)), flipping
([#24](https://github.com/S-poony/Animage/issues/24)), and capping the undo
history ([#23](https://github.com/S-poony/Animage/issues/23)) — which this
feature makes more visible but did not cause.

## What to test

Invariants rather than appearances, because every one of these is a decision the
code makes and a decision can be asserted exactly.

- An identity transform returns the same bits.
- A whole-pixel translation returns the same bits, and does not go through the
  resampler.
- An empty lasso clears the selection and does not select everything.
- Transforming a drawing held over five frames changes it once.
- A whole-track pass touches each distinct drawing once.
- Cancelling leaves the undo depth where it was.
- No two actions live in the same mode share a key.

> **What building it added to that list.** Two more that the note did not name
> and that turned out to matter. *A thin line survives a four-times reduction* —
> forcing the bilinear path makes it disappear entirely rather than merely thin,
> so the box filter is load-bearing and not a refinement. And *no two bindings in
> one mode differ only by Shift on a non-letter*, which is the actual mechanism
> of #14 and which the conflict check above passes happily.
>
> Both were verified by breaking the code and watching the test go red, which is
> the only way to know a test is testing anything.
