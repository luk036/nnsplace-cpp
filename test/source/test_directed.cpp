// -*- coding: utf-8 -*-
#include <doctest/doctest.h>

#include <cstddef>
#include <nnsplace/placement.hpp>
#include <nnsplace/readwrite.hpp>
#include <optional>
#include <vector>

TEST_SUITE("DirectedNetlist") {
    TEST_CASE("read_directed_json_edges exposes the net drivers") {
        const auto directed = read_directed_json_edges("testcases/directed_small.json");
        CHECK(directed.netlist.number_of_modules() == 5);
        CHECK(directed.netlist.number_of_nets() == 3);
        CHECK(directed.netlist.num_pads == 2);
        REQUIRE(directed.net_driver.size() == 3);
        CHECK(directed.net_driver[0] == std::optional<std::size_t>{0});
        CHECK(directed.net_driver[1] == std::optional<std::size_t>{3});
        CHECK(directed.net_driver[2] == std::optional<std::size_t>{4});
    }

    TEST_CASE("directed flow graph omits sink-to-sink arcs") {
        const auto directed = read_directed_json_edges("testcases/directed_small.json");
        const auto flow = create_flow_graph(directed.netlist, directed.net_driver);
        CHECK(has_directed_edge(flow, 0, 1));
        CHECK(has_directed_edge(flow, 1, 0));
        CHECK(has_directed_edge(flow, 0, 2));
        CHECK(!has_directed_edge(flow, 1, 2));
        CHECK(!has_directed_edge(flow, 2, 1));
        CHECK(has_directed_edge(flow, 3, 0));
    }

    TEST_CASE("undirected fallback keeps the all-pairs clique") {
        const auto directed = read_directed_json_edges("testcases/directed_small.json");
        const auto flow = create_flow_graph(directed.netlist);
        CHECK(has_directed_edge(flow, 1, 2));
        CHECK(has_directed_edge(flow, 2, 1));
    }
}
