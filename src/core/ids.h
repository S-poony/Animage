// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace animage {

using CelId = std::uint64_t;
using ImageId = std::uint64_t;
using LayerId = std::uint64_t;
using TrackId = std::uint64_t;

inline constexpr std::uint64_t kNoId = 0;

// Ids come from a monotonic counter and are never reused, even after the object
// they named is deleted. Undo depends on this: deleting image 3 and then undoing
// a stroke made on image 5 that shared its cel only works if the CelId that
// stroke recorded still means what it meant.
class IdGenerator {
public:
    std::uint64_t next() { return ++counter_; }
    std::uint64_t peek() const { return counter_; }

    // Only for deserialisation: resume past the highest id in the file.
    void resumeAfter(std::uint64_t highest) {
        if (highest > counter_) counter_ = highest;
    }

private:
    std::uint64_t counter_ = 0;
};

}  // namespace animage
