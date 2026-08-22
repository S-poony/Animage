// SPDX-License-Identifier: GPL-3.0-or-later
//
// How well carrying a mark agrees with somebody who did it by hand.
//
// Every other measurement of carrying is against a shape this repository drew:
// bench_carry moves a box a known number of pixels and asks whether the colour
// followed it. That answers the question exactly and it cannot answer the one
// that matters, because a box drawn twice by a program is the same box, and two
// drawings by a person never are. The failure rung two was actually reported
// for -- five circles that had drifted and shrunk by a fifth, and a translation
// asked to explain a change of size -- is a failure no synthetic fixture here
// can produce.
//
// So this one is scored against a shot somebody coloured. The project it opens
// was coloured drawing by drawing, with the scribbles corrected wherever the
// carry did not follow the animation -- which makes every drawing that owns
// scribbles a place the carry was wrong, and the fill those scribbles produce
// what right looked like there.
//
// The method, one drawing at a time:
//
//   1. Solve the drawing with its own marks. That is the answer to beat.
//   2. Clear them, so the drawing inherits from the one before it, and solve
//      again -- once for each way of carrying.
//   3. Compare the two *fills*, not the two sets of marks. Where a mark was put
//      by hand is not a fact about anything; what the colourist looked at and
//      accepted is the picture.
//   4. Put the marks back.
//
// Three numbers, over the pixels the hand-coloured fill gave a colour to:
// agreed, took a *different* colour, and took none. The middle one is the one
// with nothing else watching it -- it is the wrong-region failure the design
// note leaves open, and neither `spread` nor any other quantity read off one
// drawing has ever been able to see it.
//
// **Read the columns against each other and not the digits.** The hand-coloured
// fill is one plausible answer and not the only one, so no method reaches 100%
// and the absolute level means little. What is not noisy is the difference
// between the three columns, because all three are scored against exactly the
// same hand and the same drawings.
//
// One thing about the shot to know before reading a low row: the marks on it
// were made to correct the colour where it went wrong, not to give every region
// a mark of its own. A drawing whose marks are the minimum that worked *there*
// has less to carry forward than one marked generously, and that is a property
// of the source drawing rather than of the method reading it.
//
// Two shots are in the tree. The default is the coloured one, which is real
// work and answers "is this better"; the other is two circles and answers "what
// exactly is broken", which a real shot cannot:
//
//   ./build/tests/bench_hand -platform offscreen [--pictures DIR]
//   ./build/tests/bench_hand -platform offscreen --project tests/projects/two-circles.animage
//
// A project with more than one colour layer is scored once per layer, which is
// how the same drawings under two different ways of scribbling can be compared.

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QString>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "compositor.h"
#include "ctg.h"
#include "document.h"
#include "project_io.h"

using namespace animage;

namespace {

// The shot this was written against, in the tree so that the numbers cannot
// move under it. Not the same project as bench_session's: that one is this
// shot's line art before it was coloured, and what this needs is the colour.
constexpr const char* kDefaultProject = "projects/chatquimarche-coloured.animage";

QString defaultProjectFolder() {
    const QDir here(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    return QDir::cleanPath(here.absoluteFilePath(QString::fromUtf8(kDefaultProject)));
}

// One way of carrying, and what to call it.
struct Method {
    const char* name;
    bool follow = false;  // move the marks at all
    CtgSettings::Carry carry = CtgSettings::Carry::WholeDrawing;
};

// How one fill answered where another one had a colour.
struct Agreement {
    long long judged = 0;   // samples the hand-coloured fill gave a colour to
    long long agreed = 0;
    long long differed = 0;  // a colour, and the wrong one
    long long missing = 0;   // no colour at all

    double percent(long long part) const {
        return judged ? 100.0 * static_cast<double>(part) / static_cast<double>(judged) : 0.0;
    }
};

// Compared by label rather than by colour, which is the same comparison the
// seeding makes: a fill answers with the quantised colour of the mark that won,
// so two fills agree exactly or not at all and there is no tolerance to choose.
Agreement compare(const CtgFill& truth, const CtgFill& test, const PixelRect& over, int stride) {
    Agreement out;
    for (int y = over.y; y < over.y + over.height; y += stride) {
        for (int x = over.x; x < over.x + over.width; x += stride) {
            const Rgba wanted = ctgFillPixel(truth, x, y);
            if (wanted.a <= 0.5f) continue;
            ++out.judged;

            const Rgba got = ctgFillPixel(test, x, y);
            if (got.a <= 0.5f) {
                ++out.missing;
            } else if (scribbleLabel(got) == scribbleLabel(wanted)) {
                ++out.agreed;
            } else {
                ++out.differed;
            }
        }
    }
    return out;
}

// How many separate things somebody would have to go and fix.
//
// Counting pixels answers "how much of the picture is wrong", and that is not
// what a colourist pays. A whole body that came back with no colour is one
// scribble to nudge; three small areas each taking a neighbour's colour is
// three, and costs more. Weighted by area the first looks like a disaster and
// the second like nothing, which is the comparison upside down.
//
// So the unit is the region: a connected run of one label in the hand-coloured
// fill, and whether the carried fill gave it that same colour over most of it.
// Majority, because that is the rule the whole layer runs on -- a region takes
// the colour with the greater share of its pixels, so a region that is mostly
// right is right.
//
// Regions smaller than a small scribble are skipped. The labelling is exact and
// its edges are ragged at the cut, so a shape's outline leaves slivers a cell or
// two wide that nobody would call a region or go and fix.
struct Fixes {
    int regions = 0;
    int wrong = 0;
};

constexpr long long kSmallestRegion = 24 * 24;  // image pixels

Fixes countFixes(const CtgFill& truth, const CtgFill& test) {
    Fixes out;
    const int width = truth.gridWidth();
    const int height = truth.gridHeight();
    const std::size_t cells = static_cast<std::size_t>(std::max(0, width)) * std::max(0, height);
    if (cells == 0 || truth.labels.size() != cells) return out;

    const long long cell_pixels =
        static_cast<long long>(truth.step) * static_cast<long long>(truth.step);
    std::vector<char> seen(cells, 0);
    std::vector<int> stack;
    std::vector<int> region;

    for (std::size_t start = 0; start < cells; ++start) {
        if (seen[start] || truth.labels[start] < 0) continue;
        const std::int16_t label = truth.labels[start];

        region.clear();
        stack.clear();
        stack.push_back(static_cast<int>(start));
        seen[start] = 1;
        while (!stack.empty()) {
            const int at = stack.back();
            stack.pop_back();
            region.push_back(at);
            const int cx = at % width;
            const int cy = at / width;
            const auto visit = [&](int nx, int ny) {
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) return;
                const std::size_t index = static_cast<std::size_t>(ny) * width + nx;
                if (seen[index] || truth.labels[index] != label) return;
                seen[index] = 1;
                stack.push_back(static_cast<int>(index));
            };
            visit(cx - 1, cy);
            visit(cx + 1, cy);
            visit(cx, cy - 1);
            visit(cx, cy + 1);
        }

        if (static_cast<long long>(region.size()) * cell_pixels < kSmallestRegion) continue;
        ++out.regions;

        // Compared as colours and not as labels: two fills index their own
        // palettes, and the same colour is a different number in each.
        const std::uint32_t wanted =
            scribbleLabel(truth.palette_colours[static_cast<std::size_t>(label)]);
        long long agreed = 0;
        for (int at : region) {
            const int x = truth.solved.x + (at % width) * truth.step + truth.step / 2;
            const int y = truth.solved.y + (at / width) * truth.step + truth.step / 2;
            if (scribbleLabel(ctgFillPixel(test, x, y)) == wanted) ++agreed;
        }
        if (agreed * 2 < static_cast<long long>(region.size())) ++out.wrong;
    }
    return out;
}

// What the carry decided, in one column: the shift it gave the whole drawing,
// and where the field departed from it. A row that went wrong and a row that
// departed a long way are usually the same row, and without this the table says
// which one went wrong without saying what it did.
std::string decided(const CtgWarp& warp) {
    char text[64];
    if (warp.isUniform()) {
        std::snprintf(text, sizeof(text), "%d,%d", warp.overall.x, warp.overall.y);
        return text;
    }
    std::vector<CtgShift> distinct{warp.overall};
    int furthest = 0;
    for (const CtgShift& cell : warp.cells) {
        if (std::find(distinct.begin(), distinct.end(), cell) == distinct.end()) {
            distinct.push_back(cell);
        }
        furthest = std::max(furthest, std::abs(cell.x - warp.overall.x));
        furthest = std::max(furthest, std::abs(cell.y - warp.overall.y));
    }
    std::snprintf(text, sizeof(text), "%d,%d %d ways, %d off", warp.overall.x, warp.overall.y,
                  static_cast<int>(distinct.size()), furthest);
    return text;
}

// Where to look: everything either fill has an opinion about.
PixelRect judged(const CtgFill& truth, const CtgFill& test) {
    PixelRect over = unite(truth.solved, test.solved);
    return unite(over, unite(truth.marks_drawn, test.marks_drawn));
}

void writePicture(const CtgFill& fill, const PixelRect& over, const QString& path) {
    if (over.isEmpty()) return;
    QImage picture(over.width, over.height, QImage::Format_ARGB32);
    picture.fill(Qt::transparent);
    for (int y = 0; y < over.height; ++y) {
        auto* row = reinterpret_cast<QRgb*>(picture.scanLine(y));
        for (int x = 0; x < over.width; ++x) {
            const Rgba pixel = ctgFillPixel(fill, over.x + x, over.y + y);
            const auto channel = [](float value) {
                return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            row[x] = qRgba(channel(pixel.r), channel(pixel.g), channel(pixel.b),
                           channel(pixel.a));
        }
    }
    picture.save(path);
}

struct Options {
    QString project;
    QString pictures;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    Options options;
    for (int i = 1; i < argc; ++i) {
        const auto value = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if (std::strcmp(argv[i], "--project") == 0) {
            options.project = QString::fromLocal8Bit(value());
        } else if (std::strcmp(argv[i], "--pictures") == 0) {
            options.pictures = QString::fromLocal8Bit(value());
        }
    }
    if (options.project.isEmpty()) options.project = defaultProjectFolder();

    Document doc;
    QString error;
    if (!ProjectIO::load(doc, options.project, &error)) {
        std::printf("could not open %s: %s\n", qPrintable(options.project), qPrintable(error));
        return 1;
    }
    if (!options.pictures.isEmpty()) QDir().mkpath(options.pictures);

    std::printf(
        "Carrying a mark, scored against a shot somebody coloured by hand.\n\n"
        "Each drawing that owns marks is solved twice: once with them, which is\n"
        "the answer to beat, and once with them cleared so that it inherits. Of\n"
        "the pixels the hand-coloured fill gave a colour to, `same` took that\n"
        "colour, `wrong` took another one, and `none` took nothing.\n\n"
        "`wrong` is the column with nothing else watching it. Read the three\n"
        "methods against each other rather than the digits: the hand is one\n"
        "plausible answer and not the only one, so none of them reaches 100.\n\n");
    std::printf("opened %s\n", qPrintable(options.project));

    const Method methods[] = {
        {"left where drawn", false, CtgSettings::Carry::WholeDrawing},
        {"one shift  (rung 2)", true, CtgSettings::Carry::WholeDrawing},
        {"per region (rung 3)", true, CtgSettings::Carry::PerRegion},
        {"lattice    (rung 4)", true, CtgSettings::Carry::Lattice},
    };

    // Judged every other pixel in both directions. A quarter of the work, and
    // the quantity is a fraction of an area rather than a count of pixels.
    constexpr int kStride = 2;

    for (const Track& track : doc.scene().tracks) {
        for (const Layer& layer : track.layers) {
            if (layer.kind != LayerKind::Ctg) continue;

            const TrackId track_id = track.id;
            const LayerId layer_id = layer.id;

            // In the order they are shown, over distinct drawings: a drawing
            // held for five frames is one drawing and not five.
            std::vector<ImageId> shown;
            for (ImageId image : track.slots) {
                if (image == kNoId) continue;
                if (!shown.empty() && shown.back() == image) continue;
                if (std::find(shown.begin(), shown.end(), image) != shown.end()) continue;
                shown.push_back(image);
            }

            std::printf("\n  track \"%s\", layer \"%s\": %zu drawings\n", track.name.c_str(),
                        layer.name.c_str(), shown.size());
            std::printf("      drawing   marks    method                to fix      same"
                        "     wrong      none   decided\n");

            Agreement totals[std::size(methods)];
            int to_fix[std::size(methods)] = {};
            int regions[std::size(methods)] = {};
            std::vector<double> each_same[std::size(methods)];
            std::vector<double> each_wrong[std::size(methods)];

            for (std::size_t at = 0; at < shown.size(); ++at) {
                const ImageId image = shown[at];
                const Image* record = track.findImage(image);
                if (!record) continue;

                // Only a drawing with marks of its own can be scored: one that
                // is already inheriting has no hand-made answer to be compared
                // with, and clearing nothing would compare a fill with itself.
                const CelId own = record->celFor(layer_id);
                if (own == kNoId) continue;

                const CtgFill truth =
                    solveCtgJob(ctgJobFor(doc, track_id, image, layer_id, CtgSettings{},
                                          kFullSolveBudget),
                                true);
                if (!truth.valid) continue;
                const int colours = truth.colours;

                // Cleared, which is exactly "revert to inherited" -- and undone
                // afterwards, so the next drawing is asked the same question
                // this one was and not a harder one.
                //
                // The depth is taken first and undone back to, rather than one
                // undo per thing done. An edit that changes nothing records no
                // command, so counting them is how a bench quietly pops
                // somebody else's step: the first four drawings came back with
                // no colour at all, because a layer set to the value it already
                // had left an undo with nothing of its own to take.
                const std::size_t before = doc.undoDepth();
                doc.clearCel(track_id, image, layer_id);
                const bool restore = doc.undoDepth() > before;

                // Nothing earlier to inherit from, which is the first coloured
                // drawing of the shot and is a drawing somebody has to colour.
                if (doc.ctgScribblesAt(track_id, image, layer_id) == nullptr) {
                    while (restore && doc.undoDepth() > before) doc.undo();
                    continue;
                }

                // One job, read once, and the three methods are three edits to
                // the copy. Nothing about the layer is touched: an empty
                // `origin_sources` is how a job says "leave the marks where
                // they were drawn", which is the same switch the layer's own
                // setting reaches.
                const CtgJob carried = ctgJobFor(doc, track_id, image, layer_id, CtgSettings{},
                                                 kFullSolveBudget);

                for (std::size_t m = 0; m < std::size(methods); ++m) {
                    CtgJob job = carried;
                    if (!methods[m].follow) job.origin_sources.clear();
                    job.settings.carry = methods[m].carry;

                    const CtgFill test = solveCtgJob(job, true);
                    const PixelRect over = judged(truth, test);
                    const Agreement scored = compare(truth, test, over, kStride);
                    const Fixes fixes = countFixes(truth, test);
                    to_fix[m] += fixes.wrong;
                    regions[m] += fixes.regions;

                    totals[m].judged += scored.judged;
                    totals[m].agreed += scored.agreed;
                    totals[m].differed += scored.differed;
                    totals[m].missing += scored.missing;
                    each_same[m].push_back(scored.percent(scored.agreed));
                    each_wrong[m].push_back(scored.percent(scored.differed));

                    char fixed[32];
                    std::snprintf(fixed, sizeof(fixed), "%d of %d", fixes.wrong, fixes.regions);
                    std::printf("      %5zu    %5s    %-20s %7s   %6.1f%%   %6.1f%%   %6.1f%%   %s\n",
                                at + 1, m == 0 ? std::to_string(colours).c_str() : "",
                                methods[m].name, fixed, scored.percent(scored.agreed),
                                scored.percent(scored.differed), scored.percent(scored.missing),
                                methods[m].follow ? decided(test.carried_by).c_str() : "");

                    if (!options.pictures.isEmpty()) {
                        writePicture(test, over,
                                     QString("%1/%2-%3-%4.png")
                                         .arg(options.pictures)
                                         .arg(QString::fromStdString(layer.name))
                                         .arg(static_cast<int>(at + 1), 3, 10, QChar('0'))
                                         .arg(static_cast<int>(m) + 1));
                    }
                }

                if (!options.pictures.isEmpty()) {
                    writePicture(truth, judged(truth, truth),
                                 QString("%1/%2-%3-hand.png")
                                     .arg(options.pictures)
                                     .arg(QString::fromStdString(layer.name))
                                     .arg(static_cast<int>(at + 1), 3, 10, QChar('0')));
                }

                while (restore && doc.undoDepth() > before) doc.undo();
                const Image* back = track.findImage(image);
                if (back == nullptr || back->celFor(layer_id) != own) {
                    std::printf("      (drawing %zu did not come back; stopping)\n", at + 1);
                    return 1;
                }
            }

            // Two summaries, because they answer different questions and the
            // first one alone was misleading.
            //
            // The total is weighted by area, so one drawing where a mark landed
            // off its shape speaks for the whole shot -- which is worth knowing
            // and is not what "is this method better" means. The middle drawing
            // and the worst one say it the other way round: how it goes
            // ordinarily, and how badly it goes when it goes badly. A method
            // that is better on most drawings and catastrophic on one is a
            // different thing from a method that is mildly worse throughout,
            // and the total cannot tell them apart.
            std::printf("\n      %-32s%-20s %7s   %6s   %6s   %6s\n", "", "", "to fix",
                        "same", "wrong", "none");
            for (std::size_t m = 0; m < std::size(methods); ++m) {
                char fixed[32];
                std::snprintf(fixed, sizeof(fixed), "%d of %d", to_fix[m], regions[m]);
                std::printf("      %-32s%-20s %7s   %6.1f%%   %6.1f%%   %6.1f%%\n",
                            m == 0 ? "  over the whole shot" : "", methods[m].name, fixed,
                            totals[m].percent(totals[m].agreed),
                            totals[m].percent(totals[m].differed),
                            totals[m].percent(totals[m].missing));
            }
            for (std::size_t m = 0; m < std::size(methods); ++m) {
                std::vector<double> same = each_same[m];
                std::vector<double> wrong = each_wrong[m];
                if (same.empty()) continue;
                std::sort(same.begin(), same.end());
                std::sort(wrong.begin(), wrong.end());
                const double middle = same[same.size() / 2];
                const double middle_wrong = wrong[wrong.size() / 2];
                std::printf("      %-32s%-20s %7s   %6.1f%%   %6.1f%%   worst drawing %5.1f%%\n",
                            m == 0 ? "  the middle drawing" : "", methods[m].name, "", middle,
                            middle_wrong, same.front());
            }
        }
    }

    if (!options.pictures.isEmpty()) {
        std::printf("\npictures in %s, named layer-drawing-method: -hand is the colourist's,\n"
                    "then 1, 2, 3 for the methods in the order of the table\n",
                    qPrintable(options.pictures));
    }
    return 0;
}
