// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_import.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>

#include <algorithm>
#include <memory>
#include <unordered_map>

#include "color.h"

using animage::Half;
using animage::kTileSize;
using animage::Rgba;
using animage::Tile;
using animage::TileCoord;
using animage::TileGrid;

namespace image_import {
namespace {

// What a file says its colours mean, in words a recap can print. Only used for
// saying so: the conversion below reads the QColorSpace itself.
QString nameOfSpace(const QColorSpace& space) {
    if (!space.isValid()) return QStringLiteral("none stated (read as sRGB)");

    // Compared against the named spaces rather than asked which one it is:
    // QColorSpace does not report a name back, because a space read from a file
    // is a set of primaries and a transfer function and usually matches none of
    // these exactly.
    struct Known {
        QColorSpace::NamedColorSpace id;
        const char* said;
    };
    static const Known kKnown[] = {
        {QColorSpace::SRgb, "sRGB"},
        {QColorSpace::SRgbLinear, "linear sRGB"},
        {QColorSpace::AdobeRgb, "Adobe RGB"},
        {QColorSpace::DisplayP3, "Display P3"},
        {QColorSpace::ProPhotoRgb, "ProPhoto RGB"},
    };
    for (const Known& known : kKnown) {
        if (space == QColorSpace(known.id)) return QString::fromLatin1(known.said);
    }

    const QString described = space.description();
    return described.isEmpty() ? QStringLiteral("a profile of its own") : described;
}

// A file's colours, brought to sRGB primaries without banding on the way.
//
// The order matters and is the whole of this function. Converting an 8-bit
// image between colour spaces in place quantises twice -- once into the new
// primaries and once back into 8 bits -- and the second one bands, visibly, on
// exactly the flat areas a palette or a modelsheet is made of. Widening first
// costs four bytes a channel for the length of the conversion and removes it.
QImage inSrgb(QImage source, Converted* converted) {
    if (source.isNull()) return source;

    const QColorSpace space = source.colorSpace();
    // No profile at all is read as sRGB, which is what every program does with
    // an untagged file and what the overwhelming majority of them actually are.
    // Saying so is the dialog's job, not this one's.
    if (!space.isValid() || space == QColorSpace(QColorSpace::SRgb)) return source;

    if (converted) converted->from = nameOfSpace(space);
    source.convertTo(QImage::Format_RGBA64);
    source.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
    return source;
}

// One file's name, cut where the ordering rule cuts it.
struct Named {
    QString path;
    QString scheme;  // what surrounds the number, which is what groups files
    long long number = 0;
    bool has_number = false;
    QString name;  // for ordering the ones with no number at all
};

// The last run of digits, which is the convention rather than a guess: a
// renderer writes `shot2_frame010.png` and means frame ten, so reading the
// first run would order a whole shot by the number of the shot.
Named cut(const QString& path) {
    Named out;
    out.path = path;
    out.name = QFileInfo(path).fileName();

    int end = out.name.size();
    while (end > 0 && !out.name.at(end - 1).isDigit()) --end;
    if (end == 0) {
        out.scheme = out.name;  // no digits at all: the whole name is the group
        return out;
    }
    int begin = end;
    while (begin > 0 && out.name.at(begin - 1).isDigit()) --begin;

    bool ok = false;
    const long long value = out.name.mid(begin, end - begin).toLongLong(&ok);
    if (!ok) {
        // Longer than a long long, which no frame number is. Treated as having
        // no number rather than as having a wrong one: a silently wrapped index
        // would order the sequence confidently and incorrectly.
        out.scheme = out.name;
        return out;
    }

    out.number = value;
    out.has_number = true;
    // The padding is deliberately not part of the group. `frame9.png` and
    // `frame10.png` are one sequence and differ in digit count, so a scheme
    // that included the width would split every run at its first carry.
    out.scheme = out.name.left(begin) + QLatin1Char('#') + out.name.mid(end);
    return out;
}

}  // namespace

Ordering order(const QStringList& paths) {
    Ordering out;
    if (paths.isEmpty()) return out;

    std::vector<Named> cuts;
    cuts.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& path : paths) cuts.push_back(cut(path));

    // Numbered first, grouped by scheme, then by number. Unnumbered after all
    // of them, by name -- they cannot take part in the rule, and putting them
    // first would push every frame somebody does have a number for one slot
    // later than the name says.
    std::stable_sort(cuts.begin(), cuts.end(), [](const Named& a, const Named& b) {
        if (a.has_number != b.has_number) return a.has_number;
        if (!a.has_number) return a.name < b.name;
        if (a.scheme != b.scheme) return a.scheme < b.scheme;
        return a.number < b.number;
    });

    // Counted over the numbered files only. A name with no digits in it is
    // already reported on its own, and counting each of them as a scheme of its
    // own would say "named 3 different ways" and then "1 has no number" about
    // overlapping sets -- two sentences that add up to more files than there
    // are.
    std::vector<QString> schemes;
    for (const Named& one : cuts) {
        out.paths.push_back(one.path);
        if (!one.has_number) {
            ++out.unnumbered;
            continue;
        }
        ++out.numbered;
        if (std::find(schemes.begin(), schemes.end(), one.scheme) == schemes.end()) {
            schemes.push_back(one.scheme);
        }
    }
    out.schemes = static_cast<int>(schemes.size());

    // The ends of the run, and only when there is one run to have ends. Across
    // two schemes the first number comes from one group and the last from
    // another, and "1 to 24" would be a sentence about nothing.
    if (out.numbered > 0 && out.schemes == 1) {
        out.first = cuts.front().number;
        out.last = cuts[static_cast<std::size_t>(out.numbered) - 1].number;
        // A run with a hole in it is worth saying: it usually means a render
        // that did not finish, or a selection that missed a file. It is not
        // worth refusing -- a gap is a frame the sequence does not have, and
        // the frames after it keep their own numbers rather than sliding back
        // to close it.
        out.gaps = (out.last - out.first + 1) != out.numbered;
    }
    return out;
}

std::size_t tileCountFor(int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    const std::size_t across = static_cast<std::size_t>((width + kTileSize - 1) / kTileSize);
    const std::size_t down = static_cast<std::size_t>((height + kTileSize - 1) / kTileSize);
    return across * down;
}

Survey survey(const QString& path) {
    Survey out;
    QImageReader reader(path);
    // Asked of the reader rather than by decoding, because the point of a
    // survey is to say what an import will cost *before* paying it -- and on a
    // 300 dpi A4 scan the decode is the cost.
    const QSize size = reader.size();
    if (!size.isValid() || size.isEmpty()) {
        out.trouble = reader.errorString();
        if (out.trouble.isEmpty()) out.trouble = QStringLiteral("not a picture this build reads");
        return out;
    }
    out.ok = true;
    out.width = size.width();
    out.height = size.height();
    return out;
}

TileGrid decodeImage(const QImage& source, Converted* converted) {
    TileGrid grid;
    if (source.isNull()) return grid;

    // Straight (unpremultiplied) 16-bit, so that the premultiply below is ours
    // and happens once, in float, rather than having been done already in eight
    // bits by somebody else. A PNG's alpha arrives associated either way; what
    // this avoids is multiplying twice.
    const QImage image = inSrgb(source, converted).convertToFormat(QImage::Format_RGBA64);
    if (image.isNull()) return grid;

    const int width = image.width();
    const int height = image.height();

    // Built tile by tile rather than pixel by pixel into the grid, because a
    // tile is immutable once shared: there is no way to write one in place
    // afterwards, and no reason to want one.
    std::unordered_map<TileCoord, std::shared_ptr<Tile>, animage::TileCoordHash> built;

    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const quint16*>(image.constScanLine(y));
        const int tile_y = y / kTileSize;
        const int local_y = y % kTileSize;
        for (int x = 0; x < width; ++x) {
            const quint16* p = row + static_cast<std::size_t>(x) * 4;
            const float a = static_cast<float>(p[3]) / 65535.0f;
            if (a <= 0.0f) continue;  // sparse: a transparent pixel makes no tile

            // sRGB is an encoding of the *colour* and never of the alpha, so
            // the three channels go through srgbToLinear and the fourth does
            // not. Getting that wrong is invisible on an opaque image and wrong
            // on every other one.
            const Rgba colour = animage::premultiply(
                animage::srgbToLinear(static_cast<float>(p[0]) / 65535.0f),
                animage::srgbToLinear(static_cast<float>(p[1]) / 65535.0f),
                animage::srgbToLinear(static_cast<float>(p[2]) / 65535.0f), a);

            const TileCoord coord{x / kTileSize, tile_y};
            auto& tile = built[coord];
            if (!tile) tile = std::make_shared<Tile>();
            tile->setPixel(x % kTileSize, local_y, colour);
        }
    }

    for (auto& [coord, tile] : built) grid.set(coord, std::move(tile));
    return grid;
}

TileGrid decode(const QString& path, QString* trouble, Converted* converted) {
    QImageReader reader(path);
    // Qt refuses very large images by default, and a 300 dpi A4 scan is not
    // large by this program's standards -- a shot holds dozens of them.
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        if (trouble) {
            *trouble = reader.errorString().isEmpty()
                           ? QStringLiteral("not a picture this build reads")
                           : reader.errorString();
        }
        return {};
    }
    return decodeImage(image, converted);
}

}  // namespace image_import
