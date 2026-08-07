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
    std::vector<int> column;            // output column the sample mostly lands in
    std::vector<float> first_share;     // its weight there
    std::vector<float> second_share;    // and in the next column, 0 if it does not reach
    std::vector<float> column_weight;   // total weight arriving in each column
};

ColumnPlan planColumns(const SampleStep& step, const PixelRect& region, int stride,
                       int columns) {
    ColumnPlan plan;
    plan.origin = firstLatticePointAtOrAfter(region.x, stride);
    plan.column_weight.assign(static_cast<std::size_t>(columns), 0.0f);

    const int x_end = region.x + region.width;
    const long long first_column = step.entryAt(region.x);
    const int count =
        (x_end > plan.origin) ? (x_end - plan.origin + stride - 1) / stride : 0;
    plan.column.resize(static_cast<std::size_t>(count));
    plan.first_share.assign(static_cast<std::size_t>(count), 0.0f);
    plan.second_share.assign(static_cast<std::size_t>(count), 0.0f);

    for (int j = 0; j < count; ++j) {
        const int at = plan.origin + j * stride;
        // The first sample's block is widened back to the edge of the region,
        // so that the samples tile it exactly however the lattice falls.
        const int from = (j == 0) ? region.x : at;
        const int to = std::min(x_end, at + stride);

        long long lower = static_cast<long long>(from) << SampleStep::kFractionBits;
        const long long upper = static_cast<long long>(to) << SampleStep::kFractionBits;
        const int column = static_cast<int>(step.entryAt(from) - first_column);
        plan.column[static_cast<std::size_t>(j)] = column;

        for (int k = 0; k < 2 && lower < upper; ++k) {
            // The last entry can end a fraction of a pixel before the region
            // does; anything past it stays in the column that is there rather
            // than being dropped or written off the end.
            const bool last = column + k + 1 >= columns;
            const long long edge =
                last ? upper : std::min(upper, step.entryTop(first_column + column + k + 1));
            const float weight =
                static_cast<float>(edge - lower) / static_cast<float>(SampleStep::kOne);
            const int lands_in = (k == 0 || last) ? column : column + 1;

            if (lands_in == column) {
                plan.first_share[static_cast<std::size_t>(j)] += weight;
            } else {
                plan.second_share[static_cast<std::size_t>(j)] += weight;
            }
            plan.column_weight[static_cast<std::size_t>(lands_in)] += weight;
            lower = edge;
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
void blendLayerRowsBoxed(const TileGrid& grid, const Layer& layer, const PixelRect& region,
                         const SampleStep& step, long long first_row, int stride,
                         const ColumnPlan& plan, int y_begin, int y_end, Framebuffer& out,
                         std::vector<Rgba>& accumulator) {
    const float layer_opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
    if (layer_opacity <= 0.0f) return;
    if (grid.empty()) return;

    const int columns = out.width();
    const int x_end = region.x + region.width;
    const int y_bottom = region.y + region.height;
    const int first_x = plan.origin;
    const int first_y = firstLatticePointAtOrAfter(region.y, stride);

    for (int y = y_begin; y < y_end; ++y) {
        const long long top = step.entryTop(first_row + y);
        const long long bottom = step.entryTop(first_row + y + 1);

        std::fill(accumulator.begin(), accumulator.end(), Rgba{});
        float rows_weight = 0.0f;

        // The rows whose blocks reach into this entry: one before the entry's
        // own first row can overlap it, and one after, which is the y half of
        // the same split the plan does across columns.
        const int row_start = std::max(
            first_y, firstLatticePointAtOrAfter(
                         static_cast<int>(top >> SampleStep::kFractionBits) - stride + 1,
                         stride));
        for (int image_y = row_start; image_y < y_bottom; image_y += stride) {
            const int from = (image_y == first_y) ? region.y : image_y;
            const int to = std::min(y_bottom, image_y + stride);
            const long long lower =
                std::max(static_cast<long long>(from) << SampleStep::kFractionBits, top);
            const long long upper =
                std::min(static_cast<long long>(to) << SampleStep::kFractionBits, bottom);
            if (upper <= lower) {
                if ((static_cast<long long>(from) << SampleStep::kFractionBits) >= bottom) break;
                continue;
            }
            const float row_weight =
                static_cast<float>(upper - lower) / static_cast<float>(SampleStep::kOne);
            rows_weight += row_weight;

            const int tile_y = tileCoordFor(0, image_y).y;
            const int local_y = tileLocal(image_y);

            int image_x = first_x;
            while (image_x < x_end) {
                const int tile_x = tileCoordFor(image_x, 0).x;
                const int local_x = tileLocal(image_x);
                const int tile_end = std::min(x_end, (tile_x + 1) * kTileSize);
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
    return std::max(1, static_cast<int>(std::ceil(step.ratio() / kMaxBoxSamplesPerAxis)));
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
    pixels_.assign(static_cast<std::size_t>(width_) * height_, Rgba{});
}

void Framebuffer::clear() { std::fill(pixels_.begin(), pixels_.end(), Rgba{}); }

void Compositor::composite(const Document& doc, TrackId track_id, ImageId image_id,
                           const PixelRect& region, Framebuffer& out, SampleStep step) const {
    const Track* track = doc.scene().findTrack(track_id);
    if (!track) {
        out.clear();
        return;
    }

    std::vector<LayerId> layers;
    layers.reserve(track->layers.size());
    for (const Layer& layer : track->layers) layers.push_back(layer.id);

    compositeLayers(doc, track_id, image_id, layers, region, out, step);
}

// Resolving layer ids to pixels and properties. The only part of compositing
// that reads the document at all, which is what makes everything below it
// usable from a thread that must not.
static void collectPasses(const Document& doc, TrackId track_id, ImageId image_id,
                          const std::vector<LayerId>& layers, std::vector<LayerPass>& passes) {
    const Track* track = doc.scene().findTrack(track_id);
    const Image* image = track ? track->findImage(image_id) : nullptr;
    if (!image) return;

    passes.reserve(passes.size() + layers.size());
    for (auto it = layers.begin(); it != layers.end(); ++it) {
        const Layer* layer = track->findLayer(*it);
        if (!layer || !layer->visible) continue;

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
                // Only marks that were carried here. A drawing's own marks are
                // already where they are, and moving them again would move the
                // stroke you are making: a scribble in progress is shown
                // through this path, so a shift left over from before the
                // drawing took its marks over put the pen's own line half a
                // screen away from the pen.
                ImageId from = kNoId;
                const Cel* scribbles = doc.ctgScribblesAt(track_id, image_id, *it, &from);
                if (scribbles) {
                    const CtgShift offset =
                        (from == image_id) ? CtgShift{} : doc.ctgShiftAt(image_id, *it);
                    passes.push_back({&scribbles->tiles(), layer, offset});
                }
            } else if (const CtgFill* fill = doc.ctgFillFor(track_id, image_id, *it)) {
                passes.push_back({&fill->tiles, layer});
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
                                 Framebuffer& out, SampleStep step) const {
    std::vector<LayerPass> passes;
    collectPasses(doc, track_id, image_id, layers, passes);

    // An image that is not there is an empty picture and not a missing one, and
    // compositeGrids says so by clearing -- but it has to be told the size, and
    // an early return here would leave the caller's buffer as it found it.
    compositeGrids(passes, region, out, step);
}

void Compositor::compositeScene(const Document& doc, std::size_t slot, const PixelRect& region,
                                Framebuffer& out, SampleStep step) const {
    std::vector<LayerPass> passes;

    // Topmost first, which is the order compositeGrids wants and the order the
    // tracks are already in: index 0 composites on top.
    for (const Track& track : doc.scene().tracks) {
        const ImageId image = track.imageAtSlot(slot);
        if (image == kNoId) continue;

        std::vector<LayerId> layers;
        layers.reserve(track.layers.size());
        for (const Layer& layer : track.layers) layers.push_back(layer.id);
        collectPasses(doc, track.id, image, layers, passes);
    }

    compositeGrids(passes, region, out, step);
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
        reducing ? planColumns(step, region, stride, out.width()) : ColumnPlan{};

    // A layer drawn away from where its pixels are stored is read from a region
    // moved the other way and written to the same columns, which is the whole
    // of the offset. Worked out per pass and only when there is one, so every
    // other layer takes exactly the path it took before this existed.
    const auto run_band = [&](int y_begin, int y_end) {
        std::vector<Rgba> accumulator;
        if (reducing) accumulator.resize(static_cast<std::size_t>(out.width()));
        for (const LayerPass& pass : passes) {
            const bool moved = !pass.offset.isZero();
            const PixelRect from = moved ? PixelRect{region.x - pass.offset.x,
                                                     region.y - pass.offset.y, region.width,
                                                     region.height}
                                         : region;
            if (reducing) {
                const long long rows_from = moved ? step.entryAt(from.y) : first_row;
                const ColumnPlan moved_plan =
                    moved ? planColumns(step, from, stride, out.width()) : ColumnPlan{};
                blendLayerRowsBoxed(*pass.tiles, *pass.layer, from, step, rows_from, stride,
                                    moved ? moved_plan : plan, y_begin, y_end, out,
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
