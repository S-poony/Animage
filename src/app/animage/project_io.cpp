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

bool ProjectIO::save(const Document& doc, const QString& folder, QString* error) {
    // A full save is the incremental one with nothing to carry forward, which
    // keeps one code path rather than two that must agree about the layout.
    SaveState nothing;
    return save(doc, folder, nothing, error);
}

bool ProjectIO::save(const Document& doc, const QString& folder, SaveState& state,
                     QString* error) {
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

    const std::string text = writeSceneJson(doc);
    QString why;
    if (!writeFile(scratch + QStringLiteral("/scene.json"),
                   QByteArray(text.data(), static_cast<qsizetype>(text.size())), &why)) {
        return giveUp(why);
    }

    // The swap. The old project is moved aside rather than deleted first, so
    // that a failure to rename the new one into place still leaves something.
    const QString displaced = folder + QStringLiteral(".replaced-%1")
                                           .arg(QDateTime::currentMSecsSinceEpoch());
    const bool had_one = QFileInfo::exists(folder);
    if (had_one && !root.rename(folder, displaced)) {
        return giveUp(QStringLiteral("cannot move the previous project aside"));
    }
    if (!root.rename(scratch, folder)) {
        if (had_one) root.rename(displaced, folder);  // put it back
        return giveUp(QStringLiteral("cannot move the new project into place"));
    }
    if (had_one) removeTree(displaced);

    // Only now, with the folder in place, does what was written become what is
    // on disk. A save that gave up above leaves the caller's state describing
    // the project that is still there.
    state = std::move(next);
    return true;
}

bool ProjectIO::load(Document& doc, const QString& folder, SaveState& state, QString* error) {
    if (!load(doc, folder, error)) return false;

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

bool ProjectIO::load(Document& doc, const QString& folder, QString* error) {
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
        const QString path = folder + QStringLiteral("/cels/") + celFileName(id);
        QByteArray bytes;
        if (!readFile(path, bytes, error)) return false;

        TileGrid tiles;
        QString cel_error;
        if (!unpackCel(bytes, tiles, &cel_error)) {
            if (error) *error = QStringLiteral("%1: %2").arg(celFileName(id), cel_error);
            return false;
        }
        if (!loaded.setCelTiles(id, std::move(tiles))) {
            if (error) {
                *error = QStringLiteral("%1 is not part of this scene").arg(celFileName(id));
            }
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

    // Nothing is written into `doc` before this point, so a file that fails any
    // check above leaves whatever was open alone.
    doc.loadScene(std::move(scene));
    return true;
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
