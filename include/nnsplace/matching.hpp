/*! \file matching.hpp
 *  \brief Rectangular minimum-weight full (assignment) matching.
 *
 *  Solves the rectangular linear-assignment problem used by the NNS legalizer:
 *  assign every module (row) to a distinct candidate slot (column) so that the
 *  sum of edge weights is minimal, or report that no full assignment exists.
 *  Costs may be negative.  Mirrors the netoptim-cpp convention of defining
 *  library code at global scope.
 *
 *  The heavy (concrete) machinery — the Kuhn feasibility test and
 *  ``min_weight_full_matching`` — lives in ``matching.cpp``; only the
 *  ``hungarian`` template (which cannot be moved out of a header without
 *  explicit instantiation) and the declarations stay here.
 */

#pragma once

#include <algorithm>
#include <cstdint>
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
        std::vector<Weight> minv(n + 1);
        std::vector<char> used(n + 1);

        for (std::size_t i = 1; i <= m; ++i) {
            p[0] = i;
            std::size_t j0 = 0;
            std::fill(minv.begin(), minv.end(), kBig);
            std::fill(used.begin(), used.end(), false);
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
auto min_weight_full_matching(const std::vector<std::vector<CandidateEdge>>& candidates)
    -> std::optional<std::vector<uint32_t>>;
