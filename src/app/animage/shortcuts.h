// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QKeySequence>
#include <vector>

// What the keyboard does, and when.
//
// This is the cheap half of issue #14 and a prerequisite for lasso and
// transform. Every shortcut used to be a QKeySequence literal at one of fifteen
// call sites in buildActions, all of them ApplicationShortcut, which means they
// fire regardless of what the canvas thinks it is doing. That was fine while
// the program had no modes and stops being fine the moment it has one.
//
// A live transform is the first mode: Return means Play normally and Validate
// during a transform, and the arrows mean step-frame normally and nudge during
// one. Writing that as setEnabled calls scattered through the transform code is
// how an action ends up stuck disabled after some cancel path nobody tested --
// so the fact lives in the table, and one function acts on it.
//
// What this deliberately is not: a rebinding interface. No settings file,
// nothing user-facing, and Id is an internal name rather than a stored one.
// Rebinding stays in #14.
namespace shortcuts {

// The modes an action can be live in, as a set of flags rather than a single
// mode, because "live in both" is the common answer and wants saying once.
enum Modes : unsigned {
    kNormal = 1u << 0,
    kTransform = 1u << 1,
    kAlways = kNormal | kTransform,
};

enum class Mode { Normal, Transform };

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
    Brush,
    Eraser,
    Lasso,
    Transform,
    SmallerBrush,
    LargerBrush,
};

struct Entry {
    Id id;
    // What the menu item says. Empty for a bare shortcut with no menu item of
    // its own -- the brush size keys are two of those.
    const char* label;
    // A standard key is a family and not a sequence: Qt answers with whatever
    // the platform's convention is, and on some platforms with more than one.
    // UnknownKey means the binding is spelled out in `key` instead.
    QKeySequence::StandardKey standard;
    int key;  // Qt::Key with modifiers, combined; 0 when `standard` is used
    unsigned modes;
};

// Every row, in menu order.
const std::vector<Entry>& table();

// The row for an id. Every id has one -- the table is the definition.
const Entry& entryFor(Id id);

// Every sequence a row can actually produce. A standard key can be more than
// one, so anything comparing bindings has to compare them all: taking the first
// would let a second collide unnoticed.
std::vector<QKeySequence> sequencesFor(const Entry& entry);

inline bool liveIn(unsigned modes, Mode mode) {
    return (modes & (mode == Mode::Transform ? kTransform : kNormal)) != 0;
}

}  // namespace shortcuts
