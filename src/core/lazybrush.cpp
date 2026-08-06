// SPDX-License-Identifier: GPL-3.0-or-later
#include "lazybrush.h"

#include "gridflow.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>

namespace animage {
namespace {

}  // namespace

std::vector<float> laplacianOfGaussian(const std::vector<float>& intensity, int width,
                                       int height, float strength) {
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<float> filtered(count, 1.0f);
    if (intensity.size() < count || width < 3 || height < 3) return filtered;

    // A 5x5 Laplacian of Gaussian. The paper's justification is that this is
    // roughly what the first stages of human vision do, and that its maxima sit
    // at the centre of a stroke -- which is exactly where the boundary should
    // end up.
    static constexpr float kKernel[25] = {
        0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
        0.0f, -1.0f, -2.0f, -1.0f, 0.0f,
       -1.0f, -2.0f, 16.0f, -2.0f, -1.0f,
        0.0f, -1.0f, -2.0f, -1.0f, 0.0f,
        0.0f,  0.0f, -1.0f,  0.0f, 0.0f};

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int ky = -2; ky <= 2; ++ky) {
                const int sy = std::clamp(y + ky, 0, height - 1);
                for (int kx = -2; kx <= 2; ++kx) {
                    const int sx = std::clamp(x + kx, 0, width - 1);
                    sum += kKernel[(ky + 2) * 5 + (kx + 2)] *
                           intensity[static_cast<std::size_t>(sy) * width + sx];
                }
            }
            filtered[static_cast<std::size_t>(y) * width + x] =
                std::clamp(1.0f - std::max(0.0f, strength * -sum), 0.0f, 1.0f);
        }
    }
    return filtered;
}

LazyBrushResult solveLazyBrush(const LazyBrushProblem& problem,
                               const LazyBrushOptions& options,
                               const std::atomic<bool>* abandon) {
    LazyBrushResult result;
    const int width = problem.width;
    const int height = problem.height;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    if (width <= 0 || height <= 0 || problem.colour_count <= 0) return result;
    if (problem.intensity.size() < count || problem.seeds.size() < count) return result;

    result.labels.assign(count, -1);

    // K is the perimeter, which is a safe upper bound on the length of any
    // stroke in the image. White has to cost more than the longest stroke, or a
    // boundary will cut straight across open paper rather than follow the line
    // the long way round.
    const float k = 2.0f * static_cast<float>(width + height);

    const std::vector<float>& source_intensity = problem.intensity;
    std::vector<float> filtered;
    if (options.preprocess_with_log) {
        filtered = laplacianOfGaussian(source_intensity, width, height, options.log_strength);
    }
    const std::vector<float>& used = options.preprocess_with_log ? filtered : source_intensity;

    // Map [0,1] onto [1, K]. Never zero: if cutting between two black pixels
    // were free, a region bordered by solid black would come apart.
    std::vector<float> weight(count, 1.0f);
    for (std::size_t i = 0; i < count; ++i) {
        weight[i] = 1.0f + std::clamp(used[i], 0.0f, 1.0f) * (k - 1.0f);
    }

    std::vector<char> active(static_cast<std::size_t>(problem.colour_count), 0);
    std::vector<long long> scribble_area(static_cast<std::size_t>(problem.colour_count), 0);
    for (std::size_t i = 0; i < count; ++i) {
        const int seed = problem.seeds[i];
        if (seed < 0 || seed >= problem.colour_count) continue;
        active[static_cast<std::size_t>(seed)] = 1;
        ++scribble_area[static_cast<std::size_t>(seed)];
    }

    // What binds a scribble to its region. It has to cost more to sever the
    // scribble than to cut around the region along the line -- and the line
    // costs about the region's perimeter, which is why K is the image
    // perimeter: one scribbled pixel already outweighs any region in the image.
    //
    // Being finite is the whole of what makes a scribble "soft". Two colours
    // scribbled into the same region each cost (their pixel count) x K to
    // sever, so the one with fewer pixels there is cheaper to give up: the
    // majority rule is not a rule, it is what the cut does. A hard scribble is
    // simply infinite, and can never be the cheapest thing to cut.
    const auto terminalCapacity = [&](int colour) {
        const bool is_hard = static_cast<std::size_t>(colour) < problem.hard.size() &&
                             problem.hard[static_cast<std::size_t>(colour)] != 0;
        if (is_hard) return std::numeric_limits<float>::infinity();
        return k * std::clamp(options.soft_lambda, 0.0f, 1.0f);
    };

    // The background: the outermost ring of the grid, seeded unseverably.
    //
    // A *soft* background at the rim was tried before this and removed, and the
    // reason it failed says where not to look. The strength of a soft seed is
    // its area, since severing one costs lambda*K a pixel and that is exactly
    // what makes the majority rule work -- so a rim seed's authority comes from
    // the size of the canvas rather than from anything the user meant. Putting
    // it into a comparison that means "how much did you insist" is a category
    // error, and the failure was not a bad lambda but lambda existing at all.
    //
    // A finite *price* on the border was tried after that, and failed for a
    // different reason worth recording: any price makes "give the colour
    // everything" an available labelling costing price * K, against n * K to
    // bridge a hole n cells wide, so bridging wins only while n is below the
    // price. A finite border price is a gap cap exactly equal to it. Which is
    // the one thing this has to avoid.
    //
    // Unseverable removes the rim from both comparisons. It never loses on
    // strength and it can never be bought, so some boundary separating scribble
    // from rim must exist and the cut simply picks the cheapest -- along the
    // outline where there is one, across the holes where there is not, at any
    // width. Gap tolerance is then n < lambda * |S|, which is at least what two
    // scribbles give.
    constexpr float kInk = 0.5f;
    const auto isBackground = [&](std::size_t node) {
        if (!options.implicit_background) return false;

        const int x = static_cast<int>(node % static_cast<std::size_t>(width));
        const int y = static_cast<int>(node / static_cast<std::size_t>(width));
        if (x != 0 && y != 0 && x != width - 1 && y != height - 1) return false;

        // A scribble carried to the edge is the seed there instead. Not a
        // special case: the ordinary rule that your seed wins on the pixels you
        // seeded, applied to a seed that happened to be placed automatically.
        // It is also how a region running off the frame gets filled.
        if (problem.seeds[node] >= 0) return false;

        // And not on ink, so an outline crossing the frame is not fighting a
        // seed sitting on top of it.
        return used[node] > kInk;
    };

    // Regions that only one colour has scribbled on need no cut at all: there
    // is nothing to cut against. Doing these first is most of why this is
    // faster than a general multiway cut, and it is also what finishes the last
    // colour once every other one has been resolved.
    //
    // A region reaching the background is never uncontested, because the
    // background is there to contest it. Without that the shortcut swallowed
    // everything: this flood fill walks through line art -- the line changes
    // edge weights, never connectivity -- so on a fresh drawing the whole grid
    // is one region, a single colour on it is uncontested, and every pixel took
    // that colour without a max-flow ever running.
    const auto labelUncontestedRegions = [&] {
        std::vector<char> visited(count, 0);
        std::vector<std::size_t> stack;
        std::vector<std::size_t> region;

        for (std::size_t start = 0; start < count; ++start) {
            if (visited[start] || result.labels[start] >= 0) continue;

            region.clear();
            stack.assign(1, start);
            visited[start] = 1;
            int only_colour = -1;
            bool contested = false;

            while (!stack.empty()) {
                const std::size_t node = stack.back();
                stack.pop_back();
                region.push_back(node);
                if (isBackground(node)) contested = true;

                const int seed = problem.seeds[node];
                if (seed >= 0 && active[static_cast<std::size_t>(seed)]) {
                    if (only_colour < 0) {
                        only_colour = seed;
                    } else if (only_colour != seed) {
                        contested = true;
                    }
                }

                const int x = static_cast<int>(node % static_cast<std::size_t>(width));
                const int y = static_cast<int>(node / static_cast<std::size_t>(width));
                const std::size_t neighbours[4] = {node + 1, node - 1,
                                                   node + static_cast<std::size_t>(width),
                                                   node - static_cast<std::size_t>(width)};
                const bool inside[4] = {x + 1 < width, x > 0, y + 1 < height, y > 0};
                for (int i = 0; i < 4; ++i) {
                    if (!inside[i]) continue;
                    const std::size_t next = neighbours[i];
                    if (visited[next] || result.labels[next] >= 0) continue;
                    visited[next] = 1;
                    stack.push_back(next);
                }
            }

            if (!contested && only_colour >= 0) {
                for (std::size_t node : region) result.labels[node] = only_colour;
            }
        }

        // A colour with no scribble left on an unlabelled pixel is finished.
        for (int colour = 0; colour < problem.colour_count; ++colour) {
            if (!active[static_cast<std::size_t>(colour)]) continue;
            bool remaining = false;
            for (std::size_t i = 0; i < count && !remaining; ++i) {
                remaining = result.labels[i] < 0 && problem.seeds[i] == colour;
            }
            if (!remaining) active[static_cast<std::size_t>(colour)] = 0;
        }
    };

    const auto activeCount = [&] {
        return std::count(active.begin(), active.end(), static_cast<char>(1));
    };

    const auto giveUp = [&] {
        return abandon != nullptr && abandon->load(std::memory_order_relaxed);
    };

    while (true) {
        if (giveUp()) {
            result.abandoned = true;
            return result;
        }
        labelUncontestedRegions();
        if (activeCount() == 0) break;

        // Largest first. The paper measures a large speed-up from doing the
        // background before the details, because every later sub-problem is
        // then smaller. Scribble area stands in for region area, which in
        // practice orders them the same way.
        int chosen = -1;
        long long best = -1;
        for (int colour = 0; colour < problem.colour_count; ++colour) {
            if (!active[static_cast<std::size_t>(colour)]) continue;
            if (scribble_area[static_cast<std::size_t>(colour)] > best) {
                best = scribble_area[static_cast<std::size_t>(colour)];
                chosen = colour;
            }
        }
        if (chosen < 0) break;

        GridFlow flow(width, height);
        bool any_source = false;
        bool any_sink = false;

        for (std::size_t node = 0; node < count; ++node) {
            if (result.labels[node] >= 0) continue;  // already settled

            // The background is a sink for every colour, whichever is being
            // solved for and however many there are. Unconditional on purpose:
            // making it depend on the number of colours was tried, and it only
            // moved the surprise to the moment a second colour appeared.
            float sink = isBackground(node) ? std::numeric_limits<float>::infinity() : 0.0f;

            const int seed = problem.seeds[node];
            if (seed >= 0 && active[static_cast<std::size_t>(seed)]) {
                if (seed == chosen) {
                    flow.setSource(node, terminalCapacity(seed));
                    any_source = true;
                } else {
                    sink += terminalCapacity(seed);
                }
            }
            if (sink > 0.0f) {
                flow.setSink(node, sink);
                any_sink = true;
            }

            const int x = static_cast<int>(node % static_cast<std::size_t>(width));
            const int y = static_cast<int>(node / static_cast<std::size_t>(width));

            // Right and down only; connect() fills in both directions.
            if (x + 1 < width && result.labels[node + 1] < 0) {
                flow.connect(node, 0, node + 1, std::min(weight[node], weight[node + 1]));
            }
            if (y + 1 < height) {
                const std::size_t below = node + static_cast<std::size_t>(width);
                if (result.labels[below] < 0) {
                    flow.connect(node, 2, below, std::min(weight[node], weight[below]));
                }
            }
        }

        if (!any_source || !any_sink) {
            active[static_cast<std::size_t>(chosen)] = 0;
            continue;
        }

        // Where most of the time goes, and so where giving up has to be
        // possible: one colour over a megapixel is a second of max-flow, and a
        // check between sub-problems would only notice after it.
        const std::vector<char> source_side = flow.solve(abandon);
        if (giveUp()) {
            result.abandoned = true;
            return result;
        }
        ++result.cuts;
        for (std::size_t node = 0; node < count; ++node) {
            if (result.labels[node] < 0 && source_side[node]) result.labels[node] = chosen;
        }
        active[static_cast<std::size_t>(chosen)] = 0;
    }

    return result;
}

}  // namespace animage
