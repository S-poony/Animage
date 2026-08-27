// SPDX-License-Identifier: GPL-3.0-or-later
#include "compositor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

#include "color.h"

namespace animage {
namespace {

// Rounding towards negative infinity, which is what a grid anchored at the
// image origin needs: an image coordinate left of or above the origin belongs
// to the entry below it, not to the one truncation would name.
long long floorDiv(long long a, long long b) {
    const long long quotient = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? quotient - 1 : quotient;
}

long long ceilDiv(long long a, long long b) { return -floorDiv(-a, b); }

// The lattice a block is read on, in absolute image coordinates so that it does
// not shift when the region does.
int firstLatticePointAtOrAfter(int from, int stride) {
    return static_cast<int>(ceilDiv(from, stride) * stride);
}

int latticePointsIn(int from, int to, int stride) {
    if (to <= from) return 0;
    return static_cast<int>(floorDiv(to - 1, stride) - floorDiv(from - 1, stride));
}

// At most about this many image pixels are read per axis inside one entry. A
// full box reads every pixel under the entry, which at 5% zoom is four hundred
// of them for one entry, so the block is read on a lattice instead and each
// sample stands for the stride it skips -- which is what the weighting below is
// computed against, so the filter stays right, just coarser.
//
// Three, measured rather than picked. It is the smallest that leaves the
// lattice at every pixel for the whole band the issue is about: the stride only
// leaves 1 above three image pixels an entry, which is 33% zoom, and 40-60% is
// where the shimmer was reported. Two put the first coarsening at 50%, right in
// the middle of it, and cost 5 RMS at 40%; four bought nothing below and
// doubled what a 25% zoom costs to composite.
constexpr int kMaxBoxSamplesPerAxis = 3;

// One layer over the rows [y_begin, y_end) of what is already in the
// framebuffer, at one entry per image pixel. Both sides are premultiplied, so
// this is a multiply-add and nothing else.
//
// The alpha byte pair is tested before anything is decoded. Line art is mostly
// empty, and a fully transparent premultiplied pixel contributes nothing, so
// skipping on a single 16-bit compare avoids four table lookups and a blend for
// the large majority of pixels.
void blendLayerRows(const TileGrid& grid, const Layer& layer, const PixelRect& region,
                    int y_begin, int y_end, Framebuffer& out) {
    const float layer_opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
    if (layer_opacity <= 0.0f) return;
    if (grid.empty()) return;

    const bool faded = layer_opacity < 1.0f;

    for (int y = y_begin; y < y_end; ++y) {
        const int image_y = region.y + y;
        Rgba* destination = out.row(y);

        // The tile row does not change across a scanline, so the lookup is
        // hoisted and only repeated when the column crosses a tile boundary.
        const int tile_y = tileCoordFor(0, image_y).y;
        const int local_y = tileLocal(image_y);

        int x = 0;
        while (x < out.width()) {
            const int image_x = region.x + x;
            const int tile_x = tileCoordFor(image_x, 0).x;
            const int local_x = tileLocal(image_x);
            const int run = std::min(out.width() - x, kTileSize - local_x);

            // findSlot borrows the handle. find() would copy the shared_ptr,
            // and an atomic increment per tile lookup is not free at this rate.
            const TileRef* held = grid.findSlot({tile_x, tile_y});
            if (!held || !*held) {
                x += run;
                continue;  // absent tile is transparent, so nothing to blend
            }

            const Half* row =
                (*held)->rgba.data() + static_cast<std::size_t>(local_y) * kTileSize * 4;

            for (int i = 0; i < run; ++i) {
                const Half* p = row + static_cast<std::size_t>(local_x + i) * 4;
                if (p[3].bits == 0) continue;  // nothing here

                Rgba source{p[0].toFloat(), p[1].toFloat(), p[2].toFloat(), p[3].toFloat()};
                if (faded) {
                    source.r *= layer_opacity;
                    source.g *= layer_opacity;
                    source.b *= layer_opacity;
                    source.a *= layer_opacity;
                }
                destination[x + i] = over(source, destination[x + i]);
            }
            x += run;
        }
    }
}

// The same, for a source that is an answer rather than a picture.
//
// A colour layer's fill has no tiles to walk, so a scratch row is filled from
// the accessor and then blended -- and the accessor is where the run-length
// lives, since a run of image pixels inside one solved cell is one colour. The
// arithmetic below is blendLayerRows', layer opacity included.
void blendFillRows(const CtgFill& fill, const Layer& layer, const PixelRect& region,
                   int y_begin, int y_end, Framebuffer& out, std::vector<Rgba>& scratch) {
    const float layer_opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
    if (layer_opacity <= 0.0f) return;

    const bool faded = layer_opacity < 1.0f;
    const int columns = out.width();
    if (columns <= 0) return;
    scratch.resize(static_cast<std::size_t>(columns));

    for (int y = y_begin; y < y_end; ++y) {
        // Only the part of the row that can hold an answer: the rest is
        // transparent by construction, and skipping it is what an absent tile
        // does for a grid.
        const CtgFillExtent extent = ctgFillExtent(fill, region.y + y, region.x, 1, columns);
        if (extent.count <= 0) continue;
        ctgFillSpan(fill, region.y + y, region.x + extent.first, 1, extent.count,
                    scratch.data());
        Rgba* destination = out.row(y);

        for (int i = 0; i < extent.count; ++i) {
            const int x = extent.first + i;
            Rgba source = scratch[static_cast<std::size_t>(i)];
            if (source.a <= 0.0f) continue;  // nothing here
            if (faded) {
                source.r *= layer_opacity;
                source.g *= layer_opacity;
                source.b *= layer_opacity;
                source.a *= layer_opacity;
            }
            destination[x] = over(source, destination[x]);
        }
    }
}

// Which output columns each sample along a scanline lands in, and how much of
// it each one gets. The same for every row and every layer, so it is worked out
// once and then read.
//
// A sample covers `stride` image pixels and an entry covers `ratio` of them,
// neither of which need line up, so a sample lands in one column or is split
// between two. Splitting it is the whole difference between a filter and a
// staircase: with the boundaries rounded to whole pixels instead, at 1.4 image
// pixels an entry some entries average two pixels and some take one, and the
// alternation between them is a worse artefact than the shimmer being removed.
// Measured -- RMS 10.7 against a curve drawn at display size, where weighting
// the split gives 2.4.
struct ColumnPlan {
    int origin = 0;                     // image x of the first sample
    int end = 0;                        // one past the last sample's block
    std::vector<int> column;            // output column the sample mostly lands in
    std::vector<float> first_share;     // its weight there
    std::vector<float> second_share;    // and in the next column, 0 if it does not reach
    std::vector<float> column_weight;   // total weight arriving in each column
};

// `first_read_x` is where the samples are *read* from and `shift` is how far the
// layer is drawn from there, so `read + shift` is where a sample lands. The
// columns are worked out in that drawn space and not in the read space.
//
// They used to be worked out in the read space, and the two are not the same
// grid: the sample lattice is anchored at the image origin, so a rectangle of
// the same width at a different phase can span one more entry than the output
// has columns. entryAt floors, so `entryAt(p) - entryAt(region.x - shift)` and
// `entryAt(p + shift) - entryAt(region.x)` differ by one at some phases -- and
// the first was being used to index buffers sized by the second. That is a write
// one past the end of both the accumulator and the weights, on every row of
// every repaint, for a colour layer showing carried marks at any zoom below
// 100%. Computing it here in the drawn space bounds `column` to [0, columns) by
// construction rather than by a check that can be forgotten.
//
// **The samples are the ones the entries want, not the ones the region holds.**
// That is issue #64. The plan used to widen its first sample's block back to
// the edge of the region and clip its last one to the far edge, and normalise by
// the weight that produced -- so an entry on the boundary of the rectangle it
// was asked for was averaged from a different set of image pixels than the same
// entry got inside a larger rectangle. The canvas refreshes one dirty rectangle
// per dab while the pen is down and the whole cache when it lifts, so every dab
// left a line of wrong entries around itself that the lift then wiped: dragging
// the eraser past a stroke without touching it visibly chewed the stroke until
// you let go. A pan left the same thing along the strips it exposed, and nothing
// wiped that at all.
//
// So a block that reaches past either end is read past either end -- an entry is
// the average of its whole block wherever it is asked about -- and whatever
// lands in an entry nobody asked for is dropped rather than folded into the one
// that is there. `origin` and `end` are the samples the *entries* need, which is
// why they are worked out from the entry span and not from a rectangle.
ColumnPlan planColumns(const SampleStep& step, int first_read_x, int stride, int columns,
                       int shift) {
    ColumnPlan plan;
    if (columns <= 0) return plan;
    plan.column_weight.assign(static_cast<std::size_t>(columns), 0.0f);

    const long long first_column = step.entryAt(first_read_x + shift);

    // The span the asked-for columns cover, brought back into read space.
    const long long carried = static_cast<long long>(shift) << SampleStep::kFractionBits;
    const long long span_begin = step.entryTop(first_column) - carried;
    const long long span_end = step.entryTop(first_column + columns) - carried;

    // The block holding the first pixel the span touches, and one past the last
    // block that starts inside it. A block is [p, p + stride) on a lattice
    // anchored at the image origin, so this is a property of the span alone.
    const int first_pixel = static_cast<int>(floorDiv(span_begin, SampleStep::kOne));
    plan.origin = firstLatticePointAtOrAfter(first_pixel - stride + 1, stride);
    const int count = latticePointsIn(
        plan.origin, static_cast<int>(ceilDiv(span_end, SampleStep::kOne)), stride);
    plan.end = plan.origin + count * stride;

    plan.column.assign(static_cast<std::size_t>(count), 0);
    plan.first_share.assign(static_cast<std::size_t>(count), 0.0f);
    plan.second_share.assign(static_cast<std::size_t>(count), 0.0f);

    for (int j = 0; j < count; ++j) {
        const int at = plan.origin + j * stride;

        // In drawn coordinates, which is what the columns are counted in.
        const long long lower = static_cast<long long>(at + shift) << SampleStep::kFractionBits;
        const long long upper =
            static_cast<long long>(at + stride + shift) << SampleStep::kFractionBits;
        const int column = static_cast<int>(step.entryAt(at + shift) - first_column);

        // Two entries and never three: `boxSampleStride` keeps a block no longer
        // than an entry, so a block that starts in one entry ends in it or in
        // the next.
        const long long edge = std::min(upper, step.entryTop(first_column + column + 1));
        const float here = static_cast<float>(edge - lower) / static_cast<float>(SampleStep::kOne);
        const float next = static_cast<float>(upper - edge) / static_cast<float>(SampleStep::kOne);

        // Where the consumers index from. `column` is clamped into range so the
        // scatter needs no test of its own; the shares carry which parts of the
        // block are actually wanted, and a part that fell outside is zero.
        const bool first_wanted = column >= 0 && column < columns;
        const bool next_wanted = column + 1 >= 0 && column + 1 < columns;
        plan.column[static_cast<std::size_t>(j)] = std::clamp(column, 0, columns - 1);
        if (first_wanted) {
            plan.first_share[static_cast<std::size_t>(j)] = here;
            plan.column_weight[static_cast<std::size_t>(column)] += here;
        }
        if (next_wanted && next > 0.0f) {
            // Held as the share of the column after `plan.column`, so a block
            // whose first entry was not wanted puts its remainder in column 0
            // through `first_share` instead.
            if (first_wanted) {
                plan.second_share[static_cast<std::size_t>(j)] = next;
            } else {
                plan.first_share[static_cast<std::size_t>(j)] = next;
            }
            plan.column_weight[static_cast<std::size_t>(column + 1)] += next;
        }
    }
    return plan;
}

// The same as blendLayerRows, reducing a block of image pixels to each entry
// instead of taking one of them.
//
// The accumulation runs a whole output row at a time rather than an entry at a
// time, and that is not an implementation detail: it is what keeps the tile
// lookup hoisted. A block can straddle a tile boundary in either direction, so
// an entry-at-a-time loop would look a tile up per entry -- which is precisely
// what made zooming out crawl before the hoist existed. Walking image rows and
// scattering into an accumulator per output column keeps one lookup per tile
// crossing, exactly as at full resolution.
//
// Averaging premultiplied linear samples and compositing the average is not
// quite compositing every sample and averaging the result; the two part company
// where two layers overlap inside one block. It is the trade a mipmap makes,
// and the alternative is flattening at full resolution, which is the cost the
// step exists to avoid.
//
// **It takes no region.** That is the fix and not a tidy-up: an entry has to come
// out the same whatever rectangle it was asked for, and the surest way to say so
// is to leave the rectangle out of the arithmetic. What is left -- the entry
// span, the lattice, and the plan -- is the same for a dab's dirty rectangle as
// for the whole cache. See planColumns for what was going wrong.
// `first_row` counts the *drawn* rows and `row_shift` is how far the layer is
// drawn from where its pixels are, exactly as the plan does across columns. The
// rows used to be re-anchored instead -- `entryAt(region.y - offset.y)`, the
// read row of the region's corner, indexed as though it were the drawn one --
// and that is the row half of the mistake planColumns describes: entryAt floors,
// so `entryAt(band.y - offset.y) - entryAt(whole.y - offset.y)` and
// `entryAt(band.y) - entryAt(whole.y)` differ by one at some phases. A carried
// mark therefore came out a whole entry up or down depending on which rectangle
// was being refreshed, which is the same fault this function exists to remove,
// reaching the one pass that has an offset.
void blendLayerRowsBoxed(const TileGrid& grid, const Layer& layer, const SampleStep& step,
                         long long first_row, int row_shift, int stride, const ColumnPlan& plan,
                         int y_begin, int y_end, Framebuffer& out,
                         std::vector<Rgba>& accumulator) {
    const float layer_opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
    if (layer_opacity <= 0.0f) return;
    if (grid.empty()) return;

    const int columns = out.width();
    const int first_x = plan.origin;
    const int sample_end = plan.end;
    // The drawn span brought back to where the pixels are read from.
    const long long carried = static_cast<long long>(row_shift) << SampleStep::kFractionBits;

    for (int y = y_begin; y < y_end; ++y) {
        const long long top = step.entryTop(first_row + y) - carried;
        const long long bottom = step.entryTop(first_row + y + 1) - carried;

        std::fill(accumulator.begin(), accumulator.end(), Rgba{});
        float rows_weight = 0.0f;

        // The rows whose blocks reach into this entry: one before the entry's
        // own first row can overlap it, and one after, which is the y half of
        // the same split the plan does across columns.
        const int row_start = firstLatticePointAtOrAfter(
            static_cast<int>(top >> SampleStep::kFractionBits) - stride + 1, stride);
        for (int image_y = row_start;
             (static_cast<long long>(image_y) << SampleStep::kFractionBits) < bottom;
             image_y += stride) {
            const long long lower =
                std::max(static_cast<long long>(image_y) << SampleStep::kFractionBits, top);
            const long long upper =
                std::min(static_cast<long long>(image_y + stride) << SampleStep::kFractionBits,
                         bottom);
            if (upper <= lower) continue;
            const float row_weight =
                static_cast<float>(upper - lower) / static_cast<float>(SampleStep::kOne);
            rows_weight += row_weight;

            const int tile_y = tileCoordFor(0, image_y).y;
            const int local_y = tileLocal(image_y);

            int image_x = first_x;
            while (image_x < sample_end) {
                const int tile_x = tileCoordFor(image_x, 0).x;
                const int local_x = tileLocal(image_x);
                const int tile_end = std::min(sample_end, (tile_x + 1) * kTileSize);
                const int run = latticePointsIn(image_x, tile_end, stride);

                const TileRef* held = grid.findSlot({tile_x, tile_y});
                if (!held || !*held) {
                    image_x += run * stride;
                    continue;  // absent tile is transparent, so nothing to add
                }

                const Half* row =
                    (*held)->rgba.data() + static_cast<std::size_t>(local_y) * kTileSize * 4;

                int sample = (image_x - first_x) / stride;
                for (int i = 0; i < run; ++i, ++sample) {
                    const Half* p = row + static_cast<std::size_t>(local_x + i * stride) * 4;
                    if (p[3].bits == 0) continue;  // nothing here

                    const auto index = static_cast<std::size_t>(sample);
                    const float red = p[0].toFloat();
                    const float green = p[1].toFloat();
                    const float blue = p[2].toFloat();
                    const float alpha = p[3].toFloat();

                    const int column = plan.column[index];
                    const float here = plan.first_share[index] * row_weight;
                    Rgba& sum = accumulator[static_cast<std::size_t>(column)];
                    sum.r += red * here;
                    sum.g += green * here;
                    sum.b += blue * here;
                    sum.a += alpha * here;

                    const float spill = plan.second_share[index];
                    if (spill <= 0.0f || column + 1 >= columns) continue;
                    const float next = spill * row_weight;
                    Rgba& over_the_edge = accumulator[static_cast<std::size_t>(column) + 1];
                    over_the_edge.r += red * next;
                    over_the_edge.g += green * next;
                    over_the_edge.b += blue * next;
                    over_the_edge.a += alpha * next;
                }
                image_x += run * stride;
            }
        }

        if (rows_weight <= 0.0f) continue;
        Rgba* destination = out.row(y);
        for (int x = 0; x < columns; ++x) {
            const Rgba& sum = accumulator[static_cast<std::size_t>(x)];
            if (sum.a <= 0.0f) continue;  // the whole block was empty
            const float covered = plan.column_weight[static_cast<std::size_t>(x)] * rows_weight;
            if (covered <= 0.0f) continue;

            // The divisor is the whole block, including whatever part of it was
            // skipped for being transparent: a line crossing a quarter of the
            // block should come out a quarter covered.
            const float scale = layer_opacity / covered;
            const Rgba source{sum.r * scale, sum.g * scale, sum.b * scale, sum.a * scale};
            destination[x] = over(source, destination[x]);
        }
    }
}

// And the reducing twin, for a fill.
//
// blendLayerRowsBoxed with the tile walk replaced by one call per image row:
// the accessor fills a row of samples on the same lattice, and the scatter into
// the accumulator, the weights and the division are unchanged. A fill carries
// no offset, so the plan it is handed is the ordinary one.
void blendFillRowsBoxed(const CtgFill& fill, const Layer& layer, const SampleStep& step,
                        long long first_row, int stride, const ColumnPlan& plan, int y_begin,
                        int y_end, Framebuffer& out, std::vector<Rgba>& accumulator,
                        std::vector<Rgba>& scratch) {
    const float layer_opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
    if (layer_opacity <= 0.0f) return;

    const int columns = out.width();
    const int first_x = plan.origin;
    const int samples = static_cast<int>(plan.column.size());
    if (samples <= 0) return;
    scratch.resize(static_cast<std::size_t>(samples));

    for (int y = y_begin; y < y_end; ++y) {
        const long long top = step.entryTop(first_row + y);
        const long long bottom = step.entryTop(first_row + y + 1);

        std::fill(accumulator.begin(), accumulator.end(), Rgba{});
        float rows_weight = 0.0f;

        const int row_start = firstLatticePointAtOrAfter(
            static_cast<int>(top >> SampleStep::kFractionBits) - stride + 1, stride);
        for (int image_y = row_start;
             (static_cast<long long>(image_y) << SampleStep::kFractionBits) < bottom;
             image_y += stride) {
            const long long lower =
                std::max(static_cast<long long>(image_y) << SampleStep::kFractionBits, top);
            const long long upper =
                std::min(static_cast<long long>(image_y + stride) << SampleStep::kFractionBits,
                         bottom);
            if (upper <= lower) continue;
            const float row_weight =
                static_cast<float>(upper - lower) / static_cast<float>(SampleStep::kOne);
            rows_weight += row_weight;

            const CtgFillExtent extent = ctgFillExtent(fill, image_y, first_x, stride, samples);
            if (extent.count <= 0) continue;
            ctgFillSpan(fill, image_y, first_x + extent.first * stride, stride, extent.count,
                        scratch.data());

            for (int j = 0; j < extent.count; ++j) {
                const auto index = static_cast<std::size_t>(extent.first + j);
                const Rgba& source = scratch[static_cast<std::size_t>(j)];
                if (source.a <= 0.0f) continue;  // nothing here

                const int column = plan.column[index];
                const float here = plan.first_share[index] * row_weight;
                Rgba& sum = accumulator[static_cast<std::size_t>(column)];
                sum.r += source.r * here;
                sum.g += source.g * here;
                sum.b += source.b * here;
                sum.a += source.a * here;

                const float spill = plan.second_share[index];
                if (spill <= 0.0f || column + 1 >= columns) continue;
                const float next = spill * row_weight;
                Rgba& over_the_edge = accumulator[static_cast<std::size_t>(column) + 1];
                over_the_edge.r += source.r * next;
                over_the_edge.g += source.g * next;
                over_the_edge.b += source.b * next;
                over_the_edge.a += source.a * next;
            }
        }

        if (rows_weight <= 0.0f) continue;
        Rgba* destination = out.row(y);
        for (int x = 0; x < columns; ++x) {
            const Rgba& sum = accumulator[static_cast<std::size_t>(x)];
            if (sum.a <= 0.0f) continue;  // the whole block was empty
            const float covered = plan.column_weight[static_cast<std::size_t>(x)] * rows_weight;
            if (covered <= 0.0f) continue;

            const float scale = layer_opacity / covered;
            const Rgba source{sum.r * scale, sum.g * scale, sum.b * scale, sum.a * scale};
            destination[x] = over(source, destination[x]);
        }
    }
}

}  // namespace

SampleStep SampleStep::fromRatio(double image_pixels_per_entry) {
    if (!(image_pixels_per_entry > 1.0)) return SampleStep{};
    const double raw = std::floor(image_pixels_per_entry * static_cast<double>(kOne));
    // Well past any plausible zoom, and short of anything that could overflow
    // the 64-bit products the grid arithmetic forms.
    constexpr double kCeiling = 1024.0 * static_cast<double>(kOne);
    return SampleStep{static_cast<std::int64_t>(std::min(raw, kCeiling))};
}

int SampleStep::entryBegin(long long entry) const {
    // The first *whole* image pixel inside the entry. An entry begins partway
    // through a pixel more often than not, and that fraction belongs to the
    // entry before it -- which is what the weights in the reduction are for.
    return static_cast<int>(ceilDiv(entryTop(entry), kOne));
}

long long SampleStep::entryAt(int image_coordinate) const {
    return floorDiv(static_cast<long long>(image_coordinate) << kFractionBits, raw_);
}

int SampleStep::entriesAcross(int origin, int extent) const {
    if (extent <= 0) return 0;
    return static_cast<int>(entryAt(origin + extent - 1) - entryAt(origin) + 1);
}

PixelRect snapToSampleGrid(const SampleStep& step, const PixelRect& region) {
    if (region.isEmpty()) return {};
    const int x0 = step.entryBegin(step.entryAt(region.x));
    const int y0 = step.entryBegin(step.entryAt(region.y));
    const int x1 = step.entryBegin(step.entryAt(region.x + region.width - 1) + 1);
    const int y1 = step.entryBegin(step.entryAt(region.y + region.height - 1) + 1);
    return {x0, y0, x1 - x0, y1 - y0};
}

int boxSampleStride(const SampleStep& step) {
    const int budget =
        std::max(1, static_cast<int>(std::ceil(step.ratio() / kMaxBoxSamplesPerAxis)));

    // Never longer than an entry. The reduction puts each block in the entry it
    // starts in and in the one after, which is exact for as long as a block
    // cannot reach a third -- and with the sample budget at three that is true
    // of every ratio, so this clamp does nothing today. It is here because the
    // arithmetic that makes it true lives in one constant and the code that
    // depends on it lives in three loops, and a block that reached a third entry
    // would not fail: it would quietly drop the weight that fell there.
    const int longest = std::max(1, static_cast<int>(std::floor(step.ratio())));
    return std::min(budget, longest);
}

namespace {

// Threads cost about as much to start as a small band costs to composite, so
// only spread work that is worth spreading.
int chooseWorkerCount(int rows, std::size_t layers) {
    const long long work = static_cast<long long>(rows) * static_cast<long long>(layers);
    if (work < 512) return 1;
    const unsigned hardware = std::thread::hardware_concurrency();
    const int available = static_cast<int>(hardware ? hardware : 1u);
    return std::clamp(std::min(available, rows / 32), 1, 8);
}

}  // namespace

void Framebuffer::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.resize(static_cast<std::size_t>(width_) * height_);
}

void Framebuffer::resizeCleared(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.assign(static_cast<std::size_t>(width_) * height_, Rgba{});
}

void Framebuffer::clear() { std::fill(pixels_.begin(), pixels_.end(), Rgba{}); }

// swap with an empty vector rather than shrink_to_fit, which is a non-binding
// request the standard lets an implementation ignore.
void Framebuffer::release() {
    width_ = 0;
    height_ = 0;
    std::vector<Rgba>().swap(pixels_);
}

void Compositor::composite(const Document& doc, TrackId track_id, ImageId image_id,
                           const PixelRect& region, Framebuffer& out, SampleStep step,
                           const SubstitutedLayer& substituted) const {
    const Track* track = doc.scene().findTrack(track_id);
    if (!track) {
        out.clear();
        return;
    }

    std::vector<LayerId> layers;
    layers.reserve(track->layers.size());
    for (const Layer& layer : track->layers) layers.push_back(layer.id);

    compositeLayers(doc, track_id, image_id, layers, region, out, step, substituted);
}

// Resolving layer ids to pixels and properties. The only part of compositing
// that reads the document at all, which is what makes everything below it
// usable from a thread that must not.
static void collectPasses(const Document& doc, TrackId track_id, ImageId image_id,
                          const std::vector<LayerId>& layers, std::vector<LayerPass>& passes,
                          const SubstitutedLayer& substituted = {}) {
    const Track* track = doc.scene().findTrack(track_id);
    const Image* image = track ? track->findImage(image_id) : nullptr;
    if (!image) return;

    passes.reserve(passes.size() + layers.size());
    for (auto it = layers.begin(); it != layers.end(); ++it) {
        const Layer* layer = track->findLayer(*it);
        if (!layer || !layer->visible) continue;

        // Stood in for, in its own place in the stack rather than over the top
        // of it: the caller is holding this layer's pixels for the moment.
        if (*it == substituted.layer) {
            if (substituted.tiles) passes.push_back({substituted.tiles, layer});
            continue;
        }

        // A CTG layer shows its regenerated fill, never the scribbles that
        // produced it. If no fill has been built yet the layer simply does not
        // draw -- compositing is not the place to start a max-flow.
        //
        // Showing the scribbles instead reads them through the resolver rather
        // than off this image, because a drawing with none of its own is
        // showing an earlier one's and that is exactly what you are asking to
        // look at.
        if (layer->kind == LayerKind::Ctg) {
            if (layer->show_scribbles) {
                // Shown where they were used, which on a layer that moves
                // carried marks is not where they were drawn. Otherwise the
                // Marks column says the fill beside it was built from marks
                // somewhere the fill says they are not -- and the one view
                // whose job is to show what the solver saw would be the one
                // view that does not.
                //
                // Which marks, and where, is Document::ctgCarriedMarksAt's
                // question and not this one's: the same answer is owed to the
                // first stroke on a carrying drawing, and the two disagreeing
                // is the bug that was reported.
                const Document::CarriedMarks carried =
                    doc.ctgCarriedMarksAt(track_id, image_id, *it);
                if (carried.tiles) {
                    passes.push_back({carried.tiles, layer, carried.offset});
                }
            } else if (const CtgFill* fill = doc.ctgFillFor(track_id, image_id, *it)) {
                passes.push_back({nullptr, layer, {}, fill});
            }
            continue;
        }

        // An imported picture, which has no cel and is derived from a file. The
        // same bargain as the fill above and for the same reason: if it has not
        // been decoded yet the layer simply does not draw, because compositing
        // is not the place to start a decode. What arrives is an ordinary
        // TileGrid, so everything below this function is untouched -- it cannot
        // tell a decoded frame from a drawn one, and must not have to.
        if (layer->kind == LayerKind::Reference) {
            // Asked for at the layer's placement, so a frame derived at an
            // earlier one is not drawn. What that costs is a blank layer for as
            // long as it takes to re-derive; what it saves is the picture
            // confidently showing where the import used to be.
            if (const TileGrid* frame =
                    doc.referenceFrameFor(track_id, image_id, *it, layer->placement)) {
                passes.push_back({frame, layer});
            }
            continue;
        }

        const Cel* cel = doc.cel(image->celFor(*it));
        if (!cel) continue;  // no cel means the layer is empty here
        passes.push_back({&cel->tiles(), layer});
    }
}

void Compositor::compositeLayers(const Document& doc, TrackId track_id, ImageId image_id,
                                 const std::vector<LayerId>& layers, const PixelRect& region,
                                 Framebuffer& out, SampleStep step,
                                 const SubstitutedLayer& substituted) const {
    std::vector<LayerPass> passes;
    collectPasses(doc, track_id, image_id, layers, passes, substituted);

    // An image that is not there is an empty picture and not a missing one, and
    // compositeGrids says so by clearing -- but it has to be told the size, and
    // an early return here would leave the caller's buffer as it found it.
    compositeGrids(passes, region, out, step);
}

std::vector<LayerPass> Compositor::scenePasses(const Document& doc, std::size_t slot,
                                              const SubstitutedLayer& substituted) const {
    std::vector<LayerPass> passes;

    // Topmost first, which is the order compositeGrids wants and the order the
    // tracks are already in: index 0 composites on top.
    for (const Track& track : doc.scene().tracks) {
        // What it shows, not what it holds: a track past its last drawing may
        // still be holding it or cycling, and this is the picture.
        const ImageId image = track.imageShownAt(slot);
        if (image == kNoId) continue;

        std::vector<LayerId> layers;
        layers.reserve(track.layers.size());
        for (const Layer& layer : track.layers) layers.push_back(layer.id);
        collectPasses(doc, track.id, image, layers, passes, substituted);
    }

    return passes;
}

void Compositor::compositeScene(const Document& doc, std::size_t slot, const PixelRect& region,
                                Framebuffer& out, SampleStep step,
                                const SubstitutedLayer& substituted) const {
    compositeGrids(scenePasses(doc, slot, substituted), region, out, step);
}

void Compositor::compositeGrids(const std::vector<LayerPass>& topmost_first,
                                const PixelRect& region, Framebuffer& out,
                                SampleStep step) const {
    out.resize(step.entriesAcross(region.x, region.width),
               step.entriesAcross(region.y, region.height));
    if (out.isEmpty()) return;
    out.clear();
    if (topmost_first.empty()) return;

    // Bottom upwards: each layer goes over the accumulated result, and the list
    // is topmost first.
    std::vector<LayerPass> passes(topmost_first.rbegin(), topmost_first.rend());

    // Split by rows rather than by layer: each band is independent, so no
    // synchronisation is needed anywhere, and a band does all of its layers
    // while its part of the framebuffer is still in cache.
    const int rows = out.height();
    const int workers = chooseWorkerCount(rows, passes.size());

    // One entry per image pixel keeps the direct loop: the block is a single
    // pixel, so an accumulator and a division would only be arithmetic spent
    // reproducing the sample. That is the path zooming in takes, which is where
    // most drawing happens.
    const bool reducing = !step.isOne();
    const int stride = reducing ? boxSampleStride(step) : 1;
    const long long first_row = step.entryAt(region.y);
    const ColumnPlan plan =
        reducing ? planColumns(step, region.x, stride, out.width(), 0) : ColumnPlan{};

    // A layer drawn away from where its pixels are stored is read from a region
    // moved the other way and written to the same columns, which is the whole
    // of the offset. Worked out per pass and only when there is one, so every
    // other layer takes exactly the path it took before this existed.
    const auto run_band = [&](int y_begin, int y_end) {
        std::vector<Rgba> accumulator;
        std::vector<Rgba> scratch;
        if (reducing) accumulator.resize(static_cast<std::size_t>(out.width()));
        for (const LayerPass& pass : passes) {
            // A fill is an answer rather than a picture, and is read a row at a
            // time. Every band reads the same fill at the same time, which is
            // why the accessor is a pure function of what the fill stores.
            if (pass.fill != nullptr) {
                if (reducing) {
                    blendFillRowsBoxed(*pass.fill, *pass.layer, step, first_row, stride, plan,
                                       y_begin, y_end, out, accumulator, scratch);
                } else {
                    blendFillRows(*pass.fill, *pass.layer, region, y_begin, y_end, out, scratch);
                }
                continue;
            }
            if (pass.tiles == nullptr) continue;

            const bool moved = !pass.offset.isZero();
            const PixelRect from = moved ? PixelRect{region.x - pass.offset.x,
                                                     region.y - pass.offset.y, region.width,
                                                     region.height}
                                         : region;
            if (reducing) {
                // Read from `from`, drawn `pass.offset` away from it -- which is
                // back onto `region`, and that is the grid both axes count in.
                // The rows say so with `pass.offset.y` and the columns with the
                // plan; neither re-anchors to the read corner.
                const ColumnPlan moved_plan =
                    moved ? planColumns(step, from.x, stride, out.width(), pass.offset.x)
                          : ColumnPlan{};
                blendLayerRowsBoxed(*pass.tiles, *pass.layer, step, first_row, pass.offset.y,
                                    stride, moved ? moved_plan : plan, y_begin, y_end, out,
                                    accumulator);
            } else {
                blendLayerRows(*pass.tiles, *pass.layer, from, y_begin, y_end, out);
            }
        }
    };

    if (workers <= 1) {
        run_band(0, rows);
        return;
    }

    const int band = (rows + workers - 1) / workers;
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(workers) - 1);
    for (int w = 1; w < workers; ++w) {
        const int y_begin = std::min(rows, w * band);
        const int y_end = std::min(rows, y_begin + band);
        if (y_begin >= y_end) break;
        pool.emplace_back(run_band, y_begin, y_end);
    }
    run_band(0, std::min(rows, band));  // this thread takes the first band
    for (std::thread& worker : pool) worker.join();
}

PixelRect imageBounds(const Document& doc, TrackId track_id, ImageId image_id) {
    const Track* track = doc.scene().findTrack(track_id);
    if (!track) return {};
    const Image* image = track->findImage(image_id);
    if (!image) return {};

    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();
    bool any = false;

    for (const Layer& layer : track->layers) {
        const Cel* cel = doc.cel(image->celFor(layer.id));
        if (!cel) continue;
        for (const TileCoord& coord : cel->tiles().coords()) {
            any = true;
            min_x = std::min(min_x, coord.x * kTileSize);
            min_y = std::min(min_y, coord.y * kTileSize);
            max_x = std::max(max_x, (coord.x + 1) * kTileSize);
            max_y = std::max(max_y, (coord.y + 1) * kTileSize);
        }
    }

    if (!any) return {};
    return {min_x, min_y, max_x - min_x, max_y - min_y};
}

}  // namespace animage
