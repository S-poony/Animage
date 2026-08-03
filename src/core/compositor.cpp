// SPDX-License-Identifier: GPL-3.0-or-later
#include "compositor.h"

#include <algorithm>
#include <limits>
#include <thread>

#include "color.h"

namespace animage {
namespace {

// One layer over the rows [y_begin, y_end) of what is already in the
// framebuffer. Both sides are premultiplied, so this is a multiply-add and
// nothing else.
//
// The alpha byte pair is tested before anything is decoded. Line art is mostly
// empty, and a fully transparent premultiplied pixel contributes nothing, so
// skipping on a single 16-bit compare avoids four table lookups and a blend for
// the large majority of pixels.
void blendLayerRows(const Cel& cel, const Layer& layer, const PixelRect& region, int step,
                    int y_begin, int y_end, Framebuffer& out) {
    const float layer_opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
    if (layer_opacity <= 0.0f) return;

    const TileGrid& grid = cel.tiles();
    if (grid.empty()) return;

    const bool faded = layer_opacity < 1.0f;

    for (int y = y_begin; y < y_end; ++y) {
        const int image_y = region.y + y * step;
        Rgba* destination = out.row(y);

        // The tile row does not change across a scanline, so the lookup is
        // hoisted and only repeated when the column crosses a tile boundary.
        const int tile_y = tileCoordFor(0, image_y).y;
        const int local_y = tileLocal(image_y);

        int x = 0;
        while (x < out.width()) {
            const int image_x = region.x + x * step;
            const int tile_x = tileCoordFor(image_x, 0).x;
            const int local_x = tileLocal(image_x);

            // With a sampling step, consecutive outputs are not consecutive
            // image pixels, so a run cannot span more than one of them.
            const int run = (step == 1) ? std::min(out.width() - x, kTileSize - local_x) : 1;

            const TileRef tile = grid.find({tile_x, tile_y});
            if (!tile) {
                x += run;
                continue;  // absent tile is transparent, so nothing to blend
            }

            const Half* pixels =
                tile->rgba.data() + (static_cast<std::size_t>(local_y) * kTileSize + local_x) * 4;

            for (int i = 0; i < run; ++i) {
                const Half* p = pixels + static_cast<std::size_t>(i) * 4;
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

}  // namespace

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

void Compositor::composite(const Document& doc, TimelineId timeline_id, ImageId image_id,
                           const PixelRect& region, Framebuffer& out, int step) const {
    const Timeline* timeline = doc.scene().findTimeline(timeline_id);
    if (!timeline) {
        out.clear();
        return;
    }

    std::vector<LayerId> layers;
    layers.reserve(timeline->layers.size());
    for (const Layer& layer : timeline->layers) layers.push_back(layer.id);

    compositeLayers(doc, timeline_id, image_id, layers, region, out, step);
}

void Compositor::compositeLayers(const Document& doc, TimelineId timeline_id, ImageId image_id,
                                 const std::vector<LayerId>& layers, const PixelRect& region,
                                 Framebuffer& out, int step) const {
    step = std::max(1, step);
    out.resize((region.width + step - 1) / step, (region.height + step - 1) / step);
    if (out.isEmpty()) return;
    out.clear();

    const Timeline* timeline = doc.scene().findTimeline(timeline_id);
    if (!timeline) return;
    const Image* image = timeline->findImage(image_id);
    if (!image) return;

    // Bottom upwards: each layer goes over the accumulated result, and the
    // list is topmost first. Resolved once, before any threads start.
    struct Pass {
        const Cel* cel;
        const Layer* layer;
    };
    std::vector<Pass> passes;
    passes.reserve(layers.size());
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        const Layer* layer = timeline->findLayer(*it);
        if (!layer || !layer->visible) continue;
        const Cel* cel = doc.cel(image->celFor(*it));
        if (!cel) continue;  // no cel means the layer is empty here
        passes.push_back({cel, layer});
    }
    if (passes.empty()) return;

    // Split by rows rather than by layer: each band is independent, so no
    // synchronisation is needed anywhere, and a band does all of its layers
    // while its part of the framebuffer is still in cache.
    const int rows = out.height();
    const int workers = chooseWorkerCount(rows, passes.size());

    const auto run_band = [&](int y_begin, int y_end) {
        for (const Pass& pass : passes) {
            blendLayerRows(*pass.cel, *pass.layer, region, step, y_begin, y_end, out);
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

PixelRect imageBounds(const Document& doc, TimelineId timeline_id, ImageId image_id) {
    const Timeline* timeline = doc.scene().findTimeline(timeline_id);
    if (!timeline) return {};
    const Image* image = timeline->findImage(image_id);
    if (!image) return {};

    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();
    bool any = false;

    for (const Layer& layer : timeline->layers) {
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
