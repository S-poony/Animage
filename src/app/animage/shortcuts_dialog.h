// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <map>

#include "shortcuts.h"

class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Editing what the keyboard does: the rebinding half of issue #14.
//
// **Nothing is changed until Apply.** The dialog edits a copy of the bindings
// and hands it back only when Apply is pressed, and Apply is refused while any
// two live-at-once actions collide. That is the point rather than an
// implementation detail: a dialog that refuses the chord as you type it can say
// "no" but cannot show you *what* you have hit, and the collision this program
// exists to prevent is one nobody sees coming -- `0` and `Shift+0` are one chord
// on AZERTY and look like two everywhere else. So the chord goes in, the clash
// is named underneath it, and the way out is to change one of them.
//
// The rows are grouped, and the groups are ordered by what somebody who has just
// opened this is likely hunting for rather than by menu order. Two kinds of row
// cannot be typed into: none, except the held keys -- Space and Z -- which are
// held down for as long as a drag lasts and which QAction cannot express. They
// are listed anyway, because they consume their key, and a panel that showed
// every key but the two that will silently eat a rebinding would be worse than
// none.
class ShortcutsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutsDialog(const shortcuts::Bindings& from, QWidget* parent = nullptr);

    // What was edited. Only meaningful after the dialog was accepted, which only
    // Apply does, which is only enabled while nothing collides.
    const shortcuts::Bindings& bindings() const { return editing_; }

private:
    void buildRows();
    // A row was typed into. Stores it without judging it -- judging is what the
    // clash line and Apply do, once, over the whole set.
    void rowEdited(shortcuts::Id id);
    void resetRow(shortcuts::Id id);
    void restoreDefaults();
    // What every row shows: the key, and whether it has a Reset to offer.
    void syncRow(shortcuts::Id id);
    // Every collision in the set, said in words, and Apply's enabled state.
    void syncClashes();
    // Narrows the tree to what matches, and hides a group with nothing left in
    // it. A shortcuts panel is a long list, and the search box is what keeps it
    // from being the first thing anybody has to read all of.
    void filterRows(const QString& text);

    struct Row {
        QTreeWidgetItem* item = nullptr;
        QKeySequenceEdit* edit = nullptr;   // null for a held key
        QToolButton* reset = nullptr;       // shown only while the row differs
    };

    shortcuts::Bindings editing_;
    std::map<shortcuts::Id, Row> rows_;
    std::map<shortcuts::Group, QTreeWidgetItem*> headings_;

    QTreeWidget* tree_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* clashes_ = nullptr;
    QPushButton* apply_ = nullptr;
    // Every editor answers back when a row is put on its default, so the
    // handlers would recurse through each other.
    bool updating_ = false;
};
