// SPDX-License-Identifier: GPL-3.0-or-later
#include "gridflow.h"

#include <algorithm>

namespace animage {

GridFlow::GridFlow(int width, int height)
    : width_(width), height_(height), count_(static_cast<std::size_t>(width) * height) {
    source_capacity_.assign(count_, 0.0f);
    sink_capacity_.assign(count_, 0.0f);
    residual_.assign(count_ * 4, 0.0f);
    tree_.assign(count_, Tree::Free);
    parent_.assign(count_, kNoParent);
    parent_direction_.assign(count_, 0);
    queued_.assign(count_, 0);
}

void GridFlow::connect(std::size_t node, int direction, std::size_t other, float capacity) {
    residual_[node * 4 + static_cast<std::size_t>(direction)] = capacity;
    residual_[other * 4 + static_cast<std::size_t>(opposite(direction))] = capacity;
}

bool GridFlow::neighbour(std::size_t node, int direction, std::size_t& out) const {
    const std::size_t x = node % static_cast<std::size_t>(width_);
    const std::size_t y = node / static_cast<std::size_t>(width_);
    switch (direction) {
        case 0:
            if (x + 1 >= static_cast<std::size_t>(width_)) return false;
            out = node + 1;
            return true;
        case 1:
            if (x == 0) return false;
            out = node - 1;
            return true;
        case 2:
            if (y + 1 >= static_cast<std::size_t>(height_)) return false;
            out = node + static_cast<std::size_t>(width_);
            return true;
        default:
            if (y == 0) return false;
            out = node - static_cast<std::size_t>(width_);
            return true;
    }
}

void GridFlow::makeActive(std::size_t node) {
    if (queued_[node]) return;
    queued_[node] = 1;
    active_.push_back(node);
}

// Whether this node's parent chain still ends at a terminal. A branch that has
// come loose is no use as a parent to anybody else.
bool GridFlow::rootedAtTerminal(std::size_t node) const {
    std::size_t steps = 0;
    while (node != kTerminal) {
        if (node == kNoParent) return false;
        if (++steps > count_) return false;  // a cycle should be impossible
        node = parent_[node];
    }
    return true;
}

bool GridFlow::grow(std::size_t& from, std::size_t& to, int& direction) {
    while (active_head_ < active_.size()) {
        const std::size_t node = active_[active_head_];
        if (tree_[node] == Tree::Free) {
            queued_[node] = 0;
            ++active_head_;
            continue;
        }

        for (int d = 0; d < 4; ++d) {
            std::size_t next = 0;
            if (!neighbour(node, d, next)) continue;
            if (growCapacity(node, d, next) <= 0.0f) continue;

            if (tree_[next] == Tree::Free) {
                tree_[next] = tree_[node];
                parent_[next] = node;
                parent_direction_[next] = d;
                makeActive(next);
                continue;
            }
            if (tree_[next] != tree_[node]) {
                // The trees have met: source to sink, through here.
                from = node;
                to = next;
                direction = d;
                return true;
            }
        }

        queued_[node] = 0;
        ++active_head_;

        // Drop the consumed head occasionally so the queue does not grow
        // without bound over a long solve.
        if (active_head_ > 8192 && active_head_ * 2 > active_.size()) {
            active_.erase(active_.begin(),
                          active_.begin() + static_cast<std::ptrdiff_t>(active_head_));
            active_head_ = 0;
        }
    }
    return false;
}

void GridFlow::augment(std::size_t from, std::size_t to, int direction) {
    // Normalise so `from` is always the source-tree end.
    if (tree_[from] == Tree::Sink) {
        std::swap(from, to);
        direction = opposite(direction);
    }

    float bottleneck = residual(from, direction);

    std::size_t node = from;
    while (parent_[node] != kTerminal) {
        bottleneck = std::min(bottleneck, residual(parent_[node], parent_direction_[node]));
        node = parent_[node];
    }
    const std::size_t source_root = node;
    bottleneck = std::min(bottleneck, source_capacity_[source_root]);

    node = to;
    while (parent_[node] != kTerminal) {
        bottleneck = std::min(bottleneck, residual(node, opposite(parent_direction_[node])));
        node = parent_[node];
    }
    const std::size_t sink_root = node;
    bottleneck = std::min(bottleneck, sink_capacity_[sink_root]);

    // A tree path should never contain a saturated edge, but if one slips
    // through, pushing nothing would leave grow() offering the same edge for
    // ever. Orphan whatever is hanging off a dead link so adopt() has something
    // to repair, and the search moves on.
    if (!(bottleneck > 0.0f)) {
        node = from;
        while (parent_[node] != kTerminal && parent_[node] != kNoParent) {
            if (residual(parent_[node], parent_direction_[node]) <= 0.0f) {
                orphans_.push_back(node);
            }
            node = parent_[node];
        }
        node = to;
        while (parent_[node] != kTerminal && parent_[node] != kNoParent) {
            if (residual(node, opposite(parent_direction_[node])) <= 0.0f) {
                orphans_.push_back(node);
            }
            node = parent_[node];
        }
        return;
    }

    source_capacity_[source_root] -= bottleneck;
    if (source_capacity_[source_root] <= 0.0f) orphans_.push_back(source_root);

    residual(from, direction) -= bottleneck;
    residual(to, opposite(direction)) += bottleneck;

    node = from;
    while (parent_[node] != kTerminal) {
        const std::size_t up = parent_[node];
        const int d = parent_direction_[node];
        residual(up, d) -= bottleneck;
        residual(node, opposite(d)) += bottleneck;
        if (residual(up, d) <= 0.0f) orphans_.push_back(node);
        node = up;
    }

    node = to;
    while (parent_[node] != kTerminal) {
        const std::size_t up = parent_[node];
        const int d = parent_direction_[node];
        residual(node, opposite(d)) -= bottleneck;
        residual(up, d) += bottleneck;
        if (residual(node, opposite(d)) <= 0.0f) orphans_.push_back(node);
        node = up;
    }

    sink_capacity_[sink_root] -= bottleneck;
    if (sink_capacity_[sink_root] <= 0.0f) orphans_.push_back(sink_root);
}

// Saturating an edge cuts a branch loose. Rather than rebuild the tree, each
// orphan looks for another parent among its neighbours; only if none will have
// it does it leave the tree and hand the problem to its own children.
void GridFlow::adopt() {
    while (!orphans_.empty()) {
        const std::size_t node = orphans_.back();
        orphans_.pop_back();
        if (tree_[node] == Tree::Free) continue;

        const Tree side = tree_[node];

        // Cut the stale link before looking for a new one. Leaving it in place
        // lets this node adopt one of its own descendants -- the candidate's
        // chain runs back through here, still appears to reach a terminal, and
        // the result is a cycle in the tree that the next walk never leaves.
        parent_[node] = kNoParent;

        bool adopted = false;
        for (int d = 0; d < 4 && !adopted; ++d) {
            std::size_t candidate = 0;
            if (!neighbour(node, d, candidate)) continue;
            if (tree_[candidate] != side) continue;

            const float capacity =
                (side == Tree::Source) ? residual(candidate, opposite(d)) : residual(node, d);
            if (capacity <= 0.0f) continue;
            if (!rootedAtTerminal(candidate)) continue;

            parent_[node] = candidate;
            parent_direction_[node] = opposite(d);
            adopted = true;
        }
        if (adopted) continue;

        // Nobody could take it. Its neighbours may be able to grow into the
        // space it leaves behind, and its children are orphans now too.
        for (int d = 0; d < 4; ++d) {
            std::size_t other = 0;
            if (!neighbour(node, d, other)) continue;
            if (tree_[other] != side) continue;

            const float capacity =
                (side == Tree::Source) ? residual(other, opposite(d)) : residual(node, d);
            if (capacity > 0.0f) makeActive(other);
            if (parent_[other] == node) orphans_.push_back(other);
        }

        tree_[node] = Tree::Free;
        parent_[node] = kNoParent;
        queued_[node] = 0;
    }
}

std::vector<char> GridFlow::solve() {
    active_.clear();
    active_head_ = 0;
    for (std::size_t node = 0; node < count_; ++node) {
        if (source_capacity_[node] > 0.0f) {
            tree_[node] = Tree::Source;
            parent_[node] = kTerminal;
            makeActive(node);
        } else if (sink_capacity_[node] > 0.0f) {
            tree_[node] = Tree::Sink;
            parent_[node] = kTerminal;
            makeActive(node);
        }
    }

    std::size_t from = 0;
    std::size_t to = 0;
    int direction = 0;
    while (grow(from, to, direction)) {
        augment(from, to, direction);
        adopt();
    }

    // The source side of the cut is whatever is still reachable from the source
    // through residual capacity. Read off directly rather than trusting the
    // final state of the trees: one fewer thing to get subtly wrong.
    std::vector<char> source_side(count_, 0);
    std::vector<std::size_t> stack;
    for (std::size_t node = 0; node < count_; ++node) {
        if (source_capacity_[node] > 0.0f) {
            source_side[node] = 1;
            stack.push_back(node);
        }
    }
    while (!stack.empty()) {
        const std::size_t node = stack.back();
        stack.pop_back();
        for (int d = 0; d < 4; ++d) {
            std::size_t next = 0;
            if (!neighbour(node, d, next)) continue;
            if (source_side[next] || residual(node, d) <= 0.0f) continue;
            source_side[next] = 1;
            stack.push_back(next);
        }
    }
    return source_side;
}

}  // namespace animage
