// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctg_fill.h"

#include <algorithm>

namespace animage {
namespace {

// How many tiles of fill are worth keeping. A tile is 128x128 half-float RGBA,
// so 128 KB; two thousand of them is a quarter of a gigabyte, and a 1920x1080
// canvas spends 135 on one drawing. That is about fifteen coloured drawings
// held at once, which covers scrubbing around a scene and an export walking it
// in order, and refuses to cover playing the whole shot and keeping all of it.
//
// It is a budget, so by the rule this codebase learned the hard way it will
// express itself as a threshold somewhere else: the somewhere is "how far back
// along the timeline you can jump before the fill has to be solved again", and
// solving again is the ordinary cost of arriving at a drawing.
constexpr std::size_t kFillTileBudget = 2048;

}  // namespace

const CtgFill* CtgFillCache::find(const CtgKey& key) const {
    auto found = entries_.find(key);
    if (found == entries_.end()) return nullptr;
    found->second.used = ++clock_;
    return &found->second.fill;
}

CtgFill& CtgFillCache::store(const CtgKey& key, CtgFill fill) {
    ++stores_;
    auto found = entries_.find(key);
    if (found != entries_.end()) {
        tiles_ -= found->second.fill.tiles.tileCount();
        found->second.fill = std::move(fill);
    } else {
        found = entries_.emplace(key, Entry{std::move(fill), 0}).first;
    }
    found->second.used = ++clock_;
    tiles_ += found->second.fill.tiles.tileCount();

    evictDownToBudget(key);
    return found->second.fill;
}

void CtgFillCache::clear() {
    entries_.clear();
    tiles_ = 0;
    ++generation_;
}

// Oldest first, and never the entry just stored: the caller is holding a
// reference to it. One fill can be larger than the whole budget on a big
// canvas, and that is the case this rule quietly handles -- the budget is
// exceeded rather than the answer thrown away before it is read.
void CtgFillCache::evictDownToBudget(const CtgKey& keep) {
    if (tiles_ <= kFillTileBudget) return;

    std::vector<std::pair<std::uint64_t, CtgKey>> by_age;
    by_age.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        if (key == keep) continue;
        by_age.emplace_back(entry.used, key);
    }
    std::sort(by_age.begin(), by_age.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [used, key] : by_age) {
        if (tiles_ <= kFillTileBudget) break;
        auto found = entries_.find(key);
        if (found == entries_.end()) continue;
        tiles_ -= found->second.fill.tiles.tileCount();
        entries_.erase(found);
    }
}

}  // namespace animage
