// SPDX-License-Identifier: GPL-3.0-or-later
#include "command.h"

#include <algorithm>
#include <utility>

#include "document.h"

namespace animage {
namespace {

void addRefsForTimeline(Document& doc, const Timeline& timeline) {
    for (const auto& [image_id, image] : timeline.images) {
        for (const auto& [layer_id, cel_id] : image.cels) doc.addCelRef(cel_id);
    }
}

void releaseRefsForTimeline(Document& doc, const Timeline& timeline) {
    for (const auto& [image_id, image] : timeline.images) {
        for (const auto& [layer_id, cel_id] : image.cels) doc.releaseCelRef(cel_id);
    }
}

}  // namespace

void LayerListOp::applySwap(Document& doc) {
    Timeline* timeline = doc.mutableScene().findTimeline(timeline_);
    if (!timeline) return;
    std::swap(timeline->layers, layers_);
}

void SlotsOp::applySwap(Document& doc) {
    Timeline* timeline = doc.mutableScene().findTimeline(timeline_);
    if (!timeline) return;
    std::swap(timeline->slots, slots_);
}

void ImageOp::applySwap(Document& doc) {
    Timeline* timeline = doc.mutableScene().findTimeline(timeline_);
    if (!timeline) return;

    std::optional<Image> live;
    auto it = timeline->images.find(image_);
    if (it != timeline->images.end()) {
        live = std::move(it->second);
        timeline->images.erase(it);
        for (const auto& [layer_id, cel_id] : live->cels) doc.releaseCelRef(cel_id);
    }

    if (state_) {
        for (const auto& [layer_id, cel_id] : state_->cels) doc.addCelRef(cel_id);
        timeline->images.emplace(image_, std::move(*state_));
    }

    state_ = std::move(live);
}

void ImageOp::collectCelIds(std::vector<CelId>& out) const {
    if (!state_) return;
    for (const auto& [layer_id, cel_id] : state_->cels) out.push_back(cel_id);
}

void CelAssignOp::applySwap(Document& doc) {
    Timeline* timeline = doc.mutableScene().findTimeline(timeline_);
    if (!timeline) return;
    Image* image = timeline->findImage(image_);
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

void TimelineOp::applySwap(Document& doc) {
    Scene& scene = doc.mutableScene();

    if (state_) {
        const std::size_t at = std::min(index_, scene.timelines.size());
        addRefsForTimeline(doc, *state_);
        scene.timelines.insert(scene.timelines.begin() + static_cast<std::ptrdiff_t>(at),
                               std::move(*state_));
        state_.reset();
        index_ = at;
        return;
    }

    if (index_ >= scene.timelines.size()) return;
    Timeline extracted = std::move(scene.timelines[index_]);
    scene.timelines.erase(scene.timelines.begin() + static_cast<std::ptrdiff_t>(index_));
    releaseRefsForTimeline(doc, extracted);
    state_ = std::move(extracted);
}

void TimelineOp::collectCelIds(std::vector<CelId>& out) const {
    if (!state_) return;
    for (const auto& [image_id, image] : state_->images) {
        for (const auto& [layer_id, cel_id] : image.cels) out.push_back(cel_id);
    }
}

void SceneFramerateOp::applySwap(Document& doc) {
    std::swap(doc.mutableScene().framerate, framerate_);
}

}  // namespace animage
