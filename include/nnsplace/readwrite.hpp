#pragma once

/**
 * @file readwrite.hpp
 * @brief Node-link JSON netlist reader for the Python "edges" container.
 *
 * The Python ``netlistx.read_json`` (and therefore the nnsplace Python
 * testcases, e.g. ``testcases/p1.json``) reads networkx-style node-link JSON
 * whose edges live under the ``"edges"`` key.  The C++ sibling reader
 * ``netlistx::read_json_format`` (netlistx-cpp) expects the ``"links"`` key
 * emitted by its own writer instead.  Since sibling repositories cannot be
 * changed, this header provides the ``"edges"`` variant with byte-identical
 * semantics to netlistx-cpp's ``JsonReader``: module ids are ``0..M-1``, net
 * ids are ``M..M+N-1``, and ``num_pads`` is taken from the graph metadata.
 */

#include <cstdint>
#include <fstream>
#include <netlistx/netlist.hpp>
#include <nlohmann/json.hpp>
#include <string>

/**
 * @brief Read a node-link JSON netlist whose edges are under ``"edges"``.
 *
 * @param filename Path to the input JSON file
 * @return SimpleNetlist with integer module ids 0..M-1 and net ids M..M+N-1
 */
inline auto read_json_edges(const std::string& filename) -> SimpleNetlist {
    std::ifstream file(filename);
    nlohmann::json data;
    file >> data;
    const auto num_modules = data["graph"]["num_modules"].get<uint32_t>();
    const auto num_nets = data["graph"]["num_nets"].get<uint32_t>();
    const auto num_pads = data["graph"]["num_pads"].get<uint32_t>();
    xnetwork::SimpleGraph gr(num_modules + num_nets);
    for (const auto& link : data["edges"]) {
        const auto src = link["source"].get<uint32_t>();
        const auto dst = link["target"].get<uint32_t>();
        gr.add_edge(src, dst);
    }
    SimpleNetlist netlist{std::move(gr), num_modules, num_nets};
    netlist.num_pads = num_pads;
    return netlist;
}
