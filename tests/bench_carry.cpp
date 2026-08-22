// SPDX-License-Identifier: GPL-3.0-or-later
//
// How far a mark can be carried before it stops landing.
//
// This is the first rung of part 2 of docs/scribbles-through-time.md, and the
// note is explicit that it comes before any of the machinery: "Carry unchanged.
// On twos and threes it may simply be enough -- measure how often before
// building anything else, because the answer decides whether the rest is worth
// it."
//
// So: a shape that moves a known amount per drawing, a mark made on the first
// drawing only, and the fill measured on each of the ones that inherit it.
// Three numbers per drawing:
//
//   coverage -- of the shape's inside, how much took the mark's colour. Below
//               one, the fill has stopped short of what it was meant to fill.
//   leak     -- of the world outside the shape, how much took it anyway. Above
//               zero, the colour is somewhere it does not belong.
//   spread   -- how much region the worst mark won for each pixel of itself. A
//               flag was built on this and taken out again; the number stays,
//               because it is what the next attempt has to beat.
//
// Run it by hand:  ./build/tests/bench_carry

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "brush.h"
#include "ctg.h"
#include "document.h"

using namespace animage;
using Clock = std::chrono::steady_clock;

namespace {

// What the warp decided, in one column.
//
// Rung three's answer is a field, so "how far the drawing moved" stopped being
// the whole of it: this is how many distinct shifts the field holds and how far
// the furthest of them is from the drawing's own answer. A bare number is a
// uniform warp, which is rung two -- and on a case where every region agrees
// that is the right answer and not a failure to find one.
std::string decided(const CtgWarp& warp) {
    char text[64];
    if (warp.isUniform()) {
        std::snprintf(text, sizeof(text), "%d", warp.overall.x);
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
    std::snprintf(text, sizeof(text), "%d/%dx%d", warp.overall.x,
                  static_cast<int>(distinct.size()), furthest);
    return text;
}

constexpr int kCanvasWidth = 900;
constexpr int kCanvasHeight = 700;

// The shape: a box with a hole in one wall, because a closed shape is the case
// a paint bucket already does and the gap is what the solver is for. Drawn at
// `shift` pixels to the right of where it started.
struct Shape {
    int left = 250;
    int top = 200;
    int width = 300;
    int height = 300;
    int gap = 60;  // in the bottom wall, centred

    PixelRect at(int shift) const { return {left + shift, top, width, height}; }
};

void strokeOn(Document& doc, TrackId track, ImageId image, LayerId layer, float x0, float y0,
              float x1, float y1, float radius, float r, float g, float b, bool label) {
    ScopedCommand command(doc, "Stroke");
    BrushSettings settings;
    settings.radius = radius;
    settings.hardness = 0.95f;
    settings.pressure_affects_opacity = false;
    settings.label = label;
    settings.r = r;
    settings.g = g;
    settings.b = b;
    settings.a = 1.0f;
    Brush brush(settings);
    brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
    brush.extend({x1, y1, 1.0f});
    brush.end();
}

void drawShape(Document& doc, TrackId track, ImageId image, LayerId ink, const Shape& shape,
               int shift) {
    const PixelRect box = shape.at(shift);
    const float l = static_cast<float>(box.x);
    const float t = static_cast<float>(box.y);
    const float r = static_cast<float>(box.x + box.width);
    const float b = static_cast<float>(box.y + box.height);
    const float gap_from = (l + r) * 0.5f - static_cast<float>(shape.gap) * 0.5f;
    const float gap_to = (l + r) * 0.5f + static_cast<float>(shape.gap) * 0.5f;

    strokeOn(doc, track, image, ink, l, t, r, t, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, l, t, l, b, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, r, t, r, b, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, l, b, gap_from, b, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, gap_to, b, r, b, 2.5f, 0, 0, 0, false);
}

struct Landing {
    double coverage = 0.0;
    double leak = 0.0;
    float spread = 0.0f;
};

// How much of the shape's inside took the mark's colour, and how much of the
// outside did. Read a few pixels in from the walls, because the boundary itself
// is where the cut runs and neither answer is meaningful there.
Landing measure(const CtgFill& fill, const PixelRect& box) {
    constexpr int kInset = 8;
    long long inside_total = 0;
    long long inside_red = 0;
    long long outside_total = 0;
    long long outside_red = 0;

    for (int y = 0; y < kCanvasHeight; y += 2) {
        for (int x = 0; x < kCanvasWidth; x += 2) {
            const bool inside = x > box.x + kInset && x < box.x + box.width - kInset &&
                                y > box.y + kInset && y < box.y + box.height - kInset;
            const bool outside = x < box.x - kInset || x > box.x + box.width + kInset ||
                                 y < box.y - kInset || y > box.y + box.height + kInset;
            if (!inside && !outside) continue;

            const Rgba pixel = ctgFillPixel(fill, x, y);
            const bool red = pixel.a > 0.5f && pixel.r > 0.5f && pixel.g < 0.3f;
            if (inside) {
                ++inside_total;
                inside_red += red ? 1 : 0;
            } else {
                ++outside_total;
                outside_red += red ? 1 : 0;
            }
        }
    }

    Landing out;
    out.coverage = inside_total ? static_cast<double>(inside_red) / inside_total : 0.0;
    out.leak = outside_total ? static_cast<double>(outside_red) / outside_total : 0.0;
    out.spread = fill.spread;
    return out;
}

// One run: `count` drawings, the shape moving `step` pixels between each, a mark
// of radius `mark` drawn on the first drawing only and carried to the rest.
void carryAcross(int count, int step, float mark, bool follow) {
    Document doc;
    doc.setCanvasSize(kCanvasWidth, kCanvasHeight);
    const TrackId track = doc.addTrack("main");
    const LayerId colour = doc.addLayer(track, "colour", 0, LayerKind::Ctg);
    const LayerId ink = doc.addLayer(track, "ink", 1);
    {
        Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
        settings.ctg_sources = {ink};
        settings.ctg_follow_motion = follow;
        doc.updateLayer(track, colour, settings);
    }

    const Shape shape;
    std::vector<ImageId> drawings;
    for (int i = 0; i < count; ++i) {
        drawings.push_back(doc.insertImage(track, static_cast<std::size_t>(i)));
        drawShape(doc, track, drawings.back(), ink, shape, i * step);
    }

    // The mark, on the first drawing only. Across the middle of the shape,
    // which is where a colourist puts one.
    const PixelRect first = shape.at(0);
    const float middle_y = static_cast<float>(first.y + first.height / 2);
    strokeOn(doc, track, drawings.front(), colour,
             static_cast<float>(first.x + first.width / 4), middle_y,
             static_cast<float>(first.x + 3 * first.width / 4), middle_y, mark, 1.0f, 0.0f,
             0.0f, true);

    std::printf("  shape %dx%d, gap %d, mark radius %.0f, moving %d px a drawing, marks %s\n",
                shape.width, shape.height, shape.gap, static_cast<double>(mark), step,
                follow ? "follow" : "stay");
    std::printf("    drawing   shift   coverage    leak    spread   moved\n");

    for (int i = 0; i < count; ++i) {
        const CtgJob job = ctgJobFor(doc, track, drawings[static_cast<std::size_t>(i)], colour,
                                     CtgSettings{}, kFullSolveBudget);
        const CtgFill fill = solveCtgJob(job, true);
        const Landing landed = measure(fill, shape.at(i * step));
        std::printf("    %5d   %5d    %7.1f%%  %6.1f%%   %6.2f   %9s\n", i + 1, i * step,
                    landed.coverage * 100.0, landed.leak * 100.0,
                    static_cast<double>(landed.spread), decided(fill.carried_by).c_str());
    }
    std::printf("\n");
}

// The case the first one cannot see: two regions side by side, each with its own
// mark, and both marks carried over a shape that moves.
//
// With one region and open paper around it, a mark that has slid half out of its
// shape still fills the shape -- there is nothing for it to be wrong about, so
// coverage stays at 100% until the mark has left the shape entirely. Real line
// art is not like that. The moment a carried mark crosses into the *next*
// region there is a colour in a place somebody has to go and fix, and the
// majority rule decides which colour a region takes rather than whether it takes
// one at all. This is where carrying unchanged actually breaks.
struct Divided {
    int left = 250;
    int top = 200;
    int width = 300;
    int height = 300;

    PixelRect at(int shift) const { return {left + shift, top, width, height}; }
    // The dividing wall, down the middle.
    int divider(int shift) const { return left + shift + width / 2; }
};

void drawDivided(Document& doc, TrackId track, ImageId image, LayerId ink, const Divided& box,
                 int shift) {
    const PixelRect at = box.at(shift);
    const float l = static_cast<float>(at.x);
    const float t = static_cast<float>(at.y);
    const float r = static_cast<float>(at.x + at.width);
    const float b = static_cast<float>(at.y + at.height);
    const float mid = static_cast<float>(box.divider(shift));

    strokeOn(doc, track, image, ink, l, t, r, t, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, l, b, r, b, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, l, t, l, b, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, r, t, r, b, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, mid, t, mid, b, 2.5f, 0, 0, 0, false);
}

// What fraction of a rectangle carries a given label, sampled every other pixel.
double fractionOf(const CtgFill& fill, const PixelRect& area, bool red) {
    long long total = 0;
    long long matched = 0;
    for (int y = area.y; y < area.y + area.height; y += 2) {
        for (int x = area.x; x < area.x + area.width; x += 2) {
            ++total;
            const Rgba pixel = ctgFillPixel(fill, x, y);
            if (pixel.a <= 0.5f) continue;
            const bool is_red = pixel.r > 0.5f && pixel.b < 0.3f;
            const bool is_blue = pixel.b > 0.5f && pixel.r < 0.3f;
            if (red ? is_red : is_blue) ++matched;
        }
    }
    return total ? static_cast<double>(matched) / total : 0.0;
}

// The case rung three exists for: two shapes that do not move together.
//
// Everything above moves in one piece, so one translation is the whole truth
// and rung three can only agree with rung two or be wrong. Cel animation is not
// like that -- an arm goes one way while the body goes another -- and one
// translation then has to be wrong about one of them, whichever way it goes.
//
// It is also the failure that was reported against rung two from the other end:
// a global search takes its resolution from the box round everything drawn, so
// something scribbled a long way off made the carrying of a mark somewhere else
// worse. Here the two shapes are far apart on purpose.
struct Apart {
    int top = 200;
    int height = 260;
    int width = 220;
    int still_left = 60;
    int moving_left = 560;

    PixelRect still() const { return {still_left, top, width, height}; }
    PixelRect moving(int shift) const { return {moving_left + shift, top, width, height}; }
};

void drawClosed(Document& doc, TrackId track, ImageId image, LayerId ink, const PixelRect& box) {
    const float l = static_cast<float>(box.x);
    const float t = static_cast<float>(box.y);
    const float r = static_cast<float>(box.x + box.width);
    const float b = static_cast<float>(box.y + box.height);
    strokeOn(doc, track, image, ink, l, t, r, t, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, l, b, r, b, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, l, t, l, b, 2.5f, 0, 0, 0, false);
    strokeOn(doc, track, image, ink, r, t, r, b, 2.5f, 0, 0, 0, false);
}

void carryAcrossApart(int count, int step, float mark, bool follow) {
    Document doc;
    doc.setCanvasSize(kCanvasWidth, kCanvasHeight);
    const TrackId track = doc.addTrack("main");
    const LayerId colour = doc.addLayer(track, "colour", 0, LayerKind::Ctg);
    const LayerId ink = doc.addLayer(track, "ink", 1);
    {
        Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
        settings.ctg_sources = {ink};
        settings.ctg_follow_motion = follow;
        doc.updateLayer(track, colour, settings);
    }

    const Apart shapes;
    std::vector<ImageId> drawings;
    for (int i = 0; i < count; ++i) {
        drawings.push_back(doc.insertImage(track, static_cast<std::size_t>(i)));
        drawClosed(doc, track, drawings.back(), ink, shapes.still());
        drawClosed(doc, track, drawings.back(), ink, shapes.moving(i * step));
    }

    // A mark in each, on the first drawing only. Red in the one that stays,
    // blue in the one that goes.
    const PixelRect still = shapes.still();
    const PixelRect moving = shapes.moving(0);
    const float middle_y = static_cast<float>(still.y + still.height / 2);
    strokeOn(doc, track, drawings.front(), colour, static_cast<float>(still.x + still.width / 4),
             middle_y, static_cast<float>(still.x + 3 * still.width / 4), middle_y, mark, 1.0f,
             0.0f, 0.0f, true);
    strokeOn(doc, track, drawings.front(), colour,
             static_cast<float>(moving.x + moving.width / 4), middle_y,
             static_cast<float>(moving.x + 3 * moving.width / 4), middle_y, mark, 0.0f, 0.0f,
             1.0f, true);

    std::printf("  two %dx%d shapes %d px apart, one still, the other moving %d px a drawing, "
                "marks %s\n",
                shapes.width, shapes.height, shapes.moving_left - shapes.still_left - shapes.width,
                step, follow ? "follow" : "stay");
    std::printf("    drawing   shift    still red   moving blue   spread     decided\n");

    constexpr int kInset = 12;
    for (int i = 0; i < count; ++i) {
        const CtgJob job = ctgJobFor(doc, track, drawings[static_cast<std::size_t>(i)], colour,
                                     CtgSettings{}, kFullSolveBudget);
        const CtgFill fill = solveCtgJob(job, true);

        const PixelRect a{still.x + kInset, still.y + kInset, still.width - 2 * kInset,
                          still.height - 2 * kInset};
        const PixelRect at = shapes.moving(i * step);
        const PixelRect b{at.x + kInset, at.y + kInset, at.width - 2 * kInset,
                          at.height - 2 * kInset};

        std::printf("    %5d   %5d      %7.1f%%       %7.1f%%   %6.2f   %9s\n", i + 1, i * step,
                    fractionOf(fill, a, true) * 100.0, fractionOf(fill, b, false) * 100.0,
                    static_cast<double>(fill.spread), decided(fill.carried_by).c_str());
    }
    std::printf("\n");
}

void carryAcrossDivided(int count, int step, float mark, bool neighbour_marked,
                        bool follow) {
    Document doc;
    doc.setCanvasSize(kCanvasWidth, kCanvasHeight);
    const TrackId track = doc.addTrack("main");
    const LayerId colour = doc.addLayer(track, "colour", 0, LayerKind::Ctg);
    const LayerId ink = doc.addLayer(track, "ink", 1);
    {
        Layer settings = *doc.scene().findTrack(track)->findLayer(colour);
        settings.ctg_sources = {ink};
        settings.ctg_follow_motion = follow;
        doc.updateLayer(track, colour, settings);
    }

    const Divided box;
    std::vector<ImageId> drawings;
    for (int i = 0; i < count; ++i) {
        drawings.push_back(doc.insertImage(track, static_cast<std::size_t>(i)));
        drawDivided(doc, track, drawings.back(), ink, box, i * step);
    }

    // One mark in each half, on the first drawing only.
    const PixelRect first = box.at(0);
    const float middle_y = static_cast<float>(first.y + first.height / 2);
    const float quarter = static_cast<float>(first.width) * 0.25f;
    strokeOn(doc, track, drawings.front(), colour, static_cast<float>(first.x) + quarter * 0.5f,
             middle_y, static_cast<float>(first.x) + quarter * 1.5f, middle_y, mark, 1.0f, 0.0f,
             0.0f, true);
    if (neighbour_marked) {
        strokeOn(doc, track, drawings.front(), colour,
                 static_cast<float>(first.x) + quarter * 2.5f, middle_y,
                 static_cast<float>(first.x) + quarter * 3.5f, middle_y, mark, 0.0f, 0.0f, 1.0f,
                 true);
    }

    std::printf("  two %dx%d halves, %s, radius %.0f, moving %d px a drawing, marks %s\n",
                box.width / 2, box.height,
                neighbour_marked ? "a mark in each" : "a mark in the left half only",
                static_cast<double>(mark), step, follow ? "follow" : "stay");
    std::printf(
        "    drawing   shift    left red   right blue   right red   spread   moved\n");

    constexpr int kInset = 10;
    for (int i = 0; i < count; ++i) {
        const CtgJob job = ctgJobFor(doc, track, drawings[static_cast<std::size_t>(i)], colour,
                                     CtgSettings{}, kFullSolveBudget);
        const CtgFill fill = solveCtgJob(job, true);

        const PixelRect at = box.at(i * step);
        const int mid = box.divider(i * step);
        const PixelRect left_half{at.x + kInset, at.y + kInset, mid - at.x - 2 * kInset,
                                  at.height - 2 * kInset};
        const PixelRect right_half{mid + kInset, at.y + kInset, at.x + at.width - mid - 2 * kInset,
                                   at.height - 2 * kInset};

        std::printf("    %5d   %5d     %7.1f%%     %7.1f%%    %7.1f%%   %6.2f   %9s\n",
                    i + 1, i * step, fractionOf(fill, left_half, true) * 100.0,
                    fractionOf(fill, right_half, false) * 100.0,
                    fractionOf(fill, right_half, true) * 100.0,
                    static_cast<double>(fill.spread), decided(fill.carried_by).c_str());
    }
    std::printf("\n");
}

}  // namespace

int main() {
    std::printf(
        "Carrying a mark unchanged, against a shape that moves.\n\n"
        "The mark is drawn on drawing 1 and inherited by the rest. Coverage is how\n"
        "much of the shape it fills there; leak is how much of the world outside\n"
        "the shape it fills as well. What is being asked is how much motion a mark\n"
        "survives with nothing moving it -- which is what decides whether anything\n"
        "past 'carry it unchanged' is worth building.\n\n");

    // Everything twice: with the marks left where they were drawn, and with the
    // layer moving them to follow the line art. `moved` is how far the solve
    // decided the drawing had gone, which for these is exactly the shift, so
    // the estimate can be read against the truth in the same row.
    const auto started = Clock::now();
    for (bool follow : {false, true}) {
        for (float mark : {30.0f, 12.0f}) {
            for (int step : {0, 20, 40, 80}) {
                carryAcross(6, step, mark, follow);
            }
        }
    }

    std::printf(
        "\nAnd with a neighbour to be wrong about: two halves, a mark in each.\n"
        "Left red and right blue are the fill being right; right red is the\n"
        "colour of one region landing in the other, which is the failure the\n"
        "single-shape table above cannot show.\n\n");
    for (bool follow : {false, true}) {
        for (int step : {20, 40, 80}) {
            carryAcrossDivided(6, step, 30.0f, true, follow);
        }
    }

    std::printf(
        "\nAnd the same with nothing defending the neighbouring region: only the\n"
        "left half is marked. Right red is then the colour landing in a region it\n"
        "was never meant for -- the failure the design note calls 'the wrong region\n"
        "of about the right size', which no flag catches.\n\n");
    // Moving the other way, so that the dividing wall slides *across* the mark
    // rather than away from it. This is the arrangement that puts a carried mark
    // in a region that is not its own, and it is the only one that can.
    for (bool follow : {false, true}) {
        for (int step : {-20, -40}) {
            carryAcrossDivided(6, step, 30.0f, false, follow);
        }
    }
    std::printf(
        "\nAnd two shapes that do not move together, which is what one translation\n"
        "for the whole drawing cannot be right about. Still red and moving blue are\n"
        "each shape keeping its own colour; one number has to lose one of them.\n\n");
    for (bool follow : {false, true}) {
        for (int step : {20, 40, 80}) {
            carryAcrossApart(6, step, 30.0f, follow);
        }
    }

    std::printf("all of it in %.1f s\n",
                std::chrono::duration<double>(Clock::now() - started).count());
    return 0;
}
