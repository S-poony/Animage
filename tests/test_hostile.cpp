// SPDX-License-Identifier: GPL-3.0-or-later
//
// Files nobody sane wrote. A project folder is data -- somebody else's file,
// downloaded or emailed or recovered from a dead drive -- so the readers have
// to refuse the hostile file, and refuse it without crashing, without
// undefined behaviour, and without allocating memory out of proportion to what
// the file holds. Each of these tests is a crash or a sanitizer finding that
// actually happened, fixed and then pinned here so it cannot come back.

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "project_io.h"
#include "testing.h"

using namespace animage;

namespace {

// A JSON document nested a hundred thousand levels deep. The scene format
// nests about six; anything past a few hundred is either a truncated file or
// one built to break a parser, and the reader has to refuse it before the
// stack runs out.
//
// This used to crash this program: the JSON reader was ours, recursive, and a
// hundred-thousand-level file overflowed the stack -- a crash on a file a
// hundred kilobytes long. The reader is Qt's now, and Qt refuses the same file
// itself, at its own depth limit and with an error rather than a crash; the
// test pins the behaviour the reader has to have.
void aDeeplyNestedSceneIsRefused() {
    TEST("a deeply nested file is refused, not crashed on");
    std::string text;
    constexpr int kDepth = 100'000;
    text.reserve(static_cast<std::size_t>(kDepth) * 2 + 8);
    for (int i = 0; i < kDepth; ++i) text += '[';
    text += "0";
    for (int i = 0; i < kDepth; ++i) text += ']';

    Document doc;
    std::string error;
    CHECK(!ProjectIO::readSceneJson(text, doc, &error));
    CHECK(error.find("nested") != std::string::npos);
}

// The scene reader is where hostile numbers actually meet the file. A canvas of
// 1e300 by -1e300 is nonsense; the reader must fall back to its defaults and
// clamp, with no undefined behaviour on the way.
void sceneCanvasOutOfRangeFallsBack() {
    TEST("a scene with out-of-range canvas numbers loads with the defaults");
    const char* text = R"({
  "format": "animage-scene",
  "version": 1,
  "framerate": 24,
  "canvas": {"width": 1e300, "height": -1e300},
  "tracks": [
    {
      "id": 1, "name": "main", "opacity": 1.0, "blend": "normal",
      "time_offset": 0, "next_drawing_number": 2,
      "layers": [{"id": 1, "name": "ink", "kind": "raster", "opacity": 1.0,
                  "visible": true, "locked": false, "blend": "normal"}],
      "slots": [1],
      "images": [{"id": 1, "number": 1, "cels": []}]
    }
  ]
})";
    Document doc;
    std::string error;
    CHECK(ProjectIO::readSceneJson(text, doc, &error));
    CHECK_EQ(doc.scene().width, 1920);
    CHECK_EQ(doc.scene().height, 1080);
}

// An id of 1e300 is not a thing a file can mean, and an out-of-range id has
// to read as "no id" -- the same fallback as an absent one. A track with no
// id is dropped, and a scene whose only track was dropped is refused for
// having no tracks.
//
// This one fails under the old code even without a sanitizer: the garbage
// integer the old cast produced was not kNoId, so the scene loaded with a
// track that had never been asked for.
void sceneIdsOutOfRangeAreRefused() {
    TEST("a scene whose ids are out of range is refused, not misread");
    const char* text = R"({
  "format": "animage-scene",
  "version": 1,
  "framerate": 24,
  "canvas": {"width": 1920, "height": 1080},
  "tracks": [
    {
      "id": 1e300, "name": "main", "opacity": 1.0, "blend": "normal",
      "time_offset": 0, "next_drawing_number": 2,
      "layers": [{"id": 1e300, "name": "ink", "kind": "raster", "opacity": 1.0,
                  "visible": true, "locked": false, "blend": "normal"}],
      "slots": [1e300],
      "images": [{"id": 1e300, "number": 1, "cels": []}]
    }
  ]
})";
    Document doc;
    std::string error;
    CHECK(!ProjectIO::readSceneJson(text, doc, &error));
    CHECK(error.find("no tracks") != std::string::npos);
}

// A cel is a sparse set of 128x128 tiles, and one opaque pixel is enough to
// make a tile worth storing: 528 bytes in the file, 128 KB in memory. A file
// can therefore ask for a gigabyte with a few tens of kilobytes -- every tile
// is really in the file, what explodes is the memory. The reader caps the
// tile count at what a full layer over the largest canvas the format allows
// (16384 squared, 128-pixel tiles) can occupy, and refuses past it.
//
// 16385 tiles: one past the cap. The check lives in the header reader, so the
// refusal happens before any tile is allocated.
void aCelWithTooManyTilesIsRefused() {
    TEST("a cel past the tile budget is refused before a tile is allocated");
    TileGrid grid;
    constexpr int kTiles = 16385;
    for (int i = 0; i < kTiles; ++i) {
        auto tile = std::make_shared<Tile>();
        tile->setPixel(3, 3, Rgba{1.0f, 0.0f, 0.0f, 1.0f});
        grid.set({i, 0}, std::move(tile));
    }
    const std::vector<std::uint8_t> bytes = ProjectIO::encodeCel(grid);

    ProjectIO::CelFileInfo info;
    std::string error;
    CHECK(!ProjectIO::readCelFileInfo(bytes, info, &error));
    CHECK(error.find("too many") != std::string::npos);

    TileGrid out;
    CHECK(!ProjectIO::decodeCel(bytes, out, &error));
    CHECK(out.empty());  // untouched
}

// The up-front size check is a total: header, then eight bytes of coordinates
// and a 512-byte row table for every tile. What it does not account for is the
// pixels between one tile's row table and the next one's, so the guarantee it
// gives holds for the first tile and drifts further out with every tile after
// it. A file cut to exactly that total is therefore long enough to pass the
// check and too short to read.
//
// It compounds: once the cursor is past the end, the guard on the pixel data is
// an unsigned subtraction, so it wraps to about 1.8e19 and passes whatever it is
// asked, and the pixel loop then reads up to 128 KB per tile from beyond the
// allocation.
//
// The assertion is the *reason* and not only the refusal. Reading a row table
// from past the end gives whatever was next in memory, and that usually fails
// the span validation a few lines later -- so a decoder with this bug also says
// no, for a reason it invented from somebody else's memory. Only one of the two
// is the file being described correctly.
void aTruncatedMultiTileCelIsRefusedAsTruncated() {
    TEST("a cel cut to its own floor is refused as truncated, not as a bad span");

    // Several tiles, each with one pixel: enough that the drift is unambiguous,
    // and small enough that the file is a few tens of kilobytes.
    TileGrid grid;
    constexpr int kTiles = 64;
    for (int i = 0; i < kTiles; ++i) {
        auto tile = std::make_shared<Tile>();
        tile->setPixel(3, 3, Rgba{1.0f, 0.0f, 0.0f, 1.0f});
        grid.set({i, 0}, std::move(tile));
    }

    std::vector<std::uint8_t> bytes = ProjectIO::encodeCel(grid);
    // Header, coordinates and a row table each -- exactly what the check asks
    // for, and one pixel per tile short of what it takes to read.
    const std::size_t floor_size = 24 + static_cast<std::size_t>(kTiles) * (8 + 128 * 4);
    CHECK(bytes.size() > floor_size);
    bytes.resize(floor_size);

    TileGrid out;
    std::string error;
    CHECK(!ProjectIO::decodeCel(bytes, out, &error));
    CHECK(out.empty());
    CHECK(error.find("truncated") != std::string::npos);
}

}  // namespace

int main() {
    std::printf("hostile:\n");
    aDeeplyNestedSceneIsRefused();
    sceneCanvasOutOfRangeFallsBack();
    sceneIdsOutOfRangeAreRefused();
    aCelWithTooManyTilesIsRefused();
    aTruncatedMultiTileCelIsRefusedAsTruncated();
    return testing::summarise("hostile");
}
