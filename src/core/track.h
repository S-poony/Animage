// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "image.h"
#include "layer.h"

namespace animage {

// What a track shows once the playhead is past its last drawing.
//
// Tracks share one timeline and are not obliged to be the same length, so this
// is a real question and not an edge case: a background drawn once sits under a
// character animated over forty frames, and a four-drawing cycle is how a walk
// is built.
enum class TrackEnd {
    Nothing,   // the track is simply empty out there
    HoldLast,  // its last drawing stays on screen
    Cycle,     // it starts again from the beginning, and keeps going
};

// A track's group-level properties: everything about a track that is not its
// layers, its slots or its images. Those three have operations of their own
// because they are large and change one at a time; these are small, they are
// edited together in one panel, and one operation swapping the lot of them is
// both cheaper and easier to reason about than five.
struct TrackProperties {
    std::string name;
    float opacity = 1.0f;
    BlendMode blend = BlendMode::Normal;
    int time_offset = 0;

    // Whether a new drawing lands *on* the playhead and takes over the rest of
    // the hold it lands in, rather than going in after the hold and making the
    // track a frame longer.
    //
    // Off, adding a drawing lengthens the shot. On, the shot is a fixed length
    // and adding a drawing spends frames that were already there: the drawing
    // you are standing on keeps the frames before the playhead and the new one
    // takes the rest. Which of those is right is a question about the shot --
    // animating to a soundtrack means the length is decided and holds are what
    // you have to spend -- so it is a setting on the track and not a mode of
    // the program.
    //
    // On by default, because spending a hold is the ordinary case: you block a
    // shot out in holds and then break them down, and a default that lengthens
    // the shot on every breakdown means retiming it by hand afterwards.
    //
    // It never takes a drawing's last frame. Standing on the first frame of a
    // hold, the new drawing starts one frame later, and a hold of one frame has
    // nothing to spare at all so the insert falls back to lengthening the
    // track. See Track::overwriteRangeAt.
    bool overwrite_drawings = true;

    // What the track shows past its last drawing. Nothing by default, which is
    // what it did before there was a choice.
    //
    // It never makes the shot longer: the timeline is as long as the longest
    // track and a cycle fills the frames that are already there. A scene of one
    // cycling track is four frames long and cycles over nothing.
    TrackEnd end = TrackEnd::Nothing;
};

// Layers are shared by every image in the track; `slots` is time. Exposure
// is the same ImageId appearing in several consecutive slots, so nothing in
// this file may assume the slots are distinct.
struct Track {
    TrackId id = kNoId;
    std::string name;

    std::vector<Layer> layers;  // index 0 composites on top
    std::vector<ImageId> slots;
    std::unordered_map<ImageId, Image> images;

    // Group-level properties. Opacity, blend and the time offset are stored and
    // not yet applied -- the format needs them, because without a track opacity
    // you cannot fade out a character, and three layers at half opacity is a
    // different picture from three layers faded as a group wherever two of them
    // overlap.
    float opacity = 1.0f;
    BlendMode blend = BlendMode::Normal;
    int time_offset = 0;
    bool overwrite_drawings = true;
    TrackEnd end = TrackEnd::Nothing;

    TrackProperties properties() const {
        return {name, opacity, blend, time_offset, overwrite_drawings, end};
    }
    void setProperties(const TrackProperties& p) {
        name = p.name;
        opacity = p.opacity;
        blend = p.blend;
        time_offset = p.time_offset;
        overwrite_drawings = p.overwrite_drawings;
        end = p.end;
    }

    // The number a new drawing on this track should carry: the lowest one no
    // drawing here is using.
    //
    // Derived, and it used to be a stored counter that only ever went up. That
    // never reused a number, which sounds like the safe property and is not the
    // one that matters -- the number is a label on a card and in a tooltip, and
    // nothing is keyed on it. What it cost was gaps: delete drawing 2, add
    // another, and it came back as 3 with no 2 in the track at all, and a
    // reworked scene climbed into numbers that meant nothing.
    //
    // Two drawings still never share a number at one time, which is the part
    // that has to hold for the card to identify anything. What is given up is
    // that a number names the same drawing for ever -- delete 2 and the next
    // drawing is 2 -- so a note written a week ago about "drawing 2" may now
    // point elsewhere. That was the trade, made deliberately.
    int nextDrawingNumber() const {
        std::vector<int> used;
        used.reserve(images.size());
        for (const auto& [id, image] : images) used.push_back(image.number);
        std::sort(used.begin(), used.end());

        int candidate = 1;
        for (const int taken : used) {
            if (taken < candidate) continue;  // duplicates, and anything a file got wrong
            if (taken > candidate) break;     // the gap starts here
            ++candidate;
        }
        return candidate;
    }

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

    // What this track *holds* at one frame: the slot's own drawing, and kNoId
    // past the end. The track as it was written, with no policy applied.
    //
    // This is what editing works on, and what a per-layer export writes, because
    // both are about the drawings that are really there.
    ImageId imageAtSlot(std::size_t slot) const {
        return (slot < slots.size()) ? slots[slot] : kNoId;
    }

    // What this track *shows* at one frame, which past its last drawing is
    // whatever `end` says. The picture rather than the contents.
    //
    // The two are the same everywhere inside the track and differ only out past
    // it, and keeping them separate is the whole of how the export can flatten a
    // cycling background into the composite while the background's own layer
    // sequence still stops when the background does.
    //
    // This is the only place the end behaviour is read. Nothing else may compare
    // a slot against `slots.size()` and decide for itself what happens after.
    ImageId imageShownAt(std::size_t slot) const {
        if (slots.empty()) return kNoId;
        if (slot < slots.size()) return slots[slot];
        switch (end) {
            case TrackEnd::HoldLast:
                return slots.back();
            case TrackEnd::Cycle:
                return slots[slot % slots.size()];
            case TrackEnd::Nothing:
                break;
        }
        return kNoId;
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

    // The slots a drawing put down at `slot` would take over on a track that
    // overwrites, or nothing at all when there is no room for one.
    //
    // The rest of the hold from the playhead onwards, with one exception: never
    // the first frame of that hold. Standing on it, the new drawing starts one
    // frame later, so the drawing you are standing on always keeps a frame and
    // no drawing is ever wiped out by putting another one down. A hold of one
    // frame therefore has no room at all, and that is what nothing means --
    // whoever asked has to decide what to do instead, because the alternatives
    // differ: an insert lengthens the track and a move goes back to reordering.
    std::optional<std::pair<std::size_t, std::size_t>> overwriteRangeAt(std::size_t slot) const {
        if (slot >= slots.size()) return std::nullopt;
        const auto [first, last] = runBounds(slot);
        const std::size_t start = std::max(slot, first + 1);
        if (start > last) return std::nullopt;
        return std::make_pair(start, last);
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
