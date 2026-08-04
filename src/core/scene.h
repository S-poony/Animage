// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "track.h"

namespace animage {

// Bounds on the canvas. The upper one is not a format limit, it is a sanity
// limit: the CTG solve and the composite are both bounded by the canvas, and
// a mistyped resolution should not be able to ask for either at a size that
// stops the program.
constexpr int kMinCanvasSide = 16;
constexpr int kMaxCanvasSide = 16384;

// Tracks stack as flat groups: every layer of track 0 composites above
// every layer of track 1. A layer cannot be interleaved between the layers
// of another track; if a character's arm has to pass in front of an object
// on a different timing, the character is split across two tracks.
struct Scene {
    int framerate = 24;

    // The canvas: the rectangle that gets exported, with its top-left corner at
    // the origin.
    //
    // It is the only rectangle in the model. Tiles are sparse and their
    // coordinates are signed, so the surface you draw on has no edges at all --
    // deliberately, because roughs run off the edge and a drawing should not be
    // clipped while it is being made. But something has to say what "the
    // picture" is: what the frame line shows, what a colour fill is bounded by,
    // and what M5 writes to a file. Without it every exported frame would be its
    // own bounding box and no two would be the same size.
    int width = 1920;
    int height = 1080;

    PixelRect canvas() const { return {0, 0, width, height}; }

    std::vector<Track> tracks;

    const Track* findTrack(TrackId id) const {
        for (const Track& t : tracks) {
            if (t.id == id) return &t;
        }
        return nullptr;
    }

    Track* findTrack(TrackId id) {
        for (Track& t : tracks) {
            if (t.id == id) return &t;
        }
        return nullptr;
    }
};

}  // namespace animage
