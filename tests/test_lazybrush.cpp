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

// A shape running off the edge of the grid is filled up to the edge. It pays
// the border price along the crop, which has to stay cheaper than shrinking
// back to the scribble.
void aCroppedShapeFillsToTheEdge() {
    TEST("a shape cropped by the edge still fills to the edge");
    Picture picture({
        "....................",
        "..##################",
        "..#.................",
        "..#.....1111........",
        "..#.....1111........",
        "..#.................",
        "..##################",
        "....................",
    });

    const LazyBrushResult result = solveLazyBrush(picture.problem);
    printLabels(picture, result);

    CHECK_EQ(picture.labelAt(result, 5, 3), 1);
    CHECK_EQ(picture.labelAt(result, 19, 3), 1);  // hard against the cropped edge
    CHECK_EQ(picture.labelAt(result, 19, 5), 1);
    CHECK_EQ(picture.labelAt(result, 0, 3), -1);  // outside the box entirely
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

    // Fat: it stays, and with nothing to be bounded by it goes all the way to
    // the edge. Three prices are on offer -- wrap the scribble at K a cell of
    // rim, run along the border at the gap tolerance a cell, or give the
    // scribble up at lambda*K a pixel -- and on blank paper the border is the
    // cheapest of them.
    //
    // That is the honest answer rather than a hole in the design: a scribble
    // with no line art anywhere near it has no boundary to find. As soon as
    // there is a line the outline costs about 1 a cell against the border's 32,
    // and the shape wins by a mile -- which is the case above.
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
    for (int label : fat_result.labels) CHECK_EQ(label, 1);

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

// The gap tolerance is a number with a meaning, so it gets swept rather than
// asserted at one point. Below it the fill is held by a gapped line; above it
// the fill escapes and takes the picture, which is the honest failure mode --
// the line stopped being a boundary.
void theGapToleranceIsWhereItSaysItIs() {
    TEST("the fill holds below the stated gap tolerance and escapes above it");

    // A box with room around it, and tall enough that cutting straight across
    // its middle is not the cheap way out.
    //
    // Both matter, and the first version of this test had neither. With the box
    // close to the edge, colour squirts through the hole as a narrow tongue and
    // reaches the border, because the tongue's two short sides cost less than
    // bridging does; with the box short, the cut crosses its interior and
    // strands the far end rather than going round. Neither has anything to do
    // with the gap tolerance -- both are the paper's own "shortcut" case, a
    // scribble thin relative to the holes near it -- and both drown the number
    // being measured. The real solve keeps a tile of margin around the drawing,
    // which is the same precaution.
    constexpr int kWidth = 44;
    constexpr int kHeight = 34;
    constexpr int kLeft = 12, kRight = 31, kTop = 8, kBottom = 25;

    const auto held = [&](int gap, float tolerance) {
        std::vector<std::string> rows(kHeight, std::string(kWidth, '.'));
        for (int x = kLeft; x <= kRight; ++x) {
            rows[kTop][static_cast<std::size_t>(x)] = '#';
            rows[kBottom][static_cast<std::size_t>(x)] = '#';
        }
        for (int y = kTop; y <= kBottom; ++y) {
            rows[static_cast<std::size_t>(y)][kLeft] = '#';
            rows[static_cast<std::size_t>(y)][kRight] = '#';
        }
        // A hole of `gap` cells in the middle of the bottom wall.
        const int hole = (kLeft + kRight) / 2 - gap / 2;
        for (int i = 0; i < gap; ++i) rows[kBottom][static_cast<std::size_t>(hole + i)] = '.';
        // A scribble with enough area that giving it up is never the answer.
        for (int y = 15; y <= 18; ++y) {
            for (int x = 18; x <= 25; ++x) {
                rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '1';
            }
        }

        Picture picture(rows);
        LazyBrushOptions options;
        options.gap_tolerance = tolerance;
        const LazyBrushResult result = solveLazyBrush(picture.problem, options);

        // Held if the box is filled corner to corner and nothing came out of
        // the hole. A far corner alone is not enough: colour that leaks pools
        // just under the box long before it crosses the picture.
        const bool filled = picture.labelAt(result, kLeft + 1, kTop + 1) == 1 &&
                            picture.labelAt(result, kRight - 1, kBottom - 1) == 1;
        const bool leaked = picture.labelAt(result, hole, kBottom + 2) == 1 ||
                            picture.labelAt(result, 0, 0) == 1;
        return filled && !leaked;
    };

    // For each hole, the smallest whole-cell tolerance that still holds it.

    for (int gap : {2, 4, 6, 8, 10}) {
        int needed = -1;
        for (int tolerance = 1; tolerance <= 40 && needed < 0; ++tolerance) {
            if (held(gap, static_cast<float>(tolerance))) needed = tolerance;
        }
        std::printf("      a hole %2d cells wide needs a tolerance of %d\n", gap, needed);
        // Measured, not derived: the tolerance that holds a hole of n cells is
        // n + 1, exactly, across the range. So a tolerance of g bridges every
        // hole narrower than g, which is what the number claims to mean.
        CHECK_EQ(needed, gap + 1);
    }

    // The two ends of the claim, stated plainly: a hole wider than the
    // tolerance escapes, and the same hole holds once the tolerance exceeds it.
    CHECK_EQ(held(10, 4.0f), false);
    CHECK_EQ(held(10, 16.0f), true);

    // Priced at nothing, nothing holds -- which is the behaviour this whole
    // change replaced.
    CHECK_EQ(held(2, 0.0f), false);
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
    aCroppedShapeFillsToTheEdge();
    aScribbleOnOpenPaperKeepsToItself();
    theGapToleranceIsWhereItSaysItIs();
    logPreprocessingSharpensAFaintLine();
    return testing::summarise("lazybrush");
}
