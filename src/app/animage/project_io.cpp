// SPDX-License-Identifier: GPL-3.0-or-later
#include "project_io.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <system_error>
#include <unordered_set>

using namespace animage;

namespace {

// ---------------------------------------------------------------------------
// the cel bytes: "ANIMCEL2"
// ---------------------------------------------------------------------------

constexpr char kCelMagic[8] = {'A', 'N', 'I', 'M', 'C', 'E', 'L', '2'};
constexpr std::size_t kHeaderSize = 24;
constexpr std::uint32_t kSampleHalfLittleEndian = 0;
constexpr int kChannels = 4;
constexpr std::size_t kRowTableBytes = static_cast<std::size_t>(kTileSize) * 4;

// The most tiles one cel may hold. A tile is 128x128 half-float RGBA -- 128 KB
// -- so this is a memory budget as much as a count, and the count is the part
// the file gets to say. The number is what a full layer over the largest
// canvas the format allows (16384 squared, at 128-pixel tiles) can occupy:
// 16384 tiles, two gigabytes. Anything past it is either off-canvas work at a
// canvas nobody renders, or a file built to turn a few kilobytes into a
// gigabyte -- one opaque pixel per tile costs 528 bytes in the file and 128 KB
// in memory, so a sparse cel is exactly the shape a memory bomb takes.
constexpr std::size_t kMaxCelTiles = 16384;

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

// ---------------------------------------------------------------------------
// the scene text: JSON, via QJsonDocument
// ---------------------------------------------------------------------------

constexpr const char* kFormatName = "animage-scene";

// The shortest form that reads back identically, so the file stays legible.
// Tried as a float first, because almost every number here is one: opacity,
// colour channels, ids. Widening 0.6f to double and asking for the shortest
// double gives 0.6000000238418579, which is exact, useless to read, and makes
// a diff of two saves unreadable. If the float round-trips to the same double
// it is the same number, written the way it was meant.
QJsonValue jsonNumber(double value) {
    const float narrowed = static_cast<float>(value);
    const double clean = (static_cast<double>(narrowed) == value) ? narrowed : value;
    return QJsonValue(clean);
}

// The readers. Every one takes a fallback and refuses anything else, so a
// field that is missing or of the wrong type reads as the fallback and a file
// from an older version loads with the defaults rather than failing.
//
// The casts are guarded on purpose: a scene file is data, and converting a
// number outside the target type's range is undefined behaviour. The
// comparisons also refuse NaN, which no comparison to a range accepts.
bool asBool(const QJsonValue& value, bool fallback) {
    return value.isBool() ? value.toBool() : fallback;
}

float asFloat(const QJsonValue& value, float fallback) {
    return value.isDouble() ? static_cast<float>(value.toDouble()) : fallback;
}

// Wanted for exactly one thing: a reference layer's placement, whose fields are
// doubles. Reading those through asFloat would narrow them, and a placement is
// compared exactly -- see Transform::operator== -- so a number that came back
// slightly different would not be a slightly different picture, it would be a
// cache key that never matches and a layer that re-derives on every refresh.
double asDouble(const QJsonValue& value, double fallback) {
    return value.isDouble() ? value.toDouble() : fallback;
}

int asInt(const QJsonValue& value, int fallback) {
    if (!value.isDouble()) return fallback;
    const double d = value.toDouble();
    if (!(d >= static_cast<double>(std::numeric_limits<int>::min()) &&
          d <= static_cast<double>(std::numeric_limits<int>::max()))) {
        return fallback;
    }
    return static_cast<int>(d);
}

std::uint64_t asId(const QJsonValue& value, std::uint64_t fallback) {
    if (!value.isDouble()) return fallback;
    const double d = value.toDouble();
    if (!(d >= 0.0 && d < 18446744073709551616.0)) return fallback;
    return static_cast<std::uint64_t>(d);
}

std::string asText(const QJsonValue& value, std::string fallback = {}) {
    return value.isString() ? value.toString().toStdString() : std::move(fallback);
}

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

const char* endName(TrackEnd end) {
    switch (end) {
        case TrackEnd::HoldLast: return "hold";
        case TrackEnd::Cycle: return "cycle";
        case TrackEnd::Nothing: break;
    }
    return "nothing";
}

TrackEnd endFromName(const std::string& name) {
    if (name == "hold") return TrackEnd::HoldLast;
    if (name == "cycle") return TrackEnd::Cycle;
    return TrackEnd::Nothing;
}

const char* kindName(LayerKind kind) {
    switch (kind) {
        case LayerKind::Ctg: return "ctg";
        case LayerKind::Reference: return "reference";
        case LayerKind::Raster: break;
    }
    return "raster";
}

LayerKind kindFromName(const std::string& name) {
    if (name == "ctg") return LayerKind::Ctg;
    if (name == "reference") return LayerKind::Reference;
    return LayerKind::Raster;
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

QJsonArray writeColour(const Rgba& colour) {
    return QJsonArray{jsonNumber(colour.r), jsonNumber(colour.g), jsonNumber(colour.b),
                      jsonNumber(colour.a)};
}

Rgba readColour(const QJsonArray& array) {
    Rgba colour;
    colour.r = asFloat(array.at(0), 0.0f);
    colour.g = asFloat(array.at(1), 0.0f);
    colour.b = asFloat(array.at(2), 0.0f);
    colour.a = asFloat(array.at(3), 0.0f);
    return colour;
}

// Where an imported picture sits, which is the one transform in the program
// that outlives the gesture that made it. Everywhere else a transform is
// committed into pixels and forgotten; a reference layer has no pixels of its
// own, so this *is* the picture's position and losing it loses the placing.
//
// All nine fields, and the pivot is not padding. Transform::operator== compares
// every one of them because the derived pixels are keyed on the placement they
// were derived under, and two placements agreeing everywhere but the pivot put
// a rotation somewhere else. Writing eight of them would reopen the project
// with the picture in a different place and nothing on screen to say why.
QJsonObject writeTransform(const Transform& t) {
    QJsonObject out;
    out.insert("dx", jsonNumber(t.dx));
    out.insert("dy", jsonNumber(t.dy));
    out.insert("rotation", jsonNumber(t.rotation));
    out.insert("scale_x", jsonNumber(t.scale_x));
    out.insert("scale_y", jsonNumber(t.scale_y));
    out.insert("flip_x", t.flip_x);
    out.insert("flip_y", t.flip_y);
    out.insert("pivot_x", jsonNumber(t.pivot_x));
    out.insert("pivot_y", jsonNumber(t.pivot_y));
    return out;
}

// The fallbacks are the identity, member by member, so an object with keys
// missing reads as "not placed" rather than as a scale of zero.
Transform readTransform(const QJsonObject& json) {
    Transform t;
    t.dx = asDouble(json.value("dx"), 0.0);
    t.dy = asDouble(json.value("dy"), 0.0);
    t.rotation = asDouble(json.value("rotation"), 0.0);
    t.scale_x = asDouble(json.value("scale_x"), 1.0);
    t.scale_y = asDouble(json.value("scale_y"), 1.0);
    t.flip_x = asBool(json.value("flip_x"), false);
    t.flip_y = asBool(json.value("flip_y"), false);
    t.pivot_x = asDouble(json.value("pivot_x"), 0.0);
    t.pivot_y = asDouble(json.value("pivot_y"), 0.0);
    return t;
}

QJsonObject writeLayer(const Layer& layer) {
    QJsonObject out;
    out.insert("id", jsonNumber(static_cast<double>(layer.id)));
    out.insert("name", QString::fromStdString(layer.name));
    out.insert("kind", QString::fromLatin1(kindName(layer.kind)));
    out.insert("opacity", jsonNumber(layer.opacity));
    out.insert("visible", layer.visible);
    out.insert("locked", layer.locked);
    out.insert("blend", QString::fromLatin1(blendName(layer.blend)));
    if (layer.kind == LayerKind::Ctg) {
        QJsonArray sources;
        for (LayerId source : layer.ctg_sources) {
            sources.append(jsonNumber(static_cast<double>(source)));
        }
        out.insert("ctg_sources", sources);
        // `show_scribbles` is what you are looking at, not what is on the
        // layer. It is saved anyway: reopening a file with the marks showing,
        // because that is how you left it, is the least surprising thing.
        out.insert("show_scribbles", layer.show_scribbles);
        out.insert("ctg_inherit", layer.ctg_inherit);
        out.insert("ctg_direction", QString::fromLatin1(directionName(layer.ctg_direction)));
        out.insert("ctg_follow_motion", layer.ctg_follow_motion);
    }
    if (layer.kind == LayerKind::Reference) {
        QJsonArray files;
        for (const std::string& name : layer.reference_sources) {
            files.append(QString::fromStdString(name));
        }
        out.insert("reference_sources", files);
        // Under the same version gate as the line above and for the same
        // reason: a build that does not know `reference` reads the layer as
        // raster, finds no cels on it, and saves the emptiness back. The
        // picture and where it goes are lost together or not at all.
        out.insert("placement", writeTransform(layer.placement));
    }
    return out;
}

Layer readLayer(const QJsonObject& json) {
    Layer layer;
    layer.id = asId(json.value("id"), kNoId);
    layer.name = asText(json.value("name"));
    layer.kind = kindFromName(asText(json.value("kind"), "raster"));
    layer.opacity = asFloat(json.value("opacity"), 1.0f);
    layer.visible = asBool(json.value("visible"), true);
    layer.locked = asBool(json.value("locked"), false);
    layer.blend = blendFromName(asText(json.value("blend"), "normal"));
    const QJsonArray sources = json.value("ctg_sources").toArray();
    for (const QJsonValue& source : sources) layer.ctg_sources.push_back(asId(source, kNoId));
    layer.show_scribbles = asBool(json.value("show_scribbles"), false);
    // A project written before carrying existed has none of these keys, and
    // the defaults are what it behaved as: it had no choice about direction.
    // Carrying itself defaults on, which does change how such a project reads
    // -- a drawing with no marks of its own now shows an earlier drawing's
    // rather than nothing. That is the feature, and it is reversible from the
    // panel without touching a cel.
    layer.ctg_inherit = asBool(json.value("ctg_inherit"), true);
    layer.ctg_direction = directionFromName(asText(json.value("ctg_direction"), "forward"));
    layer.ctg_follow_motion = asBool(json.value("ctg_follow_motion"), true);
    for (const QJsonValue& file : json.value("reference_sources").toArray()) {
        // Kept even when it is empty, because the position in this list is what
        // Image::source_frames names. Dropping an unreadable entry would shift
        // every frame after it onto the wrong picture, which is worse than one
        // frame that draws nothing.
        layer.reference_sources.push_back(asText(file));
    }
    // A single `reference_source` is what version 2 wrote before a sequence
    // existed, and a still is a sequence of one. Read here rather than migrated,
    // because there is nothing to migrate: the shapes mean the same thing.
    if (layer.reference_sources.empty()) {
        const std::string one = asText(json.value("reference_source"));
        if (!one.empty()) layer.reference_sources.push_back(one);
    }
    layer.placement = readTransform(json.value("placement").toObject());
    return layer;
}

QJsonObject writeImage(const Image& image, const std::vector<Layer>& layers) {
    QJsonObject out;
    out.insert("id", jsonNumber(static_cast<double>(image.id)));
    out.insert("number", jsonNumber(image.number));
    if (image.marker) out.insert("marker", writeColour(*image.marker));

    // Walked in layer order rather than in the hash's order, so the file is the
    // same twice running and a diff shows what changed rather than where the
    // hash moved.
    QJsonArray cels;
    for (const Layer& layer : layers) {
        const CelId cel = image.celFor(layer.id);
        if (cel == kNoId) continue;  // sparse: absent means the layer is empty
        QJsonObject entry;
        entry.insert("layer", jsonNumber(static_cast<double>(layer.id)));
        entry.insert("cel", jsonNumber(static_cast<double>(cel)));
        cels.append(entry);
    }
    out.insert("cels", cels);

    // And which frame of its file a reference layer shows here, which is the
    // same sparse shape and written the same way: layer order, absent means the
    // layer is empty at this drawing. Only written when there is one, so a
    // project with no imports in it looks exactly as it did.
    QJsonArray sources;
    for (const Layer& layer : layers) {
        const int frame = image.sourceFrameFor(layer.id);
        if (frame == Image::kNoSourceFrame) continue;
        QJsonObject entry;
        entry.insert("layer", jsonNumber(static_cast<double>(layer.id)));
        entry.insert("frame", jsonNumber(frame));
        sources.append(entry);
    }
    if (!sources.isEmpty()) out.insert("source_frames", sources);
    return out;
}

Image readImage(const QJsonObject& json) {
    Image image;
    image.id = asId(json.value("id"), kNoId);
    image.number = asInt(json.value("number"), 0);
    if (json.contains("marker") && !json.value("marker").isNull()) {
        image.marker = readColour(json.value("marker").toArray());
    }
    const QJsonArray cels = json.value("cels").toArray();
    for (const QJsonValue& entry : cels) {
        const LayerId layer = asId(entry.toObject().value("layer"), kNoId);
        const CelId cel = asId(entry.toObject().value("cel"), kNoId);
        if (layer != kNoId && cel != kNoId) image.cels[layer] = cel;
    }
    const QJsonArray sources = json.value("source_frames").toArray();
    for (const QJsonValue& entry : sources) {
        const LayerId layer = asId(entry.toObject().value("layer"), kNoId);
        const int frame = asInt(entry.toObject().value("frame"), Image::kNoSourceFrame);
        // A negative index is what "no entry" is written as, so a file saying
        // one is saying nothing and is treated as such rather than reaching
        // backwards off the front of the source list.
        if (layer != kNoId && frame >= 0) image.source_frames[layer] = frame;
    }
    return image;
}

QJsonObject writeTrack(const Track& track) {
    QJsonObject out;
    out.insert("id", jsonNumber(static_cast<double>(track.id)));
    out.insert("name", QString::fromStdString(track.name));
    out.insert("opacity", jsonNumber(track.opacity));
    out.insert("blend", QString::fromLatin1(blendName(track.blend)));
    out.insert("time_offset", jsonNumber(track.time_offset));
    out.insert("overwrite_drawings", track.overwrite_drawings);
    out.insert("end", QString::fromLatin1(endName(track.end)));
    // No `next_drawing_number`: what a new drawing is called is the lowest
    // number the track is not using, worked out from the drawings themselves.
    // A file from before this carries the key and it is simply ignored.

    QJsonArray layers;
    for (const Layer& layer : track.layers) layers.append(writeLayer(layer));
    out.insert("layers", layers);

    // The slots are the exposure and repeat freely; that is the point of them.
    QJsonArray slots;
    for (ImageId id : track.slots) slots.append(jsonNumber(static_cast<double>(id)));
    out.insert("slots", slots);

    // `images` is a hash, so it is written in the order the slots first mention
    // each drawing. Same reason as the cels above: a stable file.
    QJsonArray images;
    std::unordered_set<ImageId> written;
    const auto writeOne = [&](ImageId id) {
        if (id == kNoId || !written.insert(id).second) return;
        const Image* image = track.findImage(id);
        if (image) images.append(writeImage(*image, track.layers));
    };
    for (ImageId id : track.slots) writeOne(id);
    // An image no slot mentions cannot happen today, but losing one silently
    // would be the kind of bug that only shows up in somebody's finished shot.
    for (const auto& [id, image] : track.images) writeOne(id);
    out.insert("images", images);
    return out;
}

Track readTrack(const QJsonObject& json) {
    Track track;
    track.id = asId(json.value("id"), kNoId);
    track.name = asText(json.value("name"));
    track.opacity = asFloat(json.value("opacity"), 1.0f);
    track.blend = blendFromName(asText(json.value("blend"), "normal"));
    track.time_offset = asInt(json.value("time_offset"), 0);
    // The track default, not the behaviour of the build that wrote the file.
    // A project from before the setting existed did not overwrite, so this does
    // change what such a file does -- deliberately, because a default that
    // depended on how old the file was would be an invisible difference between
    // two tracks that look identical. Every file this build writes carries the
    // key, so it only touches projects saved before the setting existed.
    // No version bump: an older build reading a newer file ignores the key.
    track.overwrite_drawings = asBool(json.value("overwrite_drawings"), true);
    // "nothing" for a file from before the setting, which is what it did.
    // Unlike overwrite, this default *is* the old behaviour, so nothing changes
    // under an old project.
    track.end = endFromName(asText(json.value("end"), "nothing"));

    const QJsonArray layers = json.value("layers").toArray();
    for (const QJsonValue& value : layers) track.layers.push_back(readLayer(value.toObject()));

    const QJsonArray images = json.value("images").toArray();
    for (const QJsonValue& value : images) {
        Image image = readImage(value.toObject());
        if (image.id != kNoId) track.images[image.id] = std::move(image);
    }

    const QJsonArray slots = json.value("slots").toArray();
    for (const QJsonValue& value : slots) {
        const ImageId id = asId(value, kNoId);
        // A slot naming an image that is not in the file would be a hole in the
        // timeline that every walk over `slots` would then have to guard.
        if (id != kNoId && track.images.count(id)) track.slots.push_back(id);
    }
    return track;
}

// ---------------------------------------------------------------------------
// the project folder: compression, layout, and the swap that makes a save
// atomic
// ---------------------------------------------------------------------------

constexpr char kCelFileMagic[8] = {'A', 'N', 'I', 'M', 'C', 'E', 'L', 'Z'};
constexpr int kCompression = 6;  // zlib's default; 9 costs much more for little

QString celFileName(CelId id) {
    // Zero-padded so a directory listing is in cel order, which is roughly the
    // order the drawings were made in.
    return QStringLiteral("cel-%1.acel").arg(id, 6, 10, QLatin1Char('0'));
}

bool writeFile(const QString& path, const QByteArray& bytes, QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool readFile(const QString& path, QByteArray& bytes, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return false;
    }
    bytes = file.readAll();
    return true;
}

QByteArray packCel(const TileGrid& tiles) {
    const std::vector<std::uint8_t> raw = ProjectIO::encodeCel(tiles);
    const QByteArray body(reinterpret_cast<const char*>(raw.data()),
                          static_cast<qsizetype>(raw.size()));
    QByteArray out(kCelFileMagic, sizeof kCelFileMagic);
    out += qCompress(body, kCompression);
    return out;
}

bool unpackCel(const QByteArray& bytes, TileGrid& out, QString* error) {
    if (bytes.size() < static_cast<qsizetype>(sizeof kCelFileMagic) ||
        std::memcmp(bytes.constData(), kCelFileMagic, sizeof kCelFileMagic) != 0) {
        if (error) *error = QStringLiteral("not a cel file");
        return false;
    }
    const QByteArray body = qUncompress(bytes.mid(sizeof kCelFileMagic));
    if (body.isEmpty()) {
        if (error) *error = QStringLiteral("cel data is corrupt or truncated");
        return false;
    }

    const auto* begin = reinterpret_cast<const std::uint8_t*>(body.constData());
    const std::vector<std::uint8_t> raw(begin, begin + body.size());
    std::string why;
    if (!ProjectIO::decodeCel(raw, out, &why)) {
        if (error) *error = QString::fromStdString(why);
        return false;
    }
    return true;
}

// Everything is written here and moved into place at the end, so a save that
// dies half way through -- out of disk, killed, unplugged -- cannot leave a
// project that is neither the old one nor the new one.
QString scratchFolderFor(const QString& folder) {
    return folder + QStringLiteral(".saving-%1")
                        .arg(QCoreApplication::applicationPid(), 0, 16);
}

bool removeTree(const QString& path) {
    QDir dir(path);
    return !dir.exists() || dir.removeRecursively();
}

std::filesystem::path nativePath(const QString& path) {
    return std::filesystem::path(path.toStdU16String());
}

// Puts a cel that has not changed into the folder being built, as a second name
// for the file already on disk rather than as new bytes. The build-alongside-
// and-swap that protects an interrupted save wants a complete folder before it
// swaps, and a link is how one is assembled without paying for what did not
// move: no bytes are read or written, only a directory entry.
//
// Nothing ever writes through the link -- cel files are replaced with QSaveFile,
// which renames a new file over the name -- so the two folders sharing a file
// cannot surprise either of them, and the old folder is removed after the swap
// anyway.
//
// A filesystem that will not link gets a copy, and a file that is missing gets
// `false` so the caller encodes it in full. A save can therefore be slower than
// it needed to be; it cannot be wrong.
bool carryForward(const QString& from, const QString& to) {
    if (!QFileInfo::exists(from)) return false;
    std::error_code ec;
    std::filesystem::create_hard_link(nativePath(from), nativePath(to), ec);
    if (!ec) return true;
    return QFile::copy(from, to);
}

}  // namespace

QString ProjectIO::folderSuffix() { return QStringLiteral(".animage"); }

bool ProjectIO::swapIntoPlace(const QString& scratch, const QString& folder,
                              const RenameFn& rename, QString* error) {
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };

    // The old project is moved aside rather than deleted first, so that a
    // failure to rename the new one into place still leaves something.
    const QString displaced = folder + QStringLiteral(".replaced-%1")
                                           .arg(QDateTime::currentMSecsSinceEpoch());
    const bool had_one = QFileInfo::exists(folder);
    if (had_one && !rename(folder, displaced)) {
        // Nothing moved. The project on disk is the one that was already there
        // and this save is the only thing that has been lost.
        removeTree(scratch);
        return fail(QStringLiteral("cannot move the previous project aside"));
    }

    if (rename(scratch, folder)) {
        if (had_one) removeTree(displaced);
        return true;
    }

    // `folder` is empty and the new project is still in `scratch`. Put the old
    // one back, and *check that it went back* -- this is the line the whole
    // function is here for. It used to be attempted and its answer discarded.
    if (had_one && rename(displaced, folder)) {
        removeTree(scratch);
        return fail(QStringLiteral("cannot move the new project into place"));
    }

    // Nothing is at `folder` and nothing can be put there. Both copies are
    // kept: the one just written is the only copy of the work being saved, and
    // deleting it to tidy up would be the failure doing more damage than the
    // fault that caused it.
    //
    // Moved out of the scratch name first, because the *next* save's first act
    // is to clear that path -- it is named after the process and so is the same
    // path every time -- and a rescue copy left there would be gone two minutes
    // later without a word. If even that rename is refused there is nothing
    // further to try, and the message names where it actually is.
    QString kept =
        folder + QStringLiteral(".rescued-%1").arg(QDateTime::currentMSecsSinceEpoch());
    if (!rename(scratch, kept)) kept = scratch;

    // Naming the paths is the whole point of the message. Without them this is
    // a project that has vanished from where it lives; with them it is two
    // folders and a rename.
    if (had_one) {
        return fail(QStringLiteral("cannot move the new project into place, and the previous "
                                   "project could not be put back. Nothing has been deleted: "
                                   "this save is in \"%1\" and the previous project is in "
                                   "\"%2\". Rename either one to \"%3\".")
                        .arg(kept, displaced, folder));
    }
    return fail(QStringLiteral("cannot move the new project into place. Nothing has been "
                               "deleted: this save is in \"%1\". Rename it to \"%2\".")
                    .arg(kept, folder));
}

bool ProjectIO::save(const Document& doc, const QString& folder, QString* error) {
    // A full save is the incremental one with nothing to carry forward, which
    // keeps one code path rather than two that must agree about the layout.
    SaveState nothing;
    return save(doc, folder, nothing, error);
}

bool ProjectIO::save(const Document& doc, const QString& folder, SaveState& state, QString* error,
                     const Imports& imports) {
    const QString scratch = scratchFolderFor(folder);
    if (!removeTree(scratch)) {
        if (error) *error = QStringLiteral("cannot clear %1").arg(scratch);
        return false;
    }

    QDir root;
    if (!root.mkpath(scratch + QStringLiteral("/cels"))) {
        if (error) *error = QStringLiteral("cannot create %1").arg(scratch);
        return false;
    }

    const auto giveUp = [&](const QString& why) {
        if (error) *error = why;
        removeTree(scratch);
        return false;
    };

    // Only a state describing this same folder says anything about the files
    // in it. Saving somewhere else -- Save As -- has nothing to carry forward
    // and writes a project that stands on its own.
    const bool carrying = !state.folder.isEmpty() && state.folder == folder;

    SaveState next;
    next.folder = folder;

    // The pixels first. If one of them fails there is no half-written
    // scene.json pointing at a cel that does not exist.
    for (CelId id : celsReferencedBy(doc)) {
        const Cel* cel = doc.cel(id);
        // A referenced cel with nothing in it is normal -- a layer touched and
        // then erased -- and still gets a file, so the manifest and the folder
        // agree about what exists.
        const std::uint64_t revision = cel ? cel->revision() : 0;
        const QString name = celFileName(id);
        next.revisions.emplace(id, revision);

        if (carrying) {
            const auto seen = state.revisions.find(id);
            if (seen != state.revisions.end() && seen->second == revision &&
                carryForward(folder + QStringLiteral("/cels/") + name,
                             scratch + QStringLiteral("/cels/") + name)) {
                continue;
            }
        }

        const TileGrid empty;
        const QByteArray packed = packCel(cel ? cel->tiles() : empty);
        QString why;
        if (!writeFile(scratch + QStringLiteral("/cels/") + name, packed, &why)) {
            return giveUp(why);
        }
    }

    // The imported files, which the swap would otherwise delete. Nothing in the
    // document can rebuild these -- a reference layer holds a name, not pixels
    // -- so a folder assembled without them is a folder that has lost the
    // picture. See ProjectIO::Imports for where they are looked for and why in
    // that order.
    //
    // Written once and run twice, for `imports/` and for `audio/`. The second
    // caller is what makes this a function rather than a loop; the two folders
    // differ in nothing but their name and which pending map answers for them.
    const auto carryFolder =
        [&](const char* subdir, const std::vector<std::string>& names,
            const std::unordered_map<std::string, QString>& pending) -> std::optional<QString> {
        for (const std::string& name : names) {
            const QString file = QString::fromStdString(name);
            const QString dir = QLatin1String(subdir);
            if (!root.mkpath(scratch + QLatin1Char('/') + dir)) {
                return QStringLiteral("cannot create %1/%2").arg(scratch, dir);
            }
            const QString into = scratch + QLatin1Char('/') + dir + QLatin1Char('/') + file;

            QStringList tried;
            const auto attempt = [&](const QString& from) {
                if (from.isEmpty()) return false;
                tried.append(from);
                return carryForward(from, into);
            };

            const auto found = pending.find(name);
            if (attempt(state.folder.isEmpty()
                            ? QString()
                            : state.folder + QLatin1Char('/') + dir + QLatin1Char('/') + file)) {
                continue;
            }
            if (found != pending.end() && attempt(found->second)) continue;
            if (attempt(folder + QLatin1Char('/') + dir + QLatin1Char('/') + file)) continue;

            // Said rather than skipped, and said now: the original is still
            // wherever it was imported from, and a save that quietly dropped it
            // would be discovered when the project was next opened somewhere
            // else.
            return QStringLiteral("cannot find the imported file \"%1\". Looked in: %2")
                .arg(file, tried.isEmpty() ? QStringLiteral("nowhere it could be")
                                           : tried.join(QStringLiteral(", ")));
        }
        return std::nullopt;
    };

    if (const auto why = carryFolder("imports", importsReferencedBy(doc), imports.pending)) {
        return giveUp(*why);
    }
    if (const auto why = carryFolder("audio", audioReferencedBy(doc), imports.pending_audio)) {
        return giveUp(*why);
    }

    const std::string text = writeSceneJson(doc);
    QString why;
    if (!writeFile(scratch + QStringLiteral("/scene.json"),
                   QByteArray(text.data(), static_cast<qsizetype>(text.size())), &why)) {
        return giveUp(why);
    }

    // The swap, which cleans up after itself on every path -- including the
    // paths where cleaning up means *not* deleting something. So no `giveUp`
    // here: it would remove the copy the swap has deliberately kept.
    QString swap_error;
    if (!swapIntoPlace(scratch, folder,
                       [](const QString& from, const QString& to) {
                           return QDir().rename(from, to);
                       },
                       &swap_error)) {
        if (error) *error = swap_error;
        return false;
    }

    // Only now, with the folder in place, does what was written become what is
    // on disk. A save that gave up above leaves the caller's state describing
    // the project that is still there.
    state = std::move(next);
    return true;
}

bool ProjectIO::load(Document& doc, const QString& folder, SaveState& state, QString* error,
                     Damage* damage) {
    if (!load(doc, folder, error, damage)) return false;

    // Read after the load rather than during it: setCelTiles installs a fresh
    // Cel, so a revision taken while reading would be replaced by the one the
    // document ends up holding. Every cel here came from its file and has not
    // been touched since, so the folder is current for all of them.
    SaveState fresh;
    fresh.folder = folder;
    for (CelId id : celsReferencedBy(doc)) {
        const Cel* cel = doc.cel(id);
        fresh.revisions.emplace(id, cel ? cel->revision() : 0);
    }
    state = std::move(fresh);
    return true;
}

bool ProjectIO::load(Document& doc, const QString& folder, QString* error, Damage* damage) {
    QByteArray text;
    if (!readFile(folder + QStringLiteral("/scene.json"), text, error)) return false;

    // Loaded into a document of its own first. Only once every cel has been
    // read does the open document get replaced, so a project with one bad cel
    // in it cannot leave you with half of it and none of what you had.
    Document loaded;
    std::string why;
    if (!readSceneJson(std::string_view(text.constData(), static_cast<std::size_t>(text.size())),
                       loaded, &why)) {
        if (error) *error = QString::fromStdString(why);
        return false;
    }

    for (CelId id : celsReferencedBy(loaded)) {
        const QString name = celFileName(id);
        const QString path = folder + QStringLiteral("/cels/") + name;

        // One drawing that cannot be read. With somewhere to report it, it
        // costs that drawing: the cel is simply never set, which leaves it
        // absent -- the same as a layer that was never drawn on, and what the
        // rest of the program already handles everywhere. With nowhere to
        // report it, it costs the project, exactly as it always did.
        const auto lose = [&](const QString& reason) {
            if (!damage) {
                if (error) *error = QStringLiteral("%1: %2").arg(name, reason);
                return false;
            }
            damage->lost.push_back({id, name, reason});
            return true;
        };

        QByteArray bytes;
        // Not named `why`, and neither is `lose`'s argument above: the scene
        // reader has one of those and it is still in scope here. MSVC makes
        // hiding a local an error (C4456) where GCC's -Wall says nothing, so
        // this is only visible on CI unless you ask for -Wshadow=local.
        QString cel_error;
        // Missing counts the same as unreadable. A project is a folder, so a
        // sync that brought back all of it but one file is at least as likely
        // as a file that arrived damaged.
        if (!readFile(path, bytes, &cel_error)) {
            if (!lose(cel_error)) return false;
            continue;
        }

        TileGrid tiles;
        if (!unpackCel(bytes, tiles, &cel_error)) {
            if (!lose(cel_error)) return false;
            continue;
        }

        // Not damage, and never survivable: the file is fine and the scene
        // disagrees with it about what is in this project. That is a broken
        // file rather than a lost drawing, and reading on would be guessing.
        if (!loaded.setCelTiles(id, std::move(tiles))) {
            if (error) *error = QStringLiteral("%1 is not part of this scene").arg(name);
            return false;
        }
    }

    doc = std::move(loaded);
    return true;
}

std::vector<std::uint8_t> ProjectIO::encodeCel(const TileGrid& tiles) {
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
    out.insert(out.end(), std::begin(kCelMagic), std::end(kCelMagic));
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

bool ProjectIO::readCelFileInfo(const std::vector<std::uint8_t>& bytes, CelFileInfo& out,
                                std::string* error) {
    if (bytes.size() < kHeaderSize) return fail(error, "cel file is too short to hold a header");
    if (std::memcmp(bytes.data(), kCelMagic, sizeof kCelMagic) != 0) {
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
    if (count > kMaxCelTiles) {
        return fail(error, "cel file has " + std::to_string(count) +
                               " tiles, too many: the most one cel can hold is " +
                               std::to_string(kMaxCelTiles));
    }

    out.tile_size = static_cast<int>(tile_size);
    out.channels = static_cast<int>(channels);
    out.tile_count = count;
    return true;
}

bool ProjectIO::decodeCel(const std::vector<std::uint8_t>& bytes, TileGrid& out,
                          std::string* error) {
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

        // The row table. floor_size counted one of these per tile -- but it
        // counted only the fixed parts, and `at` has also advanced by every
        // tile's pixels, which it did not count. So that guarantee held for the
        // first tile and drifted further past the end with every tile after it.
        // A file cut to exactly floor_size passed the check and was then read
        // beyond, and what came back was a drawing made partly of whatever was
        // next in memory -- reported as a successful load.
        if (at > bytes.size() || bytes.size() - at < kRowTableBytes) {
            return fail(error, "cel file is truncated before the row table of tile " +
                                   std::to_string(i));
        }

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
        // The `at >` half is not redundant with the check above: these are
        // unsigned, so if `at` is ever past the end the subtraction wraps to
        // about 1.8e19 and this guard passes whatever it is asked -- which is
        // what turned a few bytes of over-read into up to 128 KB per tile.
        if (at > bytes.size() || bytes.size() - at < pixel_bytes) {
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

std::string ProjectIO::writeSceneJson(const Document& doc) {
    const Scene& scene = doc.scene();

    QJsonObject canvas;
    canvas.insert("width", jsonNumber(scene.width));
    canvas.insert("height", jsonNumber(scene.height));

    QJsonObject out;
    out.insert("format", QString::fromLatin1(kFormatName));
    out.insert("version", jsonNumber(kSceneFormatVersion));
    out.insert("framerate", jsonNumber(scene.framerate));
    // Both, because "derived" and "sixty" are different answers: a file that
    // stored only the number could not say which one it meant. A file from
    // before the setting has neither and gets derived, which is what it did.
    out.insert("fixed_length", scene.fixed_length);
    out.insert("length", jsonNumber(scene.length));
    out.insert("canvas", canvas);

    QJsonArray tracks;
    for (const Track& track : scene.tracks) tracks.append(writeTrack(track));
    out.insert("tracks", tracks);

    // Only when there are any. A project with no sound is the same bytes it
    // always was, which is what keeps a save comparable across the version
    // bump -- and what stops every existing project's file changing the first
    // time it is opened by this build.
    if (!scene.audio_tracks.empty()) {
        QJsonArray sounds;
        for (const AudioTrack& track : scene.audio_tracks) {
            QJsonObject one;
            one.insert("id", jsonNumber(static_cast<qint64>(track.id)));
            one.insert("name", QString::fromStdString(track.name));
            one.insert("source", QString::fromStdString(track.source));
            one.insert("offset_frames", jsonNumber(track.offset_frames));
            one.insert("gain", track.gain);
            sounds.append(one);
        }
        out.insert("audio_tracks", sounds);
    }

    const QByteArray text = QJsonDocument(out).toJson(QJsonDocument::Indented);
    return std::string(text.constData(), static_cast<std::size_t>(text.size()));
}

bool ProjectIO::readSceneJson(std::string_view text, Document& doc, std::string* error) {
    const auto refuse = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };

    QJsonParseError parse_error;
    const QJsonDocument root = QJsonDocument::fromJson(
        QByteArray(text.data(), static_cast<qsizetype>(text.size())), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        return refuse("not JSON: " + parse_error.errorString().toStdString());
    }
    if (!root.isObject()) return refuse("not a scene: the file is not an object");
    const QJsonObject object = root.object();

    if (object.value("format").toString() != QString::fromLatin1(kFormatName)) {
        return refuse("not an Animage scene: \"format\" is not \"" + std::string(kFormatName) +
                      "\"");
    }
    const int version = asInt(object.value("version"), 0);
    if (version <= 0 || version > kSceneFormatVersion) {
        return refuse("scene version " + std::to_string(version) + " is newer than this build, " +
                      "which understands up to " + std::to_string(kSceneFormatVersion));
    }

    Scene scene;
    scene.framerate = std::max(1, asInt(object.value("framerate"), 24));
    scene.fixed_length = asBool(object.value("fixed_length"), false);
    scene.length = std::max(0, asInt(object.value("length"), 100));
    scene.width = std::clamp(asInt(object.value("canvas").toObject().value("width"), 1920),
                             kMinCanvasSide, kMaxCanvasSide);
    scene.height = std::clamp(asInt(object.value("canvas").toObject().value("height"), 1080),
                              kMinCanvasSide, kMaxCanvasSide);

    const QJsonArray tracks = object.value("tracks").toArray();
    for (const QJsonValue& value : tracks) {
        Track track = readTrack(value.toObject());
        if (track.id != kNoId) scene.tracks.push_back(std::move(track));
    }
    if (scene.tracks.empty()) return refuse("scene has no tracks");

    // Soundtracks, which a project may simply not have -- an absent key is not
    // damage and reads as no sound. A track with no id or no source is dropped
    // rather than refused: it can name no file and so can make no noise, and
    // refusing the whole project over one would be losing the drawings to save
    // nothing.
    for (const QJsonValue& value : object.value("audio_tracks").toArray()) {
        const QJsonObject one = value.toObject();
        AudioTrack track;
        track.id = static_cast<TrackId>(asInt(one.value("id"), 0));
        track.name = one.value("name").toString().toStdString();
        track.source = one.value("source").toString().toStdString();
        track.offset_frames = asInt(one.value("offset_frames"), 0);
        track.gain = std::clamp(one.value("gain").toDouble(1.0), 0.0, 1.0);
        if (track.id == kNoId || track.source.empty()) continue;
        scene.audio_tracks.push_back(std::move(track));
    }

    // Nothing is written into `doc` before this point, so a file that fails any
    // check above leaves whatever was open alone.
    doc.loadScene(std::move(scene));
    return true;
}

std::vector<std::string> ProjectIO::importsReferencedBy(const Document& doc) {
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    for (const Track& track : doc.scene().tracks) {
        for (const Layer& layer : track.layers) {
            if (layer.kind != LayerKind::Reference) continue;
            // A reference layer with no source is not an error to a save: it
            // draws nothing, and there is no file to carry. It is what a layer
            // looks like between being created and being pointed at a file.
            //
            // De-duplicated across the whole list and not only across layers,
            // because a sequence is allowed to name one file twice -- a hold
            // that was flattened into the files somebody handed over -- and
            // that is one file to copy, not two.
            for (const std::string& name : layer.reference_sources) {
                if (name.empty()) continue;
                if (seen.insert(name).second) names.push_back(name);
            }
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> ProjectIO::audioReferencedBy(const Document& doc) {
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    for (const AudioTrack& track : doc.scene().audio_tracks) {
        // De-duplicated for importsReferencedBy's reason: two soundtracks are
        // allowed to name one file, and that is one file to copy.
        if (track.source.empty()) continue;
        if (seen.insert(track.source).second) names.push_back(track.source);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<CelId> ProjectIO::celsReferencedBy(const Document& doc) {
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
