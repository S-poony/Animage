// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctg.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "compositor.h"

namespace animage {
namespace {

// Scribble colours are compared after quantising to eight bits a channel. The
// brush lays down one flat colour per stroke, but premultiplication and the
// half-float round trip move the last few bits about, and two strokes of "the
// same" red have to end up as one label.
std::uint32_t quantise(const Rgba& premultiplied) {
    const float alpha = std::max(premultiplied.a, 1e-4f);
    const auto channel = [&](float value) {
        const int quantised = static_cast<int>(std::lround(
            std::clamp(value / alpha, 0.0f, 1.0f) * 255.0f));
        return static_cast<std::uint32_t>(std::clamp(quantised, 0, 255));
    };
    return (channel(premultiplied.r) << 16) | (channel(premultiplied.g) << 8) |
           channel(premultiplied.b);
}

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

PixelRect celBounds(const Cel& cel) {
    PixelRect bounds;
    for (const TileCoord& coord : cel.tiles().coords()) {
        const PixelRect tile{coord.x * kTileSize, coord.y * kTileSize, kTileSize, kTileSize};
        bounds = uniteRects(bounds, tile);
    }
    return bounds;
}

Rgba unquantise(std::uint32_t key) {
    const float r = static_cast<float>((key >> 16) & 0xff) / 255.0f;
    const float g = static_cast<float>((key >> 8) & 0xff) / 255.0f;
    const float b = static_cast<float>(key & 0xff) / 255.0f;
    return {r, g, b, 1.0f};  // premultiplied by an alpha of one
}

}  // namespace

std::vector<float> ctgBarrier(const Document& doc, TrackId track, ImageId image,
                              const std::vector<LayerId>& sources, const PixelRect& region,
                              int step) {
    step = std::max(1, step);
    const int width = (region.width + step - 1) / step;
    const int height = (region.height + step - 1) / step;
    std::vector<float> intensity(static_cast<std::size_t>(std::max(0, width)) *
                                     std::max(0, height),
                                 1.0f);
    if (width <= 0 || height <= 0 || sources.empty()) return intensity;

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

        compositor.compositeLayers(doc, track, image, sources, strip, band);

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

const CtgFill& ctgFill(Document& doc, TrackId track, ImageId image, LayerId layer_id,
                       const CtgSettings& settings) {
    static const CtgFill kNothing;

    const Track* line = doc.scene().findTrack(track);
    if (!line) return kNothing;
    const Layer* layer = line->findLayer(layer_id);
    if (!layer || layer->kind != LayerKind::Ctg) return kNothing;

    const Image* record = line->findImage(image);
    if (!record) return kNothing;
    const CelId scribble_id = record->celFor(layer_id);
    const Cel* scribbles = doc.cel(scribble_id);
    if (!scribbles) return kNothing;

    // Everything the answer depends on, mixed into one number. Cheaper than
    // hashing the pixels and exact enough: a cel bumps its revision on every
    // write, so nothing can change without this changing.
    std::uint64_t inputs = scribbles->revision() * 0x9e3779b97f4a7c15ull;
    for (LayerId source : layer->ctg_sources) {
        const Cel* cel = doc.cel(record->celFor(source));
        inputs = inputs * 31 + (cel ? cel->revision() : 0) + source;
    }
    inputs = inputs * 31 + static_cast<std::uint64_t>(settings.downscale);
    // The canvas bounds the solve, so resizing it changes the answer.
    inputs = inputs * 31 + static_cast<std::uint64_t>(doc.scene().width);
    inputs = inputs * 31 + static_cast<std::uint64_t>(doc.scene().height);

    CtgFill& cached = doc.ctgCache()[scribble_id];
    if (cached.valid && cached.inputs == inputs) return cached;

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
    // Solving the canvas directly instead was correct and wasteful: the cap
    // below is on the number of cells, so paying for empty paper is paid for in
    // resolution over the drawing. A small drawing on a 1080p canvas was solved
    // at a third of full size when it could be solved at full size.
    const PixelRect filled = doc.scene().canvas();
    if (filled.isEmpty()) {
        cached = CtgFill{};
        cached.inputs = inputs;
        cached.valid = true;
        return cached;
    }

    PixelRect region = celBounds(*scribbles);
    for (LayerId source : layer->ctg_sources) {
        const Cel* cel = doc.cel(record->celFor(source));
        if (cel) region = uniteRects(region, celBounds(*cel));
    }
    region = {region.x - kTileSize, region.y - kTileSize, region.width + 2 * kTileSize,
              region.height + 2 * kTileSize};
    region = intersectRects(region, filled);
    if (region.isEmpty()) {
        cached = CtgFill{};
        cached.inputs = inputs;
        cached.valid = true;
        return cached;
    }

    // The solve is bounded, whatever it is asked for. A max-flow over a region
    // grows faster than the region does -- roughly 1.3 s for a megapixel -- and
    // it runs where the interface is waiting, so an unbounded one on a large
    // drawing does not take a while, it stops the program. Coarser is a visible
    // cost; frozen is not a cost, it is a failure.
    //
    // The blockiness this causes on a big drawing is the reason to solve on a
    // background thread and refine, which is the proper fix and is not here.
    constexpr long long kSolveBudget = 512 * 512;
    int step = std::max(1, settings.downscale);
    while (static_cast<long long>((region.width + step - 1) / step) *
               ((region.height + step - 1) / step) >
           kSolveBudget) {
        ++step;
    }

    LazyBrushProblem problem;
    problem.width = (region.width + step - 1) / step;
    problem.height = (region.height + step - 1) / step;
    problem.intensity = ctgBarrier(doc, track, image, layer->ctg_sources, region, step);
    problem.seeds.assign(static_cast<std::size_t>(problem.width) * problem.height, -1);

    // Read the scribbles off the cel, one label per distinct colour.
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
    // Two scribbles it is: one for the shape, one for what surrounds it. That
    // is a real cost and it is the honest one.
    std::unordered_map<std::uint32_t, int> index_of;
    std::vector<std::uint32_t> palette;

    for (int y = 0; y < problem.height; ++y) {
        for (int x = 0; x < problem.width; ++x) {
            const Rgba pixel = scribbles->pixel(region.x + x * step, region.y + y * step);
            if (pixel.a < settings.scribble_alpha_threshold) continue;

            const std::uint32_t key = quantise(pixel);
            auto found = index_of.find(key);
            if (found == index_of.end()) {
                found = index_of.emplace(key, static_cast<int>(palette.size())).first;
                palette.push_back(key);
            }
            problem.seeds[static_cast<std::size_t>(y) * problem.width + x] = found->second;
        }
    }

    cached = CtgFill{};
    cached.region = filled;
    cached.inputs = inputs;
    cached.valid = true;
    cached.colours = static_cast<int>(palette.size());
    if (palette.empty()) return cached;

    problem.colour_count = static_cast<int>(palette.size());
    problem.hard.assign(palette.size(), 0);

    const LazyBrushResult solved = solveLazyBrush(problem, settings.lazybrush);

    // Paint the labels back into tiles, over the whole canvas and at full
    // resolution even when the solve was coarse: a blocky fill is better than
    // none while the pen is moving.
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
    for (int y = 0; y < filled.height; ++y) {
        const int py = filled.y + y;
        const int solved_y = solvedIndex(py, region.y, region.height, problem.height);
        for (int x = 0; x < filled.width; ++x) {
            const int px = filled.x + x;
            const int solved_x = solvedIndex(px, region.x, region.width, problem.width);

            const int label =
                solved.labels[static_cast<std::size_t>(solved_y) * problem.width + solved_x];
            if (label < 0) continue;  // nothing reached this pixel

            const TileCoord coord = tileCoordFor(px, py);
            const std::uint64_t key =
                (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32) |
                static_cast<std::uint32_t>(coord.y);

            auto found = tiles.find(key);
            if (found == tiles.end()) {
                found = tiles.emplace(key, std::make_shared<Tile>()).first;
            }
            found->second->setPixel(tileLocal(px), tileLocal(py),
                                    unquantise(palette[static_cast<std::size_t>(label)]));
        }
    }
    for (auto& [key, tile] : tiles) {
        const TileCoord coord{static_cast<int>(static_cast<std::int32_t>(key >> 32)),
                              static_cast<int>(static_cast<std::int32_t>(key & 0xffffffffu))};
        cached.tiles.set(coord, std::move(tile));
    }
    return cached;
}

}  // namespace animage
