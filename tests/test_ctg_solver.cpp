// SPDX-License-Identifier: GPL-3.0-or-later
//
// Solving somewhere else. The rules under test are the three the solver exists
// to have: the newest question wins, a fill and a verdict are different
// questions, and what you are looking at goes first.

#include <atomic>
#include <chrono>
#include <thread>

#include "brush.h"
#include "ctg.h"
#include "ctg_solver.h"
#include "testing.h"

using namespace animage;

namespace {

void strokeOn(Document& doc, TrackId track, ImageId image, LayerId layer, float x0, float y0,
              float x1, float y1, float radius, float r, float g, float b) {
    ScopedCommand command(doc, "Stroke");
    BrushSettings settings;
    settings.radius = radius;
    settings.hardness = 0.95f;
    settings.pressure_affects_opacity = false;
    settings.r = r;
    settings.g = g;
    settings.b = b;
    settings.a = 1.0f;
    Brush brush(settings);
    brush.begin(doc, track, image, layer, {x0, y0, 1.0f});
    brush.extend({x1, y1, 1.0f});
    brush.end();
}

// A handful of drawings, each with a box and a mark in it, which is enough to
// give the solver work that takes long enough to be worth queueing.
struct Shot {
    Document doc;
    TrackId track;
    LayerId ink;
    LayerId colour;
    std::vector<ImageId> images;

    explicit Shot(int count) {
        doc.setCanvasSize(600, 480);
        track = doc.addTrack("main");
        colour = doc.addLayer(track, "colour", 0, LayerKind::Ctg);
        ink = doc.addLayer(track, "ink", 1);
        for (int i = 0; i < count; ++i) {
            images.push_back(doc.insertImage(track, static_cast<std::size_t>(i)));
        }

        Layer ctg = *doc.scene().findTrack(track)->findLayer(colour);
        ctg.ctg_sources = {ink};
        doc.updateLayer(track, colour, ctg);

        for (int i = 0; i < count; ++i) {
            const float shift = static_cast<float>(i) * 3.0f;
            box(i, 80 + shift, 80, 500 + shift, 400);
            strokeOn(doc, track, images[static_cast<std::size_t>(i)], colour, 200 + shift, 240,
                     380 + shift, 240, 12.0f, 1.0f, 0.0f, 0.0f);
        }
    }

    void box(int drawing, float left, float top, float right, float bottom) {
        const ImageId image = images[static_cast<std::size_t>(drawing)];
        strokeOn(doc, track, image, ink, left, top, right, top, 2.5f, 0, 0, 0);
        strokeOn(doc, track, image, ink, left, top, left, bottom, 2.5f, 0, 0, 0);
        strokeOn(doc, track, image, ink, right, top, right, bottom, 2.5f, 0, 0, 0);
        strokeOn(doc, track, image, ink, left, bottom, right, bottom, 2.5f, 0, 0, 0);
    }

    CtgKey key(int drawing) const {
        return CtgKey{images[static_cast<std::size_t>(drawing)], colour};
    }

    CtgJob job(int drawing, long long budget = kInteractiveSolveBudget) {
        return ctgJobFor(doc, track, images[static_cast<std::size_t>(drawing)], colour,
                         CtgSettings{}, budget);
    }
};

void aSolveComesBackWithTheAnswer() {
    TEST("a solve run somewhere else comes back with the answer");
    Shot shot(1);
    CtgSolver solver;

    solver.request(shot.key(0), shot.job(0), true);
    solver.waitUntilIdle();

    const std::vector<CtgSolver::Result> done = solver.collect();
    CHECK_EQ(done.size(), std::size_t{1});
    if (done.empty()) return;

    CHECK(done[0].key == shot.key(0));
    CHECK(done[0].wanted_labels);
    CHECK(done[0].fill.valid);

    // The same answer solving in place would have given.
    const CtgFill here = solveCtgFill(shot.doc, shot.track, shot.images[0], shot.colour,
                                      CtgSettings{}, true);
    CHECK_EQ(done[0].fill.inputs, here.inputs);
    CHECK_NEAR(ctgFillPixel(done[0].fill, 300, 150).r, ctgFillPixel(here, 300, 150).r, 0.001);
}

void theNewestQuestionWins() {
    TEST("asking again about a drawing supersedes the question before it");
    Shot shot(1);
    CtgSolver solver;

    const CtgJob first = shot.job(0);

    // A second mark, so the two questions have different answers and the one
    // that comes back can be told apart.
    strokeOn(shot.doc, shot.track, shot.images[0], shot.colour, 120, 120, 160, 120, 8.0f, 0.0f,
             0.0f, 1.0f);
    const CtgJob second = shot.job(0);
    CHECK(first.inputs != second.inputs);

    solver.request(shot.key(0), first, true);
    solver.request(shot.key(0), second, true);
    solver.waitUntilIdle();

    // One answer, and it is the newer one -- whether the first was still in the
    // queue or already running when it was overtaken.
    const std::vector<CtgSolver::Result> done = solver.collect();
    CHECK_EQ(done.size(), std::size_t{1});
    if (done.empty()) return;
    CHECK_EQ(done[0].fill.inputs, second.inputs);
    CHECK_EQ(done[0].fill.colours, 2);
    CHECK(solver.supersededCount() >= 1);
}

void aFillAndAVerdictAreDifferentQuestions() {
    TEST("asking for a judgement does not cancel the fill of the same drawing");
    Shot shot(1);
    CtgSolver solver;

    solver.request(shot.key(0), shot.job(0), true);
    solver.request(shot.key(0), shot.job(0), false, CtgSolver::Priority::Whenever);
    solver.waitUntilIdle();

    const std::vector<CtgSolver::Result> done = solver.collect();
    CHECK_EQ(done.size(), std::size_t{2});

    int pictures = 0;
    int judgements = 0;
    for (const CtgSolver::Result& result : done) {
        if (result.wanted_labels) {
            ++pictures;
            CHECK(result.fill.labels.size() > 0);
        } else {
            ++judgements;
            // The whole point of not asking for the labels: the verdict is a
            // few bytes and the labelling is megabytes.
            CHECK_EQ(result.fill.labels.size(), std::size_t{0});
        }
    }
    CHECK_EQ(pictures, 1);
    CHECK_EQ(judgements, 1);
}

void theDrawingOnScreenGoesFirst() {
    TEST("what you are looking at is solved before the audit of the rest");
    constexpr int kDrawings = 8;
    Shot shot(kDrawings);
    CtgSolver solver;

    // The audit: every drawing, judged, at no particular hurry.
    for (int i = 1; i < kDrawings; ++i) {
        solver.request(shot.key(i), shot.job(i), false, CtgSolver::Priority::Whenever);
    }
    // And then the drawing in front of you.
    solver.request(shot.key(0), shot.job(0), true);

    // At most one background solve can have been picked up before the
    // interactive one arrived, so it may come second and never later.
    int before = 0;
    bool seen = false;
    while (!seen) {
        for (const CtgSolver::Result& result : solver.collect()) {
            if (result.key == shot.key(0)) {
                seen = true;
            } else if (!seen) {
                ++before;
            }
        }
        if (!seen) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(before <= 1);

    solver.waitUntilIdle();
}

void givingUpOnEverythingLeavesNothing() {
    TEST("cancelling leaves nothing queued, running or waiting to be collected");
    Shot shot(6);
    CtgSolver solver;

    for (int i = 0; i < 6; ++i) solver.request(shot.key(i), shot.job(i), true);
    solver.cancelAll();

    CHECK(solver.idle());
    CHECK_EQ(solver.collect().size(), std::size_t{0});

    // And it still works afterwards: cancelling is not a way of breaking it.
    solver.request(shot.key(0), shot.job(0), true);
    solver.waitUntilIdle();
    CHECK_EQ(solver.collect().size(), std::size_t{1});
}

void shuttingDownDoesNotWaitForTheWork() {
    TEST("closing down abandons what is in flight rather than finishing it");
    Shot shot(8);

    const auto started = std::chrono::steady_clock::now();
    {
        CtgSolver solver;
        for (int i = 0; i < 8; ++i) {
            // Uncapped, so each of these is a real solve and not a coarse one.
            solver.request(shot.key(i), shot.job(i, /*budget=*/0), true);
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();

    // Eight full-resolution solves of a 600x480 canvas take far longer than
    // this; the window closing must not wait for them.
    std::printf("      [closed down in %.1f ms]\n", ms);
    CHECK(ms < 2000.0);
}

void aFinishedSolveWakesTheOwnerUp() {
    TEST("a finished solve says so, so nobody has to poll");
    Shot shot(1);
    CtgSolver solver;

    std::atomic<int> woken{0};
    solver.onFinished([&woken] { woken.fetch_add(1); });

    solver.request(shot.key(0), shot.job(0), true);
    solver.waitUntilIdle();
    // The wake-up is fired after the result is stored but not under the lock,
    // so it can arrive a moment after idle. Wait for it rather than race it.
    for (int i = 0; i < 500 && woken.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_EQ(woken.load(), 1);
    CHECK_EQ(solver.collect().size(), std::size_t{1});
}

}  // namespace

int main() {
    std::printf("ctg solver:\n");
    aSolveComesBackWithTheAnswer();
    theNewestQuestionWins();
    aFillAndAVerdictAreDifferentQuestions();
    theDrawingOnScreenGoesFirst();
    givingUpOnEverythingLeavesNothing();
    shuttingDownDoesNotWaitForTheWork();
    aFinishedSolveWakesTheOwnerUp();
    return testing::summarise("ctg solver");
}
