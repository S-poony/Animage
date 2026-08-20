// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctg_fill.h"

#include <algorithm>
#include <limits>

namespace animage {
namespace {

// How much fill is worth keeping. A quarter of a gigabyte, which is what the
// tile budget it replaces worked out at -- the budget has not moved, only the
// unit it is counted in, and the unit is now the one that was meant.
//
// A 1080p fill is about 2.07M labels and a palette, so 4 MB; a 4K one solved at
// half is 8 MB. That is about sixty-four coloured drawings held at once against
// the fifteen a picture of the same fill allowed, which is aimed straight at
// what bench_playback reports.
//
// It is a budget, so by the rule this codebase learned the hard way it will
// express itself as a threshold somewhere else: the somewhere is "how far back
// along the timeline you can jump before the fill has to be solved again", and
// solving again is the ordinary cost of arriving at a drawing.
constexpr std::size_t kFillByteBudget = 256u << 20;

// What one fill weighs. The marks are not counted -- see CtgFillCache::bytes.
std::size_t footprint(const CtgFill& fill) {
    return fill.labels.size() * sizeof(std::int16_t) +
           fill.palette.size() * sizeof(std::uint32_t);
}

// Which cell a coordinate reads from.
//
// Clamped to one cell *inside* the grid rather than to its edge, because the
// outermost ring is the background seed. It is scaffolding, not an answer:
// sampling it would paint everything beyond the drawing as background even
// where a colour had filled right up to it, and would leave a one-cell seam at
// the solve's edge.
int cellAt(int value, int origin, int extent, int step, int limit) {
    const int clamped = std::clamp(value, origin, origin + extent - 1);
    const int cell = std::min((clamped - origin) / step, limit - 1);
    return (limit >= 3) ? std::clamp(cell, 1, limit - 2) : cell;
}

// The highest cell index the clamp can produce. A coordinate right of `solved`
// lands on it, and one left of `solved` lands on its opposite.
int highestCell(int limit) { return (limit >= 3) ? limit - 2 : limit - 1; }

// Whether the labels are the shape `solved` and `step` say they are. One
// compare, and it is what keeps every index below in bounds by construction
// rather than by everyone who writes a fill having remembered.
bool labelsAreUsable(const CtgFill& fill, int& width, int& height) {
    width = fill.gridWidth();
    height = fill.gridHeight();
    if (width <= 0 || height <= 0) return false;
    return fill.labels.size() ==
           static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

bool insideCanvas(const CtgFill& fill, int x, int y) {
    return x >= fill.canvas.x && x < fill.canvas.x + fill.canvas.width && y >= fill.canvas.y &&
           y < fill.canvas.y + fill.canvas.height;
}

}  // namespace

Rgba ctgFillPixel(const CtgFill& fill, int x, int y) {
    if (!fill.valid || !insideCanvas(fill, x, y)) return {};

    // The mark first, because it wins: a scribble is a statement about the
    // pixels it covers, and the solver's job is only the pixels nobody said
    // anything about.
    if (!fill.marks.empty()) {
        const Rgba mark = fill.marks.pixel(x - fill.carried_by.x, y - fill.carried_by.y);
        if (mark.a >= fill.mark_threshold) return scribbleColour(scribbleLabel(mark));
    }

    int width = 0;
    int height = 0;
    if (!labelsAreUsable(fill, width, height)) return {};

    const int cx = cellAt(x, fill.solved.x, fill.solved.width, fill.step, width);
    const int cy = cellAt(y, fill.solved.y, fill.solved.height, fill.step, height);
    const std::int16_t label = fill.labels[static_cast<std::size_t>(cy) * width + cx];
    if (label < 0) return {};  // nothing reached here
    return scribbleColour(fill.palette[static_cast<std::size_t>(label)]);
}

void ctgFillSpan(const CtgFill& fill, int y, int first_x, int stride, int count, Rgba* out) {
    if (count <= 0 || out == nullptr) return;
    stride = std::max(1, stride);
    std::fill(out, out + count, Rgba{});
    if (!fill.valid || fill.canvas.isEmpty()) return;
    if (y < fill.canvas.y || y >= fill.canvas.y + fill.canvas.height) return;

    const int left = fill.canvas.x;
    const int right = fill.canvas.x + fill.canvas.width;

    // Which samples fall inside the bound, as a range of indices, so everything
    // below walks indices rather than testing each one against the rectangle.
    const long long begin =
        (first_x >= left) ? 0 : (static_cast<long long>(left) - first_x + stride - 1) / stride;
    const long long end =
        (first_x >= right)
            ? 0
            : std::min<long long>(
                  count, (static_cast<long long>(right) - first_x + stride - 1) / stride);
    if (begin >= end) return;

    int width = 0;
    int height = 0;
    const bool have_labels = labelsAreUsable(fill, width, height);

    // A row outside the solve reads nothing but the ring, so when the ring is
    // clear the whole row is transparent. One test rather than one per pixel,
    // and it is the shortcut an absent tile used to give the compositor.
    const bool row_outside = y < fill.solved.y || y >= fill.solved.y + fill.solved.height;

    if (have_labels && !(row_outside && fill.outside_is_clear)) {
        const int cy = cellAt(y, fill.solved.y, fill.solved.height, fill.step, height);
        const std::int16_t* row = fill.labels.data() + static_cast<std::size_t>(cy) * width;
        const int last_cell = highestCell(width);

        // A run of samples inside one cell is one label and therefore one
        // colour, which is where the constant is won: at full resolution that
        // is `step` pixels for one lookup, and outside the solve it is the rest
        // of the row for one.
        for (long long i = begin; i < end;) {
            const int x = first_x + static_cast<int>(i) * stride;
            const int cx = cellAt(x, fill.solved.x, fill.solved.width, fill.step, width);

            const long long cell_end =
                (cx >= last_cell)
                    ? std::numeric_limits<int>::max()
                    : static_cast<long long>(fill.solved.x) +
                          static_cast<long long>(cx + 1) * fill.step;
            const long long run =
                std::min<long long>(end - i, (cell_end - x + stride - 1) / stride);

            const std::int16_t label = row[cx];
            if (label >= 0) {
                const Rgba colour =
                    scribbleColour(fill.palette[static_cast<std::size_t>(label)]);
                std::fill(out + i, out + i + run, colour);
            }
            i += run;
        }
    }

    // And then the mark wins wherever it was drawn, at full resolution however
    // coarse the solve was. A tile at a time, so the lookup is hoisted across
    // the run exactly as it is at full resolution.
    if (fill.marks.empty()) return;

    const int mark_y = y - fill.carried_by.y;
    const int tile_y = tileCoordFor(0, mark_y).y;
    const int local_y = tileLocal(mark_y);

    for (long long i = begin; i < end;) {
        const int mark_x = first_x + static_cast<int>(i) * stride - fill.carried_by.x;
        const int tile_x = tileCoordFor(mark_x, 0).x;
        const long long tile_end = static_cast<long long>(tile_x + 1) * kTileSize;
        const long long run =
            std::min<long long>(end - i, (tile_end - mark_x + stride - 1) / stride);

        // findSlot borrows the handle. find() would copy the shared_ptr, and an
        // atomic increment per lookup is not free at this rate.
        const TileRef* held = fill.marks.findSlot({tile_x, tile_y});
        if (!held || !*held) {
            i += run;
            continue;  // an absent tile is no mark at all
        }

        const Half* row =
            (*held)->rgba.data() + static_cast<std::size_t>(local_y) * kTileSize * 4;
        const int local_x = tileLocal(mark_x);
        for (long long j = 0; j < run; ++j) {
            const Half* p =
                row + (static_cast<std::size_t>(local_x) + static_cast<std::size_t>(j) * stride) *
                          4;
            const Rgba mark{p[0].toFloat(), p[1].toFloat(), p[2].toFloat(), p[3].toFloat()};
            if (mark.a < fill.mark_threshold) continue;  // not a label

            // The quantised label colour and not the pixel's own, for the same
            // reason the seeding thresholds: a scribble is a label, so its
            // antialiased rim must not leave a stripe of some colour between.
            out[i + j] = scribbleColour(scribbleLabel(mark));
        }
        i += run;
    }
}

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
        bytes_ -= footprint(found->second.fill);
        found->second.fill = std::move(fill);
    } else {
        found = entries_.emplace(key, Entry{std::move(fill), 0}).first;
    }
    found->second.used = ++clock_;
    bytes_ += footprint(found->second.fill);

    evictDownToBudget(key);
    return found->second.fill;
}

void CtgFillCache::clear() {
    entries_.clear();
    bytes_ = 0;
    ++generation_;
}

// Oldest first, and never the entry just stored: the caller is holding a
// reference to it. One fill can be larger than the whole budget on a big
// canvas, and that is the case this rule quietly handles -- the budget is
// exceeded rather than the answer thrown away before it is read.
void CtgFillCache::evictDownToBudget(const CtgKey& keep) {
    if (bytes_ <= kFillByteBudget) return;

    std::vector<std::pair<std::uint64_t, CtgKey>> by_age;
    by_age.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        if (key == keep) continue;
        by_age.emplace_back(entry.used, key);
    }
    std::sort(by_age.begin(), by_age.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [used, key] : by_age) {
        if (bytes_ <= kFillByteBudget) break;
        auto found = entries_.find(key);
        if (found == entries_.end()) continue;
        bytes_ -= footprint(found->second.fill);
        entries_.erase(found);
    }
}

}  // namespace animage
