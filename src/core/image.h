// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <unordered_map>

#include "ids.h"
#include "tile.h"

namespace animage {

// One column of the track. Holds no pixels: it maps each layer to the cel
// that carries that layer's drawing for this image.
struct Image {
    ImageId id = kNoId;

    // The number an animator would write on the paper. Assigned when the
    // drawing is made and kept for life: a drawing that renumbers itself when
    // the timing changes cannot be talked about, and reordering the track is
    // exactly when you most need to know which one you are holding.
    int number = 0;

    // Sparse. A missing entry means the layer is empty here, which is why
    // adding a layer touches no image and adding an interval allocates nothing.
    std::unordered_map<LayerId, CelId> cels;

    // Which frame of an imported sequence a Reference layer shows here.
    //
    // Sparse and absent-means-empty, exactly as `cels` is, and beside it for
    // the same reason: a reference layer has no cel, so this is the entry that
    // says the layer is not blank at this drawing. It survives reordering and
    // deletion because it is a fact recorded on the drawing rather than derived
    // from where the drawing sits.
    //
    // **That survival is the whole point, and it is not a retiming feature.**
    // Without it, "which frame of the source does this drawing show" has to be
    // worked out from position -- and position moves. Add a hold and two
    // drawings share a slot index; delete a frame and everything after it
    // shifts. The very first hold breaks it, and adding the field afterwards
    // would be a migration of every project with an import in it.
    //
    // Not keyed on `Image::number`. track.h is explicit that nothing is keyed
    // on that number and that it is reused after a deletion, so keying a
    // picture on it would silently re-point another drawing's frame.
    //
    // See docs/importing.md, "one field the model needs".
    std::unordered_map<LayerId, int> source_frames;

    std::optional<Rgba> marker;

    CelId celFor(LayerId layer) const {
        auto it = cels.find(layer);
        return (it == cels.end()) ? kNoId : it->second;
    }

    // -1 where there is no entry, which means the reference layer is empty at
    // this drawing. A real index is never negative, so the two cannot be
    // confused the way a 0 default would confuse "no frame" with "frame zero".
    int sourceFrameFor(LayerId layer) const {
        auto it = source_frames.find(layer);
        return (it == source_frames.end()) ? kNoSourceFrame : it->second;
    }

    static constexpr int kNoSourceFrame = -1;
};

}  // namespace animage
