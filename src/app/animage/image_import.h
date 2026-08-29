// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include <vector>

#include "tile.h"

// Turning a file somebody imported into the pixels the compositor draws.
//
// This is the **derive step** of docs/importing.md, and the thing to know about
// it is that it is not a one-off. A reference layer holds no cels: what is on
// screen is derived from the file every time it is needed and memoised, so this
// runs again whenever that memo is lost. Two consequences, both of which the
// code below has to honour:
//
//   - **It must be deterministic.** Decoded, forgotten and decoded again has to
//     produce the same pixels, or the picture changes while somebody scrubs
//     over it. That is a correctness question and not a quality one.
//   - **It must be affordable enough to sit on a decode path**, because it does.
//
// It lives in `src/app/` rather than in `core` for the usual reason: `core` is
// the model and knows nothing about QImage or about bytes on disk.
namespace image_import {

// What a file has to say for itself before anything is decoded, so a dialog can
// say what an import will cost before paying it. Size only, and deliberately:
// what the recap needs is the tile count, and asking a reader for a size is
// cheap where decoding a 300 dpi A4 scan is the whole cost being surveyed.
struct Survey {
    bool ok = false;
    int width = 0;
    int height = 0;
    QString trouble;  // why not, when `ok` is false
};

Survey survey(const QString& path);

// What a file said its colours meant, when that was not sRGB and a conversion
// therefore happened. Empty when none did.
//
// **Worth saying out loud**, which is the whole reason this is reported rather
// than done quietly. A palette exported from Photoshop or Procreate is often
// Display P3 or Adobe RGB; converting it is right, and *not* telling anybody is
// the failure mode docs/importing.md singles out -- every swatch arrives a
// different colour from the one the artist chose, with nothing on screen to say
// why.
struct Converted {
    QString from;  // "Display P3", "Adobe RGB", ... or empty
};

// What order a set of files somebody picked is a sequence in.
//
// **Numeric, and not correctable.** `frame10.png` never goes before
// `frame9.png` and nobody has ever wanted it to, so there is no control for
// this and no list to drag rows around in. What there is instead is an
// *account* of what the rule did, which is the other half of the same decision:
// the house rule is to let the input in and explain it rather than to refuse it
// or to silently pick.
//
// The number is the **last** run of digits in the file name, which is the
// convention every renderer and scanner writes: `shot2_frame010.png` is frame
// ten of shot two and not frame two. Files with no digits at all cannot be
// ordered by this rule, so they are sorted by name and go last -- said, not
// hidden.
//
// Files are grouped by what surrounds the number before they are sorted by it,
// so two runs picked together stay two runs rather than interleaving: all of
// `a_001` to `a_010`, then all of `b_001` to `b_010`. More than one group is
// worth saying and is not worth refusing. The unnumbered ones are not groups:
// they are counted on their own line, and counting them twice would make two
// true sentences add up to more files than were picked.
struct Ordering {
    std::vector<QString> paths;  // the order the frames will be shown in

    // Everything the recap has to be able to say, counted here rather than
    // worked out again by whoever prints it.
    int numbered = 0;
    int unnumbered = 0;
    int schemes = 0;  // distinct groups among the numbered files

    // The ends of the run, and whether it has holes in it -- both only when
    // there is exactly one scheme, because across two the first number comes
    // from one group and the last from another and "1 to 24" would be a
    // sentence about nothing. Zero and false otherwise.
    long long first = 0;
    long long last = 0;
    bool gaps = false;
};

Ordering order(const QStringList& paths);

// How many tiles `width` x `height` occupies, which is what an import costs in
// memory. Exposed because the recap quotes it and a test pins it: a tile is
// 128x128 RGBA half = exactly 128 KB, so an HD still is 135 tiles and a 300 dpi
// A4 scan is 560.
std::size_t tileCountFor(int width, int height);

// Decodes to linear premultiplied half, with the top-left pixel at the origin.
//
// **Colour has to survive this**, because eyedropping an imported palette is a
// stated use. An 8-bit sRGB image is exactly lossless into half -- all 256
// values land on distinct halves and come back to the same integers -- so
// nothing is traded here when the file is already sRGB.
//
// When it is not, the numbers would otherwise be reinterpreted against the
// wrong primaries and every colour would arrive different from the one the
// artist chose, silently, which is the worst way for this to be wrong. So a
// file carrying any other profile is widened to 16 bits per channel *first* --
// converting an 8-bit image in place bands it -- and then converted to sRGB.
//
// Returns an empty grid and fills `trouble` if the file cannot be read.
animage::TileGrid decode(const QString& path, QString* trouble = nullptr,
                         Converted* converted = nullptr);

// The same from an image already in memory, which is what a test drives and
// what `decode` calls once it has one. Separated so that the conversion can be
// exercised without a file existing.
animage::TileGrid decodeImage(const QImage& source, Converted* converted = nullptr);

}  // namespace image_import
