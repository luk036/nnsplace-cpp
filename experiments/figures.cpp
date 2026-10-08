// -*- coding: utf-8 -*-
/**
 * @file figures.cpp
 * @brief Run the placer, route every net and emit the routed placement figure
 *        plus the x / y / combined congestion maps as SVG files.
 *
 * Usage:
 *   nnsplace_figures [testcase.json] [gx] [gy] [delta] [max_iters] [ratio] [seed] [outdir]
 *
 * The routed figure mirrors the Python ``gen_svg_routing.py`` output: the
 * straight pad-to-module lines are replaced by the orthogonal branches of the
 * routing trees.  The congestion maps mirror ``gen_congestion_map.py``.  All
 * netlist nets are routed, so the reported total wire length covers the whole
 * solution.
 */

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nnsplace/global_routing.hpp>
#include <nnsplace/placement.hpp>
#include <nnsplace/placement_cfg.hpp>
#include <nnsplace/readwrite.hpp>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

    auto write_file(const std::string& path, const std::string& content) -> void {
        std::ofstream out(path);
        out << content;
    }

    auto write_figures(const SimpleNetlist& netlist, const Placement& place, int gx, int gy,
                       const RoutingAnalysis& routing, const std::string& outdir) -> void {
        const auto name = std::to_string(gx) + "x" + std::to_string(gy);
        write_file(outdir + "/ioloop" + name + "-routed.svg",
                   make_routed_placement_svg(netlist, place, gx, gy, routing));

        const auto maps = build_congestion_maps(routing, gx, gy);
        write_file(outdir + "/congestion" + name + "-x.svg",
                   make_congestion_map_svg("Congestion x-direction (" + name + ")",
                                           congestion_percent(maps.x), maps.peak_x));
        write_file(outdir + "/congestion" + name + "-y.svg",
                   make_congestion_map_svg("Congestion y-direction (" + name + ")",
                                           congestion_percent(maps.y), maps.peak_y));
        write_file(outdir + "/congestion" + name + "-combined.svg",
                   make_congestion_map_svg("Congestion combined (" + name + ")",
                                           congestion_percent(maps.combined), maps.peak_combined));
    }

}  // namespace

auto main(int argc, char* argv[]) -> int {
    const std::string file = argc > 1 ? argv[1] : "testcases/p1.json";
    const auto gx = argc > 2 ? std::stoi(argv[2]) : 32;
    const auto gy = argc > 3 ? std::stoi(argv[3]) : 32;
    const auto delta = argc > 4 ? std::stoll(argv[4]) : 40;
    const auto max_iters = argc > 5 ? std::stoi(argv[5]) : 2000;
    const auto ratio = argc > 6 ? std::stod(argv[6]) : 0.0;
    const auto seed = argc > 7 ? std::stoul(argv[7]) : 831;
    const auto outdir = argc > 8 ? argv[8] : ".";

    const auto directed = read_directed_json_edges(file);
    const auto& netlist = directed.netlist;
    const auto n = netlist.number_of_modules();
    const std::optional<double> cap_ratio
        = ratio > 0.0 ? std::optional<double>{ratio} : std::nullopt;
    NnsPlacer placer{netlist, NnsConfig{gx, gy, delta, delta, std::nullopt, cap_ratio},
                     directed.net_driver};

    Placement place;
    place[0].assign(n, 0);
    place[1].assign(n, 0);

    placer.init_placement(place, seed);
    placer.io_assign(place);
    const auto worst0 = placer.calc_worst_wirelength(place);
    const auto result = placer.run(place, max_iters);
    const auto hpwl = placer.calc_total_HPWL(place);

    const auto routing = route_all_nets(netlist, place, gx, gy, directed.net_driver);
    const auto maps = build_congestion_maps(routing, gx, gy);
    const auto segments = routing.hsegments.size() + routing.vsegments.size();

    fs::create_directories(outdir);
    write_figures(netlist, place, gx, gy, routing, outdir);

    std::cout << "grid=" << gx << "x" << gy << " seed=" << seed << '\n';
    std::cout << "iterations=" << result.first << " worst_before=" << worst0
              << " worst_after=" << result.second << " hpwl_total=" << hpwl << '\n';
    std::cout << "routed_wirelength(cost)=" << routing.total_wirelength * delta
              << " routed_wirelength(grid)=" << routing.total_wirelength
              << " tree_branches=" << segments << '\n';
    std::cout << "peak_x=" << maps.peak_x << " peak_y=" << maps.peak_y
              << " peak_combined=" << maps.peak_combined << '\n';
    std::cout << "wrote figures to " << outdir << '\n';
    return 0;
}
