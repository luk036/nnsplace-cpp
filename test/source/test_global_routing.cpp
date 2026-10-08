// -*- coding: utf-8 -*-
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <nnsplace/global_routing.hpp>
#include <nnsplace/placement.hpp>
#include <nnsplace/placement_cfg.hpp>
#include <nnsplace/readwrite.hpp>
#include <string>

namespace {

    struct P1_32 {
        SimpleNetlist netlist;
        Placement place;
    };

    auto run_p1_32() -> P1_32 {
        const auto netlist = read_json_edges("testcases/p1.json");
        const auto n = netlist.number_of_modules();
        NnsPlacer placer{netlist, NnsConfig{32, 32, 40, 40}};
        Placement place;
        place[0].assign(n, 0);
        place[1].assign(n, 0);
        placer.init_placement(place, 831);
        placer.io_assign(place);
        (void)placer.run(place, 2000);
        return P1_32{netlist, std::move(place)};
    }

    auto cut_crossing_total(const RoutingAnalysis& routing) -> std::int64_t {
        std::int64_t total = 0;
        for (const auto& row : routing.h) {
            for (const auto value : row) total += value;
        }
        for (const auto& row : routing.v) {
            for (const auto value : row) total += value;
        }
        return total;
    }

}  // namespace

TEST_SUITE("GlobalRouting") {
    TEST_CASE("every routed unit step is counted exactly once") {
        const auto fixture = run_p1_32();
        const auto routing = route_all_nets(fixture.netlist, fixture.place, 32, 32);
        CHECK(routing.total_wirelength > 0);
        CHECK(routing.total_wirelength == cut_crossing_total(routing));
    }

    TEST_CASE("x and y runs decompose the pad-net branches axis aligned") {
        const auto fixture = run_p1_32();
        const auto routing = route_all_nets(fixture.netlist, fixture.place, 32, 32);
        CHECK(!routing.hsegments.empty());
        CHECK(!routing.vsegments.empty());
        for (const auto& run : routing.hsegments) {
            CHECK(run.y1 == run.y2);
            CHECK(run.x1 != run.x2);
        }
        for (const auto& run : routing.vsegments) {
            CHECK(run.x1 == run.x2);
        }
    }

    TEST_CASE("congestion maps are consistent with the cut counts") {
        const auto fixture = run_p1_32();
        const auto routing = route_all_nets(fixture.netlist, fixture.place, 32, 32);
        const auto maps = build_congestion_maps(routing, 32, 32);
        CHECK(maps.x.size() == 32);
        CHECK(maps.x.front().size() == 32);
        CHECK(maps.y.size() == 32);
        CHECK(maps.combined.size() == 32);
        CHECK(maps.peak_combined == std::max(maps.peak_x, maps.peak_y));
        CHECK(maps.peak_x > 0);
        CHECK(maps.peak_y > 0);
        for (std::size_t r = 0; r < maps.x.size(); ++r) {
            for (std::size_t c = 0; c < maps.x[r].size(); ++c) {
                CHECK(maps.x[r][c] >= 0);
                CHECK(maps.y[r][c] >= 0);
                CHECK(maps.combined[r][c] == std::max(maps.x[r][c], maps.y[r][c]));
            }
        }
        const auto px = congestion_percent(maps.combined);
        CHECK(px.size() == 32);
        for (const auto& row : px) {
            for (const auto value : row) {
                CHECK(value >= 0);
                CHECK(value <= 100);
            }
        }
    }

    TEST_CASE("routed placement svg has one use per module and one line per branch") {
        const auto fixture = run_p1_32();
        const auto routing = route_all_nets(fixture.netlist, fixture.place, 32, 32);
        const auto svg = make_routed_placement_svg(fixture.netlist, fixture.place, 32, 32, routing);
        const auto nmodules = fixture.netlist.number_of_modules();
        auto uses = 0;
        auto lines = 0;
        auto pos = std::string::size_type{0};
        while ((pos = svg.find("<use ", pos)) != std::string::npos) {
            ++uses;
            pos += 5;
        }
        pos = 0;
        while ((pos = svg.find("<line ", pos)) != std::string::npos) {
            ++lines;
            pos += 6;
        }
        CHECK(svg.rfind("<svg", 0) == 0);
        CHECK(svg.find("</svg>") != std::string::npos);
        CHECK(uses == nmodules);
        CHECK(lines == routing.hsegments.size() + routing.vsegments.size());
        CHECK(lines > 0);
    }
}
