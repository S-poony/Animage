// SPDX-License-Identifier: GPL-3.0-or-later
//
// Saving and loading: scene.json (the structure of a document, without its
// pixels) and the cel bytes. The format lives in the application now --
// ProjectIO -- with the JSON read and written by Qt's QJsonDocument, so the
// parsing itself is not ours to test; what is ours is everything this file
// tests: the scene that comes back from its own file, the cel bits that come
// back exactly, and the refusal of files that are not what they claim.

#include <cstdio>
#include <string>

#include "project_io.h"
#include "testing.h"

using namespace animage;

namespace {

// --- the scene -------------------------------------------------------------

// A document with something of everything in it: two tracks, a colour layer
// with sources, a held drawing, a drawing shared by two slots, an empty layer,
// and a non-default canvas.
Document buildScene() {
    Document doc;
    const TrackId back = doc.addTrack("background");
    const TrackId front = doc.addTrack("character");

    const LayerId rough = doc.addLayer(front, "rough");
    const LayerId clean = doc.addLayer(front, "clean");
    const LayerId colour = doc.addLayer(front, "colour", 2, LayerKind::Ctg);
    doc.addLayer(back, "sky");

    Layer settings = *doc.scene().findTrack(front)->findLayer(colour);
    settings.ctg_sources = {rough, clean};
    settings.opacity = 0.6f;
    settings.show_scribbles = true;
    doc.updateLayer(front, colour, settings);

    // The group-level properties, all four away from their defaults, because a
    // field that is only ever written at its default round-trips whatever the
    // reader does with it.
    TrackProperties group = doc.scene().findTrack(back)->properties();
    group.opacity = 0.35f;
    group.blend = BlendMode::Multiply;
    group.time_offset = -3;
    group.overwrite_drawings = false;
    doc.updateTrack(back, group);

    const ImageId first = doc.insertImage(front, 0);
    doc.extendExposure(front, 0, 2);  // held over three slots
    doc.insertImage(front, 3);
    doc.insertImage(back, 0);

    // Only the clean layer of the first drawing has been drawn on, so the file
    // has to carry a sparse map rather than a cel per layer.
    doc.celForWriting(front, first, clean);

    doc.setCanvasSize(1280, 720);
    doc.setFramerate(12);
    return doc;
}

// Compares two documents through the public model, which is what a file has to
// preserve. Cel *contents* are not here; those are the application's half.
void checkSameScene(const Document& a, const Document& b) {
    CHECK_EQ(a.scene().framerate, b.scene().framerate);
    CHECK_EQ(a.scene().width, b.scene().width);
    CHECK_EQ(a.scene().height, b.scene().height);
    CHECK_EQ(a.scene().tracks.size(), b.scene().tracks.size());

    for (std::size_t t = 0; t < a.scene().tracks.size(); ++t) {
        const Track& ta = a.scene().tracks[t];
        const Track& tb = b.scene().tracks[t];
        CHECK_EQ(ta.id, tb.id);
        CHECK_EQ(ta.name, tb.name);
        CHECK_NEAR(ta.opacity, tb.opacity, 0.0001);
        CHECK_EQ(static_cast<int>(ta.blend), static_cast<int>(tb.blend));
        CHECK_EQ(ta.time_offset, tb.time_offset);
        CHECK_EQ(ta.overwrite_drawings, tb.overwrite_drawings);
        // What a new drawing is called is derived from the drawings, so it has
        // to come back right without the file storing it.
        CHECK_EQ(ta.nextDrawingNumber(), tb.nextDrawingNumber());
        CHECK_EQ(ta.slots.size(), tb.slots.size());
        for (std::size_t i = 0; i < ta.slots.size(); ++i) CHECK_EQ(ta.slots[i], tb.slots[i]);

        CHECK_EQ(ta.layers.size(), tb.layers.size());
        for (std::size_t i = 0; i < ta.layers.size(); ++i) {
            const Layer& la = ta.layers[i];
            const Layer& lb = tb.layers[i];
            CHECK_EQ(la.id, lb.id);
            CHECK_EQ(la.name, lb.name);
            CHECK_EQ(static_cast<int>(la.kind), static_cast<int>(lb.kind));
            CHECK_EQ(la.visible, lb.visible);
            CHECK_EQ(la.show_scribbles, lb.show_scribbles);
            CHECK_NEAR(la.opacity, lb.opacity, 0.0001);
            CHECK_EQ(la.ctg_sources.size(), lb.ctg_sources.size());
            for (std::size_t s = 0; s < la.ctg_sources.size(); ++s) {
                CHECK_EQ(la.ctg_sources[s], lb.ctg_sources[s]);
            }
        }

        CHECK_EQ(ta.images.size(), tb.images.size());
        for (const auto& [id, image] : ta.images) {
            const Image* other = tb.findImage(id);
            CHECK(other != nullptr);
            if (!other) continue;
            CHECK_EQ(image.number, other->number);
            CHECK_EQ(image.cels.size(), other->cels.size());
            for (const auto& [layer, cel] : image.cels) CHECK_EQ(other->celFor(layer), cel);
        }
    }
}

void aSceneSurvivesTheRoundTrip() {
    TEST("a scene comes back from its own file unchanged");
    const Document original = buildScene();
    const std::string text = ProjectIO::writeSceneJson(original);

    // Printed because being readable is the reason the format is text at all.
    // If this ever stops looking like something you could fix in an editor, the
    // format has drifted from what it was chosen for.
    std::printf("%s", text.c_str());

    Document loaded;
    std::string error;
    CHECK(ProjectIO::readSceneJson(text, loaded, &error));
    CHECK_EQ(error, std::string());
    checkSameScene(original, loaded);

    // And writing the loaded document again produces the same bytes, which is
    // the property that keeps a project diffable across saves.
    CHECK_EQ(ProjectIO::writeSceneJson(loaded), text);
}

// The invariant the undo model rests on. A counter that restarted at one would
// hand out an id that a cel loaded from the file already answers to, and the
// two would then be the same drawing.
void idsResumePastTheFile() {
    TEST("ids resume past everything in the file, never reusing one");
    Document original = buildScene();
    const std::string text = ProjectIO::writeSceneJson(original);

    Document loaded;
    CHECK(ProjectIO::readSceneJson(text, loaded));

    // Collect every id the file mentions, then make more of each kind.
    std::vector<CelId> before = ProjectIO::celsReferencedBy(loaded);
    CHECK(!before.empty());

    const TrackId track = loaded.scene().tracks.front().id;
    const LayerId added = loaded.addLayer(track, "after loading");
    const ImageId image = loaded.insertImage(track, 0);
    loaded.celForWriting(track, image, added);

    for (const Track& t : loaded.scene().tracks) {
        CHECK(added != t.id);
        for (const Layer& layer : t.layers) {
            if (layer.id != added) CHECK(layer.id != added);
        }
    }
    for (const auto& [id, contents] : loaded.scene().tracks.front().images) {
        if (id != image) CHECK(id != image);
    }

    const std::vector<CelId> after = ProjectIO::celsReferencedBy(loaded);
    CHECK(after.size() > before.size());
    for (CelId fresh : after) {
        if (std::find(before.begin(), before.end(), fresh) != before.end()) continue;
        // A newly minted cel id must be higher than every one in the file.
        for (CelId old : before) CHECK(fresh > old);
    }
}

void loadingForgetsTheHistory() {
    TEST("loading a file leaves nothing to undo");
    Document doc = buildScene();
    CHECK(doc.undoDepth() > 0);

    const std::string text = ProjectIO::writeSceneJson(doc);
    CHECK(ProjectIO::readSceneJson(text, doc));
    CHECK_EQ(doc.undoDepth(), std::size_t{0});
    CHECK_EQ(doc.canUndo(), false);
    CHECK_EQ(doc.canRedo(), false);
}

void abadFileLeavesTheDocumentAlone() {
    TEST("a file that will not load leaves the open document untouched");
    Document doc = buildScene();
    const std::string good = ProjectIO::writeSceneJson(doc);

    const std::pair<const char*, const char*> bad[] = {
        {"not json at all", "not JSON"},
        {"[1,2,3]", "not an object"},
        {"{\"format\":\"something-else\",\"version\":1}", "not an Animage scene"},
        {"{\"format\":\"animage-scene\",\"version\":99}", "newer than this build"},
        {"{\"format\":\"animage-scene\",\"version\":1,\"tracks\":[]}", "no tracks"},
    };
    for (const auto& [text, expected] : bad) {
        std::string error;
        CHECK_EQ(ProjectIO::readSceneJson(text, doc, &error), false);
        CHECK(error.find(expected) != std::string::npos);
        // Still the document we started with.
        CHECK_EQ(ProjectIO::writeSceneJson(doc), good);
    }
}

// A cel shared by two images -- what duplicate-link is -- has to come back
// shared, not copied. Two drawings that were one drawing must still be one.
void asharedCelStaysShared() {
    TEST("a cel shared by two drawings is still shared after loading");
    Document doc;
    const TrackId track = doc.addTrack("main");
    const LayerId layer = doc.addLayer(track, "layer 1");
    const ImageId first = doc.insertImage(track, 0);
    doc.celForWriting(track, first, layer);

    // A second image pointing at the same cel is what the model calls exposure
    // by copy rather than by hold; build it directly.
    const ImageId second = doc.insertImage(track, 1);
    {
        Track* mutable_track = doc.mutableScene().findTrack(track);
        const CelId shared = mutable_track->findImage(first)->celFor(layer);
        mutable_track->findImage(second)->cels[layer] = shared;
        doc.addCelRef(shared);
    }

    Document loaded;
    CHECK(ProjectIO::readSceneJson(ProjectIO::writeSceneJson(doc), loaded));

    const Track* after = loaded.scene().findTrack(track);
    CHECK(after != nullptr);
    if (!after) return;
    const CelId a = after->findImage(first)->celFor(layer);
    const CelId b = after->findImage(second)->celFor(layer);
    CHECK(a != kNoId);
    CHECK_EQ(a, b);

    // And it is one cel in the manifest, not two.
    const std::vector<CelId> cels = ProjectIO::celsReferencedBy(loaded);
    CHECK_EQ(cels.size(), std::size_t{1});
}

// --- cel pixels ------------------------------------------------------------

TileRef makeTile(const std::vector<std::pair<std::size_t, float>>& samples) {
    auto tile = std::make_shared<Tile>();
    for (const auto& [index, value] : samples) tile->rgba[index] = Half(value);
    return tile;
}

// The whole reason the format is ours: every bit comes back.
void celPixelsSurviveExactly() {
    TEST("every half-float a cel can hold survives the round trip exactly");
    TileGrid grid;

    // Values chosen to break a 16-bit integer format: the smallest subnormal,
    // things far below one integer step, a value above one, and the awkward
    // fractions in between.
    const float awkward[] = {0.0f,       5.96e-8f,  1e-6f,     1.0f / 3.0f, 0.1f,
                             0.5f,       0.95f,     1.0f,      2.5f,        65504.0f};
    auto tile = std::make_shared<Tile>();
    for (std::size_t i = 0; i < std::size(awkward); ++i) {
        tile->rgba[i] = Half(awkward[i]);
    }
    tile->rgba[400] = Half(1.0f);  // so the tile is not transparent
    grid.set({0, 0}, tile);

    // And a tile a long way from the origin, in both directions, because the
    // drawing surface has no edges and the coordinates are signed.
    grid.set({-7, 3}, makeTile({{0, 0.25f}, {3, 1.0f}}));
    grid.set({12, -40}, makeTile({{1, 0.75f}, {3, 0.5f}}));

    const std::vector<std::uint8_t> bytes = ProjectIO::encodeCel(grid);
    TileGrid back;
    std::string error;
    CHECK(ProjectIO::decodeCel(bytes, back, &error));
    CHECK_EQ(error, std::string());

    CHECK_EQ(back.tileCount(), grid.tileCount());
    for (const TileCoord& coord : grid.coords()) {
        const TileRef before = grid.find(coord);
        const TileRef after = back.find(coord);
        CHECK(after != nullptr);
        if (!after) continue;
        // Bit for bit, not nearly: this is the claim. Counted as one check per
        // tile rather than one per sample, because sixty-five thousand identical
        // assertions tell you nothing extra and drown the suite's tally.
        std::size_t differing = 0;
        for (std::size_t i = 0; i < before->rgba.size(); ++i) {
            if (after->rgba[i].bits != before->rgba[i].bits) ++differing;
        }
        CHECK_EQ(differing, std::size_t{0});
    }
}

void anEmptyCelIsAnEmptyFile() {
    TEST("an empty cel writes a header and nothing else");
    TileGrid empty;
    const std::vector<std::uint8_t> bytes = ProjectIO::encodeCel(empty);
    CHECK_EQ(bytes.size(), std::size_t{24});

    ProjectIO::CelFileInfo info;
    CHECK(ProjectIO::readCelFileInfo(bytes, info));
    CHECK_EQ(info.tile_count, std::size_t{0});

    TileGrid back;
    CHECK(ProjectIO::decodeCel(bytes, back));
    CHECK(back.empty());
}

// Absent and transparent mean the same thing in the model, and the file must
// not be able to smuggle in a difference. An erased stroke leaves cleared tiles
// behind, and writing them would grow the file every time somebody rubbed
// something out.
void transparentTilesAreNotWritten() {
    TEST("fully transparent tiles are dropped rather than stored");
    TileGrid grid;
    grid.set({0, 0}, makeTile({{3, 1.0f}}));       // has coverage
    grid.set({1, 0}, std::make_shared<Tile>());    // cleared by an eraser
    CHECK_EQ(grid.tileCount(), std::size_t{2});

    const std::vector<std::uint8_t> bytes = ProjectIO::encodeCel(grid);
    ProjectIO::CelFileInfo info;
    CHECK(ProjectIO::readCelFileInfo(bytes, info));
    CHECK_EQ(info.tile_count, std::size_t{1});

    TileGrid back;
    CHECK(ProjectIO::decodeCel(bytes, back));
    CHECK_EQ(back.tileCount(), std::size_t{1});
    CHECK(back.find({1, 0}) == nullptr);
}

void celBytesAreStable() {
    TEST("the same cel encodes to the same bytes twice");
    TileGrid grid;
    // Inserted in an order that a hash will not preserve.
    grid.set({5, 5}, makeTile({{3, 1.0f}}));
    grid.set({-2, 9}, makeTile({{3, 0.5f}}));
    grid.set({0, 0}, makeTile({{3, 0.25f}}));
    grid.set({5, -5}, makeTile({{3, 0.75f}}));

    TileGrid other;
    other.set({0, 0}, makeTile({{3, 0.25f}}));
    other.set({5, -5}, makeTile({{3, 0.75f}}));
    other.set({-2, 9}, makeTile({{3, 0.5f}}));
    other.set({5, 5}, makeTile({{3, 1.0f}}));

    // Same tiles, different insertion order, identical file. Without this a save
    // rewrites every cel whether or not anything changed.
    CHECK(ProjectIO::encodeCel(grid) == ProjectIO::encodeCel(other));
}

void acorruptCelIsRefused() {
    TEST("a corrupt cel file is refused rather than half-read");
    TileGrid grid;
    grid.set({0, 0}, makeTile({{3, 1.0f}}));
    const std::vector<std::uint8_t> good = ProjectIO::encodeCel(grid);

    const auto refused = [](std::vector<std::uint8_t> bytes, const char* because) {
        TileGrid out;
        std::string error;
        CHECK_EQ(ProjectIO::decodeCel(bytes, out, &error), false);
        CHECK(error.find(because) != std::string::npos);
        CHECK(out.empty());  // untouched
    };

    refused({}, "too short");
    refused(std::vector<std::uint8_t>(24, 0), "wrong magic");

    // A tile count that says there is far more here than there is. This is the
    // one that matters: taken on trust it is a request to allocate a terabyte.
    // It is now caught by the per-cel tile budget, which refuses even earlier
    // than the size floor that used to catch it.
    std::vector<std::uint8_t> lying = good;
    lying.at(20) = 0xff;  // in-bounds: the header is always at least 24 bytes
    lying.at(21) = 0xff;
    refused(lying, "too many");

    // A count that is under the budget but still far larger than the file: the
    // size floor, not the budget, is what catches this one.
    std::vector<std::uint8_t> lying_plausible = good;
    lying_plausible.at(20) = 100;  // 100 tiles need 52,824 bytes; there are 552
    refused(lying_plausible, "truncated");

    // Truncated in the middle of the pixels.
    std::vector<std::uint8_t> cut = good;
    cut.resize(cut.size() - 100);
    refused(cut, "truncated");
}

}  // namespace

int main() {
    std::printf("serialise:\n");
    aSceneSurvivesTheRoundTrip();
    idsResumePastTheFile();
    loadingForgetsTheHistory();
    abadFileLeavesTheDocumentAlone();
    asharedCelStaysShared();
    celPixelsSurviveExactly();
    anEmptyCelIsAnEmptyFile();
    transparentTilesAreNotWritten();
    celBytesAreStable();
    acorruptCelIsRefused();
    return testing::summarise("serialise");
}
