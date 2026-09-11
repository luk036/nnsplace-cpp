# AGENTS.md - Agent Coding Guidelines for nnsplace-cpp

This file provides context and coding guidelines for AI agents working in this repository.

## Project Overview

- **Name**: nnsplace (No-Nonsense Placement)
- **Type**: Compiled C++20 placement library (public headers + `source/*.cpp`).
  Rule: a function body of more than 15 lines must live in a `.cpp`; the
  headers declare it (≤15-line bodies stay inline).  The only exceptions are
  templates (e.g. `hungarian`), which cannot move out of a header.
- **Language**: C++20
- **Build System**: xmake (see `xmake.lua`; sibling headers are compiled from
  `../netlistx-cpp/source/*.cpp` exactly like netoptim-cpp compiles ellalgo sources)
- **Test Framework**: doctest
- **Port of**: `https://github.com/luk036/nnsplace` (Python); the reference is
  `D:\github\py\nnsplace\src\nnsplace\placement.py` + `placement_cfg.py`.

## Directory Structure

```
nnsplace-cpp/
├── include/
│   ├── nnsplace/            # Public headers (placement.hpp, placement_cfg.hpp)
│   ├── nnsplace/matching.hpp  # Rectangular min-weight (Hungarian) matching
│   └── fractions/           # Vendored patched pyfractions.hpp (see below)
├── test/
│   └── source/              # doctest suite (main.cpp + test_placement.cpp)
├── testcases/               # p1.json, drawf.json, fix.json (JSON netlists)
├── xmake.lua                # Build configuration
└── AGENTS.md                # This file
```

## Build Commands

```bash
xmake f -m debug            # configure (debug)
xmake                       # build (must be warning-clean: /W4 /WX on Windows)
xmake run test_nnsplace     # run the doctest suite
xmake test                  # run via the registered test runner
```

Run a single test case:

```bash
xmake run test_nnsplace -tc="test_placement_p1_32"
xmake run test_nnsplace -tc="test_*"
```

## Code Style Guidelines

Follow the sibling projects (netoptim-cpp, digraphx-cpp):

- `.clang-format` Google based, 100 columns, 4-space indent.
- Doxygen doc comments on public API (`@file`, `\brief`, `\param`, `\return`).
- No namespace wrapper: mirror netoptim-cpp and define library code at global
  scope (`NnsConfig`, `NnsPlacer`, `FlowGraph`, `HowardsCost`, ...).
- Naming: classes `PascalCase`, functions/methods `snake_case`, private members
  prefixed `_` (e.g. `_arcs`), constants `kCamelCase` / `UPPER_CASE`.
- Include order: stdlib -> external (`digraphx/...`, `fractions/...`,
  `recti/...`, `xnetwork/...`, `nlohmann/...`) -> local (`nnsplace/...`).
- Prefer `std::optional`/exceptions over sentinels; use `assert()` for
  invariants (mirroring the Python `assert`s in `init_placement`).
- No raw `new`/`delete`; no macros where functions work; write code that
  compiles warning-clean with MSVC `/W4 /WX /wd4702`.

## Testing

- doctest; `test/source/main.cpp` has `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`.
- Test cases port the Python suite in
  `D:\github\py\nnsplace\tests\test_placement.py`, `test_placement_cfg.py`,
  `test_place.py` and `test_line_limit.py` (legality assertions only - no SVG).
- Integer module ids only (SimpleNetlist is int based).
- The runtime working directory is the project dir (`set_rundir`), so
  `testcases/p1.json` resolves at run time.

## Key Design Notes

- `create_flow_graph` reproduces the Python edge construction exactly: for every
  net, for every ordered vertex pair `(v1, v2)`, add `v1 -> v2` and `v2 -> v1`
  unless `module_weight[v2] == 0`; self loops appear for every non-pad net
  member; edges are de-duplicated and sorted per node.
- `apply_howard` feeds a `MapAdapter`-wrapped adjacency (node -> (neighbour,
  mutable cost)) into `digraphx::MinParametricSolver` with
  `fractions::Fraction<int64_t>` as Ratio/Domain.  Positions are read back from
  `numerator()`; every stored value is integral.
- Legalization = rectangular Hungarian assignment over module->slot candidate
  edges (see `matching.hpp`). A local +/- radius window grows up to
  `kMaxNeighborhood`; the global free-slot fallback guarantees success when the
  grid has capacity (RuntimeError otherwise, like Python).

## Deliberate Deviations From The Python Source

1. **JSON reading**: netlistx-cpp's `JsonReader` reads a `"links"` container,
   but the copied `testcases/*.json` files (written by the netlistx writers)
   use a `"edges"` container.  Tests therefore read the JSON via a local helper
   `load_json_format` that mirrors `JsonReader` but reads `"edges"`.
2. **Vendored fractions header**: the sibling `../fractions-cpp` port of
   `fractions.Fraction` divides by `abs(numerator)` inside its overflow guards,
   crashing when a zero operand (a zero-weight flow arc) is added/subtracted
   during Howard relaxation.  `include/fractions/pyfractions.hpp` is an
   otherwise-identical copy whose guards route the quotient through
   `_max_div`, which yields `max` for a zero divisor.  It is found before
   `../fractions-cpp/include` on the include path (only our headers include it).
   It additionally defines `explicit operator T()`, letting `apply_howard` run
   `MinParametricSolver` with an **integer** `Domain` (`Coord`) while the ratio
   stays rational, matching the Python reference whose `dist` is a plain int
   dict (avoids Fraction gcd work in every relaxation step).
3. Determinism: initial placement shuffles use `std::mt19937` with a fixed seed
   (not Python's `random`); tests assert legality/improvement, never bit-exact
   worst values.

## Sibling Dependencies (read-only, do not modify)

- `D:\github\cpp\digraphx-cpp\include` (MinParametricSolver, NegCycleFinderQ)
- `D:\github\cpp\fractions-cpp`, `physdes-cpp`, `mywheel-cpp`, `xnetwork-cpp`,
  `netlistx-cpp`, `py2cpp`
- The Python reference at `D:\github\py\nnsplace`.

## Follow-up Updates (2026-09)

- **JSON loader moved to the library**: the ``"edges"`` node-link reader is now
  `read_json_edges` in `include/nnsplace/readwrite.hpp` (was a test-local
  `load_json_format`).  Standalone + tests share it.
- **Standalone runner**: `standalone/source/main.cpp` (xmake target
  `nnsplace_standalone`, CMake target `NnsPlaceStandalone`) prints the same
  before/after HPWL + worst wire length metrics as Python for a side-by-side
  parity check; `experiments/parity.py` is the Python-side runner.
- **Bug fix: memento snapshot refresh**.  `optimize()`/`run()` must re-snapshot
  the best state after every improving iteration (Python re-creates
  `PlacerState` in the loop).  The initial port snapshotted once at entry, so a
  stall rolled back to the *original* placement and the placer never improved.
  Now 32x32 p1 (seed 831) improves worst 2160 -> 1480 (Python: 2240 -> 1480).
- **Regression guard**: `run_placer` and the capped p1 tests now assert strict
  worst-length improvement (`result.second < before`), which the snapshot bug
  would have failed.
- **CMake**: added mirroring netoptim-cpp's structure (CPM + `INSTALL_ONLY`,
  fmt/spdlog/abseil handling, PackageProject, doctest, format/coverage/docs
  targets).  Configure with VS 18 2026; see Build Commands above.
- **Compiled library refactor**: function bodies longer than 15 lines moved
  from the headers into `source/*.cpp` (`placement_cfg.cpp`, `readwrite.cpp`,
  `matching.cpp`, `placement.cpp`).  Both `xmake.lua` (target `NnsPlace`) and
  `CMakeLists.txt` (static `NnsPlace`, CXX_STANDARD 20) build the library and
  link it into the tests and standalone.  `hungarian` stays a header template
  (cannot be moved without explicit instantiation).  Verified: xmake
  31/31 + 19315 assertions, then CMake ctest 100% (identical results).
