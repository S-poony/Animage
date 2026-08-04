// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tile.h"

namespace animage {

// The pixels of one cel, as bytes.
//
// Lossless, and not by being careful -- by construction. What a tile holds is
// IEEE binary16, and what this writes is those same sixteen bits. There is no
// conversion in either direction and so nothing to lose.
//
// That rules out the obvious choice. The plan said a PNG per cel, and a 16-bit
// PNG cannot hold this: half-float spends its precision relatively, finely near
// zero and coarsely near one, while integers are evenly spaced. Of the 15362
// half values in [0,1], a 16-bit integer image keeps 7169 of them, and some
// non-zero values quantise to zero. sRGB-encoding first keeps 10871, which is
// better and still not all of them. A save that loses pixels is not a save.
//
// So the format is ours. It is deliberately dull and self-describing: magic
// bytes, a version, and the numbers needed to read the rest, all little-endian
// so the file means the same thing on any machine. Anything that can inflate a
// zlib stream can recover a drawing from it with the layout below in hand, which
// is the price of not using a format other programs already read.
//
//   offset  0   "ANIMCEL1"                    magic and format version
//           8   uint32  tile size in pixels   128; a change is detectable
//          12   uint32  channels              4, RGBA
//          16   uint32  sample format         0 = IEEE binary16, little-endian
//          20   uint32  tile count
//          24   int32 x, int32 y  per tile    where each tile sits, in tiles
//         ...   tile count * 128 * 128 * 4 * uint16
//
// Pixels are premultiplied and in linear light, exactly as they are in memory.
// Tiles are written in a fixed order so that saving an unchanged drawing twice
// produces identical bytes.
//
// Compression is not here. Deflating this is the caller's business, because
// `core` has no external dependencies and a compressor is one; uncompressed, a
// tile is 128 KB and a shot would run to gigabytes, so the caller should not
// skip it.
struct CelFileInfo {
    int tile_size = 0;
    int channels = 0;
    std::size_t tile_count = 0;
};

std::vector<std::uint8_t> encodeCel(const TileGrid& tiles);

// False on anything malformed, with `error` saying what. `out` is untouched on
// failure, so a corrupt cel loses that drawing rather than the project.
bool decodeCel(const std::vector<std::uint8_t>& bytes, TileGrid& out,
               std::string* error = nullptr);

// Reads the header only, for checking a file without decoding a megabyte of it.
bool readCelFileInfo(const std::vector<std::uint8_t>& bytes, CelFileInfo& out,
                     std::string* error = nullptr);

}  // namespace animage
