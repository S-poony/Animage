// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline.h"

namespace animage {

std::vector<ImageId> Timeline::distinctNeighbours(std::size_t slot, int count,
                                                  int direction) const {
    std::vector<ImageId> out;
    if (count <= 0 || slots.empty() || slot >= slots.size() || direction == 0) return out;

    const ImageId here = slots[slot];
    const int step = (direction > 0) ? 1 : -1;

    for (long long i = static_cast<long long>(slot) + step;
         i >= 0 && i < static_cast<long long>(slots.size()); i += step) {
        const ImageId id = slots[static_cast<std::size_t>(i)];
        if (id == here) continue;
        if (std::find(out.begin(), out.end(), id) != out.end()) continue;
        out.push_back(id);
        if (static_cast<int>(out.size()) == count) break;
    }
    return out;
}

}  // namespace animage
