// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline_model.h"

#include <algorithm>

using namespace animage;

TimelineModel::TimelineModel(QObject* parent) : QAbstractListModel(parent) {}

void TimelineModel::setTrack(TrackId track) {
    if (track_ == track) return;
    track_ = track;
    refresh();
}

int TimelineModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !doc_) return 0;
    const Track* track = doc_->scene().findTrack(track_);
    return track ? static_cast<int>(track->slots.size()) : 0;
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const {
    if (!doc_ || !index.isValid()) return {};
    const Track* track = doc_->scene().findTrack(track_);
    if (!track || index.row() < 0 || static_cast<std::size_t>(index.row()) >= track->slots.size()) {
        return {};
    }
    const std::size_t slot = static_cast<std::size_t>(index.row());
    const ImageId image = track->slots[slot];
    const Image* drawing = track->findImage(image);

    switch (role) {
        case NumberRole: {
            if (!drawing) return 0;
            const bool held = slot > 0 && track->slots[slot - 1] == image;
            return held ? 0 : drawing->number;
        }
        case HeldRole:
            return slot > 0 && track->slots[slot - 1] == image;
        case RunStartRole: {
            const auto [first, last] = track->runBounds(slot);
            Q_UNUSED(last);
            return first == slot;
        }
        case RunEndRole: {
            const auto [first, last] = track->runBounds(slot);
            Q_UNUSED(first);
            return last == slot;
        }
        case RunStartSlotRole: {
            const auto [first, last] = track->runBounds(slot);
            Q_UNUSED(last);
            return static_cast<int>(first);
        }
        case CarriedRole:
        case HasColourRole: {
            bool any = false;
            bool carried = false;
            if (drawing && image != kNoId) {
                for (const Layer& layer : track->layers) {
                    if (layer.kind != LayerKind::Ctg || !layer.visible) continue;
                    ImageId from = kNoId;
                    if (!doc_->ctgScribblesAt(track_, image, layer.id, &from)) continue;
                    any = true;
                    if (from != image) carried = true;
                }
            }
            return role == CarriedRole ? carried : any;
        }
        default:
            return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const {
    return {
        {NumberRole, "number"},
        {HeldRole, "held"},
        {RunStartRole, "runStart"},
        {RunEndRole, "runEnd"},
        {RunStartSlotRole, "runStartSlot"},
        {CarriedRole, "carried"},
        {HasColourRole, "hasColour"},
    };
}

int TimelineModel::slotCount() const {
    if (!doc_) return 0;
    const Track* track = doc_->scene().findTrack(track_);
    return track ? static_cast<int>(track->slots.size()) : 0;
}

int TimelineModel::dropIndexFor(int pointer_x, int cell_width, std::size_t drag_slot) const {
    if (!doc_) return 0;
    const Track* track = doc_->scene().findTrack(track_);
    if (!track || drag_slot >= track->slots.size()) return 0;

    const ImageId dragged = track->slots[drag_slot];
    const int boundary = (pointer_x + cell_width / 2) / cell_width;
    int index = 0;
    for (int i = 0; i < boundary && i < static_cast<int>(track->slots.size()); ++i) {
        if (track->slots[static_cast<std::size_t>(i)] != dragged) ++index;
    }
    return index;
}

void TimelineModel::refresh() {
    beginResetModel();
    endResetModel();
}
