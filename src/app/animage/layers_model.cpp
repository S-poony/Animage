// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers_model.h"

using namespace animage;

LayersModel::LayersModel(QObject* parent) : QAbstractListModel(parent) {}

void LayersModel::setTrack(TrackId track) {
    if (track_ == track) return;
    track_ = track;
    refresh();
}

void LayersModel::setCurrentImage(ImageId image) {
    if (image_ == image) return;
    image_ = image;
    refresh();
}

int LayersModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !doc_) return 0;
    const Track* track = doc_->scene().findTrack(track_);
    return track ? static_cast<int>(track->layers.size()) : 0;
}

QVariant LayersModel::data(const QModelIndex& index, int role) const {
    if (!doc_ || !index.isValid()) return {};
    const Track* track = doc_->scene().findTrack(track_);
    if (!track || index.row() < 0 || static_cast<std::size_t>(index.row()) >= track->layers.size()) {
        return {};
    }
    const Layer& layer = track->layers[static_cast<std::size_t>(index.row())];

    switch (role) {
        case NameRole: {
            // The same "what is this layer doing here" label the panel always
            // showed: a colour layer whose marks were made on another drawing
            // says so, because a carried mark looks exactly like one you drew.
            if (layer.kind != LayerKind::Ctg) {
                return QString::fromStdString(layer.name);
            }
            ImageId from = kNoId;
            if (doc_->ctgScribblesAt(track_, image_, layer.id, &from) && from != image_) {
                return QStringLiteral("\u2190 ") + QString::fromStdString(layer.name);
            }
            return QString::fromStdString(layer.name);
        }
        case VisibleRole:
            return layer.visible;
        case ShowScribblesRole:
            return layer.kind == LayerKind::Ctg && layer.show_scribbles;
        case IsCtgRole:
            return layer.kind == LayerKind::Ctg;
        case OpacityRole:
            return static_cast<int>(std::lround(layer.opacity * 100.0));
        case CarriedRole: {
            if (layer.kind != LayerKind::Ctg) return false;
            ImageId from = kNoId;
            return doc_->ctgScribblesAt(track_, image_, layer.id, &from) && from != image_;
        }
        case LockedRole:
            return layer.locked;
        default:
            return {};
    }
}

QHash<int, QByteArray> LayersModel::roleNames() const {
    return {
        {NameRole, "name"},
        {VisibleRole, "visible"},
        {ShowScribblesRole, "showScribbles"},
        {IsCtgRole, "isCtg"},
        {OpacityRole, "opacity"},
        {CarriedRole, "carried"},
        {LockedRole, "locked"},
    };
}

void LayersModel::refresh() {
    beginResetModel();
    endResetModel();
}
