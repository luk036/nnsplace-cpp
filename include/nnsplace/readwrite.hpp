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

#include <netlistx/netlist.hpp>
#include <string>

/**
 * @brief Read a node-link JSON netlist whose edges are under ``"edges"``.
 *
 * @param filename Path to the input JSON file
 * @return SimpleNetlist with integer module ids 0..M-1 and net ids M..M+N-1
 */
auto read_json_edges(const std::string& filename) -> SimpleNetlist;
