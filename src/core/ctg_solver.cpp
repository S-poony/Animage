// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctg_solver.h"

#include <algorithm>
#include <new>
#include <utility>

namespace animage {

CtgSolver::CtgSolver(int workers) {
    workers = std::max(1, workers);
    workers_.reserve(static_cast<std::size_t>(workers));
    for (int i = 0; i < workers; ++i) workers_.emplace_back([this] { run(); });
}

CtgSolver::~CtgSolver() {
    {
        std::lock_guard<std::mutex> held(mutex_);
        stopping_ = true;
        // Everything in flight is now worth nothing, and a solve that is not
        // told so goes on holding the process open for a second or two while
        // the window is already gone.
        for (Active& active : running_) active.abandon->store(true);
        now_.clear();
        whenever_.clear();
    }
    work_.notify_all();
    for (std::thread& worker : workers_) worker.join();
}

void CtgSolver::request(const CtgKey& key, CtgJob job, bool want_labels, Priority priority) {
    {
        std::lock_guard<std::mutex> held(mutex_);
        if (stopping_) return;

        const auto supersede = [&](std::deque<Request>& queue) {
            const auto stale = std::remove_if(
                queue.begin(), queue.end(),
                [&](const Request& queued) { return sameQuestion(queued, key, want_labels); });
            superseded_ += static_cast<std::uint64_t>(std::distance(stale, queue.end()));
            queue.erase(stale, queue.end());
        };
        supersede(now_);
        supersede(whenever_);

        // And the one already running, which is the case that matters: a
        // half-finished answer to a question about a drawing two strokes ago is
        // not a lesser answer, it is the wrong one. It counts itself as
        // superseded when it notices and stops.
        for (Active& active : running_) {
            if (sameQuestion(active, key, want_labels)) active.abandon->store(true);
        }

        Request queued;
        queued.key = key;
        queued.wanted_labels = want_labels;
        queued.priority = priority;
        queued.job = std::move(job);
        queued.abandon = std::make_shared<std::atomic<bool>>(false);
        (priority == Priority::Now ? now_ : whenever_).push_back(std::move(queued));
    }
    work_.notify_one();
}

bool CtgSolver::takeNext(Request& out) {
    std::unique_lock<std::mutex> held(mutex_);
    work_.wait(held, [this] { return stopping_ || !now_.empty() || !whenever_.empty(); });
    if (stopping_) return false;

    std::deque<Request>& queue = now_.empty() ? whenever_ : now_;
    out = std::move(queue.front());
    queue.pop_front();
    running_.push_back({out.key, out.wanted_labels, out.abandon});
    return true;
}

void CtgSolver::run() {
    while (true) {
        Request taken;
        if (!takeNext(taken)) return;

        // A solve that cannot get its memory fails that solve, and nothing else.
        //
        // An exception leaving a thread function is std::terminate: the whole
        // program gone, with no dialog and no crash report and no chance to
        // save, because one drawing's colour did not work out. Everything a
        // solve touches is its own -- the job is a copy, the fill is derived,
        // and nothing in the document is half written when this throws -- so
        // giving up on one is an ordinary outcome here in a way it is almost
        // nowhere else.
        //
        // bad_alloc only, on purpose. It is the failure this is for and the one
        // that is safe to swallow; anything else escaping a solve is a bug in
        // the solve and should still be loud. And it is a backstop rather than a
        // fix: an allocation large enough to fail is one that should not have
        // been asked for, and on a machine with room to swap it will succeed and
        // simply be slow. See ctgBarrier for the one that was asking.
        //
        // Counted, not reported. Whoever asked is left holding a question with
        // no answer coming, which it re-asks the next time the drawing changes;
        // until then the fill on screen is the one it already had, which is the
        // same thing that happens while any solve is still running.
        CtgFill fill;
        bool failed = false;
        try {
            fill = solveCtgJob(taken.job, taken.wanted_labels, taken.abandon.get());
        } catch (const std::bad_alloc&) {
            failed = true;
        }

        std::function<void()> notify;
        {
            std::lock_guard<std::mutex> held(mutex_);
            const auto mine = std::find_if(
                running_.begin(), running_.end(),
                [&](const Active& active) { return active.abandon == taken.abandon; });
            if (mine != running_.end()) running_.erase(mine);

            // An abandoned solve returns an invalid fill, and an invalid fill
            // is not an answer to anything. Dropping it here rather than making
            // the caller check is the same rule as everywhere else: a partial
            // result must not be able to reach the document.
            if (fill.valid && !taken.abandon->load()) {
                finished_.push_back(
                    {taken.key, std::move(fill), taken.wanted_labels, taken.priority});
                ++solves_;
                notify = notify_;
            } else if (failed) {
                ++failed_;
            } else {
                ++superseded_;
            }
        }
        idle_.notify_all();

        if (notify) notify();
    }
}

std::vector<CtgSolver::Result> CtgSolver::collect() {
    std::lock_guard<std::mutex> held(mutex_);
    return std::exchange(finished_, {});
}

void CtgSolver::onFinished(std::function<void()> notify) {
    std::lock_guard<std::mutex> held(mutex_);
    notify_ = std::move(notify);
}

void CtgSolver::cancel(const CtgKey& key, bool want_labels) {
    std::lock_guard<std::mutex> held(mutex_);

    const auto drop = [&](std::deque<Request>& queue) {
        const auto stale = std::remove_if(
            queue.begin(), queue.end(),
            [&](const Request& queued) { return sameQuestion(queued, key, want_labels); });
        superseded_ += static_cast<std::uint64_t>(std::distance(stale, queue.end()));
        queue.erase(stale, queue.end());
    };
    drop(now_);
    drop(whenever_);

    for (Active& active : running_) {
        if (sameQuestion(active, key, want_labels)) active.abandon->store(true);
    }

    // A result already collected into finished_ is left alone. It is an answer
    // to a question that was asked, and whoever collects it decides whether it
    // is still wanted -- which they have to be able to do anyway, because a
    // result can be in flight while this runs.
}

void CtgSolver::cancelAll() {
    {
        std::lock_guard<std::mutex> held(mutex_);
        superseded_ += static_cast<std::uint64_t>(now_.size() + whenever_.size());
        now_.clear();
        whenever_.clear();
        for (Active& active : running_) active.abandon->store(true);
        finished_.clear();
    }
    waitUntilIdle();
}

bool CtgSolver::idle() const {
    std::lock_guard<std::mutex> held(mutex_);
    return now_.empty() && whenever_.empty() && running_.empty();
}

void CtgSolver::waitUntilIdle() {
    std::unique_lock<std::mutex> held(mutex_);
    idle_.wait(held, [this] { return now_.empty() && whenever_.empty() && running_.empty(); });
}

std::uint64_t CtgSolver::solveCount() const {
    std::lock_guard<std::mutex> held(mutex_);
    return solves_;
}

std::uint64_t CtgSolver::supersededCount() const {
    std::lock_guard<std::mutex> held(mutex_);
    return superseded_;
}

std::uint64_t CtgSolver::failedCount() const {
    std::lock_guard<std::mutex> held(mutex_);
    return failed_;
}

}  // namespace animage
