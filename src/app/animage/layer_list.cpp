// SPDX-License-Identifier: GPL-3.0-or-later
#include "layer_list.h"

#include <QDropEvent>

#include <algorithm>

LayerList::LayerList(QWidget* parent) : QTreeWidget(parent) {
    // InternalMove is what turns on dragging, dropping and the drop indicator in
    // one go; what it would then *do* is overridden below.
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    // Off, so a row is never a place to drop another row onto. With it on, a
    // drop in the middle of a row means "into that one", which for a flat stack
    // of layers is a third answer where there are only two: above, and below.
    setDragDropOverwriteMode(false);
    setDropIndicatorShown(true);
}

// Both edges of the row it came from are a move to where it already is, and
// both land on `from` here rather than needing a case of their own.
int LayerList::destinationFor(int from, int boundary, int rows) {
    if (rows < 2) return from;
    boundary = std::clamp(boundary, 0, rows);
    return std::clamp(boundary > from ? boundary - 1 : boundary, 0, rows - 1);
}

void LayerList::dropEvent(QDropEvent* event) {
    // Never Qt's move, and never QTreeWidget::dropEvent. IgnoreAction is also
    // what stops QAbstractItemView::startDrag deleting the row it dragged when
    // the drop is over: it removes the source only for a drop that came back
    // MoveAction.
    event->setDropAction(Qt::IgnoreAction);
    event->accept();

    const QList<QTreeWidgetItem*> picked = selectedItems();
    const int rows = topLevelItemCount();
    if (picked.isEmpty() || !reordered || rows < 2) return;

    const int from = indexOfTopLevelItem(picked.first());
    if (from < 0) return;

    // Where the indicator was, as a boundary between rows: 0 is above the first,
    // `rows` is below the last. Dropped past the end of the list there is no
    // index under the pointer, and the answer is the bottom.
    const QModelIndex under = indexAt(event->position().toPoint());
    int boundary = under.isValid() ? under.row() : rows;
    if (dropIndicatorPosition() == QAbstractItemView::BelowItem) {
        ++boundary;
    } else if (dropIndicatorPosition() == QAbstractItemView::OnViewport) {
        boundary = rows;
    }

    const int to = destinationFor(from, boundary, rows);
    if (to == from) return;
    reordered(from, to);
}
