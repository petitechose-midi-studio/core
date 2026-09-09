#include "app/RpcLifetime.hpp"
#include <array>
#include <cassert>
#include <iostream>

int main() {
    // Exercise the desktop provider used by real endpoint construction. The
    // protocol's missing-entropy refusal is tested separately with a zero epoch.
    std::array<uint64_t, 16> lifetimes{};
    for (size_t i = 0; i < lifetimes.size(); ++i) {
        lifetimes[i] = core::app::createRpcLifetime();
        assert(lifetimes[i] != 0);
        for (size_t j = 0; j < i; ++j) assert(lifetimes[i] != lifetimes[j]);
    }
    std::cout << "Desktop endpoint entropy: 16 fresh nonzero lifetimes generated\n";
}
