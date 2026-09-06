/*! \file placement_cfg.hpp
 *  \brief Configuration for the "no-nonsense" (NNS) placement engine.
 *
 *  C++ port of the Python ``nnsplace.placement_cfg.NnsConfig``.
 *  Mirrors the netoptim-cpp convention: no namespace wrapper.
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>

/** \brief No-Nonsense Placement configuration. */
class NnsConfig {
  public:
    /** Column reserved for DSP/SRAM blocks. */
    static constexpr int DEFAULT_RESERVED_COL = 27;

    /// Grid dimensions: grid[0] = width (columns), grid[1] = height (rows).
    std::array<int64_t, 2> grid;
    /// Cost weight per unit length along each axis.
    std::array<int64_t, 2> delta;
    /// Column index reserved for DSP/SRAM (default: 27).
    int64_t reserved_col;
    /// Optional per-line capacity ratio (see ctor docs).
    std::optional<double> line_cap_ratio;

    /**
     * \brief Construct and validate the placement configuration.
     *
     * \param x           width of the core grid (number of columns)
     * \param y           height of the core grid (number of rows)
     * \param delta_x     weight factor for the x-axis wire-length cost
     * \param delta_y     weight factor for the y-axis wire-length cost
     * \param reserved_col_index column kept free for DSP/SRAM; when absent,
     *                    DEFAULT_RESERVED_COL (27) is used
     * \param cap_ratio   optional per-line core capacity factor; when present the
     *                    per-line limit becomes ``ceil(sqrt(num_cells) * ratio)``
     *
     * \throws std::invalid_argument when a dimension is too small, a delta is
     *         non-positive, ``reserved_col`` falls outside ``[1, x]``, or the
     *         line-cap ratio is non-positive (same conditions as the Python
     *         ``ValueError``).
     */
    NnsConfig(int64_t x, int64_t y, int64_t delta_x, int64_t delta_y,
              std::optional<int64_t> reserved_col_index = std::nullopt,
              std::optional<double> cap_ratio = std::nullopt);
};
