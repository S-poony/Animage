// SPDX-License-Identifier: GPL-3.0-or-later
#include "celfile.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace animage {
namespace {

constexpr char kMagic[8] = {'A', 'N', 'I', 'M', 'C', 'E', 'L', '2'};
constexpr std::size_t kHeaderSize = 24;
constexpr std::uint32_t kSampleHalfLittleEndian = 0;
constexpr int kChannels = 4;
constexpr std::size_t kRowTableBytes = static_cast<std::size_t>(kTileSize) * 4;

// Written a byte at a time rather than by memcpy of a struct: the file has to
// mean the same thing whatever the machine's word order is, and a struct would
// quietly acquire padding the first time somebody added a field.
void putU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void putI32(std::vector<std::uint8_t>& out, std::int32_t value) {
    putU32(out, static_cast<std::uint32_t>(value));
}

std::uint32_t getU32(const std::uint8_t* at) {
    return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8) |
           (static_cast<std::uint32_t>(at[2]) << 16) | (static_cast<std::uint32_t>(at[3]) << 24);
}

std::int32_t getI32(const std::uint8_t* at) { return static_cast<std::int32_t>(getU32(at)); }

void putU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

std::uint16_t getU16(const std::uint8_t* at) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(at[0]) |
                                      (static_cast<std::uint16_t>(at[1]) << 8));
}

// The span of a row that holds anything at all. A pixel counts as present if
// any of its four samples has a bit set: alpha alone would be enough for
// premultiplied data in principle, but a file should not depend on the rest of
// the program having kept that promise perfectly.
struct RowSpan {
    std::uint16_t begin = 0;
    std::uint16_t end = 0;
};

RowSpan spanOfRow(const Tile& tile, int row) {
    const std::size_t base = static_cast<std::size_t>(row) * kTileSize * kChannels;
    int first = kTileSize;
    int last = -1;
    for (int x = 0; x < kTileSize; ++x) {
        const std::size_t at = base + static_cast<std::size_t>(x) * kChannels;
        const bool present = tile.rgba[at].bits || tile.rgba[at + 1].bits ||
                             tile.rgba[at + 2].bits || tile.rgba[at + 3].bits;
        if (!present) continue;
        if (first == kTileSize) first = x;
        last = x;
    }
    if (last < 0) return {};
    return {static_cast<std::uint16_t>(first), static_cast<std::uint16_t>(last + 1)};
}

bool fail(std::string* error, const std::string& what) {
    if (error) *error = what;
    return false;
}

}  // namespace

std::vector<std::uint8_t> encodeCel(const TileGrid& tiles) {
    // Fully transparent tiles are dropped rather than written. They are what an
    // erased stroke leaves behind, and keeping them would grow a file every time
    // somebody rubbed something out -- and would put back, on load, tiles the
    // sparse model says should not exist.
    std::vector<TileCoord> coords;
    coords.reserve(tiles.tileCount());
    for (const auto& [coord, tile] : tiles.tiles()) {
        if (!tile || tile->isFullyTransparent()) continue;
        coords.push_back(coord);
    }

    // A fixed order, so saving an unchanged drawing twice gives identical bytes
    // and an unchanged file is visibly unchanged.
    std::sort(coords.begin(), coords.end(), [](const TileCoord& a, const TileCoord& b) {
        return (a.y != b.y) ? (a.y < b.y) : (a.x < b.x);
    });

    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + coords.size() * (8 + kRowTableBytes));
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    putU32(out, static_cast<std::uint32_t>(kTileSize));
    putU32(out, static_cast<std::uint32_t>(kChannels));
    putU32(out, kSampleHalfLittleEndian);
    putU32(out, static_cast<std::uint32_t>(coords.size()));

    for (const TileCoord& coord : coords) {
        putI32(out, coord.x);
        putI32(out, coord.y);
    }
    for (const TileCoord& coord : coords) {
        const TileRef tile = tiles.find(coord);

        // The row table first, so a reader knows how much follows before it
        // reads any of it.
        std::array<RowSpan, kTileSize> spans{};
        for (int row = 0; row < kTileSize; ++row) {
            spans[static_cast<std::size_t>(row)] = spanOfRow(*tile, row);
            putU16(out, spans[static_cast<std::size_t>(row)].begin);
            putU16(out, spans[static_cast<std::size_t>(row)].end);
        }

        for (int row = 0; row < kTileSize; ++row) {
            const RowSpan span = spans[static_cast<std::size_t>(row)];
            const std::size_t base = static_cast<std::size_t>(row) * kTileSize * kChannels;
            for (int x = span.begin; x < span.end; ++x) {
                const std::size_t at = base + static_cast<std::size_t>(x) * kChannels;
                for (int c = 0; c < kChannels; ++c) putU16(out, tile->rgba[at + c].bits);
            }
        }
    }
    return out;
}

bool readCelFileInfo(const std::vector<std::uint8_t>& bytes, CelFileInfo& out,
                     std::string* error) {
    if (bytes.size() < kHeaderSize) return fail(error, "cel file is too short to hold a header");
    if (std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
        return fail(error, "not a cel file: wrong magic");
    }

    const std::uint32_t tile_size = getU32(bytes.data() + 8);
    const std::uint32_t channels = getU32(bytes.data() + 12);
    const std::uint32_t sample = getU32(bytes.data() + 16);
    const std::uint32_t count = getU32(bytes.data() + 20);

    if (tile_size != static_cast<std::uint32_t>(kTileSize)) {
        return fail(error, "cel file has a tile size of " + std::to_string(tile_size) +
                               ", this build uses " + std::to_string(kTileSize));
    }
    if (channels != kChannels) {
        return fail(error, "cel file has " + std::to_string(channels) + " channels, expected 4");
    }
    if (sample != kSampleHalfLittleEndian) {
        return fail(error, "cel file uses an unknown sample format");
    }

    out.tile_size = static_cast<int>(tile_size);
    out.channels = static_cast<int>(channels);
    out.tile_count = count;
    return true;
}

bool decodeCel(const std::vector<std::uint8_t>& bytes, TileGrid& out, std::string* error) {
    CelFileInfo info;
    if (!readCelFileInfo(bytes, info, error)) return false;

    const std::size_t coords_at = kHeaderSize;

    // The smallest a file with this many tiles could possibly be: coordinates
    // and a row table each. Checked before anything is read or allocated,
    // because a corrupt tile count is exactly how a truncated file turns into a
    // request for a terabyte.
    const std::size_t floor_size = coords_at + info.tile_count * (8 + kRowTableBytes);
    if (bytes.size() < floor_size) {
        return fail(error, "cel file is truncated: " + std::to_string(info.tile_count) +
                               " tiles need at least " + std::to_string(floor_size) +
                               " bytes, file has " + std::to_string(bytes.size()));
    }

    TileGrid grid;
    std::size_t at = coords_at + info.tile_count * 8;  // just past the coordinates

    for (std::size_t i = 0; i < info.tile_count; ++i) {
        const TileCoord coord{getI32(bytes.data() + coords_at + i * 8),
                              getI32(bytes.data() + coords_at + i * 8 + 4)};

        // The row table. Its size is fixed, and floor_size already guaranteed
        // it is there.
        std::array<RowSpan, kTileSize> spans{};
        std::size_t samples = 0;
        for (int row = 0; row < kTileSize; ++row) {
            const std::uint16_t begin = getU16(bytes.data() + at);
            const std::uint16_t end = getU16(bytes.data() + at + 2);
            at += 4;
            if (begin > end || end > kTileSize) {
                return fail(error, "cel file has a row span outside its tile");
            }
            spans[static_cast<std::size_t>(row)] = {begin, end};
            samples += static_cast<std::size_t>(end - begin);
        }

        // Only now is the size of this tile's pixels known, so it is checked
        // here rather than up front.
        const std::size_t pixel_bytes = samples * kChannels * 2;
        if (bytes.size() - at < pixel_bytes) {
            return fail(error, "cel file is truncated inside tile " + std::to_string(i));
        }

        auto tile = std::make_shared<Tile>();  // zeroed; the gaps stay transparent
        for (int row = 0; row < kTileSize; ++row) {
            const RowSpan span = spans[static_cast<std::size_t>(row)];
            const std::size_t base = static_cast<std::size_t>(row) * kTileSize * kChannels;
            for (int x = span.begin; x < span.end; ++x) {
                const std::size_t into = base + static_cast<std::size_t>(x) * kChannels;
                for (int c = 0; c < kChannels; ++c) {
                    tile->rgba[into + static_cast<std::size_t>(c)].bits = getU16(bytes.data() + at);
                    at += 2;
                }
            }
        }

        // A file that carries an empty tile anyway does not get to put one into
        // the model; absent and transparent have to keep meaning the same thing.
        if (tile->isFullyTransparent()) continue;
        grid.set(coord, std::move(tile));
    }

    out = std::move(grid);
    return true;
}

}  // namespace animage
