/*! \file global_routing.cpp
 *  \brief Out-of-line definitions of the global routing + congestion analysis.
 *
 *  Routes every net of a placed netlist with the physdes global router
 *  (``recti::GlobalRouter`` / ``recti::GlobalRoutingTree``), counts the wire
 *  crossings on every grid cut segment, derives the per-cell x / y / combined
 *  congestion maps and renders the routed placement figure and the congestion
 *  heat maps as SVG documents.
 */

#include <nnsplace/global_routing.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <recti/global_router.hpp>
#include <recti/logger.hpp>
#include <recti/point.hpp>

#ifndef NNSPLACE_HAVE_RECTI_LOGGER
// The physdes global router logs through this hook; nnsplace intentionally
// keeps the routing silent, so a local no-op definition replaces the spdlog
// implementation of the sibling library when it is not linked (the xmake
// build compiles only global_router.cpp).
namespace recti {
void log_with_spdlog(const std::string& message) {
    (void)message;
}
}  // namespace recti
#endif

namespace {
using IntPoint = recti::Point<int, int>;
using Tree = recti::GlobalRoutingTree<IntPoint>;

auto is_pad(const SimpleNetlist& netlist, const std::size_t m) -> bool {
    return m >= netlist.number_of_modules() - netlist.num_pads;
}

auto vertical_first(const Placement& place, const std::size_t src, int gx, int gy) -> bool {
    const auto px = static_cast<int>(place[0][src]);
    const auto py = static_cast<int>(place[1][src]);
    return (py == 0 || py == gy + 1) && !(px == 0 || px == gx + 1);
}

void count_horizontal_run(std::vector<std::vector<std::int64_t>>& h, int row, int x1, int x2) {
    const auto c1 = static_cast<std::size_t>(std::min(x1, x2));
    const auto c2 = static_cast<std::size_t>(std::max(x1, x2));
    auto& line = h[static_cast<std::size_t>(row)];
    for (auto c = c1; c < c2; ++c) {
        ++line[c];
    }
}

void count_vertical_run(std::vector<std::vector<std::int64_t>>& v, int col, int y1, int y2) {
    const auto r1 = static_cast<std::size_t>(std::min(y1, y2));
    const auto r2 = static_cast<std::size_t>(std::max(y1, y2));
    auto& line = v[static_cast<std::size_t>(col)];
    for (auto r = r1; r < r2; ++r) {
        ++line[r];
    }
}

void add_tree_runs(const Tree& tree, bool vertical_first, bool keep_runs,
                   std::vector<RouteRun>& hsegs, std::vector<RouteRun>& vsegs,
                   std::vector<std::vector<std::int64_t>>& h,
                   std::vector<std::vector<std::int64_t>>& v) {
    const auto* source = tree.get_source();
    std::vector<const recti::RoutingNode<IntPoint>*> stack{source};
    while (!stack.empty()) {
        const auto* node = stack.back();
        stack.pop_back();
        for (const auto* child : node->children) {
            const auto x1 = node->pt.xcoord();
            const auto y1 = node->pt.ycoord();
            const auto x2 = child->pt.xcoord();
            const auto y2 = child->pt.ycoord();
            if (x1 == x2) {
                count_vertical_run(v, x1, y1, y2);
                if (keep_runs) vsegs.push_back(RouteRun{x1, y1, x2, y2});
            } else if (y1 == y2) {
                count_horizontal_run(h, y1, x1, x2);
                if (keep_runs) hsegs.push_back(RouteRun{x1, y1, x2, y2});
            } else if (vertical_first) {
                count_vertical_run(v, x1, y1, y2);
                count_horizontal_run(h, y2, x1, x2);
                if (keep_runs) {
                    vsegs.push_back(RouteRun{x1, y1, x1, y2});
                    hsegs.push_back(RouteRun{x1, y2, x2, y2});
                }
            } else {
                count_horizontal_run(h, y1, x1, x2);
                count_vertical_run(v, x2, y1, y2);
                if (keep_runs) {
                    hsegs.push_back(RouteRun{x1, y1, x2, y1});
                    vsegs.push_back(RouteRun{x2, y1, x2, y2});
                }
            }
            stack.push_back(child);
        }
    }
}

}  // namespace

auto route_all_nets(const SimpleNetlist& netlist, const Placement& place, int gx, int gy)
    -> RoutingAnalysis {
    RoutingAnalysis analysis;
    analysis.h.assign(static_cast<std::size_t>(gy + 2),
                      std::vector<std::int64_t>(static_cast<std::size_t>(gx + 1), 0));
    analysis.v.assign(static_cast<std::size_t>(gx + 2),
                      std::vector<std::int64_t>(static_cast<std::size_t>(gy + 1), 0));

    for (const auto net : netlist.nets) {
        const auto& members = netlist.gr[net];
        std::vector<std::size_t> verts(members.begin(), members.end());
        if (verts.size() < 2) continue;

        std::size_t source = verts.front();
        bool has_pad = false;
        for (const auto m : verts) {
            if (is_pad(netlist, m)) {
                source = m;
                has_pad = true;
                break;
            }
        }

        const auto src_x = static_cast<int>(place[0][source]);
        const auto src_y = static_cast<int>(place[1][source]);
        std::vector<IntPoint> terminals;
        terminals.reserve(verts.size() - 1);
        for (const auto m : verts) {
            if (m == source) continue;
            terminals.emplace_back(static_cast<int>(place[0][m]),
                                   static_cast<int>(place[1][m]));
        }

        recti::GlobalRouter<IntPoint> router{IntPoint{src_x, src_y}, std::move(terminals)};
        router.route_with_steiners();
        const auto& tree = router.get_tree();
        analysis.total_wirelength += tree.calculate_total_wirelength();

        add_tree_runs(tree, vertical_first(place, source, gx, gy), has_pad,
                      analysis.hsegments, analysis.vsegments, analysis.h, analysis.v);
    }
    return analysis;
}

auto build_congestion_maps(const RoutingAnalysis& routing, int gx, int gy) -> CongestionMaps {
    CongestionMaps maps;
    maps.x.assign(static_cast<std::size_t>(gy), std::vector<std::int64_t>(0));
    maps.y.assign(static_cast<std::size_t>(gy), std::vector<std::int64_t>(0));
    maps.combined.assign(static_cast<std::size_t>(gy), std::vector<std::int64_t>(0));

    for (int y = 1; y <= gy; ++y) {
        const auto& hrow = routing.h[static_cast<std::size_t>(y)];
        for (int x = 1; x <= gx; ++x) {
            const auto xval = std::max(hrow[static_cast<std::size_t>(x - 1)],
                                       hrow[static_cast<std::size_t>(x)]);
            const auto& vcol = routing.v[static_cast<std::size_t>(x)];
            const auto yval = std::max(vcol[static_cast<std::size_t>(y - 1)],
                                       vcol[static_cast<std::size_t>(y)]);
            maps.x[static_cast<std::size_t>(y - 1)].push_back(xval);
            maps.y[static_cast<std::size_t>(y - 1)].push_back(yval);
            maps.combined[static_cast<std::size_t>(y - 1)].push_back(std::max(xval, yval));
            maps.peak_x = std::max(maps.peak_x, xval);
            maps.peak_y = std::max(maps.peak_y, yval);
            maps.peak_combined = std::max(maps.peak_combined, std::max(xval, yval));
        }
    }
    return maps;
}

auto congestion_percent(const std::vector<std::vector<std::int64_t>>& raw)
    -> std::vector<std::vector<int>> {
    std::int64_t peak = 0;
    for (const auto& row : raw) {
        for (const auto value : row) {
            peak = std::max(peak, value);
        }
    }
    if (peak == 0) {
        return std::vector<std::vector<int>>(
            raw.size(), std::vector<int>(raw.empty() ? 0 : raw.front().size(), 0));
    }
    std::vector<std::vector<int>> percent;
    percent.reserve(raw.size());
    for (const auto& row : raw) {
        std::vector<int> pct;
        pct.reserve(row.size());
        for (const auto value : row) {
            pct.push_back(static_cast<int>(std::llround(100.0 * value / peak)));
        }
        percent.push_back(std::move(pct));
    }
    return percent;
}

auto make_routed_placement_svg(const SimpleNetlist& netlist, const Placement& place, int gx,
                               int gy, const RoutingAnalysis& routing, int pixel) -> std::string {
    const auto n = netlist.number_of_modules();
    const auto num_cells = n - netlist.num_pads;
    const auto w = (gx + 2) * pixel;
    const auto h = (gy + 2) * pixel;
    const auto iw = gx * pixel;
    const auto ih = gy * pixel;
    const auto ox = (gx + 1) * pixel;
    const auto oy = (gy + 1) * pixel;

    std::ostringstream svg;
    svg << "<svg viewBox=\"0 0 " << w << " " << h << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";
    svg << "  <style type=\"text/css\">\n";
    svg << "    circle.cell { fill: #0062ff; }\n";
    svg << "    circle.iopad { fill: #ec0000; }\n";
    svg << "    line { stroke: #00a200; stroke-width: 4; stroke-opacity: 0.4; }\n";
    svg << "  </style>\n";
    svg << "  <pattern id=\"pattern-circles\" x=\"0\" y=\"0\" width=\"" << pixel
        << "\" height=\"" << pixel << "\" patternUnits=\"userSpaceOnUse\">\n";
    svg << "    <circle class=\"cell\" opacity=\"0.2\" cx=\"20\" cy=\"20\" r=\"15\"/>\n";
    svg << "  </pattern>\n";
    svg << "  <pattern id=\"pattern-io\" x=\"0\" y=\"0\" width=\"" << pixel << "\" height=\""
        << pixel << "\" patternUnits=\"userSpaceOnUse\">\n";
    svg << "    <circle class=\"iopad\" opacity=\"0.2\" cx=\"20\" cy=\"20\" r=\"15\"/>\n";
    svg << "  </pattern>\n";
    svg << "  <rect x=\"" << pixel << "\" y=\"" << pixel << "\" width=\"" << iw << "\" height=\""
        << ih << "\" fill=\"url(#pattern-circles)\"/>\n";
    svg << "  <rect x=\"" << pixel << "\" y=\"0\" width=\"" << iw << "\" height=\"" << pixel
        << "\" fill=\"url(#pattern-io)\"/>\n";
    svg << "  <rect x=\"" << pixel << "\" y=\"" << oy << "\" width=\"" << iw << "\" height=\""
        << pixel << "\" fill=\"url(#pattern-io)\"/>\n";
    svg << "  <rect x=\"0\" y=\"" << pixel << "\" width=\"" << pixel << "\" height=\"" << ih
        << "\" fill=\"url(#pattern-io)\"/>\n";
    svg << "  <rect x=\"" << ox << "\" y=\"" << pixel << "\" width=\"" << pixel << "\" height=\""
        << ih << "\" fill=\"url(#pattern-io)\"/>\n";
    svg << "  <defs>\n";
    svg << "    <rect id=\"r1\" width=\"35\" height=\"35\" fill=\"#FF00A7\" opacity=\"0.2\" "
           "stroke=\"black\" stroke-width=\"3\"/>\n";
    svg << "    <rect id=\"io\" width=\"35\" height=\"35\" fill=\"#00E7FF\" opacity=\"0.2\" "
           "stroke=\"black\" stroke-width=\"3\"/>\n";
    svg << "  </defs>\n";

    for (std::size_t i = 0; i < n; ++i) {
        const auto px = static_cast<int>(place[0][i]) * pixel;
        const auto py = static_cast<int>(place[1][i]) * pixel;
        svg << "  <use x=\"" << px << "\" y=\"" << py << "\" href=\""
            << (i < num_cells ? "#r1" : "#io") << "\"/>\n";
    }

    for (const auto& run : routing.hsegments) {
        svg << "  <line x1=\"" << run.x1 * pixel + pixel / 2 << "\" y1=\""
            << run.y1 * pixel + pixel / 2 << "\" x2=\"" << run.x2 * pixel + pixel / 2
            << "\" y2=\"" << run.y2 * pixel + pixel / 2 << "\"/>\n";
    }
    for (const auto& run : routing.vsegments) {
        svg << "  <line x1=\"" << run.x1 * pixel + pixel / 2 << "\" y1=\""
            << run.y1 * pixel + pixel / 2 << "\" x2=\"" << run.x2 * pixel + pixel / 2
            << "\" y2=\"" << run.y2 * pixel + pixel / 2 << "\"/>\n";
    }
    svg << "</svg>\n";
    return svg.str();
}

namespace {

auto congestion_hex(const int value) -> std::string {
    unsigned rgb = 0;
    if (value <= 50) {
        const auto red = static_cast<unsigned>(255.0 * value / 50);
        rgb = (red << 16) | 0xFF00;
    } else {
        const auto green = static_cast<unsigned>(255.0 * (100 - value) / 50);
        rgb = 0xFF0000 | (green << 8);
    }
    std::ostringstream os;
    os << std::hex << std::nouppercase << std::setw(6) << std::setfill('0') << rgb;
    return os.str();
}

}  // namespace

auto make_congestion_map_svg(const std::string& title,
                             const std::vector<std::vector<int>>& percent, std::int64_t peak)
    -> std::string {
    const auto rows = percent.size();
    const auto cols = rows == 0 ? 0 : percent.front().size();
    const auto gy = static_cast<int>(rows);
    const auto gx = static_cast<int>(cols);
    const auto cell = std::max(5, std::min(40, 1000 / std::max(gx, gy)));
    constexpr int padding = 24;
    constexpr int title_height = 70;
    constexpr int legend_width = 110;
    const auto width = cols * static_cast<std::size_t>(cell) + 2 * padding + legend_width;
    const auto height = rows * static_cast<std::size_t>(cell) + 2 * padding + title_height;
    const auto show_value = cell >= 26;

    std::ostringstream svg;
    svg << "<svg width=\"" << width << "\" height=\"" << height
        << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";
    svg << "<rect width=\"" << width << "\" height=\"" << height << "\" fill=\"#ffffff\"/>\n";
    svg << "<text x=\"" << padding << "\" y=\"34\" font-size=\"22\" "
           "font-family=\"Arial\">"
        << title << "</text>\n";
    svg << "<text x=\"" << padding << "\" y=\"56\" font-size=\"14\" fill=\"#555555\" "
           "font-family=\"Arial\">busiest cut: "
        << peak << " wires</text>\n";
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            const auto pct = percent[r][c];
            const auto x = padding + static_cast<int>(c) * cell;
            const auto y = title_height + padding + static_cast<int>(r) * cell;
            svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << cell << "\" height=\""
                << cell << "\" fill=\"#" << congestion_hex(pct)
                << "\" stroke=\"#cccccc\" stroke-width=\"1\"/>\n";
            if (show_value) {
                svg << "<text x=\"" << x + cell / 2 << "\" y=\"" << y + cell / 2 + 5
                    << "\" font-size=\"14\" text-anchor=\"middle\" fill=\"black\" "
                       "font-family=\"Arial\">"
                    << pct << "</text>\n";
            }
        }
    }

    const auto legend_x = padding + static_cast<int>(cols) * cell + 30;
    const auto legend_y = title_height + padding;
    const auto bar_height = static_cast<int>(rows) * cell;
    svg << "<text x=\"" << legend_x << "\" y=\"" << legend_y - 10
        << "\" font-size=\"16\" font-family=\"Arial\">Congestion %</text>\n";
    svg << "<defs>\n";
    svg << "<linearGradient id=\"grad\" x1=\"0%\" y1=\"100%\" x2=\"0%\" y2=\"0%\">\n";
    svg << "<stop offset=\"0%\" style=\"stop-color:#00ff00\"/>\n";
    svg << "<stop offset=\"50%\" style=\"stop-color:#ffff00\"/>\n";
    svg << "<stop offset=\"100%\" style=\"stop-color:#ff0000\"/>\n";
    svg << "</linearGradient>\n";
    svg << "</defs>\n";
    svg << "<rect x=\"" << legend_x << "\" y=\"" << legend_y << "\" width=\"30\" height=\""
        << bar_height << "\" fill=\"url(#grad)\"/>\n";
    for (int label = 0; label <= 100; label += 25) {
        const auto ly = legend_y + bar_height - static_cast<int>(label / 100.0 * bar_height);
        svg << "<text x=\"" << legend_x + 38 << "\" y=\"" << ly + 5
            << "\" font-size=\"12\" font-family=\"Arial\">" << label << "</text>\n";
        svg << "<line x1=\"" << legend_x - 5 << "\" y1=\"" << ly << "\" x2=\"" << legend_x
            << "\" y2=\"" << ly << "\" stroke=\"black\" stroke-width=\"1\"/>\n";
    }
    svg << "</svg>\n";
    return svg.str();
}
