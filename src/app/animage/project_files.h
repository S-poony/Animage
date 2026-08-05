// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <cstdint>
#include <unordered_map>

#include "document.h"

// Reading and writing a project on disk.
//
// A project is a folder, not a file:
//
//   the-shot.animage/
//     scene.json          the structure, in text
//     cels/cel-000007.acel   one per cel that any drawing refers to
//
// This half is the application's because it needs a compressor, and `core` has
// no external dependencies. What is in the bytes is decided in `core`
// (celfile.h, serialise.h); what surrounds them is decided here.
//
// A cel file is the encoding from celfile.h, deflated, behind eight bytes of
// magic. Uncompressed a tile is 128 KB, and a shot would run to gigabytes.
// Recovering one by hand needs only zlib and the note in celfile.h: skip the
// eight magic bytes, then a four-byte big-endian length, then a zlib stream.
namespace project {

// What the last successful save or load left on disk, so the next save can tell
// which cel files are still current. A cel's revision is bumped by every write
// to it, undo included, so equality here means the bytes in the folder are the
// bytes this cel would encode to -- and the file can be carried forward instead
// of re-encoded.
//
// It is a hint and never a promise: a carried-forward file that has gone missing
// falls back to being written out in full. Correctness depends on the revisions
// being right about the pixels, not on the map being right about the disk.
struct SaveState {
    QString folder;  // empty, or where these revisions were written
    std::unordered_map<animage::CelId, std::uint64_t> revisions;
};

// Writes the whole project. Nothing in `folder` is disturbed unless the save
// succeeds completely: it is built alongside and swapped in at the end, so an
// interrupted save leaves the last good one where it was.
bool save(const animage::Document& doc, const QString& folder, QString* error = nullptr);

// The same, skipping the cels that have not moved since `state` was taken.
// Encoding and deflating the pixels is nearly all of a save's cost, so a save
// where little changed costs little -- which is what makes autosave affordable.
//
// `state` is read for what was written last time and, if the save succeeds,
// replaced with what was written this time. On failure it is left alone. Saving
// somewhere other than `state.folder` re-encodes everything, so Save As always
// produces a project that stands on its own.
bool save(const animage::Document& doc, const QString& folder, SaveState& state,
          QString* error = nullptr);

// Replaces everything in `doc`. On failure `doc` is untouched and `error` says
// what was wrong -- a project that will not open must not take the open one
// down with it.
bool load(animage::Document& doc, const QString& folder, QString* error = nullptr);

// The same, recording what was on disk so the first save after an open is
// incremental too. Everything just read matches its file by definition.
bool load(animage::Document& doc, const QString& folder, SaveState& state,
          QString* error = nullptr);

// The conventional suffix. Not enforced anywhere; a project is a folder and
// works under any name.
QString folderSuffix();

}  // namespace project
