# The colour benchmarks before any of it

Phase 0 of [colour without a canvas](colour-without-a-canvas.md), which is a
measurement rather than a change: every gate in that plan reads against these,
so they are kept verbatim rather than summarised. A summary is what would let a
later run be compared against what somebody remembered.

Read the differences between rows and not the digits. The millisecond columns
move by a few per cent between runs on the same build, and `bench_carry`'s
coverage and leak are the only numbers here that are exact.

- **Recorded** 2026-08-20, at commit `a0c63d0`, before phase 1.
- **Where** Intel Core i7-10700 at 2.90 GHz, 8 cores, 16 GB, Windows 11.
- **How** MSYS2 UCRT64, `RelWithDebInfo`, one run each.

```powershell
./build/tests/bench_carry
./build/tests/bench_composite
./build/tests/bench_playback -platform offscreen
```

## bench_carry

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

## bench_composite

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

## bench_playback

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
