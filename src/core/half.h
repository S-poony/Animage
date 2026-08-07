// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace animage {

// IEEE 754 binary16. Implemented in software rather than using _Float16 or
// std::float16_t so the core library compiles anywhere with a C++20 compiler.
// Pixel storage is the only hot path that touches it, and that path converts
// once per tile access, not per arithmetic operation.

inline std::uint16_t floatToHalfBits(float f) {
    std::uint32_t x;
    std::memcpy(&x, &f, sizeof x);

    const std::uint32_t sign = (x >> 16) & 0x8000u;
    std::uint32_t mant = x & 0x007fffffu;
    const std::int32_t exp = static_cast<std::int32_t>((x >> 23) & 0xffu) - 127;

    if (exp == 128) {  // inf or NaN
        return static_cast<std::uint16_t>(sign | 0x7c00u | (mant ? 0x0200u : 0u));
    }
    if (exp > 15) {  // overflows binary16
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    if (exp >= -14) {  // normal, round to nearest even
        std::uint32_t h = sign | (static_cast<std::uint32_t>(exp + 15) << 10) | (mant >> 13);
        const std::uint32_t rem = mant & 0x1fffu;
        if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;
        return static_cast<std::uint16_t>(h);
    }
    // Subnormal. exp == -25 is below the smallest subnormal (2^-24) but still
    // rounds into it: the same round-to-nearest-even lands on 0x0001 above the
    // halfway point and on zero at or below it. Anything smaller cannot reach
    // halfway, so it is left to the flush below rather than shifted by 26 bits.
    if (exp >= -25) {
        mant |= 0x00800000u;
        const int shift = -exp - 1;  // -exp - 14 + 13
        std::uint32_t h = mant >> shift;
        const std::uint32_t rem = mant & ((1u << shift) - 1u);
        const std::uint32_t halfway = 1u << (shift - 1);
        if (rem > halfway || (rem == halfway && (h & 1u))) ++h;
        return static_cast<std::uint16_t>(sign | h);
    }
    return static_cast<std::uint16_t>(sign);  // underflows to signed zero
}

inline float halfBitsToFloatComputed(std::uint16_t h) {
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
    const std::uint32_t exp = (h >> 10) & 0x1fu;
    std::uint32_t mant = h & 0x03ffu;

    std::uint32_t out;
    if (exp == 0) {
        if (mant == 0) {
            out = sign;
        } else {
            int e = -1;
            do {
                ++e;
                mant <<= 1;
            } while ((mant & 0x0400u) == 0);
            mant &= 0x03ffu;
            out = sign | (static_cast<std::uint32_t>(112 - e) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        out = sign | 0x7f800000u | (mant << 13);
    } else {
        out = sign | ((exp + 112u) << 23) | (mant << 13);
    }

    float f;
    std::memcpy(&f, &out, sizeof f);
    return f;
}

namespace detail {

// All 65536 half values, precomputed. Decoding costs a load instead of a
// branchy bit-twiddle, and every composited pixel decodes four of them, so this
// is the difference between a viewport flatten costing a frame and costing
// several. 256 kB, built once.
inline const float* halfToFloatTable() {
    static const std::vector<float> table = [] {
        std::vector<float> values(65536);
        for (std::uint32_t i = 0; i < 65536; ++i) {
            values[i] = halfBitsToFloatComputed(static_cast<std::uint16_t>(i));
        }
        return values;
    }();
    return table.data();
}

}  // namespace detail

inline float halfBitsToFloat(std::uint16_t h) { return detail::halfToFloatTable()[h]; }

struct Half {
    std::uint16_t bits = 0;

    Half() = default;
    explicit Half(float f) : bits(floatToHalfBits(f)) {}

    float toFloat() const { return halfBitsToFloat(bits); }
    operator float() const { return toFloat(); }

    friend bool operator==(Half a, Half b) { return a.bits == b.bits; }
};

static_assert(sizeof(Half) == 2);

}  // namespace animage
