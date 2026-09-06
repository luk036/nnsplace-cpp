#include <nnsplace/version.h>

#include <iostream>

auto main() -> int {
    const auto ok = (NNSPLACE_VERSION_MAJOR >= 1);
    std::cout << "nnsplace installed test: version " << NNSPLACE_VERSION << "\n";
    return ok ? 0 : 1;
}
