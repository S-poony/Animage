// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <unordered_set>
#include <utility>

namespace animage {
namespace {

// How much decoded import is worth keeping.
//
// **Not measured, and the arithmetic is here so that it can be argued with.** A
// tile is 128 KB, so one HD frame is 135 tiles = 17 MB and one 4K frame is
// 510 = 64 MB. The gesture this exists to serve is dragging the playhead back
// and forth across a syllable, which at 24 fps over a second is 24 frames --
// 408 MB at HD. Half a gigabyte is the next round number above that, so a scrub
// of about that length holds, and one twice as long re-decodes its far end.
//
// It is a budget, so by the rule this codebase learned the hard way it will
// express itself as a threshold somewhere else: the somewhere is "how far you
// can scrub before the frame you come back to has to be decoded again", and
// decoding again is the ordinary cost of arriving at a drawing.
//
// docs/importing.md lists "what a cache bound should be, in bytes, against a
// realistic scrub" as one of the things worth benchmarking, and this is what
// stands in until it is. What would change it is somebody reporting a scrub
// that stutters at one end, with the frame size they were working at.
constexpr std::size_t kReferenceByteBudget = 512u << 20;

// What one frame weighs. Every tile of it: unlike a fill's marks, nothing here
// is a handle shared with something else that would exist anyway.
std::size_t footprintOf(const TileGrid& tiles) { return tiles.tileCount() * sizeof(Tile); }

}  // namespace

ReferenceCache::ReferenceCache() : budget_(kReferenceByteBudget) {}

const TileGrid* ReferenceCache::find(const CtgKey& key, const Transform& under) const {
    auto found = entries_.find(key);
    if (found == entries_.end()) return nullptr;
    // Absent rather than stale, and the lookup is not counted as a use in that
    // case: an entry nobody can read is not one worth keeping alive, and the
    // store that is about to replace it would then be evicting on its behalf.
    if (!(found->second.under == under)) return nullptr;
    found->second.used = ++clock_;
    return &found->second.tiles;
}

bool ReferenceCache::has(const CtgKey& key, const Transform& under) const {
    auto found = entries_.find(key);
    return found != entries_.end() && found->second.under == under;
}

void ReferenceCache::store(const CtgKey& key, const Transform& under, TileGrid tiles) {
    ++stores_;
    auto found = entries_.find(key);
    if (found != entries_.end()) {
        bytes_ -= footprintOf(found->second.tiles);
        found->second.under = under;
        found->second.tiles = std::move(tiles);
    } else {
        found = entries_.emplace(key, Entry{under, std::move(tiles), 0}).first;
    }
    found->second.used = ++clock_;
    bytes_ += footprintOf(found->second.tiles);

    evictDownToBudget(key);
}

void ReferenceCache::clear() {
    entries_.clear();
    bytes_ = 0;
    ++generation_;
}

// kNoId names no drawing, so nothing is spared: a budget being lowered is not a
// store, and there is no entry a caller is holding a reference to.
void ReferenceCache::setByteBudget(std::size_t bytes) {
    budget_ = bytes;
    evictDownToBudget(CtgKey{});
}

// Oldest first, and never the entry just stored: the caller is holding a
// reference to it. One frame can be larger than the whole budget on its own --
// a 300 dpi A4 scan is 70 MB and nothing stops a larger one -- and that is the
// case this rule quietly handles: the budget is exceeded rather than the answer
// thrown away before it is read.
void ReferenceCache::evictDownToBudget(const CtgKey& keep) {
    if (bytes_ <= budget_) return;

    std::vector<std::pair<std::uint64_t, CtgKey>> by_age;
    by_age.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        if (key == keep) continue;
        by_age.emplace_back(entry.used, key);
    }
    std::sort(by_age.begin(), by_age.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [used, key] : by_age) {
        if (bytes_ <= budget_) break;
        auto found = entries_.find(key);
        if (found == entries_.end()) continue;
        bytes_ -= footprintOf(found->second.tiles);
        entries_.erase(found);
    }
}

Document::Document() = default;

// --- structure -----------------------------------------------------------

TrackId Document::addAudioTrack(std::string name, std::string source) {
    ScopedCommand command(*this, "Import audio");

    AudioTrack track;
    track.id = track_ids_.next();
    track.name = std::move(name);
    track.source = std::move(source);

    const std::size_t index = scene_.audio_tracks.size();
    scene_.audio_tracks.push_back(std::move(track));

    recordOp(std::make_unique<AudioTrackOp>(index, std::nullopt));
    return scene_.audio_tracks.back().id;
}

TrackId Document::duplicateAudioTrack(TrackId id) {
    for (std::size_t i = 0; i < scene_.audio_tracks.size(); ++i) {
        if (scene_.audio_tracks[i].id != id) continue;

        ScopedCommand command(*this, "Duplicate soundtrack");
        AudioTrack copy = scene_.audio_tracks[i];
        copy.id = track_ids_.next();
        copy.name = copy.name + " copy";

        const std::size_t index = i + 1;
        scene_.audio_tracks.insert(
            scene_.audio_tracks.begin() + static_cast<std::ptrdiff_t>(index), std::move(copy));
        recordOp(std::make_unique<AudioTrackOp>(index, std::nullopt));
        return scene_.audio_tracks[index].id;
    }
    return kNoId;
}

void Document::removeAudioTrack(TrackId track) {
    for (std::size_t i = 0; i < scene_.audio_tracks.size(); ++i) {
        if (scene_.audio_tracks[i].id != track) continue;
        ScopedCommand command(*this, "Remove audio");
        // The samples go, and that is not part of the command. They are derived
        // from a file the save still carries, so an undo re-decodes rather than
        // restoring -- the same bargain a reference frame makes. Keeping them
        // would be keeping megabytes alive against a redo that may never come.
        audio_samples_.erase(track);
        audio_peaks_.erase(track);
        AudioTrack extracted = std::move(scene_.audio_tracks[i]);
        scene_.audio_tracks.erase(scene_.audio_tracks.begin() +
                                  static_cast<std::ptrdiff_t>(i));
        recordOp(std::make_unique<AudioTrackOp>(i, std::move(extracted)));
        return;
    }
}

void Document::renameAudioTrack(TrackId track, std::string name) {
    AudioTrack* found = scene_.findAudioTrack(track);
    if (!found || name.empty() || name == found->name) return;

    ScopedCommand command(*this, "Rename soundtrack");
    recordOp(std::make_unique<AudioNameOp>(track, found->name));
    found->name = std::move(name);
}

void Document::setAudioTrackPlacement(TrackId track, AudioPlacement placement) {
    AudioTrack* found = scene_.findAudioTrack(track);
    if (!found) return;

    placement.gain = std::clamp(placement.gain, 0.0, 1.0);
    placement.trim_start_seconds = std::max(0.0, placement.trim_start_seconds);
    placement.trim_end_seconds = std::max(0.0, placement.trim_end_seconds);

    // The trim is bounded by the sound it trims, and this is the only place
    // that can do it: the clip is derived data held here, not on the track.
    //
    // **A minimum of one frame of audio survives**, rather than zero. A sound
    // trimmed to nothing draws no block, and a row with no block is a row with
    // nothing to take hold of -- so the gesture that emptied it would be the
    // last one anybody could make on it. Undo would still work; needing it to
    // is the failure.
    if (const AudioClip* clip = audioSamplesFor(track)) {
        if (clip->rate > 0 && !clip->empty()) {
            const double whole =
                static_cast<double>(clip->frames()) / static_cast<double>(clip->rate);
            const double floor_seconds = std::min(whole, 1.0 / 24.0);
            const double room = std::max(0.0, whole - floor_seconds);
            placement.trim_start_seconds = std::min(placement.trim_start_seconds, room);
            placement.trim_end_seconds =
                std::min(placement.trim_end_seconds, room - placement.trim_start_seconds);
        }
    }

    const AudioPlacement& live = found->placement;
    if (live.offset_frames == placement.offset_frames && live.gain == placement.gain &&
        live.trim_start_seconds == placement.trim_start_seconds &&
        live.trim_end_seconds == placement.trim_end_seconds) {
        return;
    }

    ScopedCommand command(*this, "Place audio");
    recordOp(std::make_unique<AudioPlacementOp>(track, found->placement));
    found->placement = placement;
}

void Document::setAudioSamples(TrackId track, AudioClip clip) {
    // The peaks first, from the clip that is about to be moved from. Both maps
    // are written in the one call so that neither can be left describing a file
    // the other one has stopped holding.
    audio_peaks_[track] = peaksOf(clip);
    audio_samples_[track] = std::make_shared<const AudioClip>(std::move(clip));
}

const AudioClip* Document::audioSamplesFor(TrackId track) const {
    const auto it = audio_samples_.find(track);
    return it == audio_samples_.end() ? nullptr : it->second.get();
}

std::shared_ptr<const AudioClip> Document::sharedAudioSamplesFor(TrackId track) const {
    const auto it = audio_samples_.find(track);
    return it == audio_samples_.end() ? nullptr : it->second;
}

const AudioPeaks* Document::audioPeaksFor(TrackId track) const {
    const auto it = audio_peaks_.find(track);
    return it == audio_peaks_.end() ? nullptr : &it->second;
}

void Document::forgetAudioSamples() {
    audio_samples_.clear();
    audio_peaks_.clear();
}

std::size_t Document::timelineFrames() const {
    std::size_t frames = scene_.timelineFrames();
    for (const AudioTrack& sound : scene_.audio_tracks) {
        const auto it = audio_samples_.find(sound.id);
        if (it == audio_samples_.end()) continue;
        // The *audible* length, so a sound cropped short stops asking the
        // timeline to reach where it used to end.
        const std::size_t length = audibleFrames(*it->second, sound.placement, scene_.framerate);
        if (length == 0) continue;
        // A soundtrack that starts before the shot contributes only what is
        // inside it: the part before frame 0 is not somewhere the playhead can
        // go, so it is not length the timeline has to reach.
        const double last = sound.placement.offset_frames + static_cast<double>(length);
        if (last > 0.0) {
            frames = std::max(frames, static_cast<std::size_t>(std::ceil(last)));
        }
    }
    return frames;
}

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

TrackId Document::duplicateTrack(TrackId id) {
    const auto at = std::find_if(scene_.tracks.begin(), scene_.tracks.end(),
                                 [&](const Track& t) { return t.id == id; });
    if (at == scene_.tracks.end()) return kNoId;
    const Track& source = *at;
    const std::size_t index = static_cast<std::size_t>(at - scene_.tracks.begin()) + 1;

    ScopedCommand command(*this, "Duplicate track");

    Track copy;
    copy.id = track_ids_.next();
    copy.setProperties(source.properties());
    copy.name = source.name + " copy";

    // The layers first, because everything below is keyed on their ids.
    std::unordered_map<LayerId, LayerId> layers;
    copy.layers.reserve(source.layers.size());
    for (const Layer& layer : source.layers) {
        Layer fresh = layer;
        fresh.id = layer_ids_.next();
        layers[layer.id] = fresh.id;
        copy.layers.push_back(std::move(fresh));
    }

    // **And then the one place a layer points at another layer.** A colour
    // layer's sources name the line art it is cut against, within this track --
    // so a copy that left them alone would have its fills cut against the
    // *original's* line art, which looks perfectly right until somebody draws
    // on one of the two tracks.
    for (Layer& layer : copy.layers) {
        for (LayerId& cut_against : layer.ctg_sources) {
            const auto found = layers.find(cut_against);
            if (found != layers.end()) cut_against = found->second;
        }
    }

    std::unordered_map<ImageId, ImageId> images;
    images.reserve(source.images.size());
    for (const auto& [image_id, image] : source.images) {
        Image fresh;
        fresh.id = image_ids_.next();
        // Kept, not renumbered: a number is unique within a track and this is a
        // whole track, so the copy's cards read the same as the original's.
        fresh.number = image.number;
        fresh.marker = image.marker;

        for (const auto& [layer_id, cel_id] : image.cels) {
            const Cel* original = cel(cel_id);
            const auto mapped = layers.find(layer_id);
            if (!original || mapped == layers.end()) continue;
            const CelId made = createCelCopy(*original);
            fresh.cels[mapped->second] = made;
            addCelRef(made);
        }
        // The other way a drawing carries a picture -- see copyOfImage, where
        // forgetting this one made a duplicate come back blank.
        for (const auto& [layer_id, frame] : image.source_frames) {
            const auto mapped = layers.find(layer_id);
            if (mapped != layers.end()) fresh.source_frames[mapped->second] = frame;
        }

        images[image_id] = fresh.id;
        copy.images.emplace(fresh.id, std::move(fresh));
    }

    copy.slots.reserve(source.slots.size());
    for (ImageId slot : source.slots) {
        const auto found = images.find(slot);
        copy.slots.push_back(found == images.end() ? kNoId : found->second);
    }

    scene_.tracks.insert(scene_.tracks.begin() + static_cast<std::ptrdiff_t>(index),
                         std::move(copy));
    recordOp(std::make_unique<TrackOp>(index, std::nullopt));
    return scene_.tracks[index].id;
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
    // The keys are drawing and layer ids, and the document arriving here hands
    // out its own from one -- so an entry kept across a load would answer to an
    // id belonging to a drawing that no longer exists, with a picture from the
    // project that was open before.
    reference_frames_.clear();
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

    // **The imported picture comes too, and forgetting it is what made
    // duplicating an import look like a refusal.** `source_frames` sits beside
    // `cels` and answers the same question for a reference layer that a cel
    // answers for a raster one -- which is exactly why a copy that took only
    // the cels produced a drawing with nothing on it. Reported from use as
    // "Duplicate drawing fails silently on an import"; what it actually did was
    // succeed, and hand back a blank frame.
    //
    // Nothing is refcounted here, unlike a cel: the entry is an int naming a
    // frame of a file the layer already lists. Two drawings pointing at the
    // same frame of the same file is the ordinary case -- it is what holding an
    // imported frame a second time means.
    copy.source_frames = source->source_frames;

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

// Which drawings a bake of this layer is about, in one place, because the bake,
// the budget and the ghost picture all have to be about exactly the same set
// and none of them may work it out for itself.
//
// Sorted, and that is not tidiness: `images` is an unordered_map, so its walk
// order is not the timeline's and is not even stable between two runs of the
// same program. A bake that journalled its tiles in a different order each time
// would be a bake no test could assert anything about.
std::vector<ImageId> Document::layerDrawings(TrackId track_id, LayerId layer_id) const {
    std::vector<ImageId> drawings;
    const Track* track = scene_.findTrack(track_id);
    if (!track) return drawings;

    drawings.reserve(track->images.size());
    for (const auto& [id, image] : track->images) {
        if (image.celFor(layer_id) != kNoId) drawings.push_back(id);
    }
    std::sort(drawings.begin(), drawings.end());
    return drawings;
}

std::vector<const TileGrid*> Document::layerGrids(TrackId track_id, LayerId layer_id) const {
    std::vector<const TileGrid*> grids;
    for (const ImageId image : layerDrawings(track_id, layer_id)) {
        if (const Cel* found = celAt(track_id, image, layer_id)) grids.push_back(&found->tiles());
    }
    return grids;
}

Document::LayerBake Document::transformLayer(TrackId track_id, LayerId layer_id,
                                             const Transform& t) {
    // Taken whatever happens next, including every way out below. A hook left
    // armed by a bake that never ran would fire on an unrelated later one, and
    // "the next call to this function" is a rule a test can hold in its head
    // where "the next call that got as far as writing something" is not.
    const std::optional<std::size_t> fail_after = std::exchange(fail_bake_after_, std::nullopt);

    Track* track = scene_.findTrack(track_id);
    if (!track || !track->findLayer(layer_id)) return {};
    if (t.isIdentity()) return {};

    const std::vector<ImageId> drawings = layerDrawings(track_id, layer_id);
    if (drawings.empty()) return {};

    // Where the history stood before any of this, so that the rescue below can
    // tell whether there is anything to put back. A depth would not do: the
    // history trims itself from the bottom, so it can gain a command and lose
    // one in the same breath and read the same either way. A stamp is unique to
    // the command that got it and the newest command is never trimmed.
    const std::uint64_t before = historyStamp();

    // **The history is not allowed to pay for a bake that is about to be
    // undone.** Closing the command trims the history to its byte budget, and
    // one bake is most of that budget on its own -- at HD, more than all of it
    // -- so an ordinary close drops every older command to make room. That is
    // the right price for a bake that *lands*: undo has to hold the old pixels
    // and there is no cheaper correct answer. It is the wrong price entirely
    // for one that runs out of memory, because the rescue then throws away the
    // very command the room was made for, and the session's undo history has
    // been spent on nothing. Reported as a bake that failed, said "nothing has
    // changed", and left Ctrl+Z doing nothing at all.
    //
    // So the trim is held until the outcome is known, and the redo stack is
    // held aside rather than cleared -- a rescued bake has to leave the history
    // exactly as it found it, which is what the refusal message promises.
    defer_trim_ = true;
    std::vector<Command> redo_before = std::move(redo_stack_);
    redo_stack_.clear();

    LayerBake done;
    {
        // One command for the whole layer, which is what makes it one undo
        // step. What that costs is a journal holding every tile of every
        // drawing it displaced -- the history's byte budget is the thing that
        // notices, and on a long shot it will drop older commands to stay
        // inside it. The newest is never dropped, so the bake itself always
        // undoes. See "what the history is allowed to cost" in
        // docs/handover.md.
        ScopedCommand command(*this, "Transform layer through time");
        try {
            for (const ImageId image : drawings) {
                if (fail_after && done.drawings >= *fail_after) throw std::bad_alloc();
                Cel* cel = celForWriting(track_id, image, layer_id);
                if (!cel) continue;
                // Nothing is created here: every drawing in the list already
                // has a cel, which is what keeps this off the inheriting path a
                // colour layer would take. Read into a new grid and swapped in
                // one step, so no drawing is ever half moved.
                cel->replaceTiles(transformTiles(cel->tiles(), t), journal());
                ++done.drawings;
            }
        } catch (const std::bad_alloc&) {
            // Caught inside the command's own scope on purpose. Letting it out
            // would unwind through ~ScopedCommand, and an exception escaping a
            // destructor during unwinding is a terminate rather than an error.
            done.ran_out_of_memory = true;
        }
    }
    defer_trim_ = false;
    // Asked once, here, and used by both outcomes: whether the bake got as far
    // as putting a command on the stack at all.
    const bool wrote = historyStamp() != before;

    if (!done.ran_out_of_memory) {
        // It landed, so the room it needs is room it has earned. Trimming here
        // rather than in endCommand is the only difference from every other
        // command in the program, and it is one line further along. What was
        // held aside is dropped rather than put back, because a bake that wrote
        // something is an edit and an edit is what invalidates a redo.
        if (wrote) {
            trimHistory();
        } else {
            redo_stack_ = std::move(redo_before);
        }
        return done;
    }

    // Put back whatever landed. Every drawing written is journalled, so this is
    // exact rather than approximate -- and it is the whole reason the bake is
    // one command: a rescue that had to unwind forty of them would have forty
    // chances to go wrong.
    //
    // Only if a command was actually pushed. A failure before the first write
    // leaves an empty command, which endCommand does not put on the stack at
    // all, and undoing then would undo whatever the person did *before* this.
    done.drawings = 0;
    if (wrote && undo()) {
        // And it must not be redoable. Undo moves the command to the redo
        // stack, and a redo of a bake that ran out of memory would put the
        // layer back into the half-written state this just rescued it from.
        redo_stack_.pop_back();
    }
    // Everything the bake displaced has been put back, so what it was holding
    // is not being held any more and the history is the size it was before. The
    // redo stack goes back too: a bake that changed nothing must not be the
    // thing that took away a redo.
    redo_stack_ = std::move(redo_before);
    return done;
}

// The track is not part of the key: ImageIds come from one counter per
// document, so a drawing names itself unambiguously without it. It stays in the
// signature because every caller has one and reads better for saying so.
const CtgFill* Document::ctgFillFor(TrackId, ImageId image_id, LayerId layer_id) const {
    const CtgFill* found = ctg_cache_.find(CtgKey{image_id, layer_id});
    return (found && found->valid) ? found : nullptr;
}

void Document::setSourceFrame(TrackId track_id, ImageId image_id, LayerId layer_id, int frame) {
    Track* track = scene_.findTrack(track_id);
    if (!track) return;
    Image* image = track->findImage(image_id);
    if (!image) return;
    if (image->sourceFrameFor(layer_id) == frame) return;  // nothing to record

    ScopedCommand command(*this, "Point at a frame");
    // The whole record, because the map is on it and ImageOp is what swaps one.
    // It costs a copy of two small hash maps and no pixels at all -- the cels
    // it carries are ids, so an undo entry for this weighs nothing against the
    // history budget, which is what makes it affordable per drawing.
    recordOp(std::make_unique<ImageOp>(track_id, image_id, *image));

    if (frame == Image::kNoSourceFrame) {
        image->source_frames.erase(layer_id);
    } else {
        image->source_frames[layer_id] = frame;
    }
}

const TileGrid* Document::referenceFrameFor(TrackId, ImageId image_id, LayerId layer_id,
                                            const Transform& under) const {
    return reference_frames_.find(CtgKey{image_id, layer_id}, under);
}

void Document::setReferenceFrame(TrackId, ImageId image_id, LayerId layer_id,
                                 const Transform& under, TileGrid tiles) {
    reference_frames_.store(CtgKey{image_id, layer_id}, under, std::move(tiles));
}

void Document::forgetReferenceFrames() { reference_frames_.clear(); }

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
        //
        // Deferred for exactly one caller. A layer bake may be undone again the
        // instant it closes -- it is the one command in the program that can
        // run out of memory halfway and put itself back -- and trimming for a
        // command that is about to be thrown away spends the session's history
        // on nothing. transformLayer trims itself once it knows. Nothing else
        // sets this, and it is cleared before that function returns.
        if (!defer_trim_) trimHistory();
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
