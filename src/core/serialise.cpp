// SPDX-License-Identifier: GPL-3.0-or-later
#include "serialise.h"

#include <algorithm>
#include <unordered_set>

#include "json.h"

namespace animage {
namespace {

constexpr const char* kFormatName = "animage-scene";

// Names rather than numbers in the file. An enum written as an integer is a
// trap the first time somebody inserts a value in the middle of it, and the
// point of choosing JSON was that a person can read the result.
const char* blendName(BlendMode blend) {
    switch (blend) {
        case BlendMode::Multiply: return "multiply";
        case BlendMode::Screen: return "screen";
        case BlendMode::Add: return "add";
        case BlendMode::Normal: break;
    }
    return "normal";
}

BlendMode blendFromName(const std::string& name) {
    if (name == "multiply") return BlendMode::Multiply;
    if (name == "screen") return BlendMode::Screen;
    if (name == "add") return BlendMode::Add;
    return BlendMode::Normal;
}

const char* kindName(LayerKind kind) { return kind == LayerKind::Ctg ? "ctg" : "raster"; }

LayerKind kindFromName(const std::string& name) {
    return name == "ctg" ? LayerKind::Ctg : LayerKind::Raster;
}

const char* directionName(CtgDirection direction) {
    switch (direction) {
        case CtgDirection::Backward: return "backward";
        case CtgDirection::Nearest: return "nearest";
        case CtgDirection::Forward: break;
    }
    return "forward";
}

CtgDirection directionFromName(const std::string& name) {
    if (name == "backward") return CtgDirection::Backward;
    if (name == "nearest") return CtgDirection::Nearest;
    return CtgDirection::Forward;
}

Json writeColour(const Rgba& colour) {
    Json out = Json::array();
    out.push(Json::number(colour.r));
    out.push(Json::number(colour.g));
    out.push(Json::number(colour.b));
    out.push(Json::number(colour.a));
    return out;
}

Rgba readColour(const Json& json) {
    Rgba colour;
    colour.r = json.at(0).asFloat();
    colour.g = json.at(1).asFloat();
    colour.b = json.at(2).asFloat();
    colour.a = json.at(3).asFloat();
    return colour;
}

Json writeLayer(const Layer& layer) {
    Json out = Json::object();
    out.set("id", Json::number(static_cast<double>(layer.id)));
    out.set("name", Json::text(layer.name));
    out.set("kind", Json::text(kindName(layer.kind)));
    out.set("opacity", Json::number(layer.opacity));
    out.set("visible", Json::boolean(layer.visible));
    out.set("locked", Json::boolean(layer.locked));
    out.set("blend", Json::text(blendName(layer.blend)));
    if (layer.kind == LayerKind::Ctg) {
        Json sources = Json::array();
        for (LayerId source : layer.ctg_sources) {
            sources.push(Json::number(static_cast<double>(source)));
        }
        out.set("ctg_sources", std::move(sources));
        // `show_scribbles` is what you are looking at, not what is on the
        // layer. It is saved anyway: reopening a file with the marks showing,
        // because that is how you left it, is the least surprising thing.
        out.set("show_scribbles", Json::boolean(layer.show_scribbles));
        out.set("ctg_inherit", Json::boolean(layer.ctg_inherit));
        out.set("ctg_direction", Json::text(directionName(layer.ctg_direction)));
        out.set("ctg_follow_motion", Json::boolean(layer.ctg_follow_motion));
    }
    return out;
}

Layer readLayer(const Json& json) {
    Layer layer;
    layer.id = json["id"].asId();
    layer.name = json["name"].asText();
    layer.kind = kindFromName(json["kind"].asText("raster"));
    layer.opacity = json["opacity"].asFloat(1.0f);
    layer.visible = json["visible"].asBool(true);
    layer.locked = json["locked"].asBool(false);
    layer.blend = blendFromName(json["blend"].asText("normal"));
    const Json& sources = json["ctg_sources"];
    for (std::size_t i = 0; i < sources.size(); ++i) {
        layer.ctg_sources.push_back(sources.at(i).asId());
    }
    layer.show_scribbles = json["show_scribbles"].asBool(false);
    // A project written before carrying existed has none of these keys, and
    // the defaults are what it behaved as: it had no choice about direction.
    // Carrying itself defaults on, which does change how such a project reads
    // -- a drawing with no marks of its own now shows an earlier drawing's
    // rather than nothing. That is the feature, and it is reversible from the
    // panel without touching a cel.
    //
    // Moving the marks defaults on for the same reason and reads the same way:
    // an old project's carried marks will land where the drawing went rather
    // than where they were drawn, and nothing on disk changes either way,
    // because what is stored is still only the marks.
    layer.ctg_inherit = json["ctg_inherit"].asBool(true);
    layer.ctg_direction = directionFromName(json["ctg_direction"].asText("forward"));
    layer.ctg_follow_motion = json["ctg_follow_motion"].asBool(true);
    return layer;
}

Json writeImage(const Image& image, const std::vector<Layer>& layers) {
    Json out = Json::object();
    out.set("id", Json::number(static_cast<double>(image.id)));
    out.set("number", Json::number(image.number));
    if (image.marker) out.set("marker", writeColour(*image.marker));

    // Walked in layer order rather than in the hash's order, so the file is the
    // same twice running and a diff shows what changed rather than where the
    // hash moved.
    Json cels = Json::array();
    for (const Layer& layer : layers) {
        const CelId cel = image.celFor(layer.id);
        if (cel == kNoId) continue;  // sparse: absent means the layer is empty
        Json entry = Json::object();
        entry.set("layer", Json::number(static_cast<double>(layer.id)));
        entry.set("cel", Json::number(static_cast<double>(cel)));
        cels.push(std::move(entry));
    }
    out.set("cels", std::move(cels));
    return out;
}

Image readImage(const Json& json) {
    Image image;
    image.id = json["id"].asId();
    image.number = json["number"].asInt();
    if (json.has("marker") && !json["marker"].isNull()) {
        image.marker = readColour(json["marker"]);
    }
    const Json& cels = json["cels"];
    for (std::size_t i = 0; i < cels.size(); ++i) {
        const LayerId layer = cels.at(i)["layer"].asId();
        const CelId cel = cels.at(i)["cel"].asId();
        if (layer != kNoId && cel != kNoId) image.cels[layer] = cel;
    }
    return image;
}

Json writeTrack(const Track& track) {
    Json out = Json::object();
    out.set("id", Json::number(static_cast<double>(track.id)));
    out.set("name", Json::text(track.name));
    out.set("opacity", Json::number(track.opacity));
    out.set("blend", Json::text(blendName(track.blend)));
    out.set("time_offset", Json::number(track.time_offset));
    out.set("next_drawing_number", Json::number(track.next_drawing_number));

    Json layers = Json::array();
    for (const Layer& layer : track.layers) layers.push(writeLayer(layer));
    out.set("layers", std::move(layers));

    // The slots are the exposure and repeat freely; that is the point of them.
    Json slots = Json::array();
    for (ImageId id : track.slots) slots.push(Json::number(static_cast<double>(id)));
    out.set("slots", std::move(slots));

    // `images` is a hash, so it is written in the order the slots first mention
    // each drawing. Same reason as the cels above: a stable file.
    Json images = Json::array();
    std::unordered_set<ImageId> written;
    const auto writeOne = [&](ImageId id) {
        if (id == kNoId || !written.insert(id).second) return;
        const Image* image = track.findImage(id);
        if (image) images.push(writeImage(*image, track.layers));
    };
    for (ImageId id : track.slots) writeOne(id);
    // An image no slot mentions cannot happen today, but losing one silently
    // would be the kind of bug that only shows up in somebody's finished shot.
    for (const auto& [id, image] : track.images) writeOne(id);
    out.set("images", std::move(images));
    return out;
}

Track readTrack(const Json& json) {
    Track track;
    track.id = json["id"].asId();
    track.name = json["name"].asText();
    track.opacity = json["opacity"].asFloat(1.0f);
    track.blend = blendFromName(json["blend"].asText("normal"));
    track.time_offset = json["time_offset"].asInt();
    track.next_drawing_number = json["next_drawing_number"].asInt(1);

    const Json& layers = json["layers"];
    for (std::size_t i = 0; i < layers.size(); ++i) track.layers.push_back(readLayer(layers.at(i)));

    const Json& images = json["images"];
    for (std::size_t i = 0; i < images.size(); ++i) {
        Image image = readImage(images.at(i));
        if (image.id != kNoId) track.images[image.id] = std::move(image);
    }

    const Json& slots = json["slots"];
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const ImageId id = slots.at(i).asId();
        // A slot naming an image that is not in the file would be a hole in the
        // timeline that every walk over `slots` would then have to guard.
        if (id != kNoId && track.images.count(id)) track.slots.push_back(id);
    }
    return track;
}

}  // namespace

std::string writeSceneJson(const Document& doc) {
    const Scene& scene = doc.scene();

    Json canvas = Json::object();
    canvas.set("width", Json::number(scene.width));
    canvas.set("height", Json::number(scene.height));

    Json out = Json::object();
    out.set("format", Json::text(kFormatName));
    out.set("version", Json::number(kSceneFormatVersion));
    out.set("framerate", Json::number(scene.framerate));
    out.set("canvas", std::move(canvas));

    Json tracks = Json::array();
    for (const Track& track : scene.tracks) tracks.push(writeTrack(track));
    out.set("tracks", std::move(tracks));

    return out.dump() + "\n";
}

bool readSceneJson(std::string_view text, Document& doc, std::string* error) {
    const auto refuse = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };

    Json root;
    std::string parse_error;
    if (!Json::parse(text, root, &parse_error)) return refuse("not JSON: " + parse_error);
    if (!root.isObject()) return refuse("not a scene: the file is not an object");
    if (root["format"].asText() != kFormatName) {
        return refuse("not an Animage scene: \"format\" is not \"" + std::string(kFormatName) +
                      "\"");
    }
    const int version = root["version"].asInt();
    if (version <= 0 || version > kSceneFormatVersion) {
        return refuse("scene version " + std::to_string(version) + " is newer than this build, " +
                      "which understands up to " + std::to_string(kSceneFormatVersion));
    }

    Scene scene;
    scene.framerate = std::max(1, root["framerate"].asInt(24));
    scene.width = std::clamp(root["canvas"]["width"].asInt(1920), kMinCanvasSide, kMaxCanvasSide);
    scene.height = std::clamp(root["canvas"]["height"].asInt(1080), kMinCanvasSide, kMaxCanvasSide);

    const Json& tracks = root["tracks"];
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        Track track = readTrack(tracks.at(i));
        if (track.id != kNoId) scene.tracks.push_back(std::move(track));
    }
    if (scene.tracks.empty()) return refuse("scene has no tracks");

    // Nothing is written into `doc` before this point, so a file that fails any
    // check above leaves whatever was open alone.
    doc.loadScene(std::move(scene));
    return true;
}

std::vector<CelId> celsReferencedBy(const Document& doc) {
    std::vector<CelId> cels;
    std::unordered_set<CelId> seen;
    for (const Track& track : doc.scene().tracks) {
        const auto collect = [&](ImageId id) {
            const Image* image = track.findImage(id);
            if (!image) return;
            for (const Layer& layer : track.layers) {
                const CelId cel = image->celFor(layer.id);
                if (cel != kNoId && seen.insert(cel).second) cels.push_back(cel);
            }
        };
        std::unordered_set<ImageId> done;
        for (ImageId id : track.slots) {
            if (done.insert(id).second) collect(id);
        }
        for (const auto& [id, image] : track.images) {
            if (done.insert(id).second) collect(id);
        }
    }
    return cels;
}

}  // namespace animage
