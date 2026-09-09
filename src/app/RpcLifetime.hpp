#pragma once
#include <cstdint>

namespace core::app {
// Fresh endpoint identity, or zero when platform entropy is unavailable.
uint64_t createRpcLifetime();
}
