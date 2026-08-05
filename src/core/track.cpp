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

// The nearest drawing on one side that has a cel on this layer, and how many
// distinct drawings away it is. kNoId and zero if there is none.
std::pair<ImageId, int> Track::nearestWithCel(ImageId image, LayerId layer, int direction) const {
    // Looking backwards starts from the *end* of the run, forwards from its
    // start. Either way the walk begins at the edge of this drawing's own hold
    // and steps away from it, which is what makes a drawing held over five
    // frames one step and not five.
    const std::size_t slot =
        (direction > 0) ? runBounds(firstSlotOf(image)).second : firstSlotOf(image);
    if (slot >= slots.size()) return {kNoId, 0};

    // distinctNeighbours already walks exactly this way for onion skin; asking
    // it for every drawing on that side rather than writing a second walk keeps
    // one definition of "the next drawing along". It returns them nearest
    // first, so where a drawing lands in the list is how far away it is.
    const std::vector<ImageId> along =
        distinctNeighbours(slot, static_cast<int>(slots.size()), direction);
    for (std::size_t i = 0; i < along.size(); ++i) {
        const Image* record = findImage(along[i]);
        if (record && record->celFor(layer) != kNoId) {
            return {along[i], static_cast<int>(i) + 1};
        }
    }
    return {kNoId, 0};
}

ImageId Track::celSourceFor(ImageId image, LayerId layer, int direction) const {
    const Image* here = findImage(image);
    if (!here) return kNoId;
    if (here->celFor(layer) != kNoId) return image;

    if (direction != 0) return nearestWithCel(image, layer, direction).first;

    // Both ways: whichever is fewer drawings off. A tie goes to the earlier
    // one, because that is the direction a shot is coloured in and the one
    // somebody reaching for "either" still means first.
    const auto [earlier, back] = nearestWithCel(image, layer, -1);
    const auto [later, forward] = nearestWithCel(image, layer, +1);
    if (earlier == kNoId) return later;
    if (later == kNoId) return earlier;
    return (forward < back) ? later : earlier;
}

}  // namespace animage
