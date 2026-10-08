/*! \file placement.cpp
 *  \brief Out-of-line definitions of the NNS placement engine.
 *
 *  Large function bodies (member or free) live here so that
 *  ``placement.hpp`` only declares them; bodies of 15 lines or fewer stay
 *  inline in the header.  The implementation is a faithful port of the Python
 *  ``nnsplace.placement`` module.
 */

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <digraphx/min_parametric_q.hpp>
#include <fractions/pyfractions.hpp>
#include <mywheel/map_adapter.hpp>
#include <nnsplace/placement.hpp>
#include <numeric>
#include <random>
#include <recti/interval.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

auto create_flow_graph(const SimpleNetlist& hyprgraph,
                       const std::vector<std::optional<node_t>>& net_driver) -> FlowGraph {
    const auto nmod = static_cast<node_t>(hyprgraph.number_of_modules());
    const auto num_nets = hyprgraph.number_of_nets();
    const auto directed = net_driver.size() == static_cast<std::size_t>(num_nets);
    std::vector<std::vector<node_t>> adj(nmod);
    std::size_t net_index = 0;
    for (const auto net : hyprgraph.nets) {
        const auto& members = hyprgraph.gr[net];
        std::vector<node_t> verts(members.begin(), members.end());
        if (directed) {
            const auto& driver = net_driver[net_index];
            if (driver && std::ranges::find(verts, *driver) != verts.end()) {
                for (const auto sink : verts) {
                    if (sink == *driver) continue;
                    if (hyprgraph.get_module_weight(static_cast<uint32_t>(*driver)) == 0U
                        && hyprgraph.get_module_weight(static_cast<uint32_t>(sink)) == 0U) {
                        continue;  // ignore pad to pad connections
                    }
                    adj[*driver].push_back(sink);
                    adj[sink].push_back(*driver);
                }
                ++net_index;
                continue;
            }
        }
        for (const auto v1 : verts) {
            for (const auto v2 : verts) {
                if (hyprgraph.get_module_weight(static_cast<uint32_t>(v2)) == 0U) continue;
                adj[v1].push_back(v2);
                adj[v2].push_back(v1);
            }
        }
        ++net_index;
    }
    for (auto& lst : adj) {
        std::ranges::sort(lst);
        lst.erase(std::ranges::unique(lst).begin(), lst.end());
    }
    return FlowGraph{std::move(adj)};
}

NnsPlacer::NnsPlacer(const SimpleNetlist& netlist, const NnsConfig& config,
                     const std::vector<std::optional<node_t>>& net_driver)
    : hyprgraph(netlist), cfg(config), ugraph{create_flow_graph(netlist, net_driver)} {
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

void NnsPlacer::init_placement(Placement& place, const std::uint32_t seed) {
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

void NnsPlacer::apply_howard(Placement& place, const int axis) {
    const auto oppo = axis ^ 1;
    const auto delta_op = cfg.delta[oppo];
    auto worst = Coord{0};
    for (node_t u = 0; u < _arcs.size(); ++u) {
        for (auto& [v, arc_cost] : _arcs[u]) {
            const auto raw = std::abs(place[oppo][v] - place[oppo][u]);
            arc_cost = static_cast<int>(delta_op * raw);
            worst = std::max(worst, raw);
        }
    }

    const auto num_modules = static_cast<node_t>(hyprgraph.number_of_modules());
    std::vector<Coord> dist(num_modules);
    for (node_t v = 0; v < num_modules; ++v) dist[v] = place[axis][v];

    auto& cnt = count[axis];
    const auto grid = cfg.grid[axis];
    const auto line = limit[axis];
    const auto update_ok = [&cnt, grid, line](const Coord& from_where, const Coord& to_where) {
        if (to_where <= 0 || to_where > grid) return false;
        if (cnt[static_cast<std::size_t>(to_where)] >= line) return false;
        ++cnt[static_cast<std::size_t>(to_where)];
        --cnt[static_cast<std::size_t>(from_where)];
        return true;
    };

    HowardsCost omega{cfg, axis};
    MapAdapter<std::vector<std::vector<std::pair<uint32_t, int>>>> ga(_arcs);
    // Integer distance domain (like the Python reference, whose `dist` is a
    // plain int dict): only the ratio stays rational, so the per-edge
    // relaxation arithmetic avoids Fraction gcd work.
    MinParametricSolver<decltype(ga), Ratio, Coord> solver{ga, omega};
    auto result = solver.run(dist, Ratio{worst}, update_ok);
    (void)result;
    for (node_t v = 0; v < num_modules; ++v) {
        place[axis][v] = dist[v];
    }
}

auto NnsPlacer::module_slot_data(const node_t v, const Placement& place, const int axis)
    -> nnsplace_detail::ModuleSlotData {
    const auto p0 = place[axis][v];
    std::vector<node_t> nbrs;
    nbrs.reserve(ugraph.adj[v].size());
    for (const auto w : ugraph.adj[v]) {
        if (w != v) nbrs.push_back(w);
    }
    if (nbrs.empty()) {
        return nnsplace_detail::ModuleSlotData{
            .p0 = p0, .as_ = {}, .pref = {}, .suff = {}, .w0 = Coord{0}};
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
    std::ranges::sort(pairs, [](const auto& a, const auto& b) { return a.first < b.first; });

    const auto m = pairs.size();
    std::vector<Coord> as_(m);
    for (std::size_t t = 0; t < m; ++t) as_[t] = pairs[t].first;

    std::vector<Coord> pref(m + 1, 0);
    auto mx = pairs[0].second - d_ax * pairs[0].first;
    pref[1] = mx;
    for (std::size_t t = 2; t <= m; ++t) {
        const auto c = pairs[t - 1].second - d_ax * pairs[t - 1].first;
        mx = std::max(c, mx);
        pref[t] = mx;
    }
    std::vector<Coord> suff(m + 1, 0);
    mx = pairs[m - 1].second + d_ax * pairs[m - 1].first;
    suff[m - 1] = mx;
    for (std::size_t t = m - 1; t-- > 0;) {
        const auto c = pairs[t].second + d_ax * pairs[t].first;
        mx = std::max(c, mx);
        suff[t] = mx;
    }
    const auto w0 = nnsplace_detail::worst_at(p0, as_, pref, suff, d_ax);
    return nnsplace_detail::ModuleSlotData{.p0 = p0,
                                           .as_ = std::move(as_),
                                           .pref = std::move(pref),
                                           .suff = std::move(suff),
                                           .w0 = w0};
}

void NnsPlacer::add_radius_edges(const std::vector<node_t>& lst,
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
            if (q > 0 && (!reserved || q != reserved_col)) {
                const auto w1 = sd.as_.empty()
                                    ? Coord{0}
                                    : nnsplace_detail::worst_at(q, sd.as_, sd.pref, sd.suff, d_ax);
                candidates[k].emplace_back(static_cast<uint32_t>(q0 - ring), w1 - sd.w0);
            }
            q = p0 + ring;
            if (q <= grid && (!reserved || q != reserved_col)) {
                const auto w1 = sd.as_.empty()
                                    ? Coord{0}
                                    : nnsplace_detail::worst_at(q, sd.as_, sd.pref, sd.suff, d_ax);
                candidates[k].emplace_back(static_cast<uint32_t>(q0 + ring), w1 - sd.w0);
            }
        }
    }
}

void NnsPlacer::add_all_slots(const std::vector<node_t>& lst,
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

void NnsPlacer::legalize(const std::vector<node_t>& lst, Placement& place, const int axis) {
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
    const auto grid = cfg.grid[axis];
    this->add_radius_edges(lst, candidates, data, axis, 1, kNeighborhood - 1);
    auto ring = kNeighborhood;
    while (ring < kMaxNeighborhood) {
        auto matched = min_weight_full_matching(candidates);
        if (matched) {
            this->apply_matches(lst, *matched, place, axis);
            return;
        }
        // Once the window spans the whole line it can no longer gain a slot, so
        // every further ring would repeat the same infeasible assignment; fall
        // through to the global fallback instead.
        if (ring >= grid) break;
        this->add_radius_edges(lst, candidates, data, axis, ring, ring);
        ++ring;
    }

    // Fallback: every free slot along the axis.
    std::vector<std::vector<CandidateEdge>> global(m);
    for (std::size_t k = 0; k < m; ++k) {
        global[k] = candidates[k];
    }
    const std::vector<node_t>& full_lst = lst;
    this->add_all_slots(full_lst, global, place, axis);
    auto matched = min_weight_full_matching(global);
    if (!matched) {
        throw std::runtime_error("Failed to legalize " + std::to_string(m) + " modules on axis "
                                 + std::to_string(axis) + " of grid " + std::to_string(cfg.grid[0])
                                 + "x" + std::to_string(cfg.grid[1])
                                 + ": not enough free slots for the bucket (reserved_col="
                                 + std::to_string(reserved_col) + ").");
    }
    this->apply_matches(lst, *matched, place, axis);
}

auto NnsPlacer::choose_nearest_iopad_vp(const Placement& place, const node_t vp, const int axis)
    -> std::tuple<int, std::optional<Coord>, std::optional<Coord>> {
    const auto oppo = axis ^ 1;
    const auto dx = cfg.delta[axis];
    const auto dy = cfg.delta[oppo];
    const auto grid = cfg.grid[axis];

    auto max0 = Coord{-1000000000000LL};
    auto min0 = Coord{1000000000000LL};
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

void NnsPlacer::choose_nearest_iopad(Placement& place) {
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
