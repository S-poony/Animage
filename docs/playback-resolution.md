# Playing at a resolution the machine can keep up with

Issue [#30](https://github.com/S-poony/Animage/issues/30).

A plan, not a description: none of this is built. It is written before the code
because the expensive half of this is deciding *when* to give up resolution, and
that argument is cheap on paper and a rewrite afterwards.

**Read the next section first.** Not repainting a hold is cheaper than any of
this, costs no sharpness, and on twos it removes the problem outright — which
makes it the thing to do before deciding whether the rest is needed.

## What is wrong

`bench_playback` measures it. At 4K, on a 4K viewport, a playback frame costs
somewhere between 52 and 59 ms against a 41.7 ms budget, and between a quarter
and two fifths of the frames are never shown. At HD it costs 13 ms and nothing is
dropped, at any track count. The spread is run-to-run noise, which is why every
number below is a range: what is solid is the gap between the rows.

The cost is **per output pixel** and not per stroke — the 4K viewport is 4.8x
the HD one's area and costs four to five times as much. Both halves of a refresh scale that
way: `Compositor::compositeScene` produces one entry per cache entry, and the
sRGB conversion loop after it reads one and writes one. Nothing about the
drawing enters into it, which is why two drawings 36x apart in tile count
refresh in the same time.

So the lever is the number of entries, and the program already has it.

## Do the cheaper thing first: stop repainting a hold

**Every playback frame is currently a full recomposite, including the frames
that show exactly the picture the frame before them did.** `CanvasWidget::
setFrame` ends in `onion_dirty_ = true; refreshAll();` unconditionally, and
`refreshAll` sets `dirty_everything_`, so a shot on twos flattens and converts
the same viewport twice for every drawing. The benchmark says so without being
asked: on 48 frames the median frame is 13.17 ms and the p95 is 13.93, and a
run where half the frames were free would be nowhere near that flat.

The arithmetic is better than the reduction's, and it costs no sharpness at all.
Take the 4K line-art row — 53.6 ms a frame against a 41.7 ms budget, 37 of 48
shown. On twos, with a held frame costing only the slot change and the playhead:

```
slot 0  53.6 ms   clock 53.6   over budget, so slot 1 is next
slot 1   0.7 ms   clock 54.3   under 83.4, so it waits for the boundary
slot 2  53.6 ms   clock 137    over, so slot 3 is next
slot 3   0.7 ms   clock 137.7  waits again
```

**Nothing drops.** A pair costs 54.3 ms against a two-frame budget of 83.4, so
the cheap frame absorbs the expensive one's overrun and the take runs at rate.
48 of 48, at full resolution. Animation is overwhelmingly on twos, so this is
the common case and not a lucky one — and on ones it buys nothing, which is
where the reduction below is still the answer.

**The comparison has to be the whole picture, not the current track's drawing.**
What gets composited is every track's shown drawing, so the test is the tuple of
`Track::imageShownAt(slot)` across every track — `imageShownAt` and not
`imageAtSlot`, because a track past its end contributes whatever its end
behaviour says. A character on ones over a background held for the whole shot
must still repaint every frame; two tracks both on twos but offset by one frame
have no held frames in common at all and must also repaint every frame. Anything
that compares one track's drawing gets both of those wrong.

Two things that have to come with it:

- **The rate readout counts paints**, so a canvas that legitimately skips a held
  frame would make it report half rate and cry wolf. The canvas has to say how
  many frames it skipped as unchanged, and the readout has to count those as
  shown -- which they are: the picture on screen is the right one for that frame.
- **Skip only while playing**, at least at first. `setFrame` is called from
  scrubbing, from `afterProjectLoaded` and from undo, and a caller that changed
  pixels without marking anything dirty is relying on that unconditional
  `refreshAll`. Confining it to playback is the version that cannot be wrong
  about a case nobody enumerated; widening it is a second change, measured.

## The idea, when a hold cannot save you

`SampleStep` says how many image pixels one composited entry stands for. It is
`max(1, 1/zoom)` today — one entry per screen pixel, which is what issue #11
settled. Multiply it during playback and both loops shrink together, because
both are per entry. The blit magnifies what is left, which it is already doing
at every zoom below 100%.

At 4K and fit zoom, `cache_step_` is 1.0 and the viewport is 6.98M entries.
A step of 2.0 makes it 1.75M — about what HD is at 100%, where the frame costs
13 ms. That is the whole of the arithmetic.

**What it costs is sharpness during playback only.** Nothing is written, no cel
is touched, and the frame the pen draws on is untouched — the step goes back the
moment playback stops. This is what every other application in this space does,
and it degrades the thing that does not matter while you are judging timing.

## The constraint: do not give up resolution that was not needed

An unconditional "half resolution while playing" would be four lines and would
be wrong. HD playback has a threefold margin at four tracks; softening it buys
nothing and costs the animator the sharpness they had. So the reduction has to
be **earned by a measurement**, and the measurement is now available: the status
bar's playback rate already works out an effective frame rate from
`CanvasWidget::paintCount`.

Three rules fall out of that, and the second is the one that is easy to get
wrong.

**1. Start at full resolution, always.** A take opens at `scale = 1.0`. If the
machine keeps up, nothing ever changes and nobody sees a soft frame. This is
what makes the feature invisible on the hardware that does not need it, which is
most of it.

**2. Never change resolution *during* a take.** The temptation is a closed loop
— overrunning, so coarsen now — and it is wrong twice over. It oscillates, since
coarsening makes frames cheap, which argues for refining, which makes them
expensive again; hysteresis papers over that and does not remove it. Worse, an
animator watching a take is judging *motion*, and a picture that changes
sharpness partway through is an event in the middle of the thing being judged.
The reduction is decided at the end of a pass and applied at the next one.

**A loop boundary is a pass boundary**, and that is the useful part: playback
cycles, and the picture already jumps there, so a sharpness change at the wrap
is hidden by a cut that exists anyway. An animator who leaves it looping sees
the first pass at full resolution, possibly dropping, and every pass after it
smooth. That is the shape the frame-cache discussion asked for, reached far more
cheaply.

**3. Go back up when the reason goes away.** The scale is not a setting and must
not behave like one. Anything that changes what a frame costs — the window
resized, the zoom changed, a track or layer added, the canvas resized — resets it
to 1.0 and lets the next pass earn it again. A scale that outlived its reason is
a program that is quietly soft for the rest of the session and never says why.

## The arithmetic of choosing a scale

Cost is proportional to entries, and entries go as `1/scale²`. So from a pass
that measured a median frame cost `c` against a budget `b`:

```
wanted = scale_now * sqrt(c / b) * safety
```

with `safety` around 1.1, because landing exactly on the budget means dropping
half the time. Round it to something coarse — halves, or quarters — so that a
pass which measures a hair over does not produce a scale of 1.03 that nobody can
see and that changes again next time. Clamp it: never below 1.0, and never above
about 3.0, past which the picture stops being worth watching and the composite
stops getting much cheaper anyway, because each entry is reading a bigger block
and `boxSampleStride` is what bounds that.

**The `sqrt` is a model and should be checked rather than trusted.** The
conversion loop really is linear in entries. The composite is not quite: a
coarser entry reads more source pixels, so it does not shrink as fast, and
`bench_zoom` already records the effect from the other end — compositing at 10%
zoom got *more* expensive when the block began to be genuinely read. If one pass
of feedback does not land inside the budget, the answer is to let a second pass
correct it rather than to fit a better curve.

## Where it goes

- `CanvasWidget` gains a playback scale, applied in exactly one place:
  `ensureCacheCoversView`, where the step is settled.

  ```cpp
  const SampleStep step =
      SampleStep::fromRatio(std::max(1.0, 1.0 / zoom_) * playback_scale_);
  ```

  It is 1.0 unless playing, so **the non-playing path must be bit-identical**.
  That is the thing to assert, because `test_render` pins the step as a function
  of zoom and this is a second input to it.

- `setPlaying(true/false)` already exists and already invalidates the cache,
  which is where the scale is picked up and put down. One hitch at each end of a
  take, which is a moment nobody is judging.

- The decision itself belongs where the measurement already is —
  `MainWindow::updatePlaybackRate` is working out the effective rate four times a
  second for the status bar. It keeps the pass's median and hands the canvas a
  scale at the wrap.

## What the animator is told

The status bar says the rate. If it is also going to say the picture has been
softened, it should say it in the same place and in the same breath:

```
playing 24 fps (half resolution)
```

Because the two are one fact: the reason it is keeping up is that it gave
something up. A reduction nobody is told about is a program that quietly got
worse at 4K and looked like it got better.

## What would change any of this

**A frame cache instead.** Keyed on the drawing rather than the slot, so that
retiming — the edit you are playing back in order to make — does not invalidate
it. It is the better answer for a shot being watched over and over, and it is
unaffordable at the size that needs it: `display_` at a 4K viewport is 28 MB a
frame, so a 48-frame shot is 1.3 GB. **Halve the resolution first and it becomes
7 MB a frame**, at which point the cache is worth discussing — which is the real
argument for doing this one first. The two compose; they do not compete.

**GPU compositing.** Still the right end state and still item 4, and this does
not foreclose it — a shader is per output pixel too, so a step that reduces
entries reduces its work identically. What this does is find out whether 4K is a
problem worth a rewrite, for the price of a constant.

**Somebody who would rather see dropped frames than a soft picture.** Real, and
it is a preference rather than a fact: a texture artist checking line quality in
motion wants every pixel and does not care that the take stutters. If that is
asked for, it is a setting on playback and not a reason to change the default —
the default should serve judging timing, which is what the mode is for.

## How it gets verified

`bench_playback` is the instrument and it exists. Run it before and after; the
4K rows are the ones that have to move, and the HD rows are the ones that have
to **not** move at all, because a scale that touches them has violated rule 1.

`shots` has two situations photographing the rate readout, and a third belongs
there once this exists: the same shot before and after the reduction, so that
what was given up can be looked at rather than argued about.

And `test_render` gains the assertion that the step is unchanged when nothing is
playing. That is the invariant most likely to be broken silently, because
everything else about this feature announces itself.
