/*! \file placement_cfg.cpp
 *  \brief Out-of-line definitions for the NNS placement configuration.
 */

#include <nnsplace/placement_cfg.hpp>

#include <optional>
#include <stdexcept>
#include <string>

NnsConfig::NnsConfig(const int64_t x, const int64_t y, const int64_t delta_x,
                     const int64_t delta_y, const std::optional<int64_t> reserved_col_index,
                     const std::optional<double> cap_ratio)
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
