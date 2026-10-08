/*! \file placement.hpp
 *  \brief "No-nonsense" (NNS) placement engine for FPGA-style designs.
 *
 *  Faithful C++ port of the Python ``nnsplace.placement`` module.  It builds a
 *  directed flow graph from a hypergraph netlist, generates an initial random
 *  placement, then iteratively minimizes the worst (HPWL) wire length using
 *  Howard's parametric minimum-cost-flow algorithm (digraphx-cpp), legalizes
 *  module positions with minimum-weight bipartite assignment, and snaps I/O
 *  pads onto the grid ring.
 *
 *  Following the netoptim-cpp convention the library code lives at global
 *  scope (no namespace wrapper).
 */

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <digraphx/min_parametric_q.hpp>
#include <fractions/pyfractions.hpp>
#include <limits>
#include <mywheel/map_adapter.hpp>
#include <netlistx/netlist.hpp>
#include <numeric>
#include <optional>
#include <random>
#include <recti/interval.hpp>
#include <stdexcept>
#include <utility>
#include <vector>
#include <xnetwork/classes/graph.hpp>

#include "matching.hpp"
#include "placement_cfg.hpp"

/// Signed coordinate / occupancy / cost type (wire lengths stay small; the
/// sentinels in the I/O-pad arithmetic need ~1e12).
using Coord = int64_t;
/// Exact rational used by Howard's algorithm (same as Python fractions.Fraction).
using Ratio = fractions::Fraction<int64_t>;
/// Module node identifier.
using node_t = std::size_t;
/// A placement: place[0][v] = x, place[1][v] = y.
using Placement = std::array<std::vector<Coord>, 2>;

/** \brief Floor division ``a / b`` for a positive divisor ``b`` (Python //). */
inline auto floor_div(const Coord a, const Coord b) -> Coord {
    if (a >= 0) return a / b;
    return -(((-a) + b - 1) / b);
}

/** \brief Directed flow graph derived from a netlist (adjacency per module). */
struct FlowGraph {
    /// adj[v] holds the sorted, de-duplicated directed neighbours of module v.
    std::vector<std::vector<node_t>> adj;

    explicit FlowGraph(std::vector<std::vector<node_t>> a) : adj(std::move(a)) {}
};

/** \brief Directed-edge set test helper (used by the doctest suite). */
inline auto has_directed_edge(const FlowGraph& fg, const node_t u, const node_t v) -> bool {
    if (u >= fg.adj.size()) return false;
    return std::binary_search(fg.adj[u].begin(), fg.adj[u].end(), v);
}

/** \brief Build the flow graph mirroring ``create_flow_graph``.
 *
 *  For every ordered pair ``(v1, v2)`` inside each net, both directed edges
 *  ``v1 -> v2`` and ``v2 -> v1`` are added unless module ``v2`` carries weight
 *  zero (an I/O pad).  Repeated edges are collapsed; self loops appear for
 *  every non-pad module that belongs to at least one net.
 *
 *  When ``net_driver`` is provided (one optional driver per net, in net
 *  order) only the **driver -> sink** connections of every net are added, so
 *  the worst wire length is measured on source-to-sink wires only; nets whose
 *  driver is unknown fall back to the all-pairs clique.
 */
auto create_flow_graph(const SimpleNetlist& hyprgraph,
                       const std::vector<std::optional<node_t>>& net_driver = {}) -> FlowGraph;

namespace nnsplace_detail {

    /// Pre-computed scoring tables that let a lateral move of one module be
    /// evaluated in O(log deg) per candidate slot (Python ``_module_slot_data``).
    struct ModuleSlotData {
        Coord p0;
        std::vector<Coord> as_;
        std::vector<Coord> pref;
        std::vector<Coord> suff;
        Coord w0;
    };

    /** \brief Worst wire length of module ``v`` placed at coordinate ``q``.
     *
     *  Ports Python ``NnsPlacer._worst_at``: neighbours sorted by their moving
     *  axis coordinate, prefix/suffix maxima of the two linear forms bound the
     *  per-candidate value.
     */
    inline auto worst_at(const Coord q, const std::vector<Coord>& as_,
                         const std::vector<Coord>& pref, const std::vector<Coord>& suff,
                         const Coord d_ax) -> Coord {
        const auto m = as_.size();
        const auto t
            = static_cast<std::size_t>(std::upper_bound(as_.begin(), as_.end(), q) - as_.begin());
        auto best = Coord{0};
        if (t > 0) best = d_ax * q + pref[t];
        if (t < m) {
            const auto cand = suff[t] - d_ax * q;
            if (cand > best) best = cand;
        }
        return best;
    }

    /// Per-line capacity of the core grid on each axis.
    inline auto core_grid_limit(const NnsConfig& cfg) -> std::array<Coord, 2> {
        return std::array<Coord, 2>{cfg.grid[1], cfg.grid[0] - 1};
    }

}  // namespace nnsplace_detail

/** \brief Interface bridging NNS edge costs to the MinParametricSolver. */
class HowardsCost : public MinParametricAPI<std::size_t, int, Ratio> {
  public:
    HowardsCost(const NnsConfig& cfg, const int axis) : _delta(cfg.delta[axis]) {}

    /** \brief ``(ratio.numerator - cost * ratio.denominator) // (delta * den)`` */
    auto distance(const Ratio& ratio, const int& edge) -> Ratio override {
        const auto n = ratio.numerator();
        const auto d = ratio.denominator();
        const auto c = static_cast<Coord>(edge) * d;
        const auto den = _delta * d;
        return Ratio{floor_div(n - c, den)};
    }

    /** \brief Mean edge cost of a cycle (``Fraction(sum, len)``). */
    auto zero_cancel(const std::vector<int>& cycle) -> Ratio override {
        Coord total = 0;
        for (const auto cost : cycle) total += cost;
        return Ratio{total, static_cast<Coord>(cycle.size())};
    }

  private:
    Coord _delta;
};

/** \brief The NNS placer (port of the Python ``NnsPlacer``). */
class NnsPlacer {
  public:
    const SimpleNetlist& hyprgraph;
    NnsConfig cfg;
    /// count[0][c] = cells on column c; count[1][r] = cells on row r.
    std::array<std::vector<Coord>, 2> count;
    std::array<Coord, 2> grid_limit;
    std::array<Coord, 2> limit;
    std::array<Coord, 2> io_limit;
    Coord reserved_col;
    /// Flow graph: per-module directed neighbour lists.
    FlowGraph ugraph;

    /** \brief Construct the placer and derive all grid limits.
     *
     *  \param netlist    hypergraph netlist to place
     *  \param config     placement configuration (copied)
     *  \param net_driver optional per-net driver module (net order); when
     *                    non-empty the flow graph only links each driver to
     *                    its sinks so the worst wire length is driver->sink
     */
    NnsPlacer(const SimpleNetlist& netlist, const NnsConfig& config,
              std::vector<std::optional<node_t>> net_driver = {});

    /** \brief cost(length, axis) = length * delta[axis] */
    auto cost(const Coord length, const int axis) const -> Coord {
        return length * cfg.delta[axis];
    }

    /** \brief Inverse of cost(): the length a cost corresponds to. */
    auto cost_inv(const Coord c, const int axis) const -> Ratio {
        return Ratio{c, cfg.delta[axis]};
    }

    /** \brief Worst HPWL over all directed flow edges. */
    auto calc_worst_wirelength(const Placement& place) const -> Coord {
        auto worst_wire = Coord{0};
        for (node_t u = 0; u < ugraph.adj.size(); ++u) {
            for (const auto v : ugraph.adj[u]) {
                if (u > v) continue;
                const auto gruv = this->cost(std::abs(place[0][v] - place[0][u]), 0)
                                  + this->cost(std::abs(place[1][v] - place[1][u]), 1);
                if (worst_wire < gruv) worst_wire = gruv;
            }
        }
        return worst_wire;
    }

    /** \brief Worst HPWL of the edges incident to module ``v``. */
    auto calc_worst_wirelength_v(const node_t v, const Placement& place) const -> Coord {
        auto worst_wire = Coord{0};
        for (const auto w : ugraph.adj[v]) {
            const auto gruv = this->cost(std::abs(place[0][v] - place[0][w]), 0)
                              + this->cost(std::abs(place[1][v] - place[1][w]), 1);
            if (worst_wire < gruv) worst_wire = gruv;
        }
        return worst_wire;
    }

    /** \brief Total (delta-scaled) hull length of every net along ``axis``. */
    auto calc_total_hull_length(const std::vector<Coord>& dist, const int axis) const -> Coord {
        auto total_hull_length = Coord{0};
        for (const auto net : hyprgraph.nets) {
            const auto& members = hyprgraph.gr[net];
            auto hull = recti::Interval<Coord>(1000000000000LL, -1000000000000LL);
            for (const auto v : members) {
                hull = hull.hull_with(dist[v]);
            }
            total_hull_length += hull.measure();
        }
        return total_hull_length * cfg.delta[axis];
    }

    /** \brief Sum of both axis hull lengths (half-perimeter wire length). */
    auto calc_total_HPWL(const Placement& place) const -> Coord {
        return this->calc_total_hull_length(place[0], 0)
               + this->calc_total_hull_length(place[1], 1);
    }

    /** \brief Snake-fill an initial random legal placement over the grid. */
    void init_placement(Placement& place, const std::uint32_t seed = 0x5F3759DFU);

    /** \brief One Howard's-algorithm optimization pass along ``axis``. */
    void apply_howard(Placement& place, const int axis);

    /** \brief Score tables allowing O(log deg) moves for module ``v``. */
    auto module_slot_data(const node_t v, const Placement& place, const int axis)
        -> nnsplace_detail::ModuleSlotData;

    /** \brief Add module->slot candidate edges for window radii [r_start, r_stop]. */
    void add_radius_edges(const std::vector<node_t>& lst,
                          std::vector<std::vector<CandidateEdge>>& candidates,
                          const std::vector<nnsplace_detail::ModuleSlotData>& data, const int axis,
                          const Coord r_start, const Coord r_stop);

    /** \brief Connect every module to every free slot (global fallback). */
    void add_all_slots(const std::vector<node_t>& lst,
                       std::vector<std::vector<CandidateEdge>>& candidates, Placement& place,
                       const int axis);

    /** \brief Reassign ``lst`` so every module owns a distinct position. */
    void legalize(const std::vector<node_t>& lst, Placement& place, const int axis);

    /** \brief Legalize every non-empty bucket of equal cross-axis coordinates. */
    void legalize_modules(Placement& place, const int axis) {
        const auto grid_cross = cfg.grid[axis ^ 1];
        std::vector<std::vector<node_t>> buckets(static_cast<std::size_t>(grid_cross) + 2);
        const auto& cross = place[axis ^ 1];
        for (node_t v = 0; v < ugraph.adj.size(); ++v) {
            buckets[static_cast<std::size_t>(cross[v])].push_back(v);
        }
        for (auto& bucket : buckets) {
            if (bucket.empty()) continue;
            this->legalize(bucket, place, axis);
        }
    }

    /** \brief Choose the ring edge + position nearest to pad ``vp``. */
    auto choose_nearest_iopad_vp(const Placement& place, const node_t vp, const int axis)
        -> std::tuple<int, std::optional<Coord>, std::optional<Coord>>;

    /** \brief Snap every I/O pad onto the nearest ring edge. */
    void choose_nearest_iopad(Placement& place);

    /** \brief Spread the pads sitting on either ``axis`` ring edge. */
    void legalize_iopad(Placement& place, const int axis) {
        std::vector<std::vector<node_t>> buckets(2);
        const auto n = static_cast<node_t>(hyprgraph.number_of_modules());
        const auto num_pads = static_cast<node_t>(hyprgraph.num_pads);
        for (node_t i = n - num_pads; i < n; ++i) {
            const auto v = i;
            if (place[axis][v] == 0) {
                buckets[0].push_back(v);
            } else if (place[axis][v] == cfg.grid[axis] + 1) {
                buckets[1].push_back(v);
            }
        }
        if (!buckets[0].empty()) this->legalize(buckets[0], place, axis ^ 1);
        if (!buckets[1].empty()) this->legalize(buckets[1], place, axis ^ 1);
    }

    /** \brief Choose nearest pads then spread each ring edge (io_assign). */
    void io_assign(Placement& place) {
        this->choose_nearest_iopad(place);
        this->legalize_iopad(place, 0);
        this->legalize_iopad(place, 1);
    }

  private:
    /// node -> (neighbour, current arc cost) skeleton, mirroring ``ugraph``.
    std::vector<std::vector<std::pair<uint32_t, int>>> _arcs;

    /** \brief Move modules to their matched slot coordinates and fix counts. */
    void apply_matches(const std::vector<node_t>& lst, const std::vector<uint32_t>& slot_of,
                       Placement& place, const int axis) {
        const auto nmod = static_cast<Coord>(hyprgraph.number_of_modules());
        auto& dist = place[axis];
        for (std::size_t k = 0; k < lst.size(); ++k) {
            const auto v = lst[k];
            const auto q = static_cast<Coord>(slot_of[k]) - nmod;
            if (dist[v] == q) continue;
            --count[axis][static_cast<std::size_t>(dist[v])];
            ++count[axis][static_cast<std::size_t>(q)];
            dist[v] = q;
        }
    }

  public:
    /** \brief One axis pass: Howard optimization + legalization + pad snap. */
    void optimize_axis(Placement& place, const int axis) {
        this->apply_howard(place, axis);
        this->legalize_modules(place, axis ^ 1);
        this->choose_nearest_iopad(place);
    }

    /** \brief Iterate both axes until the worst wire length stops improving. */
    auto optimize(Placement& place, const int max_iters) -> std::pair<int, Coord> {
        auto worst0 = this->calc_worst_wirelength(place);
        auto state = this->snapshot(place);
        for (int niter = 0; niter < max_iters; ++niter) {
            this->optimize_axis(place, 0);
            this->optimize_axis(place, 1);
            const auto worst1 = this->calc_worst_wirelength(place);
            if (worst1 >= worst0) {
                this->restore(state, place);
                return {niter, worst0};
            }
            worst0 = worst1;
            state = this->snapshot(place);
        }
        return {max_iters, worst0};
    }

    /** \brief Full placement run: optimize then re-assign the I/O pads. */
    auto run(Placement& place, const int max_iters = 2000) -> std::pair<int, Coord> {
        auto worst0 = this->calc_worst_wirelength(place);
        auto state = this->snapshot(place);
        auto worst1 = worst0;
        for (int niter = 0; niter < max_iters; ++niter) {
            (void)this->optimize(place, max_iters);
            this->io_assign(place);
            worst1 = this->calc_worst_wirelength(place);
            if (worst1 >= worst0) {
                this->restore(state, place);
                return {niter, worst0};
            }
            worst0 = worst1;
            state = this->snapshot(place);
        }
        return {max_iters, worst0};
    }

  private:
    /** \brief Deep copy of the coordinates + occupancy counts (memento). */
    auto snapshot(const Placement& place) const
        -> std::pair<Placement, std::array<std::vector<Coord>, 2>> {
        return {place, count};
    }

    /** \brief Roll back coordinates + counts to a captured state. */
    void restore(const std::pair<Placement, std::array<std::vector<Coord>, 2>>& state,
                 Placement& place) {
        place = state.first;
        count = state.second;
    }
};
