// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

// How long a track's or a layer's name may be, in two numbers, because they are
// two different questions with two different answers.
//
// They are here together rather than beside the code that uses each, because
// what makes the first one safe is its relationship to the second, and that
// arithmetic is invisible if they live apart.
namespace names {

// What a field will accept. Chosen for reading rather than for the filesystem:
// the timeline's gutter is a hundred pixels wide and a layer row not much more,
// so a name past about fifteen characters is already being elided, and one past
// sixty is not a name anybody is going to recognise on a row.
//
// It is a hard cap on the editors -- you cannot type the sixty-first character
// rather than being told off for it afterwards -- which is worth doing only
// because the limit is far past what anybody types. A cap that people bump into
// would want the other treatment.
constexpr int kTyped = 60;

// What the export can survive, and the reason the number above is safe.
//
// A name goes into an exported path *twice*: once as the folder and once as the
// stem of every file in it, `{track}_{layer}/{track}_{layer}_0007.png`. Windows
// allows 255 characters per path component -- not per path; Qt lifts the old
// 260-character total by prefixing, and a 533-character path writes fine -- so
// the binding constraint is the file name, which is the sequence name plus the
// nine characters of `_0001.png`.
//
// 255 - 9 = 246, and what happens either side of it is why this is checked
// rather than left to fail:
//
//   - up to 246, an export works;
//   - **from 247 to 255 the folder is created and no frame can be written**,
//     which is a partial export -- the sequences with shorter names are written
//     and the long one is not -- and the export folder was emptied first, so the
//     previous export has gone too;
//   - from 256 the folder cannot be created either and it fails cleanly.
//
// That nine-character window is the whole reason for this constant. Two names of
// kTyped and a separator is 121, comfortably under it, so nothing typed into a
// field can reach the wall -- but a name can also arrive from a project made by
// another build or from a hand-edited scene.json, and those are what the check
// in exporting::write is for.
constexpr std::size_t kExported = 246;

static_assert(2 * kTyped + 1 < static_cast<int>(kExported),
              "two typed names and the separator must fit in what the export can write");

}  // namespace names
