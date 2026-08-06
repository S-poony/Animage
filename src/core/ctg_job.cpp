// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctg_job.h"

#include <algorithm>
#include <memory>
#include <unordered_map>

#include "compositor.h"

namespace animage {
namespace {

PixelRect uniteRects(const PixelRect& a, const PixelRect& b) {
    if (a.isEmpty()) return b;
    if (b.isEmpty()) return a;
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.width, b.x + b.width);
    const int y1 = std::max(a.y + a.height, b.y + b.height);
    return {x0, y0, x1 - x0, y1 - y0};
}

PixelRect intersectRects(const PixelRect& a, const PixelRect& b) {
    const int x0 = std::max(a.x, b.x);
    const int y0 = std::max(a.y, b.y);
    const int x1 = std::min(a.x + a.width, b.x + b.width);
    const int y1 = std::min(a.y + a.height, b.y + b.height);
    if (x1 <= x0 || y1 <= y0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

bool abandoned(const std::atomic<bool>* abandon) {
    return abandon != nullptr && abandon->load(std::memory_order_relaxed);
}

}  // namespace

// Where anything has actually been drawn, to the nearest tile.
//
// Emptied tiles are skipped, and that is not tidiness. Erasing a mark clears
// its pixels and leaves the tile in the grid, so bounds taken from tile
// coordinates alone go on describing a mark that is no longer there -- and this
// rectangle chooses the solve resolution and where the unseverable rim sits. A
// stray scribble out in a corner, erased, left the solve permanently coarser
// than it was before the scribble was ever made: draw, erase, and the drawing
// does not come back the way it was. Nothing said so, because the region is not
// something you can see.
//
// Cheap enough to do every time: isFullyTransparent stops at the first pixel
// that is there, tiles that hold something stop immediately, and this runs once
// per solve against a max-flow costing a hundred milliseconds.
PixelRect drawnBounds(const TileGrid& grid) {
    PixelRect bounds;
    for (const auto& [coord, tile] : grid.tiles()) {
        if (!tile || tile->isFullyTransparent()) continue;
        bounds = uniteRects(bounds, {coord.x * kTileSize, coord.y * kTileSize, kTileSize,
                                     kTileSize});
    }
    return bounds;
}

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

    // A few coarse rows at a time, so the full-resolution buffer stays small
    // whatever the canvas is. At one point this was the whole region at once,
    // which for a 3000x2400 canvas is a hundred megabytes of framebuffer.
    constexpr int kBandRows = 32;
    for (int y0 = 0; y0 < height; y0 += kBandRows) {
        const int rows = std::min(kBandRows, height - y0);
        const PixelRect strip{region.x, region.y + y0 * step, region.width,
                              std::min(rows * step, region.height - y0 * step)};
        if (strip.height <= 0) break;

        compositor.compositeGrids(passes, strip, band);

        for (int y = 0; y < rows; ++y) {
            float* out = intensity.data() + static_cast<std::size_t>(y0 + y) * width;
            for (int sub = 0; sub < step; ++sub) {
                const int row = y * step + sub;
                if (row >= band.height()) break;
                const Rgba* source = band.row(row);

                for (int x = 0; x < width; ++x) {
                    const int from = x * step;
                    const int to = std::min(from + step, band.width());
                    float covered = 0.0f;
                    for (int i = from; i < to; ++i) covered = std::max(covered, source[i].a);
                    // Coverage is what stops a cut, so the barrier is one minus
                    // alpha: solid ink reads as 0, bare paper as 1, and the
                    // antialiased rim of a stroke reads as the grey between --
                    // which is the whole reason the boundary can be placed
                    // inside the line rather than beside it.
                    out[x] = std::min(out[x], std::clamp(1.0f - covered, 0.0f, 1.0f));
                }
            }
        }
    }
    return intensity;
}

CtgFill solveCtgJob(const CtgJob& job, bool want_tiles, const std::atomic<bool>* abandon) {
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
        empty.valid = true;
        return empty;
    }

    PixelRect region = drawnBounds(job.scribbles);
    for (const TileGrid& source : job.sources) {
        region = uniteRects(region, drawnBounds(source));
    }
    region = {region.x - kTileSize, region.y - kTileSize, region.width + 2 * kTileSize,
              region.height + 2 * kTileSize};
    region = intersectRects(region, filled);
    if (region.isEmpty()) {
        CtgFill empty;
        empty.inputs = job.inputs;
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
            const Rgba pixel = job.scribbles.pixel(region.x + x * step, region.y + y * step);
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
    built.region = filled;
    built.solved = region;
    built.step = step;
    built.inputs = job.inputs;
    built.valid = true;
    built.inherited = job.inherited;
    built.colours = static_cast<int>(palette.size());
    if (palette.empty()) return built;

    problem.colour_count = static_cast<int>(palette.size());
    problem.hard.assign(palette.size(), 0);

    const LazyBrushResult solved = solveLazyBrush(problem, job.settings.lazybrush, abandon);
    if (solved.abandoned) return kNothing;

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
    // the verdict is affordable for a whole track. Everything above is a
    // max-flow over a grid the size of the solve; everything below is a write
    // per pixel of the canvas, which on a 1080p frame is two million of them
    // and does not get cheaper when the solve is made coarse.
    if (!want_tiles) {
        return built;
    }
    if (abandoned(abandon)) return kNothing;

    // Paint the labels back into tiles, over the whole canvas and at full
    // resolution even when the solve was coarse: a blocky fill is better than
    // none while a finer one is still being worked out.
    //
    // Outside the solved region the lookup is clamped inwards, which extends the
    // labels across the rest of the picture. See above for why that is the same
    // answer solving the whole canvas would have given.
    //
    // Clamped to one cell *inside* the grid rather than to its edge, because the
    // outermost ring is the background seed. It is scaffolding, not an answer:
    // sampling it would paint the whole picture beyond the drawing as background
    // even where a colour had filled right up to it, and would leave a one-cell
    // seam at the region's edge.
    const auto solvedIndex = [&](int value, int origin, int extent, int limit) {
        const int clamped = std::clamp(value, origin, origin + extent - 1);
        const int cell = std::min((clamped - origin) / step, limit - 1);
        return (limit >= 3) ? std::clamp(cell, 1, limit - 2) : cell;
    };

    std::unordered_map<std::uint64_t, std::shared_ptr<Tile>> tiles;

    // Transparency is a label like any other, and paints nothing. Over a tile
    // that already holds something it punches a hole; over one that does not
    // exist it is simply the canvas, so no tile is made to hold a square of
    // nothing.
    const auto paint = [&](int px, int py, const Rgba& colour) {
        const TileCoord coord = tileCoordFor(px, py);
        const std::uint64_t key =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32) |
            static_cast<std::uint32_t>(coord.y);
        auto found = tiles.find(key);
        if (found == tiles.end()) {
            if (colour.a <= 0.0f) return;
            found = tiles.emplace(key, std::make_shared<Tile>()).first;
        }
        found->second->setPixel(tileLocal(px), tileLocal(py), colour);
    };

    for (int y = 0; y < filled.height; ++y) {
        const int py = filled.y + y;
        const int solved_y = solvedIndex(py, region.y, region.height, problem.height);
        for (int x = 0; x < filled.width; ++x) {
            const int px = filled.x + x;
            const int solved_x = solvedIndex(px, region.x, region.width, problem.width);

            const int label =
                solved.labels[static_cast<std::size_t>(solved_y) * problem.width + solved_x];
            if (label < 0) continue;  // nothing reached this pixel

            paint(px, py, scribbleColour(palette[static_cast<std::size_t>(label)]));
        }
    }

    // And then the scribble wins wherever it was drawn.
    //
    // A scribble is a statement about the pixels it covers -- this is the
    // colour here -- and the solver's job is only the pixels nobody said
    // anything about. So where the two disagree the scribble is what was meant,
    // and a mark becomes a manual touch-up for whatever the min-cut missed, at
    // no cost to the solver at all.
    //
    // It has to be the scribble over the fill rather than under it, and a
    // transparent scribble is what settles that: under the fill it would be the
    // one thing hidden, so scribbling "nothing here" would do nothing. The rule
    // that lets a transparent scribble mean anything is the rule that makes an
    // opaque one an override.
    //
    // The marks are self-effacing, which is what stops this looking like scrawl
    // over the artwork: a scribble carries the colour of the label it produces,
    // so wherever the fill agreed with it the override paints the colour that
    // was already there. What you see is exactly the disagreement.
    //
    // Painted from the marks at full resolution rather than from the solved
    // grid, because the solve may be coarse and a mark you made should not be.
    // And painted as the quantised label colour rather than the pixel's own, for
    // the same reason the seeding thresholds: a scribble is a label, so its
    // antialiased rim must not leave a stripe of some colour between.
    for (const auto& [coord, tile] : job.scribbles.tiles()) {
        const PixelRect whole{coord.x * kTileSize, coord.y * kTileSize, kTileSize, kTileSize};
        const PixelRect part = intersectRects(whole, filled);
        if (part.isEmpty()) continue;

        for (int py = part.y; py < part.y + part.height; ++py) {
            for (int px = part.x; px < part.x + part.width; ++px) {
                const Rgba pixel = job.scribbles.pixel(px, py);
                if (pixel.a < job.settings.scribble_alpha_threshold) continue;  // not a label
                paint(px, py, scribbleColour(scribbleLabel(pixel)));
            }
        }
    }

    for (auto& [key, tile] : tiles) {
        // A tile every one of whose pixels was punched back out by a
        // transparent scribble is a tile of nothing, and the grid says nothing
        // by not holding it.
        if (tile->isFullyTransparent()) continue;
        const TileCoord coord{static_cast<int>(static_cast<std::int32_t>(key >> 32)),
                              static_cast<int>(static_cast<std::int32_t>(key & 0xffffffffu))};
        built.tiles.set(coord, std::move(tile));
    }
    return built;
}

}  // namespace animage
