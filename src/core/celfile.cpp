// SPDX-License-Identifier: GPL-3.0-or-later
#include "celfile.h"

#include <algorithm>
#include <cstring>

namespace animage {
namespace {

constexpr char kMagic[8] = {'A', 'N', 'I', 'M', 'C', 'E', 'L', '1'};
constexpr std::size_t kHeaderSize = 24;
constexpr std::uint32_t kSampleHalfLittleEndian = 0;
constexpr int kChannels = 4;

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

    constexpr std::size_t kSamples = static_cast<std::size_t>(kTileSize) * kTileSize * kChannels;

    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + coords.size() * (8 + kSamples * 2));
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
        for (std::size_t i = 0; i < kSamples; ++i) {
            const std::uint16_t bits = tile->rgba[i].bits;
            out.push_back(static_cast<std::uint8_t>(bits & 0xffu));
            out.push_back(static_cast<std::uint8_t>(bits >> 8));
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

    constexpr std::size_t kSamples = static_cast<std::size_t>(kTileSize) * kTileSize * kChannels;
    const std::size_t coords_at = kHeaderSize;
    const std::size_t pixels_at = coords_at + info.tile_count * 8;
    const std::size_t needed = pixels_at + info.tile_count * kSamples * 2;

    // Checked before anything is allocated: a corrupt count is exactly how a
    // truncated file turns into a request for a terabyte.
    if (bytes.size() < needed) {
        return fail(error, "cel file is truncated: " + std::to_string(info.tile_count) +
                               " tiles need " + std::to_string(needed) + " bytes, file has " +
                               std::to_string(bytes.size()));
    }

    TileGrid grid;
    for (std::size_t i = 0; i < info.tile_count; ++i) {
        const TileCoord coord{getI32(bytes.data() + coords_at + i * 8),
                              getI32(bytes.data() + coords_at + i * 8 + 4)};

        auto tile = std::make_shared<Tile>();
        const std::uint8_t* from = bytes.data() + pixels_at + i * kSamples * 2;
        for (std::size_t s = 0; s < kSamples; ++s) {
            tile->rgba[s].bits = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(from[s * 2]) |
                (static_cast<std::uint16_t>(from[s * 2 + 1]) << 8));
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
