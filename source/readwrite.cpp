/*! \file readwrite.cpp
 *  \brief Node-link ("edges") JSON netlist reader definition.
 */

#include <nnsplace/readwrite.hpp>

#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

auto read_json_edges(const std::string& filename) -> SimpleNetlist {
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
