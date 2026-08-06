// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctg_solver.h"

#include <algorithm>
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

void CtgSolver::request(const CtgKey& key, CtgJob job, bool want_tiles, Priority priority) {
    {
        std::lock_guard<std::mutex> held(mutex_);
        if (stopping_) return;

        const auto supersede = [&](std::deque<Request>& queue) {
            const auto stale = std::remove_if(
                queue.begin(), queue.end(),
                [&](const Request& queued) { return sameQuestion(queued, key, want_tiles); });
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
            if (sameQuestion(active, key, want_tiles)) active.abandon->store(true);
        }

        Request queued;
        queued.key = key;
        queued.wanted_tiles = want_tiles;
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
    running_.push_back({out.key, out.wanted_tiles, out.abandon});
    return true;
}

void CtgSolver::run() {
    while (true) {
        Request taken;
        if (!takeNext(taken)) return;

        CtgFill fill = solveCtgJob(taken.job, taken.wanted_tiles, taken.abandon.get());

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
                    {taken.key, std::move(fill), taken.wanted_tiles, taken.priority});
                ++solves_;
                notify = notify_;
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

}  // namespace animage
