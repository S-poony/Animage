// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QKeySequence>
#include <QString>
#include <map>
#include <utility>
#include <vector>

class QKeyEvent;

// What the keyboard does, and when -- and, now, what the person holding it says
// it does instead.
//
// This is issue #14, both halves. The first half was that every shortcut used to
// be a QKeySequence literal at one of fifteen call sites in buildActions, all of
// them ApplicationShortcut, which means they fire regardless of what the canvas
// thinks it is doing. That was fine while the program had no modes and stopped
// being fine the moment it had one.
//
// A live transform is the first mode: Return means Play normally and Validate
// during a transform, and the arrows mean step-frame normally and nudge during
// one. Writing that as setEnabled calls scattered through the transform code is
// how an action ends up stuck disabled after some cancel path nobody tested --
// so the fact lives in the table, and one function acts on it.
//
// The second half is rebinding, and what it changed here is that the table is no
// longer the answer to "what is this on" but only the answer to "what was it on
// to begin with". `Bindings` is the answer, and `current()` is the one the
// program is running on. Three things follow from that and are worth having in
// mind before changing anything:
//
//   - `name` is a stored name. It goes in the settings file and must never be
//     edited: changing one does not rename a rebinding, it silently discards it.
//     `Id` stays internal and can be renamed freely.
//   - Only what differs from the default is stored, so a default improved in a
//     later version still reaches everybody who never touched that action.
//   - The two conflict rules are product code rather than a test, because the
//     dialog has to apply the same ones the table is pinned by. A rule that
//     lives in the test can only say the defaults are fine.
namespace shortcuts {

// The modes an action can be live in, as a set of flags rather than a single
// mode, because "live in both" is the common answer and wants saying once.
enum Modes : unsigned {
    kNormal = 1u << 0,
    kTransform = 1u << 1,
    kAlways = kNormal | kTransform,
};

// Typing has no flag of its own and never will. A mode is a set of rows that
// stay live, and while a name is being typed into the answer for every row is
// the same one: none of them. Giving it a bit would mean a decision per row that
// is not a decision -- and the row that mattered is Play, which owns Return,
// which is the key that finishes a rename. Exactly the collision a live
// transform has with the same key, answered the same way.
enum class Mode { Normal, Transform, Typing };

// How the key reaches the thing it does.
//
// The distinction is not bookkeeping: it decides who is allowed to be missing an
// action in the window, which is a property test_canvas asserts, and it decides
// what the dialog will let you type into.
enum class Kind {
    // A QAction the window makes. Qt delivers the shortcut, and disabling the
    // action is what makes it stop consuming its key.
    Action,
    // The canvas reads it off the key event itself. These are exactly the keys a
    // live transform borrows: the action that owns each of them normally has
    // been disabled, and a disabled QAction does not consume its shortcut, so
    // the key falls through to whatever has the keyboard -- which is the canvas.
    // They are in the table so that the conflict check knows they are taken and
    // so that they can be rebound; the canvas asks `activates` rather than
    // naming Qt::Key_Return.
    Canvas,
    // Held down rather than pressed, for as long as a click or a drag lasts.
    // QAction cannot express that at all, so these cannot be rebound. They are
    // in the table for two reasons, and the first one alone would not have been
    // enough: two of them consume their key, so an action put on one would break
    // a pan and say nothing -- and all of them are things the keyboard does, and
    // the panel is where somebody goes to find out what that is.
    Held,
};

// Where a row appears in the dialog, and in what order the groups do.
//
// The order is a judgement about what somebody who has just opened the panel is
// hunting for, and it is deliberately not menu order: nobody opens a shortcuts
// panel to find out what Save is on. Tools and timing first, the project last.
enum class Group {
    Tools,
    Timing,
    Looking,
    Transforming,
    Editing,
    Project,
    Held,
};

// Every group, in the order the dialog shows them, with what its heading says.
const std::vector<std::pair<Group, const char*>>& groups();

enum class Id {
    NewProject,
    OpenProject,
    SaveProject,
    SaveProjectAs,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    SelectAll,
    Deselect,
    EraseSelection,
    Play,
    PreviousFrame,
    NextFrame,
    PreviousDrawing,
    NextDrawing,
    InsertDrawing,
    DuplicateDrawing,
    DeleteDrawing,
    HoldLonger,
    HoldShorter,
    ActualSize,
    FitCanvas,
    FitDrawing,
    TogglePanels,
    Brush,
    Eraser,
    Lasso,
    Transform,
    SmallerBrush,
    LargerBrush,
    TransformApply,
    TransformCancel,
    NudgeLeft,
    NudgeRight,
    NudgeUp,
    NudgeDown,
    TransformSymmetrical,
    TransformConstrain,
    PanView,
    ZoomView,
    PickColour,
    StraightLine,
};

struct Entry {
    Id id;
    // What a rebinding of this row is stored under. Never edit one: the settings
    // file is keyed by it, and a changed name reads as an action that no longer
    // exists, which silently throws the rebinding away.
    const char* name;
    // What the row is called, in a menu and in the shortcuts dialog. Every row
    // has one, including the two brush-size keys that are in no menu -- a row
    // nobody can name is a row nobody can rebind.
    const char* label;
    // A standard key is a family and not a sequence: Qt answers with whatever
    // the platform's convention is, and on some platforms with more than one.
    // UnknownKey means the binding is spelled out in `key` instead.
    QKeySequence::StandardKey standard;
    int key;  // Qt::Key with modifiers, combined; 0 when `standard` is used
    unsigned modes;
    Group group;
    Kind kind;
};

// Every row, in menu order. This is what the keyboard does *by default*; what it
// does now is `current()`.
const std::vector<Entry>& table();

// The row for an id. Every id has one -- the table is the definition.
const Entry& entryFor(Id id);

// The row a stored name belongs to. False means no row has that name, which is
// how a settings file written by another version is tolerated.
bool idForName(const QString& name, Id* found);

// Every sequence a row's default can actually produce. A standard key can be
// more than one, so anything comparing bindings has to compare them all: taking
// the first would let a second collide unnoticed.
std::vector<QKeySequence> sequencesFor(const Entry& entry);

inline bool liveIn(unsigned modes, Mode mode) {
    switch (mode) {
        // The keyboard belongs to the field. A disabled QAction does not consume
        // its shortcut, which is the whole mechanism: with Play disabled, Return
        // falls through to whatever has the keyboard.
        case Mode::Typing: return false;
        case Mode::Transform: return (modes & kTransform) != 0;
        case Mode::Normal: break;
    }
    return (modes & kNormal) != 0;
}

// Two rows that are ever live at the same time. Two that are not cannot collide,
// which is the whole reason Return can mean Play and also mean Apply.
bool shareAMode(const Entry& a, const Entry& b);

// The two ways two bindings collide, and the second is the one issue #14 was
// actually about.
//
// The first is the obvious one: the same chord twice.
//
// The second is a pair like `0` and `Shift+0`, which are genuinely different
// sequences and are one physical chord on a keyboard whose digit row is the
// shifted face of another row -- which on AZERTY it is. Producing `0` there means
// holding Shift, so both bindings match, Qt calls the shortcut ambiguous, and it
// *cycles between the candidates* rather than complaining. That is why it
// presents as "sometimes the wrong thing happens" instead of as an error.
//
// Two exemptions the second rule needs, both found by writing it. Letters,
// because the unshifted face of a letter key is that letter on every layout, so
// `B` and `Shift+B` are two chords a hand can tell apart -- and so Save and
// Save As are allowed to be Ctrl+S and Ctrl+Shift+S. And anything outside
// printable ASCII, because a layout can only hide a character behind Shift if it
// is one somebody types: Qt lists the dedicated Key_Save among the standard Save
// bindings and Shift+Key_Save among Save As's, which is a real pair on a keyboard
// that has such a key and no chord at all on one that has not.
bool sameChord(const QKeySequence& a, const QKeySequence& b);
bool differOnlyByShift(const QKeySequence& a, const QKeySequence& b);
inline bool collide(const QKeySequence& a, const QKeySequence& b) {
    return sameChord(a, b) || differOnlyByShift(a, b);
}

// What the keyboard is bound to now: the table's defaults with the user's
// changes over the top.
//
// A value type on purpose. The dialog edits a copy and installs it when Apply is
// pressed, which is what lets a conflicting chord be typed in and *looked at* --
// named beside the action it collides with -- rather than refused as it is typed
// by a dialog that will not say why.
class Bindings {
public:
    // Every sequence this row produces now. A standard key left alone is still a
    // family and can be more than one; anything rebound is exactly one.
    std::vector<QKeySequence> sequencesFor(Id id) const;
    // The one to show, and the one to put on a QAction. Empty means the row has
    // been unbound on purpose, which is allowed: a key that cannot be reached on
    // somebody's keyboard is worth being able to take off an action entirely.
    QKeySequence sequenceFor(Id id) const;

    bool isDefault(Id id) const;
    bool anyChanged() const { return !changed_.empty(); }

    // No conflict checking here. Storing what was asked for and asking about
    // collisions separately is what lets the dialog show the collision.
    void set(Id id, const QKeySequence& sequence);
    void reset(Id id);
    void resetAll();

    // Every row that `id` on `sequence` would collide with. Empty means it is
    // free. The row itself is never in the answer, and neither is a row that is
    // never live at the same time.
    std::vector<Id> clashesFor(Id id, const QKeySequence& sequence) const;
    // Every collision these bindings contain, each pair once, lower id first.
    // Empty is what the dialog's Apply waits for.
    std::vector<std::pair<Id, Id>> clashes() const;

    // Does this key event produce the binding for `id`?
    //
    // `extra_shift` says the event carried a Shift the binding does not, which is
    // how the ten-pixel nudge is said without four more rows in the table for it.
    // Keypad Enter counts as Return and the keypad modifier is ignored, because a
    // binding on Return means the key called Return wherever the hand found it.
    bool activates(Id id, const QKeyEvent& event, bool* extra_shift = nullptr) const;

    // Only what differs from the default, keyed by stored name, as text: a
    // default improved in a later version then still reaches everybody who never
    // touched that action. Sequences are written in Qt's portable spelling and
    // not the native one, which is localised -- "Strg+S" is what a German
    // desktop calls Ctrl+S and is not what another machine will read back.
    QByteArray toJson() const;
    // Tolerant on purpose, and it reports rather than throws: a settings file is
    // the one file the program cannot refuse to start over. A name it does not
    // know, a value that is not a string, a chord it cannot parse and a sequence
    // of more than one chord are each skipped, and false means the file as a
    // whole could not be read -- in which case nothing has been changed.
    bool fromJson(const QByteArray& text, QString* trouble);

    // A file that is not there is not a failure: it means nothing has been
    // rebound, which is the ordinary case.
    bool load(const QString& path, QString* trouble = nullptr);
    bool save(const QString& path, QString* trouble = nullptr) const;

private:
    // What the user changed, and nothing else. An id absent from here is on its
    // default, which is the property the whole file format rests on.
    std::map<Id, QKeySequence> changed_;
};

// The bindings the program is running on. Loaded once, from main, so that a test
// or a screenshot drives the defaults rather than whatever the person running it
// has rebound.
Bindings& current();

// Where the rebindings live: shortcuts.json in the platform's per-user config
// directory, which on Windows is %APPDATA%/Animage. Empty if the platform will
// not name one, which is the one case rebinding cannot be saved at all.
QString userFilePath();

}  // namespace shortcuts
