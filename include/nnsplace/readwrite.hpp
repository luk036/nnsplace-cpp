#pragma once

/**
 * @file readwrite.hpp
 * @brief Node-link JSON netlist readers (undirected and direction-aware).
 *
 * The Python ``netlistx.read_json`` (and therefore the nnsplace Python
 * testcases, e.g. ``testcases/p1.json``) reads networkx-style node-link JSON
 * whose edges live under the ``"edges"`` key.  The C++ sibling reader
 * ``netlistx::read_json_format`` (netlistx-cpp) expects the ``"links"`` key
 * emitted by its own writer instead.  Since sibling repositories cannot be
 * changed, this header provides the ``"edges"`` variant with byte-identical
 * semantics to netlistx-cpp's ``JsonReader``: module ids are ``0..M-1``, net
 * ids are ``M..M+N-1``, and ``num_pads`` is taken from the graph metadata.
 *
 * Direction-aware variant: a net node may carry a ``"driver"`` attribute (the
 * module id that drives the net).  ``read_directed_json_edges`` exposes these
 * as a per-net optional driver vector so the placer can measure worst wire
 * length on driver -> sink wires only.
 */

#include <netlistx/netlist.hpp>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Read a node-link JSON netlist whose edges are under ``"edges"``.
 *
 * @param filename Path to the input JSON file
 * @return SimpleNetlist with integer module ids 0..M-1 and net ids M..M+N-1
 */
auto read_json_edges(const std::string& filename) -> SimpleNetlist;

/** \brief A netlist plus the driver module of every net (direction aware). */
struct DirectedNetlist {
    SimpleNetlist netlist;
    /// per-net optional driver module id, indexed by net position (0..N-1)
    std::vector<std::optional<std::size_t>> net_driver;
};

/**
 * @brief Read a directed node-link JSON netlist (``"edges"`` + net drivers).
 *
 * @param filename Path to the input JSON file
 * @return The netlist together with the driver of every net (``std::nullopt``
 *         when the net is undirected or the file carries no driver attribute)
 */
auto read_directed_json_edges(const std::string& filename) -> DirectedNetlist;
