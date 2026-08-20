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

// The two cell indices the clamp can produce at the ends. A coordinate left of
// `solved` lands on the first and one right of it on the second.
int lowestCell(int limit) { return (limit >= 3) ? 1 : 0; }
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

}  // namespace

Rgba ctgFillPixel(const CtgFill& fill, int x, int y) {
    if (!fill.valid) return {};

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
    return fill.palette_colours[static_cast<std::size_t>(label)];
}

namespace {

// The half-open range of samples covering a stretch of image pixels.
//
// Worked out as a range of indices rather than tested per sample, so the loops
// below walk indices and nothing checks a rectangle twice.
CtgFillExtent samplesWithin(int from, int to, int first_x, int stride, int count) {
    const long long begin =
        (first_x >= from) ? 0 : (static_cast<long long>(from) - first_x + stride - 1) / stride;
    const long long end =
        (first_x >= to) ? 0
                        : std::min<long long>(count, (static_cast<long long>(to) - first_x +
                                                      stride - 1) / stride);
    if (begin >= end) return {};
    return {static_cast<int>(begin), static_cast<int>(end - begin)};
}

// Whether a colour reaches everywhere, which is what a ring holding one means:
// outside the solve every answer comes from that ring, so if any of it is a
// label then everything out there takes a colour, in every direction and with
// nothing to stop it. There is then no rectangle to skip on.
bool answersEverywhere(const CtgFill& fill) {
    return !fill.labels.empty() && !fill.outside_is_clear;
}

// Where along a row the labels can say anything but "nothing reached", when the
// ring is clear and so the world outside the solve is transparent.
PixelRect labelledPart(const CtgFill& fill) {
    return fill.labels.empty() ? PixelRect{} : fill.solved;
}

PixelRect markedPart(const CtgFill& fill) {
    return fill.marks.empty() ? PixelRect{} : fill.marks_drawn;
}

}  // namespace

CtgFillExtent ctgFillExtent(const CtgFill& fill, int y, int first_x, int stride, int count) {
    if (count <= 0 || !fill.valid) return {};
    stride = std::max(1, stride);

    if (answersEverywhere(fill)) return {0, count};

    const PixelRect answers = unite(labelledPart(fill), markedPart(fill));
    if (answers.isEmpty()) return {};
    if (y < answers.y || y >= answers.y + answers.height) return {};

    return samplesWithin(answers.x, answers.x + answers.width, first_x, stride, count);
}

void ctgFillSpan(const CtgFill& fill, int y, int first_x, int stride, int count, Rgba* out) {
    if (count <= 0 || out == nullptr) return;
    stride = std::max(1, stride);
    std::fill(out, out + count, Rgba{});
    if (!fill.valid) return;

    const long long begin = 0;
    const long long end = count;

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
        const Rgba* colours = fill.palette_colours.data();
        const int low = lowestCell(width);
        const int high = highestCell(width);
        const int step = fill.step;
        const int origin = fill.solved.x;

        // Three stretches, and the middle one is the drawing. Left of `from`
        // every sample clamps to the same cell, right of `to` every sample
        // clamps to the other, and between them the cell is a division -- so
        // the clamps are paid twice per row instead of twice per sample.
        const long long from = static_cast<long long>(origin) + static_cast<long long>(low) * step;
        const long long to =
            std::min(static_cast<long long>(origin) + fill.solved.width,
                     static_cast<long long>(origin) + static_cast<long long>(high + 1) * step);

        const auto index_of = [&](long long x) {
            return (x <= first_x) ? 0LL : (x - first_x + stride - 1) / stride;
        };
        const long long middle_begin = std::clamp(index_of(from), begin, end);
        const long long middle_end = std::clamp(index_of(to), begin, end);

        const auto paint = [&](long long i0, long long i1, int cell) {
            if (i0 >= i1) return;
            const std::int16_t label = row[cell];
            if (label < 0) return;  // nothing reached here
            std::fill(out + i0, out + i1, colours[static_cast<std::size_t>(label)]);
        };
        paint(begin, middle_begin, low);
        paint(middle_end, end, high);

        // The interior. One label per cell, so a run of `step` samples is one
        // colour -- and at full resolution with the compositor reading every
        // pixel the run is one sample and the cell simply walks alongside it,
        // which is the case worth being fast at.
        if (step == 1 && stride == 1) {
            int cell = static_cast<int>(first_x + middle_begin - origin);
            for (long long i = middle_begin; i < middle_end; ++i, ++cell) {
                const std::int16_t label = row[cell];
                if (label >= 0) out[i] = colours[static_cast<std::size_t>(label)];
            }
        } else {
            for (long long i = middle_begin; i < middle_end;) {
                const long long x = first_x + i * stride;
                const int cell = static_cast<int>((x - origin) / step);
                const long long cell_end =
                    static_cast<long long>(origin) + static_cast<long long>(cell + 1) * step;
                const long long run = std::min(middle_end - i, (cell_end - x + stride - 1) / stride);
                paint(i, i + run, cell);
                i += run;
            }
        }
    }

    // And then the mark wins wherever it was drawn, at full resolution however
    // coarse the solve was. A tile at a time, so the lookup is hoisted across
    // the run exactly as it is at full resolution.
    const PixelRect marked = markedPart(fill);
    if (marked.isEmpty() || y < marked.y || y >= marked.y + marked.height) return;

    const CtgFillExtent over =
        samplesWithin(marked.x, marked.x + marked.width, first_x, stride, count);
    if (over.count <= 0) return;

    const int mark_y = y - fill.carried_by.y;
    const int tile_y = tileCoordFor(0, mark_y).y;
    const int local_y = tileLocal(mark_y);

    for (long long i = over.first, stop = over.first + over.count; i < stop;) {
        const int mark_x = first_x + static_cast<int>(i) * stride - fill.carried_by.x;
        const int tile_x = tileCoordFor(mark_x, 0).x;
        const long long tile_end = static_cast<long long>(tile_x + 1) * kTileSize;
        const long long run = std::min(stop - i, (tile_end - mark_x + stride - 1) / stride);

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
            if (p[3].toFloat() < fill.mark_threshold) continue;  // not a label

            const Rgba mark{p[0].toFloat(), p[1].toFloat(), p[2].toFloat(), p[3].toFloat()};
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
