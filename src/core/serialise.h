// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "document.h"

namespace animage {

// `scene.json`: the structure of a document, with no pixels in it.
//
// A project is a folder -- this file, plus one image per cel -- because the plan
// asked for something readable, diffable and repairable by hand, and because a
// single archive would have to be rewritten whole every time one drawing
// changed.
//
// The pixels are deliberately not here. This half has no Qt in it and can be
// tested headlessly, which is the same line the rest of `core` sits on; writing
// and reading the cel images belongs to the application, which already has an
// image library.
//
// What a CTG cel stores is its scribbles. The fill is derived and is never
// written, so a file cannot carry a fill that disagrees with the drawing.
constexpr int kSceneFormatVersion = 1;

std::string writeSceneJson(const Document& doc);

// Replaces everything in `doc`, including the history: an undo across a file
// load has nothing to mean. False on a malformed or wrong-version file, with
// `error` saying why, and `doc` left untouched.
bool readSceneJson(std::string_view text, Document& doc, std::string* error = nullptr);

// Every cel the scene refers to, in the order the file lists them. This is the
// manifest the caller needs in order to write or read one image per cel, and it
// is derived from the scene rather than stored: a cel no image references is not
// part of the document, whatever the undo stack still remembers.
std::vector<CelId> celsReferencedBy(const Document& doc);

}  // namespace animage
