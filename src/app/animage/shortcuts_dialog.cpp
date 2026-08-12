// SPDX-License-Identifier: GPL-3.0-or-later
#include "shortcuts_dialog.h"

#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <vector>

namespace {

using shortcuts::Entry;
using shortcuts::Id;

// The column the label is in, the one that is typed into, and the one the Reset
// appears in when there is anything to reset.
constexpr int kWhat = 0;
constexpr int kKey = 1;
constexpr int kReset = 2;

// Which id a row is, carried on the item so that a click anywhere in the tree
// can find its way back to the table.
constexpr int kIdRole = Qt::UserRole + 1;

// Which groups are open when the panel opens, which is the same judgement as the
// order they are in: what fits on the screen at once should be what somebody who
// has just opened this came for, and the folded headings under it are what say
// the list goes on.
bool opensExpanded(shortcuts::Group group) {
    return group == shortcuts::Group::Tools || group == shortcuts::Group::Timing ||
           group == shortcuts::Group::Looking;
}

QString labelOf(Id id) {
    return QString::fromUtf8(shortcuts::entryFor(id).label).remove(QLatin1Char('&'));
}

QString shownKey(const QKeySequence& sequence) {
    // Native and not portable: this one is read by a person, and "Strg+S" is what
    // Ctrl+S is called on the desktop that says so. The *stored* spelling is the
    // portable one -- see Bindings::toJson.
    return sequence.isEmpty() ? QStringLiteral("--")
                              : sequence.toString(QKeySequence::NativeText);
}

// A collision, said the way the person who caused it can act on it. Two
// sentences, because the two rules are two different surprises: the same chord
// twice is one anybody can see, and a pair that differs only by Shift on a digit
// is the one that looks fine and is not.
QString sentenceFor(const shortcuts::Bindings& bindings, Id a, Id b) {
    for (const QKeySequence& left : bindings.sequencesFor(a)) {
        for (const QKeySequence& right : bindings.sequencesFor(b)) {
            if (shortcuts::sameChord(left, right)) {
                return QStringLiteral("%1 and %2 are both on %3.")
                    .arg(labelOf(a), labelOf(b), shownKey(left));
            }
            if (shortcuts::differOnlyByShift(left, right)) {
                return QStringLiteral(
                           "%1 (%2) and %3 (%4) are one chord on a keyboard whose digit row is "
                           "the shifted face of another -- AZERTY, for one.")
                    .arg(labelOf(a), shownKey(left), labelOf(b), shownKey(right));
            }
        }
    }
    return QStringLiteral("%1 and %2 collide.").arg(labelOf(a), labelOf(b));
}

}  // namespace

ShortcutsDialog::ShortcutsDialog(const shortcuts::Bindings& from, QWidget* parent)
    : QDialog(parent), editing_(from) {
    setWindowTitle(QStringLiteral("Keyboard shortcuts"));
    // Tall enough that the three open groups and the four folded headings under
    // them are all on screen at once: the folded headings are what say the list
    // goes on, and they only say it if they can be seen without scrolling.
    resize(780, 700);

    auto* layout = new QVBoxLayout(this);

    // Said rather than discovered. The staged edit is the unusual half -- most
    // programs refuse the chord as you type it -- and a dialog whose Apply is
    // greyed out with no sentence to say why is a dialog people close.
    auto* explains = new QLabel(
        QStringLiteral("Click a shortcut and press the keys you want. Nothing changes until "
                       "you press Apply, and Apply waits until no two of them collide."),
        this);
    explains->setWordWrap(true);
    layout->addWidget(explains);

    auto* find_row = new QHBoxLayout();
    find_row->addWidget(new QLabel(QStringLiteral("Search"), this));
    search_ = new QLineEdit(this);
    search_->setClearButtonEnabled(true);
    search_->setPlaceholderText(QStringLiteral("what it does, or the key it is on"));
    connect(search_, &QLineEdit::textChanged, this, &ShortcutsDialog::filterRows);
    find_row->addWidget(search_);
    layout->addLayout(find_row);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({QStringLiteral("Action"), QStringLiteral("Shortcut"), QString()});
    tree_->setRootIsDecorated(true);
    tree_->setUniformRowHeights(false);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(kWhat, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(kKey, QHeaderView::Fixed);
    tree_->header()->setSectionResizeMode(kReset, QHeaderView::Fixed);
    tree_->setColumnWidth(kKey, 230);
    tree_->setColumnWidth(kReset, 80);
    layout->addWidget(tree_, 1);

    clashes_ = new QLabel(this);
    clashes_->setWordWrap(true);
    layout->addWidget(clashes_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults,
        this);
    apply_ = buttons->button(QDialogButtonBox::Apply);
    apply_->setDefault(true);
    // Apply closes, because there is nothing to look at afterwards: the window
    // behind it is where a shortcut is tried out, not this.
    connect(apply_, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            &ShortcutsDialog::restoreDefaults);
    layout->addWidget(buttons);

    buildRows();
    syncClashes();
}

void ShortcutsDialog::buildRows() {
    for (const auto& [group, heading] : shortcuts::groups()) {
        auto* parent = new QTreeWidgetItem(tree_);
        parent->setText(kWhat, QString::fromUtf8(heading));
        QFont bold = parent->font(kWhat);
        bold.setBold(true);
        parent->setFont(kWhat, bold);
        parent->setFlags(Qt::ItemIsEnabled);
        parent->setExpanded(opensExpanded(group));
        if (group == shortcuts::Group::Held) {
            // The one group whose rows are worth explaining rather than listing:
            // nothing here can be changed, and the reason is what makes the rows
            // worth showing at all.
            parent->setText(kKey, QStringLiteral("held while you click or drag"));
        }
        headings_[group] = parent;
    }

    for (const Entry& entry : shortcuts::table()) {
        const auto heading = headings_.find(entry.group);
        if (heading == headings_.end()) continue;

        auto* item = new QTreeWidgetItem(heading->second);
        item->setText(kWhat, labelOf(entry.id));
        item->setData(kWhat, kIdRole, static_cast<int>(entry.id));
        Row row;
        row.item = item;

        if (entry.kind == shortcuts::Kind::Held) {
            // A label and not a disabled editor. A disabled field says "not now";
            // a plain key says "this is what it is", which is true.
            auto* fixed = new QLabel(shownKey(editing_.sequenceFor(entry.id)), tree_);
            fixed->setEnabled(false);
            tree_->setItemWidget(item, kKey, fixed);
            rows_[entry.id] = row;
            continue;
        }

        auto* edit = new QKeySequenceEdit(tree_);
        // One chord, never two. Every rule in shortcuts.cpp is written about a
        // single chord, and Qt's default of four would let a binding through that
        // the conflict check cannot reason about at all.
        edit->setMaximumSequenceLength(1);
        // How a row is unbound, which is a real thing to want: a key that cannot
        // be reached on somebody's keyboard is worth taking off an action.
        edit->setClearButtonEnabled(true);
        edit->setKeySequence(editing_.sequenceFor(entry.id));
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this,
                [this, id = entry.id] { rowEdited(id); });
        tree_->setItemWidget(item, kKey, edit);
        row.edit = edit;

        rows_[entry.id] = row;
        syncRow(entry.id);
    }
}

void ShortcutsDialog::rowEdited(Id id) {
    if (updating_) return;
    const auto found = rows_.find(id);
    if (found == rows_.end() || !found->second.edit) return;

    editing_.set(id, found->second.edit->keySequence());
    syncRow(id);
    syncClashes();
}

void ShortcutsDialog::resetRow(Id id) {
    editing_.reset(id);
    const auto found = rows_.find(id);
    if (found != rows_.end() && found->second.edit) {
        const QSignalBlocker quiet(found->second.edit);
        found->second.edit->setKeySequence(editing_.sequenceFor(id));
    }
    syncRow(id);
    syncClashes();
}

void ShortcutsDialog::restoreDefaults() {
    editing_.resetAll();
    updating_ = true;
    for (const auto& [id, row] : rows_) {
        if (row.edit) row.edit->setKeySequence(editing_.sequenceFor(id));
        syncRow(id);
    }
    updating_ = false;
    syncClashes();
}

void ShortcutsDialog::syncRow(Id id) {
    const auto found = rows_.find(id);
    if (found == rows_.end() || !found->second.edit) return;
    Row& row = found->second;

    // The Reset exists only where there is something to undo. Thirty of them on
    // a panel where nothing has been changed are thirty pieces of furniture; one,
    // beside the row you have just altered, is an offer.
    //
    // Made and unmade rather than shown and hidden: a widget handed to a tree is
    // one the view manages, and it shows it again on the next relayout -- which
    // is why the first version of this had a Reset on all thirty-odd rows.
    const bool changed = !editing_.isDefault(id);
    if (changed && !row.reset) {
        auto* reset = new QToolButton(tree_);
        reset->setText(QStringLiteral("Reset"));
        reset->setToolTip(QStringLiteral("Put this back on the key it came with"));
        connect(reset, &QToolButton::clicked, this, [this, id] { resetRow(id); });
        tree_->setItemWidget(row.item, kReset, reset);
        row.reset = reset;
    } else if (!changed && row.reset) {
        // The button may be the one that is being clicked right now. Qt takes the
        // old index widget away with deleteLater for exactly this, so the click
        // it is in the middle of finishes on a widget that is still there.
        tree_->removeItemWidget(row.item, kReset);
        row.reset = nullptr;
    }
}

void ShortcutsDialog::syncClashes() {
    const std::vector<std::pair<Id, Id>> found = editing_.clashes();

    // The rows the sentences are about, marked where they are rather than only
    // named underneath: a list of collisions with nothing to point at is a list
    // you have to hold in your head while you scroll.
    const QColor warned(190, 40, 40);
    for (auto& row : rows_) row.second.item->setForeground(kWhat, QBrush());

    QStringList said;
    for (const auto& [a, b] : found) {
        said << sentenceFor(editing_, a, b);
        for (const Id id : {a, b}) {
            const auto row = rows_.find(id);
            if (row != rows_.end()) row->second.item->setForeground(kWhat, warned);
        }
    }

    if (said.isEmpty()) {
        clashes_->setText(QString());
        apply_->setEnabled(true);
        return;
    }
    QPalette warning = clashes_->palette();
    warning.setColor(QPalette::WindowText, warned);
    clashes_->setPalette(warning);
    clashes_->setText(said.join(QLatin1Char('\n')));
    apply_->setEnabled(false);
}

void ShortcutsDialog::filterRows(const QString& text) {
    const QString wanted = text.trimmed();
    for (auto& group : headings_) {
        QTreeWidgetItem* heading = group.second;
        // A search has to reach into the folded groups, or half the list is
        // unsearchable and nothing says so. Folding them again when the box is
        // emptied puts the panel back the way it opens.
        heading->setExpanded(wanted.isEmpty() ? opensExpanded(group.first) : true);
        int shown = 0;
        for (int i = 0; i < heading->childCount(); ++i) {
            QTreeWidgetItem* item = heading->child(i);
            const Id id = static_cast<Id>(item->data(kWhat, kIdRole).toInt());
            const bool matches =
                wanted.isEmpty() ||
                item->text(kWhat).contains(wanted, Qt::CaseInsensitive) ||
                shownKey(editing_.sequenceFor(id)).contains(wanted, Qt::CaseInsensitive);
            item->setHidden(!matches);
            if (matches) ++shown;
        }
        // A heading over nothing reads as a group with no shortcuts in it rather
        // than as one the search has emptied.
        heading->setHidden(shown == 0);
    }
}
