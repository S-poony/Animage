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

    // Distinct ImageIds walking outwards from `slot`, nearest first. An image
    // held for five frames counts once, which is what onion skin wants.
    std::vector<ImageId> distinctNeighbours(std::size_t slot, int count, int direction) const;
};

}  // namespace animage
