// SPDX-License-Identifier: GPL-3.0-or-later
#include "tracks_model.h"

using namespace animage;

TracksModel::TracksModel(QObject* parent) : QAbstractListModel(parent) {}

void TracksModel::setCurrentTrack(TrackId track) {
    if (current_ == track) return;
    current_ = track;
    refresh();
}

int TracksModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !doc_) return 0;
    return static_cast<int>(doc_->scene().tracks.size());
}

QVariant TracksModel::data(const QModelIndex& index, int role) const {
    if (!doc_ || !index.isValid()) return {};
    const auto& tracks = doc_->scene().tracks;
    if (index.row() < 0 || static_cast<std::size_t>(index.row()) >= tracks.size()) return {};
    const Track& track = tracks[static_cast<std::size_t>(index.row())];

    switch (role) {
        case NameRole:
            return QString::fromStdString(track.name);
        case IsCurrentRole:
            return track.id == current_;
        case OverwriteRole:
            return track.overwrite_drawings;
        case TrackEndRole:
            return static_cast<int>(track.end);
        case TrackIdRole:
            return static_cast<qint64>(track.id);
        default:
            return {};
    }
}

QHash<int, QByteArray> TracksModel::roleNames() const {
    return {
        {NameRole, "name"},
        {IsCurrentRole, "isCurrent"},
        {OverwriteRole, "overwrite"},
        {TrackEndRole, "trackEnd"},
        {TrackIdRole, "trackId"},
    };
}

void TracksModel::refresh() {
    beginResetModel();
    endResetModel();
}
