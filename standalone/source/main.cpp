// -*- coding: utf-8 -*-
/**
 * @file main.cpp
 * @brief Run the NNS placer on a node-link JSON netlist and report metrics.
 *
 * Usage: nnsplace [testcase.json] [grid_x] [grid_y] [delta] [max_iters] [ratio]
 *
 * Prints the initial/optimized HPWL and worst wire length so the C++ port can
 * be compared side by side against the Python reference (same algorithm).
 * A positive ``ratio`` enables the per-line capacity cap (line_cap_ratio).
 */

#include <cstdint>
#include <iostream>
#include <nnsplace/placement.hpp>
#include <nnsplace/placement_cfg.hpp>
#include <nnsplace/readwrite.hpp>
#include <optional>
#include <string>
#include <vector>

namespace {

    /// Verify cells sit in the core (off the reserved column) and pads on the ring.
    auto is_legal(const SimpleNetlist& netlist, const Placement& place, const Coord gx,
                  const Coord gy, const Coord reserved_col) -> bool {
        const auto n = netlist.number_of_modules();
        const auto num_cells = n - netlist.num_pads;
        for (std::size_t i = 0; i < n; ++i) {
            const auto x = place[0][i];
            const auto y = place[1][i];
            for (std::size_t j = 0; j < i; ++j) {
                if (x == place[0][j] && y == place[1][j]) return false;
            }
            if (i < num_cells) {
                if (x < 1 || x > gx || y < 1 || y > gy) return false;
                if (x == reserved_col) return false;
            } else if (x != 0 && x != gx + 1 && y != 0 && y != gy + 1) {
                return false;
            }
        }
        return true;
    }

}  // namespace

auto main(int argc, char* argv[]) -> int {
    const std::string file = argc > 1 ? argv[1] : "testcases/p1.json";
    const auto gx = argc > 2 ? std::stoll(argv[2]) : 32;
    const auto gy = argc > 3 ? std::stoll(argv[3]) : 32;
    const auto delta = argc > 4 ? std::stoll(argv[4]) : 40;
    const auto max_iters = argc > 5 ? std::stoi(argv[5]) : 2000;
    const auto ratio = argc > 6 ? std::stod(argv[6]) : 0.0;

    const auto netlist = read_json_edges(file);
    const auto n = netlist.number_of_modules();
    const std::optional<double> cap_ratio
        = ratio > 0.0 ? std::optional<double>{ratio} : std::nullopt;
    NnsPlacer placer{netlist, NnsConfig{gx, gy, delta, delta, std::nullopt, cap_ratio}};

    Placement place;
    place[0].assign(n, 0);
    place[1].assign(n, 0);

    placer.init_placement(place, 831);
    placer.io_assign(place);

    const auto hpwl_x0 = placer.calc_total_hull_length(place[0], 0);
    const auto hpwl_y0 = placer.calc_total_hull_length(place[1], 1);
    const auto worst0 = placer.calc_worst_wirelength(place);
    std::cout << "modules=" << n << " pads=" << netlist.num_pads << " grid=" << gx << "x" << gy
              << " delta=" << delta << '\n';
    std::cout << "Total HPWL before = " << hpwl_x0 << " + " << hpwl_y0 << " = "
              << (hpwl_x0 + hpwl_y0) << '\n';
    std::cout << "Worst wirelength before = " << worst0 << '\n';

    const auto result = placer.run(place, max_iters);

    const auto hpwl_x1 = placer.calc_total_hull_length(place[0], 0);
    const auto hpwl_y1 = placer.calc_total_hull_length(place[1], 1);
    std::cout << "iterations = " << result.first << '\n';
    std::cout << "Worst wirelength after = " << result.second << '\n';
    std::cout << "Total HPWL after = " << hpwl_x1 << " + " << hpwl_y1 << " = "
              << (hpwl_x1 + hpwl_y1) << '\n';
    std::cout << "legal = "
              << (is_legal(netlist, place, gx, gy, placer.cfg.reserved_col) ? "yes" : "no") << '\n';
    return 0;
}
