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

#include "celfile.h"
#include "json.h"
#include "serialise.h"
#include "testing.h"

using namespace animage;

namespace {

// A JSON document nested a hundred thousand levels deep. The scene format
// nests about six; anything past a few hundred is either a truncated file or
// one built to break a recursive-descent parser, and the parser's depth limit
// has to refuse it before the stack runs out.
//
// This used to be a crash: every level is a stack frame, a frame has no idea
// how many are below it, and the process died on a file a hundred kilobytes
// long. The depth limit turns the crash into an ordinary refusal.
void jsonRefusesDeepNesting() {
    TEST("JSON nested past the depth limit is refused, not crashed on");
    std::string text;
    constexpr int kDepth = 100'000;
    text.reserve(static_cast<std::size_t>(kDepth) * 2 + 8);
    for (int i = 0; i < kDepth; ++i) text += '[';
    text += "0";
    for (int i = 0; i < kDepth; ++i) text += ']';

    Json parsed;
    std::string error;
    CHECK(!Json::parse(text, parsed, &error));
    CHECK(error.find("nested") != std::string::npos);
}

// A number in a scene file is data, and the file may put any double it likes
// in a JSON number. Converting one outside the target type's range is
// undefined behaviour, so the readers refuse rather than cast.
//
// Under the old code this failed by tripping UndefinedBehaviorSanitizer's
// float-cast-overflow check -- which is why the sanitizers are on by default:
// the cast "worked", in the sense that it produced a garbage integer and did
// not crash, so only the sanitizer could see it.
void numbersOutOfRangeAreRefused() {
    TEST("numbers outside a type's range read as the fallback, not as UB");
    Json root;
    std::string error;
    CHECK(Json::parse(R"({"huge": 1e300, "negative": -1e300})", root, &error));

    CHECK_EQ(root["huge"].asInt(42), 42);
    CHECK_EQ(root["huge"].asId(std::uint64_t{42}), std::uint64_t{42});
    CHECK_EQ(root["negative"].asInt(42), 42);
    CHECK_EQ(root["negative"].asId(std::uint64_t{42}), std::uint64_t{42});
}

// The scene reader is where those numbers actually meet the file. A canvas of
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
    CHECK(readSceneJson(text, doc, &error));
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
    CHECK(!readSceneJson(text, doc, &error));
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
    const std::vector<std::uint8_t> bytes = encodeCel(grid);

    CelFileInfo info;
    std::string error;
    CHECK(!readCelFileInfo(bytes, info, &error));
    CHECK(error.find("too many") != std::string::npos);

    TileGrid out;
    CHECK(!decodeCel(bytes, out, &error));
    CHECK(out.empty());  // untouched
}

}  // namespace

int main() {
    std::printf("hostile:\n");
    jsonRefusesDeepNesting();
    numbersOutOfRangeAreRefused();
    sceneCanvasOutOfRangeFallsBack();
    sceneIdsOutOfRangeAreRefused();
    aCelWithTooManyTilesIsRefused();
    return testing::summarise("hostile");
}
