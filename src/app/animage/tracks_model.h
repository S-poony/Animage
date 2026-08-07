// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>

#include "document.h"

// The track list, as a model: one row per track in scene order, index 0 on
// top (composites first). Rebuilt wholesale when the document changes.
class TracksModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        IsCurrentRole,
        OverwriteRole,
        TrackEndRole,
        TrackIdRole,
    };

    explicit TracksModel(QObject* parent = nullptr);

    void setDocument(animage::Document* document) { doc_ = document; }
    void setCurrentTrack(animage::TrackId track);

    void refresh();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    animage::Document* doc_ = nullptr;
    animage::TrackId current_ = animage::kNoId;
};
