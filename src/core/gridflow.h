// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace animage {

// Boykov-Kolmogorov maximum flow, from the 2004 paper rather than from the
// authors' own implementation, whose licence is restricted to non-commercial
// research.
//
// Two search trees are grown, one from each terminal, and -- this is the part
// that matters -- they are kept between augmentations and repaired locally
// rather than rebuilt. A first version here rebuilt them every time, which is
// Edmonds-Karp wearing a disguise: correct, and three and a half minutes on a
// megapixel.
//
// Specialised to a four-connected grid rather than written against a general
// graph. A million pixels through an adjacency list costs more in bookkeeping
// than the flow costs to compute, and the neighbours here are always the same
// four.
class GridFlow {
public:
    GridFlow(int width, int height);

    void setSource(std::size_t node, float capacity) { source_capacity_[node] = capacity; }
    void setSink(std::size_t node, float capacity) { sink_capacity_[node] = capacity; }

    // Undirected: the same capacity both ways, which is what an unordered cut
    // between two pixels means.
    void connect(std::size_t node, int direction, std::size_t other, float capacity);

    // Runs the flow and reports, per node, whether it ended on the source side
    // of the minimum cut.
    std::vector<char> solve();

    static int opposite(int direction) {
        static constexpr int kOpposite[4] = {1, 0, 3, 2};
        return kOpposite[direction];
    }

private:
    enum class Tree : std::uint8_t { Free, Source, Sink };
    static constexpr std::size_t kNoParent = static_cast<std::size_t>(-1);
    static constexpr std::size_t kTerminal = static_cast<std::size_t>(-2);

    bool neighbour(std::size_t node, int direction, std::size_t& out) const;

    float& residual(std::size_t node, int direction) {
        return residual_[node * 4 + static_cast<std::size_t>(direction)];
    }

    // What `node` may grow along. The two trees look opposite ways: flow runs
    // away from the source and towards the sink.
    float growCapacity(std::size_t node, int direction, std::size_t other) {
        return (tree_[node] == Tree::Source) ? residual(node, direction)
                                             : residual(other, opposite(direction));
    }

    void makeActive(std::size_t node);
    bool rootedAtTerminal(std::size_t node) const;
    bool grow(std::size_t& from, std::size_t& to, int& direction);
    void augment(std::size_t from, std::size_t to, int direction);
    void adopt();

    int width_;
    int height_;
    std::size_t count_;

    std::vector<float> source_capacity_;
    std::vector<float> sink_capacity_;
    std::vector<float> residual_;  // four per node: right, left, down, up

    std::vector<Tree> tree_;
    std::vector<std::size_t> parent_;
    std::vector<int> parent_direction_;  // direction leading from parent to node
    std::vector<char> queued_;

    std::vector<std::size_t> active_;
    std::vector<std::size_t> orphans_;
    std::size_t active_head_ = 0;
};

}  // namespace animage
