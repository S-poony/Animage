# Why Animage has its own file formats

Written because this is the first question people ask about the repository, and
it is a fair one. Rolling your own file format is usually a mistake, so the
burden of proof is on us.

This document is meant to be readable without knowing the codebase.

It covers the cel (drawing) format, which we still have and would choose again.
The repository also used to contain a hand-written JSON reader, and **it no
longer does** — that was the weaker of the two decisions, it was challenged, and
the challenge was right. The last section says what happened, because a document
that only defends things is not worth reading.

## The short answer

Every off-the-shelf image format would have forced us to choose one of two
things we are not willing to lose:

1. **Pixels.** PNG, TIFF and every other integer format cannot store what a
   drawing actually contains without throwing some of it away.
2. **The infinite canvas.** PNG, OpenEXR and every other format stores a
   rectangle. Our drawings are not rectangles; they are scattered patches on an
   endless sheet.

Our format keeps both. It is 200 lines, it is documented byte for byte, and it
was written *after* the obvious approach was tried and measured.

## Three facts about the data

Everything else follows from these.

**1. A pixel is four half-floats.** Colour is stored in "linear light" as
16-bit floating point numbers (the format called *half*, or IEEE binary16). This
was decided in the original design document, before any code, and that document
calls it the one choice that is genuinely painful to reverse later.

Floating point spends its precision *relatively*: very fine steps near zero,
coarser steps near one. Integers are spaced evenly all the way along. This
difference is the whole reason PNG will not do, and it is explained below.

**2. A drawing has no edges.** A drawing is not an image of a fixed size. It is
a sparse grid of 128×128 pixel *tiles*, and a tile only exists where somebody
has actually drawn. Tile coordinates are signed, so you can draw as far left and
as far up as you like, forever.

This is what makes the canvas infinite. It is also why an empty layer costs
nothing, and why adding a layer to a 500-drawing scene takes no time and no
memory — there is a test pinning exactly that.

**3. A drawing is mostly nothing.** A line is thin and paper is wide. A
three-pixel line crossing a 128×128 tile touches about 384 of its 16,384 pixels
— roughly 2%. The other 98% is transparent.

That is not an edge case. It is what every frame of hand-drawn animation looks
like, which is the point returned to below.

## Why not PNG

PNG's 16-bit mode stores **integers**. Our pixels are **floating point**. These
are different number systems and there is no conversion between them that
survives a round trip.

Concretely, and measured rather than assumed: there are **15,362** distinct half
values between 0 and 1.

| Stored as | Values that survive |
|---|---|
| 16-bit integer, linear | 7,169 |
| 16-bit integer, sRGB-encoded first | 10,871 |
| our format | **all 15,362** |

So more than half of them collapse into each other, and some non-zero values
become exactly zero — a faint mark becomes no mark. The second row is the
standard trick for squeezing linear data into integers, and it helps, but
helping is not the same as being lossless.

There is a second problem: half-float reaches to 65504, while a 16-bit integer
image stops at 1.0. Nothing in Animage exceeds 1.0 today, but additive blending
is already in the layer model, and two half-bright layers added together come to
more than one.

**A save that loses pixels is not a save.** It is a lossy export that you cannot
tell apart from your master, which is worse than an export because you will keep
working from it.

### Would switching the pixels to 16-bit integer fix this?

It is a fair question and the answer is no, though not for the reason people
expect.

Precision would be *fine*. At 16 bits, evenly spaced integers are comfortably
detailed enough for 0–1 imagery — the darkest step is around 20× finer than an
8-bit sRGB code, and around 390× finer at mid-grey. Banding is a genuine
objection to 8-bit linear storage; it is not one here.

What you would actually lose is the range above 1.0, and about a day of careful
work through the hottest loops in the program. But the reason it does not help
is the next section: **PNG would still be the wrong shape**, whatever numbers
were in it.

## Why not OpenEXR, or any other rectangular format

OpenEXR is the obvious candidate once PNG is out. It stores half-float natively,
losslessly, and every tool in a visual effects pipeline reads it. We considered
it seriously and it was very nearly the answer.

The problem is shape, not precision. **Every image format stores a rectangle.**
Our drawings are sparse patches on an unbounded plane.

Saving a drawing as one rectangular image means:

- computing a bounding box around everything drawn
- filling in all the empty space inside that box
- on load, working out which parts came back empty, so that "an empty layer
  costs nothing" stays true

That last step is the one that matters. It is a tested invariant, and it is
what keeps memory and drawing speed proportional to *what you have drawn* rather
than to how big the canvas is or how many frames the scene has. A format that
forces us to rebuild it on every open is a format that can silently break it.

There is also a practical cost. A rectangular format stores the whole bounding
box and relies on its compressor to squeeze the emptiness back out. That is
precisely the approach we tried first, and it is what the measurements below
rejected.

## What our format actually does

Two ideas, both dull:

**Store only the tiles that exist.** A drawing writes the patches somebody drew
on and nothing else. No bounding box, no filling in the gaps.

**Within a tile, store only the occupied part of each row.** For each of the 128
rows, record where the ink starts and stops, then store only those pixels.

That is the entire idea. The row table costs 512 bytes per tile — against
131,072 bytes for a full tile, so about 0.4% overhead in the worst case, when a
tile really is completely painted.

The file starts with magic bytes, a version and the numbers needed to read the
rest, all little-endian. The layout is written out byte by byte at the top of
[`src/app/animage/project_io.h`](../src/app/animage/project_io.h). Anything that
can decompress a zlib stream can recover a drawing from it with that comment in
hand. That documentation is deliberate: it is standing in for "any image viewer
can open it", which is what we gave up.

## The numbers

Measured with `tests/bench_save.cpp` on a shot of 96 drawings at 1920×1080, each
with line art and a colour layer. First the naive version — tiles written whole,
which is what a rectangular format would also have done:

| | tiles written whole | occupied spans only |
|---|---|---|
| Save | 10,503 ms | **2,998 ms** |
| Open | 3,946 ms | **1,582 ms** |
| Handed to the compressor | 1,690 MB | 224 MB |
| Size on disk | 12.0 MB | **6.1 MB** |

Three and a half times faster to save, two and a half times faster to open, and
**half the size on disk**.

The last column is the surprising one and it is worth dwelling on. Storing less
made the files *smaller*, not larger. Writing tiles whole meant handing the
compressor 1.7 GB of which **92.6% was zeroes**, and asking it to work out that
they were nothing. Not writing them in the first place is both quicker and
tighter than compressing them well.

This is also the answer to "why not just turn the compression up". Compression
level 9 costs seven times level 6 and saves 6% of the size. The saving was never
in the compressor.

## What this means at the drawing board

The 98%-empty figure above is not a corner case chosen to flatter the design.
It is the normal state of the work, and it gets *more* favourable, not less, as
a shot progresses.

**Roughs and line art.** A drawing is lines on nothing — a few percent coverage.
This is the overwhelming majority of what a project contains while it is being
animated. Every one of those drawings is nearly all empty.

**Colouring.** Coverage goes up, but less than you would think, because a colour
layer does not store colour. It stores the *scribbles* — the rough marks you
scrawl inside a region — and the filled result is regenerated from them and
never written to disk. So a fully coloured drawing still only stores line art
plus a handful of marks.

**The densest images never reach us.** The point where a frame is genuinely full
— a painted background, effects, everything composited together — happens
*outside* Animage, in a compositor, from the sequence we export. Animage's own
worst case is rarer than it looks, and even that case only pays 0.4% overhead.

The practical result: a 96-drawing shot is a **6 MB folder**. It fits in email.
It goes into version control. Backing up a project is copying a directory.

## The infinite canvas, and zooming and panning

These matter, so it is worth being precise about what the file format does and
does not do for them.

**The file format does not make panning fast.** What makes drawing, zooming and
panning cost time proportional to what you drew — rather than to the size of the
canvas — is the sparse tile model itself: the compositor walks the tiles that
exist and skips the ones that do not.

**What the file format does is refuse to compromise that model.** A rectangular
save format applies quiet pressure in the other direction. Once a drawing has to
become a rectangle to be written down, a bounded document starts to look like
the simpler design, and the infinite canvas is exactly the sort of thing that
gets traded away for a simpler file. Our format stores what the model holds, so
the model never has to bend to it.

There is a concrete version of this too. A project you have just opened must
behave identically to one you have just drawn. If loading rebuilt drawings from
a rectangle, it would have to correctly detect which tiles came back empty; get
that wrong and a freshly opened project quietly carries tiles full of nothing,
costing memory and compositing time on every frame, for the rest of the session.
Our format cannot make that mistake, because absent tiles were never written.

## "Isn't this over-optimising for a narrow case?"

The honest response, point by point.

**The case is not narrow — it is the only case.** Every drawing, in every
project, is sparse and mostly empty. There is no second kind of content that
Animage stores. If this were a photo editor the answer would be completely
different, and PNG would be right.

**It was not optimised first.** The first version wrote tiles whole. It was
written, it worked, it was measured, and it took 10.5 seconds to save a shot and
4 seconds to open one — every single time, whether or not anything had changed.
The change was a response to a measurement, not a guess about one. The benchmark
that produced those numbers is committed alongside.

**Both alternatives were considered and one was nearly chosen.** OpenEXR lost on
shape, after the argument had gone the other way for a while. The reasoning is
recorded rather than assumed.

**It is small and it is fenced.** The format is about 200 lines in one file with
one job. It has tests covering exact round trips of every awkward value,
transparent tiles being dropped, byte-identical output for unchanged drawings,
and corrupt files being refused rather than half-read — including a file that
lies about how much it contains, which taken on trust is a request to allocate a
terabyte.

**Where the criticism does land:** we cannot open a cel in another program.
That is a real cost, it was the strongest argument for OpenEXR, and we paid it
knowingly. It is mitigated by documenting the layout precisely, by keeping the
scene structure in plain text so only pixels are ever at risk, and by the fact
that export — which is what you hand to anybody else — is an ordinary image
sequence.

## What would change the decision

Not dogma. Any of these would reopen it:

- **If cels needed to be readable by other software** more than they need to fit
  the sparse model — for instance if people started exchanging individual
  drawings rather than exported sequences — OpenEXR becomes the right answer and
  the conversion is mechanical.
- **If the drawing surface became bounded**, the shape objection disappears and
  a standard format is straightforwardly better.
- **If pixels moved to 16-bit integer *and* the surface became bounded**, PNG
  becomes both lossless and appropriate, and we should use it.

Note that the third needs *both*. Changing the pixel type alone buys nothing,
which is the most common misunderstanding about this decision.

## The JSON parser, which we no longer have

This section is kept because the episode is more instructive than the outcome.

The repository used to contain a hand-written JSON reader and writer, about 350
lines with its own tests. It was removed in
[#19](https://github.com/S-poony/Animage/pull/19); the scene file is read and
written with Qt's JSON now.

**Why it existed.** `animage_core` — the data model, undo, compositing, the
colour solver — has no external dependencies at all, deliberately, and that
includes Qt. That rule is what lets the model be tested headlessly, and it is
why the trickiest parts of the model are trustworthy. The scene structure was
saved by `core`, so its serialiser could not reach for Qt's JSON.

That reasoning was sound and the conclusion was still wrong, which is worth
sitting with. The premise nobody questioned was that the *serialiser* belonged in
`core`. It did not. Files are an application concern, and once the whole of
saving and loading moved out — which is what #19 did — `core` became more purely
the model than it was before, and the JSON question simply evaporated. **The
argument was won on the wrong ground.**

**It also had two real bugs**, both found by writing hostile files at it, and
both of the kind hand-written parsers are known for:

- **Unbounded recursion.** The parser descended one stack frame per nesting
  level with no limit. A file of a hundred thousand open brackets — about 200 KB
  — crashed the program outright. Opening a project you were sent is reading
  somebody else's data, so this was a genuine defect and not a curiosity.
- **Undefined behaviour on out-of-range numbers.** A canvas width of `1e300` was
  converted straight to an integer. In C++ that conversion is undefined when the
  value does not fit, which means the compiler is entitled to do anything at all.

Neither would have existed in a library that thousands of projects have already
attacked. Both are now pinned as tests
([`tests/test_hostile.cpp`](../tests/test_hostile.cpp)) so they cannot return by
another route.

**What survived the change**, because they were requirements rather than
preferences: numbers are still written in the shortest form that reads back
exactly, so an opacity of 0.6 appears as `0.6` and not `0.6000000238418579`; and
saving the same scene twice still produces identical bytes, so a diff shows what
changed. Key order is alphabetical now rather than the order we wrote them —
still deterministic, slightly less nicely grouped, and not worth 350 lines.

**The general lesson.** "Our constraints rule out the library" deserves the
follow-up question "are these the right constraints, in the right place?". Here
they were the right constraints applied to the wrong module, and the fix was to
move the module rather than to keep the code.

The cel format is not in that position, and the distinction is the whole point of
this document: no library choice would have worked, because the objection is to
the *shape* of the data and not to who wrote the parser.

## Where to look

| | |
|---|---|
| [`src/app/animage/project_io.h`](../src/app/animage/project_io.h) | All of saving and loading: the cel format documented byte by byte, `scene.json`, and the folder |
| [`src/core/tile.h`](../src/core/tile.h) | Sparse tiles: where the infinite canvas comes from |
| [`tests/bench_save.cpp`](../tests/bench_save.cpp) | The measurements quoted here |
| [`tests/test_serialise.cpp`](../tests/test_serialise.cpp) | Round trips, and the corrupt-file cases |
| [`tests/test_hostile.cpp`](../tests/test_hostile.cpp) | Files nobody sane wrote, and the crashes they used to cause |
| [`docs/handover.md`](handover.md) | The short version, among the other departures from the plan |

Note that `core` no longer contains any of this. It holds the model and nothing
about files, which is the arrangement the JSON episode above arrived at.
