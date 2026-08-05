// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "image.h"
#include "layer.h"

namespace animage {

// Layers are shared by every image in the track; `slots` is time. Exposure
// is the same ImageId appearing in several consecutive slots, so nothing in
// this file may assume the slots are distinct.
struct Track {
    TrackId id = kNoId;
    std::string name;

    std::vector<Layer> layers;  // index 0 composites on top
    std::vector<ImageId> slots;
    std::unordered_map<ImageId, Image> images;

    // Group-level properties. Not exposed in the prototype UI, but the format
    // needs them: without a track opacity you cannot fade out a character.
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

    // Where a drawing starts, or slots.size() if no slot shows it. A drawing
    // and its holds are one contiguous run -- moveDrawing keeps them so -- and
    // anything that means "earlier than this drawing" has to start from the
    // front of that run rather than from wherever the playhead happens to be.
    std::size_t firstSlotOf(ImageId id) const;

    // Which drawing a layer's cel is actually read from at `image`: the image
    // itself when it has one, otherwise the nearest earlier distinct drawing
    // that does, or kNoId if none of them does.
    //
    // Sparse absence already meant "the layer is empty here" for a raster
    // layer. For a CTG layer it means "inherited", and this is the whole of
    // that mechanism: resolved at read time by walking time backwards, never a
    // parent pointer stored per image. A stored pointer would be invalidated by
    // every reorder and every deletion, and the bugs would be intermittent.
    // This walk gets both for free -- reordering changes who inherits from
    // whom, deleting a drawing leaves the ones after it inheriting from
    // whatever now precedes them, and neither touches a cel.
    //
    // `direction` is -1 to look at earlier drawings, +1 at later ones, and 0 at
    // both -- taking whichever is fewer distinct drawings away, earlier on a
    // tie.
    //
    // Nothing here knows about layer kinds. Whether absence means empty or
    // inherited is a decision about the layer, and it is made by the caller;
    // see Document::ctgScribblesAt, which is the only one that should.
    ImageId celSourceFor(ImageId image, LayerId layer, int direction = -1) const;

    // The nearest drawing on one side carrying a cel for this layer, with how
    // many distinct drawings away it is. Exposed because "which is closer" is
    // the whole of what carrying both ways has to decide.
    std::pair<ImageId, int> nearestWithCel(ImageId image, LayerId layer, int direction) const;
};

}  // namespace animage
