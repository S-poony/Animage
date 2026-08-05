// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <functional>

#include "document.h"

// Writing the shot out as image sequences, for somebody else's program to open.
//
// This is the other half of M5 and it is not the same job as saving. A save
// keeps the bits the tiles hold, because losing one is losing the drawing; an
// export converts on purpose, because the destination is a compositor that
// expects sRGB in a format it has heard of.
//
// The layout is the one the specification asks for -- a folder per layer, and
// inside it `{track}_{layer}_{frame:04}.png`:
//
//   the-shot-export/
//     main_ink/     main_ink_0001.png     main_ink_0002.png   ...
//     main_colour/  main_colour_0001.png  ...
//     composite/    composite_0001.png    ...
//
// **16-bit PNG is lossy here**, and knowingly so. A half spends its precision
// relatively and an integer spends it evenly, so of the 15362 half values in
// [0,1] an sRGB-encoded 16-bit image keeps 10871. That is the same arithmetic
// that decided cels are not saved as PNG -- but a save that loses pixels is not
// a save, while an export that converts is doing what it was asked. EXR is what
// a lossless deliverable wants and is the named next step; the format list is
// easier to add to than the layout is to change.
namespace exporting {

struct Options {
    QString folder;
    // One sequence per layer, which is what a compositor is for. Hidden layers
    // are not written: hidden means not in the picture, and the per-layer
    // sequences and the flattened one have to agree about what the shot is.
    bool layers = true;
    // The picture as the canvas shows it, all tracks and layers over each
    // other. Nearly free -- it is what the compositor already produces for the
    // screen -- and it is what you watch the shot back with.
    bool flattened = false;
};

// Called with (written so far, total). Return false to stop; `write` then
// reports failure with an error saying it was cancelled, leaving whatever had
// already been written on disk. An export is not atomic and does not pretend
// to be: it produces files for somebody else, and half a sequence is visibly
// half a sequence.
using Progress = std::function<bool(int, int)>;

// How many files these options will produce. Wanted before starting, so a
// progress dialog can say how far along it is rather than counting up forever.
int fileCount(const animage::Document& doc, const Options& options);

// Takes the document by mutable reference, and the reason is worth knowing.
// A CTG layer stores scribbles and its fill is a cache, regenerated on demand
// and held in the document -- and the canvas only ever regenerates it for the
// frame being looked at, because compositing is not allowed to start a max-flow.
// So the fills for frames nobody has visited do not exist, and an export that
// only composited what was cached would write blank colour layers for a project
// that had just been opened, without saying anything. It solves them instead.
//
// The consequence is that exporting a coloured shot for the first time is slow:
// it pays one max-flow per CTG layer per distinct drawing. Fills already built
// are reused, and they are solved at the same capped resolution the screen gets
// -- see the handover on solving off the interface thread, which is what would
// remove both the cap and the wait.
bool write(animage::Document& doc, const Options& options, const Progress& progress,
           QString* error);

// `{track}_{layer}`, with anything a filesystem would object to replaced. This
// is both the folder name and the stem of every file in it.
QString sequenceName(const std::string& track, const std::string& layer);

}  // namespace exporting
