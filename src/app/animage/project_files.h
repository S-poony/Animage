// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

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

// Writes the whole project. Nothing in `folder` is disturbed unless the save
// succeeds completely: it is built alongside and swapped in at the end, so an
// interrupted save leaves the last good one where it was.
bool save(const animage::Document& doc, const QString& folder, QString* error = nullptr);

// Replaces everything in `doc`. On failure `doc` is untouched and `error` says
// what was wrong -- a project that will not open must not take the open one
// down with it.
bool load(animage::Document& doc, const QString& folder, QString* error = nullptr);

// The conventional suffix. Not enforced anywhere; a project is a folder and
// works under any name.
QString folderSuffix();

}  // namespace project
