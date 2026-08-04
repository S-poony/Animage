// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "ids.h"

namespace animage {

enum class LayerKind {
    Raster,
    Ctg,  // stores scribbles, not pixels; the fill is regenerated
};

// Only Normal is implemented in the prototype. The rest are here because the
// serialised format has to be able to name them before the compositor can do
// anything with them.
enum class BlendMode {
    Normal,
    Multiply,
    Screen,
    Add,
};

// Properties only. A Layer never holds pixels, and it belongs to the track
// rather than to any one image: adding a layer adds it to every image at once,
// and changing its opacity changes it for the whole track.
struct Layer {
    LayerId id = kNoId;
    std::string name;
    float opacity = 1.0f;
    bool visible = true;
    bool locked = false;
    LayerKind kind = LayerKind::Raster;
    BlendMode blend = BlendMode::Normal;

    // For a CTG layer: the line-art layers used as barriers. More than one is
    // allowed on purpose — combining rough and clean closes most of the gaps
    // that leak with a single source.
    std::vector<LayerId> ctg_sources;

    // Show the scribbles instead of the fill they produce. A view setting, not
    // a property of the drawing: what is on the layer does not change, only
    // which of the two you are looking at.
    bool show_scribbles = false;
};

}  // namespace animage
