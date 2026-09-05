"""Side-by-side Python reference run for the C++ nnsplace port.

Mirrors standalone/source/main.cpp: read a node-link netlist, place it
randomly (seed 831), snap pads to the ring, then optimize with the NNS placer
and print the same metrics.  Compare output 1:1 against:

    xmake run nnsplace_standalone testcases/p1.json <gx> <gy> 40 2000 [ratio]
"""

import json
import random
import sys

sys.path.insert(0, r"D:\github\py\digraphx\src")
sys.path.insert(0, r"D:\github\py\nnsplace\src")

from netlistx.netlist import Netlist  # noqa: E402
from networkx.readwrite import json_graph  # noqa: E402
from nnsplace.placement import NnsPlacer  # noqa: E402
from nnsplace.placement_cfg import NnsConfig  # noqa: E402


def read_json_edges(file):
    """Read a node-link netlist (edges under ``"edges"``) like netlistx.read_json."""
    with open(file) as f:
        data = json.load(f)
    ugraph = json_graph.node_link_graph(
        data, directed=False, multigraph=False, link="edges"
    )
    num_modules = ugraph.graph["num_modules"]
    num_nets = ugraph.graph["num_nets"]
    num_pads = ugraph.graph["num_pads"]
    netlist = Netlist(ugraph, range(num_modules), range(num_modules, num_modules + num_nets))
    netlist.num_pads = num_pads
    return netlist


def run(file, gx, gy, delta=40, max_iters=2000, ratio=None):
    random.seed(831)
    H = read_json_edges(file)
    n = H.number_of_modules()
    cfg = NnsConfig(gx, gy, delta, delta, line_cap_ratio=ratio)
    placer = NnsPlacer(H, cfg)
    place = [{i: 0 for i in range(n)}, {i: 0 for i in range(n)}]
    placer.init_placement(place)
    placer.io_assign(place)

    hpwl_x0 = placer.calc_total_hull_length(place[0], 0)
    hpwl_y0 = placer.calc_total_hull_length(place[1], 1)
    worst0 = placer.calc_worst_wirelength(place)
    print(f"modules={n} pads={H.num_pads} grid={gx}x{gy} delta={delta}")
    print(f"Total HPWL before = {hpwl_x0} + {hpwl_y0} = {hpwl_x0 + hpwl_y0}")
    print(f"Worst wirelength before = {worst0}")

    niter, worst = placer.run(place, max_iters)

    hpwl_x1 = placer.calc_total_hull_length(place[0], 0)
    hpwl_y1 = placer.calc_total_hull_length(place[1], 1)
    print(f"iterations = {niter}")
    print(f"Worst wirelength after = {worst}")
    print(f"Total HPWL after = {hpwl_x1} + {hpwl_y1} = {hpwl_x1 + hpwl_y1}")


if __name__ == "__main__":
    f = sys.argv[1] if len(sys.argv) > 1 else "testcases/p1.json"
    gx = int(sys.argv[2]) if len(sys.argv) > 2 else 32
    gy = int(sys.argv[3]) if len(sys.argv) > 3 else 32
    r = float(sys.argv[4]) if len(sys.argv) > 4 else None
    run(f, gx, gy, ratio=r)
