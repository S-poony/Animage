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
//   offset  0   "ANIMCEL2"                    magic and format version
//           8   uint32  tile size in pixels   128; a change is detectable
//          12   uint32  channels              4, RGBA
//          16   uint32  sample format         0 = IEEE binary16, little-endian
//          20   uint32  tile count
//          24   int32 x, int32 y  per tile    where each tile sits, in tiles
//         ...   per tile, in the same order:
//                 128 * (uint16 begin, uint16 end)   the occupied span of each
//                                                    row; begin == end is empty
//                 then (end - begin) * 4 * uint16 for each row, in row order
//
// Pixels are premultiplied and in linear light, exactly as they are in memory.
// Anything outside a row's span is transparent. Tiles are written in a fixed
// order so that saving an unchanged drawing twice produces identical bytes.
//
// Only the occupied span of each row is stored, and that is the difference
// between a usable save and an unusable one. A three-pixel line crossing a
// 128x128 tile leaves it 99% empty, so writing tiles whole meant handing the
// compressor 457 MB to produce 3.3 MB -- measured at 92.6% zero bytes, and 2.8
// seconds to save twenty-four drawings. Spans cost 512 bytes a tile and remove
// almost all of it. A tile that really is full pays that 512 bytes and nothing
// else.
//
// Compression is still not here. Deflating this is the caller's business,
// because `core` has no external dependencies and a compressor is one.
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
