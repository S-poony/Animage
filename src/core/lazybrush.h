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

    // Seed the outermost ring of the grid as background, unseverably.
    //
    // This is what lets one scribble fill one shape. Without an opponent the
    // solver has no reason to stop anywhere, so a lone scribble labels
    // everything it can reach; with one, some separating boundary must exist,
    // and the cut picks the cheapest -- which follows the outline where there is
    // one and jumps the holes, however wide, because jumping is still cheaper
    // than any other way of telling the two apart.
    //
    // It has to be unseverable. Anything a boundary can *buy* the border with,
    // however dear, makes "give the colour everything" an available labelling,
    // and then holes wider than that price are not bridged. A finite border
    // price is a gap cap exactly equal to it, whatever the price -- so there is
    // no value to go looking for.
    //
    // The gap tolerance it does give is `n < lambda * |S|`: bridging a hole n
    // cells wide costs n*K, giving the scribble up costs lambda*K per pixel of
    // it. That is at least what two scribbles give -- they sever whichever of
    // the two is smaller -- and it is a rule a person can act on, which a hidden
    // threshold is not: scribble bigger to bridge bigger holes.
    //
    // Where the user has scribbled on the ring, their scribble is the seed and
    // this is not applied; nor is it applied on ink, so an outline crossing the
    // frame is not fighting a seed laid on top of it.
    bool implicit_background = true;

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
