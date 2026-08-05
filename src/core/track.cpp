// SPDX-License-Identifier: GPL-3.0-or-later
#include "track.h"

namespace animage {

std::vector<ImageId> Track::distinctNeighbours(std::size_t slot, int count,
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

std::size_t Track::firstSlotOf(ImageId id) const {
    const auto it = std::find(slots.begin(), slots.end(), id);
    return static_cast<std::size_t>(it - slots.begin());
}

ImageId Track::celSourceFor(ImageId image, LayerId layer, int direction) const {
    const Image* here = findImage(image);
    if (!here) return kNoId;
    if (here->celFor(layer) != kNoId) return image;

    // Looking backwards starts from the *end* of the run, forwards from its
    // start. Either way the walk begins at the edge of this drawing's own hold
    // and steps away from it, which is what makes a drawing held over five
    // frames one step and not five.
    const std::size_t slot =
        (direction > 0) ? runBounds(firstSlotOf(image)).second : firstSlotOf(image);
    if (slot >= slots.size()) return kNoId;

    // distinctNeighbours already walks exactly this way for onion skin; asking
    // it for every drawing on that side rather than writing a second walk keeps
    // one definition of "the next drawing along".
    for (ImageId other : distinctNeighbours(slot, static_cast<int>(slots.size()), direction)) {
        const Image* record = findImage(other);
        if (record && record->celFor(layer) != kNoId) return other;
    }
    return kNoId;
}

}  // namespace animage
