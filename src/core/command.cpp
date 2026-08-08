// SPDX-License-Identifier: GPL-3.0-or-later
#include "command.h"

#include <algorithm>
#include <utility>

#include "document.h"

namespace animage {
namespace {

void addRefsForTrack(Document& doc, const Track& track) {
    for (const auto& [image_id, image] : track.images) {
        for (const auto& [layer_id, cel_id] : image.cels) doc.addCelRef(cel_id);
    }
}

void releaseRefsForTrack(Document& doc, const Track& track) {
    for (const auto& [image_id, image] : track.images) {
        for (const auto& [layer_id, cel_id] : image.cels) doc.releaseCelRef(cel_id);
    }
}

}  // namespace

void LayerListOp::applySwap(Document& doc) {
    Track* track = doc.mutableScene().findTrack(track_);
    if (!track) return;
    std::swap(track->layers, layers_);
}

void SlotsOp::applySwap(Document& doc) {
    Track* track = doc.mutableScene().findTrack(track_);
    if (!track) return;
    std::swap(track->slots, slots_);
}

void ImageOp::applySwap(Document& doc) {
    Track* track = doc.mutableScene().findTrack(track_);
    if (!track) return;

    std::optional<Image> live;
    auto it = track->images.find(image_);
    if (it != track->images.end()) {
        live = std::move(it->second);
        track->images.erase(it);
        for (const auto& [layer_id, cel_id] : live->cels) doc.releaseCelRef(cel_id);
    }

    if (state_) {
        for (const auto& [layer_id, cel_id] : state_->cels) doc.addCelRef(cel_id);
        track->images.emplace(image_, std::move(*state_));
    }

    state_ = std::move(live);
}

void ImageOp::collectCelIds(std::vector<CelId>& out) const {
    if (!state_) return;
    for (const auto& [layer_id, cel_id] : state_->cels) out.push_back(cel_id);
}

void CelAssignOp::applySwap(Document& doc) {
    Track* track = doc.mutableScene().findTrack(track_);
    if (!track) return;
    Image* image = track->findImage(image_);
    if (!image) return;

    const CelId live = image->celFor(layer_);
    if (live != kNoId) doc.releaseCelRef(live);

    if (cel_ != kNoId) {
        image->cels[layer_] = cel_;
        doc.addCelRef(cel_);
    } else {
        image->cels.erase(layer_);
    }

    cel_ = live;
}

void CelAssignOp::collectCelIds(std::vector<CelId>& out) const {
    if (cel_ != kNoId) out.push_back(cel_);
}

void TrackPropsOp::applySwap(Document& doc) {
    Track* track = doc.mutableScene().findTrack(track_);
    if (!track) return;
    TrackProperties live = track->properties();
    track->setProperties(state_);
    state_ = std::move(live);
}

void TrackOp::applySwap(Document& doc) {
    Scene& scene = doc.mutableScene();

    if (state_) {
        const std::size_t at = std::min(index_, scene.tracks.size());
        addRefsForTrack(doc, *state_);
        scene.tracks.insert(scene.tracks.begin() + static_cast<std::ptrdiff_t>(at),
                               std::move(*state_));
        state_.reset();
        index_ = at;
        return;
    }

    if (index_ >= scene.tracks.size()) return;
    Track extracted = std::move(scene.tracks[index_]);
    scene.tracks.erase(scene.tracks.begin() + static_cast<std::ptrdiff_t>(index_));
    releaseRefsForTrack(doc, extracted);
    state_ = std::move(extracted);
}

void TrackOp::collectCelIds(std::vector<CelId>& out) const {
    if (!state_) return;
    for (const auto& [image_id, image] : state_->images) {
        for (const auto& [layer_id, cel_id] : image.cels) out.push_back(cel_id);
    }
}

void SceneFramerateOp::applySwap(Document& doc) {
    std::swap(doc.mutableScene().framerate, framerate_);
}

void SceneLengthOp::applySwap(Document& doc) {
    std::swap(doc.mutableScene().fixed_length, fixed_);
    std::swap(doc.mutableScene().length, length_);
}

void SceneCanvasOp::applySwap(Document& doc) {
    std::swap(doc.mutableScene().width, width_);
    std::swap(doc.mutableScene().height, height_);
}

}  // namespace animage
