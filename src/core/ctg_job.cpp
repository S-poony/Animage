// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctg_job.h"

#include <algorithm>
#include <unordered_map>

#include "compositor.h"

namespace animage {
namespace {



bool abandoned(const std::atomic<bool>* abandon) {
    return abandon != nullptr && abandon->load(std::memory_order_relaxed);
}

// Whether every label on the ring the lookup clamps to is -1.
//
// The ring is the border of the grid one cell in, because that is what the
// clamp produces: a coordinate outside the solved rectangle lands on it in at
// least one axis and ranges over it in the other. So this is exactly the set of
// labels anything outside the solve can read, and when none of them is a colour
// the whole world out there is transparent.
//
// A fact about the labels that were computed, not an estimate of them. It is
// what lets ctgFillSpan answer an empty row in one test, which is the shortcut
// an absent tile used to give the compositor for nothing.
bool ringIsClear(const std::vector<int>& labels, int width, int height) {
    if (width <= 0 || height <= 0) return true;
    if (labels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return false;
    }

    const int low_x = (width >= 3) ? 1 : 0;
    const int high_x = (width >= 3) ? width - 2 : width - 1;
    const int low_y = (height >= 3) ? 1 : 0;
    const int high_y = (height >= 3) ? height - 2 : height - 1;

    const auto at = [&](int x, int y) {
        return labels[static_cast<std::size_t>(y) * width + x];
    };
    for (int x = low_x; x <= high_x; ++x) {
        if (at(x, low_y) >= 0 || at(x, high_y) >= 0) return false;
    }
    for (int y = low_y; y <= high_y; ++y) {
        if (at(low_x, y) >= 0 || at(high_x, y) >= 0) return false;
    }
    return true;
}

}  // namespace

// drawnBounds lives in tile.h now: it is a property of a grid, and the box a
// transform draws round a drawing with no selection is the same rectangle this
// picks a solve resolution with. The reasoning that put it here -- what an
// emptied tile does to a bounding box -- moved with it.

std::vector<float> ctgBarrier(const std::vector<TileGrid>& sources, const PixelRect& region,
                              int step) {
    step = std::max(1, step);
    const int width = (region.width + step - 1) / step;
    const int height = (region.height + step - 1) / step;
    std::vector<float> intensity(static_cast<std::size_t>(std::max(0, width)) *
                                     std::max(0, height),
                                 1.0f);
    if (width <= 0 || height <= 0 || sources.empty()) return intensity;

    // Drawn plainly, at full strength. What the barrier asks of a source is
    // where its ink is, and a layer's opacity is about looking at it.
    static const Layer kPlain;
    std::vector<LayerPass> passes;
    passes.reserve(sources.size());
    for (const TileGrid& source : sources) passes.push_back({&source, &kPlain});

    // Sampled at full resolution and reduced, never point-sampled.
    //
    // The compositor can sample every nth pixel itself, and using that here was
    // a bug that hid until the solve region became the whole canvas and the step
    // grew: line art is thin, and a two-pixel line looked at every sixth pixel
    // is a dotted line. The barrier then had holes that were not in the drawing,
    // and the fill poured out through its own outline.
    //
    // The reduction takes the *most* covered pixel in each block, so a line
    // passing anywhere through a block still stops a cut there. That bias is the
    // right one: a barrier that is slightly too solid loses a little gap
    // tolerance, and one that is slightly too thin loses the whole fill.
    Compositor compositor;
    Framebuffer band;

    // A band at a time, counted in bytes.
    //
    // Banding at all is because the whole region at once is a hundred megabytes
    // of framebuffer on a 3000x2400 drawing. Counting the band in *coarse* rows
    // was the mistake: thirty-two coarse rows is thirty-two times `step` image
    // rows, so the buffer went on growing with the drawing exactly as it had
    // before. On a 16384-wide one matched at step 171 -- which is what
    // estimateCtgShift asks for, its grid being a fixed number of cells across
    // however large the drawing is -- that is 1.4 GB, asked for on a worker
    // thread where a bad_alloc is the end of the program rather than the end of
    // the fill.
    //
    // A band does not have to line up with a coarse row, and that is what lets
    // the floor here be one image row rather than one coarse row -- `step` times
    // smaller, and the difference between a bound and a smaller unbounded thing.
    // The reduction below accumulates with min() into an array that starts at
    // 1.0, so a coarse row finished by two bands is the same answer as one
    // finished by a single band.
    constexpr long long kBandBytes = 4LL << 20;
    const long long row_bytes =
        static_cast<long long>(region.width) * static_cast<long long>(sizeof(Rgba));
    const int band_rows =
        static_cast<int>(std::clamp(kBandBytes / std::max<long long>(1, row_bytes), 1LL,
                                    static_cast<long long>(region.height)));

    for (int y0 = 0; y0 < region.height; y0 += band_rows) {
        const PixelRect strip{region.x, region.y + y0, region.width,
                              std::min(band_rows, region.height - y0)};

        compositor.compositeGrids(passes, strip, band);

        const int rows = std::min(band.height(), strip.height);
        for (int row = 0; row < rows; ++row) {
            // Which coarse row this image row falls in. One division is the
            // whole of what banding in image rows costs.
            float* out =
                intensity.data() + static_cast<std::size_t>((y0 + row) / step) * width;
            const Rgba* source = band.row(row);

            for (int x = 0; x < width; ++x) {
                const int from = x * step;
                const int to = std::min(from + step, band.width());
                float covered = 0.0f;
                for (int i = from; i < to; ++i) covered = std::max(covered, source[i].a);
                // Coverage is what stops a cut, so the barrier is one minus
                // alpha: solid ink reads as 0, bare paper as 1, and the
                // antialiased rim of a stroke reads as the grey between --
                // which is the whole reason the boundary can be placed inside
                // the line rather than beside it.
                out[x] = std::min(out[x], std::clamp(1.0f - covered, 0.0f, 1.0f));
            }
        }
    }
    return intensity;
}

namespace {

// One level of the search pyramid: ink coverage, 0 where the paper is bare.
struct InkLevel {
    int width = 0;
    int height = 0;
    std::vector<float> ink;
};

// Spread the ink out, a little, in place.
//
// Line art is thin, and two thin lines either coincide or they do not: agreement
// between them is all or nothing, and its maximum can be nowhere near the right
// answer. Two circles a person drew freehand at the same place are never quite
// the same size, and two rings of different radius have no overlap at all when
// they are concentric -- they overlap most when slid until they touch. So the
// sharp-ink answer for "the same circle, drawn twice" is "it moved by the
// difference of the radii", which is nonsense and is what was reported.
//
// Blurred, a drawing stops being a line and becomes a shape, and shapes agree
// most when they sit on top of each other. Three taps, and the pyramid does the
// rest: by the top level a stroke is a smudge several cells wide.
void blur(InkLevel& level) {
    std::vector<float> pass(level.ink.size(), 0.0f);
    const auto at = [&](const std::vector<float>& from, int x, int y) {
        x = std::clamp(x, 0, level.width - 1);
        y = std::clamp(y, 0, level.height - 1);
        return from[static_cast<std::size_t>(y) * level.width + x];
    };
    for (int y = 0; y < level.height; ++y) {
        for (int x = 0; x < level.width; ++x) {
            pass[static_cast<std::size_t>(y) * level.width + x] =
                0.25f * at(level.ink, x - 1, y) + 0.5f * at(level.ink, x, y) +
                0.25f * at(level.ink, x + 1, y);
        }
    }
    for (int y = 0; y < level.height; ++y) {
        for (int x = 0; x < level.width; ++x) {
            level.ink[static_cast<std::size_t>(y) * level.width + x] =
                0.25f * at(pass, x, y - 1) + 0.5f * at(pass, x, y) + 0.25f * at(pass, x, y + 1);
        }
    }
}

InkLevel halve(const InkLevel& fine) {
    InkLevel coarse;
    coarse.width = std::max(1, fine.width / 2);
    coarse.height = std::max(1, fine.height / 2);
    coarse.ink.assign(static_cast<std::size_t>(coarse.width) * coarse.height, 0.0f);

    // Averaged rather than maxed, which is the opposite of what the barrier
    // does and right for the opposite reason. A barrier must not lose a thin
    // line, because a hole in it is a fill pouring out; a correlation wants the
    // ink to weigh what there is of it, so that half a line under a cell counts
    // half.
    for (int y = 0; y < coarse.height; ++y) {
        for (int x = 0; x < coarse.width; ++x) {
            float sum = 0.0f;
            int count = 0;
            for (int dy = 0; dy < 2; ++dy) {
                const int sy = y * 2 + dy;
                if (sy >= fine.height) continue;
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = x * 2 + dx;
                    if (sx >= fine.width) continue;
                    sum += fine.ink[static_cast<std::size_t>(sy) * fine.width + sx];
                    ++count;
                }
            }
            coarse.ink[static_cast<std::size_t>(y) * coarse.width + x] =
                count ? sum / static_cast<float>(count) : 0.0f;
        }
    }
    return coarse;
}

// How much ink the two have in common, with `from` shifted by (dx, dy).
// Bigger is better, and it is what the search maximises.
//
// Agreement, and emphatically not difference. Difference was what this did
// first, and on line art it has a fatal minimum: a drawing is nearly all bare
// paper, so a *wrong* alignment is charged twice -- once for the ink it puts
// where there is none, once for the ink it fails to cover -- while sliding the
// whole drawing off the edge is charged only once, for the ink left uncovered.
// Two circles a person drew in the same place never coincide exactly, so
// "disappear entirely" scored better than "line them up", and the search
// happily reported four hundred and eighty pixels of movement for a circle that
// had not moved.
//
// Agreement cannot do that: ink pushed off the edge agrees with nothing and
// scores zero, which is the worst score there is rather than the best. It is
// also the right question to be asking -- what a translation is *for* is
// putting ink on ink.
double agreement(const InkLevel& from, const InkLevel& to, int dx, int dy) {
    double total = 0.0;
    for (int y = 0; y < to.height; ++y) {
        const int sy = y - dy;
        if (sy < 0 || sy >= from.height) continue;
        for (int x = 0; x < to.width; ++x) {
            const int sx = x - dx;
            if (sx < 0 || sx >= from.width) continue;
            total += static_cast<double>(from.ink[static_cast<std::size_t>(sy) * from.width + sx]) *
                     static_cast<double>(to.ink[static_cast<std::size_t>(y) * to.width + x]);
        }
    }
    return total;
}

}  // namespace

CtgShift estimateCtgShift(const std::vector<TileGrid>& from, const std::vector<TileGrid>& to,
                          const PixelRect& area) {
    if (area.isEmpty() || from.empty() || to.empty()) return {};

    // A few dozen cells across is enough to find a translation, and it is what
    // keeps an exhaustive search affordable: the cost is offsets times cells,
    // and both scale with this.
    constexpr int kAcross = 96;
    const int step = std::max(1, (std::max(area.width, area.height) + kAcross - 1) / kAcross);

    InkLevel a;
    InkLevel b;
    a.width = b.width = (area.width + step - 1) / step;
    a.height = b.height = (area.height + step - 1) / step;
    if (a.width < 4 || a.height < 4) return {};

    a.ink = ctgBarrier(from, area, step);
    b.ink = ctgBarrier(to, area, step);
    for (float& value : a.ink) value = 1.0f - value;  // coverage, not intensity
    for (float& value : b.ink) value = 1.0f - value;

    // Nothing to match. Two blank drawings agree at every offset, and the
    // smallest shift is the honest answer.
    const auto ink_total = [](const InkLevel& level) {
        double sum = 0.0;
        for (float value : level.ink) sum += value;
        return sum;
    };
    if (ink_total(a) < 1.0 || ink_total(b) < 1.0) return {};

    blur(a);
    blur(b);

    std::vector<InkLevel> pyramid_a{a};
    std::vector<InkLevel> pyramid_b{b};
    while (pyramid_a.back().width > 12 && pyramid_a.back().height > 12) {
        pyramid_a.push_back(halve(pyramid_a.back()));
        pyramid_b.push_back(halve(pyramid_b.back()));
        blur(pyramid_a.back());
        blur(pyramid_b.back());
    }

    // Exhaustive at the top, where the grid is a dozen cells across and every
    // shift can simply be tried, then one refinement per level down. A
    // translation found at the top is worth two cells at the next level, so the
    // window below only has to cover the halving.
    //
    // Every shift, and not half of them. The area covers both drawings, so a
    // shape that moved by more than half of it -- which is any shape that has
    // moved most of its own width -- sits outside a window of half the grid,
    // and the search then reports the best wrong answer with no sign that it
    // was looking in the wrong place. It is a few hundred thousand operations
    // at this size either way.
    CtgShift best;
    for (int level = static_cast<int>(pyramid_a.size()) - 1; level >= 0; --level) {
        const InkLevel& coarse_a = pyramid_a[static_cast<std::size_t>(level)];
        const InkLevel& coarse_b = pyramid_b[static_cast<std::size_t>(level)];
        const bool top = level == static_cast<int>(pyramid_a.size()) - 1;

        const int reach_x = top ? coarse_b.width : 2;
        const int reach_y = top ? coarse_b.height : 2;
        const CtgShift from_above{best.x * (top ? 1 : 2), best.y * (top ? 1 : 2)};

        // Ties go to the shift already in hand, which at the top is no shift at
        // all. Nothing to choose between two alignments means the drawing did
        // not move, and that is the answer that carries a mark unchanged.
        CtgShift found = from_above;
        double score = agreement(coarse_a, coarse_b, found.x, found.y);
        for (int dy = from_above.y - reach_y; dy <= from_above.y + reach_y; ++dy) {
            for (int dx = from_above.x - reach_x; dx <= from_above.x + reach_x; ++dx) {
                const double here = agreement(coarse_a, coarse_b, dx, dy);
                if (here <= score) continue;
                score = here;
                found = {dx, dy};
            }
        }
        best = found;
    }

    return {best.x * step, best.y * step};
}

CtgFill solveCtgJob(const CtgJob& job, bool want_labels, const std::atomic<bool>* abandon) {
    const CtgFill kNothing;
    if (!job.valid) return kNothing;

    // Two rectangles, and the difference between them is where the resolution
    // comes from.
    //
    // `filled` is the canvas: the whole picture takes a colour, and nothing
    // outside the picture does. `region` is only the part worth solving -- what
    // has been drawn on, plus a tile of margin -- and the labels are extended
    // outwards from it to cover the rest.
    //
    // The extension is exact rather than an approximation. Outside the drawn
    // area there is, by definition, no line art, so everything out there is one
    // connected stretch of blank paper: a cut cannot pass through it and it can
    // only take one label. Whatever label reaches the edge of the solved region
    // is therefore the label of everything beyond that edge, and clamping the
    // lookup at the region's border is exactly that answer.
    //
    // Solving the canvas directly instead was correct and wasteful: the budget
    // below is on the number of cells, so paying for empty paper is paid for in
    // resolution over the drawing. A small drawing on a 1080p canvas was solved
    // at a third of full size when it could be solved at full size.
    const PixelRect filled = job.canvas;
    if (filled.isEmpty()) {
        CtgFill empty;
        empty.inputs = job.inputs;
        empty.budget = job.budget;
        empty.valid = true;
        return empty;
    }

    // Where the drawing has got to since the marks were made on it.
    //
    // Estimated here rather than stored anywhere: both drawings' line art is in
    // the job, so the answer can be worked out again whenever it is wanted and
    // thrown away with the fill it produced. A carried mark is provisional by
    // definition, and a stored transform would be a second derived thing to
    // keep in step with drawings that move.
    //
    // Empty origin_sources is how the job says not to: the marks are this
    // drawing's own, or the layer is set to leave them where they were put.
    CtgShift shift;
    if (!job.origin_sources.empty()) {
        PixelRect ink = intersect(
            [&] {
                PixelRect all;
                for (const TileGrid& source : job.sources) {
                    all = unite(all, drawnBounds(source));
                }
                for (const TileGrid& source : job.origin_sources) {
                    all = unite(all, drawnBounds(source));
                }
                return all;
            }(),
            filled);
        shift = estimateCtgShift(job.origin_sources, job.sources, ink);
        if (abandoned(abandon)) return kNothing;
    }

    // The marks, where they are being read from: their own place plus wherever
    // the drawing has taken them.
    PixelRect region = drawnBounds(job.scribbles);
    region = {region.x + shift.x, region.y + shift.y, region.width, region.height};
    for (const TileGrid& source : job.sources) {
        region = unite(region, drawnBounds(source));
    }
    region = {region.x - kTileSize, region.y - kTileSize, region.width + 2 * kTileSize,
              region.height + 2 * kTileSize};
    region = intersect(region, filled);
    if (region.isEmpty()) {
        CtgFill empty;
        empty.canvas = filled;
        empty.inputs = job.inputs;
        empty.budget = job.budget;
        empty.valid = true;
        return empty;
    }

    // The solve is bounded by whatever the caller can afford to wait for. Where
    // the interface is waiting that is a few hundred thousand cells, because a
    // max-flow over a region grows faster than the region does and an unbounded
    // one on a large drawing does not take a while, it stops the program.
    // Somewhere else, it is nothing at all: a coarse answer arrives first and a
    // full-resolution one replaces it, and neither of them is in anybody's way.
    int step = std::max(1, job.settings.downscale);
    while (job.budget > 0 && static_cast<long long>((region.width + step - 1) / step) *
                                     ((region.height + step - 1) / step) >
                                 job.budget) {
        ++step;
    }

    LazyBrushProblem problem;
    problem.width = (region.width + step - 1) / step;
    problem.height = (region.height + step - 1) / step;
    problem.intensity = ctgBarrier(job.sources, region, step);
    problem.seeds.assign(static_cast<std::size_t>(problem.width) * problem.height, -1);
    if (abandoned(abandon)) return kNothing;

    // Read the scribbles off the grid, one label per distinct colour.
    //
    // Nothing is seeded that the user did not draw. An implicit background at
    // the rim was tried, so that one scribble could fill one shape rather than
    // needing a second scribble for the world outside it -- and it was removed.
    // Its strength could not be made to work: weak enough to lose to a real
    // scribble is weak enough for a gap in the line to defeat, and strong
    // enough to hold a gapped shape is strong enough to overrule the scribble
    // the user actually drew. Making it conditional on there being one colour
    // only moved the surprise to the moment a second colour appeared.
    //
    // What replaced it is the unseverable rim inside the solver itself. See
    // LazyBrushOptions::implicit_background.
    std::unordered_map<std::uint32_t, int> index_of;
    std::vector<std::uint32_t> palette;

    for (int y = 0; y < problem.height; ++y) {
        for (int x = 0; x < problem.width; ++x) {
            // Read through the shift: the mark is where it was drawn, and this
            // is the drawing having moved out from under it.
            const Rgba pixel = job.scribbles.pixel(region.x + x * step - shift.x,
                                                   region.y + y * step - shift.y);
            if (pixel.a < job.settings.scribble_alpha_threshold) continue;

            const std::uint32_t key = scribbleLabel(pixel);
            auto found = index_of.find(key);
            if (found == index_of.end()) {
                found = index_of.emplace(key, static_cast<int>(palette.size())).first;
                palette.push_back(key);
            }
            problem.seeds[static_cast<std::size_t>(y) * problem.width + x] = found->second;
        }
    }

    CtgFill built;
    built.canvas = filled;
    built.solved = region;
    built.step = step;
    built.inputs = job.inputs;
    built.budget = job.budget;
    built.valid = true;
    built.inherited = job.inherited;
    built.carried_by = shift;
    built.colours = static_cast<int>(palette.size());

    // No mark landed on the sampling lattice, so there is nothing to cut and
    // the labelling is empty -- but the marks are still shown at the bottom of
    // this function, at full resolution. This used to return from here instead,
    // which made a mark too small for the solve's own lattice invisible: the
    // one case where the rule that a mark shows its own pixels whatever the
    // solver decided was not kept.
    //
    // The verdict below is a no-op on an empty palette: every loop in it runs
    // over the colours there are.
    LazyBrushResult solved;
    if (!palette.empty()) {
        problem.colour_count = static_cast<int>(palette.size());
        problem.hard.assign(palette.size(), 0);

        solved = solveLazyBrush(problem, job.settings.lazybrush, abandon);
        if (solved.abandoned) return kNothing;
    }

    // Two numbers about how well each mark landed, both free at solve time and
    // both taken from the solver's labels rather than the finished fill. That
    // last part is the trap: a mark wins its own pixels in the fill whatever
    // the solver decided, so read back off the fill every mark is perfectly
    // placed, always. What is being asked is whether the *region* agreed, and
    // only the labelling knows.
    //
    // The worst mark is the score in both cases, never the average. One
    // scribble in the wrong place is a drawing to go and look at however well
    // the others landed, and averaging is exactly how it would be hidden.
    //
    // `confidence` is the fraction of a mark the solver labelled with the
    // mark's own colour, which is what the design notes propose. It is kept and
    // it is nearly useless, which is worth writing down so nobody derives it
    // again: over every case in test_ctg it is exactly 1. A seed is only
    // overruled when severing it beats isolating it, and that needs a mark
    // that is almost all edge, so in practice the solver honours what it can
    // see and this measures the wrong thing.
    //
    // `spread` is how much region a mark won for each pixel of itself, and it
    // is the one that separates. A mark that filled a shape wins many times its
    // own area; a mark carried onto blank paper wins nothing but itself,
    // because the cut simply hugs the seed. Measured: 17, 23, 65 and 188 for
    // marks that landed properly, and exactly 1.00 for one carried off its
    // shape.
    std::vector<long long> seeded(palette.size(), 0);
    std::vector<long long> honoured(palette.size(), 0);
    for (std::size_t i = 0; i < problem.seeds.size(); ++i) {
        const int seed = problem.seeds[i];
        if (seed < 0) continue;
        ++seeded[static_cast<std::size_t>(seed)];
        if (solved.labels[i] == seed) ++honoured[static_cast<std::size_t>(seed)];
    }
    std::vector<long long> won(palette.size(), 0);
    for (int label : solved.labels) {
        if (label >= 0) ++won[static_cast<std::size_t>(label)];
    }
    for (std::size_t c = 0; c < palette.size(); ++c) {
        if (seeded[c] <= 0) continue;
        built.confidence = std::min(built.confidence,
                                    static_cast<float>(honoured[c]) /
                                        static_cast<float>(seeded[c]));
        built.spread = std::min(built.spread, static_cast<float>(won[c]) /
                                                  static_cast<float>(seeded[c]));
    }

    // A caller that only wants the verdict stops here, and that is most of why
    // the verdict is affordable for a whole track: everything above is a
    // max-flow over a grid the size of the solve, and what it keeps below is
    // the labelling itself, which on a large sparse sheet is the larger half of
    // what a fill weighs.
    if (!want_labels) {
        return built;
    }
    if (abandoned(abandon)) return kNothing;

    // The answer, kept as the answer.
    //
    // There is no paint-out any more: the labels and the palette are the fill,
    // and ctgFillPixel works a colour out per pixel asked for -- extending the
    // labels outwards from the solve by clamping, exactly as the paint-out used
    // to, and letting a mark win in its own pixels at full resolution however
    // coarse the solve was. See docs/colour-without-a-canvas.md, phase 1.
    //
    // The marks travel with the fill, and the shift with them, so that reading
    // a fill needs nothing but the fill. Copying a grid copies handles, and a
    // run of drawings inheriting one scribble cel shares one set of pixels.
    built.marks = job.scribbles;
    built.mark_threshold = job.settings.scribble_alpha_threshold;
    built.palette = std::move(palette);
    // Narrowed to two bytes on the way in. See CtgFill::labels for why 32767
    // colours is not a cap anybody can reach.
    built.labels.assign(solved.labels.begin(), solved.labels.end());
    built.outside_is_clear = ringIsClear(solved.labels, problem.width, problem.height);
    return built;
}

}  // namespace animage
