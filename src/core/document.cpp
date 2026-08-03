// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace animage {

Document::Document() = default;

// --- structure -----------------------------------------------------------

TimelineId Document::addTimeline(std::string name) {
    ScopedCommand command(*this, "Add timeline");

    Timeline timeline;
    timeline.id = timeline_ids_.next();
    timeline.name = std::move(name);

    const std::size_t index = scene_.timelines.size();
    scene_.timelines.push_back(std::move(timeline));

    // The op holds the state that is not live: nothing, because before this
    // command the timeline did not exist.
    recordOp(std::make_unique<TimelineOp>(index, std::nullopt));
    return scene_.timelines.back().id;
}

void Document::removeTimeline(TimelineId id) {
    auto it = std::find_if(scene_.timelines.begin(), scene_.timelines.end(),
                           [&](const Timeline& t) { return t.id == id; });
    if (it == scene_.timelines.end()) return;

    ScopedCommand command(*this, "Remove timeline");

    const std::size_t index = static_cast<std::size_t>(it - scene_.timelines.begin());
    Timeline removed = std::move(*it);
    scene_.timelines.erase(it);

    for (const auto& [image_id, image] : removed.images) {
        for (const auto& [layer_id, cel_id] : image.cels) releaseCelRef(cel_id);
    }
    recordOp(std::make_unique<TimelineOp>(index, std::move(removed)));
}

void Document::setFramerate(int framerate) {
    if (framerate <= 0 || framerate == scene_.framerate) return;
    ScopedCommand command(*this, "Set framerate");
    recordOp(std::make_unique<SceneFramerateOp>(scene_.framerate));
    scene_.framerate = framerate;
}

LayerId Document::addLayer(TimelineId timeline_id, std::string name, std::size_t index,
                           LayerKind kind) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline) return kNoId;

    ScopedCommand command(*this, "Add layer");
    recordOp(std::make_unique<LayerListOp>(timeline_id, timeline->layers));

    // Two layers with the same name is a trap: the panel becomes ambiguous and
    // so does anything that later refers to a layer by name, such as the export
    // filenames. Enforced here rather than in the interface so it holds however
    // the layer got created.
    const auto taken = [&](const std::string& candidate) {
        return std::any_of(timeline->layers.begin(), timeline->layers.end(),
                           [&](const Layer& l) { return l.name == candidate; });
    };
    std::string unique = name;
    for (int suffix = 2; taken(unique); ++suffix) {
        unique = name + " (" + std::to_string(suffix) + ")";
    }

    Layer layer;
    layer.id = layer_ids_.next();
    layer.name = std::move(unique);
    layer.kind = kind;

    const std::size_t at = std::min(index, timeline->layers.size());
    timeline->layers.insert(timeline->layers.begin() + static_cast<std::ptrdiff_t>(at), layer);

    // Note what did not happen: no Image was touched. A cel appears on first
    // use, so this costs the same on a timeline of 5 images and one of 500.
    return layer.id;
}

void Document::removeLayer(TimelineId timeline_id, LayerId layer_id) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline || !timeline->findLayer(layer_id)) return;

    ScopedCommand command(*this, "Remove layer");
    recordOp(std::make_unique<LayerListOp>(timeline_id, timeline->layers));

    for (auto& [image_id, image] : timeline->images) {
        const CelId cel_id = image.celFor(layer_id);
        if (cel_id == kNoId) continue;
        recordOp(std::make_unique<CelAssignOp>(timeline_id, image_id, layer_id, cel_id));
        image.cels.erase(layer_id);
        releaseCelRef(cel_id);
    }

    timeline->layers.erase(std::remove_if(timeline->layers.begin(), timeline->layers.end(),
                                          [&](const Layer& l) { return l.id == layer_id; }),
                           timeline->layers.end());
}

void Document::moveLayer(TimelineId timeline_id, std::size_t from, std::size_t to) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline || from >= timeline->layers.size() || to >= timeline->layers.size() ||
        from == to) {
        return;
    }

    ScopedCommand command(*this, "Move layer");
    recordOp(std::make_unique<LayerListOp>(timeline_id, timeline->layers));

    Layer moved = timeline->layers[from];
    timeline->layers.erase(timeline->layers.begin() + static_cast<std::ptrdiff_t>(from));
    timeline->layers.insert(timeline->layers.begin() + static_cast<std::ptrdiff_t>(to), moved);
}

void Document::updateLayer(TimelineId timeline_id, LayerId layer_id, const Layer& properties) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline) return;
    Layer* layer = timeline->findLayer(layer_id);
    if (!layer) return;

    ScopedCommand command(*this, "Change layer");
    recordOp(std::make_unique<LayerListOp>(timeline_id, timeline->layers));

    const LayerId keep = layer->id;
    *layer = properties;
    layer->id = keep;
}

// --- time ----------------------------------------------------------------

ImageId Document::insertImage(TimelineId timeline_id, std::size_t slot) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline) return kNoId;

    ScopedCommand command(*this, "Insert image");
    recordOp(std::make_unique<SlotsOp>(timeline_id, timeline->slots));

    Image image;
    image.id = image_ids_.next();
    image.number = timeline->next_drawing_number++;
    const ImageId id = image.id;
    recordOp(std::make_unique<ImageOp>(timeline_id, id, std::nullopt));

    timeline->images.emplace(id, std::move(image));
    const std::size_t at = std::min(slot, timeline->slots.size());
    timeline->slots.insert(timeline->slots.begin() + static_cast<std::ptrdiff_t>(at), id);
    return id;
}

void Document::extendExposure(TimelineId timeline_id, std::size_t slot, int extra) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline || slot >= timeline->slots.size() || extra <= 0) return;

    ScopedCommand command(*this, "Extend exposure");
    recordOp(std::make_unique<SlotsOp>(timeline_id, timeline->slots));

    // Exposure is nothing more than the same id appearing again. No cel, no
    // image record and no tile is created.
    const ImageId id = timeline->slots[slot];
    timeline->slots.insert(timeline->slots.begin() + static_cast<std::ptrdiff_t>(slot) + 1,
                           static_cast<std::size_t>(extra), id);
}

void Document::removeSlot(TimelineId timeline_id, std::size_t slot) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline || slot >= timeline->slots.size()) return;

    ScopedCommand command(*this, "Remove frame");
    recordOp(std::make_unique<SlotsOp>(timeline_id, timeline->slots));

    const ImageId id = timeline->slots[slot];
    timeline->slots.erase(timeline->slots.begin() + static_cast<std::ptrdiff_t>(slot));

    // The image record only goes away when no slot shows it any more.
    if (std::find(timeline->slots.begin(), timeline->slots.end(), id) != timeline->slots.end()) {
        return;
    }
    auto it = timeline->images.find(id);
    if (it == timeline->images.end()) return;

    Image removed = std::move(it->second);
    timeline->images.erase(it);
    for (const auto& [layer_id, cel_id] : removed.cels) releaseCelRef(cel_id);
    recordOp(std::make_unique<ImageOp>(timeline_id, id, std::move(removed)));
}

void Document::removeDrawing(TimelineId timeline_id, ImageId image_id) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline || image_id == kNoId) return;

    ScopedCommand command(*this, "Delete drawing");
    // Shortening a hold to nothing and deleting the drawing are the same
    // operation seen from different ends; this one means the drawing goes.
    while (true) {
        auto it = std::find(timeline->slots.begin(), timeline->slots.end(), image_id);
        if (it == timeline->slots.end()) break;
        removeSlot(timeline_id,
                   static_cast<std::size_t>(std::distance(timeline->slots.begin(), it)));
    }
}

void Document::moveDrawing(TimelineId timeline_id, ImageId image_id, std::size_t destination) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline || image_id == kNoId) return;

    // A drawing and its holds are one contiguous run, and they travel together:
    // moving a drawing without its exposure would silently change the timing.
    std::vector<ImageId> remaining;
    remaining.reserve(timeline->slots.size());
    std::size_t held = 0;
    for (ImageId id : timeline->slots) {
        if (id == image_id) {
            ++held;
        } else {
            remaining.push_back(id);
        }
    }
    if (held == 0) return;

    const std::size_t at = std::min(destination, remaining.size());
    std::vector<ImageId> moved = remaining;
    moved.insert(moved.begin() + static_cast<std::ptrdiff_t>(at), held, image_id);
    if (moved == timeline->slots) return;

    ScopedCommand command(*this, "Move drawing");
    recordOp(std::make_unique<SlotsOp>(timeline_id, timeline->slots));
    timeline->slots = std::move(moved);
}

ImageId Document::duplicateImage(TimelineId timeline_id, std::size_t slot) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline || slot >= timeline->slots.size()) return kNoId;
    const Image* source = timeline->findImage(timeline->slots[slot]);
    if (!source) return kNoId;

    ScopedCommand command(*this, "Duplicate image");

    Image copy;
    copy.id = image_ids_.next();
    copy.number = timeline->next_drawing_number++;  // a copy is a new drawing
    copy.marker = source->marker;
    for (const auto& [layer_id, cel_id] : source->cels) {
        const Cel* original = cel(cel_id);
        if (!original) continue;
        const CelId fresh = createCelCopy(*original);
        copy.cels[layer_id] = fresh;
        addCelRef(fresh);
    }

    const ImageId id = copy.id;
    recordOp(std::make_unique<SlotsOp>(timeline_id, timeline->slots));
    recordOp(std::make_unique<ImageOp>(timeline_id, id, std::nullopt));

    timeline->images.emplace(id, std::move(copy));
    timeline->slots.insert(timeline->slots.begin() + static_cast<std::ptrdiff_t>(slot) + 1, id);
    return id;
}

// --- pixels --------------------------------------------------------------

const Cel* Document::cel(CelId id) const {
    auto it = cels_.find(id);
    return (it == cels_.end()) ? nullptr : it->second.get();
}

const Cel* Document::celAt(TimelineId timeline_id, ImageId image_id, LayerId layer_id) const {
    const Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline) return nullptr;
    const Image* image = timeline->findImage(image_id);
    if (!image) return nullptr;
    return cel(image->celFor(layer_id));
}

Cel* Document::celForWriting(TimelineId timeline_id, ImageId image_id, LayerId layer_id) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline) return nullptr;
    Image* image = timeline->findImage(image_id);
    if (!image) return nullptr;

    CelId cel_id = image->celFor(layer_id);
    if (cel_id == kNoId) {
        cel_id = createCel();
        recordOp(std::make_unique<CelAssignOp>(timeline_id, image_id, layer_id, kNoId));
        image->cels[layer_id] = cel_id;
        addCelRef(cel_id);
    }

    auto it = cels_.find(cel_id);
    return (it == cels_.end()) ? nullptr : it->second.get();
}

void Document::clearCel(TimelineId timeline_id, ImageId image_id, LayerId layer_id) {
    Timeline* timeline = scene_.findTimeline(timeline_id);
    if (!timeline) return;
    Image* image = timeline->findImage(image_id);
    if (!image) return;

    const CelId cel_id = image->celFor(layer_id);
    if (cel_id == kNoId) return;

    ScopedCommand command(*this, "Clear layer");
    recordOp(std::make_unique<CelAssignOp>(timeline_id, image_id, layer_id, cel_id));
    image->cels.erase(layer_id);
    releaseCelRef(cel_id);
}

std::size_t Document::totalTileCount() const {
    std::size_t total = 0;
    for (const auto& [id, cel] : cels_) total += cel->tiles().tileCount();
    return total;
}

CelId Document::createCel() {
    const CelId id = cel_ids_.next();
    cels_.emplace(id, std::make_shared<Cel>(id));
    return id;
}

CelId Document::createCelCopy(const Cel& source) {
    const CelId id = cel_ids_.next();
    cels_.emplace(id, std::make_shared<Cel>(id, source.tiles()));
    return id;
}

void Document::addCelRef(CelId id) {
    auto it = cels_.find(id);
    if (it != cels_.end()) it->second->addImageRef();
}

void Document::releaseCelRef(CelId id) {
    auto it = cels_.find(id);
    if (it != cels_.end()) it->second->releaseImageRef();
}

// --- history -------------------------------------------------------------

void Document::beginCommand(std::string label) {
    if (command_depth_++ == 0) {
        pending_ = Command{};
        pending_.label = std::move(label);
    }
}

void Document::endCommand() {
    if (command_depth_ == 0) return;
    if (--command_depth_ > 0) return;

    pending_.tiles = journal_.take();
    if (!pending_.empty()) {
        undo_stack_.push_back(std::move(pending_));
        redo_stack_.clear();
    }
    pending_ = Command{};
}

void Document::recordOp(std::unique_ptr<Op> op) { pending_.ops.push_back(std::move(op)); }

std::string Document::undoLabel() const {
    return undo_stack_.empty() ? std::string{} : undo_stack_.back().label;
}

bool Document::undo() {
    if (undo_stack_.empty()) return false;
    Command command = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    // Tiles first: the cel they belong to may be about to be detached by the
    // structural ops below, and it has to still be reachable now.
    for (TileSnapshot& snapshot : command.tiles) {
        auto it = cels_.find(snapshot.cel);
        if (it != cels_.end()) it->second->swapTile(snapshot.coord, snapshot.tile);
    }
    for (auto it = command.ops.rbegin(); it != command.ops.rend(); ++it) {
        (*it)->applySwap(*this);
    }

    redo_stack_.push_back(std::move(command));
    return true;
}

bool Document::redo() {
    if (redo_stack_.empty()) return false;
    Command command = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    for (auto& op : command.ops) op->applySwap(*this);
    for (TileSnapshot& snapshot : command.tiles) {
        auto it = cels_.find(snapshot.cel);
        if (it != cels_.end()) it->second->swapTile(snapshot.coord, snapshot.tile);
    }

    undo_stack_.push_back(std::move(command));
    return true;
}

void Document::clearHistory() {
    undo_stack_.clear();
    redo_stack_.clear();
    // Cels the history was keeping alive can go now.
    collectGarbage();
}

bool Document::historyReferences(CelId id) const {
    std::vector<CelId> ids;
    auto scan = [&](const std::vector<Command>& stack) {
        for (const Command& command : stack) {
            for (const TileSnapshot& snapshot : command.tiles) {
                if (snapshot.cel == id) return true;
            }
            for (const auto& op : command.ops) {
                ids.clear();
                op->collectCelIds(ids);
                if (std::find(ids.begin(), ids.end(), id) != ids.end()) return true;
            }
        }
        return false;
    };
    return scan(undo_stack_) || scan(redo_stack_);
}

std::size_t Document::collectGarbage() {
    std::size_t removed = 0;
    for (auto it = cels_.begin(); it != cels_.end();) {
        const bool orphaned = it->second->imageRefcount() <= 0;
        if (orphaned && !historyReferences(it->first)) {
            it = cels_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

}  // namespace animage
