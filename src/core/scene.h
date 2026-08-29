// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "audio_track.h"
#include "track.h"

namespace animage {

// Bounds on the canvas. The upper one is not a format limit, it is a sanity
// limit: an export composites the canvas and writes a file of it, and a
// mistyped resolution should not be able to ask for either at a size that stops
// the program.
//
// It used to bound the colour solve as well and no longer does -- see
// docs/colour-without-a-canvas.md. What bounds a solve is the drawing and a
// cell budget, neither of which this number can reach.
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
    // picture" is: what the frame line shows and what an export writes to a
    // file. Without it every exported frame would be its own bounding box and no
    // two would be the same size.
    //
    // It is not what a colour fill is bounded by. It was, and the last thing
    // derived from a drawing that was still a rectangle was the fill; a shape
    // running off the frame is coloured out there too now, and a ball animating
    // off-screen keeps its colour instead of losing it at the frame line. Export
    // is unaffected, because it composites this rectangle and always did.
    int width = 1920;
    int height = 1080;

    PixelRect canvas() const { return {0, 0, width, height}; }

    std::vector<Track> tracks;

    // Soundtracks, in their own list rather than as a kind of Track. See
    // audio_track.h for the argument, which is the specification's and which
    // the code makes sharper: about twenty places walk `tracks`, and every one
    // of them goes on meaning what it meant because this is not in there.
    //
    // **It does not enter shotFrames or longestTrack**, and that is a decision.
    // A shot's length is what the drawings make it, or what the scene was told;
    // a soundtrack running long is reference material, and a scene that grew
    // every time somebody imported one would be deciding the shot from the
    // wrong thing.
    std::vector<AudioTrack> audio_tracks;

    // Whether the shot's length is the scene's to say, or is taken from whatever
    // the tracks add up to.
    //
    // Off by default, which is what happened before it could be said: a shot
    // being made up as it goes has no length yet, and deriving one is the honest
    // answer. Switch it on and the number below is the shot, whatever the tracks
    // do -- which is the state you want when the length is decided first,
    // animating to a soundtrack or filling an exposure sheet, and it is what
    // makes a cycle worth having. A four-drawing walk cycles over sixty frames
    // because the scene says sixty; with nothing to say it, the walk is the
    // longest track and cycles over nothing at all.
    //
    // Two fields rather than a sentinel value, because "derived" and "sixty" are
    // different kinds of answer and a zero pretending to mean the first is the
    // sort of thing that gets typed into by accident.
    bool fixed_length = false;
    int length = 100;

    // What the tracks alone make it.
    std::size_t longestTrack() const {
        std::size_t frames = 0;
        for (const Track& track : tracks) frames = std::max(frames, track.frameCount());
        return frames;
    }

    // The shot: what plays, and what is exported. Nothing else.
    //
    // With a fixed length this is a cap, and a track is allowed to run past it.
    // Drawings out there are not lost and not hidden -- the timeline still shows
    // them and you can still work on them -- they are simply not in the shot
    // until the boundary is moved past them. Cutting a shot short must not mean
    // destroying what is beyond the cut.
    std::size_t shotFrames() const {
        return fixed_length ? static_cast<std::size_t>(std::max(0, length)) : longestTrack();
    }

    // Everything the timeline can reach: the shot, or a track that runs past it.
    //
    // Separate from shotFrames on purpose, and the split is the whole of how a
    // cap works. Everything that draws or scrubs wants this one; everything that
    // plays or writes files wants the other. A max() hidden inside one function
    // could not tell them apart.
    std::size_t timelineFrames() const { return std::max(shotFrames(), longestTrack()); }

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

    // Deliberately a different function from findTrack rather than one that
    // searches both lists. An id handed to the wrong one answers *nothing here*
    // rather than something plausible, and the timeline's two selections are
    // what keep them apart in the first place -- see docs/importing.md, "the
    // two selections".
    const AudioTrack* findAudioTrack(TrackId id) const {
        for (const AudioTrack& t : audio_tracks) {
            if (t.id == id) return &t;
        }
        return nullptr;
    }

    AudioTrack* findAudioTrack(TrackId id) {
        for (AudioTrack& t : audio_tracks) {
            if (t.id == id) return &t;
        }
        return nullptr;
    }
};

}  // namespace animage
