// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>

#include "document.h"

// The timeline: the shared time axis, showing one track as a strip of slots.
// There is no row per layer, and that absence is the whole point of the model —
// timing belongs to the image, so every layer of a drawing is held for exactly
// as long as the drawing is.
//
// A drawing held over several frames is one block with a tail, not several
// identical cells, because that is what it is: the same ImageId repeated. The
// model gives each slot the facts the panel needs to draw that: its drawing
// number (zero on a held tail), whether it starts or ends a run, and what its
// colour layers are doing here.
class TimelineModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        NumberRole = Qt::UserRole + 1,
        HeldRole,
        RunStartRole,
        RunEndRole,
        RunStartSlotRole,
        CarriedRole,
        HasColourRole,
    };

    explicit TimelineModel(QObject* parent = nullptr);

    void setDocument(animage::Document* document) { doc_ = document; }
    void setTrack(animage::TrackId track);

    void refresh();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // How many slots the track has; the panel sizes itself to this.
    int slotCount() const;

    // The insertion point for a dragged drawing at `pointer_x`, counted in the
    // track as it will be once the drawing at `drag_slot` has been lifted out
    // of it. Mirrors the widget timeline's dropIndexFor.
    int dropIndexFor(int pointer_x, int cell_width, std::size_t drag_slot) const;

private:
    animage::Document* doc_ = nullptr;
    animage::TrackId track_ = animage::kNoId;
};
