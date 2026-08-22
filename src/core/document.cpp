// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace animage {

Document::Document() = default;

// --- structure -----------------------------------------------------------

TrackId Document::addTrack(std::string name) {
    ScopedCommand command(*this, "Add track");

    Track track;
    track.id = track_ids_.next();
    track.name = std::move(name);

    const std::size_t index = scene_.tracks.size();
    scene_.tracks.push_back(std::move(track));

    // The op holds the state that is not live: nothing, because before this
    // command the track did not exist.
    recordOp(std::make_unique<TrackOp>(index, std::nullopt));
    return scene_.tracks.back().id;
}

void Document::removeTrack(TrackId id) {
    auto it = std::find_if(scene_.tracks.begin(), scene_.tracks.end(),
                           [&](const Track& t) { return t.id == id; });
    if (it == scene_.tracks.end()) return;

    ScopedCommand command(*this, "Remove track");

    const std::size_t index = static_cast<std::size_t>(it - scene_.tracks.begin());
    Track removed = std::move(*it);
    scene_.tracks.erase(it);

    for (const auto& [image_id, image] : removed.images) {
        for (const auto& [layer_id, cel_id] : image.cels) releaseCelRef(cel_id);
    }
    recordOp(std::make_unique<TrackOp>(index, std::move(removed)));
}

void Document::updateTrack(TrackId track_id, const TrackProperties& properties) {
    Track* track = scene_.findTrack(track_id);
    if (!track) return;

    ScopedCommand command(*this, "Change track");
    recordOp(std::make_unique<TrackPropsOp>(track_id, track->properties()));
    track->setProperties(properties);
}

void Document::moveTrack(std::size_t from, std::size_t to) {
    if (from >= scene_.tracks.size() || to >= scene_.tracks.size() || from == to) return;

    ScopedCommand command(*this, "Move track");
    // The move that undoes this one, recorded before it happens as every other
    // op is. Two numbers rather than a copy of the track list: restacking moves
    // no drawing, and a track carries every Image record it has.
    recordOp(std::make_unique<TrackOrderOp>(to, from));

    Track moved = std::move(scene_.tracks[from]);
    scene_.tracks.erase(scene_.tracks.begin() + static_cast<std::ptrdiff_t>(from));
    scene_.tracks.insert(scene_.tracks.begin() + static_cast<std::ptrdiff_t>(to),
                         std::move(moved));
}

void Document::setFramerate(int framerate) {
    if (framerate <= 0 || framerate == scene_.framerate) return;
    ScopedCommand command(*this, "Set framerate");
    recordOp(std::make_unique<SceneFramerateOp>(scene_.framerate));
    scene_.framerate = framerate;
}

void Document::setSceneLength(bool fixed, int frames) {
    const int wanted = std::max(0, frames);
    if (fixed == scene_.fixed_length && wanted == scene_.length) return;
    ScopedCommand command(*this, "Scene length");
    recordOp(std::make_unique<SceneLengthOp>(scene_.fixed_length, scene_.length));
    scene_.fixed_length = fixed;
    scene_.length = wanted;
}

void Document::loadScene(Scene scene) {
    scene_ = std::move(scene);
    cels_.clear();
    ctg_cache_.clear();
    ctg_carries_.clear();
    carried_marks_.clear();
    undo_stack_.clear();
    redo_stack_.clear();
    pending_ = Command{};
    command_depth_ = 0;
    journal_.take();

    std::uint64_t highest_track = 0;
    std::uint64_t highest_layer = 0;
    std::uint64_t highest_image = 0;
    std::uint64_t highest_cel = 0;

    for (const Track& track : scene_.tracks) {
        highest_track = std::max(highest_track, track.id);
        for (const Layer& layer : track.layers) highest_layer = std::max(highest_layer, layer.id);

        for (const auto& [image_id, image] : track.images) {
            highest_image = std::max(highest_image, image_id);
            for (const auto& [layer_id, cel_id] : image.cels) {
                if (cel_id == kNoId) continue;
                highest_cel = std::max(highest_cel, cel_id);
                auto found = cels_.find(cel_id);
                if (found == cels_.end()) {
                    found = cels_.emplace(cel_id, std::make_shared<Cel>(cel_id)).first;
                }
                // Counted from the scene, not read from the file: a refcount
                // that disagrees with the images is a leak or a crash, and the
                // images are the thing that is true.
                found->second->addImageRef();
            }
        }
    }

    cel_ids_.resumeAfter(highest_cel);
    image_ids_.resumeAfter(highest_image);
    layer_ids_.resumeAfter(highest_layer);
    track_ids_.resumeAfter(highest_track);
}

bool Document::setCelTiles(CelId id, TileGrid tiles) {
    auto found = cels_.find(id);
    if (found == cels_.end()) return false;
    const int refs = found->second->imageRefcount();
    found->second = std::make_shared<Cel>(id, std::move(tiles));
    for (int i = 0; i < refs; ++i) found->second->addImageRef();
    return true;
}

void Document::setCanvasSize(int width, int height) {
    const int w = std::clamp(width, kMinCanvasSide, kMaxCanvasSide);
    const int h = std::clamp(height, kMinCanvasSide, kMaxCanvasSide);
    if (w == scene_.width && h == scene_.height) return;

    ScopedCommand command(*this, "Canvas size");
    recordOp(std::make_unique<SceneCanvasOp>(scene_.width, scene_.height));
    scene_.width = w;
    scene_.height = h;
}

LayerId Document::addLayer(TrackId track_id, std::string name, std::size_t index,
                           LayerKind kind) {
    Track* track = scene_.findTrack(track_id);
    if (!track) return kNoId;

    ScopedCommand command(*this, "Add layer");
    recordOp(std::make_unique<LayerListOp>(track_id, track->layers));

    // Two layers with the same name is a trap: the panel becomes ambiguous and
    // so does anything that later refers to a layer by name, such as the export
    // filenames. Enforced here rather than in the interface so it holds however
    // the layer got created.
    const auto taken = [&](const std::string& candidate) {
        return std::any_of(track->layers.begin(), track->layers.end(),
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

    const std::size_t at = std::min(index, track->layers.size());
    track->layers.insert(track->layers.begin() + static_cast<std::ptrdiff_t>(at), layer);

    // Note what did not happen: no Image was touched. A cel appears on first
    // use, so this costs the same on a track of 5 images and one of 500.
    return layer.id;
}

void Document::removeLayer(TrackId track_id, LayerId layer_id) {
    Track* track = scene_.findTrack(track_id);
    if (!track || !track->findLayer(layer_id)) return;

    ScopedCommand command(*this, "Remove layer");
    recordOp(std::make_unique<LayerListOp>(track_id, track->layers));

    for (auto& [image_id, image] : track->images) {
        const CelId cel_id = image.celFor(layer_id);
        if (cel_id == kNoId) continue;
        recordOp(std::make_unique<CelAssignOp>(track_id, image_id, layer_id, cel_id));
        image.cels.erase(layer_id);
        releaseCelRef(cel_id);
    }

    track->layers.erase(std::remove_if(track->layers.begin(), track->layers.end(),
                                          [&](const Layer& l) { return l.id == layer_id; }),
                           track->layers.end());
}

void Document::moveLayer(TrackId track_id, std::size_t from, std::size_t to) {
    Track* track = scene_.findTrack(track_id);
    if (!track || from >= track->layers.size() || to >= track->layers.size() ||
        from == to) {
        return;
    }

    ScopedCommand command(*this, "Move layer");
    recordOp(std::make_unique<LayerListOp>(track_id, track->layers));

    Layer moved = track->layers[from];
    track->layers.erase(track->layers.begin() + static_cast<std::ptrdiff_t>(from));
    track->layers.insert(track->layers.begin() + static_cast<std::ptrdiff_t>(to), moved);
}

void Document::updateLayer(TrackId track_id, LayerId layer_id, const Layer& properties) {
    Track* track = scene_.findTrack(track_id);
    if (!track) return;
    Layer* layer = track->findLayer(layer_id);
    if (!layer) return;

    ScopedCommand command(*this, "Change layer");
    recordOp(std::make_unique<LayerListOp>(track_id, track->layers));

    const LayerId keep = layer->id;
    *layer = properties;
    layer->id = keep;
}

// --- time ----------------------------------------------------------------

ImageId Document::insertImage(TrackId track_id, std::size_t slot) {
    Track* track = scene_.findTrack(track_id);
    if (!track) return kNoId;

    ScopedCommand command(*this, "Insert image");
    recordOp(std::make_unique<SlotsOp>(track_id, track->slots));

    Image image;
    image.id = image_ids_.next();
    image.number = track->nextDrawingNumber();
    const ImageId id = image.id;
    recordOp(std::make_unique<ImageOp>(track_id, id, std::nullopt));

    track->images.emplace(id, std::move(image));
    const std::size_t at = std::min(slot, track->slots.size());
    track->slots.insert(track->slots.begin() + static_cast<std::ptrdiff_t>(at), id);
    return id;
}

void Document::extendExposure(TrackId track_id, std::size_t slot, int extra) {
    Track* track = scene_.findTrack(track_id);
    if (!track || slot >= track->slots.size() || extra <= 0) return;

    ScopedCommand command(*this, "Extend exposure");
    recordOp(std::make_unique<SlotsOp>(track_id, track->slots));

    // Exposure is nothing more than the same id appearing again. No cel, no
    // image record and no tile is created.
    const ImageId id = track->slots[slot];
    track->slots.insert(track->slots.begin() + static_cast<std::ptrdiff_t>(slot) + 1,
                           static_cast<std::size_t>(extra), id);
}

void Document::removeSlot(TrackId track_id, std::size_t slot) {
    Track* track = scene_.findTrack(track_id);
    if (!track || slot >= track->slots.size()) return;

    ScopedCommand command(*this, "Remove frame");
    recordOp(std::make_unique<SlotsOp>(track_id, track->slots));

    const ImageId id = track->slots[slot];
    track->slots.erase(track->slots.begin() + static_cast<std::ptrdiff_t>(slot));

    // The image record only goes away when no slot shows it any more.
    if (std::find(track->slots.begin(), track->slots.end(), id) != track->slots.end()) {
        return;
    }
    auto it = track->images.find(id);
    if (it == track->images.end()) return;

    Image removed = std::move(it->second);
    track->images.erase(it);
    for (const auto& [layer_id, cel_id] : removed.cels) releaseCelRef(cel_id);
    recordOp(std::make_unique<ImageOp>(track_id, id, std::move(removed)));
}

void Document::removeDrawing(TrackId track_id, ImageId image_id) {
    Track* track = scene_.findTrack(track_id);
    if (!track || image_id == kNoId) return;

    ScopedCommand command(*this, "Delete drawing");
    // Shortening a hold to nothing and deleting the drawing are the same
    // operation seen from different ends; this one means the drawing goes.
    while (true) {
        auto it = std::find(track->slots.begin(), track->slots.end(), image_id);
        if (it == track->slots.end()) break;
        removeSlot(track_id,
                   static_cast<std::size_t>(std::distance(track->slots.begin(), it)));
    }
}

void Document::moveDrawing(TrackId track_id, ImageId image_id, std::size_t destination) {
    Track* track = scene_.findTrack(track_id);
    if (!track || image_id == kNoId) return;

    // A drawing and its holds are one contiguous run, and they travel together:
    // moving a drawing without its exposure would silently change the timing.
    std::vector<ImageId> remaining;
    remaining.reserve(track->slots.size());
    std::size_t held = 0;
    for (ImageId id : track->slots) {
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
    if (moved == track->slots) return;

    ScopedCommand command(*this, "Move drawing");
    recordOp(std::make_unique<SlotsOp>(track_id, track->slots));
    track->slots = std::move(moved);
}

std::optional<Image> Document::copyOfImage(Track& track, ImageId source_id) {
    const Image* source = track.findImage(source_id);
    if (!source) return std::nullopt;

    Image copy;
    copy.id = image_ids_.next();
    copy.number = track.nextDrawingNumber();  // a copy is a new drawing
    copy.marker = source->marker;
    for (const auto& [layer_id, cel_id] : source->cels) {
        const Cel* original = cel(cel_id);
        if (!original) continue;
        const CelId fresh = createCelCopy(*original);
        copy.cels[layer_id] = fresh;
        addCelRef(fresh);
    }
    return copy;
}

ImageId Document::duplicateImage(TrackId track_id, std::size_t slot) {
    Track* track = scene_.findTrack(track_id);
    if (!track || slot >= track->slots.size()) return kNoId;

    ScopedCommand command(*this, "Duplicate image");
    std::optional<Image> copy = copyOfImage(*track, track->slots[slot]);
    if (!copy) return kNoId;

    const ImageId id = copy->id;
    recordOp(std::make_unique<SlotsOp>(track_id, track->slots));
    recordOp(std::make_unique<ImageOp>(track_id, id, std::nullopt));

    track->images.emplace(id, std::move(*copy));
    track->slots.insert(track->slots.begin() + static_cast<std::ptrdiff_t>(slot) + 1, id);
    return id;
}

ImageId Document::addDrawing(TrackId track_id, std::size_t slot) {
    Track* track = scene_.findTrack(track_id);
    if (!track) return kNoId;
    if (track->slots.empty()) return insertImage(track_id, 0);

    const std::size_t here = std::min(slot, track->slots.size() - 1);
    const auto range = track->overwriteRangeAt(here);

    // No overwrite, or nothing to overwrite: after the whole hold, and the track
    // is one frame longer. A hold of one frame lands here, because taking its
    // only frame is the one thing overwriting will not do.
    if (!track->overwrite_drawings || !range) {
        return insertImage(track_id, track->runBounds(here).second + 1);
    }

    ScopedCommand command(*this, "Insert image");
    recordOp(std::make_unique<SlotsOp>(track_id, track->slots));

    Image image;
    image.id = image_ids_.next();
    image.number = track->nextDrawingNumber();
    const ImageId id = image.id;
    recordOp(std::make_unique<ImageOp>(track_id, id, std::nullopt));
    track->images.emplace(id, std::move(image));

    for (std::size_t i = range->first; i <= range->second; ++i) track->slots[i] = id;
    return id;
}

ImageId Document::duplicateDrawing(TrackId track_id, std::size_t slot) {
    Track* track = scene_.findTrack(track_id);
    if (!track || track->slots.empty()) return kNoId;

    const std::size_t here = std::min(slot, track->slots.size() - 1);
    const auto range = track->overwriteRangeAt(here);

    // duplicateImage puts the copy just after the slot it is given, so the last
    // frame of the hold is what keeps the original whole.
    if (!track->overwrite_drawings || !range) {
        return duplicateImage(track_id, track->runBounds(here).second);
    }

    ScopedCommand command(*this, "Duplicate image");
    std::optional<Image> copy = copyOfImage(*track, track->slots[here]);
    if (!copy) return kNoId;

    const ImageId id = copy->id;
    recordOp(std::make_unique<SlotsOp>(track_id, track->slots));
    recordOp(std::make_unique<ImageOp>(track_id, id, std::nullopt));
    track->images.emplace(id, std::move(*copy));

    for (std::size_t i = range->first; i <= range->second; ++i) track->slots[i] = id;
    return id;
}

void Document::moveDrawingOver(TrackId track_id, ImageId image_id, std::size_t slot) {
    Track* track = scene_.findTrack(track_id);
    if (!track || image_id == kNoId || slot >= track->slots.size()) return;

    const std::size_t from = track->firstSlotOf(image_id);
    if (from >= track->slots.size()) return;
    const auto [own_first, own_last] = track->runBounds(from);

    // Dropped exactly where it already starts: nothing to do.
    if (slot == own_first) return;
    const bool inside_itself = slot > own_first && slot <= own_last;

    // Where it lands, read from the track as it stands -- *not* from a copy it
    // has already been lifted out of.
    //
    // That ordering was the bug. Lifting first hands the frames it is leaving to
    // the neighbour, so the neighbour's run measures as both runs together and
    // "the rest of the hold it lands in" swallows the lot: on `1...2....3.....`,
    // nudging drawing 1 one frame right left drawing 2 holding a single frame
    // and drawing 1 holding everything up to drawing 3. Reading the run before
    // anything moves is the whole fix.
    const auto range = track->overwriteRangeAt(slot);
    if (!range) {
        // A hold of one frame has nothing to spare, so this becomes the reorder
        // it would have been on a track that does not overwrite.
        std::size_t destination = 0;
        for (std::size_t i = 0; i < slot; ++i) {
            if (track->slots[i] != image_id) ++destination;
        }
        moveDrawing(track_id, image_id, destination);
        return;
    }

    std::vector<ImageId> moved = track->slots;
    for (std::size_t i = range->first; i <= range->second; ++i) moved[i] = image_id;

    if (inside_itself) {
        // Dragged along its own hold, which means "start here". The frames
        // before the drop belong to the drawing before this one now, so the
        // hold in front simply grows by what this one gave up.
        //
        // Unless there is no drawing in front, which is the whole of the
        // reported bug: the first drawing of a track has nowhere to put the
        // frames it would be vacating, so it cannot move forward and nothing
        // happens. Every other drawing can.
        const ImageId before = (own_first > 0) ? moved[own_first - 1] : kNoId;
        if (before == kNoId || before == image_id) return;
        for (std::size_t i = own_first; i < range->first; ++i) moved[i] = before;
    } else {
        // Moved into another drawing's hold. The frames it came from are only
        // vacated when they are not already touching where it landed: a drawing
        // that ended up against its old hold simply keeps them, which is what
        // makes the issue's own example come out at nine frames and not eight.
        const bool joins = (own_last + 1 == range->first) || (range->second + 1 == own_first);
        if (!joins) {
            // Given to whichever drawing is beside them -- the one before, or
            // the one after when they were at the very start. Erasing them
            // would shorten a track whose fixed length is the point of it.
            const ImageId before = (own_first > 0) ? moved[own_first - 1] : kNoId;
            const ImageId after = (own_last + 1 < moved.size()) ? moved[own_last + 1] : kNoId;
            const ImageId filler = (before != kNoId && before != image_id) ? before
                                   : (after != kNoId && after != image_id) ? after
                                                                           : kNoId;
            if (filler == kNoId) return;  // the only drawing in the track
            for (std::size_t i = own_first; i <= own_last; ++i) moved[i] = filler;
        }
    }
    if (moved == track->slots) return;

    // Nothing here can retire a drawing, so nothing here has to tidy one away.
    // The frames a drawing leaves are always taken by a drawing that is still in
    // the track, and the run it lands in always keeps its first frame -- so
    // every drawing that was in `slots` is still in it. There is a test.
    ScopedCommand command(*this, "Move drawing");
    recordOp(std::make_unique<SlotsOp>(track_id, track->slots));
    track->slots = std::move(moved);
}

// --- pixels --------------------------------------------------------------

const Cel* Document::cel(CelId id) const {
    auto it = cels_.find(id);
    return (it == cels_.end()) ? nullptr : it->second.get();
}

const Cel* Document::celAt(TrackId track_id, ImageId image_id, LayerId layer_id) const {
    const Track* track = scene_.findTrack(track_id);
    if (!track) return nullptr;
    const Image* image = track->findImage(image_id);
    if (!image) return nullptr;
    return cel(image->celFor(layer_id));
}

const Cel* Document::ctgScribblesAt(TrackId track_id, ImageId image_id, LayerId layer_id,
                                    ImageId* source) const {
    if (source) *source = kNoId;

    const Track* track = scene_.findTrack(track_id);
    if (!track) return nullptr;
    const Layer* layer = track->findLayer(layer_id);
    if (!layer || layer->kind != LayerKind::Ctg) return nullptr;

    // With carrying switched off, absence means what it means everywhere else:
    // the layer is empty here. The drawing's own cel is still its own.
    if (!layer->ctg_inherit) {
        const Image* here = track->findImage(image_id);
        if (!here) return nullptr;
        const Cel* own = cel(here->celFor(layer_id));
        if (own && source) *source = image_id;
        return own;
    }

    const int direction = (layer->ctg_direction == CtgDirection::Backward)  ? +1
                          : (layer->ctg_direction == CtgDirection::Nearest) ? 0
                                                                            : -1;
    const ImageId from = track->celSourceFor(image_id, layer_id, direction);
    if (from == kNoId) return nullptr;

    const Image* record = track->findImage(from);
    if (!record) return nullptr;
    const Cel* found = cel(record->celFor(layer_id));
    if (found && source) *source = from;
    return found;
}

Cel* Document::celForWriting(TrackId track_id, ImageId image_id, LayerId layer_id) {
    Track* track = scene_.findTrack(track_id);
    if (!track) return nullptr;
    Image* image = track->findImage(image_id);
    if (!image) return nullptr;

    CelId cel_id = image->celFor(layer_id);
    if (cel_id == kNoId) {
        // "Unless the user changes them", in one place. A CTG layer with no cel
        // of its own is showing an earlier drawing's scribbles, so the first
        // mark made here has to start from those: creating an empty cel instead
        // would make one stroke silently throw away every inherited mark, and
        // the colour would vanish from the drawing you were adding to.
        //
        // Copy-on-write makes this a copy of tile handles rather than of
        // pixels, the same mechanism that makes duplicateImage nearly free. Two
        // things fall out of it and neither needs its own concept: erasing an
        // inherited mark works, because you are editing your own copy of it,
        // and reverting is clearCel, because absence is what inheriting means.
        // Copied where they were being *shown*, not where they were drawn. On a
        // layer that moves carried marks, those are not the same place, and
        // taking a drawing over has to hand you the drawing you were looking
        // at: copying the marks unmoved would put them back where the drawing
        // no longer is, undo the fill you could see, and do it in response to a
        // stroke somewhere else entirely.
        const Cel* inherited = ctgScribblesAt(track_id, image_id, layer_id);
        const CarriedMarks carried = ctgCarriedMarksAt(track_id, image_id, layer_id);
        const bool moved = carried.tiles != nullptr &&
                           (!carried.offset.isZero() ||
                            (inherited && carried.tiles != &inherited->tiles()));
        if (moved) {
            cel_id = createCel();
            auto made = cels_.find(cel_id);
            if (made != cels_.end()) {
                made->second->adoptTiles(
                    translated(*carried.tiles, carried.offset.x, carried.offset.y));
            }
        } else {
            cel_id = inherited ? createCelCopy(*inherited) : createCel();
        }

        recordOp(std::make_unique<CelAssignOp>(track_id, image_id, layer_id, kNoId));
        image->cels[layer_id] = cel_id;
        addCelRef(cel_id);

        // The marks are this drawing's own now, and they are where they were
        // being shown. Anything that goes on carrying them carries them twice.
        ctg_carries_.erase(CtgKey{image_id, layer_id});
        carried_marks_.erase(CtgKey{image_id, layer_id});
    }

    auto it = cels_.find(cel_id);
    return (it == cels_.end()) ? nullptr : it->second.get();
}

void Document::clearCel(TrackId track_id, ImageId image_id, LayerId layer_id) {
    Track* track = scene_.findTrack(track_id);
    if (!track) return;
    Image* image = track->findImage(image_id);
    if (!image) return;

    const CelId cel_id = image->celFor(layer_id);
    if (cel_id == kNoId) return;

    ScopedCommand command(*this, "Clear layer");
    recordOp(std::make_unique<CelAssignOp>(track_id, image_id, layer_id, cel_id));
    image->cels.erase(layer_id);
    releaseCelRef(cel_id);
}

// The track is not part of the key: ImageIds come from one counter per
// document, so a drawing names itself unambiguously without it. It stays in the
// signature because every caller has one and reads better for saying so.
const CtgFill* Document::ctgFillFor(TrackId, ImageId image_id, LayerId layer_id) const {
    const CtgFill* found = ctg_cache_.find(CtgKey{image_id, layer_id});
    return (found && found->valid) ? found : nullptr;
}

void Document::setCtgCarry(const CtgKey& key, const CtgWarp& warp) {
    ctg_carries_[key] = warp;
    carried_marks_.erase(key);
}

const CtgWarp& Document::ctgCarryAt(ImageId image_id, LayerId layer_id) const {
    static const CtgWarp kStayedPut;
    auto found = ctg_carries_.find(CtgKey{image_id, layer_id});
    return (found == ctg_carries_.end()) ? kStayedPut : found->second;
}

Document::CarriedMarks Document::ctgCarriedMarksAt(TrackId track_id, ImageId image_id,
                                                   LayerId layer_id) const {
    ImageId from = kNoId;
    const Cel* scribbles = ctgScribblesAt(track_id, image_id, layer_id, &from);
    if (!scribbles) return {};

    // A drawing's own marks are where they are. Moving them again would move
    // the stroke being made: a scribble in progress is shown through this path,
    // and a warp left over from before the drawing took its marks over put the
    // pen's own line half a screen away from the pen.
    if (from == image_id) return {&scribbles->tiles(), {}};

    const CtgWarp& warp = ctgCarryAt(image_id, layer_id);
    if (warp.isUniform()) return {&scribbles->tiles(), warp.overall};

    const CtgKey key{image_id, layer_id};
    auto found = carried_marks_.find(key);
    if (found != carried_marks_.end() &&
        (found->second.cel != scribbles->id() ||
         found->second.revision != scribbles->revision())) {
        carried_marks_.erase(found);
        found = carried_marks_.end();
    }
    if (found == carried_marks_.end()) {
        if (carried_marks_.size() >= kCarriedMarksKept) carried_marks_.clear();
        found = carried_marks_
                    .emplace(key, CarriedMarksEntry{scribbles->id(), scribbles->revision(),
                                                    ctgCarriedMarks(scribbles->tiles(), warp)})
                    .first;
    }
    return {&found->second.tiles, {}};
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

// A tile the command emptied is dropped here, at the end of it, rather than by
// whoever wrote the last transparent pixel: a stroke crosses one tile many
// times and only the whole command knows when the writing stopped. Doing it per
// dab would also mean freeing a tile the next dab immediately allocates again.
//
// Undo needs nothing extra. The journal recorded what was in the tile before
// the first write of the command, so putting that back is what it was always
// going to do -- and redo swaps the absence back in, because swapTile is its
// own inverse whether or not either side is there.
//
// An entry that found nothing and left nothing is then not a change at all, and
// is dropped rather than being carried in the history: rubbing out over blank
// paper used to leave an undo step that put an empty tile back.
void Document::releaseEmptiedTiles(std::vector<TileSnapshot>& tiles) {
    std::vector<TileSnapshot> kept;
    kept.reserve(tiles.size());
    for (TileSnapshot& snapshot : tiles) {
        auto it = cels_.find(snapshot.cel);
        const Cel* cel = (it == cels_.end()) ? nullptr : it->second.get();
        if (it != cels_.end()) it->second->releaseIfEmpty(snapshot.coord);

        const bool absent_now = !cel || !cel->tiles().find(snapshot.coord);
        if (absent_now && !snapshot.tile) continue;
        kept.push_back(std::move(snapshot));
    }
    tiles = std::move(kept);
}

void Document::endCommand() {
    if (command_depth_ == 0) return;
    if (--command_depth_ > 0) return;

    pending_.tiles = journal_.take();
    releaseEmptiedTiles(pending_.tiles);
    if (!pending_.empty()) {
        pending_.stamp = ++command_stamps_;
        undo_stack_.push_back(std::move(pending_));
        redo_stack_.clear();
        // The only moment the history grows. Undo and redo move a command from
        // one stack to the other and free nothing, so nothing else can put it
        // over its budget.
        trimHistory();
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

std::uint64_t Document::historyStamp() const {
    return undo_stack_.empty() ? 0 : undo_stack_.back().stamp;
}

std::size_t Document::historyBytes() const {
    std::size_t bytes = 0;
    for (const Command& command : undo_stack_) bytes += command.retainedBytes();
    for (const Command& command : redo_stack_) bytes += command.retainedBytes();
    return bytes;
}

void Document::setHistoryBudget(std::size_t bytes) {
    history_budget_ = bytes;
    trimHistory();
}

// The oldest end of the undo stack is what goes, because it is the part of the
// history furthest from being wanted and because everything above it has to
// stay reachable: undo is a walk backwards and a hole in it is not a history.
//
// The redo stack is never touched. Everything on it is *newer* than everything
// on the undo stack -- it is the branch that was undone away from -- so dropping
// from there would take the recent work and leave the old.
void Document::trimHistory() {
    std::size_t bytes = historyBytes();
    std::size_t steps = undo_stack_.size() + redo_stack_.size();

    std::size_t drop = 0;
    // Never the last one. A single 4K transform can be most of the budget on
    // its own, and a history that dropped the command it had just recorded
    // would make the edit you are looking at the one thing you cannot take back.
    while (drop + 1 < undo_stack_.size() && (bytes > history_budget_ || steps > kHistoryStepCap)) {
        bytes -= undo_stack_[drop].retainedBytes();
        --steps;
        ++drop;
    }
    if (drop == 0) return;

    // Those commands are exactly what was holding the cels the collector could
    // not take -- a deleted drawing's cel outlives the deletion for as long as
    // undo can bring it back. Only the ids they mentioned can have become
    // collectable, so an orphan among them is what decides whether the scan is
    // worth running at all: collectGarbage is O(cels x history) and this runs
    // at the end of a stroke.
    std::vector<CelId> mentioned;
    for (std::size_t i = 0; i < drop; ++i) {
        for (const TileSnapshot& snapshot : undo_stack_[i].tiles) mentioned.push_back(snapshot.cel);
        for (const auto& op : undo_stack_[i].ops) op->collectCelIds(mentioned);
    }

    undo_stack_.erase(undo_stack_.begin(), undo_stack_.begin() + static_cast<std::ptrdiff_t>(drop));

    for (CelId id : mentioned) {
        auto it = cels_.find(id);
        if (it == cels_.end() || it->second->imageRefcount() > 0) continue;
        collectGarbage();
        break;
    }
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
