// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "timeline.h"

namespace animage {

// Timelines stack as flat groups: every layer of timeline 0 composites above
// every layer of timeline 1. A layer cannot be interleaved between the layers
// of another timeline; if a character's arm has to pass in front of an object
// on a different timing, the character is split across two timelines.
struct Scene {
    int framerate = 24;
    std::vector<Timeline> timelines;

    const Timeline* findTimeline(TimelineId id) const {
        for (const Timeline& t : timelines) {
            if (t.id == id) return &t;
        }
        return nullptr;
    }

    Timeline* findTimeline(TimelineId id) {
        for (Timeline& t : timelines) {
            if (t.id == id) return &t;
        }
        return nullptr;
    }
};

}  // namespace animage
