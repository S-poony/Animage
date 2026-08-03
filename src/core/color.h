// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "tile.h"

namespace animage {

// Storage and compositing happen in linear RGB (sRGB primaries for now, wider
// later without a format change). The interface -- colour wheel, gradients,
// mixing -- works in OKLab, converted on the way in and on the way out.
//
// This split is the one decision in the design that is genuinely painful to
// change later: retrofitting linear 16-bit onto an 8-bit sRGB pipeline means
// rewriting every shader and converting every existing file.
//
// OKLab rather than CIELAB because CIELAB has a well-known hue shift on blues
// that turns gradients purple.

struct Oklab {
    float l = 0.0f;
    float a = 0.0f;
    float b = 0.0f;
};

struct Oklch {
    float l = 0.0f;
    float c = 0.0f;
    float h = 0.0f;  // radians
};

float srgbToLinear(float encoded);
float linearToSrgb(float linear);

Oklab linearRgbToOklab(float r, float g, float b);
void oklabToLinearRgb(const Oklab& lab, float& r, float& g, float& b);

Oklch oklabToOklch(const Oklab& lab);
Oklab oklchToOklab(const Oklch& lch);

// Perceptually even interpolation, which is what a gradient or a colour mixer
// wants. Straight linear-RGB interpolation goes through mud.
Oklab mix(const Oklab& a, const Oklab& b, float t);

// Premultiplied "over". Both operands must be linear and premultiplied.
Rgba over(const Rgba& src, const Rgba& dst);

Rgba premultiply(float r, float g, float b, float a);
void unpremultiply(const Rgba& c, float& r, float& g, float& b, float& a);

}  // namespace animage
