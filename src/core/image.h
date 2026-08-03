// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <unordered_map>

#include "ids.h"
#include "tile.h"

namespace animage {

// One column of the timeline. Holds no pixels: it maps each layer to the cel
// that carries that layer's drawing for this image.
struct Image {
    ImageId id = kNoId;

    // The number an animator would write on the paper. Assigned when the
    // drawing is made and kept for life: a drawing that renumbers itself when
    // the timing changes cannot be talked about, and reordering the timeline is
    // exactly when you most need to know which one you are holding.
    int number = 0;

    // Sparse. A missing entry means the layer is empty here, which is why
    // adding a layer touches no image and adding an interval allocates nothing.
    std::unordered_map<LayerId, CelId> cels;

    std::optional<Rgba> marker;

    CelId celFor(LayerId layer) const {
        auto it = cels.find(layer);
        return (it == cels.end()) ? kNoId : it->second;
    }
};

}  // namespace animage
