// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QTreeWidget>
#include <functional>

#include "double_tap.h"

// The layer panel's list, which is a list that refuses to reorder itself.
//
// Dragging a row is how the stack is restacked. The Move up and Move down
// buttons it replaces were one click per position, so putting a layer of ten at
// the bottom was nine of them, and neither button ever said what it was about to
// move past.
//
// The drop is intercepted before Qt's InternalMove can act on it, and that is
// the point of the class rather than an implementation detail: QTreeWidget's own
// drop takes the *item* out and puts it back somewhere else, which would leave
// the panel showing a stack the document has never heard of -- until the next
// rebuild, which would then silently undo the drag. So this reports where the
// row was dropped and changes nothing. The document moves the layer and the list
// is rebuilt from it, which is the only order in which what you see is the stack
// that will be composited.
//
// No Q_OBJECT: it has no signals of its own, one caller is enough for a
// std::function, and `findChild<QTreeWidget*>` -- which is how the tests and the
// screenshot tool reach the panel -- keeps finding it either way.
class LayerList : public QTreeWidget {
public:
    explicit LayerList(QWidget* parent = nullptr);

    // Where the row was picked up, and where it should end up counted in the
    // list with that row already taken out of it -- which is what
    // Document::moveLayer means by `to`.
    std::function<void(int from, int to)> reordered;

    // Renaming, and it refuses to do that itself for the same reason it refuses
    // to reorder itself: the document is edited and the list is rebuilt from it.
    //
    // `nameOf` is what the editor opens on, and it is not the row's text. A
    // colour layer whose marks come from another drawing has an arrow in front
    // of its name -- see MainWindow::layerLabel -- and a rename seeded from what
    // the row says would put that arrow in the name, then a second arrow in
    // front of that. So the panel is asked what the layer is actually called.
    std::function<QString(int row)> nameOf;
    std::function<void(int row, const QString& name)> renamed;

    // A name is being typed into, or has stopped being. The window turns the
    // keyboard shortcuts off while it is true: Return is Play, and Return is
    // also how a rename is finished. See shortcuts::Mode::Typing.
    std::function<void(bool renaming)> renaming;

    // The editor the delegate has just made, so that this class knows which
    // widget is the live one. Public because the delegate is a detail of the
    // .cpp and this is its way back in.
    //
    // Held rather than looked for. A closed editor is only `deleteLater`d, so
    // it stays a child of the viewport until the event loop next runs and
    // `findChild<QLineEdit*>` goes on answering with it -- so a second rename
    // opened before the first editor was collected closed the *old* one and
    // left the live one open. See issue #51: that is how a rename outlived the
    // window it was going to report to.
    void editorOpened(QWidget* editor) { editor_ = editor; }

    // Give up an open rename without keeping what was typed, which is what
    // Escape does.
    //
    // For a window on its way out, and MainWindow's destructor is the caller
    // that matters: an editor still open when the window is destroyed commits
    // itself on the way down, and `renamed` then reaches a window whose
    // document has already been destroyed.
    void abandonRename();

    // Where a row picked up at `from` lands when it is dropped at `boundary` --
    // a gap between rows, 0 above the first and `rows` below the last.
    //
    // Public and static because it is the whole of the thinking in the drop and
    // the one part of it a test can reach: Qt's drag and drop cannot be driven
    // by synthetic events, so a test of the gesture end to end is not available
    // and this is what would be wrong in it. The subtraction is the trap -- a
    // boundary is counted with the row still in place and moveLayer counts the
    // destination with it taken out, so the two agree going up and differ by one
    // going down.
    static int destinationFor(int from, int boundary, int rows);

    // Whether Qt has decided a press is turning into a drag. The half of the
    // gesture a test can still reach: the *start* is ordinary mouse events, and
    // it is where a wrong flag would leave a row that simply cannot be picked
    // up -- silently, and identically to a drag that starts and does nothing.
    // The drop itself is the platform's and cannot be driven from here.
    bool dragHasBegunForTesting() const { return state() == DraggingState; }

    // Open the rename editor on a row, as a double click does. Qt's own double
    // click is not the part that would be wrong; what the editor is seeded with
    // and what it does with what is typed are, and those a test can reach.
    void renameRowForTesting(int row) {
        if (QTreeWidgetItem* item = topLevelItem(row)) {
            editItem(item, 0);
        }
    }

    // Finish an open rename: `keep` true is what Return means, false is Escape.
    //
    // Driven at the seam, like the drop above and for the same kind of reason.
    // Qt answers Return by posting a queued call inside the delegate, and a
    // synthetic key event never gets it delivered offscreen -- Escape, which is
    // emitted straight from the filter, does work and is what proved the filter
    // is installed at all. This is the pair of calls that queued hop makes, so
    // everything downstream of it is ours and is tested.
    void finishRenameForTesting(bool keep);

protected:
    void dropEvent(QDropEvent* event) override;
    // The far end of an open editor, which with the delegate's createEditor is
    // what `renaming` reports. Qt's own entry point rather than somewhere we
    // call ourselves, so a rename closed by anything at all -- Return, Escape, a
    // click elsewhere, the panel being rebuilt -- says so, and the keyboard
    // cannot be left switched off with no editor to show for it.
    void closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint) override;
    // Where the pen's second tap is turned into the edit that a mouse's double
    // click starts through the DoubleClicked trigger. See DoubleTap: the pen
    // produces two presses and no double click, so the trigger never fires.
    void mousePressEvent(QMouseEvent* event) override;

private:
    DoubleTap taps_;
    // The rename editor that is being typed into, or nullptr. See editorOpened.
    QWidget* editor_ = nullptr;
};
