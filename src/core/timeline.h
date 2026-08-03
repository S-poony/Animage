// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "image.h"
#include "layer.h"

namespace animage {

// Layers are shared by every image in the timeline; `slots` is time. Exposure
// is the same ImageId appearing in several consecutive slots, so nothing in
// this file may assume the slots are distinct.
struct Timeline {
    TimelineId id = kNoId;
    std::string name;

    std::vector<Layer> layers;  // index 0 composites on top
    std::vector<ImageId> slots;
    std::unordered_map<ImageId, Image> images;

    // Group-level properties. Not exposed in the prototype UI, but the format
    // needs them: without a timeline opacity you cannot fade out a character.
    float opacity = 1.0f;
    BlendMode blend = BlendMode::Normal;
    int time_offset = 0;

    // Counts up and is never reused, for the same reason CelIds are not: a
    // number that comes back means two drawings in one scene answer to it.
    int next_drawing_number = 1;

    const Layer* findLayer(LayerId id) const {
        auto it = std::find_if(layers.begin(), layers.end(),
                               [&](const Layer& l) { return l.id == id; });
        return (it == layers.end()) ? nullptr : &*it;
    }

    Layer* findLayer(LayerId id) {
        auto it = std::find_if(layers.begin(), layers.end(),
                               [&](const Layer& l) { return l.id == id; });
        return (it == layers.end()) ? nullptr : &*it;
    }

    const Image* findImage(ImageId id) const {
        auto it = images.find(id);
        return (it == images.end()) ? nullptr : &it->second;
    }

    Image* findImage(ImageId id) {
        auto it = images.find(id);
        return (it == images.end()) ? nullptr : &it->second;
    }

    ImageId imageAtSlot(std::size_t slot) const {
        return (slot < slots.size()) ? slots[slot] : kNoId;
    }

    std::size_t frameCount() const { return slots.size(); }

    // How many slots hold this image. This is the exposure, and it is not the
    // same as the cel refcount.
    std::size_t exposureOf(ImageId id) const {
        return static_cast<std::size_t>(std::count(slots.begin(), slots.end(), id));
    }

    // First and last slot of the unbroken run of identical ImageIds around
    // `slot` -- that is, the frames over which this drawing is held. Anything
    // that means "after this drawing" has to mean after the whole hold, not
    // after the frame the playhead happens to be on.
    std::pair<std::size_t, std::size_t> runBounds(std::size_t slot) const {
        if (slot >= slots.size()) return {slot, slot};
        const ImageId id = slots[slot];
        std::size_t first = slot;
        while (first > 0 && slots[first - 1] == id) --first;
        std::size_t last = slot;
        while (last + 1 < slots.size() && slots[last + 1] == id) ++last;
        return {first, last};
    }

    // Distinct ImageIds walking outwards from `slot`, nearest first. An image
    // held for five frames counts once, which is what onion skin wants.
    std::vector<ImageId> distinctNeighbours(std::size_t slot, int count, int direction) const;
};

}  // namespace animage
