// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace animage {

// LazyBrush -- Sýkora, Dingliana and Collins, Eurographics 2009. Reimplemented
// from the published paper.
//
// The problem it solves: a paint bucket fails on hand-drawn line art because
// the line has gaps, so colour leaks out. Instead of flooding from a point,
// this takes a rough scribble inside each region and finds the best boundary
// between them by minimum cut. Gaps cost something to cross, so the cut prefers
// to run along the line -- through its darkest pixels, which is what keeps the
// scan's antialiasing intact rather than shredding it.
//
// Two details in here carry most of the quality, and both are easy to lose:
//
//   - the smoothing weight is never zero. If cutting between two black pixels
//     were free, regions bordered by solid black would come apart and leave
//     holes along the line.
//   - white costs more than the longest stroke in the image. Otherwise a
//     boundary takes a shortcut straight across open paper instead of going
//     the long way round the drawing.

struct LazyBrushOptions {
    // The paper's value. A soft scribble does not have to be placed precisely:
    // the region takes the colour with the greater share of its pixels inside,
    // which is what lets you scrawl rather than aim.
    float soft_lambda = 0.95f;

    // The longest gap in the line art the fill will jump, in cells of this
    // grid. This is the price of the border, and it is the whole of what makes
    // one scribble fill one shape rather than the picture.
    //
    // It is a number with a meaning rather than a knob. Compare two labellings:
    // colouring everything cuts along the border, for `gap_tolerance` per border
    // cell -- about `gap_tolerance * K` in total, since K is the perimeter.
    // Bridging a hole `n` cells wide crosses blank paper, which costs K a cell,
    // so `n * K`. Bridging is the cheaper of the two exactly when `n <
    // gap_tolerance`. Hence the name and the unit.
    //
    // Note what it is not: it is not a seed and takes part in no majority. A
    // background that competed on scribble strength was tried and abandoned --
    // see the note in solveLazyBrush.
    float gap_tolerance = 32.0f;

    // Pencil and other low-contrast art need the line found before it can be
    // used as a barrier. The labelling is still applied to the untouched image.
    bool preprocess_with_log = false;
    float log_strength = 1.0f;
};

// A barrier image and the scribbles drawn over it.
struct LazyBrushProblem {
    int width = 0;
    int height = 0;

    // Row-major, one per pixel, 0 = solid line, 1 = blank paper.
    std::vector<float> intensity;

    // Row-major, one per pixel. -1 where nothing was scribbled, otherwise the
    // index of the colour scribbled there.
    std::vector<int> seeds;

    int colour_count = 0;

    // Per colour. A hard scribble is an absolute constraint; a soft one can be
    // overruled by the majority rule. Soft is the normal case.
    std::vector<char> hard;
};

struct LazyBrushResult {
    // Row-major, one per pixel. -1 means the pixel could not be reached from
    // any scribble -- the labelling is always connected to a scribble, which
    // falls out of the graph being sparse rather than being enforced.
    std::vector<int> labels;

    int cuts = 0;  // how many max-flow problems were actually solved
};

LazyBrushResult solveLazyBrush(const LazyBrushProblem& problem,
                               const LazyBrushOptions& options = {});

// Exposed for testing: the Laplacian-of-Gaussian preprocessing the paper
// prescribes for pencil. Its maxima fall along the centre of a stroke, so after
// filtering the inside of a region is white whatever it started as.
std::vector<float> laplacianOfGaussian(const std::vector<float>& intensity, int width,
                                       int height, float strength);

}  // namespace animage
