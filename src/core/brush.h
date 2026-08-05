// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "document.h"

namespace animage {

// One sample from the tablet, in image coordinates.
struct StrokePoint {
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 1.0f;
    float tilt_x = 0.0f;
    float tilt_y = 0.0f;
};

struct BrushSettings {
    float radius = 12.0f;            // at full pressure, in pixels
    float min_radius_ratio = 0.15f;  // fraction of radius left at zero pressure
    float opacity = 1.0f;
    float hardness = 0.7f;  // fraction of the radius that is fully opaque
    float spacing = 0.10f;  // distance between dabs, as a fraction of diameter

    bool pressure_affects_size = true;
    bool pressure_affects_opacity = true;

    // Straight, not premultiplied, and linear. Alpha is the maximum coverage
    // a single dab can lay down.
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    bool erase = false;

    // Lay down a label rather than paint: a pixel the dab covers takes the
    // colour outright, one it does not is left alone, and nothing in between is
    // written at all.
    //
    // This is what a CTG layer always meant and never did. A scribble is a
    // label -- a pixel either carries it or it does not -- and the solver has
    // read it that way from the start, thresholding coverage at a half. The
    // brush went on blending, so the two only agreed in the middle of a stroke.
    // At its rim, and anywhere one colour crossed another, it left pixels that
    // quantised to some third colour and became a label nobody drew.
    //
    // That was invisible while the fill was all you could see. It is not now
    // that a mark draws over its own fill, and it is what a transparent
    // scribble cannot survive: a blended rim between "no colour" and a colour
    // is neither. Writing hard fixes all three at once, and it costs nothing --
    // the antialiasing it gives up was never being read.
    //
    // Erasing in this mode writes exact zeros, so a rubbed-out mark leaves a
    // genuinely empty tile rather than a rim of residue too faint to see and
    // strong enough to seed.
    bool label = false;
};

// Stamps dabs along the stroke. Everything it writes goes through
// Document::celForWriting and the tile journal, so a stroke is one undo step
// and costs the handles of the tiles it touched.
//
// The caller must hold an open command for the whole stroke:
//
//     ScopedCommand stroke(doc, "Stroke");
//     brush.begin(doc, track, image, layer, first);
//     brush.extend(next);
//     brush.end();
class Brush {
public:
    Brush() = default;
    explicit Brush(const BrushSettings& settings) : settings_(settings) {}

    const BrushSettings& settings() const { return settings_; }
    BrushSettings& settings() { return settings_; }

    void begin(Document& doc, TrackId track, ImageId image, LayerId layer,
               const StrokePoint& point);
    void extend(const StrokePoint& point);
    void end();

    bool active() const { return active_; }

    // Number of dabs laid down by the current or most recent stroke. Exposed
    // for tests: spacing is easy to get wrong in a way nothing else notices.
    int dabCount() const { return dabs_; }

private:
    float radiusFor(float pressure) const;
    float flowFor(float pressure) const;
    void stamp(float cx, float cy, float radius, float flow);

    BrushSettings settings_;

    Document* doc_ = nullptr;
    Cel* cel_ = nullptr;
    bool active_ = false;

    StrokePoint last_{};
    float distance_to_next_dab_ = 0.0f;
    int dabs_ = 0;
};

}  // namespace animage
