// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ctg_fill.h"  // CtgKey: a drawing and a layer, which is what a frame is
#include "transform.h"

// Somewhere else for a decode to happen.
//
// **`CtgSolver` is the model and this is deliberately the same object with a
// different job in it.** Read that class first: the queue, the superseding, the
// cancelling and "results are collected, not delivered" are all the same
// decisions, taken for the same reasons, and the differences below are the
// interesting part.
//
// It exists because a still could re-derive on the interface thread and a
// sequence cannot. One 300 dpi scan is a decode you can feel; an animatic is a
// decode per frame while somebody drags the playhead, and doing that where the
// program draws is the thing all of this is here to stop.
//
// Three differences from the solver, each of which is a decision:
//
//   - **A job names a path, not a document.** `CtgJob` exists because a solve
//     must not look at a document being edited; a decode never had that
//     problem, because what it reads is a file. What it costs instead is that
//     the path has to be resolved *before* the job is queued -- where an import
//     lives depends on whether the project has been saved, which is MainWindow
//     state and moves on the interface thread.
//   - **Nothing is abandoned mid-decode.** A superseded max-flow is told to
//     give up because it may run for a second and a half; a decode is tens of
//     milliseconds, so it finishes and its answer is dropped. Same outcome,
//     no flag to thread through a QImage.
//   - **One kind of question.** The solver has to tell a fill from a judgement
//     because one must not cancel the other. There is one thing to ask here, so
//     the identity of a request is the drawing and the layer and nothing else.
//
// This is in `src/app/` and not in `core` for the usual reason: `core` is the
// model and knows nothing about QImage or about bytes on disk. What that gives
// up is that the queue cannot be tested headlessly the way `CtgSolver`'s is --
// the tests for it drive a real window, which is where the paths come from
// anyway.
class ReferenceDecoder {
public:
    // Everything one decode needs, and it holds no reference to anything that
    // could be edited while it runs.
    struct Job {
        QString path;
        // What the layer calls this file, carried through untouched so that a
        // failure can be reported in the name somebody typed rather than in a
        // path they never saw. The decode does not read it.
        QString name;
        // Applied after the decode, so what comes back is already placed and
        // the compositor is handed a plain grid. See docs/importing.md: this is
        // the whole of why a placement can be stored rather than baked.
        animage::Transform placement;
    };

    struct Result {
        animage::CtgKey key;
        // The placement it was derived under, carried back rather than looked
        // up again. What the layer says now may not be what it said when this
        // was asked for, and installing a frame under the current placement
        // when it was made under the old one is exactly the picture-of-where-
        // the-import-used-to-be that the cache refuses to serve.
        animage::Transform under;
        animage::TileGrid tiles;
        QString name;
        bool ok = false;
        QString trouble;  // why not, when `ok` is false
    };

    // One worker, and the reasoning is the solver's rather than a copy of its
    // number: what is wanted is the frame on screen soon, not several frames at
    // once. A handful can be outstanding -- two ghosts and a second track --
    // and serialising them costs nothing but the order they arrive in. Each
    // worker also holds a decoded frame while it builds tiles, so this is the
    // peak as well as the parallelism.
    explicit ReferenceDecoder(int workers = 1);
    ~ReferenceDecoder();

    ReferenceDecoder(const ReferenceDecoder&) = delete;
    ReferenceDecoder& operator=(const ReferenceDecoder&) = delete;

    // Queue one. Supersedes any earlier request about the same drawing and
    // layer, queued or running, because the earlier one describes the layer at a
    // placement it has since left.
    void request(const animage::CtgKey& key, Job job);

    // Everything that has finished since the last call.
    std::vector<Result> collect();

    // Call one off: the drawing it is about has been left, which while playing
    // is twenty-four questions a second nobody wants the answer to.
    void cancel(const animage::CtgKey& key);

    // Forget everything queued and give up on everything running. For when the
    // document underneath is being replaced -- opening a project, closing one --
    // where every answer in flight is about a document that will not exist.
    void cancelAll();

    bool idle() const;

    // For tests, and for shutting down in an orderly way.
    void waitUntilIdle();

    // How many decodes produced an answer, and how many were thrown away before
    // they could. Exposed for the reason CtgFillCache counts its stores: "did
    // that decode again?" has no honest answer but a count, and a wrong key
    // does not fail, it only gets slow.
    std::uint64_t decodeCount() const;
    std::uint64_t supersededCount() const;

private:
    struct Request {
        animage::CtgKey key;
        Job job;
        std::shared_ptr<std::atomic<bool>> abandon;
    };

    // A request that has been picked up. Only what is needed to recognise it
    // and to call it off -- the job itself is with the worker running it.
    struct Active {
        animage::CtgKey key;
        std::shared_ptr<std::atomic<bool>> abandon;
    };

    void run();
    bool takeNext(Request& out);

    mutable std::mutex mutex_;
    std::condition_variable work_;
    std::condition_variable idle_;

    std::deque<Request> queued_;
    std::vector<Active> running_;
    std::vector<Result> finished_;

    std::vector<std::thread> workers_;
    bool stopping_ = false;
    std::uint64_t decodes_ = 0;
    std::uint64_t superseded_ = 0;
};
