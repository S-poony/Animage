// SPDX-License-Identifier: GPL-3.0-or-later
#include "shortcuts.h"

#include <QList>
#include <QtGlobal>

namespace shortcuts {

namespace {

constexpr QKeySequence::StandardKey kSpelledOut = QKeySequence::UnknownKey;

// The disable list, from docs/lasso-and-transform.md: what must go quiet while a
// transform is live, and what must not.
//
// Quiet: Play, the frame and drawing steps, Insert, Duplicate, Delete drawing,
// Hold longer and Hold shorter. A disabled QAction does not consume its
// shortcut, so disabling Play is precisely what frees Return to validate with
// and disabling the steps is what frees the arrows to nudge with.
//
// Live: everything about *looking* at the drawing. Zoom, Actual size, Fit
// canvas and Fit drawing stay because judging a placement means looking at it
// from another zoom, and a transform that ends when you look at it is unusable
// exactly when it is working.
//
// Undo and Redo are the exception that is neither: they are not disabled but
// redefined, so they stay live here and the handler asks what mode it is in.
const std::vector<Entry>& built() {
    static const std::vector<Entry> rows = {
        {Id::NewProject, "&New", QKeySequence::New, 0, kAlways},
        {Id::OpenProject, "&Open...", QKeySequence::Open, 0, kAlways},
        {Id::SaveProject, "&Save", QKeySequence::Save, 0, kAlways},
        {Id::SaveProjectAs, "Save &As...", QKeySequence::SaveAs, 0, kAlways},

        {Id::Undo, "&Undo", QKeySequence::Undo, 0, kAlways},
        // Ctrl+Shift+Z, not the Ctrl+Y that QKeySequence::Redo gives on Windows.
        {Id::Redo, "&Redo", kSpelledOut,
         QKeyCombination(Qt::CTRL | Qt::SHIFT | Qt::Key_Z).toCombined(), kAlways},

        {Id::SelectAll, "Select &all", QKeySequence::SelectAll, 0, kNormal},
        {Id::Deselect, "&Deselect", kSpelledOut,
         QKeyCombination(Qt::CTRL | Qt::SHIFT | Qt::Key_A).toCombined(), kNormal},
        // Backspace erases what is selected and Delete still deletes the
        // drawing. The natural expectation on Delete is "erase what I selected"
        // and the existing binding is "delete this drawing", which is a bad
        // surprise in the dangerous direction; making it depend on whether a
        // selection exists is a bad surprise in the other. Two keys, no mode.
        {Id::EraseSelection, "&Erase selection", kSpelledOut,
         QKeyCombination(Qt::Key_Backspace).toCombined(), kNormal},

        {Id::Play, "Play", kSpelledOut, QKeyCombination(Qt::Key_Return).toCombined(), kNormal},
        {Id::PreviousFrame, "Previous frame", kSpelledOut,
         QKeyCombination(Qt::Key_Left).toCombined(), kNormal},
        {Id::NextFrame, "Next frame", kSpelledOut, QKeyCombination(Qt::Key_Right).toCombined(),
         kNormal},
        // Up and down move by drawing, skipping over held frames -- the two
        // questions "what is next in time" and "what is the next drawing" are
        // different, and both get asked constantly.
        {Id::PreviousDrawing, "Previous drawing", kSpelledOut,
         QKeyCombination(Qt::Key_Up).toCombined(), kNormal},
        {Id::NextDrawing, "Next drawing", kSpelledOut, QKeyCombination(Qt::Key_Down).toCombined(),
         kNormal},

        {Id::InsertDrawing, "Insert drawing", kSpelledOut,
         QKeyCombination(Qt::Key_Insert).toCombined(), kNormal},
        {Id::DuplicateDrawing, "Duplicate drawing", kSpelledOut,
         QKeyCombination(Qt::CTRL | Qt::Key_D).toCombined(), kNormal},
        {Id::DeleteDrawing, "Delete drawing", kSpelledOut,
         QKeyCombination(Qt::Key_Delete).toCombined(), kNormal},
        {Id::HoldLonger, "Hold longer", kSpelledOut, QKeyCombination(Qt::Key_Plus).toCombined(),
         kNormal},
        {Id::HoldShorter, "Hold shorter", kSpelledOut, QKeyCombination(Qt::Key_Minus).toCombined(),
         kNormal},

        {Id::ActualSize, "Actual size", kSpelledOut, QKeyCombination(Qt::Key_1).toCombined(),
         kAlways},
        {Id::FitCanvas, "Fit canvas", kSpelledOut, QKeyCombination(Qt::Key_0).toCombined(),
         kAlways},
        // F and not Shift+0, which is issue #14 and is not a duplicate binding.
        //
        // Fit canvas is `0` and Fit drawing was `Shift+0`, two genuinely
        // different sequences that a conflict check over this table would pass.
        // They are the same physical chord on a keyboard whose digit row is the
        // shifted face of another row, which on AZERTY it is: producing `0` at
        // all means holding Shift, so Qt finds both bindings, calls the shortcut
        // ambiguous and cycles between them -- which presents as "sometimes the
        // wrong thing happens" rather than as an error.
        //
        // No digit is safe from that, so the fix is to leave the digits alone
        // rather than to pick a different one. The rule is pinned in
        // test_shortcuts: within one mode, no two bindings may differ only by
        // Shift on a key that is not a letter.
        {Id::FitDrawing, "Fit drawing", kSpelledOut, QKeyCombination(Qt::Key_F).toCombined(),
         kAlways},

        {Id::Brush, "Brush", kSpelledOut, QKeyCombination(Qt::Key_B).toCombined(), kAlways},
        {Id::Eraser, "Eraser", kSpelledOut, QKeyCombination(Qt::Key_E).toCombined(), kAlways},
        {Id::Lasso, "Lasso", kSpelledOut, QKeyCombination(Qt::Key_L).toCombined(), kAlways},
        // A tool and not a button, because it competes with the brush for the
        // pen -- which is exactly what the exclusive group it joins means. It is
        // live in both modes: pressing it during a transform is how you would
        // reach for it, and pressing another tool commits, so the tools are the
        // way out as much as Apply is.
        {Id::Transform, "Transform", kSpelledOut, QKeyCombination(Qt::Key_T).toCombined(),
         kAlways},
        // [ and ] are what every drawing program uses; not having them is
        // jarring. No menu item of their own, hence no label.
        {Id::SmallerBrush, "", kSpelledOut, QKeyCombination(Qt::Key_BracketLeft).toCombined(),
         kAlways},
        {Id::LargerBrush, "", kSpelledOut, QKeyCombination(Qt::Key_BracketRight).toCombined(),
         kAlways},
    };
    return rows;
}

}  // namespace

const std::vector<Entry>& table() { return built(); }

const Entry& entryFor(Id id) {
    for (const Entry& entry : built()) {
        if (entry.id == id) return entry;
    }
    // Unreachable: Id and the table are written together, and a row missing for
    // an id is a mistake at compile time in every practical sense. Answering
    // with a row rather than crashing keeps the interface buildable while it is
    // being extended.
    static const Entry nothing{id, "", QKeySequence::UnknownKey, 0, kAlways};
    return nothing;
}

std::vector<QKeySequence> sequencesFor(const Entry& entry) {
    if (entry.standard != QKeySequence::UnknownKey) {
        const QList<QKeySequence> bindings = QKeySequence::keyBindings(entry.standard);
        return std::vector<QKeySequence>(bindings.begin(), bindings.end());
    }
    if (entry.key == 0) return {};
    return {QKeySequence(entry.key)};
}

}  // namespace shortcuts
