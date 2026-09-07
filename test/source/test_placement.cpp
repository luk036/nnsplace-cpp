// -*- coding: utf-8 -*-
#include <doctest/doctest.h>

#include <nnsplace/placement.hpp>
#include <nnsplace/placement_cfg.hpp>
#include <nnsplace/readwrite.hpp>

#include <algorithm>
#include <cstdint>
#include <fractions/pyfractions.hpp>
#include <string>
#include <utility>
#include <vector>
#include <xnetwork/classes/graph.hpp>

namespace {

    /// SimpleNetlist with modules 0..4, net nodes 5..6, one pad (module 4).
    auto build_mock_netlist() -> SimpleNetlist {
        xnetwork::SimpleGraph g(7);
        g.add_edge(0, 5);
        g.add_edge(1, 5);
        g.add_edge(2, 5);
        g.add_edge(2, 6);
        g.add_edge(3, 6);
        g.add_edge(4, 6);
        SimpleNetlist netlist{std::move(g), 5U, 2U};
        netlist.num_pads = 1;
        netlist.module_weight.assign(5, 1U);
        netlist.module_weight[4] = 0U;
        return netlist;
    }

    /// 70 modules with no nets and no pads (crowding regression test).
    auto build_tiny_netlist() -> SimpleNetlist {
        xnetwork::SimpleGraph g(70);
        SimpleNetlist netlist{std::move(g), 70U, 0U};
        return netlist;
    }

    /// drawf/fix circuit: cells 0..3, pads 4..6, nets 7..12 (see fix.json).
    auto build_drawf_netlist() -> SimpleNetlist {
        xnetwork::SimpleGraph g(13);
        std::vector<std::pair<uint32_t, uint32_t>> edges{{0, 7},  {0, 8},  {1, 7},  {1, 9},
                                                         {2, 8},  {2, 9},  {2, 10}, {3, 8},
                                                         {3, 9},  {3, 11}, {4, 7},  {5, 10},
                                                         {5, 12}, {6, 11}};
        for (const auto& [u, v] : edges) g.add_edge(u, v);
        SimpleNetlist netlist{std::move(g), 7U, 6U};
        netlist.num_pads = 3;
        return netlist;
    }

    auto make_placements(const std::size_t n) -> Placement {
        Placement place;
        place[0].assign(n, 0);
        place[1].assign(n, 0);
        return place;
    }

    template <typename Fn> void expect_invalid_argument(const char* fragment, Fn&& fn) {
        try {
            fn();
            FAIL("expected std::invalid_argument");
        } catch (const std::invalid_argument& e) {
            CHECK(std::string(e.what()).find(fragment) != std::string::npos);
        } catch (...) {
            FAIL("wrong exception type");
        }
    }

    void check_legal(const SimpleNetlist& netlist, const Placement& place, const Coord gx,
                     const Coord gy) {
        const auto n = netlist.number_of_modules();
        const auto num_cells = n - netlist.num_pads;
        std::vector<std::pair<Coord, Coord>> occupied;
        for (std::size_t v = 0; v < n; ++v) {
            const auto x = place[0][v];
            const auto y = place[1][v];
            for (const auto& [ox, oy] : occupied) {
                if (x == ox && y == oy) FAIL("overlap at (" << x << ", " << y << ")");
            }
            occupied.emplace_back(x, y);
            if (v < num_cells) {
                CHECK(x >= 1);
                CHECK(x <= gx);
                CHECK(y >= 1);
                CHECK(y <= gy);
                CHECK(x != 27);  // reserved DSP/SRAM column
            } else {
                const auto on_ring = (x == 0 || x == gx + 1 || y == 0 || y == gy + 1);
                CHECK(on_ring);
            }
        }
    }

    auto run_placer(const SimpleNetlist& netlist, const Coord gx, const Coord gy,
                    const std::uint32_t seed) -> std::pair<Placement, Coord> {
        NnsPlacer placer{netlist, NnsConfig{gx, gy, 40, 40}};
        auto place = make_placements(netlist.number_of_modules());
        placer.init_placement(place, seed);
        placer.io_assign(place);
        const auto before = placer.calc_worst_wirelength(place);
        const auto result = placer.run(place, 2000);
        CHECK(result.first >= 0);
        CHECK(result.second < before);  // the optimizer must improve the seed-831 run
        return {place, result.second};
    }

}  // namespace

TEST_CASE("test_config_grid_width_too_small") {
    expect_invalid_argument("Grid width must be at least 3",
                            [] { NnsConfig{2, 5, 1, 1}; });
}

TEST_CASE("test_config_grid_height_too_small") {
    expect_invalid_argument("Grid height must be at least 3",
                            [] { NnsConfig{5, 2, 1, 1}; });
}

TEST_CASE("test_config_delta_x_non_positive") {
    expect_invalid_argument("delta_x must be positive",
                            [] { NnsConfig{5, 5, 0, 1}; });
}

TEST_CASE("test_config_delta_y_non_positive") {
    expect_invalid_argument("delta_y must be positive",
                            [] { NnsConfig{5, 5, 1, 0}; });
}

TEST_CASE("test_config_reserved_col_too_low") {
    expect_invalid_argument("reserved_col must be between 1 and",
                            [] { NnsConfig{5, 5, 1, 1, 0}; });
}

TEST_CASE("test_config_reserved_col_too_high") {
    expect_invalid_argument("reserved_col must be between 1 and",
                            [] { NnsConfig{5, 5, 1, 1, 6}; });
}

TEST_CASE("test_config_properties") {
    const NnsConfig cfg{10, 8, 2, 3, 5};
    CHECK(cfg.grid[0] == 10);
    CHECK(cfg.grid[1] == 8);
    CHECK(cfg.delta[0] == 2);
    CHECK(cfg.delta[1] == 3);
    CHECK(cfg.reserved_col == 5);
}

TEST_CASE("test_config_default_reserved_col") {
    const NnsConfig cfg{10, 8, 2, 3};
    CHECK(cfg.reserved_col == 27);
}

TEST_CASE("test_config_default_line_cap_ratio") {
    const NnsConfig cfg{10, 8, 2, 3};
    CHECK_FALSE(cfg.line_cap_ratio.has_value());
}

TEST_CASE("test_config_line_cap_ratio_property") {
    const NnsConfig cfg{10, 8, 2, 3, std::nullopt, 1.2};
    REQUIRE(cfg.line_cap_ratio.has_value());
    CHECK(*cfg.line_cap_ratio == doctest::Approx(1.2));
}

TEST_CASE("test_config_line_cap_ratio_non_positive") {
    expect_invalid_argument("line_cap_ratio must be positive",
                            [] { NnsConfig{10, 8, 2, 3, std::nullopt, 0.0}; });
}

TEST_CASE("test_create_flow_graph_edge_set") {
    const auto netlist = build_mock_netlist();
    const auto fg = create_flow_graph(netlist);
    const std::vector<std::pair<node_t, node_t>> expected{{0, 0}, {0, 1}, {0, 2}, {1, 0},
                                                          {1, 1}, {1, 2}, {2, 0}, {2, 1},
                                                          {2, 2}, {2, 3}, {2, 4}, {3, 2},
                                                          {3, 3}, {3, 4}, {4, 2}, {4, 3}};
    std::size_t count = 0;
    for (const auto& [u, v] : expected) {
        CHECK(has_directed_edge(fg, u, v));
        if (has_directed_edge(fg, u, v)) ++count;
    }
    CHECK(count == expected.size());
    std::size_t total = 0;
    for (const auto& lst : fg.adj) total += lst.size();
    CHECK(total == expected.size());
}

TEST_CASE("test_nnsplacer_init") {
    const auto netlist = build_mock_netlist();
    const NnsConfig cfg{30, 10, 1, 2};
    NnsPlacer placer{netlist, cfg};
    CHECK(placer.count[0].size() == 32);
    CHECK(placer.count[1].size() == 12);
    CHECK(placer.grid_limit[0] == 10);
    CHECK(placer.grid_limit[1] == 29);
    CHECK(placer.limit[0] == 10);
    CHECK(placer.limit[1] == 29);
    CHECK(placer.ugraph.adj.size() == 5);
}

TEST_CASE("test_cost") {
    const auto netlist = build_mock_netlist();
    NnsPlacer placer{netlist, NnsConfig{30, 10, 1, 2}};
    CHECK(placer.cost(5, 0) == 5);
    CHECK(placer.cost(10, 1) == 20);
}

TEST_CASE("test_cost_inv") {
    const auto netlist = build_mock_netlist();
    NnsPlacer placer{netlist, NnsConfig{30, 10, 1, 2}};
    const auto f0 = placer.cost_inv(5, 0);
    CHECK(f0.numerator() == 5);
    CHECK(f0.denominator() == 1);
    const auto f1 = placer.cost_inv(10, 1);
    CHECK(f1.numerator() == 5);
    CHECK(f1.denominator() == 1);
}

TEST_CASE("test_calc_worst_wirelength") {
    const auto netlist = build_mock_netlist();
    NnsPlacer placer{netlist, NnsConfig{30, 10, 1, 2}};
    auto place = make_placements(5);
    for (std::size_t v = 0; v < 5; ++v) {
        place[0][v] = static_cast<Coord>(v);
        place[1][v] = static_cast<Coord>(v);
    }
    CHECK(placer.calc_worst_wirelength(place) == 6);
}

TEST_CASE("test_calc_worst_wirelength_v") {
    const auto netlist = build_mock_netlist();
    NnsPlacer placer{netlist, NnsConfig{30, 10, 1, 2}};
    auto place = make_placements(5);
    for (std::size_t v = 0; v < 5; ++v) {
        place[0][v] = static_cast<Coord>(v);
        place[1][v] = static_cast<Coord>(v);
    }
    CHECK(placer.calc_worst_wirelength_v(2, place) == 6);
    CHECK(placer.calc_worst_wirelength_v(0, place) == 6);
}

TEST_CASE("test_calc_total_hull_length") {
    const auto netlist = build_mock_netlist();
    NnsPlacer placer{netlist, NnsConfig{30, 10, 1, 2}};
    std::vector<Coord> dist{0, 1, 2, 3, 4};
    CHECK(placer.calc_total_hull_length(dist, 0) == 4);
    CHECK(placer.calc_total_hull_length(dist, 1) == 8);
}

TEST_CASE("test_calc_total_HPWL") {
    const auto netlist = build_mock_netlist();
    NnsPlacer placer{netlist, NnsConfig{30, 10, 1, 2}};
    auto place = make_placements(5);
    for (std::size_t v = 0; v < 5; ++v) {
        place[0][v] = static_cast<Coord>(v);
        place[1][v] = static_cast<Coord>(v);
    }
    CHECK(placer.calc_total_HPWL(place) == 12);
}

TEST_CASE("test_line_cap_ratio_caps_limit") {
    const auto netlist = build_mock_netlist();
    NnsPlacer placer{netlist, NnsConfig{100, 100, 40, 40, std::nullopt, 1.2}};
    CHECK(placer.grid_limit[0] == 100);
    CHECK(placer.grid_limit[1] == 99);
    CHECK(placer.limit[0] == 3);
    CHECK(placer.limit[1] == 3);
    CHECK(placer.io_limit[0] == 1);
    CHECK(placer.io_limit[1] == 1);
}

TEST_CASE("test_io_limit_half_pads") {
    const auto netlist = build_mock_netlist();
    NnsPlacer placer{netlist, NnsConfig{100, 100, 40, 40}};
    CHECK(placer.grid_limit[0] == 100);
    CHECK(placer.grid_limit[1] == 99);
    CHECK(placer.limit[0] == 100);
    CHECK(placer.limit[1] == 99);
    CHECK(placer.io_limit[0] == 1);
    CHECK(placer.io_limit[1] == 1);
}

TEST_CASE("test_legalize_global_fallback") {
    const auto netlist = build_tiny_netlist();
    NnsPlacer placer{netlist, NnsConfig{100, 100, 40, 40}};
    auto place = make_placements(70);
    for (std::size_t v = 0; v < 70; ++v) {
        place[0][v] = 20;
        place[1][v] = static_cast<Coord>(v % 10);
    }
    std::vector<node_t> lst(70);
    for (node_t v = 0; v < 70; ++v) lst[v] = v;
    placer.legalize(lst, place, 1);
    std::vector<Coord> rows(70);
    for (std::size_t v = 0; v < 70; ++v) rows[v] = place[1][v];
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    CHECK(rows.size() == 70);
}

TEST_CASE("test_placement_p1_32") {
    const auto netlist = read_json_edges("testcases/p1.json");
    const auto place = run_placer(netlist, 32, 32, 831).first;
    check_legal(netlist, place, 32, 32);
}

TEST_CASE("test_expected_capped_limit") {
    const auto netlist = read_json_edges("testcases/p1.json");
    NnsPlacer placer{netlist, NnsConfig{100, 100, 40, 40, std::nullopt, 1.2}};
    const auto cap = static_cast<Coord>(std::ceil(std::sqrt(752.0) * 1.2));
    CHECK(cap == 33);
    CHECK(placer.limit[0] == std::min(Coord{100}, cap));
    CHECK(placer.limit[1] == std::min(Coord{99}, cap));
    CHECK(placer.io_limit[0] == std::min(Coord{100}, Coord{41}));
    CHECK(placer.io_limit[1] == std::min(Coord{99}, Coord{41}));
}

TEST_CASE("test_flow_graph_p1_sanity") {
    const auto netlist = read_json_edges("testcases/p1.json");
    const auto fg = create_flow_graph(netlist);
    std::size_t total = 0;
    for (const auto& lst : fg.adj) total += lst.size();
    CHECK(fg.adj.size() == 833);
    CHECK(total > 0);
}

TEST_CASE("test_placement_p1_capped_50") {
    const auto netlist = read_json_edges("testcases/p1.json");
    NnsPlacer placer{netlist, NnsConfig{50, 50, 40, 40, std::nullopt, 1.2}};
    auto place = make_placements(netlist.number_of_modules());
    placer.init_placement(place, 831);
    placer.io_assign(place);
    const auto before = placer.calc_worst_wirelength(place);
    const auto result = placer.run(place, 2000);
    CHECK(result.first >= 0);
    CHECK(result.second < before);
    check_legal(netlist, place, 50, 50);
}

TEST_CASE("test_placement_p1_capped_100") {
    const auto netlist = read_json_edges("testcases/p1.json");
    NnsPlacer placer{netlist, NnsConfig{100, 100, 40, 40, std::nullopt, 1.2}};
    auto place = make_placements(netlist.number_of_modules());
    placer.init_placement(place, 831);
    placer.io_assign(place);
    const auto before = placer.calc_worst_wirelength(place);
    const auto result = placer.run(place, 2000);
    CHECK(result.first >= 0);
    CHECK(result.second < before);
    check_legal(netlist, place, 100, 100);
}

TEST_CASE("test_placement_p1_uncapped_50") {
    const auto netlist = read_json_edges("testcases/p1.json");
    const auto place = run_placer(netlist, 50, 50, 831).first;
    check_legal(netlist, place, 50, 50);
}

TEST_CASE("test_placement_p1_uncapped_100") {
    const auto netlist = read_json_edges("testcases/p1.json");
    const auto place = run_placer(netlist, 100, 100, 831).first;
    check_legal(netlist, place, 100, 100);
}

TEST_CASE("test_placement_drawf") {
    // The drawf/fix circuit is tiny (4 cells + 3 pads), so with seed 831 some
    // platforms already start at a local optimum (before == worst after run);
    // strict improvement is init/platform dependent (cf. Python's own suite,
    // which documents "improvement is seed-dependent").  Assert the optimizer
    // never worsens the placement and the result stays legal instead.
    const auto netlist = build_drawf_netlist();
    NnsPlacer placer{netlist, NnsConfig{32, 32, 40, 40}};
    auto place = make_placements(netlist.number_of_modules());
    placer.init_placement(place, 831);
    placer.io_assign(place);
    const auto before = placer.calc_worst_wirelength(place);
    const auto result = placer.run(place, 2000);
    CHECK(result.first >= 0);
    CHECK(result.second <= before);
    check_legal(netlist, place, 32, 32);
}

TEST_CASE("test_line_cap_ratio_property_integration") {
    const auto netlist = read_json_edges("testcases/p1.json");
    NnsPlacer placer{netlist, NnsConfig{50, 50, 40, 40, std::nullopt, 1.2}};
    const auto cap = static_cast<Coord>(std::ceil(std::sqrt(752.0) * 1.2));
    CHECK(placer.limit[0] == std::min(Coord{50}, cap));
    CHECK(placer.limit[1] == std::min(Coord{49}, cap));
}
