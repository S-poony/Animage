// SPDX-License-Identifier: GPL-3.0-or-later
#include "compositor.h"

#include <algorithm>
#include <limits>

#include "color.h"

namespace animage {
namespace {

// One layer over what is already in the framebuffer. Both sides are
// premultiplied, so this is a multiply-add and nothing else.
void blendLayerOver(const Cel& cel, const Layer& layer, const PixelRect& region,
                    Framebuffer& out) {
    const float layer_opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
    if (layer_opacity <= 0.0f) return;

    const TileGrid& grid = cel.tiles();
    if (grid.empty()) return;

    for (int y = 0; y < out.height(); ++y) {
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

            const TileRef tile = grid.find({tile_x, tile_y});
            if (!tile) {
                x += run;
                continue;  // absent tile is transparent, so nothing to blend
            }

            for (int i = 0; i < run; ++i) {
                Rgba source = tile->pixel(local_x + i, local_y);
                if (source.a <= 0.0f && source.r <= 0.0f && source.g <= 0.0f &&
                    source.b <= 0.0f) {
                    continue;
                }
                if (layer_opacity < 1.0f) {
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

void Framebuffer::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.assign(static_cast<std::size_t>(width_) * height_, Rgba{});
}

void Framebuffer::clear() { std::fill(pixels_.begin(), pixels_.end(), Rgba{}); }

void Compositor::composite(const Document& doc, TimelineId timeline_id, ImageId image_id,
                           const PixelRect& region, Framebuffer& out) const {
    const Timeline* timeline = doc.scene().findTimeline(timeline_id);
    if (!timeline) {
        out.clear();
        return;
    }

    std::vector<LayerId> layers;
    layers.reserve(timeline->layers.size());
    for (const Layer& layer : timeline->layers) layers.push_back(layer.id);

    compositeLayers(doc, timeline_id, image_id, layers, region, out);
}

void Compositor::compositeLayers(const Document& doc, TimelineId timeline_id, ImageId image_id,
                                 const std::vector<LayerId>& layers, const PixelRect& region,
                                 Framebuffer& out) const {
    out.resize(region.width, region.height);
    if (out.isEmpty()) return;
    out.clear();

    const Timeline* timeline = doc.scene().findTimeline(timeline_id);
    if (!timeline) return;
    const Image* image = timeline->findImage(image_id);
    if (!image) return;

    // Bottom upwards: each layer goes over the accumulated result, and the
    // list is topmost first.
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        const Layer* layer = timeline->findLayer(*it);
        if (!layer || !layer->visible) continue;

        const Cel* cel = doc.cel(image->celFor(*it));
        if (!cel) continue;  // no cel means the layer is empty here

        blendLayerOver(*cel, *layer, region, out);
    }
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
