# Using Animage

What every tool, key and panel does. For what Animage is, how it is built, and
the design notes behind it, see the [README](../README.md).

## Running it

If you downloaded a build, run `animage.exe`, the `.AppImage`, or the app bundle —
see the [README](../README.md#download). From a source tree, double-click
`run-animage.bat` on Windows, or:

```bash
cmake --build build --target animage && ./build/src/app/animage
```

| | |
|---|---|
| Draw | pen, or left mouse |
| `Shift`-drag | a straight line, at any angle: where the pen lands to where it lifts |
| `B` / `E` | brush / eraser (turning the stylus over also erases) |
| `L` | lasso: loop round part of the drawing |
| `T` | transform: move, turn or resize the selection, or the whole drawing |
| Backspace | erase what is selected (`Delete` still deletes the drawing) |
| `Ctrl+X` / `C` / `V` | cut, copy, paste — the selection, or the whole drawing |
| Enter, Escape, arrows | during a transform: apply, cancel, nudge (`Shift` for ten) |
| `[` / `]` | smaller / larger |
| Wheel | zoom about the pointer |
| Space-drag, middle-drag | pan |
| `1` / `0` | actual size / fit the canvas |
| `F` | fit the drawing, including whatever ran off the edge |
| `H` | hide the panels, and bring them back |
| Maximise / restore | fits the canvas, since the window it was framed for has gone |
| `Ctrl+Z`, `Ctrl+Shift+Z` | undo, redo |
| `Alt`+right-drag | brush size |
| `Alt`+click | pick the colour under the pointer (follows the pointer, taken where you let go) |
| Hold `Z` and drag | scrubby zoom |

## Drawing

**A straight line is any straight line.** Hold `Shift` when the pen goes down and
the stroke runs from where it landed to where it lifts, at whatever angle the
hand chose — it is not snapped to the horizontal, the vertical or a diagonal,
because a drawing has edges at every angle and a constraint that only knows three
of them is a constraint you have to work around. The path in between is thrown
away: wander wherever you like and the mark is the line.

Nothing is written until the pen lifts, so what you see meanwhile is a thin band
where the mark will go, and letting go of it is what puts ink on the drawing.
That means the line is one undo step, a straight line held through a frame change
lands whole on the drawing you end up on, and nothing is left behind on the one
you aimed from. Shift is read when the pen lands and the whole gesture keeps that
answer: reaching for it half way through cannot straighten a stroke that is
already on the paper.

The eraser is the same gesture. During a transform `Shift` means the
fifteen-degree constraint instead, which is the same key doing the analogous job
for the tool that has the pen.

## The lasso

**The lasso does not clip the brush.** You can draw anywhere whether or not
something is selected, and drawing outside a selection is not blocked, masked or
warned about — which is the largest difference from every other program with a
lasso in it. What a selection is here is an argument to three operations,
transform, erase and (later) copy, so it needs no mode and no panel, it is not
saved with the project, and an ordinary click clears it. It is cleared by
changing frame and survives changing layer: a loop is geometry, so re-lifting it
from another layer of the same drawing means something, and carrying it to
another drawing is how you transform the wrong thing.

## Transform

**Transform** takes the selection, or the whole drawing on the layer you are on
if there is none. Corner handles resize both ways and edge handles one way, the
round knob above the box turns it (so does dragging just outside a corner),
dragging anywhere else moves it — inside the box or well away from it, which is
what you need when the thing being moved is a thin line or is being lined up
against the drawing underneath. `Shift` constrains a rotation to fifteen degrees
and a move to an axis, and the
numeric fields on the bar are the way to place something exactly, or to move a
box whose handles have gone off screen. Nothing is written until you apply, so
cancelling leaves no undo step; moving by a whole number of pixels does not
resample, so registration nudges never soften a line. Colour layers are excluded:
a mark there is a label rather than paint, and interpolating one invents colours.

This is the drawing in front of you and nothing else. To move a layer across a
whole shot at once, see
[transforming a whole layer through time](#transforming-a-whole-layer-through-time).

**Flip X and Flip Y** are on the same bar, and they mirror about the middle of the
box. A flip is a state of the transform rather than something that happens to the
drawing — press it twice and you are exactly where you started — and it is exact:
mirroring moves the pixels without resampling them, so a flipped drawing is the
drawing, to the bit. That is why a handle dragged past its anchor squashes to
nothing instead of flipping: a mirror made out of a −1 scale would go through the
resampler, and a blurred mirror half a pixel out of place is not something
anything on screen would tell you about.

**A paste is a transform that came from the clipboard.** It lands at the
coordinates it was copied from — you paste to re-register something, not to drop
it wherever the view happens to be — and it arrives as a float you can place
before applying it, so nothing is written until you press Enter. The clipboard is
the program's own and not the system one: a 16-bit-per-channel image handed to
another program would lose precision, and it would be a different feature.

## Transforming a whole layer through time

**"Transform layer through time"**, in the layer panel beside Add and Remove,
moves, turns or scales *every drawing of one layer at once* — a whole character
shifted left across a shot, a background nudged into register, a layer scaled to
match a new canvas. The `T` tool takes the drawing in front of you; this button
takes the layer. There is no switch between them: which one you came through is
fixed for the gesture, and the bar says which — *This drawing* or *Whole layer*.

What is different once the box is up:

- **The box is green**, and it is drawn round every drawing of the layer united,
  not the one you are looking at. So turning the layer means the same thing
  wherever the playhead is, and there is a box to grab even on a frame where
  this layer happens to be empty.
- **The rest of the layer is under the float, faintly, moving with it.** Those
  are the layer's other drawings in their own colours — not onion skin, which
  tints warm and cool to say *when* a drawing is. There is no when to say here:
  they are all one layer, and what is worth seeing is what is going to land.
- **You can scrub while it is up.** Walking the playhead along the shot moves
  the solid drawing to whichever one you have arrived at and puts the one you
  left back among the faint ones. Nothing is written, and the box does not move
  — looking at the placement on another drawing is part of placing it.
- **A lasso is ignored**, and cleared when the gesture starts. A loop describes a
  shape on the drawing you drew it on and nothing at all on the other forty.

Everything else is the ordinary transform: the same handles, the same numeric
fields, `Shift` to constrain, Enter to apply and Escape to cancel.

**Apply writes every drawing, and that is the thing to know before you press
it.** It is one undo step, so one Ctrl+Z takes the whole thing back. But it is a
real edit to every drawing in the layer: moving by a whole number of pixels or
flipping is exact, and turning or scaling resamples each drawing once, the same
softening a transform of a single drawing costs. On a long shot it takes a few
seconds, during which the program stops and the status bar says how many
drawings it is writing; when it is done it says how many it wrote.

**A big bake can push older steps out of the undo history.** Undo has to hold
the drawings as they were, and a whole HD layer is more than the history's whole
budget, so the bake itself will always undo but the steps before it may be
dropped to make room. Nothing is lost from the drawing — only from what you can
take back. If the program runs out of memory partway it puts every drawing back
and tells you so, and there the history is left alone.

**When the button is greyed out, its tooltip says why.** A colour layer cannot
be transformed at all — a mark on one is a label rather than paint, and turning
or scaling it would blend two labels into a third colour. Nor can a locked or
hidden layer, a layer nothing has been drawn on yet, or a frame past the end of
this track, where there is no drawing to place.

## The keyboard

**All of those keys can be changed**, under Edit > Keyboard shortcuts. The list
is grouped and searchable, nothing changes until you press Apply, and Apply waits
until no two shortcuts that are ever live at the same time collide — it names
what has hit what rather than refusing the keys as you type them. Only what you
change is written down, to `shortcuts.json` in the platform's per-user
configuration directory — `%LOCALAPPDATA%\Animage\Animage` on Windows — so a
default improved in a later version still reaches you everywhere you left one
alone. Every tooltip in the program says
which key its control is on, and says the one it is on now.

Four things in that panel cannot be changed and are listed anyway. `Space`, `Z`,
`Alt` and `Shift` are *held* while you click or drag rather than pressed, which a
shortcut cannot express. Two of them still take their key, so an action rebound
onto `Space` would take panning away with nothing to say it had; `Alt` and
`Shift` take nothing from anybody and are listed because the panel is where you
go to find out what the keyboard does, and an answer without the eyedropper or
the straight line in it is the wrong answer.

Fitting the drawing is `F` and not the `Shift+0` it used to be. On a keyboard
whose digit row is the shifted face of another row — AZERTY, for one — typing
`0` at all means holding Shift, so `0` and `Shift+0` are one chord; Qt answers an
ambiguous shortcut by cycling between the candidates rather than by complaining,
which presents as the wrong thing happening every other press. That pair is one
of the two things the panel refuses, and it is the one nobody sees coming: they
are genuinely different sequences, and a check for duplicates passes them both.

The eyedropper is `Alt`+click on the drawing rather than the colour dialog's
"pick screen colour", which cannot work with a stylus — see **what the pen can
and cannot reach** below. Sampling the document is better regardless: it reads
the colour that was stored rather than what the monitor was showing after sRGB
encoding, the zoom filter and the onion skin.

## What the pen can and cannot reach

**What the pen can and cannot reach** is worth knowing, because three things that
each look like the pen being broken are one mechanism. Qt's widgets are built for
a mouse, and a pen reaches them by promotion: Qt turns a tablet event into a
mouse event, but only when nothing accepted the tablet event, and it sends that
mouse event to whatever sits under the tip. Almost everything in the window is
fine — buttons, menus, sliders, the layer panel, dragging a layer to restack it —
because the widget under the tip is the widget the gesture meant.

What is not fine is anything that works by *grabbing* the mouse, which is a
widget saying "send me the pointer wherever it goes, until I say stop". A grab is
a promise about a mouse, and the pen never made it: its events keep going to
whatever is under the tip. So:

- The colour dialog's **pick screen colour** grabs the pointer to follow it
  across the screen. With a pen it never hears the click. Hence `Alt`+click.
- A **modal dialog** is the same fact from the other side: Qt withholds mouse
  events from a window a dialog has blocked, but tablet events go by what is
  under the tip regardless, so the pen used to draw on the canvas behind an open
  dialog. That one is fixed.

**Panels can be torn off and dragged back with a pen.** A floating panel used to
be given a *native* window frame whose title bar a pen could not press, so a
panel dragged out could not be picked up again. They now wear a title bar Qt
draws. See [#50](https://github.com/S-poony/Animage/issues/50); the mechanism is
in [handover.md](handover.md).

**Double-tapping a name to rename it works with a pen** — in the layer panel and
the timeline — because those two count the taps themselves rather than relying on
a double click, which a pen produces on some platforms and not others. What a
tablet gesture does and does not carry is in [handover.md](handover.md).

## The canvas

**The canvas.** The outlined rectangle is what will be exported; everything
outside it is veiled. You can draw out there and nothing is clipped — roughs run
off the edge, and the tile model has no edges at all — but what is outside the
canvas is not in the picture, so a colour fill stops at the frame line. Set the
size under Edit > Scene settings, as an aspect ratio and a resolution or as a
width and a height in pixels; each is kept true to the other.

## The timeline

In the timeline: drag the ruler to scrub, drag the right edge of a card to
change how long the drawing is held, and drag the body of a numbered card to
reorder it. Held frames carry no number and cannot be picked up -- they are the
same drawing still showing, not a thing of their own, and they travel with it.

## Tracks

**The ruler stays put.** Scroll the timeline down past a few tracks and the
strip of frame numbers stays at the top, because that strip is where you scrub —
and soundtracks are under every drawing row, so reaching the sound is exactly
when you have scrolled. The row directly under the ruler is cut off while you
are down there; scroll a little further and it comes out.

**Tracks.** One row each, under one ruler and one playhead. Click a row to work
on that track: the layer panel, the brush and every timing button follow it,
while the canvas goes on showing all of them. The Track menu adds, duplicates,
renames and deletes them; a new track arrives at the bottom of the stack, with a
layer and a drawing so there is something to draw on.

**Duplicate track** makes a complete copy directly under the original — the same
layers, the same drawings, the same timing — and puts you on the copy. It is a
real copy and not a second view of the same thing: drawing on one does not
reach the other. On a soundtrack row it duplicates the soundtrack instead, which
is how a scene gets a second one.

Drag a track's name up or down the strip on the left to restack it — the top row
is the front of the picture, so that is how a background gets behind a character.
Hovering a name says what the track is and what it does with a drawing put down
on it. Double-click a name to rename it there, and a layer's name in the panel
the same way; Enter keeps the new name and Escape leaves the old one.

**What a layer row's colour means.** In the layer panel, an ordinary drawing
layer is in the usual text colour, a **greyed** row is an imported picture, and
a **blue** row is a colour layer. The grey is the same grey everything else uses
for "you cannot act here": an imported picture's pixels come from a file, so the
brush, the eraser and the transform all refuse there. The blue is the same blue
the timeline draws carried marks in, because it is the same thing — a mark you
make on a blue row is a label rather than paint. Hover any of them and the
tooltip says the rest, including which file an imported row came from.

Tracks need not be the same length. What a track shows once the playhead is past
its last drawing is set under Track ▸ Past the last drawing: nothing (the
default), hold the last drawing, or cycle — and the status bar says which, for
the track you are on. That is about the picture, so
it applies to the flattened `composite/` export and not to the per-layer
sequences: **a layer's folder is as long as its own track.** A background drawn
once exports one frame, and downstream you import the still rather than a
sequence. It does mean layer folders can differ in length.

**How long the shot is.** By default, as long as the longest track. Tick *Fixed
scene length* under Edit ▸ Scene settings and the number beside it is the shot
instead, in frames, with the duration in seconds shown under it.

Fixed, the boundary is a red line down the timeline with a grip in the ruler, and
you can drag it. A track is allowed to run past it: those frames are washed out
in the timeline, the status bar says *outside the shot*, and they are not played
and not exported until you move the boundary. Nothing is thrown away -- you can
still scrub to them and draw on them -- so cutting a shot short is not the same
as deleting the end of it. Adding a drawing never moves the boundary; the scene
says how long the shot is, not the tracks.

**Overwrite drawings**, in the Track menu, is per track and on by default. On,
the shot is a fixed length and a new drawing lands on the playhead and takes over
the rest of the hold it lands in: a drawing held 11 frames with the playhead on
frame 4 keeps 3, and the new one takes the other 8. Off, adding a drawing puts it
in after the whole hold and the shot gets a frame longer. Duplicating
does the same, and so does dragging a card -- it takes over the rest of the hold
it is dropped on, and the frames it left are absorbed by the drawing beside them,
so the length never changes. It never takes a drawing's last frame, so nothing is
wiped out by putting something down: standing on the first frame of a hold the
new drawing starts one frame later, and a hold of one frame has nothing to spare
at all, so there the track goes back to getting longer.

**Transform layer through time** needs more than one drawing on the layer — with
one there is no "through time" to speak of, and the Transform tool is the same
gesture. The button says so when it is greyed.

**An imported sequence cannot be transformed with the Transform tool.** That
tool moves one drawing and a sequence is several, so it is greyed out on one.
Moving the whole sequence around the canvas is a different thing and still
works: "Transform layer through time" in the layer panel does it, and on an
import it writes nothing at all — the numbers are stored and the picture is made
from the file again at them, so you can nudge and resize it as often as you like
without ever costing it any quality. The transform bar says "Placing" while you
are doing it, which is how you can tell.

## Painting on an imported picture

**You cannot, until you convert it — and the program offers to.** An import holds
no drawings of its own; it is shown from its files. So the brush will not mark
it, and neither will copy, cut or paste. Try any of those on one and a dialog
asks whether to convert the layer to drawings, saying how many drawings it will
write and what they weigh.

Say yes and the whole layer becomes ordinary drawings in one step. **What you
see is exactly what is kept** — including any moving or resizing you have already
done, because what is written is the picture on screen. Afterwards it can be
painted on, erased, and used as the line art a colour layer cuts against, which
an import cannot be at all.

Two things worth knowing before you say yes. **Place it first if you are going
to.** Once converted, the drawings are the picture, so moving it again resamples
what has already been resampled — whereas before converting, moving it costs
nothing however often you do it. And **it can be undone**: it costs the undo
history almost nothing, and the imported files stay in the project folder either
way, so nothing is thrown away.

It refuses if the layer is too long — roughly a hundred and twenty frames of HD,
because every drawing is written at once. It says the number when it refuses.
Importing a shorter range, or importing at half size, is what makes a long one
fit.

## Colour layers

**Colour layers.** "Add colour layer" makes a layer that holds scribbles rather
than colour, at the bottom of the pile — it is cut against the line art and
belongs under it. There is no scribble tool: scrawl roughly inside a region with the
ordinary brush and the whole region takes that colour, gaps in the line art
included. One scribble fills one shape; what is outside it stays uncoloured until
you scribble there too. What is stored is the scrawl, not the fill, so moving a
scribble recolours the region and redrawing the line art re-cuts it.

Holes in the line are bridged however wide they are — the fill follows the ink
where there is ink and jumps where there is not, so a shape drawn with a fifth of
its outline missing still fills from one scribble. There is no gap setting to
tune. What limits it is the size of your scribble: giving a scribble up is what
the boundary is measured against, so **a bigger scribble bridges a bigger hole**.

The outside of a shape is left uncoloured. Scribbling a colour out there does not
fill the background — the edge of the picture is background and cannot be
overruled, so the mark keeps roughly its own pixels. Carry a scribble off the
edge of the picture and the region it is in fills to that edge.

**A scribble wins the pixels it covers**, whatever the solver decided. The
solver's job is the pixels you said nothing about, so a mark is a manual
touch-up for anything the fill got wrong — dab on the spot and it takes that
colour. The marks are invisible wherever the fill agreed with them, because a
scribble carries the colour of the label it produces, so what you see is the
disagreement and nothing else.

The **None** swatch beside the colour scribbles *no colour at all*: the region it
wins is left empty, and the spots it covers have their colour taken back off
them. It is offered on colour layers only, where a mark is a label rather than
paint.

**Colour carries from drawing to drawing.** A drawing with no marks of its own
shows the nearest coloured drawing's, so colouring the first drawing of a run
colours the run; scribbling on a drawing takes it over from there, and the
drawings after it follow that one instead. Clearing a drawing's marks puts it
back to carrying. Nothing is copied and nothing is stored — it is resolved as it
is read, so reordering and deleting drawings change what follows what for free.

**And the marks move with the drawing.** Where a mark is carried to a drawing it
was not made on, it is shifted by however far the line art moved between the
two, measured from the drawings themselves and stored nowhere. Left where it was
drawn a carried mark holds its region only while the drawing has moved less than
about half that region's width — which between two drawings is not much — and
past that the region takes the wrong colour or none. It is one shift for the
whole drawing, so a shot where two things move apart is a shot where it is right
about one of them; that is what the switch is for.

The **Colour layer** box in the layer panel is where this is set: which layers
the fill is cut against, whether it carries at all, whether it carries forwards,
backwards or to whichever coloured drawing is nearer, and whether carried marks
move with the drawing. Cutting against a rough as well as a clean closes gaps
that leak from either alone, which is why several sources is the default. The
**Marks** column shows the scribbles instead of the fill.

**The fill is worked out beside the interface, not inside it.** A max-flow over
a 1080p drawing takes about a second and a half, so it happens on another
thread: the status bar says "colouring..." while it does, the last answer stays
on screen until the new one lands, and nothing waits. A coarse answer arrives
about a tenth of a second after the pen lifts and a full-resolution one replaces
it, so a stroke costs the coarse one and pausing is what buys the rest. Export
solves the same way, so the window keeps drawing while it does — and at the
resolution the drawing was made at rather than the interactive cap, which is
what the screen gets only after you have paused on a drawing.

**The timeline says where the colour came from.** A blue bar under a drawing's
number means its colour was carried there rather than drawn there, and an arrow
before the colour layer's name says the same about the drawing you are standing
on, with the drawing it came from in the tooltip. Both are a walk over the
drawings and cost nothing, so they are true everywhere whether or not you have
been there.

There was a warning beside them — an orange corner for carried marks that had
landed on nothing — and it was taken out because it fired on drawings whose
colour was perfectly good. What it was measuring, and why the measurement cannot
carry a flag, is in [handover.md](handover.md).

## Importing a soundtrack

**File ▸ Import ▸ Audio…** brings in a `.wav`, `.mp3`, `.m4a`, `.flac`, `.ogg`
or `.opus`. The file is copied into the project, so the project goes on working
after you move or delete the original.

**You can hear it by scrubbing.** Drag the playhead along the ruler — the strip
of frame numbers at the top of the timeline — and you hear the sound under it as
you go. Click a frame in the ruler and you hear that frame. That is how you find
which frame a consonant is on, which is most of what a soundtrack is for.

If you switch a speaker on, plug one in, or pull one out, Animage moves to
whatever the machine now uses. It does not wait to be asked: a take that is
playing carries on from the frame it had reached, on the new speaker.

**Play carries the sound with it**, and the picture follows the sound rather
than the other way round — so what you see on a frame is what you hear on it,
however busy the drawing is. There is a pause of about a quarter of a second
between pressing Play and the picture moving: that is the sound reaching the
speakers, and the picture waiting for it is what starting together means.

Nothing else makes a noise. Stepping through frames with the arrow keys is
silent on purpose: you do that all day while drawing, and it is not a request to
hear anything.

The dialog says how long the sound is **in frames** as well as in seconds —
which frame a sound is on is the whole of lipsync — and asks one thing: which
frame it starts on. **Before frame 1 is allowed**, and it is what you want when
a line of dialogue has a breath in front of the word: put the word on frame 1
and let the breath fall off the start.

If the sound runs past the end of your shot, the dialog offers to **make the
shot reach the end of the sound**. It is ticked when nothing has said how long
the shot is, and offered unticked when you have already set a length — an import
will not overrule a decision you made. It never makes the shot *shorter*.

### The soundtrack's row

A soundtrack gets a row under every drawing row. The block in it is where the
sound sits in the shot, **shaped by the sound itself** so you can see where the
syllables are, and it does three things:

| gesture | what it does |
|---|---|
| drag the block **sideways** | moves the sound along the shot |
| drag the block **up or down** | sets the level — the line across the block is where you have set it, and at the bottom it is silent |
| drag either **end** | crops the sound, without changing the file |

The waveform is the block's own top edge rather than a picture drawn on it, so
the level scales the whole shape: turn the sound down and the syllables shrink
with it, and at the bottom it is a flat line. It is drawn relative to the
loudest moment in that file rather than to full scale — a take recorded at a
sensible level would otherwise be a ripple with nothing readable in it — so the
shape tells you *where* the sound is and the block's height tells you how loud
it will be. The line across the block is the level itself: the waveform touches
it at the loudest moment in the file and stays under it everywhere else.

Nothing snaps to whole frames. A frame at 24 fps is 42 milliseconds, which is
most of the way to a syllable, so placing a sound to the nearest frame is not
placing it — the drag is as fine as the pixels are.

**Cropping takes nothing away.** It moves two numbers, so the whole take is
still there: drag the end back out and it returns. Cropping the front does not
move the rest of the sound — the audio under every frame you kept is the audio
that was there before.

**Clicking a soundtrack's row does not change which track you are drawing on.**
The row you clicked is highlighted; the track your brush is on keeps a fainter
version of the same highlight, so you can always see where it is. As soon as you
draw, the brush's row goes back to being the bright one.

**Rename it** by double-clicking its name, or with Track ▸ Rename. This changes
the label on the row and nothing else — the file in the project keeps the name
it had, and the row's tooltip says which file that is. Worth doing the moment
you have two takes both called `dialogue`.

**Delete it** with Track ▸ Delete track while its row is highlighted. The Track
menu always acts on the row you are pointed at, which is why "Overwrite
drawings" and "Past the last drawing" go grey while a soundtrack is highlighted:
neither means anything for a sound. Deleting takes the soundtrack out of the
shot and leaves the file in the project folder, and it undoes.

**Audio is not exported.** It is there to animate against; an exported sequence
has no sound in it.

## Exporting

**Exporting.** File ▸ Export sequences asks what to write — a sequence per
layer, the flattened picture, or both — in which format, and what to call the
export, then where to put it. The name is a folder of its own, so a shot's dozen sequence folders
arrive together instead of loose among whatever else was in the directory you
picked; it starts as the project's own name. Inside are the frames over the canvas
rectangle, a folder per layer:

```
the-shot/
  track-1_ink/     track-1_ink_0001.png     track-1_ink_0002.png  ...
  track-1_colour/  track-1_colour_0001.png  ...
  composite/       composite_0001.png       ...
```

The underscore separates the track from the layer from the frame number and
nothing else in a name is allowed to be one, so a layer called "layer 1" is
`layer-1` and the last number is always the frame. Hidden layers are not
written at all.

Name a layer whatever you like, up to sixty characters: punctuation and spaces
become hyphens, and accented and non-Latin letters are kept as they are. The one
thing to know is that this makes two names into one folder — `rough 1` and
`rough-1` both give `rough-1` — so if two layers of a track would collide, the
export stops before writing anything and tells you which two to rename. It
refuses rather than inventing a name, because a folder quietly called
`rough-1-2` is not one you would go looking for.

**16-bit PNG or EXR, and they are not the same picture.** PNG converts on
purpose — sRGB, unpremultiplied — and throws away about a third of what a
drawing holds; it is right where the destination expects PNG. EXR converts
nothing: half-float, linear light, premultiplied alpha, exactly the pixels the
compositor produced. So a frame written both ways holds different numbers, and
comparing them channel by channel will suggest one is broken when neither is. Colouring is solved for drawings nobody has opened — otherwise a
project straight off disk would export blank colour sequences — and it is solved
off the interface thread, so the progress dialog moves and Cancel answers.

**Exporting again over an old export replaces it**, after asking, rather than
writing in among it. Merging is the dangerous one: re-export a shot you have
since cut short and the old export's later frames sit after the new ones,
reading downstream as a perfectly well-formed sequence of the wrong length.
Cancelling halfway would splice two shots together at the seam. A folder that is
*not* an export — the project folder itself, most obviously — is never offered
for deletion; it asks for another name instead.

The layer panel on the right adds, removes, hides, fades and
[transforms](#transforming-a-whole-layer-through-time) layers, and layers are
restacked by dragging a row up or down the list — the top row is the top of the
stack. Layers belong to the track rather than to the image, which is the point of
the whole model: adding a layer touches no drawing, and a drawing held over five
frames holds every one of its layers for those five frames.
