// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

#include "image_import.h"

class QCheckBox;
class QSpinBox;

// What File ▸ Import ▸ Image sequence… asks before it imports anything.
//
// **A recap first, and the settings beside it.** The files have already been
// picked when this opens; what is left is to say what was found -- how many
// frames, at what size, in what order, and anything about the selection that
// the ordering rule had to decide -- and to offer the two things that are
// genuinely choices. Nothing here is a file picker.
//
// The account of the order is the part worth protecting. Ordering is numeric
// and not correctable, so there is no list to drag rows about in; what replaces
// it is saying what the rule did, which is this program's house rule for input
// it will not refuse and will not silently pick over. See image_import::order.
//
// Deliberately does no work. It reads a survey it is handed and hands back two
// numbers, so a test and `shots` can drive an import without answering it --
// the same shape the still's import already has, where the menu item is a
// dialog in front of a function that does the importing.
class SequenceImportDialog : public QDialog {
    Q_OBJECT

public:
    // What the caller found out before opening this: the files in order, the
    // size of the first readable one, and how many would not read at all.
    struct Found {
        image_import::Ordering ordering;
        int width = 0;
        int height = 0;
        int unreadable = 0;
    };

    // What it hands back.
    struct Answer {
        // The frame the first picture lands on, counting from 1 as the timeline
        // does. Never a slot index; the conversion is the caller's.
        int start_frame = 1;
        // Half size is a *placement* of 50% and not a separate mechanism -- the
        // derive step applies it, so the frames cached are a quarter of the
        // tiles rather than full ones being shrunk on the way to the screen.
        bool half_size = false;
    };

    SequenceImportDialog(const Found& found, int playhead_frame, QWidget* parent = nullptr);

    Answer answer() const;

private:
    QSpinBox* start_ = nullptr;
    QCheckBox* half_ = nullptr;
};
