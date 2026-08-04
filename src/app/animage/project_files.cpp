// SPDX-License-Identifier: GPL-3.0-or-later
#include "project_files.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

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

}  // namespace

QString folderSuffix() { return QStringLiteral(".animage"); }

bool save(const Document& doc, const QString& folder, QString* error) {
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

    // The pixels first. If one of them fails there is no half-written
    // scene.json pointing at a cel that does not exist.
    for (CelId id : celsReferencedBy(doc)) {
        const Cel* cel = doc.cel(id);
        // A referenced cel with nothing in it is normal -- a layer touched and
        // then erased -- and still gets a file, so the manifest and the folder
        // agree about what exists.
        const TileGrid empty;
        const QByteArray packed = packCel(cel ? cel->tiles() : empty);
        QString why;
        if (!writeFile(scratch + QStringLiteral("/cels/") + celFileName(id), packed, &why)) {
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
