/*! \file matching.hpp
 *  \brief Rectangular minimum-weight full (assignment) matching.
 *
 *  Solves the rectangular linear-assignment problem used by the NNS legalizer:
 *  assign every module (row) to a distinct candidate slot (column) so that the
 *  sum of edge weights is minimal, or report that no full assignment exists.
 *  Costs may be negative.  Mirrors the netoptim-cpp convention of defining
 *  library code at global scope.
 */

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

/// Edge weight used by the assignment solver.
using Weight = int64_t;

/// A module->slot candidate edge: (slot-node id, cost delta).
using CandidateEdge = std::pair<uint32_t, Weight>;

namespace nnsplace_detail {

    /// Sentinel larger than any achievable sum of real candidate weights.
    inline constexpr Weight kBig = Weight{1} << 40;

    /// Maximum cost an edge contributes before the instance is declared infeasible.
    inline constexpr Weight kBigThreshold = kBig / 2;

    /** \brief Cheap feasibility test: does a full (row-covering) matching exist?
     *
     *  Kuhn's DFS augmenting-path algorithm on the sparse candidate graph.
     *
     *  \param edges per-row candidate (column-index) lists
     *  \return true when every row can be assigned a distinct column
     */
    inline auto has_full_matching(const std::vector<std::vector<std::size_t>>& edges) -> bool {
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

    /** \brief O(n^3) Hungarian algorithm for a rectangular m x n cost matrix.
     *
     *  Finds the minimum-cost assignment of all m rows (m <= n) to distinct
     *  columns.  Missing edges carry cost \a inf.  \a cost(row, col) is
     *  1-indexed in the classic formulation.
     */
    template <typename CostFn>  //
    auto hungarian(const std::size_t m, const std::size_t n, CostFn&& cost)
        -> std::vector<std::size_t> {
        std::vector<Weight> u(m + 1, 0);
        std::vector<Weight> v(n + 1, 0);
        std::vector<std::size_t> p(n + 1, 0);  // p[col] = matched row (0 = free)
        std::vector<std::size_t> way(n + 1, 0);

        for (std::size_t i = 1; i <= m; ++i) {
            p[0] = i;
            std::size_t j0 = 0;
            std::vector<Weight> minv(n + 1, kBig);
            std::vector<char> used(n + 1, false);
            do {
                used[j0] = true;
                const auto i0 = p[j0];
                Weight delta = kBig;
                std::size_t j1 = 0;
                for (std::size_t j = 1; j <= n; ++j) {
                    if (used[j]) continue;
                    const auto cur = cost(i0, j) - u[i0] - v[j];
                    if (cur < minv[j]) {
                        minv[j] = cur;
                        way[j] = j0;
                    }
                    if (minv[j] < delta) {
                        delta = minv[j];
                        j1 = j;
                    }
                }
                for (std::size_t j = 0; j <= n; ++j) {
                    if (used[j]) {
                        u[p[j]] += delta;
                        v[j] -= delta;
                    } else {
                        minv[j] -= delta;
                    }
                }
                j0 = j1;
            } while (p[j0] != 0);
            do {
                const auto j1 = way[j0];
                p[j0] = p[j1];
                j0 = j1;
            } while (j0 != 0);
        }
        std::vector<std::size_t> result(m, n);
        for (std::size_t j = 1; j <= n; ++j) {
            if (p[j] != 0) result[p[j] - 1] = j - 1;
        }
        return result;
    }

}  // namespace nnsplace_detail

/** \brief Rectangular minimum-weight full matching over module->slot edges.
 *
 *  \param candidates  per-module candidate edges (slot node id + weight)
 *  \return  for each module the chosen distinct slot-node id, or std::nullopt
 *           when no full assignment exists
 */
inline auto min_weight_full_matching(const std::vector<std::vector<CandidateEdge>>& candidates)
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
