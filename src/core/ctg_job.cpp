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

namespace {

// Where any source has a tile, over the tiles the region covers.
//
// One pass over every source's tile coordinates, which costs a hash walk and no
// pixels at all. A tile that exists but holds nothing counts as occupied and is
// composited anyway: it costs time and never an answer, and testing it would
// cost a scan of every pixel in it.
struct InkTiles {
    int origin_x = 0;  // tile coordinate of column 0
    int origin_y = 0;
    int width = 0;  // in tiles
    int height = 0;
    std::vector<std::uint8_t> occupied;

    bool anyIn(int column, int first_row, int last_row) const {
        for (int row = first_row; row <= last_row; ++row) {
            if (occupied[static_cast<std::size_t>(row) * width + column]) return true;
        }
        return false;
    }
};

InkTiles occupiedTiles(const std::vector<TileGrid>& sources, const PixelRect& region) {
    InkTiles ink;
    if (region.isEmpty()) return ink;

    const TileCoord first = tileCoordFor(region.x, region.y);
    const TileCoord last = tileCoordFor(region.x + region.width - 1, region.y + region.height - 1);
    ink.origin_x = first.x;
    ink.origin_y = first.y;
    ink.width = last.x - first.x + 1;
    ink.height = last.y - first.y + 1;
    ink.occupied.assign(static_cast<std::size_t>(ink.width) * ink.height, 0);

    for (const TileGrid& source : sources) {
        for (const auto& [coord, tile] : source.tiles()) {
            if (!tile) continue;
            const int column = coord.x - ink.origin_x;
            const int row = coord.y - ink.origin_y;
            if (column < 0 || column >= ink.width || row < 0 || row >= ink.height) continue;
            ink.occupied[static_cast<std::size_t>(row) * ink.width + column] = 1;
        }
    }
    return ink;
}

}  // namespace

std::vector<float> ctgInkCoverage(const std::vector<TileGrid>& sources, const PixelRect& region,
                                  int step, InkReduce reduce_with) {
    step = std::max(1, step);
    const int width = (region.width + step - 1) / step;
    const int height = (region.height + step - 1) / step;
    std::vector<float> coverage(static_cast<std::size_t>(std::max(0, width)) *
                                    std::max(0, height),
                                0.0f);
    if (width <= 0 || height <= 0 || sources.empty()) return coverage;

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

    const InkTiles ink = occupiedTiles(sources, region);

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
    // Both reductions decompose across bands, which is what the banding needs:
    // `Most` accumulates with max() into an array of zeros and `Mean`
    // accumulates a sum, divided at the end by the cell's own pixel count. So a
    // coarse row finished by two bands is the same answer as one finished by a
    // single band, and a cell no band touched stays at zero -- which is bare
    // paper, and correct for both.
    constexpr long long kBandBytes = 4LL << 20;
    const long long row_bytes =
        static_cast<long long>(region.width) * static_cast<long long>(sizeof(Rgba));
    const int band_rows =
        static_cast<int>(std::clamp(kBandBytes / std::max<long long>(1, row_bytes), 1LL,
                                    static_cast<long long>(region.height)));

    // One stretch of a band, flattened and reduced into the coarse rows it
    // touches. Whether it is the whole band or a run of tile columns inside it
    // makes no difference to any of this.
    const auto reduce = [&](int from_x, int to_x, int band_top, int band_height, int y0) {
        const PixelRect strip{from_x, band_top, to_x - from_x, band_height};
        compositor.compositeGrids(passes, strip, band);

        const int rows = std::min(band.height(), strip.height);
        const int first_cell = (from_x - region.x) / step;
        const int last_cell = std::min(width - 1, (to_x - 1 - region.x) / step);

        for (int row = 0; row < rows; ++row) {
            // Which coarse row this image row falls in. One division is the
            // whole of what banding in image rows costs.
            float* out =
                coverage.data() + static_cast<std::size_t>((y0 + row) / step) * width;
            const Rgba* source = band.row(row);

            for (int cell = first_cell; cell <= last_cell; ++cell) {
                // The part of the cell this stretch covers, which is all of it
                // unless the stretch began or ended inside one.
                const int from = std::max(from_x, region.x + cell * step);
                const int to = std::min({to_x, region.x + (cell + 1) * step,
                                         from_x + band.width()});

                if (reduce_with == InkReduce::Most) {
                    float covered = 0.0f;
                    for (int i = from; i < to; ++i) {
                        covered = std::max(covered, source[i - from_x].a);
                    }
                    out[cell] = std::max(out[cell], covered);
                } else {
                    float summed = 0.0f;
                    for (int i = from; i < to; ++i) summed += source[i - from_x].a;
                    out[cell] += summed;
                }
            }
        }
    };

    for (int y0 = 0; y0 < region.height; y0 += band_rows) {
        const int band_top = region.y + y0;
        const int band_height = std::min(band_rows, region.height - y0);

        // Only the runs of tile columns that have something under them, in this
        // band's own tile rows.
        //
        // Exact rather than an approximation, and that is what makes it small:
        // a stretch with no tile under it composites to fully transparent,
        // which is a coverage of zero -- the identity for max() and for a sum
        // alike. Skipping it and compositing it produce the same array,
        // including where a coarse cell straddles the end of a run.
        //
        // In both directions and not only by band. A whole-band test alone buys
        // everything on two patches stacked one above the other and nothing at
        // all on two side by side, since every row then has ink somewhere in it
        // -- and nothing on a long diagonal, which is an ordinary thing to draw.
        const int first_row = tileCoordFor(0, band_top).y - ink.origin_y;
        const int last_row = tileCoordFor(0, band_top + band_height - 1).y - ink.origin_y;

        int column = 0;
        while (column < ink.width) {
            if (!ink.anyIn(column, first_row, last_row)) {
                ++column;
                continue;
            }
            int end = column + 1;
            while (end < ink.width && ink.anyIn(end, first_row, last_row)) ++end;

            const int from_x = std::max(region.x, (ink.origin_x + column) * kTileSize);
            const int to_x =
                std::min(region.x + region.width, (ink.origin_x + end) * kTileSize);
            if (to_x > from_x) reduce(from_x, to_x, band_top, band_height, y0);
            column = end;
        }
    }

    // The mean's divisor is the cell's own pixel count, which is `step` squared
    // everywhere but along the far edges, where the region can end partway
    // through a cell. Dividing by the block a cell stands for rather than by
    // the part of it inside the region would read those edge cells as emptier
    // than they are.
    if (reduce_with == InkReduce::Mean) {
        for (int cy = 0; cy < height; ++cy) {
            const float rows_in = static_cast<float>(std::min(step, region.height - cy * step));
            for (int cx = 0; cx < width; ++cx) {
                const float columns_in =
                    static_cast<float>(std::min(step, region.width - cx * step));
                float& value = coverage[static_cast<std::size_t>(cy) * width + cx];
                value = std::clamp(value / (rows_in * columns_in), 0.0f, 1.0f);
            }
        }
    }
    return coverage;
}

std::vector<float> ctgBarrier(const std::vector<TileGrid>& sources, const PixelRect& region,
                              int step) {
    // Coverage is what stops a cut, so the barrier is one minus it: solid ink
    // reads as 0, bare paper as 1, and the antialiased rim of a stroke reads as
    // the grey between -- which is the whole reason the boundary can be placed
    // inside the line rather than beside it.
    std::vector<float> intensity = ctgInkCoverage(sources, region, step, InkReduce::Most);
    for (float& value : intensity) value = std::clamp(1.0f - value, 0.0f, 1.0f);
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

    // Averaged, the same way level zero is now built -- see InkReduce. This is
    // where the argument for it was written down first, and for a while it was
    // the only level that took it: a correlation wants the ink to weigh what
    // there is of it, so that half a line under a cell counts half.
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
// **Changing this will not fix the aliasing, and that has been measured.** Ink
// landing on blank paper earns nothing here and costs nothing, so covering one
// shape exactly and abandoning another looks better than covering both
// partially -- which is the obvious thing to blame for a mark sliding onto the
// wrong shape, and it is not the reason.
//
// On drawing 3 of tests/projects/two-circles.animage, where the estimate slides
// the whole drawing 780 px sideways and puts the left circle's mark on the
// right circle, the alias beats the honest small shift on *every* criterion
// that was tried, not only on this one:
//
//     criterion            alias (780 px)   honest (-72 px)
//     agreement                     5.181             4.606
//     intersection over union       0.350             0.219
//     normalised correlation        0.636             0.440
//     the worse of the two
//       coverages                   0.527             0.413
//     source ink left unmatched      8.72             33.18
//
// It is not that the score is measuring the wrong thing. As a description of
// two drawings by one translation, sliding everything sideways really is the
// better answer: the circles move most of their own width between drawings, so
// lining each up with itself overlaps badly, while lining one up with the other
// is nearly exact. The data supports the alias.
//
// So what is wrong is the model and not the measure. One translation cannot
// describe two shapes that moved differently, and when it is asked to, the
// answer it gives is whichever single shape it can explain best -- which is a
// coin toss between them and has nothing to do with which one the marks are on.
// Getting past it needs one of: a prior that says drawings do not jump (a
// scale nobody here has measured), a per-region search that does not start from
// the global answer, or a deformation that never picks one translation at all,
// which is the paper. See docs/scribbles-through-time.md.
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

// The fewest cells a grid can have across and still be searched over.
//
// Hoisted out of estimateCtgShift because the search is shared now: a region's
// slice can be smaller than a drawing, and "not enough grid to look at" has to
// mean the same thing to both.
constexpr int kLeastAcross = 4;

// The search itself, on two grids that are already ink.
//
// Split out from estimateCtgShift because rung three asks it the same question
// about a region that rung two asks about a drawing, and the two must not drift
// apart: every lesson in the comments below -- agreement rather than
// difference, blur before matching, ties to the answer in hand -- was paid for
// once and applies to both.
//
// What a region adds is the two arguments. `prior` is where to start and what
// to fall back to, which for a region is what the whole drawing did; `reach` is
// how far from it to look, in cells, with a negative meaning the whole grid.
// Together they are the only thing keeping regions from flying apart: a small
// box has less ink in it than a drawing and therefore more alignments that look
// good, and rung two already fails by answering confidently rather than by
// answering vaguely.
struct SearchResult {
    bool found = false;
    CtgShift shift;  // in cells of the level-0 grid
};

SearchResult searchShiftCells(InkLevel a, InkLevel b, CtgShift prior, int reach,
                              const std::atomic<bool>* abandon) {
    if (a.width != b.width || a.height != b.height) return {};
    if (a.width < kLeastAcross || a.height < kLeastAcross) return {};

    blur(a);
    blur(b);

    std::vector<InkLevel> pyramid_a{std::move(a)};
    std::vector<InkLevel> pyramid_b{std::move(b)};
    while (pyramid_a.back().width > 12 && pyramid_a.back().height > 12) {
        pyramid_a.push_back(halve(pyramid_a.back()));
        pyramid_b.push_back(halve(pyramid_b.back()));
        blur(pyramid_a.back());
        blur(pyramid_b.back());
    }

    // Exhaustive at the top, where the grid is a dozen cells across and every
    // shift can simply be tried -- then several of the answers it liked are
    // refined down separately and the one that is still best at full
    // resolution wins.
    //
    // Several, because one was not enough and the case that showed it is in the
    // tree. On drawing 2 of tests/projects/two-circles.animage the search
    // answered -84 px, which scores 3.73 at the finest level, while +84 px
    // scores 4.08 and was never looked at: the coarsest level preferred the
    // wrong one of two peaks and everything below it only ever refined that
    // choice. A coarse decision is exactly the decision the coarse level is
    // worst placed to make.
    //
    // This cannot answer worse than taking one. The peak the old search started
    // from is always among the candidates, refining from it follows the same
    // path it followed, and the winner is chosen on the finest level's score --
    // so the answer is that one or something that beats it where it counts.
    const int top = static_cast<int>(pyramid_a.size()) - 1;
    const bool bounded = reach >= 0;

    // The bound holds at *every* level, and not only where the search starts.
    //
    // Bounding the top alone is not a bound. Each level below refines by two of
    // its own cells, which is 2^level of the finest ones, so the refinement
    // walks several times as far again as the top level was ever allowed to
    // look. It did: with the window applied at the top only, a region of
    // bench_carry's divided box came back 114 px from where the drawing went,
    // on a search allowed 75. A bound that holds only at the coarsest level is
    // the kind that reads correct and is not.
    const auto centre_at = [&](int level) {
        return CtgShift{prior.x >> level, prior.y >> level};
    };
    const auto reach_at = [&](int level) { return std::max(1, reach >> level); };

    // How many of the top level's peaks are worth following down, and how far
    // apart two of them have to be to count as different answers.
    //
    // The separation is the refinement window: two starting points closer than
    // that converge on the same place and the second one is a wasted descent.
    // The count is a price rather than a rule -- more of them can only find a
    // better answer, never a worse one -- and four is where the descents stop
    // costing anything next to the exhaustive pass above them.
    constexpr std::size_t kCandidates = 4;
    constexpr int kApart = 3;

    const InkLevel& coarse_a = pyramid_a[static_cast<std::size_t>(top)];
    const InkLevel& coarse_b = pyramid_b[static_cast<std::size_t>(top)];

    // Everything, at the top of an unbounded search -- and not half of it. The
    // area covers both drawings, so a shape that moved by more than half of it,
    // which is any shape that has moved most of its own width, sits outside a
    // window of half the grid and the search then reports the best wrong answer
    // with nothing to say it was looking in the wrong place. It is a few
    // hundred thousand operations at this size either way.
    const CtgShift centre = centre_at(top);
    int low_x = centre.x - coarse_b.width;
    int high_x = centre.x + coarse_b.width;
    int low_y = centre.y - coarse_b.height;
    int high_y = centre.y + coarse_b.height;
    if (bounded) {
        const int allowed = reach_at(top);
        low_x = std::max(low_x, centre.x - allowed);
        high_x = std::min(high_x, centre.x + allowed);
        low_y = std::max(low_y, centre.y - allowed);
        high_y = std::min(high_y, centre.y + allowed);
    }

    std::vector<std::pair<double, CtgShift>> peaks;
    peaks.reserve(static_cast<std::size_t>(std::max(0, high_x - low_x + 1)) *
                  static_cast<std::size_t>(std::max(0, high_y - low_y + 1)));
    for (int dy = low_y; dy <= high_y; ++dy) {
        for (int dx = low_x; dx <= high_x; ++dx) {
            peaks.emplace_back(agreement(coarse_a, coarse_b, dx, dy), CtgShift{dx, dy});
        }
    }
    if (abandoned(abandon)) return {};
    std::sort(peaks.begin(), peaks.end(),
              [](const auto& left, const auto& right) { return left.first > right.first; });

    // The prior first, so that a tie anywhere below still goes to the answer
    // already in hand: nothing to choose between two alignments means this
    // region did whatever the drawing did, and for a whole drawing that it did
    // not move at all.
    std::vector<CtgShift> starts{centre};
    for (const auto& [score, at] : peaks) {
        if (starts.size() > kCandidates) break;
        const bool crowded = std::any_of(starts.begin(), starts.end(), [&](const CtgShift& taken) {
            return std::abs(taken.x - at.x) < kApart && std::abs(taken.y - at.y) < kApart;
        });
        if (!crowded) starts.push_back(at);
    }

    // One candidate, carried down to the finest level.
    const auto refine = [&](CtgShift from) {
        double score = agreement(coarse_a, coarse_b, from.x, from.y);
        for (int level = top - 1; level >= 0; --level) {
            const InkLevel& fine_a = pyramid_a[static_cast<std::size_t>(level)];
            const InkLevel& fine_b = pyramid_b[static_cast<std::size_t>(level)];

            // Two cells is what a halving can be out by.
            CtgShift here{from.x * 2, from.y * 2};
            int from_x = here.x - 2;
            int to_x = here.x + 2;
            int from_y = here.y - 2;
            int to_y = here.y + 2;
            if (bounded) {
                const CtgShift middle = centre_at(level);
                const int allowed = reach_at(level);
                here.x = std::clamp(here.x, middle.x - allowed, middle.x + allowed);
                here.y = std::clamp(here.y, middle.y - allowed, middle.y + allowed);
                from_x = std::max(from_x, middle.x - allowed);
                to_x = std::min(to_x, middle.x + allowed);
                from_y = std::max(from_y, middle.y - allowed);
                to_y = std::min(to_y, middle.y + allowed);
            }

            CtgShift found = here;
            score = agreement(fine_a, fine_b, found.x, found.y);
            for (int dy = from_y; dy <= to_y; ++dy) {
                for (int dx = from_x; dx <= to_x; ++dx) {
                    const double scored = agreement(fine_a, fine_b, dx, dy);
                    if (scored <= score) continue;
                    score = scored;
                    found = {dx, dy};
                }
            }
            from = found;
        }
        return std::pair<CtgShift, double>{from, score};
    };

    CtgShift best = centre;
    double best_score = -1.0;
    for (const CtgShift& from : starts) {
        if (abandoned(abandon)) return {};
        const auto [landed, score] = refine(from);
        if (score <= best_score) continue;  // ties keep the earlier candidate
        best_score = score;
        best = landed;
    }

    return {true, best};
}

}  // namespace

CtgShift estimateCtgShift(const std::vector<TileGrid>& from, const std::vector<TileGrid>& to,
                          const PixelRect& area) {
    if (area.isEmpty() || from.empty() || to.empty()) return {};

    // A few dozen cells across is enough to find a translation, and it is what
    // keeps an exhaustive search affordable: the cost is offsets times cells,
    // and both scale with this.
    constexpr int kAcross = 96;

    // But the step has to leave the *short* axis a grid too, and it was taken
    // from the long one alone. A region much wider than it is tall therefore
    // collapsed the short axis below the minimum above and the whole estimate
    // was abandoned -- silently, and back to carrying marks unchanged. Twenty-
    // four to one was enough, which was unreachable while the region was
    // clipped to the canvas and is ordinary now that it is the drawn bounds of
    // a whole sheet.

    // And a ceiling on the long axis, because paying for the short one in step
    // is paying for the long one in cells: the exhaustive search at the top is
    // offsets times cells, which is roughly the fourth power of the grid. A
    // sliver is the one shape where the two cannot both be had, and that is
    // what the minimum below is then for.
    constexpr int kMostAcross = 4 * kAcross;

    const int longest = std::max(area.width, area.height);
    const int shortest = std::min(area.width, area.height);
    const int wanted = std::max(1, (longest + kAcross - 1) / kAcross);
    const int coarsest = std::max(1, shortest / kLeastAcross);
    const int finest = std::max(1, (longest + kMostAcross - 1) / kMostAcross);
    const int step = std::max(finest, std::min(wanted, coarsest));

    InkLevel a;
    InkLevel b;
    a.width = b.width = (area.width + step - 1) / step;
    a.height = b.height = (area.height + step - 1) / step;

    // Now this means what it says: not enough region to search over, rather
    // than enough region in one direction and none in the other.
    if (a.width < kLeastAcross || a.height < kLeastAcross) return {};

    // Averaged and not maxed, which is the opposite of what the barrier does
    // and right for the opposite reason -- see InkReduce. `halve` below has
    // always averaged, and said in its own comment why the barrier must not;
    // level zero was the one level built the other way, and only because it was
    // borrowing a function written for something else.
    a.ink = ctgInkCoverage(from, area, step, InkReduce::Mean);
    b.ink = ctgInkCoverage(to, area, step, InkReduce::Mean);

    // Nothing to match. Two blank drawings agree at every offset, and the
    // smallest shift is the honest answer.
    //
    // Counted in image pixels of ink and not in cells, because a threshold has
    // to mean the same thing at every step and this one stopped when the
    // reduction changed under it. `Most` gave a cell holding any ink a value
    // near 1, so a sum over cells was "how many cells have ink in them" and a
    // threshold of one meant "none of them". `Mean` gives that same cell
    // `ink / step^2`, so the same sum is the ink divided by a cell's area --
    // and the same threshold silently became "fewer than step^2 pixels of ink",
    // which at a step of 107 is four hundred times stricter than it reads.
    //
    // That step is not hypothetical. The region is the drawn bounds of the
    // whole sheet now rather than the canvas, so two things drawn ten thousand
    // pixels apart give exactly it -- and the drawing then had no shift
    // estimated at all, silently, falling back to carrying marks unchanged.
    // Measured: 428 px found for a true 400 before, 0 after.
    //
    // Multiplying by the cell's area restores what was meant and says it in a
    // unit that does not move: fewer than one opaque pixel of ink in the whole
    // drawing is nothing to match.
    const double cell_pixels = static_cast<double>(step) * static_cast<double>(step);
    const auto ink_pixels = [&](const InkLevel& level) {
        double sum = 0.0;
        for (float value : level.ink) sum += value;
        return sum * cell_pixels;
    };
    if (ink_pixels(a) < 1.0 || ink_pixels(b) < 1.0) return {};

    const SearchResult found = searchShiftCells(std::move(a), std::move(b), {}, -1, nullptr);
    if (!found.found) return {};
    return {found.shift.x * step, found.shift.y * step};
}

namespace {

// How many cells the source drawing's labelling may cover.
//
// Coarse deliberately. What is wanted from it is which pieces of the drawing
// the marks own and roughly where the boundary between two of them runs -- a
// judgement about areas, which survives a blocky answer, exactly as the
// whole-track audit's did. Solving it finely would be paying for a second full
// solve inside every solve, and nothing downstream could tell the difference.
constexpr long long kRegionSolveBudget = 256LL * 256;

// How coarse the field a warp carries is allowed to be.
//
// The warp is stored per drawing in a map nothing evicts, so its size is a
// budget and not a detail: at the labelling's own resolution, marks spread over
// a 1080p drawing would be half a megabyte of shifts per drawing.
//
// What it costs is sharpness at the seam between two regions that moved
// differently, and the majority rule is what makes that affordable. A mark
// needs most of its pixels in the right region; a mark straddling a seam has a
// strip of itself up to this wide carried with the neighbour, and it still wins
// the region it was drawn in.
constexpr int kWarpCellFloor = 32;

// The drawing the marks were made on, cut into the pieces the marks own.
//
// This is the design note's "the previous drawing's solved fill already gives
// regions for nothing", solved here rather than looked up. The job carries the
// source drawing's line art and the marks made on it, which is everything the
// cut needs -- and a job may not read the document, so the fill of the drawing
// they came from is not something this could ask for even if it were certain to
// still be in the cache, which on a drawing nobody has visited it is not.
//
// A component is a connected piece of one label and not the label itself. Two
// shapes scribbled the same colour are one label, and a box round both is the
// box round the drawing -- which is rung two again, on exactly the drawings
// rung three is for.
struct MarkRegions {
    PixelRect area;
    int step = 1;
    int width = 0;
    int height = 0;
    std::vector<int> component;  // -1 where nothing reached, or where no mark owns it
    int count = 0;
};

MarkRegions markRegions(const std::vector<TileGrid>& from, const TileGrid& marks,
                        const CtgSettings& settings, const std::atomic<bool>* abandon) {
    MarkRegions found;

    PixelRect area = drawnBounds(marks);
    for (const TileGrid& source : from) area = unite(area, drawnBounds(source));
    if (area.isEmpty()) return found;
    area = {area.x - kTileSize, area.y - kTileSize, area.width + 2 * kTileSize,
            area.height + 2 * kTileSize};

    int step = std::max(1, settings.downscale);
    while (static_cast<long long>((area.width + step - 1) / step) *
               ((area.height + step - 1) / step) >
           kRegionSolveBudget) {
        ++step;
    }

    LazyBrushProblem problem;
    problem.width = (area.width + step - 1) / step;
    problem.height = (area.height + step - 1) / step;
    if (problem.width < kLeastAcross || problem.height < kLeastAcross) return found;
    problem.intensity = ctgBarrier(from, area, step);
    problem.seeds.assign(static_cast<std::size_t>(problem.width) * problem.height, -1);

    // The marks where they were made, because this is the drawing they were
    // made on. Nothing is carried yet -- what is being worked out is what would
    // carry them.
    std::unordered_map<std::uint32_t, int> index_of;
    int colours = 0;
    for (int y = 0; y < problem.height; ++y) {
        for (int x = 0; x < problem.width; ++x) {
            const Rgba pixel = marks.pixel(area.x + x * step, area.y + y * step);
            if (pixel.a < settings.scribble_alpha_threshold) continue;
            const std::uint32_t key = scribbleLabel(pixel);
            auto at = index_of.find(key);
            if (at == index_of.end()) at = index_of.emplace(key, colours++).first;
            problem.seeds[static_cast<std::size_t>(y) * problem.width + x] = at->second;
        }
    }
    if (colours == 0) return found;  // no mark landed on this lattice
    problem.colour_count = colours;
    problem.hard.assign(static_cast<std::size_t>(colours), 0);

    const LazyBrushResult solved = solveLazyBrush(problem, settings.lazybrush, abandon);
    if (solved.abandoned) return found;
    if (solved.labels.size() != problem.seeds.size()) return found;

    // Connected pieces of one label, then only the pieces a mark is in.
    //
    // A piece with no mark in it is a part of the drawing nothing is being
    // carried into, so what it did with itself is not a question anybody is
    // asking. Dropping them keeps the searches below to the number of marks
    // rather than to the number of shapes.
    const std::size_t cells = solved.labels.size();
    std::vector<int> owner(cells, -1);
    std::vector<char> owns_a_mark;
    std::vector<int> stack;

    for (std::size_t seed = 0; seed < cells; ++seed) {
        if (solved.labels[seed] < 0 || owner[seed] >= 0) continue;
        const int label = solved.labels[seed];
        const int id = static_cast<int>(owns_a_mark.size());
        owns_a_mark.push_back(0);

        stack.clear();
        stack.push_back(static_cast<int>(seed));
        owner[seed] = id;
        while (!stack.empty()) {
            const int at = stack.back();
            stack.pop_back();
            if (problem.seeds[static_cast<std::size_t>(at)] >= 0) {
                owns_a_mark[static_cast<std::size_t>(id)] = 1;
            }
            const int cx = at % problem.width;
            const int cy = at / problem.width;
            const auto visit = [&](int nx, int ny) {
                if (nx < 0 || ny < 0 || nx >= problem.width || ny >= problem.height) return;
                const std::size_t index = static_cast<std::size_t>(ny) * problem.width + nx;
                if (owner[index] >= 0 || solved.labels[index] != label) return;
                owner[index] = id;
                stack.push_back(static_cast<int>(index));
            };
            visit(cx - 1, cy);
            visit(cx + 1, cy);
            visit(cx, cy - 1);
            visit(cx, cy + 1);
        }
    }

    std::vector<int> renumbered(owns_a_mark.size(), -1);
    int count = 0;
    for (std::size_t id = 0; id < owns_a_mark.size(); ++id) {
        if (owns_a_mark[id]) renumbered[id] = count++;
    }

    found.area = area;
    found.step = step;
    found.width = problem.width;
    found.height = problem.height;
    found.count = count;
    found.component.assign(cells, -1);
    for (std::size_t i = 0; i < cells; ++i) {
        if (owner[i] >= 0) found.component[i] = renumbered[static_cast<std::size_t>(owner[i])];
    }
    return found;
}

// The largest multiple of `step` from `origin` that is not past `value`.
int snappedDown(int value, int origin, int step) {
    const int delta = value - origin;
    const int cells = (delta >= 0) ? delta / step : -((-delta + step - 1) / step);
    return origin + cells * step;
}

}  // namespace

CtgWarp estimateCtgWarp(const std::vector<TileGrid>& from, const std::vector<TileGrid>& to,
                        const TileGrid& marks, const CtgSettings& settings,
                        const std::atomic<bool>* abandon) {
    CtgWarp warp;

    PixelRect ink;
    for (const TileGrid& source : to) ink = unite(ink, drawnBounds(source));
    for (const TileGrid& source : from) ink = unite(ink, drawnBounds(source));
    warp.overall = estimateCtgShift(from, to, ink);
    if (marks.empty() || abandoned(abandon)) return warp;

    if (!settings.carry_per_region) return warp;

    // What the marks own on the drawing they were made on.
    const MarkRegions regions = markRegions(from, marks, settings, abandon);
    if (regions.count < 2 || abandoned(abandon)) return warp;

    // One pair of ink grids for every region, at the labelling's resolution and
    // over everything either drawing touches.
    //
    // Built once and sliced, rather than measured per region. A region's search
    // wants its own small box, but ctgInkCoverage pays for compositing the ink
    // under that box at full resolution -- so asking it once per region would
    // multiply the one genuinely expensive part of this by the number of marks,
    // and the boxes overlap, so most of that would be the same pixels again.
    //
    // The origin is snapped to the labelling's grid so that a cell of one is a
    // cell of the other and the two are related by a constant, which is what
    // lets the mask below be a lookup rather than a rescale.
    const int step = regions.step;
    PixelRect field = unite(ink, regions.area);
    const int snapped_x = snappedDown(field.x, regions.area.x, step);
    const int snapped_y = snappedDown(field.y, regions.area.y, step);
    field = {snapped_x, snapped_y, field.x + field.width - snapped_x,
             field.y + field.height - snapped_y};

    InkLevel whole_from;
    InkLevel whole_to;
    whole_from.width = whole_to.width = (field.width + step - 1) / step;
    whole_from.height = whole_to.height = (field.height + step - 1) / step;
    if (whole_from.width < kLeastAcross || whole_from.height < kLeastAcross) return warp;
    whole_from.ink = ctgInkCoverage(from, field, step, InkReduce::Mean);
    whole_to.ink = ctgInkCoverage(to, field, step, InkReduce::Mean);
    if (abandoned(abandon)) return warp;

    const int offset_x = (regions.area.x - field.x) / step;
    const int offset_y = (regions.area.y - field.y) / step;

    // Each region's own cells, as a box on the labelling's grid.
    struct CellBox {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;  // half-open
        int y1 = 0;
        bool any = false;
    };
    std::vector<CellBox> boxes(static_cast<std::size_t>(regions.count));
    for (int y = 0; y < regions.height; ++y) {
        for (int x = 0; x < regions.width; ++x) {
            const int id = regions.component[static_cast<std::size_t>(y) * regions.width + x];
            if (id < 0) continue;
            CellBox& box = boxes[static_cast<std::size_t>(id)];
            if (!box.any) {
                box = {x, y, x + 1, y + 1, true};
                continue;
            }
            box.x0 = std::min(box.x0, x);
            box.y0 = std::min(box.y0, y);
            box.x1 = std::max(box.x1, x + 1);
            box.y1 = std::max(box.y1, y + 1);
        }
    }

    // And what each of them did, starting from what the drawing did.
    std::vector<CtgShift> moved(static_cast<std::size_t>(regions.count), warp.overall);
    bool anyone_disagreed = false;

    const auto prior_cells = CtgShift{warp.overall.x / step, warp.overall.y / step};

    for (int id = 0; id < regions.count; ++id) {
        if (abandoned(abandon)) return CtgWarp{warp.overall, {}, 1, {}};
        const CellBox& box = boxes[static_cast<std::size_t>(id)];
        if (!box.any) continue;

        // How far this region is allowed to have gone that the drawing did not.
        //
        // Half its own *shorter* side, and both halves of that are measured
        // rather than chosen.
        //
        // Half a region's width is where a carried mark stops holding its
        // region -- bench_carry's first table, and it is the majority rule
        // rather than a property of any estimator. So beyond this the mark is
        // lost whatever the search answers, and what a wider window buys is
        // only the chance of a confident wrong answer.
        //
        // The shorter side and not the longer one because that is the distance
        // at which a region's own ink starts repeating: a rectangle's outline
        // matches the next rectangle's outline one width along, and with the
        // longer side as the window the two halves of bench_carry's divided box
        // did exactly that -- 150 px of "movement" on a box that had moved with
        // everything else, and the right half took the left half's colour on
        // every drawing. Measured before and after; the reach is the whole of
        // the difference.
        //
        // A wider window and a confidence margin instead of this was measured
        // and not built. Scoring a region's best alignment against its score at
        // the prior does separate the two -- the divided box's wrong answers
        // reached x1.003 to x1.575, and the departures the apart case genuinely
        // needs ran x1.61 to x14.8 -- but the gap between 1.575 and 1.607 is a
        // constant fitted to a fixture, and this codebase has been bitten by
        // that shape of number before. What it would buy is the one case this
        // cannot reach: a region that moved further than half its own width
        // relative to the drawing, which is past where a carried mark holds its
        // region anyway. A two-stage search -- this window unconditionally, and
        // a wider one accepted only above about x2 -- is where to start if that
        // case turns out to matter.
        const int across = std::min(box.x1 - box.x0, box.y1 - box.y0);
        const int reach = std::max(across / 2, kLeastAcross);

        const int x0 = std::max(0, box.x0 + offset_x - reach);
        const int y0 = std::max(0, box.y0 + offset_y - reach);
        const int x1 = std::min(whole_from.width, box.x1 + offset_x + reach);
        const int y1 = std::min(whole_from.height, box.y1 + offset_y + reach);
        if (x1 - x0 < kLeastAcross || y1 - y0 < kLeastAcross) continue;

        InkLevel mine;
        InkLevel theirs;
        mine.width = theirs.width = x1 - x0;
        mine.height = theirs.height = y1 - y0;
        mine.ink.assign(static_cast<std::size_t>(mine.width) * mine.height, 0.0f);
        theirs.ink.assign(mine.ink.size(), 0.0f);

        // The source side is masked to this region and the target side is not.
        //
        // That is the whole of what makes this a region's question rather than
        // a box's: what is being asked is where *this* region's ink went, and
        // the answer is allowed to be anywhere in the neighbourhood -- so the
        // ink of the region next door must not be in the source or it drags the
        // answer back towards what that neighbour did.
        //
        // Masked a cell out from the region's own cells, because the labelling
        // is the region's inside and the ink is its outline: the cut runs
        // through the line, so the line itself belongs to whichever side won it
        // and a mask of the interior alone would throw away the only thing
        // there is to match.
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const std::size_t at =
                    static_cast<std::size_t>(y - y0) * mine.width + (x - x0);
                const std::size_t whole =
                    static_cast<std::size_t>(y) * whole_from.width + x;
                theirs.ink[at] = whole_to.ink[whole];

                bool near_region = false;
                for (int dy = -1; dy <= 1 && !near_region; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int lx = x - offset_x + dx;
                        const int ly = y - offset_y + dy;
                        if (lx < 0 || ly < 0 || lx >= regions.width || ly >= regions.height) {
                            continue;
                        }
                        if (regions.component[static_cast<std::size_t>(ly) * regions.width + lx] ==
                            id) {
                            near_region = true;
                            break;
                        }
                    }
                }
                if (near_region) mine.ink[at] = whole_from.ink[whole];
            }
        }

        // Nothing to match, in the unit the whole drawing's guard uses: fewer
        // than one opaque pixel of ink. A region whose outline the mask kept
        // none of has no question to answer, and the drawing's own answer is
        // the honest one for it.
        const double cell_pixels = static_cast<double>(step) * static_cast<double>(step);
        const auto ink_pixels = [&](const InkLevel& level) {
            double sum = 0.0;
            for (float value : level.ink) sum += value;
            return sum * cell_pixels;
        };
        if (ink_pixels(mine) < 1.0 || ink_pixels(theirs) < 1.0) continue;

        const SearchResult found =
            searchShiftCells(std::move(mine), std::move(theirs), prior_cells, reach, abandon);
        if (!found.found) continue;

        const CtgShift shift{found.shift.x * step, found.shift.y * step};
        moved[static_cast<std::size_t>(id)] = shift;
        if (!(shift == warp.overall)) anyone_disagreed = true;
    }

    // Every region did what the drawing did, so this is rung two and says so.
    // Worth being exact about: a uniform warp is the cheap path everywhere that
    // reads one, and a field of identical shifts would pay for a field and buy
    // nothing.
    if (!anyone_disagreed) return warp;

    // The field, over the marks and no further.
    //
    // Nothing reads it anywhere else: the one thing a warp is ever asked is
    // where a mark pixel goes, and outside the marks the answer is the
    // drawing's. Cropping it here is what keeps a warp a few kilobytes on a
    // drawing whose labelling was half a megabyte.
    const PixelRect marked = drawnBounds(marks);
    if (marked.isEmpty()) return warp;

    warp.step = std::max(step, kWarpCellFloor);
    warp.area = marked;
    const int field_w = (marked.width + warp.step - 1) / warp.step;
    const int field_h = (marked.height + warp.step - 1) / warp.step;
    warp.cells.assign(static_cast<std::size_t>(field_w) * field_h, warp.overall);

    for (int cy = 0; cy < field_h; ++cy) {
        for (int cx = 0; cx < field_w; ++cx) {
            // Whichever region covers most of this cell, since a warp cell is
            // coarser than a labelling cell and a seam can run through it.
            std::unordered_map<int, int> votes;
            int winner = -1;
            int best = 0;
            for (int y = 0; y < warp.step; y += step) {
                for (int x = 0; x < warp.step; x += step) {
                    const int lx = (marked.x + cx * warp.step + x - regions.area.x) / step;
                    const int ly = (marked.y + cy * warp.step + y - regions.area.y) / step;
                    if (lx < 0 || ly < 0 || lx >= regions.width || ly >= regions.height) continue;
                    const int id =
                        regions.component[static_cast<std::size_t>(ly) * regions.width + lx];
                    if (id < 0) continue;
                    const int count = ++votes[id];
                    if (count > best) {
                        best = count;
                        winner = id;
                    }
                }
            }
            if (winner < 0) continue;
            warp.cells[static_cast<std::size_t>(cy) * field_w + cx] =
                moved[static_cast<std::size_t>(winner)];
        }
    }
    return warp;
}

CtgFill solveCtgJob(const CtgJob& job, bool want_labels, const std::atomic<bool>* abandon) {
    const CtgFill kNothing;
    if (!job.valid) return kNothing;

    // One rectangle now, and nothing clips it.
    //
    // `region` is what is worth solving -- what has been drawn on, plus a tile
    // of margin -- and the labels are extended outwards from it to cover
    // everything else, which is to say the rest of the world.
    //
    // The extension is exact rather than an approximation. Outside the drawn
    // area there is, by definition, no line art, so everything out there is one
    // connected stretch of blank paper: a cut cannot pass through it and it can
    // only take one label. Whatever label reaches the edge of the solved region
    // is therefore the label of everything beyond that edge, and clamping the
    // lookup at the region's border is exactly that answer. Nothing in that
    // argument names a rectangle -- the canvas was only ever where somebody
    // stopped writing.
    //
    // It used to be clipped to the canvas, and that clip is the one this whole
    // change is about: a shape running off the frame was coloured up to the
    // frame and no further, and a ball animating off-screen lost its colour at
    // the frame line. What the clip was quietly also doing was protecting the
    // solve's resolution, since the budget below is on the number of cells --
    // so ink far off the frame now coarsens the whole drawing. Time and memory
    // do not run away, because the budget still caps both and the barrier costs
    // what the ink costs; what is lost is sharpness, and that is issue #61.

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
    CtgWarp warp;
    if (!job.origin_sources.empty()) {
        warp = estimateCtgWarp(job.origin_sources, job.sources, job.scribbles, job.settings,
                               abandon);
        if (abandoned(abandon)) return kNothing;
    }

    // Moved once, here, and read where they are from now on.
    //
    // Every other reader of the marks in this function -- the seeding, the
    // region, the bounds the fill reports -- takes them from this grid, so
    // there is one place where a mark's position is decided and no arithmetic
    // anywhere else to get wrong. It also means a warp with a field costs the
    // same to read as one without.
    const TileGrid marks =
        ctgCarriedMarks(job.scribbles, warp, job.settings.scribble_alpha_threshold);

    // The marks, where they are being read from.
    PixelRect region = drawnBounds(marks);
    for (const TileGrid& source : job.sources) {
        region = unite(region, drawnBounds(source));
    }
    if (region.isEmpty()) {
        // Nothing drawn is still an empty fill. The margin is added below
        // rather than here so that this stays reachable: a tile of margin round
        // nothing is a rectangle, and solving one would be solving a square of
        // blank paper to find out it is blank.
        CtgFill empty;
        empty.inputs = job.inputs;
        empty.budget = job.budget;
        empty.valid = true;
        return empty;
    }
    region = {region.x - kTileSize, region.y - kTileSize, region.width + 2 * kTileSize,
              region.height + 2 * kTileSize};

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
            const Rgba pixel = marks.pixel(region.x + x * step, region.y + y * step);
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
    built.solved = region;
    built.step = step;
    built.inputs = job.inputs;
    built.budget = job.budget;
    built.valid = true;
    built.inherited = job.inherited;
    built.carried_by = warp;
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
    // The marks travel with the fill, already carried, so that reading a fill
    // needs nothing but the fill. Copying a grid copies handles, and a run of
    // drawings inheriting one scribble cel and standing still shares one set of
    // pixels.
    built.marks = marks;
    built.marks_drawn = drawnBounds(marks);
    built.mark_threshold = job.settings.scribble_alpha_threshold;
    built.palette_colours.reserve(palette.size());
    for (const std::uint32_t key : palette) built.palette_colours.push_back(scribbleColour(key));
    built.palette = std::move(palette);
    // Narrowed to two bytes on the way in, one label at a time and with the
    // cast written down. See CtgFill::labels for why 32767 colours is not a cap
    // anybody can reach.
    //
    // Not `assign` from the solver's vector<int>, which narrows *inside* the
    // standard library: the compiler that minds then reports its own header as
    // the error and names this line only as the instantiation that reached it.
    // The conversion is the same one either way; the difference is whether it
    // is written here or inferred there.
    built.labels.reserve(solved.labels.size());
    for (const int label : solved.labels) {
        built.labels.push_back(static_cast<std::int16_t>(label));
    }
    built.outside_is_clear = ringIsClear(solved.labels, problem.width, problem.height);
    return built;
}

}  // namespace animage
