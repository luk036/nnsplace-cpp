# nnsplace-cpp

Affordable placement C++ library (for FPGA), a C++20 port of
[luk036/nnsplace](https://github.com/luk036/nnsplace).

Function bodies of more than 15 lines live in `source/*.cpp`; the headers only
declare them (bodies of 15 lines or fewer stay inline).

The placer builds a directed flow graph from a hypergraph netlist, generates a
random initial placement, then minimizes the worst (HPWL) wire length with
Howard's parametric minimum-cost-flow algorithm
([digraphx-cpp](https://github.com/luk036/digraphx-cpp)), legalizes module
positions through minimum-weight bipartite (Hungarian) assignment, and snaps
I/O pads onto the grid ring.

## Features

- "No-nonsense" (NNS) iterative placement minimizing worst-case wire length
- Parametric min-cost flow optimization (Howard's algorithm) via `digraphx-cpp`
- Min-weight rectangular assignment (Hungarian O(n^3)) for legalization
- I/O pad ring assignment with per-edge capacity limits
- Optional per-line capacity capping (`line_cap_ratio`)

## Build & Test

Requires [xmake](https://xmake.io). Sibling header-only libraries
(`../netlistx-cpp`, `../digraphx-cpp`, `../physdes-cpp`, `../fractions-cpp`,
`../mywheel-cpp`, `../xnetwork-cpp`, `../py2cpp`) are referenced by relative
path exactly like `netoptim-cpp`.

```bash
xmake f -m debug     # or xmake f -m release
xmake
xmake run test_nnsplace
```

The build is warning-clean (MSVC `/W4 /WX` on Windows); tests are doctest cases
ported from the Python suite.

## Related projects

- [luk036/nnsplace](https://github.com/luk036/nnsplace): the Python reference
- [luk036/digraphx-cpp](https://github.com/luk036/digraphx-cpp)
- [luk036/netlistx-cpp](https://github.com/luk036/netlistx-cpp)
- [luk036/netoptim-cpp](https://github.com/luk036/netoptim-cpp)

## CMake build (mirrors netoptim-cpp)

```bash
cmake -B build-cmake -G "Visual Studio 18 2026" -A x64   # any CMake generator works
cmake --build build-cmake --config Debug
ctest --test-dir build-cmake -C Debug --output-on-failure
```

## Side-by-side parity with the Python reference

```bash
xmake run nnsplace_standalone testcases/p1.json 50 50 40 2000   # C++ (seed 831)
python experiments/parity.py testcases/p1.json 50 50             # Python (seed 831)
```

## Routed figures & congestion maps

`nnsplace_figures` runs the placer, routes every net with the physdes global
router (`recti::GlobalRouter`) and emits an SVG of the routed placement (the
straight pad-to-module lines replaced by the orthogonal routing-tree branches)
plus green-yellow-red congestion heat maps for the x direction, the y
direction and their element-wise maximum.  Usage:

```bash
xmake run nnsplace_figures testcases/p1.json 32 32 40 2000 0 831 ./out
```

writes `ioloop32x32-routed.svg` and `congestion32x32-{x,y,combined}.svg` into
`./out`.  Each map's heat scale is normalised to its own busiest cut.
