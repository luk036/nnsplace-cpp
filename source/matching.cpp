/*! \file matching.cpp
 *  \brief Rectangular minimum-weight full (assignment) matching definitions.
 */

#include <nnsplace/matching.hpp>

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace nnsplace_detail {

    /// Cheap feasibility test: does a full (row-covering) matching exist?
    /// Kuhn's DFS augmenting-path algorithm on the sparse candidate graph.
    auto has_full_matching(const std::vector<std::vector<std::size_t>>& edges) -> bool {
        const auto m = edges.size();
        std::size_t n = 0;
        for (const auto& row : edges) {
            for (const auto col : row) {
                if (n <= col) n = col + 1;
            }
        }
        if (n < m) return false;
        std::vector<std::size_t> col_row(n, n);
        std::vector<char> seen(n, 0);
        const auto try_kuhn = [&](const auto& self, const std::size_t v) -> bool {
            for (const auto to : edges[v]) {
                if (seen[to]) continue;
                seen[to] = 1;
                if (col_row[to] == n || self(self, col_row[to])) {
                    col_row[to] = v;
                    return true;
                }
            }
            return false;
        };
        for (std::size_t v = 0; v < m; ++v) {
            std::fill(seen.begin(), seen.end(), 0);
            if (!try_kuhn(try_kuhn, v)) return false;
        }
        return true;
    }

}  // namespace nnsplace_detail

auto min_weight_full_matching(const std::vector<std::vector<CandidateEdge>>& candidates)
    -> std::optional<std::vector<uint32_t>> {
    const auto m = candidates.size();
    if (m == 0) return std::vector<uint32_t>{};

    // Collect the distinct slot-node ids and map them to dense columns,
    // preserving first-seen order (the Hungarian tie-break depends on it).
    std::vector<uint32_t> columns;
    absl::flat_hash_map<uint32_t, std::size_t> col_of;
    for (const auto& row : candidates) {
        for (const auto& [slot, weight] : row) {
            (void)weight;
            if (col_of.emplace(slot, columns.size()).second) {
                columns.push_back(slot);
            }
        }
    }
    const auto n = columns.size();
    if (n < m) return std::nullopt;

    // Sparse rows + cheap feasibility test first: the dense matrix build is
    // skipped whenever the instance is infeasible, which is the common case
    // while the legalizer widens its window one ring at a time.
    std::vector<std::vector<std::size_t>> sparse(m);
    for (std::size_t r = 0; r < m; ++r) {
        for (const auto& [slot, weight] : candidates[r]) {
            (void)weight;
            sparse[r].push_back(col_of.at(slot));
        }
    }
    if (!nnsplace_detail::has_full_matching(sparse)) return std::nullopt;

    // Flat row-major dense weights: kBig marks a missing edge.
    std::vector<Weight> dense(m * n, nnsplace_detail::kBig);
    for (std::size_t r = 0; r < m; ++r) {
        for (const auto& [slot, weight] : candidates[r]) {
            dense[r * n + col_of.at(slot)] = weight;
        }
    }

    const auto cost = [&dense, n](const std::size_t row, const std::size_t col) -> Weight {
        return dense[(row - 1) * n + (col - 1)];
    };
    const auto assigned = nnsplace_detail::hungarian(m, n, cost);

    // Verify every chosen (row, column) pair is a real edge.
    std::vector<uint32_t> result(m);
    for (std::size_t r = 0; r < m; ++r) {
        const auto c = assigned[r];
        if (c >= n || dense[r * n + c] >= nnsplace_detail::kBigThreshold) return std::nullopt;
        result[r] = columns[c];
    }
    return result;
}
