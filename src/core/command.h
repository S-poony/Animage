// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "audio_track.h"
#include "cel.h"
#include "image.h"
#include "layer.h"
#include "track.h"

namespace animage {

class Document;

// Every operation is written to be its own inverse: it swaps the state it holds
// with the state in the document. Undo and redo are then the same code running
// in opposite order, which removes a whole class of "redo does not quite undo
// the undo" bugs.
class Op {
public:
    virtual ~Op() = default;
    virtual void applySwap(Document& doc) = 0;

    // Cels this operation could bring back. The garbage collector treats them
    // as live even when no image references them any more.
    virtual void collectCelIds(std::vector<CelId>& out) const { (void)out; }
};

// Swaps the whole layer list of a track. Covers add, remove, reorder and
// property changes in one type. It costs a copy of the layer list, which holds
// no pixels and is a handful of entries, so adding a layer stays O(layers) --
// independent of how many images the track has.
class LayerListOp final : public Op {
public:
    LayerListOp(TrackId track, std::vector<Layer> layers)
        : track_(track), layers_(std::move(layers)) {}
    void applySwap(Document& doc) override;

private:
    TrackId track_;
    std::vector<Layer> layers_;
};

// Swaps the slots vector: exposure changes, inserts, deletions, reordering.
class SlotsOp final : public Op {
public:
    SlotsOp(TrackId track, std::vector<ImageId> slots)
        : track_(track), slots_(std::move(slots)) {}
    void applySwap(Document& doc) override;

private:
    TrackId track_;
    std::vector<ImageId> slots_;
};

// Swaps one Image record in or out. An empty optional means the image is not
// present, so this single type covers creating and deleting images. Applying it
// fixes up cel refcounts on both sides.
class ImageOp final : public Op {
public:
    ImageOp(TrackId track, ImageId image, std::optional<Image> state)
        : track_(track), image_(image), state_(std::move(state)) {}
    void applySwap(Document& doc) override;
    void collectCelIds(std::vector<CelId>& out) const override;

private:
    TrackId track_;
    ImageId image_;
    std::optional<Image> state_;
};

// Swaps which cel a (image, layer) pair points at. kNoId means no cel, which is
// what a layer looks like before the first stroke creates one lazily.
class CelAssignOp final : public Op {
public:
    CelAssignOp(TrackId track, ImageId image, LayerId layer, CelId cel)
        : track_(track), image_(image), layer_(layer), cel_(cel) {}
    void applySwap(Document& doc) override;
    void collectCelIds(std::vector<CelId>& out) const override;

    CelId cel() const { return cel_; }

private:
    TrackId track_;
    ImageId image_;
    LayerId layer_;
    CelId cel_;
};

// Swaps a track's group-level properties: its name, its opacity, its blend, its
// time offset and whether it overwrites drawings. Not its layers, its slots or
// its images, each of which has an operation of its own.
class TrackPropsOp final : public Op {
public:
    TrackPropsOp(TrackId track, TrackProperties state)
        : track_(track), state_(std::move(state)) {}
    void applySwap(Document& doc) override;

private:
    TrackId track_;
    TrackProperties state_;
};

// Inserts or extracts a whole track at an index.
class TrackOp final : public Op {
public:
    TrackOp(std::size_t index, std::optional<Track> state)
        : index_(index), state_(std::move(state)) {}
    void applySwap(Document& doc) override;
    void collectCelIds(std::vector<CelId>& out) const override;

private:
    std::size_t index_;
    std::optional<Track> state_;
};

// A soundtrack entering or leaving the scene. Its own inverse: applying it
// either puts the track back or takes it out, and holds the other half.
//
// **Much smaller than TrackOp and for one reason: there are no cels.** That op
// has to move refcounts on every cel of every image of the track it lifts,
// because the history holding a track is what keeps its drawings alive. A
// soundtrack holds a file name and three numbers, and its decoded samples are
// derived -- so nothing here has to be reference counted and nothing here can
// be lost by being forgotten.
class AudioTrackOp final : public Op {
public:
    AudioTrackOp(std::size_t index, std::optional<AudioTrack> state)
        : index_(index), state_(std::move(state)) {}
    void applySwap(Document& doc) override;

private:
    std::size_t index_;
    std::optional<AudioTrack> state_;
};

// Where a soundtrack sits in the shot, and how loud. One op for both numbers,
// because they are what the row's drag and the import dialog's box write and
// neither of those wants half a struct.
class AudioPlacementOp final : public Op {
public:
    AudioPlacementOp(TrackId track, AudioPlacement placement)
        : track_(track), placement_(placement) {}
    void applySwap(Document& doc) override;

private:
    TrackId track_;
    AudioPlacement placement_;
};

// Moves a track from one place in the stack to another. Its own inverse like
// everything else here: applying it performs the move it holds, then holds the
// move back.
//
// Deliberately not a pair of TrackOps and deliberately not "swap the whole track
// list". Both would copy every Image record in the scene to record an edit that
// touches no drawing at all -- restacking is the one structural change whose
// entire content is two numbers.
class TrackOrderOp final : public Op {
public:
    TrackOrderOp(std::size_t from, std::size_t to) : from_(from), to_(to) {}
    void applySwap(Document& doc) override;

private:
    std::size_t from_;
    std::size_t to_;
};

class SceneFramerateOp final : public Op {
public:
    explicit SceneFramerateOp(int framerate) : framerate_(framerate) {}
    void applySwap(Document& doc) override;

private:
    int framerate_;
};

// How long the shot is, and whether the scene says so at all.
class SceneLengthOp final : public Op {
public:
    SceneLengthOp(bool fixed, int length) : fixed_(fixed), length_(length) {}
    void applySwap(Document& doc) override;

private:
    bool fixed_;
    int length_;
};

// The canvas size. Undoable like everything else: resizing the picture is an
// edit to the scene, and finding out you preferred the old one is normal.
class SceneCanvasOp final : public Op {
public:
    SceneCanvasOp(int width, int height) : width_(width), height_(height) {}
    void applySwap(Document& doc) override;

private:
    int width_;
    int height_;
};

struct Command {
    std::string label;
    std::vector<std::unique_ptr<Op>> ops;
    std::vector<TileSnapshot> tiles;

    // Handed out once, when the command is pushed, and never reused. It names
    // the state the document is in rather than counting the steps that got it
    // there, which is what "changed since the last save" has to be asked with:
    // a depth answers that question wrongly when you undo a step and do a
    // different one, and cannot answer it at all now that the oldest steps can
    // be dropped from underneath it.
    std::uint64_t stamp = 0;

    bool empty() const { return ops.empty() && tiles.empty(); }

    // The pixels this command is keeping alive: the tiles it displaced, which
    // are held as TileRefs and would otherwise have been freed.
    //
    // The ops are not counted. They copy layer lists, slot vectors and Image
    // records, which are two to three orders of magnitude below a tile grid --
    // and pricing them properly would mean a virtual on every Op estimating
    // the size of containers it does not own. What bounds a history made
    // entirely of them is the step cap, not this.
    //
    // A tile is counted once per command holding it, so two commands sharing
    // one are charged for both. That over-counts only where two cels shared a
    // tile before either was written to; it trims sooner rather than later,
    // which is the safe direction for a memory cap.
    std::size_t retainedBytes() const;
};

}  // namespace animage
