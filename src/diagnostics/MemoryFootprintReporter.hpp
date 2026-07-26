#pragma once

#include <oc/Config.hpp>

namespace core::diagnostics {

#if OC_ENABLE_STATS
/** Starts the Teensy stack watermark before product initialization. */
void beginMemoryFootprintTracking();
/**
 * Keeps an exact, bounded view of the Teensy PSRAM allocator without scanning
 * the complete pool. Calls must bracket every product-owned extmem allocation.
 */
void trackExtmemAllocation(void* ptr);
void trackExtmemFree(void* ptr);
/** Emits one allocation-free PSRAM free/largest-block sample. */
void recordDynamicMemorySample(const char* label);
void logMemoryFootprint(const char* phase);
#endif

}  // namespace core::diagnostics
