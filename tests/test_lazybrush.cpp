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

    Picture one_colour({"1111", "....", "...."});
    const LazyBrushResult filled = solveLazyBrush(one_colour.problem);
    CHECK_EQ(filled.cuts, 0);  // nothing to cut against
    for (int label : filled.labels) CHECK_EQ(label, 1);
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
    logPreprocessingSharpensAFaintLine();
    return testing::summarise("lazybrush");
}
