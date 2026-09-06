/*! \file matching.cpp
 *  \brief Rectangular minimum-weight full (assignment) matching definitions.
 */

#include <nnsplace/matching.hpp>

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
        const auto try_kuhn = [&](const auto& self, const std::size_t v,
                                  std::vector<bool>& seen) -> bool {
            for (const auto to : edges[v]) {
                if (seen[to]) continue;
                seen[to] = true;
                if (col_row[to] == n || self(self, col_row[to], seen)) {
                    col_row[to] = v;
                    return true;
                }
            }
            return false;
        };
        for (std::size_t v = 0; v < m; ++v) {
            std::vector<bool> seen(n, false);
            if (!try_kuhn(try_kuhn, v, seen)) return false;
        }
        return true;
    }

}  // namespace nnsplace_detail

auto min_weight_full_matching(const std::vector<std::vector<CandidateEdge>>& candidates)
    -> std::optional<std::vector<uint32_t>> {
    const auto m = candidates.size();
    if (m == 0) return std::vector<uint32_t>{};

    // Collect the distinct slot-node ids and map them to dense columns.
    std::vector<uint32_t> columns;
    for (const auto& row : candidates) {
        for (const auto& [slot, weight] : row) {
            (void)weight;
            bool found = false;
            for (const auto c : columns) {
                if (c == slot) {
                    found = true;
                    break;
                }
            }
            if (!found) columns.push_back(slot);
        }
    }
    const auto n = columns.size();
    if (n < m) return std::nullopt;

    // Column id lookup.
    const auto dense_col = [&columns](const uint32_t slot) -> std::size_t {
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (columns[i] == slot) return i;
        }
        return columns.size();
    };

    // Per-row dense weight rows: kBig marks a missing edge.
    std::vector<std::vector<Weight>> dense(m, std::vector<Weight>(n, nnsplace_detail::kBig));
    std::vector<std::vector<std::size_t>> sparse(m);
    for (std::size_t r = 0; r < m; ++r) {
        for (const auto& [slot, weight] : candidates[r]) {
            const auto c = dense_col(slot);
            if (c != n) {
                dense[r][c] = weight;
                sparse[r].push_back(c);
            }
        }
    }

    if (!nnsplace_detail::has_full_matching(sparse)) return std::nullopt;

    const auto cost = [&dense](const std::size_t row, const std::size_t col) -> Weight {
        return dense[row - 1][col - 1];
    };
    const auto assigned = nnsplace_detail::hungarian(m, n, cost);

    // Verify every chosen (row, column) pair is a real edge.
    std::vector<uint32_t> result(m);
    for (std::size_t r = 0; r < m; ++r) {
        const auto c = assigned[r];
        if (c >= n || dense[r][c] >= nnsplace_detail::kBigThreshold) return std::nullopt;
        result[r] = columns[c];
    }
    return result;
}
