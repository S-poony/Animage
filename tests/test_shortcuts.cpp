// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shortcut table, and the two things it exists to make impossible.
//
// No window is built here. What is being asserted is a property of the table
// itself, and a test that needed the interface up to state it would be a slower
// test making a weaker claim.

#include <QGuiApplication>
#include <QKeySequence>

#include <string>
#include <vector>

#include "shortcuts.h"
#include "testing.h"

using shortcuts::Entry;
using shortcuts::Id;
using shortcuts::Mode;

namespace {

std::string nameOf(const Entry& entry) {
    // A row with no menu item of its own still has to be nameable in a failure,
    // and the key is the only thing it has.
    const std::vector<QKeySequence> keys = shortcuts::sequencesFor(entry);
    const std::string label = entry.label;
    const std::string key =
        keys.empty() ? std::string("no key") : keys.front().toString().toStdString();
    return (label.empty() ? key : label + " (" + key + ")");
}

bool shareAMode(const Entry& a, const Entry& b) { return (a.modes & b.modes) != 0; }

// Two bindings that are the same physical chord on a keyboard whose digit and
// punctuation rows are the shifted face of another row -- which on AZERTY they
// are. Producing `0` there means holding Shift, so a `0` binding and a `Shift+0`
// binding both match, Qt calls the shortcut ambiguous, and it cycles between the
// two rather than complaining. That is issue #14, and it presents as "sometimes
// the wrong thing happens".
//
// Two exemptions, and both are needed.
//
// Letters, because the unshifted face of a letter key is that letter on every
// layout: B and Shift+B are two chords a hand can tell apart, and forbidding the
// pair would cost bindings for nothing.
//
// And anything outside printable ASCII, because a layout can only hide a
// character behind Shift if it is one somebody types. Qt lists the dedicated
// Key_Save among the standard Save bindings and Shift+Key_Save among Save As's;
// on a keyboard with that key they are two chords, and on one without they are
// no chord at all.
bool differOnlyByShift(const QKeySequence& a, const QKeySequence& b) {
    if (a.count() != 1 || b.count() != 1) return false;
    const QKeyCombination first = a[0];
    const QKeyCombination second = b[0];
    if (first.key() != second.key()) return false;
    if (first.key() < Qt::Key_Space || first.key() > Qt::Key_AsciiTilde) return false;
    if (first.key() >= Qt::Key_A && first.key() <= Qt::Key_Z) return false;
    return (first.keyboardModifiers() ^ second.keyboardModifiers()) == Qt::ShiftModifier;
}

void everyRowIsUsable() {
    TEST("every row has a key and a mode it is live in");
    for (const Entry& entry : shortcuts::table()) {
        // A row with nothing bound to it is a row in the wrong table: this one
        // is what the keyboard does, and a menu item with no key belongs at its
        // call site.
        CHECK(!shortcuts::sequencesFor(entry).empty());
        CHECK(shortcuts::liveIn(entry.modes, Mode::Normal) ||
              shortcuts::liveIn(entry.modes, Mode::Transform));
        // entryFor is how everything else reaches a row, so a row it cannot
        // find is a row that does not exist as far as the window is concerned.
        CHECK(shortcuts::entryFor(entry.id).id == entry.id);
    }
}

void noTwoActionsInOneModeShareAKey() {
    TEST("no two actions live in the same mode share a key");
    const std::vector<Entry>& rows = shortcuts::table();

    for (std::size_t i = 0; i < rows.size(); ++i) {
        for (std::size_t j = i + 1; j < rows.size(); ++j) {
            if (!shareAMode(rows[i], rows[j])) continue;

            // Every sequence, not the first one: a standard key is a family, and
            // Qt answers with more than one binding on some platforms.
            for (const QKeySequence& left : shortcuts::sequencesFor(rows[i])) {
                for (const QKeySequence& right : shortcuts::sequencesFor(rows[j])) {
                    if (left.isEmpty() || right.isEmpty()) continue;
                    if (left != right) continue;
                    testing::fail(__FILE__, __LINE__,
                                  nameOf(rows[i]) + " and " + nameOf(rows[j]) +
                                      " are both on " + left.toString().toStdString() +
                                      " in the same mode");
                }
            }
            ++testing::g_checks;
        }
    }
}

void noTwoBindingsDifferOnlyByShift() {
    TEST("no two bindings in one mode differ only by Shift on a non-letter");
    const std::vector<Entry>& rows = shortcuts::table();

    for (std::size_t i = 0; i < rows.size(); ++i) {
        for (std::size_t j = i + 1; j < rows.size(); ++j) {
            if (!shareAMode(rows[i], rows[j])) continue;

            for (const QKeySequence& left : shortcuts::sequencesFor(rows[i])) {
                for (const QKeySequence& right : shortcuts::sequencesFor(rows[j])) {
                    if (!differOnlyByShift(left, right)) continue;
                    testing::fail(__FILE__, __LINE__,
                                  nameOf(rows[i]) + " and " + nameOf(rows[j]) + " are " +
                                      left.toString().toStdString() + " and " +
                                      right.toString().toStdString() +
                                      ", which are one chord where the digits are shifted");
                }
            }
            ++testing::g_checks;
        }
    }
}

// The disable list, asserted as a list rather than as a rule: what must go quiet
// while a transform is live was decided in docs/lasso-and-transform.md, and a
// test that derived it from the table would agree with whatever the table says.
void theTransformModeGivesUpTheRightKeys() {
    TEST("a live transform silences timing and leaves looking alone");

    const Id quiet[] = {Id::Play,           Id::PreviousFrame,    Id::NextFrame,
                        Id::PreviousDrawing, Id::NextDrawing,     Id::InsertDrawing,
                        Id::DuplicateDrawing, Id::DeleteDrawing,  Id::HoldLonger,
                        Id::HoldShorter};
    for (const Id id : quiet) {
        CHECK(!shortcuts::liveIn(shortcuts::entryFor(id).modes, Mode::Transform));
        CHECK(shortcuts::liveIn(shortcuts::entryFor(id).modes, Mode::Normal));
    }

    // Nothing about looking at the drawing may be given up, because judging a
    // placement means looking at it from another zoom. And Undo is not disabled
    // but redefined, so it stays live in both.
    const Id live[] = {Id::ActualSize, Id::FitCanvas, Id::FitDrawing,
                       Id::Undo,       Id::Redo,      Id::SaveProject};
    for (const Id id : live) {
        CHECK(shortcuts::liveIn(shortcuts::entryFor(id).modes, Mode::Transform));
    }
}

}  // namespace

int main(int argc, char** argv) {
    // A standard key is answered by the platform theme, which needs an
    // application to have been made. Without one Save comes back with no
    // bindings at all and every check about it passes vacuously.
    QGuiApplication app(argc, argv);

    std::printf("shortcuts:\n");
    everyRowIsUsable();
    noTwoActionsInOneModeShareAKey();
    noTwoBindingsDifferOnlyByShift();
    theTransformModeGivesUpTheRightKeys();
    return testing::summarise("shortcuts");
}
