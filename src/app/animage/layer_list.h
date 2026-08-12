// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QTreeWidget>
#include <functional>

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

protected:
    void dropEvent(QDropEvent* event) override;
};
