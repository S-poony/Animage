// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cel.h"
#include "image.h"
#include "layer.h"
#include "timeline.h"

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

// Swaps the whole layer list of a timeline. Covers add, remove, reorder and
// property changes in one type. It costs a copy of the layer list, which holds
// no pixels and is a handful of entries, so adding a layer stays O(layers) --
// independent of how many images the timeline has.
class LayerListOp final : public Op {
public:
    LayerListOp(TimelineId timeline, std::vector<Layer> layers)
        : timeline_(timeline), layers_(std::move(layers)) {}
    void applySwap(Document& doc) override;

private:
    TimelineId timeline_;
    std::vector<Layer> layers_;
};

// Swaps the slots vector: exposure changes, inserts, deletions, reordering.
class SlotsOp final : public Op {
public:
    SlotsOp(TimelineId timeline, std::vector<ImageId> slots)
        : timeline_(timeline), slots_(std::move(slots)) {}
    void applySwap(Document& doc) override;

private:
    TimelineId timeline_;
    std::vector<ImageId> slots_;
};

// Swaps one Image record in or out. An empty optional means the image is not
// present, so this single type covers creating and deleting images. Applying it
// fixes up cel refcounts on both sides.
class ImageOp final : public Op {
public:
    ImageOp(TimelineId timeline, ImageId image, std::optional<Image> state)
        : timeline_(timeline), image_(image), state_(std::move(state)) {}
    void applySwap(Document& doc) override;
    void collectCelIds(std::vector<CelId>& out) const override;

private:
    TimelineId timeline_;
    ImageId image_;
    std::optional<Image> state_;
};

// Swaps which cel a (image, layer) pair points at. kNoId means no cel, which is
// what a layer looks like before the first stroke creates one lazily.
class CelAssignOp final : public Op {
public:
    CelAssignOp(TimelineId timeline, ImageId image, LayerId layer, CelId cel)
        : timeline_(timeline), image_(image), layer_(layer), cel_(cel) {}
    void applySwap(Document& doc) override;
    void collectCelIds(std::vector<CelId>& out) const override;

    CelId cel() const { return cel_; }

private:
    TimelineId timeline_;
    ImageId image_;
    LayerId layer_;
    CelId cel_;
};

// Inserts or extracts a whole timeline at an index.
class TimelineOp final : public Op {
public:
    TimelineOp(std::size_t index, std::optional<Timeline> state)
        : index_(index), state_(std::move(state)) {}
    void applySwap(Document& doc) override;
    void collectCelIds(std::vector<CelId>& out) const override;

private:
    std::size_t index_;
    std::optional<Timeline> state_;
};

class SceneFramerateOp final : public Op {
public:
    explicit SceneFramerateOp(int framerate) : framerate_(framerate) {}
    void applySwap(Document& doc) override;

private:
    int framerate_;
};

struct Command {
    std::string label;
    std::vector<std::unique_ptr<Op>> ops;
    std::vector<TileSnapshot> tiles;

    bool empty() const { return ops.empty() && tiles.empty(); }
};

}  // namespace animage
