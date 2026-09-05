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
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include <digraphx/min_parametric_q.hpp>
#include <fractions/pyfractions.hpp>
#include <mywheel/map_adapter.hpp>
#include <netlistx/netlist.hpp>
#include <recti/interval.hpp>
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
 */
inline auto create_flow_graph(const SimpleNetlist& hyprgraph) -> FlowGraph {
    const auto nmod = static_cast<node_t>(hyprgraph.number_of_modules());
    std::vector<std::vector<node_t>> adj(nmod);
    for (const auto net : hyprgraph.nets) {
        const auto& members = hyprgraph.gr[net];
        std::vector<node_t> verts(members.begin(), members.end());
        for (const auto v1 : verts) {
            for (const auto v2 : verts) {
                if (hyprgraph.get_module_weight(static_cast<uint32_t>(v2)) == 0U) continue;
                adj[v1].push_back(v2);
                adj[v2].push_back(v1);
            }
        }
    }
    for (auto& lst : adj) {
        std::sort(lst.begin(), lst.end());
        lst.erase(std::unique(lst.begin(), lst.end()), lst.end());
    }
    return FlowGraph{std::move(adj)};
}

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
        const auto t = static_cast<std::size_t>(
            std::upper_bound(as_.begin(), as_.end(), q) - as_.begin());
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
     *  \param netlist  hypergraph netlist to place
     *  \param config   placement configuration (copied)
     */
    NnsPlacer(const SimpleNetlist& netlist, const NnsConfig& config)
        : hyprgraph(netlist), cfg(config), ugraph{create_flow_graph(netlist)} {
        const auto num_modules = static_cast<node_t>(hyprgraph.number_of_modules());
        const auto num_pads = static_cast<node_t>(hyprgraph.num_pads);
        count[0].assign(static_cast<std::size_t>(cfg.grid[0]) + 2, 0);
        count[1].assign(static_cast<std::size_t>(cfg.grid[1]) + 2, 0);
        grid_limit = nnsplace_detail::core_grid_limit(cfg);
        if (cfg.line_cap_ratio) {
            const auto num_cells = num_modules - num_pads;
            const auto cap = static_cast<Coord>(
                std::ceil(std::sqrt(static_cast<double>(num_cells)) * *cfg.line_cap_ratio));
            limit = {std::min(grid_limit[0], cap), std::min(grid_limit[1], cap)};
        } else {
            limit = grid_limit;
        }
        const auto io_cap = num_pads > 0 ? static_cast<Coord>((num_pads + 1) / 2)
                                         : std::max(grid_limit[0], grid_limit[1]);
        io_limit = {std::min(grid_limit[0], io_cap), std::min(grid_limit[1], io_cap)};
        reserved_col = cfg.reserved_col;

        // Arc skeleton for apply_howard: node -> (neighbour, mutable cost).
        _arcs.assign(num_modules, {});
        for (node_t u = 0; u < num_modules; ++u) {
            auto& arcs = _arcs[u];
            arcs.reserve(ugraph.adj[u].size());
            for (const auto v : ugraph.adj[u]) {
                arcs.emplace_back(static_cast<uint32_t>(v), 0);
            }
        }
    }

    /** \brief cost(length, axis) = length * delta[axis] */
    auto cost(const Coord length, const int axis) const -> Coord { return length * cfg.delta[axis]; }

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
    void init_placement(Placement& place, const std::uint32_t seed = 0x5F3759DFU) {
        const auto num_modules = static_cast<node_t>(hyprgraph.number_of_modules());
        std::vector<node_t> lst(num_modules);
        std::iota(lst.begin(), lst.end(), 0);
        std::mt19937 gen(seed);
        std::shuffle(lst.begin(), lst.end(), gen);

        auto col = Coord{1};
        auto row = Coord{1};
        for (const auto v : lst) {
            place[0][v] = col;
            place[1][v] = row;
            ++count[0][static_cast<std::size_t>(col)];
            ++count[1][static_cast<std::size_t>(row)];
            if (col == cfg.grid[0]) {
                col = 1;
                ++row;
            } else {
                ++col;
            }
            if (col == reserved_col) ++col;
        }
        assert(count[0][static_cast<std::size_t>(reserved_col)] == 0);
        assert(count[0][1] <= grid_limit[0]);
        assert(count[1][1] <= grid_limit[1]);
    }

    /** \brief One Howard's-algorithm optimization pass along ``axis``. */
    void apply_howard(Placement& place, const int axis) {
        const auto oppo = axis ^ 1;
        const auto delta_op = cfg.delta[oppo];
        auto worst = Coord{0};
        for (node_t u = 0; u < _arcs.size(); ++u) {
            for (auto& [v, arc_cost] : _arcs[u]) {
                const auto raw = std::abs(place[oppo][v] - place[oppo][u]);
                arc_cost = static_cast<int>(delta_op * raw);
                if (worst < raw) worst = raw;
            }
        }

        const auto num_modules = static_cast<node_t>(hyprgraph.number_of_modules());
        std::vector<Ratio> dist(num_modules);
        for (node_t v = 0; v < num_modules; ++v) dist[v] = Ratio{place[axis][v]};

        auto& cnt = count[axis];
        const auto grid = cfg.grid[axis];
        const auto line = limit[axis];
        const auto update_ok = [&cnt, grid, line, this](const Ratio& from, const Ratio& to) {
            const auto from_where = from.numerator();
            const auto to_where = to.numerator();
            if (to_where <= 0 || to_where > grid) return false;
            if (cnt[static_cast<std::size_t>(to_where)] >= line) return false;
            ++cnt[static_cast<std::size_t>(to_where)];
            --cnt[static_cast<std::size_t>(from_where)];
            return true;
        };

        HowardsCost omega{cfg, axis};
        MapAdapter<std::vector<std::vector<std::pair<uint32_t, int>>>> ga(_arcs);
        MinParametricSolver<decltype(ga), Ratio, Ratio> solver{ga, omega};
        auto result = solver.run(dist, Ratio{worst}, update_ok);
        (void)result;
        for (node_t v = 0; v < num_modules; ++v) {
            place[axis][v] = dist[v].numerator();
        }
    }

    /** \brief Score tables allowing O(log deg) moves for module ``v``. */
    auto module_slot_data(const node_t v, const Placement& place, const int axis)
        -> nnsplace_detail::ModuleSlotData {
        const auto p0 = place[axis][v];
        std::vector<node_t> nbrs;
        nbrs.reserve(ugraph.adj[v].size());
        for (const auto w : ugraph.adj[v]) {
            if (w != v) nbrs.push_back(w);
        }
        if (nbrs.empty()) {
            return nnsplace_detail::ModuleSlotData{p0, {}, {}, {}, Coord{0}};
        }
        const auto oppo = axis ^ 1;
        const auto o0 = place[oppo][v];
        const auto d_ax = cfg.delta[axis];
        const auto d_op = cfg.delta[oppo];

        std::vector<std::pair<Coord, Coord>> pairs;
        pairs.reserve(nbrs.size());
        for (const auto w : nbrs) {
            pairs.emplace_back(place[axis][w], d_op * std::abs(o0 - place[oppo][w]));
        }
        std::sort(pairs.begin(), pairs.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        const auto m = pairs.size();
        std::vector<Coord> as_(m);
        for (std::size_t t = 0; t < m; ++t) as_[t] = pairs[t].first;

        std::vector<Coord> pref(m + 1, 0);
        auto mx = pairs[0].second - d_ax * pairs[0].first;
        pref[1] = mx;
        for (std::size_t t = 2; t <= m; ++t) {
            const auto c = pairs[t - 1].second - d_ax * pairs[t - 1].first;
            if (c > mx) mx = c;
            pref[t] = mx;
        }
        std::vector<Coord> suff(m + 1, 0);
        mx = pairs[m - 1].second + d_ax * pairs[m - 1].first;
        suff[m - 1] = mx;
        for (std::size_t t = m - 1; t-- > 0;) {
            const auto c = pairs[t].second + d_ax * pairs[t].first;
            if (c > mx) mx = c;
            suff[t] = mx;
        }
        const auto w0 = nnsplace_detail::worst_at(p0, as_, pref, suff, d_ax);
        return nnsplace_detail::ModuleSlotData{p0, std::move(as_), std::move(pref),
                                               std::move(suff), w0};
    }

    /** \brief Add module->slot candidate edges for window radii [r_start, r_stop]. */
    void add_radius_edges(const std::vector<node_t>& lst,
                          std::vector<std::vector<CandidateEdge>>& candidates,
                          const std::vector<nnsplace_detail::ModuleSlotData>& data,
                          const int axis, const Coord r_start, const Coord r_stop) {
        const auto grid = cfg.grid[axis];
        const auto nmod = static_cast<Coord>(hyprgraph.number_of_modules());
        const auto d_ax = cfg.delta[axis];
        const auto reserved = (axis == 0);
        for (auto ring = r_start; ring <= r_stop; ++ring) {
            for (std::size_t k = 0; k < lst.size(); ++k) {
                const auto& sd = data[k];
                const auto p0 = sd.p0;
                const auto q0 = p0 + nmod;
                auto q = p0 - ring;
                if (q > 0 && !(reserved && q == reserved_col)) {
                    const auto w1 = sd.as_.empty()
                                        ? Coord{0}
                                        : nnsplace_detail::worst_at(q, sd.as_, sd.pref, sd.suff,
                                                                    d_ax);
                    candidates[k].emplace_back(static_cast<uint32_t>(q0 - ring), w1 - sd.w0);
                }
                q = p0 + ring;
                if (q <= grid && !(reserved && q == reserved_col)) {
                    const auto w1 = sd.as_.empty()
                                        ? Coord{0}
                                        : nnsplace_detail::worst_at(q, sd.as_, sd.pref, sd.suff,
                                                                    d_ax);
                    candidates[k].emplace_back(static_cast<uint32_t>(q0 + ring), w1 - sd.w0);
                }
            }
        }
    }

    /** \brief Connect every module to every free slot (global fallback). */
    void add_all_slots(const std::vector<node_t>& lst,
                       std::vector<std::vector<CandidateEdge>>& candidates, Placement& place,
                       const int axis) {
        const auto grid = cfg.grid[axis];
        const auto nmod = static_cast<Coord>(hyprgraph.number_of_modules());
        for (std::size_t k = 0; k < lst.size(); ++k) {
            const auto v = lst[k];
            const auto p0 = place[axis][v];
            const auto w0 = this->calc_worst_wirelength_v(v, place);
            for (auto pos = Coord{1}; pos <= grid; ++pos) {
                if (axis == 0 && pos == reserved_col) continue;
                place[axis][v] = pos;
                const auto w1 = this->calc_worst_wirelength_v(v, place);
                candidates[k].emplace_back(static_cast<uint32_t>(pos + nmod), w1 - w0);
            }
            place[axis][v] = p0;
        }
    }

    /** \brief Reassign ``lst`` so every module owns a distinct position. */
    void legalize(const std::vector<node_t>& lst, Placement& place, const int axis) {
        auto& dist = place[axis];
        const auto nmod = static_cast<Coord>(hyprgraph.number_of_modules());
        const auto m = lst.size();

        std::vector<nnsplace_detail::ModuleSlotData> data;
        data.reserve(m);
        for (const auto v : lst) data.push_back(this->module_slot_data(v, place, axis));

        // Module -> slot-node candidate edges, indexed by position in `lst`.
        std::vector<std::vector<CandidateEdge>> candidates(m);
        for (std::size_t k = 0; k < m; ++k) {
            const auto v = lst[k];
            const auto q = dist[v] + nmod;
            if (axis == 0 && dist[v] == reserved_col) continue;
            candidates[k].emplace_back(static_cast<uint32_t>(q), 0);  // stay put
        }

        // Primary strategy: grow a +/- radius window until an assignment exists.
        constexpr Coord kNeighborhood = 11;
        constexpr Coord kMaxNeighborhood = 50;
        this->add_radius_edges(lst, candidates, data, axis, 1, kNeighborhood - 1);
        auto ring = kNeighborhood;
        while (ring < kMaxNeighborhood) {
            auto matched = min_weight_full_matching(candidates);
            if (matched) {
                this->apply_matches(lst, *matched, place, axis);
                return;
            }
            this->add_radius_edges(lst, candidates, data, axis, ring, ring);
            ++ring;
        }

        // Fallback: every free slot along the axis.
        std::vector<std::vector<CandidateEdge>> global(m);
        for (std::size_t k = 0; k < m; ++k) {
            global[k] = candidates[k];
        }
        std::vector<node_t> full_lst = lst;
        this->add_all_slots(full_lst, global, place, axis);
        auto matched = min_weight_full_matching(global);
        if (!matched) {
            throw std::runtime_error("Failed to legalize " + std::to_string(m)
                                     + " modules on axis " + std::to_string(axis) + " of grid "
                                     + std::to_string(cfg.grid[0]) + "x"
                                     + std::to_string(cfg.grid[1])
                                     + ": not enough free slots for the bucket (reserved_col="
                                     + std::to_string(reserved_col) + ").");
        }
        this->apply_matches(lst, *matched, place, axis);
    }

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
        -> std::tuple<int, std::optional<Coord>, std::optional<Coord>> {
        const auto oppo = axis ^ 1;
        const auto dx = cfg.delta[axis];
        const auto dy = cfg.delta[oppo];
        const auto grid = cfg.grid[axis];

        auto max0 = Coord{-1000000000000LL};        auto min0 = Coord{1000000000000LL};
        auto max1 = Coord{-1000000000000LL};
        auto min1 = Coord{1000000000000LL};
        for (const auto vi : ugraph.adj[vp]) {
            const auto li0 = dx * place[axis][vi];
            const auto li1 = dx * (grid - place[axis][vi]);
            const auto ui = dy * place[oppo][vi];
            const auto tem_max0 = ui + li0;
            const auto tem_min0 = ui - li0;
            const auto tem_max1 = ui + li1;
            const auto tem_min1 = ui - li1;
            max0 = std::max(tem_max0, max0);
            min0 = std::min(tem_min0, min0);
            max1 = std::max(tem_max1, max1);
            min1 = std::min(tem_min1, min1);
        }
        const auto worst0 = floor_div(max0 - min0 + 1, 2);
        const auto pos0 = floor_div(max0 + min0, 2 * dx);
        const auto worst1 = floor_div(max1 - min1 + 1, 2);
        const auto pos1 = floor_div(max1 + min1, 2 * dx);

        const auto full0 = count[axis][0] >= io_limit[axis];
        const auto full1 = count[axis][static_cast<std::size_t>(grid + 1)] >= io_limit[axis];
        if (full0 && full1) return {2, std::nullopt, std::nullopt};
        const auto pick0 = full0 != full1 ? !full0 : worst0 <= worst1;
        if (pick0) return {0, pos0, worst0};
        return {1, pos1, worst1};
    }

    /** \brief Snap every I/O pad onto the nearest ring edge. */
    void choose_nearest_iopad(Placement& place) {
        const auto n = static_cast<node_t>(hyprgraph.number_of_modules());
        const auto num_pads = static_cast<node_t>(hyprgraph.num_pads);
        const auto grid_x = cfg.grid[0];
        const auto grid_y = cfg.grid[1];
        for (node_t i = n - num_pads; i < n; ++i) {
            const auto vp = i;
            const auto [which_x, posy, worstx] = this->choose_nearest_iopad_vp(place, vp, 0);
            const auto [which_y, posx, worsty] = this->choose_nearest_iopad_vp(place, vp, 1);

            --count[0][static_cast<std::size_t>(place[0][vp])];
            --count[1][static_cast<std::size_t>(place[1][vp])];

            const auto full_x = (which_x == 2);
            const auto full_y = (which_y == 2);
            if (full_x && full_y) throw std::runtime_error("Not enough I/O area!!!");

            if (full_x) {
                place[1][vp] = (which_y == 0) ? 0 : grid_y + 1;
                place[0][vp] = posx.value();
            } else if (full_y) {
                place[0][vp] = (which_x == 0) ? 0 : grid_x + 1;
                place[1][vp] = posy.value();
            } else {
                if (worstx.value() <= worsty.value()) {
                    place[0][vp] = (which_x == 0) ? 0 : grid_x + 1;
                    place[1][vp] = posy.value();
                } else {
                    place[1][vp] = (which_y == 0) ? 0 : grid_y + 1;
                    place[0][vp] = posx.value();
                }
            }
            ++count[1][static_cast<std::size_t>(place[1][vp])];
            ++count[0][static_cast<std::size_t>(place[0][vp])];
        }
    }

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
    void apply_matches(const std::vector<node_t>& lst,
                       const std::vector<uint32_t>& slot_of, Placement& place, const int axis) {
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
