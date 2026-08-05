// SPDX-License-Identifier: GPL-3.0-or-later
#include "project_files.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <filesystem>
#include <system_error>

#include "celfile.h"
#include "serialise.h"

using namespace animage;

namespace project {
namespace {

constexpr char kCelMagic[8] = {'A', 'N', 'I', 'M', 'C', 'E', 'L', 'Z'};
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
    const std::vector<std::uint8_t> raw = encodeCel(tiles);
    const QByteArray body(reinterpret_cast<const char*>(raw.data()),
                          static_cast<qsizetype>(raw.size()));
    QByteArray out(kCelMagic, sizeof kCelMagic);
    out += qCompress(body, kCompression);
    return out;
}

bool unpackCel(const QByteArray& bytes, TileGrid& out, QString* error) {
    if (bytes.size() < static_cast<qsizetype>(sizeof kCelMagic) ||
        std::memcmp(bytes.constData(), kCelMagic, sizeof kCelMagic) != 0) {
        if (error) *error = QStringLiteral("not a cel file");
        return false;
    }
    const QByteArray body = qUncompress(bytes.mid(sizeof kCelMagic));
    if (body.isEmpty()) {
        if (error) *error = QStringLiteral("cel data is corrupt or truncated");
        return false;
    }

    const auto* begin = reinterpret_cast<const std::uint8_t*>(body.constData());
    const std::vector<std::uint8_t> raw(begin, begin + body.size());
    std::string why;
    if (!decodeCel(raw, out, &why)) {
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

QString folderSuffix() { return QStringLiteral(".animage"); }

bool save(const Document& doc, const QString& folder, QString* error) {
    // A full save is the incremental one with nothing to carry forward, which
    // keeps one code path rather than two that must agree about the layout.
    SaveState nothing;
    return save(doc, folder, nothing, error);
}

bool save(const Document& doc, const QString& folder, SaveState& state, QString* error) {
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

bool load(Document& doc, const QString& folder, SaveState& state, QString* error) {
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

bool load(Document& doc, const QString& folder, QString* error) {
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

}  // namespace project
