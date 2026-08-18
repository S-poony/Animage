// SPDX-License-Identifier: GPL-3.0-or-later
#include "layer_list.h"

#include <QDropEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStyle>
#include <QStyledItemDelegate>

#include <algorithm>

#include "name_limits.h"

namespace {

// The rename editor for a layer row.
//
// Three overrides, each one of them load-bearing:
//
// - Only the first column can be edited. A QTreeWidgetItem's flags are the
//   item's and not the column's, so making the row editable makes the Marks
//   column editable too -- and that column holds a tick and no text at all, so
//   an editor there would be a blank field that saved a blank string.
// - The editor opens on the layer's name, which is not the row's text: see
//   LayerList::nameOf.
// - And nothing is written back into the model. The document is edited and the
//   panel rebuilt from it, which is the same rule the drop follows and for the
//   same reason -- a row showing a name the document has never heard of would
//   be undone by the next rebuild, silently.
//
// No Q_OBJECT: it adds no signals and no slots, and one in a .cpp would need
// its own moc include to say nothing.
class LayerNameDelegate : public QStyledItemDelegate {
public:
    explicit LayerNameDelegate(LayerList* list) : QStyledItemDelegate(list), list_(list) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override {
        if (index.column() != 0) return nullptr;
        QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
        // The same cap the timeline's editor has. See names::kTyped.
        if (auto* line = qobject_cast<QLineEdit*>(editor)) line->setMaxLength(names::kTyped);
        if (editor) {
            // Which widget is the live editor. See LayerList::editorOpened.
            list_->editorOpened(editor);
            // The moment a rename starts, and the only honest one. It was hung
            // off QAbstractItemView::edit at first, which does not mean what its
            // name suggests: it returns true when the *delegate* merely consumed
            // the event, so ticking a layer's visibility box counted as starting
            // a rename and left the keyboard shortcuts switched off afterwards.
            if (list_->renaming) list_->renaming(true);
        }
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        auto* line = qobject_cast<QLineEdit*>(editor);
        if (!line || !list_->nameOf) {
            QStyledItemDelegate::setEditorData(editor, index);
            return;
        }
        line->setText(list_->nameOf(index.row()));
        line->selectAll();
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override {
        auto* line = qobject_cast<QLineEdit*>(editor);
        if (!line || !list_->renamed) {
            QStyledItemDelegate::setModelData(editor, model, index);
            return;
        }
        list_->renamed(index.row(), line->text());
    }

private:
    LayerList* list_;
};

}  // namespace

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

    // A double click and nothing else. Qt's default set includes SelectedClicked,
    // which starts an edit on a plain click on the row you are already on -- and
    // the row you are already on is the layer you are drawing on, so that is a
    // rename every time you come back to the panel to change the opacity.
    setEditTriggers(QAbstractItemView::DoubleClicked);
    setItemDelegate(new LayerNameDelegate(this));
}

// Both edges of the row it came from are a move to where it already is, and
// both land on `from` here rather than needing a case of their own.
int LayerList::destinationFor(int from, int boundary, int rows) {
    if (rows < 2) return from;
    boundary = std::clamp(boundary, 0, rows);
    return std::clamp(boundary > from ? boundary - 1 : boundary, 0, rows - 1);
}

void LayerList::mousePressEvent(QMouseEvent* event) {
    // The pen's second tap, which arrives as an ordinary press.
    //
    // On the name and nowhere else, which is stricter than the double click Qt
    // gives a mouse and is meant to be. The visibility tick is in this same
    // column, and flicking a layer off and on to compare is about the most
    // ordinary thing anybody does in this panel -- with the whole row live, that
    // gesture would open a rename every time instead.
    if (event->button() == Qt::LeftButton &&
        taps_.isSecond(event->globalPosition().toPoint(), event->timestamp())) {
        const QPoint at = event->position().toPoint();
        const QModelIndex under = indexAt(at);
        if (under.isValid() && under.column() == 0) {
            QStyleOptionViewItem opt;
            initViewItemOption(&opt);
            opt.rect = visualRect(under);
            opt.features |= QStyleOptionViewItem::HasCheckIndicator;
            if (style()->subElementRect(QStyle::SE_ItemViewItemText, &opt, this).contains(at)) {
                edit(under, DoubleClicked, event);
                return;
            }
        }
    }
    QTreeWidget::mousePressEvent(event);
}

void LayerList::closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint) {
    if (editor == editor_) editor_ = nullptr;
    QTreeWidget::closeEditor(editor, hint);
    if (renaming) renaming(false);
}

void LayerList::abandonRename() {
    if (editor_) closeEditor(editor_, QAbstractItemDelegate::RevertModelCache);
}

void LayerList::finishRenameForTesting(bool keep) {
    if (!keep) {
        // Escape and giving up on the way out are the same act.
        abandonRename();
        return;
    }
    // Taken before the commit, which renames the layer and rebuilds the panel
    // underneath the editor -- and a rebuild is one of the things that closes
    // one.
    QWidget* editor = editor_;
    if (!editor) return;
    commitData(editor);
    closeEditor(editor, QAbstractItemDelegate::SubmitModelCache);
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
