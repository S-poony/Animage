# M0 — measuring pen-to-pixel latency

The gate before everything else. If latency is above 25 ms the tool will be
unpleasant whatever is built around it, and it is far cheaper to find that out
now than after the rendering architecture has been assumed by fifty thousand
lines of code.

## Running it

```bash
cmake --build build --target animage_m0_latency
```

The binary lands at `build/src/app/animage_m0_latency`. It keeps a console
open: the device listing at startup and the report on exit both go to stdout.

| Key | |
|---|---|
| `M` | camera mode: on-screen millisecond clock, everything else hidden |
| `C` | clear the canvas |
| `H` | hide the HUD |
| `X` | hide the crosshair |
| `S` | print a report to the console |
| `F` | fullscreen |
| `Q` / `Esc` | quit |

Turning the stylus over uses the eraser, which is a quick check that pointer
type is being reported.

## What the HUD number is not

The HUD reports the time from a tablet event arriving in the widget to the
frame that carries it being painted. That is the application's own share and
nothing else. It cannot see:

- the tablet firmware and the USB or Bluetooth link;
- the vendor driver and the WinTab or Windows Ink stack;
- the compositor;
- the display's own pipeline and pixel response.

Those are most of the budget. A flattering HUD number tells you the application
is not the bottleneck; it does not tell you the tool will feel good.

## The measurement that counts

**Press `M` first.** Camera mode clears the screen down to the stroke and a
large four-digit millisecond clock, and it is what makes the measurement work
without knowing anything about the camera.

Counting frames requires knowing the frame rate, and phones are unhelpful here:
slow-motion modes are relabelled between models and often are not reported at
all. So the clock on screen is a real timestamp rather than a frame counter.
Read it in the frame where the pen tip reaches a point, read it again in the
frame where ink appears under it, and subtract. **That difference is the
latency in milliseconds.** The camera's frame rate never enters the arithmetic.

**Procedure.** Film the pen tip and the screen together, in slow motion if the
phone offers it. Draw a fast, straight, steady stroke. Step through the footage
and find:

1. the frame where the tip is physically over a chosen point — note the digits;
2. the frame where ink first appears at that point — note the digits again.

Do it three times and take the worst, not the average.

The square next to the digits flips light and dark on every repaint. Digits
smear when the exposure is long; a block that is simply light or dark survives
that, and a skipped step in the sequence is easy to spot.

**Resolution is one display refresh**, because that is how often the digits can
change — about 17 ms on a 60 Hz panel. The measurement cannot be finer than the
thing being measured.

**The crosshair trick, when there is no camera.** With the crosshair on, move
the pen steadily and look at the gap between the physical tip and the red
cross. Gap divided by pen speed is the latency. It is rough — it misses
everything downstream of the paint call — but it makes a 60 ms problem obvious
in five seconds.

## The runs that are needed

The plan asks for all of these, because they are separate code paths and there
is no reason to expect them to agree:

| Platform | Path | Result | Notes |
|---|---|---|---|
| Windows | Windows Ink | | Qt 6's default |
| Windows | WinTab | | still what many professionals run |
| Linux | libinput / XInput2 | | |
| macOS | NSEvent | | optional; free if Qt behaves |

Record the display refresh rate and the tablet's event rate alongside each
figure — the HUD shows both. A 60 Hz panel puts a floor of roughly 16 ms under
any result, so a 60 Hz measurement and a 144 Hz measurement are not comparable.

**Pass: under 25 ms.** Above it, the cause has to be found before M1's
structures get an interface built on them.

Selecting WinTab rather than Windows Ink under Qt 6 has not been verified yet.
It needs checking against the Qt version in use rather than guessing, and the
answer belongs in this table once it is known.

## Recording the result

Add a row to this file with the date, the machine, the tablet, the driver
version, the refresh rate and the measured figure. Latency regresses quietly;
without a baseline written down there is nothing to regress against.

| Date | Machine | Tablet | Path | Refresh | Event rate | App share | Camera |
|---|---|---|---|---|---|---|---|
| 2026-08-03 | Windows 11, windowed | `wmpointer` (pen) | Windows Ink | 59.94 Hz | 189 Hz | 0.03 ms median, 8.1 ms worst | pending |
| 2026-08-03 | Windows 11, fullscreen | `wmpointer` (pen) | Windows Ink | 59.94 Hz | 149 Hz | 0.05 ms median, 3.6 ms worst | pending |
| | | | WinTab | | | | not yet measured |

Fullscreen did not move the application's share, which was already negligible.
Whether it removed the DWM composition frame cannot be seen from inside the
process at all — that difference only shows up on camera, and it is the main
reason the windowed and fullscreen runs both need filming.

## What the first measurement showed

The application's own share is **0.03 ms median, 0.08 ms at p95**. That is
nothing. Whatever the camera ends up saying, essentially none of it is being
added by our code, and optimising the drawing path further would be optimising
a rounding error.

The queue-depth figure equals the ink-lag figure, which means events are never
backing up: at 189 Hz the tablet reports about three times per frame and each
one is painted before the next arrives.

So the budget is spent downstream, and on a 59.94 Hz display it is roughly:

| Stage | Cost | Whose |
|---|---|---|
| Pen sampling at 189 Hz | 0–5.3 ms | tablet firmware |
| Driver and Windows Ink | ? | Microsoft, Wacom |
| **Application** | **0.03 ms** | **ours** |
| Waiting for vsync | 0–16.7 ms | unavoidable |
| DWM composition | ~16.7 ms | Windows |
| Scanout and pixel response | ~8–16 ms | the panel |

That totals something in the region of 35–55 ms, or two to three frames at
60 Hz — and it is consistent with a camera showing several frames of lag.

### The uncomfortable consequence

**On a 59.94 Hz display, the 25 ms criterion is close to physically
unreachable.** One frame is already 16.7 ms, and a composited desktop normally
holds two to three. No amount of work on our side changes that.

This does not invalidate the criterion, but it does relocate it. 25 ms is a
statement about the hardware an animator will be using, not about this code.
It should be re-measured on a 120 Hz or 144 Hz panel before being treated as a
pass or a fail, because that is where the two largest terms roughly halve.

### What is actually still worth trying

In descending order of expected effect:

1. **A higher refresh display.** 120 Hz halves the vsync wait and the
   composition frame. This is the single biggest lever and it is hardware.
2. **Fullscreen.** Windows can promote a fullscreen window to direct scanout
   and skip DWM composition entirely, which is worth a whole frame. Press `F`
   and measure again — this is free if it works.
3. **WinTab instead of Windows Ink.** A different driver path with different
   buffering. Still unmeasured, and the plan asks for it.
4. **Late latching.** Deferring the paint until just before vsync instead of
   painting as soon as the event arrives cuts most of the 0–16.7 ms wait. This
   is what the fast drawing applications do, and it is an M2 concern rather
   than a fix to apply here.
5. **Pen prediction.** Extrapolating the stroke ahead of the reported position
   hides latency rather than removing it. It is a real technique and it costs
   accuracy at the ends of strokes. Worth knowing about, not worth doing yet.
