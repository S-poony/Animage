// SPDX-License-Identifier: GPL-3.0-or-later
//
// What an estimator does to a family of shapes, all moved the same known
// amount.
//
// Every other measurement of carrying moves *one* shape. bench_carry moves a
// box and asks how far a mark survives; bench_hand opens a shot somebody
// coloured and asks whether the answer agrees with a person. Both are about
// motion, and neither varies the thing being moved -- so between them they
// asked four rungs the same question about a rectangle for a year and none of
// them noticed that rung four cannot follow a rectangle at all.
//
// That is what this is for. The shapes are chosen to differ in one property
// each -- open against closed, straight against curved, hollow against filled,
// with and without something in the middle -- and the motion is a plain
// translation that every rung ought to find. Where an estimator fails, the
// neighbouring rows say what the failure is a property *of*, which is the part
// no single fixture can tell you.
//
// It found the shape of issue #69 in about ten minutes:
//
//   - three walls of a box are fine and four are not, so it is closure;
//   - the same shape turned 45 degrees is just as bad, so it is not axis
//     alignment;
//   - filling it does not help, while a filled *disc* is fine, so it is not
//     line art against filled art;
//   - a box with a cross scribbled through it is fine, so it is the empty
//     middle rather than the walls.
//
// What the working shapes have and the failing ones lack is something within a
// block's reach that pins both directions at once. A long straight run has
// none, and a closed loop of them can shear while every local rigid fit stays
// satisfied.
//
// Run it by hand:  ./build/tests/bench_shapes
//
// **Reading it.** `truth` is the translation the shape was actually given, so
// it is the answer. `worst` is how far the furthest cell of the field is from
// that -- which is the number that decides whether a carried mark lands, since
// a mark is carried through the field and not through the average. A uniform
// warp has no field and reports its one answer as the worst.
//
// Rung three is deliberately absent. It asks a different question -- where did
// *this region's* marks go -- so it needs marks and a region to own them, and
// several of these shapes have no interior at all. Its instrument is
// bench_carry. What is here is the two rungs that read line art and nothing
// else.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "ctg_job.h"
#include "tile.h"

using namespace animage;

namespace {

// A scratch drawing surface that freezes into a TileGrid.
//
// Deliberately not the brush: what is wanted here is a shape with known
// geometry, and going through Document and BrushSettings to get one would make
// the fixture depend on the paint path in order to measure the estimator. A
// stroke is a run of discs, which is all any of these need.
class Sheet {
public:
    void plot(int x, int y) {
        const TileCoord c = tileCoordFor(x, y);
        auto& tile = tiles_[std::make_pair(c.x, c.y)];
        if (!tile) tile = std::make_shared<Tile>();
        tile->setPixel(tileLocal(x), tileLocal(y), Rgba{0.0f, 0.0f, 0.0f, 1.0f});
    }

    void disc(int cx, int cy, int r) {
        for (int y = cy - r; y <= cy + r; ++y) {
            for (int x = cx - r; x <= cx + r; ++x) {
                const int dx = x - cx;
                const int dy = y - cy;
                if (dx * dx + dy * dy <= r * r) plot(x, y);
            }
        }
    }

    void line(int x0, int y0, int x1, int y1, int width) {
        const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        for (int i = 0; i <= steps; ++i) {
            const double t = steps ? static_cast<double>(i) / steps : 0.0;
            disc(static_cast<int>(std::lround(x0 + (x1 - x0) * t)),
                 static_cast<int>(std::lround(y0 + (y1 - y0) * t)), width / 2);
        }
    }

    void box(int x0, int y0, int x1, int y1, int width) {
        line(x0, y0, x1, y0, width);
        line(x1, y0, x1, y1, width);
        line(x1, y1, x0, y1, width);
        line(x0, y1, x0, y0, width);
    }

    void filledBox(int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y) line(x0, y, x1, y, 2);
    }

    void ring(int cx, int cy, int radius, int width) {
        const int steps = 8 * radius;
        for (int i = 0; i <= steps; ++i) {
            const double a = 2.0 * 3.14159265358979 * i / steps;
            disc(static_cast<int>(std::lround(cx + radius * std::cos(a))),
                 static_cast<int>(std::lround(cy + radius * std::sin(a))), width / 2);
        }
    }

    void filledDisc(int cx, int cy, int radius) {
        for (int r = 0; r < radius; ++r) ring(cx, cy, r, 3);
    }

    void diamond(int cx, int cy, int r, int width) {
        line(cx, cy - r, cx + r, cy, width);
        line(cx + r, cy, cx, cy + r, width);
        line(cx, cy + r, cx - r, cy, width);
        line(cx - r, cy, cx, cy - r, width);
    }

    TileGrid freeze() const {
        TileGrid grid;
        for (const auto& [key, tile] : tiles_) {
            grid.set(TileCoord{key.first, key.second}, TileRef(tile));
        }
        return grid;
    }

private:
    std::map<std::pair<int, int>, std::shared_ptr<Tile>> tiles_;
};

// How far the furthest cell of a warp is from what really happened.
//
// The field and not the average, because a mark is carried through the field: a
// warp whose mean is right and whose worst cell is three hundred pixels out
// will put some of a mark three hundred pixels from where it belongs.
int furthestFrom(const CtgWarp& warp, CtgShift truth) {
    int worst = std::max(std::abs(warp.overall.x - truth.x), std::abs(warp.overall.y - truth.y));
    for (const CtgShift& cell : warp.cells) {
        worst = std::max({worst, std::abs(cell.x - truth.x), std::abs(cell.y - truth.y)});
    }
    return worst;
}

// Which shapes to run, when a run is a trace rather than a table.
//
// `--only` is here because ANIMAGE_LATTICE_TRACE prints a hundred lines per
// lattice, and fifteen shapes of that is not something anybody reads. It
// matches on any part of the name: `--only box,` is the four box rows.
const char* g_only = nullptr;

void report(const char* what, const Sheet& before, const Sheet& after, CtgShift truth) {
    if (g_only != nullptr && std::strstr(what, g_only) == nullptr) return;
    const TileGrid a = before.freeze();
    const TileGrid b = after.freeze();

    PixelRect area = drawnBounds(a);
    area = unite(area, drawnBounds(b));

    const CtgShift rung2 = estimateCtgShift({a}, {b}, area);
    const CtgWarp rung4 = estimateCtgLattice({a}, {b}, area);

    char two[32];
    std::snprintf(two, sizeof(two), "%d,%d", rung2.x, rung2.y);
    char four[32];
    std::snprintf(four, sizeof(four), "%d,%d", rung4.overall.x, rung4.overall.y);
    char moved[32];
    std::snprintf(moved, sizeof(moved), "%d,%d", truth.x, truth.y);

    std::printf("  %-36s %9s  %9s  %5d   %9s  %5d  %s\n", what, moved, two,
                std::max(std::abs(rung2.x - truth.x), std::abs(rung2.y - truth.y)), four,
                furthestFrom(rung4, truth), rung4.isUniform() ? "uniform" : "a field");
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) g_only = argv[++i];
    }

    std::printf(
        "\nOne translation, many shapes. `truth` is what the shape was really\n"
        "given, so it is the answer; `worst` is how far the furthest cell of\n"
        "the answer is from it, which is what decides whether a carried mark\n"
        "lands. Read down the `worst` columns and then read the neighbouring\n"
        "shapes, because what a failure is a property *of* is the thing one\n"
        "fixture cannot say.\n\n");
    std::printf("  %-36s %9s  %9s  %5s   %9s  %5s\n", "shape", "truth", "rung 2", "worst",
                "rung 4", "worst");

    constexpr int kMoved = 20;
    const CtgShift kAcross{kMoved, 0};
    const CtgShift kStill{0, 0};

    // Straight, open, and no closed loop anywhere. Every one of these is the
    // case rung four gets right, and they are here so that the failures below
    // have something to be read against.
    {
        Sheet a, b;
        a.line(100, 250, 400, 250, 4);
        b.line(100 + kMoved, 250, 400 + kMoved, 250, 4);
        report("one straight line", a, b, kAcross);
    }
    {
        Sheet a, b;
        a.line(100, 100, 400, 100, 4);
        a.line(100, 400, 400, 400, 4);
        b.line(100 + kMoved, 100, 400 + kMoved, 100, 4);
        b.line(100 + kMoved, 400, 400 + kMoved, 400, 4);
        report("two parallel walls", a, b, kAcross);
    }
    {
        Sheet a, b;
        a.line(100, 400, 400, 400, 4);
        a.line(100, 100, 100, 400, 4);
        b.line(100 + kMoved, 400, 400 + kMoved, 400, 4);
        b.line(100 + kMoved, 100, 100 + kMoved, 400, 4);
        report("L shape, two walls at a corner", a, b, kAcross);
    }
    {
        Sheet a, b;
        a.line(100, 100, 400, 100, 4);
        a.line(100, 400, 400, 400, 4);
        a.line(100, 100, 100, 400, 4);
        b.line(100 + kMoved, 100, 400 + kMoved, 100, 4);
        b.line(100 + kMoved, 400, 400 + kMoved, 400, 4);
        b.line(100 + kMoved, 100, 100 + kMoved, 400, 4);
        report("C shape, three walls of a box", a, b, kAcross);
    }

    // Curved, and closed. A circle has no direction along which it looks like
    // itself, so closing the loop costs nothing here.
    {
        Sheet a, b;
        a.ring(250, 250, 150, 4);
        b.ring(250 + kMoved, 250, 150, 4);
        report("ring", a, b, kAcross);
    }
    {
        Sheet a, b;
        a.ring(250, 250, 150, 4);
        b.ring(250, 250, 140, 4);
        report("ring, shrunk 10 px", a, b, kStill);
    }
    {
        Sheet a, b;
        a.filledDisc(250, 250, 150);
        b.filledDisc(250, 250, 140);
        report("filled disc, shrunk 10 px", a, b, kStill);
    }

    // Closed, straight, and empty in the middle. This is where it goes.
    {
        Sheet a, b;
        a.box(100, 100, 400, 400, 4);
        b.box(100, 100, 400, 400, 4);
        report("box, not moved at all", a, b, kStill);
    }
    {
        Sheet a, b;
        a.box(100, 100, 160, 160, 4);
        b.box(100 + 10, 100, 160 + 10, 160, 4);
        report("box, 60 px across, moved 10 px", a, b, {10, 0});
    }
    {
        Sheet a, b;
        a.box(100, 100, 400, 400, 4);
        b.box(100 + kMoved, 100, 400 + kMoved, 400, 4);
        report("box, 300 px across", a, b, kAcross);
    }
    {
        Sheet a, b;
        a.box(100, 100, 400, 400, 12);
        b.box(100 + kMoved, 100, 400 + kMoved, 400, 12);
        report("box, walls 12 px thick", a, b, kAcross);
    }
    {
        Sheet a, b;
        a.diamond(250, 250, 150, 4);
        b.diamond(250 + kMoved, 250, 150, 4);
        report("diamond, a box turned 45 degrees", a, b, kAcross);
    }
    {
        Sheet a, b;
        a.filledBox(100, 100, 400, 400);
        b.filledBox(100 + kMoved, 100, 400 + kMoved, 400);
        report("box, filled in", a, b, kAcross);
    }
    {
        Sheet a, b;
        a.box(100, 100, 400, 400, 4);
        a.line(100, 250, 400, 250, 4);
        a.line(250, 100, 250, 400, 4);
        b.box(100 + kMoved, 100, 400 + kMoved, 400, 4);
        b.line(100 + kMoved, 250, 400 + kMoved, 250, 4);
        b.line(250 + kMoved, 100, 250 + kMoved, 400, 4);
        report("box with a cross through it", a, b, kAcross);
    }
    {
        Sheet a, b;
        a.box(100, 100, 400, 400, 4);
        b.box(108, 112, 388, 384, 4);
        report("box, redrawn 8-16 px smaller", a, b, kStill);
    }

    // The same box, over and over, at sizes between the one that works and the
    // one that does not.
    //
    // A 60 px box is followed and a 300 px box is not, which leaves the reason
    // in the gap between two rows rather than in a row. These are that gap made
    // readable: one shape, one motion, and the only thing varying is how much of
    // it there is. Where the answer comes apart says what the defect is a
    // property of -- how many nodes lie along a wall, how far a corner is from
    // the middle of one, how many steps it takes to walk there -- and none of
    // those can be told apart from a single failing size.
    //
    // The motion is the same 20 px every other row uses, so these are readable
    // against them. The row at 300 is the same shape as "box, 300 px across"
    // above and should answer the same thing; it is repeated here so the sweep
    // can be read without looking away.
    for (const int across : {60, 100, 140, 180, 220, 260, 300, 400}) {
        Sheet a, b;
        a.box(100, 100, 100 + across, 100 + across, 4);
        b.box(100 + kMoved, 100, 100 + across + kMoved, 100 + across, 4);
        char what[64];
        std::snprintf(what, sizeof(what), "box sweep, %d px across", across);
        report(what, a, b, kAcross);
    }

    std::printf(
        "\nThe last seven rows are one shape's worth of difference apart. Three\n"
        "walls against four, sixty pixels across against three hundred, a\n"
        "cross through the middle or nothing there -- see issue #69, and the\n"
        "handover's \"rung four, and what it took to make it the paper's\".\n");
    return 0;
}
