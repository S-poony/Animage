// SPDX-License-Identifier: GPL-3.0-or-later
//
// Not a test -- a stopwatch. Saving rewrites every cel, and whether that is
// fine or ruinous depends on numbers nobody has taken yet. Autosave is the
// thing that will care.

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <cstdio>

#include "brush.h"
#include "document.h"
#include "project_io.h"

using namespace animage;

namespace {

// A shot: `drawings` drawings, each with line art and a colour layer, each
// drawn over most of a 1920x1080 canvas the way a full-frame drawing is.
Document buildShot(int drawings) {
    Document doc;
    const TrackId track = doc.addTrack("main");
    const LayerId ink = doc.addLayer(track, "ink");
    const LayerId colour = doc.addLayer(track, "colour", 1, LayerKind::Ctg);
    doc.setCanvasSize(1920, 1080);

    const auto stroke = [&](ImageId image, LayerId layer, float x0, float y0, float x1, float y1,
                            float radius, float r, float g, float b) {
        ScopedCommand command(doc, "Stroke");
        BrushSettings s;
        s.radius = radius;
        s.pressure_affects_opacity = false;
        s.r = r; s.g = g; s.b = b; s.a = 1.0f;
        Brush brush(s);
        brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
        brush.extend({x1, y1, 1.0f});
        brush.end();
    };

    for (int d = 0; d < drawings; ++d) {
        const ImageId image = doc.insertImage(track, static_cast<std::size_t>(d));
        const float drift = static_cast<float>(d) * 3.0f;
        // A rough outline across the frame, then a scribble inside it. Enough
        // strokes to touch most of the tiles a real drawing would.
        for (int i = 0; i < 14; ++i) {
            const float y = 80.0f + static_cast<float>(i) * 70.0f;
            stroke(image, ink, 100.0f + drift, y, 1800.0f - drift, y + 40.0f, 3.0f, 0, 0, 0);
        }
        for (int i = 0; i < 4; ++i) {
            const float y = 300.0f + static_cast<float>(i) * 120.0f;
            stroke(image, colour, 400.0f, y, 1500.0f, y, 20.0f, 0.9f, 0.4f, 0.1f);
        }
    }
    return doc;
}

// One dab on one drawing: what an animator does between two autosaves, and the
// case the incremental save exists for.
void touchOneCel(Document& doc) {
    const Track& track = doc.scene().tracks.front();
    const TrackId track_id = track.id;
    const LayerId layer = track.layers.front().id;
    const ImageId image = track.imageAtSlot(0);

    ScopedCommand command(doc, "Stroke");
    BrushSettings s;
    s.radius = 4.0f;
    s.pressure_affects_opacity = false;
    s.r = 0; s.g = 0; s.b = 0; s.a = 1.0f;
    Brush brush(s);
    brush.begin(doc, track_id, image, layer, {200.0f, 200.0f, 1.0f});
    brush.extend({260.0f, 240.0f, 1.0f});
    brush.end();
}

long long folderBytes(const QString& path) {
    long long total = 0;
    QDir dir(path);
    for (const QFileInfo& entry :
         dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        total += entry.isDir() ? folderBytes(entry.filePath()) : entry.size();
    }
    return total;
}

void time(int drawings) {
    Document doc = buildShot(drawings);
    const std::size_t cels = ProjectIO::celsReferencedBy(doc).size();
    const std::size_t tiles = doc.totalTileCount();

    QTemporaryDir scratch;
    if (!scratch.isValid()) return;
    const QString folder = scratch.filePath(QStringLiteral("shot.animage"));

    QElapsedTimer clock;
    clock.start();
    const bool ok = ProjectIO::save(doc, folder, nullptr);
    const qint64 first = clock.elapsed();
    if (!ok) {
        std::printf("  %3d drawings: save failed\n", drawings);
        return;
    }

    // Again, with nothing changed and nothing remembered: the whole project
    // re-encoded, which is what a save cost before it learned which cels moved.
    clock.restart();
    ProjectIO::save(doc, folder, nullptr);
    const qint64 again = clock.elapsed();

    // And again knowing what is already there. The first of these is what an
    // autosave costs when the animator has paused, the second what it costs
    // mid-drawing -- one cel touched out of all of them, which is the shape of
    // nearly every autosave there will ever be.
    ProjectIO::SaveState state;
    ProjectIO::save(doc, folder, state, nullptr);
    clock.restart();
    ProjectIO::save(doc, folder, state, nullptr);
    const qint64 untouched = clock.elapsed();

    touchOneCel(doc);
    clock.restart();
    ProjectIO::save(doc, folder, state, nullptr);
    const qint64 one_moved = clock.elapsed();

    clock.restart();
    Document back;
    ProjectIO::load(back, folder, nullptr);
    const qint64 opened = clock.elapsed();

    const double megabytes = static_cast<double>(folderBytes(folder)) / (1024.0 * 1024.0);
    const double raw = static_cast<double>(tiles) * 128.0 * 128.0 * 4.0 * 2.0 / (1024.0 * 1024.0);
    std::printf("  %3d drawings %4zu cels %5zu tiles | full %5lld ms | again %5lld ms | "
                "nothing moved %4lld ms | one cel %4lld ms | open %5lld ms | "
                "%7.1f MB on disk from %7.1f MB of tiles\n",
                drawings, cels, tiles, first, again, untouched, one_moved, opened, megabytes,
                raw);
}

// Where the time actually goes, and how much of it is deflate looking at zeros.
void breakdown() {
    Document doc = buildShot(24);
    std::vector<const Cel*> cels;
    for (CelId id : ProjectIO::celsReferencedBy(doc)) {
        if (const Cel* cel = doc.cel(id)) cels.push_back(cel);
    }

    QElapsedTimer clock;
    clock.start();
    std::vector<QByteArray> encoded;
    encoded.reserve(cels.size());
    long long raw_bytes = 0;
    for (const Cel* cel : cels) {
        const std::vector<std::uint8_t> bytes = ProjectIO::encodeCel(cel->tiles());
        raw_bytes += static_cast<long long>(bytes.size());
        encoded.emplace_back(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<qsizetype>(bytes.size()));
    }
    const qint64 encode_ms = clock.elapsed();

    std::printf("\n  encoding %zu cels to %.0f MB of tiles: %lld ms\n", cels.size(),
                static_cast<double>(raw_bytes) / (1024.0 * 1024.0), encode_ms);

    for (int level : {1, 6, 9}) {
        clock.restart();
        long long out = 0;
        for (const QByteArray& body : encoded) out += qCompress(body, level).size();
        const qint64 ms = clock.elapsed();
        std::printf("  deflate level %d: %6lld ms, %6.1f MB\n", level, ms,
                    static_cast<double>(out) / (1024.0 * 1024.0));
    }

    // How much of what deflate is being handed is nothing at all.
    long long zero = 0;
    for (const QByteArray& body : encoded) {
        for (char c : body) {
            if (c == 0) ++zero;
        }
    }
    std::printf("  of those bytes, %.1f%% are zero\n",
                100.0 * static_cast<double>(zero) / static_cast<double>(raw_bytes));
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("Saving a shot, 1920x1080, line art and a colour layer per drawing:\n");
    for (int drawings : {12, 24, 48, 96}) time(drawings);
    std::printf("\nAutosave pays the \"one cel\" column. It used to pay \"again\".\n");
    breakdown();
    return 0;
}
