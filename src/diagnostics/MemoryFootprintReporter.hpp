#pragma once

#include <oc/Config.hpp>

namespace core::diagnostics {

#if OC_ENABLE_STATS
void logMemoryFootprint(const char* phase);
#endif

}  // namespace core::diagnostics
