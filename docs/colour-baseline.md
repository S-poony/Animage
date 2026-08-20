# The colour benchmarks, phase by phase

The measurements [colour without a canvas](colour-without-a-canvas.md) gates
itself on. Phase 0 of that plan is a measurement rather than a change, and every
later gate reads against it -- so the output is kept verbatim rather than
summarised. A summary is what would let a later run be compared against what
somebody remembered.

Each phase adds a section. Nothing here is edited afterwards: a number that was
measured stays as it was measured, and a number that moved is a section further
down.

Read the differences between rows and not the digits. The millisecond columns
move by a few per cent between runs on the same build, and `bench_carry`'s
coverage and leak are the only numbers here that are exact.

## Phase 0 -- before any of it

- **Recorded** 2026-08-20, at commit `a0c63d0`, before phase 1.
- **Where** Intel Core i7-10700 at 2.90 GHz, 8 cores, 16 GB, Windows 11.
- **How** MSYS2 UCRT64, `RelWithDebInfo`, one run each.

```powershell
./build/tests/bench_carry
./build/tests/bench_composite
./build/tests/bench_playback -platform offscreen
```

### bench_carry

```
Carrying a mark unchanged, against a shape that moves.

The mark is drawn on drawing 1 and inherited by the rest. Coverage is how
much of the shape it fills there; leak is how much of the world outside
the shape it fills as well. What is being asked is how much motion a mark
survives with nothing moving it -- which is what decides whether anything
past 'carry it unchanged' is worth building.

  shape 300x300, gap 60, mark radius 30, moving 0 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2       0      100.0%     0.0%     7.70       0
        3       0      100.0%     0.0%     7.70       0
        4       0      100.0%     0.0%     7.70       0
        5       0      100.0%     0.0%     7.70       0
        6       0      100.0%     0.0%     7.70       0

  shape 300x300, gap 60, mark radius 30, moving 20 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      20      100.0%     0.0%     7.70       0
        3      40      100.0%     0.0%     7.70       0
        4      60      100.0%     0.0%     7.75       0
        5      80      100.0%     0.2%     7.85       0
        6     100      100.0%     0.4%     7.95       0

  shape 300x300, gap 60, mark radius 30, moving 40 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      40      100.0%     0.0%     7.70       0
        3      80      100.0%     0.2%     7.85       0
        4     120      100.0%     0.7%     8.05       0
        5     160      100.0%     1.1%     8.26       0
        6     200      100.0%     1.5%     8.46       0

  shape 300x300, gap 60, mark radius 30, moving 80 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      80      100.0%     0.2%     7.85       0
        3     160      100.0%     1.1%     8.26       0
        4     240      100.0%     2.0%     8.67       0
        5     320        0.0%     2.2%     1.00       0
        6     400        0.0%     2.1%     1.00       0

  shape 300x300, gap 60, mark radius 12, moving 0 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2       0      100.0%     0.0%    21.88       0
        3       0      100.0%     0.0%    21.88       0
        4       0      100.0%     0.0%    21.88       0
        5       0      100.0%     0.0%    21.88       0
        6       0      100.0%     0.0%    21.88       0

  shape 300x300, gap 60, mark radius 12, moving 20 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      20      100.0%     0.0%    21.88       0
        3      40      100.0%     0.0%    21.88       0
        4      60      100.0%     0.0%    21.88       0
        5      80      100.0%     0.0%    21.98       0
        6     100      100.0%     0.1%    22.10       0

  shape 300x300, gap 60, mark radius 12, moving 40 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      40      100.0%     0.0%    21.88       0
        3      80      100.0%     0.0%    21.98       0
        4     120      100.0%     0.2%    22.22       0
        5     160      100.0%     0.4%    22.46       0
        6     200      100.0%     0.6%    22.70       0

  shape 300x300, gap 60, mark radius 12, moving 80 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      80      100.0%     0.0%    21.98       0
        3     160      100.0%     0.4%    22.46       0
        4     240        0.0%     0.8%     1.03       0
        5     320        0.0%     0.8%     1.00       0
        6     400        0.0%     0.7%     1.00       0

  shape 300x300, gap 60, mark radius 30, moving 0 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2       0      100.0%     0.0%     7.70       0
        3       0      100.0%     0.0%     7.70       0
        4       0      100.0%     0.0%     7.70       0
        5       0      100.0%     0.0%     7.70       0
        6       0      100.0%     0.0%     7.70       0

  shape 300x300, gap 60, mark radius 30, moving 20 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      20      100.0%     0.0%     7.70      18
        3      40      100.0%     0.0%     7.70      36
        4      60      100.0%     0.0%     7.70      60
        5      80      100.0%     0.0%     7.70      78
        6     100      100.0%     0.0%     7.70      98

  shape 300x300, gap 60, mark radius 30, moving 40 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      40      100.0%     0.0%     7.70      36
        3      80      100.0%     0.0%     7.70      78
        4     120      100.0%     0.0%     7.70     119
        5     160      100.0%     0.0%     7.70     161
        6     200      100.0%     0.0%     7.70     203

  shape 300x300, gap 60, mark radius 30, moving 80 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      80      100.0%     0.0%     7.70      78
        3     160      100.0%     0.0%     7.70     161
        4     240      100.0%     0.0%     7.70     240
        5     320      100.0%     0.0%     7.70     320
        6     400      100.0%     0.0%     6.42     396

  shape 300x300, gap 60, mark radius 12, moving 0 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2       0      100.0%     0.0%    21.88       0
        3       0      100.0%     0.0%    21.88       0
        4       0      100.0%     0.0%    21.88       0
        5       0      100.0%     0.0%    21.88       0
        6       0      100.0%     0.0%    21.88       0

  shape 300x300, gap 60, mark radius 12, moving 20 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      20      100.0%     0.0%    21.88      18
        3      40      100.0%     0.0%    21.88      36
        4      60      100.0%     0.0%    21.88      60
        5      80      100.0%     0.0%    21.88      78
        6     100      100.0%     0.0%    21.88      98

  shape 300x300, gap 60, mark radius 12, moving 40 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      40      100.0%     0.0%    21.88      36
        3      80      100.0%     0.0%    21.88      78
        4     120      100.0%     0.0%    21.88     119
        5     160      100.0%     0.0%    21.88     161
        6     200      100.0%     0.0%    21.88     203

  shape 300x300, gap 60, mark radius 12, moving 80 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      80      100.0%     0.0%    21.88      78
        3     160      100.0%     0.0%    21.88     161
        4     240      100.0%     0.0%    21.88     240
        5     320      100.0%     0.0%    21.88     320
        6     400       92.5%     0.0%    16.93     396


And with a neighbour to be wrong about: two halves, a mark in each.
Left red and right blue are the fill being right; right red is the
colour of one region landing in the other, which is the failure the
single-shape table above cannot show.

  two 150x300 halves, a mark in each, radius 30, moving 20 px a drawing, marks stay
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      20        99.8%       100.0%        0.0%     6.32       0
        3      40        96.5%       100.0%        0.0%     6.28       0
        4      60        93.3%       100.0%        0.0%     6.28       0
        5      80         6.9%       100.0%        0.0%     1.00       0
        6     100         3.7%       100.0%        0.0%     1.00       0

  two 150x300 halves, a mark in each, radius 30, moving 40 px a drawing, marks stay
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      40        96.5%       100.0%        0.0%     6.28       0
        3      80         6.9%       100.0%        0.0%     1.00       0
        4     120         0.7%       100.0%        0.0%     1.00       0
        5     160         0.0%         0.0%        0.0%     1.22       0
        6     200         0.0%         0.0%        0.0%     1.00       0

  two 150x300 halves, a mark in each, radius 30, moving 80 px a drawing, marks stay
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      80         6.9%       100.0%        0.0%     1.00       0
        3     160         0.0%         0.0%        0.0%     1.22       0
        4     240         0.0%         0.0%        0.0%     1.00       0
        5     320         0.0%         0.0%        0.0%     1.00       0
        6     400         0.0%         0.0%        0.0%     1.00       0

  two 150x300 halves, a mark in each, radius 30, moving 20 px a drawing, marks follow
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      20       100.0%       100.0%        0.0%     6.31      18
        3      40       100.0%       100.0%        0.0%     6.31      36
        4      60       100.0%       100.0%        0.0%     6.31      60
        5      80       100.0%       100.0%        0.0%     6.31      78
        6     100       100.0%       100.0%        0.0%     6.31      98

  two 150x300 halves, a mark in each, radius 30, moving 40 px a drawing, marks follow
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      40       100.0%       100.0%        0.0%     6.31      36
        3      80       100.0%       100.0%        0.0%     6.31      78
        4     120       100.0%       100.0%        0.0%     6.31     119
        5     160       100.0%       100.0%        0.0%     6.31     161
        6     200       100.0%       100.0%        0.0%     6.31     196

  two 150x300 halves, a mark in each, radius 30, moving 80 px a drawing, marks follow
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      80       100.0%       100.0%        0.0%     6.31      78
        3     160       100.0%       100.0%        0.0%     6.31     161
        4     240       100.0%       100.0%        0.0%     6.31     240
        5     320       100.0%       100.0%        0.0%     6.31     320
        6     400         0.0%         0.0%        0.0%     1.11     252


And the same with nothing defending the neighbouring region: only the
left half is marked. Right red is then the colour landing in a region it
was never meant for -- the failure the design note calls 'the wrong region
of about the right size', which no flag catches.

  two 150x300 halves, a mark in the left half only, radius 30, moving -20 px a drawing, marks stay
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%         0.0%        0.0%     6.31       0
        2     -20       100.0%         0.0%      100.0%    12.78       0
        3     -40       100.0%         0.0%      100.0%    12.78       0
        4     -60       100.0%         0.0%      100.0%    12.78       0
        5     -80       100.0%         0.0%      100.0%    12.78       0
        6    -100       100.0%         0.0%      100.0%    12.78       0

  two 150x300 halves, a mark in the left half only, radius 30, moving -40 px a drawing, marks stay
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%         0.0%        0.0%     6.31       0
        2     -40       100.0%         0.0%      100.0%    12.78       0
        3     -80       100.0%         0.0%      100.0%    12.78       0
        4    -120       100.0%         0.0%      100.0%    12.78       0
        5    -160         0.0%         0.0%      100.0%     6.31       0
        6    -200         0.0%         0.0%      100.0%     6.60       0

  two 150x300 halves, a mark in the left half only, radius 30, moving -20 px a drawing, marks follow
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%         0.0%        0.0%     6.31       0
        2     -20       100.0%         0.0%        0.0%     6.31     -24
        3     -40       100.0%         0.0%        0.0%     6.31     -42
        4     -60       100.0%         0.0%        0.0%     6.31     -60
        5     -80       100.0%         0.0%        0.0%     6.31     -84
        6    -100       100.0%         0.0%        0.0%     6.31    -102

  two 150x300 halves, a mark in the left half only, radius 30, moving -40 px a drawing, marks follow
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%         0.0%        0.0%     6.31       0
        2     -40       100.0%         0.0%        0.0%     6.31     -42
        3     -80       100.0%         0.0%        0.0%     6.31     -84
        4    -120       100.0%         0.0%        0.0%     6.31    -120
        5    -160       100.0%         0.0%        0.0%     6.31    -161
        6    -200       100.0%         0.0%        0.0%     6.31    -196

all of it in 15.3 s
```

### bench_composite

```
compositing a 1150x640 viewport

1 layer       77 tiles
    no margin           2.33 ms
    margin  64 px       3.03 ms   (1.30x)
    margin 192 px       4.59 ms   (1.97x)
2 layers     154 tiles
    no margin           2.81 ms
    margin  64 px       4.23 ms   (1.51x)
    margin 192 px       6.29 ms   (2.24x)
3 layers     234 tiles
    no margin           3.69 ms
    margin  64 px       5.27 ms   (1.43x)
    margin 192 px       7.90 ms   (2.14x)
4 layers     309 tiles
    no margin           4.70 ms
    margin  64 px       6.55 ms   (1.39x)
    margin 192 px       9.88 ms   (2.10x)

zoomed out, 4 layers over a wide drawing
    zoom  1.00 ( 1.00 image px an entry, read every 1)     2.12 ms
    zoom  0.70 ( 1.43 image px an entry, read every 1)     4.24 ms
    zoom  0.50 ( 2.00 image px an entry, read every 1)     5.89 ms
    zoom  0.20 ( 5.00 image px an entry, read every 2)    15.30 ms
    zoom  0.10 (10.00 image px an entry, read every 4)    14.01 ms
    zoom  0.05 (20.00 image px an entry, read every 7)    16.81 ms

LazyBrush: three boxed regions on a background, each wall gapped
     128x128         7.2 ms   (4 cuts)
     256x256        27.2 ms   (4 cuts)
     512x512       113.6 ms   (4 cuts)
    1024x1024      606.7 ms   (4 cuts)

A CTG solve on a 1920x1080 drawing, coarse then full:
    motion estimate, paid once per solve         9.9 ms  (0, 0)
    first    budget   262144  step 3      127.1 ms  (3 colours, 84 tiles)
    refined  budget  4194304  step 1     1660.9 ms  (3 colours, 84 tiles)

A frame at 60 Hz is 16.7 ms. Scrubbing wants one of these per frame.
```

### bench_playback

```
What playback costs, and what it drops. A frame at 24 fps is 41.7 ms.

a shot you would review
  1920x1080 canvas, 2 tracks, 24 drawings on 2s = 48 frames, canvas widget 1640x870 at 81%
                  1612 tiles
                per frame: slot / canvas / timeline      frame: med / p95 / worst      shown at 24 fps    real timer
  line art       0.04 /  12.14 /  0.68 ms           12.89 /  14.60 /  15.12       48 of 48          47 of 47 (48 painted)
  coloured       0.04 /  14.63 /  0.81 ms           15.49 /  16.89 /  18.23       48 of 48          47 of 47 (48 painted)
                fill covers 26% of the canvas when solved
                48 drawings, 48 fills held (1626 tiles), 0 solves during 2 s of playback

the same at 4K
  3840x2160 canvas, 2 tracks, 24 drawings on 2s = 48 frames, canvas widget 3560x1950 at 90%
                  3727 tiles
                per frame: slot / canvas / timeline      frame: med / p95 / worst      shown at 24 fps    real timer
  line art       0.05 /  52.45 /  0.93 ms           53.44 /  56.18 /  63.28       38 of 48          40 of 49 (41 painted)
  coloured       0.05 /  68.10 /  1.04 ms           69.17 /  72.12 /  80.52       30 of 48          29 of 48 (30 painted)
                fill covers 24% of the canvas when solved
                48 drawings, 20 fills held (1990 tiles), 0 solves during 2 s of playback

four tracks, 96 frames
  1920x1080 canvas, 4 tracks, 48 drawings on 2s = 96 frames, canvas widget 1640x870 at 81%
                  6452 tiles
                per frame: slot / canvas / timeline      frame: med / p95 / worst      shown at 24 fps    real timer
  line art       0.08 /  14.14 /  2.31 ms           16.52 /  18.02 /  18.69       96 of 96          47 of 47 (48 painted)
  coloured       0.08 /  18.23 /  2.57 ms           20.83 /  22.29 /  24.92       96 of 96          47 of 47 (55 painted)
                fill covers 19% of the canvas when solved
                192 drawings, 62 fills held (2029 tiles), 8 solves during 2 s of playback

The two right-hand columns have to agree on slots. The deterministic one is
what to optimise against; the real timer is the cross-check, and it is the
one that caught this file's own drop model counting 53 frames out of 48.

The painted count is what reached the screen, and it agrees with the slot
count here. A shortfall between them would mean two slot changes collapsed
into one paint, which is what overrunning looks like from the inside.
```


## Phase 1 -- the fill stops being a picture

- **Recorded** 2026-08-20, at commit `4e01aff`, same machine and same build
  type as above.

The gate for this phase was not a benchmark: `ctgFillPixel` was asserted equal
to the tiles it replaces, at every pixel of the canvas and two tiles beyond it,
over seven situations, and then the tiles were deleted. What is below is the
second half of the gate -- read the coloured rows.

| | phase 0 | phase 1 |
|---|---|---|
| HD coloured frame | 14.63 ms | 15.52 ms |
| 4K coloured frame | 68.10 ms | 64.16 ms |
| four tracks, coloured | 18.23 ms | 19.06 ms |
| HD fills held | 48 of 48 | 48 of 48 |
| 4K fills held | **20 of 48** | **40 of 48** |
| four tracks, fills held | **62 of 192** | **127 of 192** |
| a 1080p fill | 84 tiles, ~10.5 MB | 4050 KB |
| coarse solve, 1080p | 127.1 ms | 93.9 ms |

The frame times are the same to within the few per cent a run moves by; the
cache holds about twice as much of a shot, which is what the change was for.
The coarse solve is 33 ms quicker because there is no paint-out loop in it any
more.

The line-art rows are untouched, which is the other half of what wants
checking: a colour layer that got faster by making everything else slower would
not show up in the coloured column alone.

### bench_carry

Identical to phase 0, byte for byte, every coverage, leak, spread and shift on
every row. Only the wall-clock line at the bottom differs (14.9 s against
15.3 s). That is the strongest statement available that the fill's *answer* did
not change, and it is why it is recorded as an identity rather than pasted
again.

### bench_composite

```
compositing a 1150x640 viewport

1 layer       77 tiles
    no margin           1.94 ms
    margin  64 px       2.97 ms   (1.53x)
    margin 192 px       4.64 ms   (2.39x)
2 layers     154 tiles
    no margin           2.89 ms
    margin  64 px       4.07 ms   (1.41x)
    margin 192 px       6.33 ms   (2.19x)
3 layers     234 tiles
    no margin           3.64 ms
    margin  64 px       5.38 ms   (1.48x)
    margin 192 px       8.08 ms   (2.22x)
4 layers     309 tiles
    no margin           4.61 ms
    margin  64 px       6.47 ms   (1.40x)
    margin 192 px       9.45 ms   (2.05x)

zoomed out, 4 layers over a wide drawing
    zoom  1.00 ( 1.00 image px an entry, read every 1)     2.15 ms
    zoom  0.70 ( 1.43 image px an entry, read every 1)     4.38 ms
    zoom  0.50 ( 2.00 image px an entry, read every 1)     5.40 ms
    zoom  0.20 ( 5.00 image px an entry, read every 2)    15.08 ms
    zoom  0.10 (10.00 image px an entry, read every 4)    13.81 ms
    zoom  0.05 (20.00 image px an entry, read every 7)    16.64 ms

LazyBrush: three boxed regions on a background, each wall gapped
     128x128         7.1 ms   (4 cuts)
     256x256        26.2 ms   (4 cuts)
     512x512       112.5 ms   (4 cuts)
    1024x1024      601.0 ms   (4 cuts)

A CTG solve on a 1920x1080 drawing, coarse then full:
    motion estimate, paid once per solve         9.8 ms  (0, 0)
    first    budget   262144  step 3       93.9 ms  (3 colours, 450 KB)
    refined  budget  4194304  step 1     1624.4 ms  (3 colours, 4050 KB)

A frame at 60 Hz is 16.7 ms. Scrubbing wants one of these per frame.
```

### bench_playback

```
What playback costs, and what it drops. A frame at 24 fps is 41.7 ms.

a shot you would review
  1920x1080 canvas, 2 tracks, 24 drawings on 2s = 48 frames, canvas widget 1640x870 at 81%
                  1612 tiles
                per frame: slot / canvas / timeline      frame: med / p95 / worst      shown at 24 fps    real timer
  line art       0.04 /  12.10 /  0.69 ms           12.82 /  14.85 /  15.66       48 of 48          47 of 47 (48 painted)
  coloured       0.04 /  15.52 /  0.81 ms           16.40 /  17.74 /  19.36       48 of 48          47 of 47 (48 painted)
                fill covers 26% of the canvas when solved
                48 drawings, 48 fills held (117 MB), 0 solves during 2 s of playback

the same at 4K
  3840x2160 canvas, 2 tracks, 24 drawings on 2s = 48 frames, canvas widget 3560x1950 at 90%
                  3727 tiles
                per frame: slot / canvas / timeline      frame: med / p95 / worst      shown at 24 fps    real timer
  line art       0.05 /  52.38 /  0.94 ms           53.44 /  57.64 /  58.75       38 of 48          38 of 48 (39 painted)
  coloured       0.05 /  64.16 /  1.05 ms           64.91 / 105.10 / 107.76       30 of 48          27 of 48 (28 painted)
                fill covers 24% of the canvas when solved
                48 drawings, 40 fills held (252 MB), 2 solves during 2 s of playback

four tracks, 96 frames
  1920x1080 canvas, 4 tracks, 48 drawings on 2s = 96 frames, canvas widget 1640x870 at 81%
                  6452 tiles
                per frame: slot / canvas / timeline      frame: med / p95 / worst      shown at 24 fps    real timer
  line art       0.09 /  14.39 /  2.46 ms           16.94 /  18.26 /  19.71       96 of 96          47 of 47 (48 painted)
  coloured       0.09 /  19.06 /  2.56 ms           21.73 /  23.56 /  26.72       96 of 96          47 of 47 (65 painted)
```


## Phase 2 -- the ink stops paying for the paper

- **Recorded** 2026-08-20, at commit `a0a3704`, same machine and same build type
  as above.

### 2a, the barrier

The gate for 2a was an identity rather than a stopwatch: skipping bare paper had
to give exactly the same array, byte for byte, over ten arrangements chosen for
what a whole-band test alone would miss. It did, and the unskipped
implementation was then deleted.

The stopwatch needed a new row to see anything, because a whole solve is mostly
max-flow and the drawing `bench_composite` uses fills its frame. It now times
the barrier over the drawing and over four times as much paper, and the ratio
between those two is the change:

| | before 2a | after 2a |
|---|---|---|
| barrier over the drawing, 2.1 Mpx | 11.1 ms | 8.3 ms |
| barrier over four times the paper, 8.3 Mpx | 38.4 ms | 11.0 ms |
| motion estimate, two barriers a solve | 9.8 ms | 7.7 ms |

Compositing everywhere costs 3.5x for four times the paper. Compositing where
the ink is costs 1.3x, and what is left of that is the reduction walking a larger
array of cells rather than the flattening. That ratio is what phase 3 depends
on.

### 2b, the correlation

The gate for 2b is `bench_carry`, the whole table, and the rule is that nothing
worse than the phase 0 baseline reaches `main`.

**Coverage, leak and spread are unchanged on every row but one.** The estimated
shift moved on eight rows, and on every one of them it moved by exactly one cell
of the search grid -- the search reports a multiple of its own step, which is six
or seven image pixels at these sizes. Six moved towards the true shift and one
away from it:

| true shift | error before | error after | |
|---|---|---|---|
| 40 | 4 | 2 | three rows |
| -20 | 4 | 2 | |
| -80 | 4 | 2 | two rows |
| -200 | 4 | 3 | |
| 200 | 4 | 3 | two halves |
| 200 | 3 | **4** | one shape -- the row that got worse |
| 400 | **148** | **4** | two halves |

The last row is what 2b is for. A shape carried 400 px was matched at 252 and
lost both regions outright; it is matched at 396 now, the left region is fully
coloured and the right reads 14.3 per cent because two thirds of it is off the
canvas at that shift. Nothing lands in the wrong region -- right red is 0.0
before and after. That row is also the one phase 3 should improve again, and for
the same reason it is 14.3 rather than 100.

The row that got worse is one grid cell, at a true shift of 200: 203 and 196 are
adjacent cells of the same search and the truth lies between them. Coverage
stays at 100, leak at 0 and spread at 7.70 on that row, so nothing downstream
can tell the two answers apart.

Nothing is slower, and there is no version of 2b that could be: `Mean` is a sum
where `Most` was a max, and it also drops the two vector walks that flipped
intensity back into coverage.

### bench_composite

```
compositing a 1150x640 viewport

1 layer       77 tiles
    no margin           2.00 ms
    margin  64 px       2.95 ms   (1.48x)
    margin 192 px       4.60 ms   (2.30x)
2 layers     154 tiles
    no margin           2.88 ms
    margin  64 px       4.36 ms   (1.52x)
    margin 192 px       7.51 ms   (2.61x)
3 layers     234 tiles
    no margin           3.79 ms
    margin  64 px       5.41 ms   (1.43x)
    margin 192 px       8.09 ms   (2.13x)
4 layers     309 tiles
    no margin           5.55 ms
    margin  64 px       7.60 ms   (1.37x)
    margin 192 px      10.52 ms   (1.89x)

zoomed out, 4 layers over a wide drawing
    zoom  1.00 ( 1.00 image px an entry, read every 1)     2.13 ms
    zoom  0.70 ( 1.43 image px an entry, read every 1)     4.26 ms
    zoom  0.50 ( 2.00 image px an entry, read every 1)     5.51 ms
    zoom  0.20 ( 5.00 image px an entry, read every 2)    14.80 ms
    zoom  0.10 (10.00 image px an entry, read every 4)    13.71 ms
    zoom  0.05 (20.00 image px an entry, read every 7)    16.58 ms

LazyBrush: three boxed regions on a background, each wall gapped
     128x128         7.1 ms   (4 cuts)
     256x256        26.0 ms   (4 cuts)
     512x512       113.9 ms   (4 cuts)
    1024x1024      609.7 ms   (4 cuts)

A CTG solve on a 1920x1080 drawing, coarse then full:
    motion estimate, paid once per solve         7.0 ms  (0, 0)
    barrier over the drawing                 7.3 ms  (2.1 Mpx)
    barrier over four times the paper       11.7 ms  (8.3 Mpx)
    first    budget   262144  step 3       92.4 ms  (3 colours, 450 KB)
    refined  budget  4194304  step 1     1631.9 ms  (3 colours, 4050 KB)

A frame at 60 Hz is 16.7 ms. Scrubbing wants one of these per frame.
```

### bench_carry

```
Carrying a mark unchanged, against a shape that moves.

The mark is drawn on drawing 1 and inherited by the rest. Coverage is how
much of the shape it fills there; leak is how much of the world outside
the shape it fills as well. What is being asked is how much motion a mark
survives with nothing moving it -- which is what decides whether anything
past 'carry it unchanged' is worth building.

  shape 300x300, gap 60, mark radius 30, moving 0 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2       0      100.0%     0.0%     7.70       0
        3       0      100.0%     0.0%     7.70       0
        4       0      100.0%     0.0%     7.70       0
        5       0      100.0%     0.0%     7.70       0
        6       0      100.0%     0.0%     7.70       0

  shape 300x300, gap 60, mark radius 30, moving 20 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      20      100.0%     0.0%     7.70       0
        3      40      100.0%     0.0%     7.70       0
        4      60      100.0%     0.0%     7.75       0
        5      80      100.0%     0.2%     7.85       0
        6     100      100.0%     0.4%     7.95       0

  shape 300x300, gap 60, mark radius 30, moving 40 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      40      100.0%     0.0%     7.70       0
        3      80      100.0%     0.2%     7.85       0
        4     120      100.0%     0.7%     8.05       0
        5     160      100.0%     1.1%     8.26       0
        6     200      100.0%     1.5%     8.46       0

  shape 300x300, gap 60, mark radius 30, moving 80 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      80      100.0%     0.2%     7.85       0
        3     160      100.0%     1.1%     8.26       0
        4     240      100.0%     2.0%     8.67       0
        5     320        0.0%     2.2%     1.00       0
        6     400        0.0%     2.1%     1.00       0

  shape 300x300, gap 60, mark radius 12, moving 0 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2       0      100.0%     0.0%    21.88       0
        3       0      100.0%     0.0%    21.88       0
        4       0      100.0%     0.0%    21.88       0
        5       0      100.0%     0.0%    21.88       0
        6       0      100.0%     0.0%    21.88       0

  shape 300x300, gap 60, mark radius 12, moving 20 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      20      100.0%     0.0%    21.88       0
        3      40      100.0%     0.0%    21.88       0
        4      60      100.0%     0.0%    21.88       0
        5      80      100.0%     0.0%    21.98       0
        6     100      100.0%     0.1%    22.10       0

  shape 300x300, gap 60, mark radius 12, moving 40 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      40      100.0%     0.0%    21.88       0
        3      80      100.0%     0.0%    21.98       0
        4     120      100.0%     0.2%    22.22       0
        5     160      100.0%     0.4%    22.46       0
        6     200      100.0%     0.6%    22.70       0

  shape 300x300, gap 60, mark radius 12, moving 80 px a drawing, marks stay
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      80      100.0%     0.0%    21.98       0
        3     160      100.0%     0.4%    22.46       0
        4     240        0.0%     0.8%     1.03       0
        5     320        0.0%     0.8%     1.00       0
        6     400        0.0%     0.7%     1.00       0

  shape 300x300, gap 60, mark radius 30, moving 0 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2       0      100.0%     0.0%     7.70       0
        3       0      100.0%     0.0%     7.70       0
        4       0      100.0%     0.0%     7.70       0
        5       0      100.0%     0.0%     7.70       0
        6       0      100.0%     0.0%     7.70       0

  shape 300x300, gap 60, mark radius 30, moving 20 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      20      100.0%     0.0%     7.70      18
        3      40      100.0%     0.0%     7.70      42
        4      60      100.0%     0.0%     7.70      60
        5      80      100.0%     0.0%     7.70      78
        6     100      100.0%     0.0%     7.70      98

  shape 300x300, gap 60, mark radius 30, moving 40 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      40      100.0%     0.0%     7.70      42
        3      80      100.0%     0.0%     7.70      78
        4     120      100.0%     0.0%     7.70     119
        5     160      100.0%     0.0%     7.70     161
        6     200      100.0%     0.0%     7.70     196

  shape 300x300, gap 60, mark radius 30, moving 80 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%     7.70       0
        2      80      100.0%     0.0%     7.70      78
        3     160      100.0%     0.0%     7.70     161
        4     240      100.0%     0.0%     7.70     240
        5     320      100.0%     0.0%     7.70     320
        6     400      100.0%     0.0%     6.42     396

  shape 300x300, gap 60, mark radius 12, moving 0 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2       0      100.0%     0.0%    21.88       0
        3       0      100.0%     0.0%    21.88       0
        4       0      100.0%     0.0%    21.88       0
        5       0      100.0%     0.0%    21.88       0
        6       0      100.0%     0.0%    21.88       0

  shape 300x300, gap 60, mark radius 12, moving 20 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      20      100.0%     0.0%    21.88      18
        3      40      100.0%     0.0%    21.88      42
        4      60      100.0%     0.0%    21.88      60
        5      80      100.0%     0.0%    21.88      78
        6     100      100.0%     0.0%    21.88      98

  shape 300x300, gap 60, mark radius 12, moving 40 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      40      100.0%     0.0%    21.88      42
        3      80      100.0%     0.0%    21.88      78
        4     120      100.0%     0.0%    21.88     119
        5     160      100.0%     0.0%    21.88     161
        6     200      100.0%     0.0%    21.88     196

  shape 300x300, gap 60, mark radius 12, moving 80 px a drawing, marks follow
    drawing   shift   coverage    leak    spread   moved
        1       0      100.0%     0.0%    21.88       0
        2      80      100.0%     0.0%    21.88      78
        3     160      100.0%     0.0%    21.88     161
        4     240      100.0%     0.0%    21.88     240
        5     320      100.0%     0.0%    21.88     320
        6     400       92.5%     0.0%    16.93     396


And with a neighbour to be wrong about: two halves, a mark in each.
Left red and right blue are the fill being right; right red is the
colour of one region landing in the other, which is the failure the
single-shape table above cannot show.

  two 150x300 halves, a mark in each, radius 30, moving 20 px a drawing, marks stay
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      20        99.8%       100.0%        0.0%     6.32       0
        3      40        96.5%       100.0%        0.0%     6.28       0
        4      60        93.3%       100.0%        0.0%     6.28       0
        5      80         6.9%       100.0%        0.0%     1.00       0
        6     100         3.7%       100.0%        0.0%     1.00       0

  two 150x300 halves, a mark in each, radius 30, moving 40 px a drawing, marks stay
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      40        96.5%       100.0%        0.0%     6.28       0
        3      80         6.9%       100.0%        0.0%     1.00       0
        4     120         0.7%       100.0%        0.0%     1.00       0
        5     160         0.0%         0.0%        0.0%     1.22       0
        6     200         0.0%         0.0%        0.0%     1.00       0

  two 150x300 halves, a mark in each, radius 30, moving 80 px a drawing, marks stay
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      80         6.9%       100.0%        0.0%     1.00       0
        3     160         0.0%         0.0%        0.0%     1.22       0
        4     240         0.0%         0.0%        0.0%     1.00       0
        5     320         0.0%         0.0%        0.0%     1.00       0
        6     400         0.0%         0.0%        0.0%     1.00       0

  two 150x300 halves, a mark in each, radius 30, moving 20 px a drawing, marks follow
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      20       100.0%       100.0%        0.0%     6.31      18
        3      40       100.0%       100.0%        0.0%     6.31      42
        4      60       100.0%       100.0%        0.0%     6.31      60
        5      80       100.0%       100.0%        0.0%     6.31      78
        6     100       100.0%       100.0%        0.0%     6.31      98

  two 150x300 halves, a mark in each, radius 30, moving 40 px a drawing, marks follow
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      40       100.0%       100.0%        0.0%     6.31      42
        3      80       100.0%       100.0%        0.0%     6.31      78
        4     120       100.0%       100.0%        0.0%     6.31     119
        5     160       100.0%       100.0%        0.0%     6.31     161
        6     200       100.0%       100.0%        0.0%     6.31     203

  two 150x300 halves, a mark in each, radius 30, moving 80 px a drawing, marks follow
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%       100.0%        0.0%     6.31       0
        2      80       100.0%       100.0%        0.0%     6.31      78
        3     160       100.0%       100.0%        0.0%     6.31     161
        4     240       100.0%       100.0%        0.0%     6.31     240
        5     320       100.0%       100.0%        0.0%     6.31     320
        6     400       100.0%        14.3%        0.0%     1.14     396


And the same with nothing defending the neighbouring region: only the
left half is marked. Right red is then the colour landing in a region it
was never meant for -- the failure the design note calls 'the wrong region
of about the right size', which no flag catches.

  two 150x300 halves, a mark in the left half only, radius 30, moving -20 px a drawing, marks stay
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%         0.0%        0.0%     6.31       0
        2     -20       100.0%         0.0%      100.0%    12.78       0
        3     -40       100.0%         0.0%      100.0%    12.78       0
        4     -60       100.0%         0.0%      100.0%    12.78       0
        5     -80       100.0%         0.0%      100.0%    12.78       0
        6    -100       100.0%         0.0%      100.0%    12.78       0

  two 150x300 halves, a mark in the left half only, radius 30, moving -40 px a drawing, marks stay
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%         0.0%        0.0%     6.31       0
        2     -40       100.0%         0.0%      100.0%    12.78       0
        3     -80       100.0%         0.0%      100.0%    12.78       0
        4    -120       100.0%         0.0%      100.0%    12.78       0
        5    -160         0.0%         0.0%      100.0%     6.31       0
        6    -200         0.0%         0.0%      100.0%     6.60       0

  two 150x300 halves, a mark in the left half only, radius 30, moving -20 px a drawing, marks follow
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%         0.0%        0.0%     6.31       0
        2     -20       100.0%         0.0%        0.0%     6.31     -18
        3     -40       100.0%         0.0%        0.0%     6.31     -42
        4     -60       100.0%         0.0%        0.0%     6.31     -60
        5     -80       100.0%         0.0%        0.0%     6.31     -78
        6    -100       100.0%         0.0%        0.0%     6.31    -102

  two 150x300 halves, a mark in the left half only, radius 30, moving -40 px a drawing, marks follow
    drawing   shift    left red   right blue   right red   spread   moved
        1       0       100.0%         0.0%        0.0%     6.31       0
        2     -40       100.0%         0.0%        0.0%     6.31     -42
        3     -80       100.0%         0.0%        0.0%     6.31     -78
        4    -120       100.0%         0.0%        0.0%     6.31    -120
        5    -160       100.0%         0.0%        0.0%     6.31    -161
        6    -200       100.0%         0.0%        0.0%     6.31    -203

all of it in 14.5 s
```
