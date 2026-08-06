// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ctg_fill.h"
#include "ctg_job.h"

namespace animage {

// Somewhere else for a max-flow to happen.
//
// This owns worker threads and a queue of jobs, and it is deliberately Qt-free
// and knows nothing about a document: a job is a complete description of one
// solve (see ctg_job.h), and a fill is a complete description of one answer, so
// nothing shared has to be locked and the interface thread never waits.
//
// Three rules, and each of them is a bug this design is choosing not to have:
//
//   - **The newest question wins.** Asking again about a drawing supersedes any
//     earlier question about it, queued or already running, because the earlier
//     one describes the drawing as it was two strokes ago and nobody wants that
//     answer. A superseded solve is told to give up and produces nothing.
//   - **A fill and a verdict are different questions.** The audit judges every
//     drawing coarsely and keeps a few bytes; the canvas wants the picture. One
//     may not cancel the other, so the identity of a request is the drawing,
//     the layer *and* which of the two was asked for.
//   - **What you are looking at goes first.** The audit is a whole track's
//     worth of work and the canvas is one drawing, so an interactive request
//     jumps the queue. Without this, colouring a shot means every stroke
//     waiting behind the judgement of ninety-five drawings nobody is looking
//     at.
//
// Results are not delivered, they are collected: the owning thread calls
// collect() when it is ready to install them, which is what keeps every write
// to the document on the thread that owns the document. `onFinished` exists
// only to save that thread from polling.
class CtgSolver {
public:
    enum class Priority {
        Now,       // the drawing on screen
        Whenever,  // the whole-track audit
    };

    struct Result {
        CtgKey key;
        CtgFill fill;
        bool wanted_tiles = false;
        Priority priority = Priority::Now;
    };

    // One worker by default. The canvas wants an answer soon rather than
    // several answers at once, and a solve is already parallel inside itself
    // where it is worth being -- the barrier is composited by a thread pool.
    // More workers is for the audit, and the audit is cheap.
    explicit CtgSolver(int workers = 1);
    ~CtgSolver();

    CtgSolver(const CtgSolver&) = delete;
    CtgSolver& operator=(const CtgSolver&) = delete;

    // Queue one. Supersedes any earlier request for the same drawing, layer and
    // kind of answer.
    void request(const CtgKey& key, CtgJob job, bool want_tiles,
                 Priority priority = Priority::Now);

    // Everything that has finished since the last call.
    std::vector<Result> collect();

    // Called on a worker thread each time something finishes. Keep it to a
    // wake-up: anything that touches a document from in here is a race.
    void onFinished(std::function<void()> notify);

    // Call one off. What playing a coloured shot needs: the drawing that was on
    // screen a frame ago is a question nobody is waiting for the answer to any
    // more, and without this the queue fills faster than it drains.
    void cancel(const CtgKey& key, bool want_tiles);

    // Forget everything queued and give up on everything running. Used when the
    // document underneath is being replaced -- opening a project, closing one --
    // where every answer in flight is about a document that will not exist.
    void cancelAll();

    // Nothing queued and nothing running.
    bool idle() const;

    // For tests, and for shutting down in an orderly way.
    void waitUntilIdle();

    // How many solves produced an answer, and how many were thrown away before
    // they could. Exposed for the same reason CtgFillCache counts its stores:
    // "did that supersede?" has no honest answer but a count.
    std::uint64_t solveCount() const;
    std::uint64_t supersededCount() const;

private:
    struct Request {
        CtgKey key;
        bool wanted_tiles = false;
        Priority priority = Priority::Now;
        CtgJob job;
        std::shared_ptr<std::atomic<bool>> abandon;
    };

    // A request that has been picked up. Only what is needed to recognise it
    // and to call it off -- the job itself is with the worker running it.
    struct Active {
        CtgKey key;
        bool wanted_tiles = false;
        std::shared_ptr<std::atomic<bool>> abandon;
    };

    // What makes two requests the same question: the drawing, the layer, and
    // whether a picture or a judgement was asked for.
    template <typename T>
    static bool sameQuestion(const T& a, const CtgKey& key, bool tiles) {
        return a.key == key && a.wanted_tiles == tiles;
    }

    void run();
    bool takeNext(Request& out);

    mutable std::mutex mutex_;
    std::condition_variable work_;
    std::condition_variable idle_;

    std::deque<Request> now_;
    std::deque<Request> whenever_;
    std::vector<Active> running_;
    std::vector<Result> finished_;

    std::function<void()> notify_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
    std::uint64_t solves_ = 0;
    std::uint64_t superseded_ = 0;
};

}  // namespace animage
