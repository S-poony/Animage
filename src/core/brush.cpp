// SPDX-License-Identifier: GPL-3.0-or-later
#include "brush.h"

#include <algorithm>
#include <cmath>

#include "scribble.h"

namespace animage {
namespace {

// Coverage of one pixel by a dab: flat out to `hardness`, then a smoothstep
// down to nothing at the rim. The smooth shoulder is what gives the edge its
// antialiasing -- there is no separate AA pass anywhere in the brush.
float falloff(float distance, float radius, float hardness) {
    if (distance >= radius) return 0.0f;
    const float inner = std::clamp(hardness, 0.0f, 0.99f) * radius;
    if (distance <= inner) return 1.0f;
    const float t = (distance - inner) / (radius - inner);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

float Brush::radiusFor(float pressure) const {
    const float p = std::clamp(pressure, 0.0f, 1.0f);
    if (!settings_.pressure_affects_size) return settings_.radius;
    const float ratio = std::clamp(settings_.min_radius_ratio, 0.0f, 1.0f);
    return settings_.radius * lerp(ratio, 1.0f, p);
}

float Brush::flowFor(float pressure) const {
    const float p = std::clamp(pressure, 0.0f, 1.0f);
    float flow = std::clamp(settings_.opacity, 0.0f, 1.0f) * std::clamp(settings_.a, 0.0f, 1.0f);
    if (settings_.pressure_affects_opacity) flow *= p;
    return flow;
}

void Brush::stamp(float cx, float cy, float radius, float flow) {
    if (!doc_ || !cel_ || radius <= 0.0f || flow <= 0.0f) return;

    const int x0 = static_cast<int>(std::floor(cx - radius));
    const int x1 = static_cast<int>(std::ceil(cx + radius));
    const int y0 = static_cast<int>(std::floor(cy - radius));
    const int y1 = static_cast<int>(std::ceil(cy + radius));

    const TileCoord first = tileCoordFor(x0, y0);
    const TileCoord last = tileCoordFor(x1, y1);

    for (int ty = first.y; ty <= last.y; ++ty) {
        for (int tx = first.x; tx <= last.x; ++tx) {
            const int base_x = tx * kTileSize;
            const int base_y = ty * kTileSize;

            const int px0 = std::max(x0, base_x);
            const int px1 = std::min(x1, base_x + kTileSize - 1);
            const int py0 = std::max(y0, base_y);
            const int py1 = std::min(y1, base_y + kTileSize - 1);
            if (px0 > px1 || py0 > py1) continue;

            // Only now: allocating a tile is what makes a layer stop being
            // free, so it must not happen for a tile the dab misses.
            Tile* tile = cel_->writableTile({tx, ty}, doc_->journal());
            if (!tile) continue;

            for (int py = py0; py <= py1; ++py) {
                for (int px = px0; px <= px1; ++px) {
                    const float dx = static_cast<float>(px) + 0.5f - cx;
                    const float dy = static_cast<float>(py) + 0.5f - cy;
                    const float coverage = falloff(std::sqrt(dx * dx + dy * dy), radius,
                                                   settings_.hardness);
                    if (coverage <= 0.0f) continue;

                    const float alpha = coverage * flow;
                    const int lx = px - base_x;
                    const int ly = py - base_y;
                    const Rgba dst = tile->pixel(lx, ly);

                    // A label is written or it is not; there is no partly. The
                    // threshold is the one the solver reads with, so what the
                    // brush puts down and what the fill makes of it cannot
                    // drift apart.
                    if (settings_.label) {
                        if (alpha < kScribbleAlphaThreshold) continue;
                        tile->setPixel(lx, ly,
                                       settings_.erase
                                           ? Rgba{}
                                           : Rgba{settings_.r, settings_.g, settings_.b, 1.0f});
                        continue;
                    }

                    if (settings_.erase) {
                        const float keep = 1.0f - alpha;
                        tile->setPixel(lx, ly,
                                       {dst.r * keep, dst.g * keep, dst.b * keep, dst.a * keep});
                    } else {
                        // Premultiplied source over premultiplied destination.
                        const float keep = 1.0f - alpha;
                        tile->setPixel(lx, ly,
                                       {settings_.r * alpha + dst.r * keep,
                                        settings_.g * alpha + dst.g * keep,
                                        settings_.b * alpha + dst.b * keep, alpha + dst.a * keep});
                    }
                }
            }
        }
    }

    ++dabs_;
}

void Brush::begin(Document& doc, TrackId track, ImageId image, LayerId layer,
                  const StrokePoint& point) {
    doc_ = &doc;
    cel_ = doc.celForWriting(track, image, layer);
    if (!cel_) {
        active_ = false;
        return;
    }

    active_ = true;
    dabs_ = 0;
    last_ = point;

    const float radius = radiusFor(point.pressure);
    stamp(point.x, point.y, radius, flowFor(point.pressure));
    distance_to_next_dab_ = std::max(settings_.spacing * 2.0f * radius, 0.5f);
}

void Brush::extend(const StrokePoint& point) {
    if (!active_) return;

    const float dx = point.x - last_.x;
    const float dy = point.y - last_.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f) {
        last_ = point;
        return;
    }

    // Walk the segment placing dabs at fixed spacing, carrying the remainder
    // into the next segment. Without that carry, spacing resets at every
    // event and a fast stroke stipples while a slow one blots.
    float travelled = 0.0f;
    float remaining = length;
    while (distance_to_next_dab_ <= remaining) {
        travelled += distance_to_next_dab_;
        remaining -= distance_to_next_dab_;

        const float t = travelled / length;
        const float pressure = lerp(last_.pressure, point.pressure, t);
        const float radius = radiusFor(pressure);
        stamp(last_.x + dx * t, last_.y + dy * t, radius, flowFor(pressure));
        distance_to_next_dab_ = std::max(settings_.spacing * 2.0f * radius, 0.5f);
    }
    distance_to_next_dab_ -= remaining;

    last_ = point;
}

void Brush::end() {
    active_ = false;
    cel_ = nullptr;
    doc_ = nullptr;
}

}  // namespace animage
