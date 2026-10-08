/*! \file readwrite.cpp
 *  \brief Node-link ("edges") JSON netlist readers (undirected + directed).
 */

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>
#include <nnsplace/readwrite.hpp>
#include <string>
#include <vector>

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

auto read_directed_json_edges(const std::string& filename) -> DirectedNetlist {
    std::ifstream file(filename);
    nlohmann::json data;
    file >> data;
    const auto num_modules = data["graph"]["num_modules"].get<uint32_t>();
    const auto num_nets = data["graph"]["num_nets"].get<uint32_t>();
    const auto num_pads = data["graph"]["num_pads"].get<uint32_t>();
    xnetwork::SimpleGraph gr(static_cast<std::size_t>(num_modules) + num_nets);
    for (const auto& link : data["edges"]) {
        const auto src = link["source"].get<uint32_t>();
        const auto dst = link["target"].get<uint32_t>();
        gr.add_edge(src, dst);
    }
    SimpleNetlist netlist{std::move(gr), num_modules, num_nets};
    netlist.num_pads = num_pads;

    std::vector<std::optional<std::size_t>> net_driver(num_nets, std::nullopt);
    for (const auto& node : data["nodes"]) {
        if (!node.contains("driver") || node["driver"].is_null()) continue;
        const auto id = node["id"].get<std::size_t>();
        if (id < num_modules) continue;
        const auto net_index = id - num_modules;
        if (net_index < num_nets) {
            net_driver[net_index] = node["driver"].get<std::size_t>();
        }
    }
    return DirectedNetlist{.netlist = std::move(netlist), .net_driver = std::move(net_driver)};
}
