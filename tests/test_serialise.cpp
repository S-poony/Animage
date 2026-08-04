// SPDX-License-Identifier: GPL-3.0-or-later
//
// scene.json: the structure of a document, without its pixels. The pixels are
// the application's half of saving and are tested elsewhere.

#include <cstdio>
#include <string>

#include "json.h"
#include "serialise.h"
#include "testing.h"

using namespace animage;

namespace {

// --- the JSON underneath ---------------------------------------------------

void jsonWritesWhatItReads() {
    TEST("JSON round-trips the shapes a scene file is made of");

    Json root = Json::object();
    root.set("text", Json::text("layer 1"));
    root.set("number", Json::number(24));
    root.set("fraction", Json::number(0.95));
    root.set("yes", Json::boolean(true));
    root.set("nothing", Json());

    Json list = Json::array();
    list.push(Json::number(1));
    list.push(Json::number(2));
    root.set("list", std::move(list));

    Json parsed;
    std::string error;
    CHECK(Json::parse(root.dump(), parsed, &error));
    CHECK_EQ(error, std::string());

    CHECK_EQ(parsed["text"].asText(), std::string("layer 1"));
    CHECK_EQ(parsed["number"].asInt(), 24);
    // Shortest-round-trip output, so a float survives exactly rather than
    // arriving as 0.94999999999999996.
    CHECK_EQ(parsed["fraction"].asNumber(), 0.95);
    CHECK_EQ(parsed["yes"].asBool(), true);
    CHECK(parsed["nothing"].isNull());
    CHECK_EQ(parsed["list"].size(), std::size_t{2});
    CHECK_EQ(parsed["list"].at(1).asInt(), 2);

    // A missing key reads as the fallback rather than throwing, which is what
    // lets an older file load with defaults.
    CHECK_EQ(parsed["absent"].asInt(7), 7);
    CHECK_EQ(parsed.has("absent"), false);
    CHECK_EQ(parsed.has("nothing"), true);
}

void jsonKeepsKeysInOrder() {
    TEST("JSON keeps object keys in the order they were set");
    Json root = Json::object();
    root.set("zebra", Json::number(1));
    root.set("apple", Json::number(2));
    // A hash would sort or shuffle these, and every save would rewrite the file
    // into a different order -- which is the whole reason for choosing a text
    // format.
    const std::string dumped = root.dump(0);
    CHECK(dumped.find("zebra") < dumped.find("apple"));
}

void jsonRefusesRubbish() {
    TEST("JSON refuses malformed input rather than guessing");
    const char* bad[] = {
        "",  "{",  "[1,2",  "{\"a\" 1}",  "{\"a\":}",  "tru",
        "{\"a\":1} trailing",  "\"unterminated",  "[1,]",
    };
    for (const char* text : bad) {
        Json parsed;
        std::string error;
        CHECK_EQ(Json::parse(text, parsed, &error), false);
        CHECK(!error.empty());
    }

    // And a string with the characters that have to be escaped survives.
    Json quoted = Json::text("a \"quoted\"\n\\path\\");
    Json parsed;
    CHECK(Json::parse(quoted.dump(), parsed));
    CHECK_EQ(parsed.asText(), std::string("a \"quoted\"\n\\path\\"));
}

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
        CHECK_EQ(ta.next_drawing_number, tb.next_drawing_number);
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
    const std::string text = writeSceneJson(original);

    // Printed because being readable is the reason the format is text at all.
    // If this ever stops looking like something you could fix in an editor, the
    // format has drifted from what it was chosen for.
    std::printf("%s", text.c_str());

    Document loaded;
    std::string error;
    CHECK(readSceneJson(text, loaded, &error));
    CHECK_EQ(error, std::string());
    checkSameScene(original, loaded);

    // And writing the loaded document again produces the same bytes, which is
    // the property that keeps a project diffable across saves.
    CHECK_EQ(writeSceneJson(loaded), text);
}

// The invariant the undo model rests on. A counter that restarted at one would
// hand out an id that a cel loaded from the file already answers to, and the
// two would then be the same drawing.
void idsResumePastTheFile() {
    TEST("ids resume past everything in the file, never reusing one");
    Document original = buildScene();
    const std::string text = writeSceneJson(original);

    Document loaded;
    CHECK(readSceneJson(text, loaded));

    // Collect every id the file mentions, then make more of each kind.
    std::vector<CelId> before = celsReferencedBy(loaded);
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

    const std::vector<CelId> after = celsReferencedBy(loaded);
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

    const std::string text = writeSceneJson(doc);
    CHECK(readSceneJson(text, doc));
    CHECK_EQ(doc.undoDepth(), std::size_t{0});
    CHECK_EQ(doc.canUndo(), false);
    CHECK_EQ(doc.canRedo(), false);
}

void abadFileLeavesTheDocumentAlone() {
    TEST("a file that will not load leaves the open document untouched");
    Document doc = buildScene();
    const std::string good = writeSceneJson(doc);

    const std::pair<const char*, const char*> bad[] = {
        {"not json at all", "not JSON"},
        {"[1,2,3]", "not an object"},
        {"{\"format\":\"something-else\",\"version\":1}", "not an Animage scene"},
        {"{\"format\":\"animage-scene\",\"version\":99}", "newer than this build"},
        {"{\"format\":\"animage-scene\",\"version\":1,\"tracks\":[]}", "no tracks"},
    };
    for (const auto& [text, expected] : bad) {
        std::string error;
        CHECK_EQ(readSceneJson(text, doc, &error), false);
        CHECK(error.find(expected) != std::string::npos);
        // Still the document we started with.
        CHECK_EQ(writeSceneJson(doc), good);
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
    CHECK(readSceneJson(writeSceneJson(doc), loaded));

    const Track* after = loaded.scene().findTrack(track);
    CHECK(after != nullptr);
    if (!after) return;
    const CelId a = after->findImage(first)->celFor(layer);
    const CelId b = after->findImage(second)->celFor(layer);
    CHECK(a != kNoId);
    CHECK_EQ(a, b);

    // And it is one cel in the manifest, not two.
    const std::vector<CelId> cels = celsReferencedBy(loaded);
    CHECK_EQ(cels.size(), std::size_t{1});
}

}  // namespace

int main() {
    std::printf("serialise:\n");
    jsonWritesWhatItReads();
    jsonKeepsKeysInOrder();
    jsonRefusesRubbish();
    aSceneSurvivesTheRoundTrip();
    idsResumePastTheFile();
    loadingForgetsTheHistory();
    abadFileLeavesTheDocumentAlone();
    asharedCelStaysShared();
    return testing::summarise("serialise");
}
