// SPDX-License-Identifier: GPL-3.0-or-later
//
// Not a test -- a stopwatch, for the one number docs/importing.md says decides
// whether a reference sequence is usable: **decode time per frame**.
//
// It exists because a report arrived that it could not answer. Playing a
// 151-frame sequence of 4000x2250 PNGs re-decoded the same frames on every pass
// and said so through a warning from libpng, and the two candidate
// explanations -- decoding is slow, or the cache is too small to hold them --
// have different fixes and nothing here could tell them apart.
//
// Three questions, in order:
//   1. What does one frame cost to decode, by size, and where does the time go
//      -- the PNG, or turning it into tiles?
//   2. What does that frame then weigh, which is what decides how many of them
//      the cache holds at once?
//   3. How many frames a second is that, against the 41.7 ms a playback frame
//      has -- which is the number that says whether playing a reference at rate
//      is a tuning question or a different feature.
//
// Run it by hand:  ./build/tests/bench_import -platform offscreen
// Give it a folder and it measures those files instead of made-up ones:
//                  ./build/tests/bench_import -platform offscreen <folder>

#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "image_import.h"
#include "tile.h"
#include "transform.h"

using namespace animage;

namespace {

struct Case {
    const char* what;
    int width;
    int height;
};

// The sizes worth telling apart. HD is the canvas most shots are; 4000x2250 is
// what the report was about, and is what a compositing render hands over when
// nobody has been asked to size it down.
const Case kCases[] = {
    {"HD           1920x1080", 1920, 1080},
    {"the report   4000x2250", 4000, 2250},
    {"half of it   2000x1125", 2000, 1125},
    {"a quarter    1000x563 ", 1000, 563},
};

// Something with detail in it, because a flat fill compresses to nothing and
// would time the PNG reader on a file no renderer ever writes.
QImage aBoard(int width, int height) {
    QImage board(width, height, QImage::Format_RGBA8888);
    board.fill(QColor(246, 244, 238));
    QPainter painter(&board);
    for (int i = 0; i < 400; ++i) {
        const int x = (i * 7919) % width;
        const int y = (i * 6151) % height;
        painter.setPen(QColor((i * 37) % 255, (i * 91) % 255, (i * 53) % 255));
        painter.drawEllipse(QPoint(x, y), width / 40, height / 40);
    }
    painter.end();
    board.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return board;
}

double medianOf(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// One row: read the file, then turn the pixels into tiles, timed apart.
//
// Apart because the two have different fixes. The PNG is somebody else's
// decoder and the only lever on it is the file; the tiling is ours, and it is a
// per-pixel loop through srgbToLinear that docs/importing.md already flags as
// the thing that would dominate a scrub if it dominated anything.
void measure(const char* what, const QString& path) {
    const image_import::Survey survey = image_import::survey(path);
    if (!survey.ok) {
        std::printf("  %-24s could not be read: %s\n", what, qPrintable(survey.trouble));
        return;
    }

    std::vector<double> whole;
    std::vector<double> reading;
    for (int pass = 0; pass < 5; ++pass) {
        QElapsedTimer clock;
        clock.start();
        const QImage image(path);
        const double read_ms = static_cast<double>(clock.nsecsElapsed()) / 1e6;

        clock.restart();
        const TileGrid tiles = image_import::decodeImage(image);
        const double tile_ms = static_cast<double>(clock.nsecsElapsed()) / 1e6;

        whole.push_back(read_ms + tile_ms);
        reading.push_back(read_ms);
        if (pass == 0) {
            const std::size_t megabytes = tiles.tileCount() * sizeof(Tile) / (1024 * 1024);
            std::printf("  %-24s %5d x %-5d  %4zu tiles  %4zu MB", what, survey.width,
                        survey.height, tiles.tileCount(), megabytes);
        }
    }

    const double total = medianOf(whole);
    const double read = medianOf(reading);
    std::printf("   %7.1f ms  (%.1f reading, %.1f tiling)  %5.1f fps\n", total, read, total - read,
                total > 0.0 ? 1000.0 / total : 0.0);
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    std::printf("import decode, median of five\n");
    std::printf("a playback frame at 24 fps has 41.7 ms, all of it, for everything\n\n");

    // A folder on the command line measures the real files somebody reported
    // about, which is worth more than any made-up picture: what a PNG costs
    // depends on what is in it and on what wrote it.
    QString folder;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (!arg.startsWith(QLatin1Char('-')) && QDir(arg).exists()) folder = arg;
    }

    if (!folder.isEmpty()) {
        QDir dir(folder);
        QStringList names = dir.entryList(QDir::Files, QDir::Name);
        std::printf("%s\n", qPrintable(folder));
        int done = 0;
        for (const QString& name : names) {
            if (done >= 3) break;  // three of them is the shape; the rest is the same
            measure(qPrintable(name), dir.filePath(name));
            ++done;
        }
        std::printf("\n");
    }

    QTemporaryDir scratch;
    if (!scratch.isValid()) return 1;
    for (const Case& one : kCases) {
        const QString path = scratch.filePath(QStringLiteral("%1.png").arg(one.width));
        if (!aBoard(one.width, one.height).save(path, "PNG")) continue;
        measure(one.what, path);
    }
    return 0;
}
