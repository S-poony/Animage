// SPDX-License-Identifier: GPL-3.0-or-later
#include "color.h"

#include <algorithm>
#include <cmath>

namespace animage {

float srgbToLinear(float encoded) {
    if (encoded <= 0.04045f) return encoded / 12.92f;
    return std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float linear) {
    if (linear <= 0.0031308f) return linear * 12.92f;
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// Björn Ottosson's OKLab, sRGB primaries.
Oklab linearRgbToOklab(float r, float g, float b) {
    const float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    const float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    const float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;

    const float l_ = std::cbrt(l);
    const float m_ = std::cbrt(m);
    const float s_ = std::cbrt(s);

    return {0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
            1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
            0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_};
}

void oklabToLinearRgb(const Oklab& lab, float& r, float& g, float& b) {
    const float l_ = lab.l + 0.3963377774f * lab.a + 0.2158037573f * lab.b;
    const float m_ = lab.l - 0.1055613458f * lab.a - 0.0638541728f * lab.b;
    const float s_ = lab.l - 0.0894841775f * lab.a - 1.2914855480f * lab.b;

    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;

    r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

Oklch oklabToOklch(const Oklab& lab) {
    return {lab.l, std::sqrt(lab.a * lab.a + lab.b * lab.b), std::atan2(lab.b, lab.a)};
}

Oklab oklchToOklab(const Oklch& lch) {
    return {lch.l, lch.c * std::cos(lch.h), lch.c * std::sin(lch.h)};
}

Oklab mix(const Oklab& a, const Oklab& b, float t) {
    return {a.l + (b.l - a.l) * t, a.a + (b.a - a.a) * t, a.b + (b.b - a.b) * t};
}

Rgba over(const Rgba& src, const Rgba& dst) {
    const float inv = 1.0f - src.a;
    return {src.r + dst.r * inv, src.g + dst.g * inv, src.b + dst.b * inv,
            src.a + dst.a * inv};
}

Rgba premultiply(float r, float g, float b, float a) { return {r * a, g * a, b * a, a}; }

void unpremultiply(const Rgba& c, float& r, float& g, float& b, float& a) {
    a = c.a;
    if (a <= 0.0f) {
        r = g = b = 0.0f;
        return;
    }
    r = c.r / a;
    g = c.g / a;
    b = c.b / a;
}

}  // namespace animage
