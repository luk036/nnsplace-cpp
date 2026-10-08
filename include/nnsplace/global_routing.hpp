/*! \file global_routing.hpp
 *  \brief Global routing and congestion analysis of a placed netlist.
 *
 *  Routes every net of a placed netlist with the existing physdes global
 *  router (``recti::GlobalRouter`` / ``recti::GlobalRoutingTree``), reports
 *  the total routed wire length, counts the wire crossings on every grid cut
 *  segment, derives the per-cell x / y / combined congestion maps, and
 *  renders the results as SVG figures:
 *
 *  - a routed placement figure where the straight pad-to-module lines are
 *    replaced by the orthogonal branches of the routing trees, and
 *  - green-yellow-red congestion heat maps (x direction, y direction, and
 *    their element-wise maximum).
 *
 *  This is the C++ port of the Python experiment scripts
 *  ``nnsplace/experiments/gen_svg_routing.py`` and
 *  ``gen_congestion_map.py``.
 */

#pragma once

#include <cstdint>
#include <netlistx/netlist.hpp>
#include <recti/global_router.hpp>
#include <recti/point.hpp>
#include <string>
#include <vector>

#include "placement.hpp"

/// \brief 2-D lattice point type used by the physdes global router.
using RoutingPoint = recti::Point<int, int>;

/** \brief One axis-aligned wire run between two lattice points. */
struct RouteRun {
    int x1;  ///< first endpoint column
    int y1;  ///< first endpoint row
    int x2;  ///< second endpoint column
    int y2;  ///< second endpoint row
};

/** \brief Routing and cut-usage analysis of every net over a placement.
 *
 *  ``h[y][c]`` is the number of routed horizontal unit steps crossing the
 *  vertical cut between column ``c`` and ``c + 1`` while travelling on row
 *  ``y``; ``v[x][r]`` counts vertical unit steps crossing the horizontal cut
 *  between row ``r`` and ``r + 1`` while travelling on column ``x``.  Row
 *  ``y`` runs over ``0 .. gy + 1`` (ring rows included), cut ``c`` over
 *  ``0 .. gx``, column ``x`` over ``0 .. gx + 1`` and cut ``r`` over
 *  ``0 .. gy``.
 */
struct RoutingAnalysis {
    std::int64_t total_wirelength = 0;  ///< sum over all nets, in grid units
    std::vector<RouteRun> hsegments;    ///< horizontal runs of pad nets
    std::vector<RouteRun> vsegments;    ///< vertical runs of pad nets
    /// horizontal cut crossings, indexed [y][c]
    std::vector<std::vector<std::int64_t>> h;
    /// vertical cut crossings, indexed [x][r]
    std::vector<std::vector<std::int64_t>> v;
};

/** \brief Route every net of a placed netlist and analyse the cut usage.
 *
 *  The source of a net is the **driver** module given in ``net_driver`` when a
 *  direction-aware vector is supplied (one optional driver per net, in net
 *  order); otherwise it falls back to the I/O pad of the net, else its first
 *  module (mirroring the Python pipeline).  Direction-aware routing uses
 *  ``route_with_constraints()`` and only records the branches of pad-driven
 *  nets in the figure segments, while the cut usage always covers every net.
 *  Undirected netlists use ``route_with_steiners()`` and draw every net that
 *  touches an I/O pad.
 *
 *  \param[in] netlist    the hypergraph netlist (cells then I/O pads)
 *  \param[in] place      the placement solution to route
 *  \param[in] gx         core grid width  (number of columns)
 *  \param[in] gy         core grid height (number of rows)
 *  \param[in] net_driver optional per-net driver module ids (net order)
 *  \return the routing analysis of the netlist
 */
auto route_all_nets(const SimpleNetlist& netlist, const Placement& place, int gx, int gy,
                    const std::vector<std::optional<node_t>>& net_driver = {}) -> RoutingAnalysis;

/** \brief Raw per-cell congestion counts derived from a routing analysis.
 *
 *  Cell ``(x, y)`` (core cell column ``x`` in ``1..gx``, row ``y`` in
 *  ``1..gy``) holds the number of wires on the most congested adjacent cut
 *  segment: for the x map the horizontal cuts to the left and right of the
 *  cell, for the y map the vertical cuts below and above it.  The combined
 *  map is the element-wise maximum of the two direction maps.
 */
struct CongestionMaps {
    std::vector<std::vector<std::int64_t>> x;         ///< rows y = 1..gy, cols x = 1..gx
    std::vector<std::vector<std::int64_t>> y;         ///< rows y = 1..gy, cols x = 1..gx
    std::vector<std::vector<std::int64_t>> combined;  ///< element-wise max of x and y
    std::int64_t peak_x = 0;                          ///< busiest horizontal cut of the x map
    std::int64_t peak_y = 0;                          ///< busiest vertical cut of the y map
    std::int64_t peak_combined = 0;                   ///< busiest cut of the combined map
};

/** \brief Derive the x / y / combined congestion maps from a routing analysis.
 *
 *  \param[in] routing the cut-usage analysis of every net
 *  \param[in] gx      core grid width
 *  \param[in] gy      core grid height
 *  \return the three raw per-cell congestion grids and their peaks
 */
auto build_congestion_maps(const RoutingAnalysis& routing, int gx, int gy) -> CongestionMaps;

/** \brief Scale a raw congestion grid so its busiest cut becomes 100.
 *
 *  \param[in] raw per-cell congestion counts
 *  \return integer percentages in ``0..100`` (0 when the grid is empty)
 */
auto congestion_percent(const std::vector<std::vector<std::int64_t>>& raw)
    -> std::vector<std::vector<int>>;

/** \brief Build the routed placement figure for a placed netlist.
 *
 *  Mirrors the Python ``gen_svg_routing.py`` output: the core cells and I/O
 *  pads are drawn at their placed coordinates and the straight pad-to-module
 *  connection lines are replaced by the orthogonal routing-tree branches
 *  recorded in ``routing``.
 *
 *  \param[in] netlist the hypergraph netlist
 *  \param[in] place   the placement solution
 *  \param[in] gx      core grid width
 *  \param[in] gy      core grid height
 *  \param[in] routing the routing analysis holding the pad-net branches
 *  \param[in] pixel   pixel pitch of one grid cell
 *  \return the SVG document as a string
 */
auto make_routed_placement_svg(const SimpleNetlist& netlist, const Placement& place, int gx, int gy,
                               const RoutingAnalysis& routing, int pixel = 40) -> std::string;

/** \brief Build one congestion heat-map SVG figure.
 *
 *  \param[in] title   figure title (e.g. "Congestion x-direction (30x30)")
 *  \param[in] percent per-cell congestion percentages in ``0..100``
 *  \param[in] peak    busiest raw cut count, shown under the title
 *  \return the SVG document as a string
 */
auto make_congestion_map_svg(const std::string& title, const std::vector<std::vector<int>>& percent,
                             std::int64_t peak) -> std::string;
