// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <functional>

#include "ctg_fill.h"
#include "ctg_job.h"
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
//   the-shot/
//     track-1_ink/     track-1_ink_0001.png     track-1_ink_0002.png   ...
//     track-1_colour/  track-1_colour_0001.png  ...
//     composite/       composite_0001.png       ...
//
// The underscore is the separator and nothing else is: every character of a
// track or layer name that is not a letter or a digit becomes a hyphen, so
// "layer 1" is `layer-1` and the number in a file name is always the frame. See
// sequenceName.
//
// **16-bit PNG is lossy here**, and knowingly so. A half spends its precision
// relatively and an integer spends it evenly, so of the 15362 half values in
// [0,1] an sRGB-encoded 16-bit image keeps 10871. That is the same arithmetic
// that decided cels are not saved as PNG -- but a save that loses pixels is not
// a save, while an export that converts is doing what it was asked.
//
// This is the format list, and it is easier to add to than the layout is to
// change. Two formats are wanted and they answer different questions, which is
// worth keeping straight before either is written:
//
//   - **TIFF, for compatibility.** The common deliverable in 2D animation, and
//     from here a radio button over the same integer conversion PNG does. Note
//     it does *not* make the export lossless: a TIFF can hold a half
//     (`SampleFormat = 3`, `BitsPerSample = 16`), but that corner is thinly
//     supported by readers, and the cheap route -- Qt's imageformats add-on --
//     writes integers exactly as lossy as the PNG above.
//   - **EXR, for losslessness.** Its default pixel type is half, premultiplied,
//     linear, which is bit for bit what the tiles hold. `tinyexr` is a single
//     BSD header. It could also put every layer in one file per frame, which is
//     a change to the *layout* and so wants deciding before more is built on
//     the folder-per-layer one.
//
// The full comparison, and the correction to the claim that TIFF could not hold
// our pixels at all, is in docs/handover.md and docs/why-our-own-formats.md.
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

// Called with (finished so far, the whole job, what is happening now). Return
// false to stop; `write` then reports failure with an error saying it was
// cancelled, leaving whatever had already been written on disk. An export is
// not atomic and does not pretend to be: it produces files for somebody else,
// and half a sequence is visibly half a sequence.
//
// The total counts colour solves as well as files, because a max-flow is a
// second and a half and a PNG is a handful of milliseconds -- a bar that
// counted only files would sit still through the whole of the slow half and
// then run to the end. It is called before a solve as well as after one, with
// the count unchanged, so the label says what is being waited for rather than
// what has just finished.
using Progress = std::function<bool(int done, int total, const QString& what)>;

// Running one colour solve somewhere that is not here.
//
// A CTG layer stores scribbles and its fill is a cache, regenerated on demand
// and held in the document -- and the canvas only ever builds it for the frame
// being looked at, because compositing is not allowed to start a max-flow. So
// the fills for frames nobody has visited do not exist, and an export that
// composited only what was cached would write blank colour layers for a project
// that had just been opened, without saying anything.
//
// So the export solves them, and this is how it asks. Fill `out` with the
// answer to `job` and return true; return false to mean the caller gave up,
// which stops the export the same way a cancelled progress step does. `write`
// installs the answer in the document itself, so an implementation is a queue
// and a wait and nothing about a fill.
//
// Passing nothing solves where the caller stands -- correct, and what the tests
// do, and what freezes a window for a minute and a half on a coloured shot.
// It is also *capped*: a solve nobody can wait for gets the interactive budget,
// so an exported fill is coarser than the one on screen. A solver gets the full
// one.
using Solve = std::function<bool(const animage::CtgKey& key, const animage::CtgJob& job,
                                 animage::CtgFill& out)>;

// How many files these options will produce. Wanted before starting, so a
// progress dialog can say how far along it is rather than counting up forever.
// Files only: the solves are counted inside `write`, which is the only place
// that knows which fills the document is already holding.
int fileCount(const animage::Document& doc, const Options& options);

// Takes the document by mutable reference, because it builds the fills that are
// missing (see Solve) and a fill lives in the document.
//
// Frames are written in slot order, every sequence at once, rather than one
// whole sequence after another. That is not a detail: it is what makes each
// drawing solve exactly once. Sequence by sequence, a colour layer's pass
// solves every drawing in the shot and the flattened pass then asks for them
// all over again, by which time the bounded fill cache has evicted the early
// ones -- so the same max-flows run twice and the progress cannot be counted in
// advance.
bool write(animage::Document& doc, const Options& options, const Progress& progress,
           const Solve& solve, QString* error);

// What is already in the folder an export is about to be written to.
//
// Exporting twice into one folder used to merge, silently, and a merge is the
// wrong shape of thing: writing a shot you have since cut short leaves the old
// export's later frames sitting after the new ones, and downstream that reads
// as a perfectly well-formed sequence of the wrong length. Cancelling halfway
// splices two shots together at the seam. So an export replaces what was there,
// and this is what the window asks before it does.
enum class Occupant {
    Nothing,        // no folder, or an empty one: write into it and say nothing
    AnExport,       // one of ours, and safe to delete once somebody has said so
    SomethingElse,  // somebody's files. Refuse; never offer to delete these.
};

// Whether `folder` is one of ours: sequence folders, holding frames named after
// them, and nothing else whatever.
//
// This is a delete guard, so it is strict on purpose and errs towards
// SomethingElse. A loose file, a folder of anything but frames, a project
// folder that happens to share the name -- none of those is an export, and the
// answer to a folder we do not recognise is to leave it alone and ask for
// another name, never to weigh up deleting it.
//
// The one indulgence is the junk a file browser leaves behind (`.DS_Store` and
// friends), which is ignored here and deleted with the rest. Without it a
// folder anybody had opened would stop being recognisable as an export.
Occupant occupantOf(const QString& folder);

// Deletes the folder and everything in it. Only ever call this on a folder
// `occupantOf` called AnExport -- it is `rm -r` and it has no opinion of its
// own about what it is pointed at.
bool removeExport(const QString& folder, QString* error);

// `{track}_{layer}`, with every character that is not a letter or a digit
// replaced by a hyphen, runs of them collapsed, and the ends trimmed. This is
// both the folder name and the stem of every file in it.
//
// Hyphen rather than underscore so that the underscore means one thing: it
// separates the track from the layer from the frame number, and nothing else in
// the name can be mistaken for it. "layer 1" is `layer-1`, where the 1 is
// visibly part of the layer's name and not a count of anything.
QString sequenceName(const std::string& track, const std::string& layer);

}  // namespace exporting
