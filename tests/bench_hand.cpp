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
#include <utility>
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

// A carried mark that filled nothing but itself, and how much of the drawing it
// put a colour on anyway.
//
// For [#73](https://github.com/S-poony/Animage/issues/73), and a measurement
// before any of it is built: how often does a carried mark land somewhere it
// wins no region, and when it does, is the mess it leaves big enough to see?
// The second half is the question that decides whether this is worth doing --
// a stray that colours forty pixels is one a colourist never notices, and a
// stray that colours a tenth of the drawing is one they have to hunt for and
// erase.
//
// **Off the solver's labels and never off the finished fill.** A mark wins its
// own pixels in the fill whatever the solver decided, so read back from the
// picture every mark looks perfectly placed -- the handover's "why the proposed
// confidence score reads 1 on every case", and there is a test pinning it.
//
// A *component* and not a mark, which is the granularity that makes this
// answerable. `CtgFill::spread` is per palette colour and reduced to one number
// for the whole fill, so it cannot say which mark was stranded, and two
// scribbles sharing a colour average each other out. A connected run of labels
// can: if a run is barely larger than the mark sitting in it, that mark won
// nothing, and if two same-coloured scribbles landed in one region the run
// holds both and is not a stray. No new plumbing in the estimator to find out.
struct Strays {
    int found = 0;           // components a mark won nothing in
    long long pixels = 0;    // image pixels those components coloured
    long long worst = 0;     // and the largest single one
    int marked = 0;          // components with any mark in them, as the floor
};

// How much bigger than the mark inside it a component has to be to count as
// won. Deliberately generous: a mark that filled nothing measures exactly 1.00
// and one that snugly fills a small region measures 1.96, so anything at or
// under 1.5 is the stranded end of a gap the handover measured rather than a
// cutoff chosen here. Being a measurement and not a rule, what matters is that
// the number is stated, not that it is exactly right.
constexpr double kWonNothingBelow = 1.5;

Strays strays(const CtgFill& fill) {
    Strays out;
    const int width = fill.gridWidth();
    const int height = fill.gridHeight();
    const std::size_t cells = static_cast<std::size_t>(std::max(0, width)) * std::max(0, height);
    if (cells == 0 || fill.labels.size() != cells || fill.step <= 0) return out;

    const long long cell_pixels =
        static_cast<long long>(fill.step) * static_cast<long long>(fill.step);
    std::vector<char> seen(cells, 0);
    std::vector<int> stack;

    for (std::size_t start = 0; start < cells; ++start) {
        if (seen[start] || fill.labels[start] < 0) continue;
        const std::int16_t label = fill.labels[start];

        long long area = 0;
        long long seeded = 0;
        stack.clear();
        stack.push_back(static_cast<int>(start));
        seen[start] = 1;
        while (!stack.empty()) {
            const int at = stack.back();
            stack.pop_back();
            ++area;

            // Is a mark standing on this cell? The marks travel with the fill
            // and are already where they are used, so this is the drawing's own
            // coordinates and no warp is applied here.
            const int cx = at % width;
            const int cy = at / width;
            const int px = fill.solved.x + cx * fill.step;
            const int py = fill.solved.y + cy * fill.step;
            if (fill.marks.pixel(px, py).a >= fill.mark_threshold) ++seeded;

            const auto visit = [&](int nx, int ny) {
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) return;
                const std::size_t index = static_cast<std::size_t>(ny) * width + nx;
                if (seen[index] || fill.labels[index] != label) return;
                seen[index] = 1;
                stack.push_back(static_cast<int>(index));
            };
            visit(cx - 1, cy);
            visit(cx + 1, cy);
            visit(cx, cy - 1);
            visit(cx, cy + 1);
        }

        if (seeded == 0) continue;  // reached by spreading, not by a mark of its own
        ++out.marked;
        if (static_cast<double>(area) > kWonNothingBelow * static_cast<double>(seeded)) continue;
        ++out.found;
        const long long coloured = area * cell_pixels;
        out.pixels += coloured;
        out.worst = std::max(out.worst, coloured);
    }
    return out;
}

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

// One drawing's line art, flattened, and the rectangle it was flattened over.
//
// The two travel as one thing because a coverage indexed against a rectangle it
// was not flattened over is off by an origin, which is a wrong picture rather
// than a crash -- and because every picture of one drawing now shares both.
struct FlatInk {
    PixelRect over;
    std::vector<float> coverage;  // one per pixel of `over`: 0 bare paper, 1 solid
};

// The sources flattened over `over`, once for every picture of that drawing.
//
// Reduced by the most covered pixel, which is the barrier's rule and is right
// here for the barrier's reason: a line that vanishes is a boundary the eye
// cannot check.
//
// Once per drawing and not once per picture, because this is what writing a
// picture actually costs: every source layer composited at full resolution and a
// float per pixel to hold the answer -- about 8 MB over a 1920x1080 drawing. It
// was done inside writePicture, so a shot of fifty drawings composited the same
// line art two hundred and fifty times, over five rectangles that were nearly
// the same rectangle.
FlatInk flattenInk(const std::vector<TileGrid>& sources, const PixelRect& over) {
    if (over.isEmpty()) return {};
    return {over, ctgInkCoverage(sources, over, 1, InkReduce::Most)};
}

// The fill, with the line art it was cut against drawn over it.
//
// The line art is not decoration. A fill on its own is a field of flat colour,
// and the question being asked of it -- did this colour land where somebody
// meant it to -- is a question about where the regions are, which is a fact
// about the drawing and not about the fill. Judged without it, an answer that
// has put a whole limb in the wrong colour looks like the right picture
// slightly moved.
//
// Which is also why the picture covers `lines.over` -- one rectangle for every
// picture of one drawing -- rather than the rectangle of the fill that made it.
// Each fill has its own idea of how much of the drawing it is about: a method
// that carried a mark off its shape solves a different rectangle from one that
// left it where it was drawn, and the hand-coloured fill a third. Cut to that,
// the five PNGs of one drawing have five origins and five sizes, so flipping
// between them in a viewer shifts and rescales the drawing and the eye reads a
// translation that is not there -- the very thing they are written to show. A
// common rectangle costs nothing but the pixels it adds: a fill answers outside
// the rectangle it solved, exactly rather than approximately, and ctgFillPixel
// says why.
void writePicture(const CtgFill& fill, const FlatInk& lines, const QString& path) {
    const PixelRect& over = lines.over;
    if (over.isEmpty()) return;
    QImage picture(over.width, over.height, QImage::Format_ARGB32);
    picture.fill(Qt::transparent);
    for (int y = 0; y < over.height; ++y) {
        auto* row = reinterpret_cast<QRgb*>(picture.scanLine(y));
        for (int x = 0; x < over.width; ++x) {
            const Rgba pixel = ctgFillPixel(fill, over.x + x, over.y + y);
            const std::size_t at = static_cast<std::size_t>(y) * over.width + x;
            const float inked =
                (at < lines.coverage.size()) ? std::clamp(lines.coverage[at], 0.0f, 1.0f) : 0.0f;
            const auto channel = [&](float value) {
                // The ink is black, so what it covers it darkens to nothing.
                return static_cast<int>(
                    std::clamp(value * (1.0f - inked), 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            const float alpha = std::clamp(std::max(pixel.a, inked), 0.0f, 1.0f);
            row[x] = qRgba(channel(pixel.r), channel(pixel.g), channel(pixel.b),
                           static_cast<int>(alpha * 255.0f + 0.5f));
        }
    }
    // Said nothing when it failed: a bad path or a full disk left the run
    // reporting "pictures in DIR" over a directory that had none.
    if (!picture.save(path)) {
        std::fprintf(stderr, "could not write %s\n", qPrintable(path));
    }
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
        "`wrong` is the column with nothing else watching it. Read the methods\n"
        "against each other rather than the digits: the hand is one plausible\n"
        "answer and not the only one, so none of them reaches 100.\n\n");
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
            int stray_found[std::size(methods)] = {};
            int stray_marked[std::size(methods)] = {};
            long long stray_pixels[std::size(methods)] = {};
            long long stray_worst[std::size(methods)] = {};
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

                // One job, read once, and each method is one edit to the copy.
                // Nothing about the layer is touched: an empty
                // `origin_sources` is how a job says "leave the marks where
                // they were drawn", which is the same switch the layer's own
                // setting reaches.
                const CtgJob carried = ctgJobFor(doc, track_id, image, layer_id, CtgSettings{},
                                                 kFullSolveBudget);

                // Every picture of this drawing covers one rectangle, and which
                // one is not known until the last method has been solved: it is
                // the union of the rectangles the scoring looked at, starting
                // from the hand's own. So the fills are kept and the pictures
                // written afterwards -- and a solve is by far the most
                // expensive thing here, so keeping them is the alternative to
                // doing it twice.
                //
                // Kept only when there are pictures to write. A fill is one
                // label per solved cell, about 4 MB at 1080p, and four of them
                // per drawing is memory a run that writes nothing would never
                // read.
                std::vector<CtgFill> for_pictures;
                PixelRect pictured = judged(truth, truth);
                if (!options.pictures.isEmpty()) for_pictures.reserve(std::size(methods));

                for (std::size_t m = 0; m < std::size(methods); ++m) {
                    CtgJob job = carried;
                    if (!methods[m].follow) job.origin_sources.clear();
                    job.settings.carry = methods[m].carry;

                    CtgFill test = solveCtgJob(job, true);
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

                    const Strays stray = strays(test);
                    stray_found[m] += stray.found;
                    stray_marked[m] += stray.marked;
                    stray_pixels[m] += stray.pixels;
                    stray_worst[m] = std::max(stray_worst[m], stray.worst);

                    char fixed[32];
                    std::snprintf(fixed, sizeof(fixed), "%d of %d", fixes.wrong, fixes.regions);
                    char stranded[32] = "";
                    if (stray.found > 0) {
                        std::snprintf(stranded, sizeof(stranded), "%d stray, %lldpx", stray.found,
                                      stray.worst);
                    }
                    std::printf(
                        "      %5zu    %5s    %-20s %7s   %6.1f%%   %6.1f%%   %6.1f%%   %-14s %s\n",
                        at + 1, m == 0 ? std::to_string(colours).c_str() : "", methods[m].name,
                        fixed, scored.percent(scored.agreed), scored.percent(scored.differed),
                        scored.percent(scored.missing), stranded,
                        methods[m].follow ? decided(test.carried_by).c_str() : "");

                    if (!options.pictures.isEmpty()) {
                        pictured = unite(pictured, over);
                        for_pictures.push_back(std::move(test));
                    }
                }

                if (!options.pictures.isEmpty()) {
                    const FlatInk lines = flattenInk(carried.sources, pictured);
                    for (std::size_t m = 0; m < for_pictures.size(); ++m) {
                        writePicture(for_pictures[m], lines,
                                     QString("%1/%2-%3-%4.png")
                                         .arg(options.pictures)
                                         .arg(QString::fromStdString(layer.name))
                                         .arg(static_cast<int>(at + 1), 3, 10, QChar('0'))
                                         .arg(static_cast<int>(m) + 1));
                    }
                    writePicture(truth, lines,
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

            // And what a stray costs, for #73.
            //
            // `stray` is a mark that won no region; `of` is every component a
            // mark stands in, so the two together are a rate and not a count.
            // `px` is what those strays coloured in total and `worst` the
            // largest one, which is the pair that says whether a colourist
            // would see them: a stray the size of a scribble is one nobody
            // notices, and one the size of a limb is one they have to hunt.
            std::printf("\n      %-32s%-20s %10s   %10s   %10s\n", "", "", "strays", "px",
                        "worst px");
            for (std::size_t m = 0; m < std::size(methods); ++m) {
                char rate[32];
                std::snprintf(rate, sizeof(rate), "%d of %d", stray_found[m], stray_marked[m]);
                std::printf("      %-32s%-20s %10s   %10lld   %10lld\n",
                            m == 0 ? "  marks that won nothing" : "", methods[m].name, rate,
                            stray_pixels[m], stray_worst[m]);
            }
            for (std::size_t m = 0; m < std::size(methods); ++m) {
                std::vector<double> same = each_same[m];
                std::vector<double> wrong = each_wrong[m];
                if (same.empty()) continue;
                std::sort(same.begin(), same.end());
                std::sort(wrong.begin(), wrong.end());
                // Three order statistics over two independently sorted vectors,
                // so the row is not a drawing -- the median `same`, the median
                // `wrong` and the lowest `same` can each come from a different
                // one. It said "the middle drawing", which read as though one
                // drawing had all three numbers.
                const double middle = same[same.size() / 2];
                const double middle_wrong = wrong[wrong.size() / 2];
                std::printf("      %-32s%-20s %7s   %6.1f%%   %6.1f%%   worst drawing %5.1f%%\n",
                            m == 0 ? "  median over drawings" : "", methods[m].name, "", middle,
                            middle_wrong, same.front());
            }
        }
    }

    if (!options.pictures.isEmpty()) {
        std::printf("\npictures in %s, named layer-drawing-method: -hand is the colourist's,\n"
                    "then 1 to %zu for the methods, in the order of the table. The line\n"
                    "art is drawn over each, because where a colour belongs is a fact\n"
                    "about the drawing and not about the fill. All the pictures of one\n"
                    "drawing cover the same rectangle, so flipping between them in a\n"
                    "viewer moves only what the method moved.\n",
                    qPrintable(options.pictures), std::size(methods));
    }
    return 0;
}
