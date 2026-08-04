// SPDX-License-Identifier: GPL-3.0-or-later
//
// The point of LazyBrush is that it copes with line art a paint bucket cannot:
// lines with gaps in them. So the tests are mostly drawings with holes.

#include <cstdio>
#include <string>
#include <vector>

#include "lazybrush.h"
#include "testing.h"

using namespace animage;

namespace {

// A tiny drawing language, so the test cases read as pictures:
//   '#' line art   '.' paper   digits 0-9  a scribble of that colour
struct Picture {
    LazyBrushProblem problem;
    int width = 0;
    int height = 0;

    explicit Picture(const std::vector<std::string>& rows) {
        height = static_cast<int>(rows.size());
        width = rows.empty() ? 0 : static_cast<int>(rows[0].size());
        problem.width = width;
        problem.height = height;
        problem.intensity.assign(static_cast<std::size_t>(width) * height, 1.0f);
        problem.seeds.assign(static_cast<std::size_t>(width) * height, -1);

        int highest = -1;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * width + x;
                const char c = rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                if (c == '#') {
                    problem.intensity[i] = 0.0f;
                } else if (c >= '0' && c <= '9') {
                    problem.seeds[i] = c - '0';
                    highest = std::max(highest, c - '0');
                }
            }
        }
        problem.colour_count = highest + 1;
        problem.hard.assign(static_cast<std::size_t>(problem.colour_count), 0);
    }

    int labelAt(const LazyBrushResult& result, int x, int y) const {
        return result.labels[static_cast<std::size_t>(y) * width + x];
    }
};

void printLabels(const Picture& picture, const LazyBrushResult& result) {
    for (int y = 0; y < picture.height; ++y) {
        std::string row;
        for (int x = 0; x < picture.width; ++x) {
            const int label = picture.labelAt(result, x, y);
            row += (label < 0) ? '?' : static_cast<char>('0' + label);
        }
        std::printf("      %s\n", row.c_str());
    }
}

// A closed box. The easy case, and the one a paint bucket also gets right.
void closedRegionsSplitAtTheLine() {
    TEST("a closed shape separates inside from outside");
    Picture picture({
        "0000000000000000",
        "0..............0",
        "0..##########..0",
        "0..#........#..0",
        "0..#...11...#..0",
        "0..#...11...#..0",
        "0..#........#..0",
        "0..##########..0",
        "0..............0",
        "0000000000000000",
    });

    const LazyBrushResult result = solveLazyBrush(picture.problem);
    CHECK(result.cuts >= 1);

    CHECK_EQ(picture.labelAt(result, 6, 4), 1);   // inside
    CHECK_EQ(picture.labelAt(result, 6, 5), 1);
    CHECK_EQ(picture.labelAt(result, 5, 3), 1);   // inside, no scribble on it
    CHECK_EQ(picture.labelAt(result, 1, 5), 0);   // outside
    CHECK_EQ(picture.labelAt(result, 14, 1), 0);
}

// The case the whole algorithm exists for: the same box with a hole in the wall.
// A paint bucket leaks through it; the cut should still run along the line.
void colourDoesNotLeakThroughAGap() {
    TEST("colour does not leak through a gap in the line");
    Picture picture({
        "0000000000000000",
        "0..............0",
        "0..##########..0",
        "0..#........#..0",
        "0..#...11...#..0",
        "0..#...11...#..0",
        "0..#........#..0",
        "0..#####..###..0",
        "0..............0",
        "0000000000000000",
    });

    const LazyBrushResult result = solveLazyBrush(picture.problem);
    std::printf("    with a two-pixel gap in the bottom wall:\n");
    printLabels(picture, result);

    CHECK_EQ(picture.labelAt(result, 6, 4), 1);
    CHECK_EQ(picture.labelAt(result, 6, 5), 1);
    CHECK_EQ(picture.labelAt(result, 5, 6), 1);   // still inside, next to the gap
    CHECK_EQ(picture.labelAt(result, 8, 6), 1);   // directly above the gap
    CHECK_EQ(picture.labelAt(result, 8, 8), 0);   // directly below it: outside
    CHECK_EQ(picture.labelAt(result, 1, 5), 0);
    CHECK_EQ(picture.labelAt(result, 14, 5), 0);
}

// White has to cost more than the longest stroke, or the boundary takes a
// shortcut across open paper instead of following the line round.
void theBoundaryFollowsTheLineRatherThanCuttingAcross() {
    TEST("the boundary follows the line instead of crossing open paper");
    Picture picture({
        "000000000000000000",
        "0................0",
        "0..############..0",
        "0..#..........#..0",
        "0..#..1111....#..0",
        "0..#..........#..0",
        "0..#..........#..0",
        "0..#..........#..0",
        "0..############..0",
        "0................0",
        "000000000000000000",
    });

    const LazyBrushResult result = solveLazyBrush(picture.problem);

    // Every pixel enclosed by the box takes the inside colour, including the
    // rows far from the scribble. A cheap shortcut across the middle would
    // leave the lower half labelled 0.
    for (int y = 3; y <= 7; ++y) {
        for (int x = 4; x <= 13; ++x) {
            CHECK_EQ(picture.labelAt(result, x, y), 1);
        }
    }
}

// A soft scribble need not be placed accurately: the region goes to whichever
// colour has more of its scribble inside. This is what lets you scrawl.
void theMajorityRuleDecidesASloppyScribble() {
    TEST("a scribble that spills over is decided by majority");
    Picture picture({
        "22222222222222",
        "2............2",
        "2..########..2",
        "2..#......#..2",
        "2..#.1111.#..2",
        "2..#.1111.#..2",
        "2..#......#..2",
        "2..########..2",
        "2............2",
        "22222222222222",
    });

    // Bleed one pixel of colour 1 outside the box, and a lot of colour 2 in.
    picture.problem.seeds[static_cast<std::size_t>(1) * picture.width + 6] = 1;

    const LazyBrushResult result = solveLazyBrush(picture.problem);
    CHECK_EQ(picture.labelAt(result, 6, 4), 1);  // inside is still 1
    CHECK_EQ(picture.labelAt(result, 1, 1), 2);  // outside is still 2
    CHECK_EQ(picture.labelAt(result, 12, 8), 2);
}

// A hard scribble is an absolute constraint and cannot be overruled.
void hardScribblesAreNotNegotiable() {
    TEST("a hard scribble keeps its pixels");
    Picture picture({
        "000000000000",
        "0..........0",
        "0..######..0",
        "0..#....#..0",
        "0..#.11.#..0",
        "0..#....#..0",
        "0..######..0",
        "0..........0",
        "000000000000",
    });
    picture.problem.hard[1] = 1;

    const LazyBrushResult result = solveLazyBrush(picture.problem);
    CHECK_EQ(picture.labelAt(result, 5, 4), 1);
    CHECK_EQ(picture.labelAt(result, 6, 4), 1);
    CHECK_EQ(picture.labelAt(result, 1, 1), 0);
}

// Three colours means more than one cut, and the greedy order has to leave
// every region labelled.
void threeColoursAllGetTheirRegion() {
    TEST("three colours each keep their own region");
    Picture picture({
        "000000000000000000",
        "0................0",
        "0.####...####....0",
        "0.#11#...#22#....0",
        "0.#11#...#22#....0",
        "0.####...####....0",
        "0................0",
        "0..####..........0",
        "0..#33#..........0",
        "0..####..........0",
        "000000000000000000",
    });

    const LazyBrushResult result = solveLazyBrush(picture.problem);

    // Fewer cuts than colours. Once the background is settled the three boxes
    // are separate regions with one colour each, so they need no cut at all --
    // which is the pruning the paper measures its speed-up from, and the reason
    // to do the largest colour first.
    CHECK(result.cuts >= 1);
    CHECK(result.cuts < 4);

    CHECK_EQ(picture.labelAt(result, 3, 3), 1);
    CHECK_EQ(picture.labelAt(result, 10, 3), 2);
    CHECK_EQ(picture.labelAt(result, 4, 8), 3);
    CHECK_EQ(picture.labelAt(result, 16, 6), 0);
    CHECK_EQ(picture.labelAt(result, 8, 1), 0);
}

// An enclosure nobody scribbled in takes the surrounding colour. There is no
// competing terminal inside it, so nothing makes the cut stop at its wall --
// which is right: an unpainted hole would be worse than a filled one.
void anUnscribbledEnclosureTakesTheSurroundingColour() {
    TEST("an enclosure nobody scribbled in takes the surrounding colour");
    Picture picture({
        "000000000000000000",
        "0................0",
        "0..#####....####.0",
        "0..#...#....#..#.0",
        "0..#.1.#....#..#.0",
        "0..#...#....#..#.0",
        "0..#####....####.0",
        "0................0",
        "000000000000000000",
    });

    const LazyBrushResult result = solveLazyBrush(picture.problem);
    CHECK_EQ(picture.labelAt(result, 5, 4), 1);   // the scribbled box
    CHECK_EQ(picture.labelAt(result, 13, 4), 0);  // the empty one: background
    CHECK_EQ(picture.labelAt(result, 9, 4), 0);   // between them
}

// With no scribble at all there is nothing to say, and the solver says nothing
// rather than inventing a label.
void nothingIsSaidWithoutScribbles() {
    TEST("nothing is labelled when nothing is scribbled");
    Picture picture({"....", "....", "...."});
    picture.problem.colour_count = 2;
    picture.problem.hard.assign(2, 0);

    const LazyBrushResult result = solveLazyBrush(picture.problem);
    CHECK_EQ(result.cuts, 0);
    for (int label : result.labels) CHECK_EQ(label, -1);
}

void emptyAndDegenerateInputsAreSafe() {
    TEST("empty and degenerate inputs do not misbehave");
    LazyBrushProblem nothing;
    CHECK(solveLazyBrush(nothing).labels.empty());

    Picture no_scribbles({"....", "....", "...."});
    no_scribbles.problem.colour_count = 1;
    no_scribbles.problem.hard.assign(1, 0);
    const LazyBrushResult result = solveLazyBrush(no_scribbles.problem);
    CHECK_EQ(result.labels.size(), std::size_t{12});
    CHECK_EQ(result.cuts, 0);
    for (int label : result.labels) CHECK_EQ(label, -1);

    // This case used to pin the bug: one colour and no line art labelled every
    // pixel, because the flood fill that looks for uncontested regions walks
    // straight through line art and so saw the whole grid as one region with a
    // single colour on it. The border is priced now, so a lone scribble no
    // longer takes the picture by default.
    //
    // Here it takes nothing at all: the scribble is the top row, which is the
    // border, so it is cheaper to give the scribble up than to pay for the edge
    // it sits on. On a grid this small (K = 14) there is no room for anything
    // else to happen. What a scribble on open paper does when it is *not*
    // jammed against the edge is pinned separately below.
    Picture one_colour({"1111", "....", "...."});
    const LazyBrushResult filled = solveLazyBrush(one_colour.problem);
    CHECK_EQ(filled.cuts, 1);  // the border is something to cut against
    for (int label : filled.labels) CHECK_EQ(label, -1);
}

// The point of the whole change: one scribble, one shape.
void oneScribbleFillsOneShape() {
    TEST("one scribble in a closed shape fills the shape, not the picture");
    Picture picture({
        "....................",
        "....................",
        "...##############...",
        "...#............#...",
        "...#....1111....#...",
        "...#....1111....#...",
        "...#............#...",
        "...##############...",
        "....................",
        "....................",
    });

    const LazyBrushResult result = solveLazyBrush(picture.problem);
    printLabels(picture, result);

    // Everything the box encloses, including the corners far from the scribble.
    for (int y = 3; y <= 6; ++y) {
        for (int x = 4; x <= 15; ++x) CHECK_EQ(picture.labelAt(result, x, y), 1);
    }

    // And nothing outside it. No second scribble was needed to say so.
    CHECK_EQ(picture.labelAt(result, 0, 0), -1);
    CHECK_EQ(picture.labelAt(result, 19, 9), -1);
    CHECK_EQ(picture.labelAt(result, 1, 5), -1);
    CHECK_EQ(picture.labelAt(result, 18, 5), -1);
}

// Adding a second colour must not disturb what the first one filled. The
// background is unconditional precisely so that this holds -- a background that
// only existed while there was one colour would move the boundary the moment a
// second appeared.
void asecondColourDoesNotDisturbTheFirst() {
    TEST("adding a second colour leaves the first one's fill alone");
    const std::vector<std::string> rows = {
        "........................",
        "..##########............",
        "..#........#............",
        "..#..1111..#....####....",
        "..#..1111..#....#..#....",
        "..#........#....#..#....",
        "..##########....####....",
        "........................",
    };

    Picture alone(rows);
    const LazyBrushResult before = solveLazyBrush(alone.problem);

    std::vector<std::string> with_second = rows;
    with_second[4].replace(17, 2, "22");  // a scribble in the small box
    Picture pair(with_second);
    const LazyBrushResult after = solveLazyBrush(pair.problem);

    // The first box is filled the same way either way.
    for (int y = 2; y <= 5; ++y) {
        for (int x = 3; x <= 10; ++x) {
            CHECK_EQ(alone.labelAt(before, x, y), 1);
            CHECK_EQ(pair.labelAt(after, x, y), 1);
        }
    }
    // And the second box only fills once something is scribbled in it.
    CHECK_EQ(alone.labelAt(before, 17, 4), -1);
    CHECK_EQ(pair.labelAt(after, 17, 4), 2);
}

// The price of an unseverable rim, stated rather than hidden.
//
// A shape running off the edge of the grid is not filled to the edge by a
// scribble that stops short of it: the rim is background and cannot be bought.
// Worse than that, where it stops is arbitrary -- cutting across the region at
// the scribble's edge and cutting against the rim cost exactly the same, so this
// is the paper's own ambiguity case and the tie goes wherever the label order
// sends it.
//
// The remedy is the ordinary rule, that your seed wins where you seeded:
// carrying the scribble off the edge fills the region. One gesture, against a
// second scribble on every shape, which is what this design replaced.
void aCroppedShapeNeedsTheScribbleCarriedToTheEdge() {
    TEST("a cropped shape fills to the edge when the scribble is carried there");

    Picture short_of_it({
        "....................",
        "..##################",
        "..#.................",
        "..#.....1111........",
        "..#.....1111........",
        "..#.................",
        "..##################",
        "....................",
    });
    const LazyBrushResult stopped = solveLazyBrush(short_of_it.problem);
    printLabels(short_of_it, stopped);
    CHECK_EQ(short_of_it.labelAt(stopped, 9, 3), 1);    // around the scribble
    CHECK_EQ(short_of_it.labelAt(stopped, 19, 3), -1);  // but not out to the crop
    CHECK_EQ(short_of_it.labelAt(stopped, 15, 3), -1);
    CHECK_EQ(short_of_it.labelAt(stopped, 0, 3), -1);   // and never outside the box

    Picture carried({
        "....................",
        "..##################",
        "..#.................",
        "..#.....11111111111.",
        "..#.....11111111111.",
        "..#.................",
        "..##################",
        "....................",
    });
    // The last column is the rim; the scribble reaching it is what displaces the
    // background there.
    for (int y = 3; y <= 4; ++y) {
        carried.problem.seeds[static_cast<std::size_t>(y) * carried.width + 19] = 1;
    }
    const LazyBrushResult reaching = solveLazyBrush(carried.problem);
    printLabels(carried, reaching);
    CHECK_EQ(carried.labelAt(reaching, 5, 3), 1);
    CHECK_EQ(carried.labelAt(reaching, 19, 3), 1);  // hard against the cropped edge

    // Rows the scribble did not itself reach fill to one cell short of the rim,
    // because that cell is the background seed. The application never shows it:
    // the ring is scaffolding for the solve, and the fill is read from one cell
    // inside it -- see the label extension in ctg.cpp.
    CHECK_EQ(carried.labelAt(reaching, 18, 5), 1);
    CHECK_EQ(carried.labelAt(reaching, 19, 5), -1);
    CHECK_EQ(carried.labelAt(reaching, 0, 3), -1);
}

// The test the whole change exists for: a hole big enough to be embarrassing.
void aBadlyClosedShapeFillsFromOneScribble() {
    TEST("one scribble fills a shape whose outline is a quarter open");
    std::vector<std::string> rows(24, std::string(40, '.'));
    for (int x = 6; x <= 33; ++x) {
        rows[4][static_cast<std::size_t>(x)] = '#';
        rows[19][static_cast<std::size_t>(x)] = '#';
    }
    for (int y = 4; y <= 19; ++y) {
        rows[static_cast<std::size_t>(y)][6] = '#';
        rows[static_cast<std::size_t>(y)][33] = '#';
    }
    // Seven cells of a twenty-eight cell wall are simply missing.
    for (int x = 16; x < 23; ++x) rows[19][static_cast<std::size_t>(x)] = '.';
    // A scribble of the size a hand makes, nowhere near the hole.
    for (int y = 10; y <= 13; ++y) {
        for (int x = 14; x <= 25; ++x) rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '1';
    }

    Picture picture(rows);
    const LazyBrushResult result = solveLazyBrush(picture.problem);
    printLabels(picture, result);

    // The whole interior, corners included, and the strip right beside the hole.
    for (int y = 5; y <= 18; ++y) {
        for (int x = 7; x <= 32; ++x) CHECK_EQ(picture.labelAt(result, x, y), 1);
    }
    // And nothing out through the hole.
    CHECK_EQ(picture.labelAt(result, 19, 20), -1);
    CHECK_EQ(picture.labelAt(result, 19, 23), -1);
    CHECK_EQ(picture.labelAt(result, 0, 0), -1);
}

// With no line art at all, a scribble has nothing to be bounded by. What it does
// then is worth pinning rather than assuming, and it is not one behaviour but
// two, decided by the scribble's own shape.
//
// Wrapping a scribble costs K along its rim; giving it up costs lambda*K a
// pixel. So wrapping wins exactly when d(S)/|S| < lambda -- which is the paper's
// own constraint on lambda, arrived at from the other end. A thin scribble is
// mostly rim and is cheaper to abandon; a fat one is not.
void aScribbleOnOpenPaperKeepsToItself() {
    TEST("a scribble on open paper never takes the picture");

    // Thin: twelve cells of rim around eight cells of area, so it goes.
    Picture thin({
        "....................",
        "....................",
        "....................",
        "........1111........",
        "........1111........",
        "....................",
        "....................",
        "....................",
    });
    const LazyBrushResult thin_result = solveLazyBrush(thin.problem);
    printLabels(thin, thin_result);
    for (int label : thin_result.labels) CHECK_EQ(label, -1);

    // Fat: it stays, and wraps itself. With no line anywhere there is no
    // cheaper boundary to find, so the cut hugs the scribble -- wrapping costs K
    // along its rim against lambda*K a pixel to give it up, and a fat scribble
    // is more area than rim. It cannot reach the edge of the picture: the rim is
    // unseverable, so no amount of area buys it.
    Picture fat({
        "..............................",
        "..............................",
        "..............................",
        "..............................",
        "..........1111111111..........",
        "..........1111111111..........",
        "..........1111111111..........",
        "..........1111111111..........",
        "..........1111111111..........",
        "..........1111111111..........",
        "..............................",
        "..............................",
        "..............................",
        "..............................",
    });
    const LazyBrushResult fat_result = solveLazyBrush(fat.problem);
    printLabels(fat, fat_result);
    CHECK_EQ(fat.labelAt(fat_result, 12, 6), 1);   // its own pixels, kept
    CHECK_EQ(fat.labelAt(fat_result, 15, 7), 1);
    CHECK_EQ(fat.labelAt(fat_result, 0, 0), -1);   // and nothing beyond them
    CHECK_EQ(fat.labelAt(fat_result, 29, 13), -1);
    CHECK_EQ(fat.labelAt(fat_result, 5, 6), -1);

    // Put a line round it and it stops at the line instead, at a fraction of
    // the price. This is the pair that shows the border is priced sensibly:
    // the same scribble, and the only difference is that there is now something
    // to find.
    Picture boxed({
        "..............................",
        "..............................",
        "........####################..",
        "........#..................#..",
        "........#.1111111111.......#..",
        "........#.1111111111.......#..",
        "........#.1111111111.......#..",
        "........#.1111111111.......#..",
        "........#.1111111111.......#..",
        "........#.1111111111.......#..",
        "........#..................#..",
        "........####################..",
        "..............................",
        "..............................",
    });
    const LazyBrushResult boxed_result = solveLazyBrush(boxed.problem);
    CHECK_EQ(boxed.labelAt(boxed_result, 12, 6), 1);
    CHECK_EQ(boxed.labelAt(boxed_result, 25, 3), 1);   // inside, far from the marks
    CHECK_EQ(boxed.labelAt(boxed_result, 0, 0), -1);   // outside stays outside
    CHECK_EQ(boxed.labelAt(boxed_result, 29, 13), -1);
}

// Gap tolerance is no longer a number in the code, so it is measured against the
// thing it has to beat: two scribbles. The claim the whole design rests on is
// that one scribble against an unseverable rim bridges at least what two user
// scribbles bridge on the same drawing.
//
// A hard rim needs no special support to set up here -- `hard` already makes a
// seed infinite -- so the two arrangements are compared through the same solver.
void oneScribbleBridgesAtLeastWhatTwoDo() {
    TEST("one scribble bridges at least the gap two scribbles do");

    constexpr int kW = 60;
    constexpr int kH = 40;
    constexpr int kLeft = 15, kRight = 45, kTop = 10, kBottom = 30;

    // `half` is half the width of the inside scribble, so its area can be
    // varied: tolerance is supposed to go as the area of the scribble.
    const auto build = [&](int gap, int half, bool rim, bool outside) {
        std::vector<std::string> rows(kH, std::string(kW, '.'));
        for (int x = kLeft; x <= kRight; ++x) {
            rows[kTop][static_cast<std::size_t>(x)] = '#';
            rows[kBottom][static_cast<std::size_t>(x)] = '#';
        }
        for (int y = kTop; y <= kBottom; ++y) {
            rows[static_cast<std::size_t>(y)][kLeft] = '#';
            rows[static_cast<std::size_t>(y)][kRight] = '#';
        }
        const int hole = (kLeft + kRight) / 2 - gap / 2;
        for (int i = 0; i < gap; ++i) rows[kBottom][static_cast<std::size_t>(hole + i)] = '.';

        const int cx = (kLeft + kRight) / 2;
        const int cy = (kTop + kBottom) / 2;
        for (int y = cy - 2; y <= cy + 1; ++y) {
            for (int x = cx - half; x < cx + half; ++x) {
                rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '1';
            }
        }
        if (rim) {  // the outermost ring, seeded as colour 0
            for (int x = 0; x < kW; ++x) {
                rows[0][static_cast<std::size_t>(x)] = '0';
                rows[kH - 1][static_cast<std::size_t>(x)] = '0';
            }
            for (int y = 0; y < kH; ++y) {
                rows[static_cast<std::size_t>(y)][0] = '0';
                rows[static_cast<std::size_t>(y)][kW - 1] = '0';
            }
        }
        if (outside) {  // a hand-drawn background mark of the same area
            for (int y = 3; y <= 6; ++y) {
                for (int x = 3; x < 3 + 2 * half; ++x) {
                    rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '0';
                }
            }
        }
        return rows;
    };

    const auto fills = [&](int gap, int half, bool rim, bool outside) {
        Picture picture(build(gap, half, rim, outside));
        if (rim) picture.problem.hard[0] = 1;
        LazyBrushOptions options;
        // The two-scribble arrangement is what the solver did before there was
        // a background at all, so it is measured without one. Otherwise both
        // columns get the rim and the comparison says nothing.
        options.implicit_background = rim;
        const LazyBrushResult result = solveLazyBrush(picture.problem, options);
        const int hole = (kLeft + kRight) / 2 - gap / 2;
        const bool filled = picture.labelAt(result, kLeft + 1, kTop + 1) == 1 &&
                            picture.labelAt(result, kRight - 1, kBottom - 1) == 1;
        const bool leaked = picture.labelAt(result, hole, kBottom + 3) == 1 ||
                            picture.labelAt(result, 1, kH - 2) == 1;
        return filled && !leaked;
    };

    const auto widest = [&](int half, bool rim, bool outside) {
        int found = 0;
        for (int gap = 1; gap <= kRight - kLeft - 1; ++gap) {
            if (fills(gap, half, rim, outside)) found = gap;
        }
        return found;
    };

    std::printf("      inside scribble | two scribbles | one, hard rim\n");
    int previous = 0;
    for (int half : {2, 4, 6, 8}) {
        const int two = widest(half, false, true);   // scribble inside and out
        const int one = widest(half, true, false);   // scribble inside, rim hard
        std::printf("      %2d x 4 = %2d cells |      %2d       |      %2d\n", 2 * half, 8 * half,
                    two, one);
        CHECK(one >= two);      // the claim the design rests on
        CHECK(one >= previous); // and a bigger scribble never bridges less
        previous = one;
    }
}

// Pencil is too low-contrast to act as a barrier directly. The filter finds the
// stroke; the labelling is still applied to the original image.
void logPreprocessingSharpensAFaintLine() {
    TEST("the LoG filter brings out a faint line");
    Picture picture({
        "000000000000",
        "0..........0",
        "0.677777776.0",
        "0.6......6.0",
        "0.6..11..6.0",
        "0.6......6.0",
        "0.677777776.0",
        "0..........0",
        "000000000000",
    });
    // '6' and '7' are not scribbles -- rewrite them as a faint grey line.
    for (std::size_t i = 0; i < picture.problem.seeds.size(); ++i) {
        const int seed = picture.problem.seeds[i];
        if (seed == 6 || seed == 7) {
            picture.problem.seeds[i] = -1;
            picture.problem.intensity[i] = 0.72f;  // barely darker than paper
        }
    }
    picture.problem.colour_count = 2;
    picture.problem.hard.assign(2, 0);

    LazyBrushOptions options;
    options.preprocess_with_log = true;
    options.log_strength = 3.0f;

    const LazyBrushResult result = solveLazyBrush(picture.problem, options);
    CHECK_EQ(picture.labelAt(result, 5, 4), 1);
    CHECK_EQ(picture.labelAt(result, 1, 1), 0);

    // And the filter itself does what it says: darker where the line is.
    const std::vector<float> filtered =
        laplacianOfGaussian(picture.problem.intensity, picture.width, picture.height, 3.0f);
    CHECK_EQ(filtered.size(), picture.problem.intensity.size());
}

}  // namespace

int main() {
    std::printf("lazybrush:\n");
    closedRegionsSplitAtTheLine();
    colourDoesNotLeakThroughAGap();
    theBoundaryFollowsTheLineRatherThanCuttingAcross();
    theMajorityRuleDecidesASloppyScribble();
    hardScribblesAreNotNegotiable();
    threeColoursAllGetTheirRegion();
    anUnscribbledEnclosureTakesTheSurroundingColour();
    nothingIsSaidWithoutScribbles();
    emptyAndDegenerateInputsAreSafe();
    oneScribbleFillsOneShape();
    asecondColourDoesNotDisturbTheFirst();
    aCroppedShapeNeedsTheScribbleCarriedToTheEdge();
    aBadlyClosedShapeFillsFromOneScribble();
    aScribbleOnOpenPaperKeepsToItself();
    oneScribbleBridgesAtLeastWhatTwoDo();
    logPreprocessingSharpensAFaintLine();
    return testing::summarise("lazybrush");
}
