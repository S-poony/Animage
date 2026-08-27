// SPDX-License-Identifier: GPL-3.0-or-later
#include "reference_decoder.h"

#include <algorithm>
#include <new>
#include <utility>

#include "image_import.h"

using animage::CtgKey;
using animage::TileGrid;
using animage::transformTiles;

ReferenceDecoder::ReferenceDecoder(int workers) {
    workers = std::max(1, workers);
    workers_.reserve(static_cast<std::size_t>(workers));
    for (int i = 0; i < workers; ++i) workers_.emplace_back([this] { run(); });
}

ReferenceDecoder::~ReferenceDecoder() {
    {
        std::lock_guard<std::mutex> held(mutex_);
        stopping_ = true;
        // Everything in flight is now worth nothing. A decode is short enough
        // that this only shortens the wait rather than making one bearable, but
        // the flag is what stops a finished answer being pushed onto a queue
        // nobody will collect from.
        for (Active& active : running_) active.abandon->store(true);
        queued_.clear();
    }
    work_.notify_all();
    for (std::thread& worker : workers_) worker.join();
}

void ReferenceDecoder::request(const CtgKey& key, Job job) {
    {
        std::lock_guard<std::mutex> held(mutex_);
        if (stopping_) return;

        const auto stale = std::remove_if(queued_.begin(), queued_.end(),
                                          [&](const Request& q) { return q.key == key; });
        superseded_ += static_cast<std::uint64_t>(std::distance(stale, queued_.end()));
        queued_.erase(stale, queued_.end());

        // And the one already running. It finishes -- there is nothing to check
        // a flag inside a QImage decode -- and its answer is dropped when it
        // gets back, which is the same outcome a step later.
        for (Active& active : running_) {
            if (active.key == key) active.abandon->store(true);
        }

        Request wanted;
        wanted.key = key;
        wanted.job = std::move(job);
        wanted.abandon = std::make_shared<std::atomic<bool>>(false);
        queued_.push_back(std::move(wanted));
    }
    work_.notify_one();
}

bool ReferenceDecoder::takeNext(Request& out) {
    std::unique_lock<std::mutex> held(mutex_);
    work_.wait(held, [this] { return stopping_ || !queued_.empty(); });
    if (stopping_) return false;

    // Newest first, which is the opposite of the solver and is right here.
    //
    // A queue that has backed up during a scrub is a list of frames somebody
    // has already dragged past, and the one they are looking at *now* is the
    // one at the end. Oldest-first would decode the whole trail before reaching
    // it, so the picture would arrive later the faster you scrubbed. Everything
    // in front of it is dropped by the cancel that leaving a frame issues, so
    // this is a bound on how wrong the order can be rather than a race with it.
    out = std::move(queued_.back());
    queued_.pop_back();
    running_.push_back({out.key, out.abandon});
    return true;
}

void ReferenceDecoder::run() {
    while (true) {
        Request taken;
        if (!takeNext(taken)) return;

        // The same rescue the solver has, for the same reason and with the same
        // narrowness. An exception leaving a thread function is std::terminate:
        // the whole program gone, with no dialog and no crash report, because
        // one imported frame did not decode. Everything this touches is its own
        // -- a path, a copy of a placement, and a grid nothing else can see --
        // so giving up on one frame is an ordinary outcome here.
        //
        // A 300 dpi A4 scan is 70 MB of tiles and nothing bounds what somebody
        // imports, so bad_alloc is a real outcome and not a formality. It is
        // counted rather than reported: whoever asked is left holding a question
        // with no answer coming, and asks again the next time that drawing is
        // painted. Until then the layer draws nothing, which is what it does
        // while any decode is still running.
        Result result;
        result.key = taken.key;
        result.under = taken.job.placement;
        result.name = taken.job.name;
        try {
            QString trouble;
            TileGrid decoded = image_import::decode(taken.job.path, &trouble);
            if (decoded.empty() && !trouble.isEmpty()) {
                result.trouble = trouble;
            } else {
                // Placed here rather than by whoever installs it, so that what
                // reaches the document is already what the compositor draws and
                // the interface thread does no resampling at all. A 4K source
                // at 25% caches a quarter-size grid for the same reason: the
                // derive is what applies the scale.
                result.tiles = taken.job.placement.isIdentity()
                                   ? std::move(decoded)
                                   : transformTiles(decoded, taken.job.placement);
                result.ok = true;
            }
        } catch (const std::bad_alloc&) {
            result.trouble = QStringLiteral("there was not enough memory to decode it");
        }

        {
            std::lock_guard<std::mutex> held(mutex_);
            const auto mine =
                std::find_if(running_.begin(), running_.end(),
                             [&](const Active& active) { return active.abandon == taken.abandon; });
            if (mine != running_.end()) running_.erase(mine);

            if (taken.abandon->load()) {
                ++superseded_;
            } else {
                // Kept even when it failed. A frame that will not read is an
                // answer -- it is what stops the same file being asked for on
                // every paint for ever -- and it is the only route by which
                // anybody is told the picture is missing.
                finished_.push_back(std::move(result));
                ++decodes_;
            }
        }
        idle_.notify_all();
    }
}

std::vector<ReferenceDecoder::Result> ReferenceDecoder::collect() {
    std::lock_guard<std::mutex> held(mutex_);
    return std::exchange(finished_, {});
}

void ReferenceDecoder::cancel(const CtgKey& key) {
    std::lock_guard<std::mutex> held(mutex_);

    const auto stale = std::remove_if(queued_.begin(), queued_.end(),
                                      [&](const Request& q) { return q.key == key; });
    superseded_ += static_cast<std::uint64_t>(std::distance(stale, queued_.end()));
    queued_.erase(stale, queued_.end());

    for (Active& active : running_) {
        if (active.key == key) active.abandon->store(true);
    }

    // A result already collected into finished_ is left alone, exactly as the
    // solver leaves one: it is an answer to a question that was asked, and
    // whoever collects it decides whether it is still wanted -- which they have
    // to be able to do anyway, because a result can land while this runs.
}

void ReferenceDecoder::cancelAll() {
    {
        std::lock_guard<std::mutex> held(mutex_);
        superseded_ += static_cast<std::uint64_t>(queued_.size());
        queued_.clear();
        for (Active& active : running_) active.abandon->store(true);
        finished_.clear();
    }
    waitUntilIdle();
}

bool ReferenceDecoder::idle() const {
    std::lock_guard<std::mutex> held(mutex_);
    return queued_.empty() && running_.empty();
}

void ReferenceDecoder::waitUntilIdle() {
    std::unique_lock<std::mutex> held(mutex_);
    idle_.wait(held, [this] { return queued_.empty() && running_.empty(); });
}

std::uint64_t ReferenceDecoder::decodeCount() const {
    std::lock_guard<std::mutex> held(mutex_);
    return decodes_;
}

std::uint64_t ReferenceDecoder::supersededCount() const {
    std::lock_guard<std::mutex> held(mutex_);
    return superseded_;
}
