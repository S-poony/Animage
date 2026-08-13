// SPDX-License-Identifier: GPL-3.0-or-later
#include "shortcuts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QList>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtGlobal>

namespace shortcuts {

namespace {

constexpr QKeySequence::StandardKey kSpelledOut = QKeySequence::UnknownKey;

// What the file says it is. An older build reading a newer file keeps its
// defaults and says so rather than guessing at what changed -- which for a
// settings file is the whole of the version's job.
constexpr int kFileVersion = 1;

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
        {Id::NewProject, "new-project", "&New", QKeySequence::New, 0, kAlways, Group::Project,
         Kind::Action},
        {Id::OpenProject, "open-project", "&Open...", QKeySequence::Open, 0, kAlways,
         Group::Project, Kind::Action},
        {Id::SaveProject, "save-project", "&Save", QKeySequence::Save, 0, kAlways, Group::Project,
         Kind::Action},
        {Id::SaveProjectAs, "save-project-as", "Save &As...", QKeySequence::SaveAs, 0, kAlways,
         Group::Project, Kind::Action},

        {Id::Undo, "undo", "&Undo", QKeySequence::Undo, 0, kAlways, Group::Editing, Kind::Action},
        // Ctrl+Shift+Z, not the Ctrl+Y that QKeySequence::Redo gives on Windows.
        {Id::Redo, "redo", "&Redo", kSpelledOut,
         QKeyCombination(Qt::CTRL | Qt::SHIFT | Qt::Key_Z).toCombined(), kAlways, Group::Editing,
         Kind::Action},

        // The clipboard is internal: the system one cannot carry half-float
        // precision or the CTG label encoding, and an image handed to another
        // program is a different feature with a different argument behind it.
        // The keys are the usual ones anyway, because the gesture is the usual
        // one.
        {Id::Cut, "cut", "Cu&t", QKeySequence::Cut, 0, kNormal, Group::Editing, Kind::Action},
        {Id::Copy, "copy", "&Copy", QKeySequence::Copy, 0, kNormal, Group::Editing, Kind::Action},
        {Id::Paste, "paste", "&Paste", QKeySequence::Paste, 0, kNormal, Group::Editing,
         Kind::Action},
        {Id::SelectAll, "select-all", "Select &all", QKeySequence::SelectAll, 0, kNormal,
         Group::Editing, Kind::Action},
        {Id::Deselect, "deselect", "&Deselect", kSpelledOut,
         QKeyCombination(Qt::CTRL | Qt::SHIFT | Qt::Key_A).toCombined(), kNormal, Group::Editing,
         Kind::Action},
        // Backspace erases what is selected and Delete still deletes the
        // drawing. The natural expectation on Delete is "erase what I selected"
        // and the existing binding is "delete this drawing", which is a bad
        // surprise in the dangerous direction; making it depend on whether a
        // selection exists is a bad surprise in the other. Two keys, no mode.
        {Id::EraseSelection, "erase-selection", "&Erase selection", kSpelledOut,
         QKeyCombination(Qt::Key_Backspace).toCombined(), kNormal, Group::Editing, Kind::Action},

        {Id::Play, "play", "Play", kSpelledOut, QKeyCombination(Qt::Key_Return).toCombined(),
         kNormal, Group::Timing, Kind::Action},
        {Id::PreviousFrame, "previous-frame", "Previous frame", kSpelledOut,
         QKeyCombination(Qt::Key_Left).toCombined(), kNormal, Group::Timing, Kind::Action},
        {Id::NextFrame, "next-frame", "Next frame", kSpelledOut,
         QKeyCombination(Qt::Key_Right).toCombined(), kNormal, Group::Timing, Kind::Action},
        // Up and down move by drawing, skipping over held frames -- the two
        // questions "what is next in time" and "what is the next drawing" are
        // different, and both get asked constantly.
        {Id::PreviousDrawing, "previous-drawing", "Previous drawing", kSpelledOut,
         QKeyCombination(Qt::Key_Up).toCombined(), kNormal, Group::Timing, Kind::Action},
        {Id::NextDrawing, "next-drawing", "Next drawing", kSpelledOut,
         QKeyCombination(Qt::Key_Down).toCombined(), kNormal, Group::Timing, Kind::Action},

        {Id::InsertDrawing, "insert-drawing", "Insert drawing", kSpelledOut,
         QKeyCombination(Qt::Key_Insert).toCombined(), kNormal, Group::Timing, Kind::Action},
        {Id::DuplicateDrawing, "duplicate-drawing", "Duplicate drawing", kSpelledOut,
         QKeyCombination(Qt::CTRL | Qt::Key_D).toCombined(), kNormal, Group::Timing, Kind::Action},
        {Id::DeleteDrawing, "delete-drawing", "Delete drawing", kSpelledOut,
         QKeyCombination(Qt::Key_Delete).toCombined(), kNormal, Group::Timing, Kind::Action},
        {Id::HoldLonger, "hold-longer", "Hold longer", kSpelledOut,
         QKeyCombination(Qt::Key_Plus).toCombined(), kNormal, Group::Timing, Kind::Action},
        {Id::HoldShorter, "hold-shorter", "Hold shorter", kSpelledOut,
         QKeyCombination(Qt::Key_Minus).toCombined(), kNormal, Group::Timing, Kind::Action},

        {Id::ActualSize, "actual-size", "Actual size", kSpelledOut,
         QKeyCombination(Qt::Key_1).toCombined(), kAlways, Group::Looking, Kind::Action},
        {Id::FitCanvas, "fit-canvas", "Fit canvas", kSpelledOut,
         QKeyCombination(Qt::Key_0).toCombined(), kAlways, Group::Looking, Kind::Action},
        // F and not Shift+0, which is issue #14 and is not a duplicate binding.
        //
        // Fit canvas is `0` and Fit drawing was `Shift+0`, two genuinely
        // different sequences that a conflict check over this table would pass.
        // They are the same physical chord on a keyboard whose digit row is the
        // shifted face of another row, which on AZERTY it is. See
        // differOnlyByShift, which is now the rule the dialog refuses on rather
        // than only the rule a test asserts.
        {Id::FitDrawing, "fit-drawing", "Fit drawing", kSpelledOut,
         QKeyCombination(Qt::Key_F).toCombined(), kAlways, Group::Looking, Kind::Action},

        {Id::Brush, "brush", "Brush", kSpelledOut, QKeyCombination(Qt::Key_B).toCombined(),
         kAlways, Group::Tools, Kind::Action},
        {Id::Eraser, "eraser", "Eraser", kSpelledOut, QKeyCombination(Qt::Key_E).toCombined(),
         kAlways, Group::Tools, Kind::Action},
        {Id::Lasso, "lasso", "Lasso", kSpelledOut, QKeyCombination(Qt::Key_L).toCombined(),
         kAlways, Group::Tools, Kind::Action},
        // A tool and not a button, because it competes with the brush for the
        // pen -- which is exactly what the exclusive group it joins means. It is
        // live in both modes: pressing it during a transform is how you would
        // reach for it, and pressing another tool commits, so the tools are the
        // way out as much as Apply is.
        {Id::Transform, "transform", "Transform", kSpelledOut,
         QKeyCombination(Qt::Key_T).toCombined(), kAlways, Group::Tools, Kind::Action},
        // [ and ] are what every drawing program uses; not having them is
        // jarring. In no menu -- but named all the same, because a row nobody
        // can name is a row nobody can rebind, and on AZERTY both of these need
        // AltGr and are the first two anybody will want to move.
        {Id::SmallerBrush, "smaller-brush", "Smaller brush", kSpelledOut,
         QKeyCombination(Qt::Key_BracketLeft).toCombined(), kAlways, Group::Tools, Kind::Action},
        {Id::LargerBrush, "larger-brush", "Larger brush", kSpelledOut,
         QKeyCombination(Qt::Key_BracketRight).toCombined(), kAlways, Group::Tools, Kind::Action},

        // The keys a live transform borrows, and the reason they are Kind::Canvas
        // rather than actions of their own: each is the key of an action that has
        // just been disabled, and a disabled QAction does not consume its
        // shortcut, so the key falls through to the canvas. The canvas is
        // therefore where they are handled -- but the *fact* belongs here, or
        // else nothing would stop a rebinding putting Fit drawing on Left and
        // taking the nudge away with nothing to say it had.
        {Id::TransformApply, "transform-apply", "Apply the transform", kSpelledOut,
         QKeyCombination(Qt::Key_Return).toCombined(), kTransform, Group::Transforming,
         Kind::Canvas},
        {Id::TransformCancel, "transform-cancel", "Cancel the transform", kSpelledOut,
         QKeyCombination(Qt::Key_Escape).toCombined(), kTransform, Group::Transforming,
         Kind::Canvas},
        // Shift is ten pixels instead of one, and that is not four more rows: it
        // is read as "the binding, with a Shift it does not have". See
        // Bindings::activates.
        {Id::NudgeLeft, "nudge-left", "Nudge left", kSpelledOut,
         QKeyCombination(Qt::Key_Left).toCombined(), kTransform, Group::Transforming,
         Kind::Canvas},
        {Id::NudgeRight, "nudge-right", "Nudge right", kSpelledOut,
         QKeyCombination(Qt::Key_Right).toCombined(), kTransform, Group::Transforming,
         Kind::Canvas},
        {Id::NudgeUp, "nudge-up", "Nudge up", kSpelledOut,
         QKeyCombination(Qt::Key_Up).toCombined(), kTransform, Group::Transforming, Kind::Canvas},
        {Id::NudgeDown, "nudge-down", "Nudge down", kSpelledOut,
         QKeyCombination(Qt::Key_Down).toCombined(), kTransform, Group::Transforming,
         Kind::Canvas},

        // Held rather than pressed, which is why they cannot be rebound: a
        // QAction fires and these last as long as the click or the drag does.
        //
        // Space and Z are here because they *consume* their key -- an action put
        // on Space would take the pan away silently, since an application-wide
        // shortcut wins the press before the canvas ever sees it.
        //
        // Alt is here for the other reason, and it was left out of the first
        // version for want of it: it consumes nothing (the canvas watches it
        // without accepting it, because it is also the menu bar's own key on
        // Windows), so there is no collision it could ever be in -- but the
        // panel is where somebody looks to find out what the keyboard does, and
        // an answer with the eyedropper missing from it is the wrong answer.
        {Id::PanView, "pan-view", "Pan the view", kSpelledOut,
         QKeyCombination(Qt::Key_Space).toCombined(), kAlways, Group::Held, Kind::Held},
        {Id::ZoomView, "zoom-view", "Zoom the view", kSpelledOut,
         QKeyCombination(Qt::Key_Z).toCombined(), kAlways, Group::Held, Kind::Held},
        // Alt+click on the drawing: the eyedropper.
        {Id::PickColour, "pick-colour", "Pick up a colour", kSpelledOut,
         QKeyCombination(Qt::Key_Alt).toCombined(), kAlways, Group::Held, Kind::Held},
        // Shift held while the pen goes down: the stroke is a straight line from
        // where it landed to where it lifts, at whatever angle the hand chose.
        //
        // Listed for the same reason Alt is -- it consumes nothing and so can
        // never be in a collision -- and it is kNormal rather than kAlways
        // because during a transform the same key means the fifteen-degree
        // constraint instead. One key, two modes, two meanings, and this row is
        // about the one the brush is under.
        {Id::StraightLine, "straight-line", "Draw a straight line", kSpelledOut,
         QKeyCombination(Qt::Key_Shift).toCombined(), kNormal, Group::Held, Kind::Held},
    };
    return rows;
}

// A chord the way a binding spells it, from a key event. Empty for anything that
// is not one.
QKeySequence chordOf(const QKeyEvent& event) {
    int key = event.key();
    // A modifier held on its own is not a chord, and asking whether it activates
    // something would answer about Qt::Key_Shift rather than about the key it is
    // waiting for.
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt ||
        key == Qt::Key_Meta || key == 0 || key == Qt::Key_unknown) {
        return {};
    }
    // The keypad's Return is Return. A binding on it means the key by that name
    // wherever the hand found it, and the keypad modifier is where "wherever"
    // would otherwise leak into the comparison.
    if (key == Qt::Key_Enter) key = Qt::Key_Return;
    const Qt::KeyboardModifiers modifiers = event.modifiers() & ~Qt::KeypadModifier;
    return QKeySequence(QKeyCombination(modifiers, static_cast<Qt::Key>(key)));
}

// The same chord with a Shift taken off it, or empty if it had none.
QKeySequence withoutShift(const QKeySequence& chord) {
    if (chord.count() != 1) return {};
    const QKeyCombination only = chord[0];
    if (!(only.keyboardModifiers() & Qt::ShiftModifier)) return {};
    return QKeySequence(
        QKeyCombination(only.keyboardModifiers() & ~Qt::ShiftModifier, only.key()));
}

}  // namespace

const std::vector<std::pair<Group, const char*>>& groups() {
    // The order somebody hunting reads in, which is not menu order: nobody opens
    // a shortcuts panel to find out what Save is on. What is left after the tools
    // and the timing is what a menu already tells you.
    static const std::vector<std::pair<Group, const char*>> order = {
        {Group::Tools, "Tools"},
        {Group::Timing, "Drawings and timing"},
        {Group::Looking, "Looking at the drawing"},
        {Group::Transforming, "While a transform is live"},
        {Group::Editing, "Editing"},
        {Group::Project, "The project"},
        {Group::Held, "Held down, not pressed"},
    };
    return order;
}

const std::vector<Entry>& table() { return built(); }

const Entry& entryFor(Id id) {
    for (const Entry& entry : built()) {
        if (entry.id == id) return entry;
    }
    // Unreachable: Id and the table are written together, and a row missing for
    // an id is a mistake at compile time in every practical sense. Answering
    // with a row rather than crashing keeps the interface buildable while it is
    // being extended.
    static const Entry nothing{id,      "",           "", QKeySequence::UnknownKey,
                               0,       kAlways,      Group::Tools,
                               Kind::Action};
    return nothing;
}

bool idForName(const QString& name, Id* found) {
    for (const Entry& entry : built()) {
        if (name == QLatin1String(entry.name)) {
            if (found) *found = entry.id;
            return true;
        }
    }
    return false;
}

std::vector<QKeySequence> sequencesFor(const Entry& entry) {
    if (entry.standard != QKeySequence::UnknownKey) {
        const QList<QKeySequence> bindings = QKeySequence::keyBindings(entry.standard);
        return std::vector<QKeySequence>(bindings.begin(), bindings.end());
    }
    if (entry.key == 0) return {};
    return {QKeySequence(entry.key)};
}

bool shareAMode(const Entry& a, const Entry& b) { return (a.modes & b.modes) != 0; }

bool sameChord(const QKeySequence& a, const QKeySequence& b) {
    return !a.isEmpty() && !b.isEmpty() && a == b;
}

bool differOnlyByShift(const QKeySequence& a, const QKeySequence& b) {
    if (a.count() != 1 || b.count() != 1) return false;
    const QKeyCombination first = a[0];
    const QKeyCombination second = b[0];
    if (first.key() != second.key()) return false;
    if (first.key() < Qt::Key_Space || first.key() > Qt::Key_AsciiTilde) return false;
    if (first.key() >= Qt::Key_A && first.key() <= Qt::Key_Z) return false;
    return (first.keyboardModifiers() ^ second.keyboardModifiers()) == Qt::ShiftModifier;
}

// --- what the keyboard is bound to now ------------------------------------

std::vector<QKeySequence> Bindings::sequencesFor(Id id) const {
    const auto changed = changed_.find(id);
    if (changed == changed_.end()) return shortcuts::sequencesFor(entryFor(id));
    if (changed->second.isEmpty()) return {};
    return {changed->second};
}

QKeySequence Bindings::sequenceFor(Id id) const {
    const std::vector<QKeySequence> all = sequencesFor(id);
    return all.empty() ? QKeySequence() : all.front();
}

bool Bindings::isDefault(Id id) const { return changed_.find(id) == changed_.end(); }

void Bindings::set(Id id, const QKeySequence& sequence) {
    // Putting a row back on its default by typing the default is the same fact
    // as never having touched it, and storing it would write a line to the file
    // that pins a default this build happens to have.
    const std::vector<QKeySequence> fallback = shortcuts::sequencesFor(entryFor(id));
    if (!fallback.empty() && fallback.front() == sequence) {
        changed_.erase(id);
        return;
    }
    changed_[id] = sequence;
}

void Bindings::reset(Id id) { changed_.erase(id); }

void Bindings::resetAll() { changed_.clear(); }

std::vector<Id> Bindings::clashesFor(Id id, const QKeySequence& sequence) const {
    std::vector<Id> found;
    if (sequence.isEmpty()) return found;  // unbound collides with nothing
    const Entry& mine = entryFor(id);
    for (const Entry& other : built()) {
        if (other.id == id) continue;
        if (!shareAMode(mine, other)) continue;
        for (const QKeySequence& theirs : sequencesFor(other.id)) {
            if (!collide(sequence, theirs)) continue;
            found.push_back(other.id);
            break;
        }
    }
    return found;
}

std::vector<std::pair<Id, Id>> Bindings::clashes() const {
    std::vector<std::pair<Id, Id>> found;
    const std::vector<Entry>& rows = built();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        for (std::size_t j = i + 1; j < rows.size(); ++j) {
            if (!shareAMode(rows[i], rows[j])) continue;
            bool clashed = false;
            for (const QKeySequence& left : sequencesFor(rows[i].id)) {
                for (const QKeySequence& right : sequencesFor(rows[j].id)) {
                    if (collide(left, right)) clashed = true;
                }
            }
            if (clashed) found.push_back({rows[i].id, rows[j].id});
        }
    }
    return found;
}

bool Bindings::activates(Id id, const QKeyEvent& event, bool* extra_shift) const {
    if (extra_shift) *extra_shift = false;
    const QKeySequence chord = chordOf(event);
    if (chord.isEmpty()) return false;

    for (const QKeySequence& bound : sequencesFor(id)) {
        if (sameChord(chord, bound)) return true;
    }
    const QKeySequence lighter = withoutShift(chord);
    if (lighter.isEmpty()) return false;
    for (const QKeySequence& bound : sequencesFor(id)) {
        if (!sameChord(lighter, bound)) continue;
        if (extra_shift) *extra_shift = true;
        return true;
    }
    return false;
}

// --- the file --------------------------------------------------------------

QByteArray Bindings::toJson() const {
    QJsonObject bindings;
    for (const auto& [id, sequence] : changed_) {
        // Portable and not native: the native spelling is localised, so a file
        // written on a German desktop says "Strg+S" and reads back as nothing at
        // all anywhere else.
        bindings.insert(QLatin1String(entryFor(id).name),
                        sequence.toString(QKeySequence::PortableText));
    }
    QJsonObject out;
    out.insert(QStringLiteral("version"), kFileVersion);
    out.insert(QStringLiteral("shortcuts"), bindings);
    return QJsonDocument(out).toJson(QJsonDocument::Indented);
}

bool Bindings::fromJson(const QByteArray& text, QString* trouble) {
    const auto refuse = [trouble](const QString& why) {
        if (trouble) *trouble = why;
        return false;
    };

    QJsonParseError problem{};
    const QJsonDocument root = QJsonDocument::fromJson(text, &problem);
    if (problem.error != QJsonParseError::NoError || !root.isObject()) {
        return refuse(QStringLiteral("not readable as JSON: %1").arg(problem.errorString()));
    }
    const QJsonObject object = root.object();
    const int version = object.value(QStringLiteral("version")).toInt(0);
    if (version > kFileVersion) {
        return refuse(QStringLiteral("shortcuts version %1 is newer than this build, which "
                                     "reads %2")
                          .arg(version)
                          .arg(kFileVersion));
    }
    const QJsonValue listed = object.value(QStringLiteral("shortcuts"));
    if (!listed.isObject()) return refuse(QStringLiteral("no shortcuts in it"));

    // Built alongside and adopted at the end, so a file that turns out to be
    // unreadable halfway through leaves the bindings it was going to replace
    // exactly as they were.
    std::map<Id, QKeySequence> read;
    const QJsonObject bindings = listed.toObject();
    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        Id id{};
        // A name this build has never heard of: an action from another version,
        // or one that has been taken out. Skipping is the only thing to do with
        // it, and it must not stop the rest of the file being read.
        if (!idForName(it.key(), &id)) continue;
        if (!it.value().isString()) continue;

        const QString spelled = it.value().toString();
        // The empty string is a row unbound on purpose and is not a parse
        // failure -- it is the one thing the dialog can produce that no key
        // sequence can express.
        if (spelled.isEmpty()) {
            read[id] = QKeySequence();
            continue;
        }
        const QKeySequence sequence(spelled, QKeySequence::PortableText);
        if (sequence.count() != 1) continue;              // no chord, or more than one
        if (sequence[0].key() == Qt::Key_unknown) continue;  // not a key this Qt knows
        read[id] = sequence;
    }
    changed_ = read;
    return true;
}

bool Bindings::load(const QString& path, QString* trouble) {
    if (trouble) trouble->clear();
    if (path.isEmpty()) {
        if (trouble) *trouble = QStringLiteral("nowhere to keep shortcuts on this platform");
        return false;
    }
    // Nothing rebound yet, which is the ordinary case and not a failure.
    if (!QFileInfo::exists(path)) return true;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (trouble) *trouble = QStringLiteral("cannot read %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return fromJson(file.readAll(), trouble);
}

bool Bindings::save(const QString& path, QString* trouble) const {
    if (trouble) trouble->clear();
    if (path.isEmpty()) {
        if (trouble) *trouble = QStringLiteral("nowhere to keep shortcuts on this platform");
        return false;
    }
    const QString folder = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(folder)) {
        if (trouble) {
            *trouble = QStringLiteral("cannot make %1").arg(QDir::toNativeSeparators(folder));
        }
        return false;
    }
    // Written whole or not at all. A settings file half replaced is one the next
    // start refuses, and refusing to start over a keyboard is the worst thing
    // this feature could do.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (trouble) {
            *trouble = QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    file.write(toJson());
    if (!file.commit()) {
        if (trouble) {
            *trouble = QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    return true;
}

Bindings& current() {
    static Bindings running;
    return running;
}

QString userFilePath() {
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (folder.isEmpty()) return {};
    return folder + QStringLiteral("/shortcuts.json");
}

}  // namespace shortcuts
