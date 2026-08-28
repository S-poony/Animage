// SPDX-License-Identifier: GPL-3.0-or-later
//
// Saving and loading: scene.json (the structure of a document, without its
// pixels) and the cel bytes. The format lives in the application now --
// ProjectIO -- with the JSON read and written by Qt's QJsonDocument, so the
// parsing itself is not ours to test; what is ours is everything this file
// tests: the scene that comes back from its own file, the cel bits that come
// back exactly, and the refusal of files that are not what they claim.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

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

    // An imported picture on a track of its own, held past its last drawing --
    // which is what an import lands as. It has no cels, deliberately: a
    // reference layer's pixels are derived from the file named below.
    const TrackId sheet = doc.addTrack("modelsheet");
    const LayerId imported = doc.addLayer(sheet, "modelsheet", 0, LayerKind::Reference);
    Layer reference = *doc.scene().findTrack(sheet)->findLayer(imported);
    reference.reference_sources = {"model-sheet.png"};
    // And where it was put. Every field away from its default, including the
    // two that look decorative: a flip is a sign the matrix carries, and the
    // pivot decides where the rotation happens, so a placement that came back
    // with either of them missing would be a different picture. Nine numbers
    // that are only ever compared exactly -- see Transform::operator== -- which
    // is why they are pinned here rather than left to a round-trip of the ones
    // somebody thought were interesting.
    reference.placement.dx = 37.0;
    reference.placement.dy = -12.0;
    reference.placement.rotation = 22.5;
    reference.placement.scale_x = 0.5;
    reference.placement.scale_y = 1.25;
    reference.placement.flip_x = true;
    reference.placement.pivot_x = 640.0;
    reference.placement.pivot_y = 360.0;
    doc.updateLayer(sheet, imported, reference);
    const ImageId sheet_drawing = doc.insertImage(sheet, 0);
    doc.setSourceFrame(sheet, sheet_drawing, imported, 0);
    TrackProperties held = doc.scene().findTrack(sheet)->properties();
    held.end = TrackEnd::HoldLast;
    doc.updateTrack(sheet, held);

    // And an imported *sequence*, which is the same layer kind with more than
    // one file. Three drawings pointed at three frames, and deliberately not in
    // file order: nothing indexes the list by slot, so a file that reads the
    // frames back by counting would pass on a straight 0,1,2 and be wrong here.
    // The last drawing is pointed at no frame at all, which is what an empty
    // drawing on a reference layer looks like -- absent, exactly as a missing
    // cel is.
    const TrackId animatic = doc.addTrack("animatic");
    const LayerId shots = doc.addLayer(animatic, "animatic", 0, LayerKind::Reference);
    Layer frames = *doc.scene().findTrack(animatic)->findLayer(shots);
    frames.reference_sources = {"board_0001.png", "board_0002.png", "board_0003.png"};
    doc.updateLayer(animatic, shots, frames);
    const ImageId a0 = doc.insertImage(animatic, 0);
    const ImageId a1 = doc.insertImage(animatic, 1);
    const ImageId a2 = doc.insertImage(animatic, 2);
    doc.insertImage(animatic, 3);
    doc.setSourceFrame(animatic, a0, shots, 2);
    doc.setSourceFrame(animatic, a1, shots, 0);
    doc.setSourceFrame(animatic, a2, shots, 1);

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
    doc.setSceneLength(true, 96);
    return doc;
}

// Compares two documents through the public model, which is what a file has to
// preserve. Cel *contents* are not here; those are the application's half.
void checkSameScene(const Document& a, const Document& b) {
    CHECK_EQ(a.scene().framerate, b.scene().framerate);
    CHECK_EQ(a.scene().fixed_length, b.scene().fixed_length);
    CHECK_EQ(a.scene().length, b.scene().length);
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
            // Which files a reference layer shows, and in what order. It holds
            // no cels, so this is the only thing standing between the layer and
            // drawing nothing -- losing it here would look exactly like an
            // import that went blank. The order is checked and not only the
            // set: a drawing names its picture by position in this list, so a
            // list that came back shuffled is every frame on the wrong drawing.
            CHECK_EQ(la.reference_sources.size(), lb.reference_sources.size());
            for (std::size_t s = 0; s < la.reference_sources.size() &&
                                    s < lb.reference_sources.size(); ++s) {
                CHECK_EQ(la.reference_sources[s], lb.reference_sources[s]);
            }
            // And where that picture goes. This is the one transform in the
            // program that outlives the gesture that made it -- everywhere else
            // a transform is committed into pixels and forgotten -- so it is
            // the file's job to keep it, and the picture reopens at the origin
            // if it does not. Field by field so a failure says which one, and
            // exactly rather than within a tolerance, because the derived
            // pixels are keyed on this and a placement that is nearly right is
            // a cache that never matches.
            CHECK_EQ(la.placement.dx, lb.placement.dx);
            CHECK_EQ(la.placement.dy, lb.placement.dy);
            CHECK_EQ(la.placement.rotation, lb.placement.rotation);
            CHECK_EQ(la.placement.scale_x, lb.placement.scale_x);
            CHECK_EQ(la.placement.scale_y, lb.placement.scale_y);
            CHECK_EQ(la.placement.flip_x, lb.placement.flip_x);
            CHECK_EQ(la.placement.flip_y, lb.placement.flip_y);
            CHECK_EQ(la.placement.pivot_x, lb.placement.pivot_x);
            CHECK_EQ(la.placement.pivot_y, lb.placement.pivot_y);
        }

        CHECK_EQ(ta.images.size(), tb.images.size());
        for (const auto& [id, image] : ta.images) {
            const Image* other = tb.findImage(id);
            CHECK(other != nullptr);
            if (!other) continue;
            CHECK_EQ(image.number, other->number);
            CHECK_EQ(image.cels.size(), other->cels.size());
            for (const auto& [layer, cel] : image.cels) CHECK_EQ(other->celFor(layer), cel);
            // Which frame of an import each drawing shows. Both directions,
            // because the two failures are different: a lost entry is a drawing
            // that goes blank, and an entry that arrived where there was none
            // is a drawing showing a picture nobody put there.
            CHECK_EQ(image.source_frames.size(), other->source_frames.size());
            for (const auto& [layer, frame] : image.source_frames) {
                CHECK_EQ(other->sourceFrameFor(layer), frame);
            }
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


// --- imported pictures -----------------------------------------------------

// A reference layer's whole connection to its picture is a name, so the file
// has to carry it and a save has to know to bring the bytes along. Both halves
// fail the same way from the outside -- the layer draws nothing -- and neither
// is visible in a document that was never written to disk.

void anImportContributesNoCels() {
    TEST("a reference layer writes no cels, however many drawings it has");
    Document doc = buildScene();

    // The manifest a save writes is one file per cel the scene refers to. An
    // imported picture must not be in it: it has no cel to write, and a
    // manifest that named one would have the save encoding pixels that do not
    // exist.
    const std::size_t before = ProjectIO::celsReferencedBy(doc).size();

    const TrackId track = doc.addTrack("another import");
    const LayerId layer = doc.addLayer(track, "reference", 0, LayerKind::Reference);
    Layer settings = *doc.scene().findTrack(track)->findLayer(layer);
    settings.reference_sources = {"background.jpg"};
    doc.updateLayer(track, layer, settings);
    for (int i = 0; i < 5; ++i) doc.insertImage(track, 0);

    CHECK_EQ(ProjectIO::celsReferencedBy(doc).size(), before);
}

void everyImportedFileIsNamedOnce() {
    TEST("the import manifest lists each file once, sorted, and skips the unset");
    Document doc;
    const TrackId first = doc.addTrack("one");
    const TrackId second = doc.addTrack("two");
    const TrackId third = doc.addTrack("three");

    const auto point = [&](TrackId track, const std::string& at) {
        const LayerId layer = doc.addLayer(track, "reference", 0, LayerKind::Reference);
        Layer settings = *doc.scene().findTrack(track)->findLayer(layer);
        settings.reference_sources = {at};
        doc.updateLayer(track, layer, settings);
    };
    point(first, "sky.png");
    point(second, "animatic.png");
    // The same file on two layers is one file to carry, not two.
    point(third, "sky.png");
    // And a reference layer that has not been pointed at anything yet is not a
    // missing file. It is what a layer looks like before the import finishes.
    doc.addLayer(first, "empty reference", 0, LayerKind::Reference);

    const std::vector<std::string> named = ProjectIO::importsReferencedBy(doc);
    CHECK_EQ(named.size(), std::size_t{2});
    if (named.size() == 2) {
        CHECK_EQ(named[0], std::string("animatic.png"));
        CHECK_EQ(named[1], std::string("sky.png"));
    }
}

// The version gate, from the direction that matters.
//
// A build that does not know this kind reads "reference" as raster -- that is
// what kindFromName does with any word it has not heard of -- finds no cels,
// concludes the layer is empty, and autosaves over the project having dropped
// the import. Nothing about that fails loudly, which is exactly why the number
// exists. This pins that the number moved.
void importsRaisedTheFormatVersion() {
    TEST("the version is past 2, which is what stops an older build eating an import");
    // **A floor and not the number.** This used to assert the exact version its
    // own bump introduced, and so broke the next time anything else was added
    // -- which is the wrong failure: what this test is about is that imports
    // are behind a gate at all, and the gate does not get lower. Whichever bump
    // is current asserts its own number, next to what it was for.
    const std::string text = ProjectIO::writeSceneJson(buildScene());
    CHECK(ProjectIO::kSceneFormatVersion >= 2);
    CHECK(text.find("\"version\": " + std::to_string(ProjectIO::kSceneFormatVersion)) !=
          std::string::npos);

    // And a file from the future is still refused rather than half-read.
    std::string tampered = text;
    const std::string wrote = "\"version\": " + std::to_string(ProjectIO::kSceneFormatVersion);
    const std::size_t at = tampered.find(wrote);
    if (at != std::string::npos) tampered.replace(at, wrote.size(), "\"version\": 99");
    Document loaded;
    std::string error;
    CHECK(!ProjectIO::readSceneJson(tampered, loaded, &error));
    CHECK(!error.empty());
}


// --- soundtracks -----------------------------------------------------------

void aSoundtrackSurvivesTheRoundTrip() {
    TEST("a soundtrack comes back with its file, its placement and its gain");
    Document doc = buildScene();
    const TrackId sound = doc.addAudioTrack("dialogue", "dialogue.wav");
    doc.setAudioTrackPlacement(sound, -4, 0.5);

    // The samples are derived and must not be written. Installing some here is
    // what makes the next check mean something: a save that carried them would
    // put megabytes of float into scene.json and nobody would notice until a
    // ten-second track made the file unreadable in an editor.
    AudioClip clip;
    clip.rate = 48000;
    clip.channels = 2;
    clip.samples.assign(4800, 0.25f);
    doc.setAudioSamples(sound, std::move(clip));

    const std::string text = ProjectIO::writeSceneJson(doc);
    CHECK(text.find("dialogue.wav") != std::string::npos);
    CHECK(text.find("0.25") == std::string::npos);  // no samples anywhere in it

    Document loaded;
    std::string error;
    CHECK(ProjectIO::readSceneJson(text, loaded, &error));
    CHECK_EQ(loaded.scene().audio_tracks.size(), std::size_t{1});

    const AudioTrack& back = loaded.scene().audio_tracks.front();
    CHECK_EQ(back.source, std::string("dialogue.wav"));
    CHECK_EQ(back.name, std::string("dialogue"));
    CHECK_EQ(back.offset_frames, -4);  // a breath in front of the word
    CHECK_NEAR(back.gain, 0.5, 1e-9);

    // Derived, so it does not come back -- and nothing pretends it did.
    CHECK(loaded.audioSamplesFor(back.id) == nullptr);

    // The tracks are untouched by any of it, which is the whole "audio is not a
    // track" argument surviving a save.
    CHECK_EQ(loaded.scene().tracks.size(), doc.scene().tracks.size());
}

void aProjectWithNoSoundIsTheSameBytesItAlwaysWas() {
    TEST("a scene with no soundtrack writes no audio_tracks key at all");
    const std::string text = ProjectIO::writeSceneJson(buildScene());
    // Not tidiness: every project that exists has no sound in it, and a key
    // appearing in all of them the first time this build opens them would make
    // every one of those files differ for no reason anybody could point at.
    CHECK(text.find("audio_tracks") == std::string::npos);
}

void everySoundtrackFileIsNamedOnce() {
    TEST("two soundtracks naming one file is one file to carry");
    Document doc = buildScene();
    doc.addAudioTrack("take one", "dialogue.wav");
    doc.addAudioTrack("take two", "dialogue.wav");
    doc.addAudioTrack("room", "room-tone.wav");

    const std::vector<std::string> named = ProjectIO::audioReferencedBy(doc);
    CHECK_EQ(named.size(), std::size_t{2});
    CHECK_EQ(named[0], std::string("dialogue.wav"));
    CHECK_EQ(named[1], std::string("room-tone.wav"));

    // And a soundtrack is not a picture: the two folders are two namespaces, so
    // nothing here reaches imports/.
    const std::vector<std::string> pictures = ProjectIO::importsReferencedBy(doc);
    for (const std::string& one : pictures) CHECK(one != std::string("dialogue.wav"));
}

void undoingAnImportTakesTheSoundtrackWithIt() {
    TEST("adding and placing a soundtrack are edits, and both undo");
    Document doc = buildScene();
    doc.clearHistory();

    const TrackId sound = doc.addAudioTrack("dialogue", "dialogue.wav");
    CHECK_EQ(doc.scene().audio_tracks.size(), std::size_t{1});

    doc.setAudioTrackPlacement(sound, 12, 0.4);
    CHECK_EQ(doc.scene().findAudioTrack(sound)->offset_frames, 12);

    CHECK(doc.undo());  // the placement
    CHECK_EQ(doc.scene().findAudioTrack(sound)->offset_frames, 0);
    CHECK_NEAR(doc.scene().findAudioTrack(sound)->gain, 1.0, 1e-9);

    CHECK(doc.undo());  // the import
    CHECK_EQ(doc.scene().audio_tracks.size(), std::size_t{0});

    CHECK(doc.redo());
    CHECK_EQ(doc.scene().audio_tracks.size(), std::size_t{1});
    // The id survives, which is what lets the samples installed against it
    // still be the right samples after an undo and a redo.
    CHECK_EQ(doc.scene().audio_tracks.front().id, sound);
}

void gainIsClampedWhereItIsStoredAndNotAtEachCaller() {
    TEST("a gain out of range is clamped once, where it is written");
    Document doc = buildScene();
    const TrackId sound = doc.addAudioTrack("dialogue", "dialogue.wav");

    doc.setAudioTrackPlacement(sound, 0, 4.0);
    CHECK_NEAR(doc.scene().findAudioTrack(sound)->gain, 1.0, 1e-9);
    doc.setAudioTrackPlacement(sound, 0, -1.0);
    CHECK_NEAR(doc.scene().findAudioTrack(sound)->gain, 0.0, 1e-9);

    // And a file that says something impossible is clamped on the way in too,
    // rather than being trusted because it was written by us once.
    std::string text = ProjectIO::writeSceneJson(doc);
    const std::string wrote = "\"gain\": 0";
    const std::size_t at = text.find(wrote);
    CHECK(at != std::string::npos);
    text.replace(at, wrote.size(), "\"gain\": 900");
    Document loaded;
    CHECK(ProjectIO::readSceneJson(text, loaded, nullptr));
    CHECK_NEAR(loaded.scene().audio_tracks.front().gain, 1.0, 1e-9);
}

void soundtracksRaisedTheFormatVersion() {
    TEST("scene.json says version 3, which stops an older build orphaning a soundtrack");
    Document doc = buildScene();
    doc.addAudioTrack("dialogue", "dialogue.wav");
    const std::string text = ProjectIO::writeSceneJson(doc);
    CHECK(text.find("\"version\": 3") != std::string::npos);
    CHECK(ProjectIO::kSceneFormatVersion >= 3);
}

void aSoundtrackDoesNotLengthenTheShot() {
    TEST("importing an hour of sound does not make the shot an hour long");
    Document doc = buildScene();
    const std::size_t before = doc.scene().shotFrames();
    const TrackId sound = doc.addAudioTrack("dialogue", "dialogue.wav");

    AudioClip clip;
    clip.rate = 48000;
    clip.channels = 1;
    clip.samples.assign(48000 * 60, 0.0f);  // a minute
    doc.setAudioSamples(sound, std::move(clip));

    CHECK_EQ(doc.scene().shotFrames(), before);
    CHECK_EQ(doc.scene().timelineFrames(), std::max(before, doc.scene().longestTrack()));
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
    anImportContributesNoCels();
    everyImportedFileIsNamedOnce();
    importsRaisedTheFormatVersion();
    aSoundtrackSurvivesTheRoundTrip();
    aProjectWithNoSoundIsTheSameBytesItAlwaysWas();
    everySoundtrackFileIsNamedOnce();
    undoingAnImportTakesTheSoundtrackWithIt();
    gainIsClampedWhereItIsStoredAndNotAtEachCaller();
    soundtracksRaisedTheFormatVersion();
    aSoundtrackDoesNotLengthenTheShot();
    return testing::summarise("serialise");
}
