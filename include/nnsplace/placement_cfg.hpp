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
#include <stdexcept>
#include <string>

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
              std::optional<double> cap_ratio = std::nullopt)
        : grid{x, y},
          delta{delta_x, delta_y},
          reserved_col{reserved_col_index.value_or(DEFAULT_RESERVED_COL)},
          line_cap_ratio{cap_ratio} {
        if (x < 3) {
            throw std::invalid_argument("Grid width must be at least 3, got " + std::to_string(x));
        }
        if (y < 3) {
            throw std::invalid_argument("Grid height must be at least 3, got " + std::to_string(y));
        }
        if (delta_x <= 0) {
            throw std::invalid_argument("delta_x must be positive, got " + std::to_string(delta_x));
        }
        if (delta_y <= 0) {
            throw std::invalid_argument("delta_y must be positive, got " + std::to_string(delta_y));
        }
        if (reserved_col_index && (reserved_col < 1 || reserved_col > x)) {
            throw std::invalid_argument("reserved_col must be between 1 and " + std::to_string(x)
                                        + ", got " + std::to_string(reserved_col));
        }
        if (cap_ratio && *cap_ratio <= 0.0) {
            throw std::invalid_argument("line_cap_ratio must be positive, got "
                                        + std::to_string(*cap_ratio));
        }
    }
};
