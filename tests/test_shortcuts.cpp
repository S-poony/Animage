// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shortcut table, the two things it exists to make impossible, and what
// happens once the person holding the keyboard is allowed to change it.
//
// No window is built here. What is being asserted is a property of the table and
// of `Bindings`, and a test that needed the interface up to state it would be a
// slower test making a weaker claim.
//
// The rules themselves moved out of this file when the dialog needed them --
// `sameChord` and `differOnlyByShift` are product code now, because a rule that
// only a test knows can only say the *defaults* are fine. So there are two kinds
// of check here: that the defaults obey the rules, and that the rules would
// notice if they did not.

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>

#include <set>
#include <string>
#include <vector>

#include "shortcuts.h"
#include "testing.h"

using shortcuts::Bindings;
using shortcuts::Entry;
using shortcuts::Id;
using shortcuts::Kind;
using shortcuts::Mode;

namespace {

std::string nameOf(const Entry& entry) { return entry.label; }

std::string spell(const QKeySequence& sequence) {
    return sequence.toString(QKeySequence::PortableText).toStdString();
}

QKeySequence chord(const char* portable) {
    return QKeySequence(QString::fromUtf8(portable), QKeySequence::PortableText);
}

void everyRowIsUsable() {
    TEST("every row has a name, a label, a key and a mode it is live in");
    std::set<std::string> names;
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

        // A row nobody can name is a row nobody can rebind, which is why the two
        // brush-size keys have labels now despite being in no menu.
        CHECK(entry.label != nullptr && *entry.label != '\0');

        // And the stored name, which is the one that must not be edited: it is
        // what a settings file is keyed by. Two rows sharing one would make a
        // rebinding of either land on whichever came first.
        const std::string name = entry.name;
        CHECK(!name.empty());
        CHECK(names.insert(name).second);
        for (const char c : name) {
            CHECK((c >= 'a' && c <= 'z') || c == '-');
        }
        Id round_trip{};
        CHECK(shortcuts::idForName(QString::fromStdString(name), &round_trip));
        CHECK(round_trip == entry.id);
    }
    // A name from another version, or one taken out of the table since. The file
    // reader leans on this answer rather than on the name being there.
    CHECK(!shortcuts::idForName(QStringLiteral("not-an-action"), nullptr));
}

void theDefaultsCollideWithNothing() {
    TEST("no two default bindings live in the same mode collide");
    // Through the same call the dialog's Apply is gated on, so the claim is
    // about what the program will actually refuse rather than about a rule
    // written twice.
    const Bindings defaults;
    for (const auto& [a, b] : defaults.clashes()) {
        testing::fail(__FILE__, __LINE__,
                      nameOf(shortcuts::entryFor(a)) + " and " + nameOf(shortcuts::entryFor(b)) +
                          " collide on the defaults");
    }
    CHECK(defaults.clashes().empty());
}

// The rules would notice. Asserting only that the defaults pass is how a rule
// that has quietly stopped working goes on looking green forever.
void theRulesCatchWhatTheyAreFor() {
    TEST("the two collision rules catch what they were written for");

    CHECK(shortcuts::sameChord(chord("Ctrl+S"), chord("Ctrl+S")));
    CHECK(!shortcuts::sameChord(chord("Ctrl+S"), chord("Ctrl+D")));
    // An unbound row is not a collision with every other unbound row.
    CHECK(!shortcuts::sameChord(QKeySequence(), QKeySequence()));

    // Issue #14 itself: two genuinely different sequences that are one physical
    // chord where the digit row is the shifted face of another.
    CHECK(shortcuts::differOnlyByShift(chord("0"), chord("Shift+0")));
    CHECK(shortcuts::collide(chord("0"), chord("Shift+0")));

    // And the two exemptions, both of which cost real bindings if they go.
    // A letter key's unshifted face is that letter on every layout...
    CHECK(!shortcuts::differOnlyByShift(chord("B"), chord("Shift+B")));
    // ...which is what lets Save and Save As be Ctrl+S and Ctrl+Shift+S.
    CHECK(!shortcuts::differOnlyByShift(chord("Ctrl+S"), chord("Ctrl+Shift+S")));
    // And a key outside printable ASCII cannot be hidden behind Shift by a
    // layout, because a layout can only do that to a character somebody types.
    CHECK(!shortcuts::differOnlyByShift(chord("Left"), chord("Shift+Left")));

    // A modifier other than Shift is a different chord and not this rule's
    // business.
    CHECK(!shortcuts::differOnlyByShift(chord("0"), chord("Ctrl+0")));
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

    // And what takes the freed keys. These are rows so that a rebinding cannot
    // put another action on Left without anything noticing -- which is the whole
    // reason they stopped being Qt::Key_Left in the canvas.
    const Id borrowed[] = {Id::TransformApply, Id::TransformCancel, Id::NudgeLeft,
                           Id::NudgeRight,     Id::NudgeUp,         Id::NudgeDown};
    for (const Id id : borrowed) {
        const Entry& entry = shortcuts::entryFor(id);
        CHECK(entry.kind == Kind::Canvas);
        CHECK(shortcuts::liveIn(entry.modes, Mode::Transform));
        // Live in Normal too and they would collide with Play and the steps,
        // which is precisely the arrangement they exist to replace.
        CHECK(!shortcuts::liveIn(entry.modes, Mode::Normal));
    }

    // The held keys, which are in the table for two reasons. Space and Z consume
    // their key, so an action rebound onto Space would take the pan away
    // silently. Alt consumes nothing and could not collide with anything -- it
    // is listed because the panel is where somebody finds out what the keyboard
    // does, and an answer with the eyedropper missing is the wrong answer.
    // Shift is here for Alt's reason and not Space's: it is the straight line,
    // it consumes nothing, and it is listed because the panel is the answer to
    // "what does the keyboard do".
    for (const Id id : {Id::PanView, Id::ZoomView, Id::PickColour, Id::StraightLine}) {
        CHECK(shortcuts::entryFor(id).kind == Kind::Held);
    }
    const Bindings defaults;
    CHECK(defaults.clashesFor(Id::Brush, chord("Space")).size() == 1);
    CHECK(defaults.clashesFor(Id::Brush, chord("Space")).front() == Id::PanView);
    // And a bare modifier is a row nothing can be put on top of: no chord the
    // panel can record is Alt or Shift by itself.
    CHECK(defaults.clashesFor(Id::Brush, chord("Alt+B")).empty());
    CHECK(defaults.clashesFor(Id::Brush, chord("Shift+B")).empty());
    // Which is the claim worth pinning about the straight line's row rather
    // than the one about it colliding: Shift is a modifier on every other row in
    // the table, so a rule that read this one as a chord would report the whole
    // of Ctrl+Shift+Z, Save As and the ten-pixel nudge as hitting it.
    CHECK(defaults.clashesFor(Id::StraightLine, chord("Shift")).empty());
    // Named, because a row nobody can name is a row nobody can find.
    CHECK(!defaults.sequenceFor(Id::StraightLine).isEmpty());
    CHECK(defaults.sequenceFor(Id::StraightLine).toString() == QStringLiteral("Shift"));
}

void whatWouldCollideIsAskedBeforeItIsDone() {
    TEST("clashesFor names what a proposed binding would hit");
    Bindings bindings;

    // The bug this issue was raised for, asked as the dialog asks it.
    const std::vector<Id> hit = bindings.clashesFor(Id::FitDrawing, chord("Shift+0"));
    CHECK(hit.size() == 1);
    if (!hit.empty()) CHECK(hit.front() == Id::FitCanvas);

    // The plain duplicate, which is the case everybody expects.
    CHECK(bindings.clashesFor(Id::Brush, chord("0")).size() == 1);

    // A free key is free.
    CHECK(bindings.clashesFor(Id::Brush, chord("Q")).empty());

    // A row never collides with itself, or nothing could be left where it is.
    CHECK(bindings.clashesFor(Id::Brush, chord("B")).empty());

    // Two rows that are never live at once cannot collide, which is the whole
    // reason Return can mean Play and also mean Apply. Escape belongs to the
    // transform and Play belongs to Normal.
    CHECK(bindings.clashesFor(Id::Play, chord("Esc")).empty());

    // Unbinding collides with nothing, which is what makes it a way out of a
    // collision rather than another one.
    CHECK(bindings.clashesFor(Id::Brush, QKeySequence()).empty());

    // And the answer follows what has already been changed rather than the
    // defaults: Q is free until something is put on it.
    bindings.set(Id::Eraser, chord("Q"));
    CHECK(bindings.clashesFor(Id::Brush, chord("Q")).size() == 1);
    // Moving the eraser there is not itself a collision, or the check would be
    // answering about the table rather than about these bindings.
    CHECK(bindings.clashes().empty());
}

void changingOneChangesOnlyThatOne() {
    TEST("a rebinding is one row, and putting the default back is not a change");
    Bindings bindings;
    CHECK(bindings.isDefault(Id::FitDrawing));
    CHECK(!bindings.anyChanged());

    bindings.set(Id::FitDrawing, chord("Ctrl+Shift+F"));
    CHECK(!bindings.isDefault(Id::FitDrawing));
    CHECK_EQ(spell(bindings.sequenceFor(Id::FitDrawing)), std::string("Ctrl+Shift+F"));
    // And nothing else moved.
    CHECK(bindings.isDefault(Id::FitCanvas));
    CHECK_EQ(spell(bindings.sequenceFor(Id::FitCanvas)), std::string("0"));

    bindings.reset(Id::FitDrawing);
    CHECK(bindings.isDefault(Id::FitDrawing));
    CHECK_EQ(spell(bindings.sequenceFor(Id::FitDrawing)), std::string("F"));

    // Typing the default back in is the same fact as never having touched it.
    // Storing it would write a line to the settings file pinning a default that
    // a later version is then unable to improve.
    bindings.set(Id::FitDrawing, chord("F"));
    CHECK(bindings.isDefault(Id::FitDrawing));
    CHECK(!bindings.anyChanged());

    // An unbound row is a row with no key, and is not the same as a default one.
    bindings.set(Id::FitDrawing, QKeySequence());
    CHECK(!bindings.isDefault(Id::FitDrawing));
    CHECK(bindings.sequenceFor(Id::FitDrawing).isEmpty());
    CHECK(bindings.sequencesFor(Id::FitDrawing).empty());

    bindings.resetAll();
    CHECK(!bindings.anyChanged());
}

void aStandardKeyIsStillAFamilyUntilItIsMoved() {
    TEST("a standard key answers with every binding the platform gives it");
    const Bindings bindings;
    // Save is whatever the platform says, and on some platforms that is more
    // than one sequence. Anything comparing bindings has to compare them all --
    // taking the first would let a second collide unnoticed.
    CHECK(!bindings.sequencesFor(Id::SaveProject).empty());

    Bindings moved;
    moved.set(Id::SaveProject, chord("Ctrl+Alt+S"));
    // Rebound, it is exactly one.
    CHECK(moved.sequencesFor(Id::SaveProject).size() == 1);
}

void aKeyEventIsAskedAboutRatherThanNamed() {
    TEST("activates answers for the key that is bound, and Shift is the big step");
    const Bindings bindings;

    const QKeyEvent returned(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    CHECK(bindings.activates(Id::TransformApply, returned));
    CHECK(!bindings.activates(Id::TransformCancel, returned));

    // The keypad's Enter is Return: a binding on Return means the key by that
    // name wherever the hand found it.
    const QKeyEvent keypad(QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier);
    CHECK(bindings.activates(Id::TransformApply, keypad));

    bool bigger = true;
    const QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    CHECK(bindings.activates(Id::NudgeLeft, left, &bigger));
    CHECK(!bigger);

    // Ten pixels is "the binding, with a Shift it does not have", which is what
    // keeps it working when the nudge keys are moved somewhere else.
    const QKeyEvent shifted(QEvent::KeyPress, Qt::Key_Left, Qt::ShiftModifier);
    CHECK(bindings.activates(Id::NudgeLeft, shifted, &bigger));
    CHECK(bigger);

    // A modifier held on its own is not a chord and must not answer for the key
    // it is waiting for.
    const QKeyEvent alone(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
    CHECK(!bindings.activates(Id::NudgeLeft, alone));

    // And once it is moved, the key it was on stops meaning it. This is the
    // check that fails if anything goes back to naming Qt::Key_Return.
    Bindings moved;
    moved.set(Id::TransformApply, chord("Ctrl+K"));
    CHECK(!moved.activates(Id::TransformApply, returned));
    const QKeyEvent ctrl_k(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
    CHECK(moved.activates(Id::TransformApply, ctrl_k));
}

void onlyWhatChangedIsWrittenDown() {
    TEST("the file holds the changes and nothing else, and reads back the same");
    Bindings bindings;
    bindings.set(Id::FitDrawing, chord("Ctrl+Shift+F"));
    bindings.set(Id::SmallerBrush, QKeySequence());  // unbound on purpose

    const QByteArray text = bindings.toJson();
    const QJsonObject listed =
        QJsonDocument::fromJson(text).object().value(QStringLiteral("shortcuts")).toObject();
    // Two rows moved, so two lines. A default improved in a later version has to
    // reach everybody who never touched that action, and it cannot if every
    // action is written down.
    CHECK_EQ(listed.size(), 2);
    CHECK(listed.contains(QStringLiteral("fit-drawing")));
    CHECK(listed.contains(QStringLiteral("smaller-brush")));
    CHECK_EQ(listed.value(QStringLiteral("fit-drawing")).toString().toStdString(),
             std::string("Ctrl+Shift+F"));
    // Unbound is the empty string, which is the one thing the dialog can produce
    // that no key sequence spells.
    CHECK_EQ(listed.value(QStringLiteral("smaller-brush")).toString().toStdString(),
             std::string(""));

    Bindings read;
    QString trouble;
    CHECK(read.fromJson(text, &trouble));
    CHECK(trouble.isEmpty());
    CHECK_EQ(spell(read.sequenceFor(Id::FitDrawing)), std::string("Ctrl+Shift+F"));
    CHECK(read.sequenceFor(Id::SmallerBrush).isEmpty());
    CHECK(!read.isDefault(Id::SmallerBrush));
    CHECK(read.isDefault(Id::FitCanvas));
}

void aFileFromSomewhereElseIsSteppedOverRatherThanObeyed() {
    TEST("a settings file is read as far as it makes sense and no further");

    // A name this build has never heard of -- an action from another version, or
    // one taken out since. Skipping it must not stop the rest being read.
    Bindings mixed;
    QString trouble;
    CHECK(mixed.fromJson(R"({"version":1,"shortcuts":{
              "not-an-action":"Ctrl+J",
              "fit-drawing":"Ctrl+Shift+F"}})",
                         &trouble));
    CHECK_EQ(spell(mixed.sequenceFor(Id::FitDrawing)), std::string("Ctrl+Shift+F"));

    // A value that is not a string, a chord Qt cannot parse, and a sequence of
    // more than one chord -- every rule here is written about a single chord, so
    // a two-chord binding is one the collision check could not reason about.
    Bindings junk;
    CHECK(junk.fromJson(R"({"version":1,"shortcuts":{
              "fit-drawing":7,
              "fit-canvas":"NotAKeyAtAll",
              "brush":"Ctrl+K, Ctrl+B"}})",
                        &trouble));
    CHECK(junk.isDefault(Id::FitDrawing));
    CHECK(junk.isDefault(Id::FitCanvas));
    CHECK(junk.isDefault(Id::Brush));

    // Not JSON at all, and JSON that is not this file: reported, and nothing
    // adopted. A settings file is the one file the program cannot refuse to
    // start over, so the answer is always "carry on with the defaults".
    Bindings kept;
    kept.set(Id::Brush, chord("Ctrl+B"));
    CHECK(!kept.fromJson("{not json", &trouble));
    CHECK(!trouble.isEmpty());
    CHECK(!kept.fromJson(R"({"version":1})", &trouble));
    CHECK_EQ(spell(kept.sequenceFor(Id::Brush)), std::string("Ctrl+B"));

    // A file from a newer build. Guessing at what changed is worse than saying
    // so and keeping the defaults.
    Bindings ahead;
    CHECK(!ahead.fromJson(R"({"version":99,"shortcuts":{"brush":"Ctrl+B"}})", &trouble));
    CHECK(trouble.contains(QStringLiteral("newer")));
    CHECK(ahead.isDefault(Id::Brush));
}

void itSurvivesTheRoundTripThroughADisk() {
    TEST("saved and loaded again says the same thing");
    const QString path = QDir(QDir::tempPath())
                             .filePath(QStringLiteral("animage-shortcuts-test/shortcuts.json"));
    QFile::remove(path);

    Bindings written;
    written.set(Id::FitDrawing, chord("Ctrl+Shift+F"));
    QString trouble;
    CHECK(written.save(path, &trouble));
    CHECK(trouble.isEmpty());

    Bindings read;
    CHECK(read.load(path, &trouble));
    CHECK_EQ(spell(read.sequenceFor(Id::FitDrawing)), std::string("Ctrl+Shift+F"));

    // A file that is not there means nothing has been rebound, which is the
    // ordinary case on every first start and is not a failure.
    QFile::remove(path);
    Bindings missing;
    CHECK(missing.load(path, &trouble));
    CHECK(trouble.isEmpty());
    CHECK(!missing.anyChanged());
    QDir(QDir::tempPath()).rmdir(QStringLiteral("animage-shortcuts-test"));
}

}  // namespace

int main(int argc, char** argv) {
    // A standard key is answered by the platform theme, which needs an
    // application to have been made. Without one Save comes back with no
    // bindings at all and every check about it passes vacuously.
    QGuiApplication app(argc, argv);

    std::printf("shortcuts:\n");
    everyRowIsUsable();
    theDefaultsCollideWithNothing();
    theRulesCatchWhatTheyAreFor();
    theTransformModeGivesUpTheRightKeys();
    whatWouldCollideIsAskedBeforeItIsDone();
    changingOneChangesOnlyThatOne();
    aStandardKeyIsStillAFamilyUntilItIsMoved();
    aKeyEventIsAskedAboutRatherThanNamed();
    onlyWhatChangedIsWrittenDown();
    aFileFromSomewhereElseIsSteppedOverRatherThanObeyed();
    itSurvivesTheRoundTripThroughADisk();
    return testing::summarise("shortcuts");
}
