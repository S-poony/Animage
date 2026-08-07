// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>

#include "document.h"

// The layer panel, as a model: one row per layer in the track, topmost first,
// with everything the panel shows about the layer on the drawing in front of
// you.
//
// Rebuilt wholesale when the document changes -- a layer panel is a few dozen
// rows, and the individual change signals a finer model would need are not
// there to listen to. The controller is the only writer, so rebuilding loses
// nothing the interface cannot recreate.
class LayersModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        VisibleRole,
        ShowScribblesRole,
        IsCtgRole,
        OpacityRole,
        CarriedRole,
        LockedRole,
    };

    explicit LayersModel(QObject* parent = nullptr);

    // Reads the track the controller points this model at.
    void setDocument(animage::Document* document) { doc_ = document; }
    void setTrack(animage::TrackId track);
    void setCurrentImage(animage::ImageId image);

    void refresh();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    animage::Document* doc_ = nullptr;
    animage::TrackId track_ = animage::kNoId;
    animage::ImageId image_ = animage::kNoId;
};
