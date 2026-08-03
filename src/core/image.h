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
