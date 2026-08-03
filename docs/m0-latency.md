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
| `C` | clear the canvas |
| `H` | hide the HUD (do this before filming) |
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

**Camera.** Film the pen tip and the screen together at 120 fps or more — a
recent phone in slow-motion mode is enough. Draw a fast, straight, steady
stroke. Step through the recording and count the frames between the tip
reaching a point and ink appearing under it. At 240 fps each frame is 4.2 ms.

Do this three times and take the worst, not the average.

**Before filming:** press `H` to hide the HUD and `X` to hide the crosshair.
Both add drawing work per frame and the crosshair is easy to mistake for ink.

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

| Date | Machine | Tablet | Path | Refresh | Event rate | Measured |
|---|---|---|---|---|---|---|
| | | | | | | |
