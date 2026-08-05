// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "tile.h"

namespace animage {

// What a mark on a CTG layer means, in one place.
//
// The brush writes these and the solver reads them, and they used to agree only
// by both having been written carefully. They are here so that they agree by
// construction: a threshold the brush rounds to and the solver compares
// against, and a colour that is a label rather than paint.

// Below this a pixel is not scribbled. A label is not a quantity: a
// half-transparent scribble pixel would be half a vote for a colour, which
// means nothing.
//
// Marks are written hard now, so what is stored is 0 or 1 and any threshold
// between would do. It stays at a half because projects made before that are
// full of soft-edged scribbles, and their edges have to keep meaning what they
// meant when they were drawn.
inline constexpr float kScribbleAlphaThreshold = 0.5f;

// The label that means "no colour here".
//
// Transparency cannot be written as alpha zero, because alpha zero is already
// how the layer says nothing was scribbled at all -- the solver would simply
// not see it, and scribbling "nothing" would do nothing. So it is stored as a
// pixel that is unmistakably present and unmistakably not a colour: fully
// opaque, with negative light in every channel.
//
// Negative is what makes it safe. A premultiplied colour lives in [0,1], so
// nothing the brush, the compositor or a file can produce lands here by
// accident, and no colour is taken away from the user to pay for the encoding.
// Half stores -1 exactly, so it survives the file and every round trip
// unchanged, and the format does not have to learn a new field.
//
// It is only unambiguous because a CTG stroke writes hard labels and never
// blends. A blended rim between this and a real colour would be some arbitrary
// half-negative thing that is neither one nor the other. See
// BrushSettings::label.
inline constexpr Rgba kTransparentScribble{-1.0f, -1.0f, -1.0f, 1.0f};

inline bool isScribbled(const Rgba& pixel) { return pixel.a >= kScribbleAlphaThreshold; }

inline bool isTransparentScribble(const Rgba& pixel) {
    return pixel.r < 0.0f || pixel.g < 0.0f || pixel.b < 0.0f;
}

// A label key. Colours are compared after quantising to eight bits a channel:
// the brush lays one flat colour per stroke, but premultiplication and the
// half-float round trip move the last few bits about, and two strokes of "the
// same" red have to end up as one label.
//
// Transparency takes a key above everything twenty-four bits can hold, so the
// palette needs no second list and no parallel flag to carry it.
inline constexpr std::uint32_t kTransparentScribbleLabel = 0x01000000u;

inline std::uint32_t scribbleLabel(const Rgba& premultiplied) {
    if (isTransparentScribble(premultiplied)) return kTransparentScribbleLabel;

    const float alpha = std::max(premultiplied.a, 1e-4f);
    const auto channel = [&](float value) {
        const int quantised =
            static_cast<int>(std::lround(std::clamp(value / alpha, 0.0f, 1.0f) * 255.0f));
        return static_cast<std::uint32_t>(std::clamp(quantised, 0, 255));
    };
    return (channel(premultiplied.r) << 16) | (channel(premultiplied.g) << 8) |
           channel(premultiplied.b);
}

// What the fill paints for a label. Transparency paints nothing, which is the
// whole of it: the hole is the answer.
inline Rgba scribbleColour(std::uint32_t label) {
    if (label == kTransparentScribbleLabel) return {};

    const float r = static_cast<float>((label >> 16) & 0xff) / 255.0f;
    const float g = static_cast<float>((label >> 8) & 0xff) / 255.0f;
    const float b = static_cast<float>(label & 0xff) / 255.0f;
    return {r, g, b, 1.0f};  // premultiplied by an alpha of one
}

}  // namespace animage
