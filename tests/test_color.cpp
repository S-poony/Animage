// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>

#include "color.h"
#include "half.h"
#include "testing.h"

using namespace animage;

namespace {

void halfRoundTrips() {
    TEST("half-float round trips within its precision");
    const float values[] = {0.0f,  1.0f,  0.5f,   0.25f,   -1.0f,  65504.0f,
                            1e-5f, 1e-7f, 0.001f, 0.3333f, -0.75f, 12345.0f};
    for (float v : values) {
        const float back = Half(v).toFloat();
        const double tolerance = std::fabs(v) * 1e-3 + 1e-7;
        CHECK_NEAR(back, v, tolerance);
    }

    CHECK_EQ(Half(0.0f).bits, static_cast<std::uint16_t>(0));
    CHECK_EQ(Half(1.0f).bits, static_cast<std::uint16_t>(0x3c00));
    CHECK_EQ(Half(-2.0f).bits, static_cast<std::uint16_t>(0xc000));

    // Beyond the range of binary16 it must saturate to infinity, not wrap.
    CHECK(std::isinf(Half(1e30f).toFloat()));
    CHECK(Half(1e30f).toFloat() > 0.0f);
    CHECK(Half(-1e30f).toFloat() < 0.0f);

    // Subnormals still carry a value rather than flushing to zero.
    CHECK(Half(1e-7f).toFloat() > 0.0f);
}

void srgbTransferRoundTrips() {
    TEST("sRGB transfer function round trips");
    for (int i = 0; i <= 100; ++i) {
        const float encoded = static_cast<float>(i) / 100.0f;
        CHECK_NEAR(linearToSrgb(srgbToLinear(encoded)), encoded, 1e-5);
    }
    CHECK_NEAR(srgbToLinear(0.0f), 0.0, 1e-9);
    CHECK_NEAR(srgbToLinear(1.0f), 1.0, 1e-6);
    // Mid grey in sRGB is much darker than half in linear light.
    CHECK_NEAR(srgbToLinear(0.5f), 0.2140, 1e-3);
}

void oklabRoundTrips() {
    TEST("OKLab round trips from linear RGB");
    const float samples[][3] = {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                                {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.2f, 0.4f, 0.6f},
                                {0.9f, 0.1f, 0.35f}};
    for (const auto& s : samples) {
        const Oklab lab = linearRgbToOklab(s[0], s[1], s[2]);
        float r = 0, g = 0, b = 0;
        oklabToLinearRgb(lab, r, g, b);
        CHECK_NEAR(r, s[0], 1e-4);
        CHECK_NEAR(g, s[1], 1e-4);
        CHECK_NEAR(b, s[2], 1e-4);
    }

    // White sits at L = 1 with no chroma.
    const Oklab white = linearRgbToOklab(1.0f, 1.0f, 1.0f);
    CHECK_NEAR(white.l, 1.0, 1e-3);
    CHECK_NEAR(white.a, 0.0, 1e-3);
    CHECK_NEAR(white.b, 0.0, 1e-3);
}

void oklchRoundTrips() {
    TEST("OKLCh round trips through OKLab");
    const Oklab lab = linearRgbToOklab(0.3f, 0.6f, 0.2f);
    const Oklch lch = oklabToOklch(lab);
    const Oklab back = oklchToOklab(lch);
    CHECK_NEAR(back.l, lab.l, 1e-6);
    CHECK_NEAR(back.a, lab.a, 1e-6);
    CHECK_NEAR(back.b, lab.b, 1e-6);
    CHECK(lch.c > 0.0f);
}

// The reason for OKLab rather than CIELAB: a blue-to-white ramp must not detour
// through purple. Hue should stay put along the interpolation.
void blueToWhiteKeepsItsHue() {
    TEST("blue to white does not swing through purple");
    const Oklab blue = linearRgbToOklab(0.0f, 0.0f, 1.0f);
    const Oklab white = linearRgbToOklab(1.0f, 1.0f, 1.0f);
    const float start_hue = oklabToOklch(blue).h;

    for (int i = 1; i < 10; ++i) {
        const Oklab step = mix(blue, white, static_cast<float>(i) / 10.0f);
        const float hue = oklabToOklch(step).h;
        CHECK_NEAR(hue, start_hue, 0.12);  // radians
    }
}

void alphaCompositing() {
    TEST("premultiplied over composites correctly");
    const Rgba opaque_red = premultiply(1.0f, 0.0f, 0.0f, 1.0f);
    const Rgba half_white = premultiply(1.0f, 1.0f, 1.0f, 0.5f);

    const Rgba result = over(half_white, opaque_red);
    CHECK_NEAR(result.a, 1.0, 1e-5);
    CHECK_NEAR(result.r, 1.0, 1e-5);   // 0.5 white + 0.5 of the red beneath
    CHECK_NEAR(result.g, 0.5, 1e-5);

    // Compositing anything over nothing leaves it unchanged.
    const Rgba nothing{};
    const Rgba unchanged = over(opaque_red, nothing);
    CHECK_NEAR(unchanged.r, opaque_red.r, 1e-6);
    CHECK_NEAR(unchanged.a, 1.0, 1e-6);

    float r = 0, g = 0, b = 0, a = 0;
    unpremultiply(half_white, r, g, b, a);
    CHECK_NEAR(r, 1.0, 1e-5);
    CHECK_NEAR(a, 0.5, 1e-5);
}

}  // namespace

int main() {
    std::printf("color:\n");
    halfRoundTrips();
    srgbTransferRoundTrips();
    oklabRoundTrips();
    oklchRoundTrips();
    blueToWhiteKeepsItsHue();
    alphaCompositing();
    return testing::summarise("color");
}
