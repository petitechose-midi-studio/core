#pragma once

#include <cstdint>

#include <oc/Config.hpp>

namespace core::diagnostics {

#if OC_ENABLE_STATS
struct DynamicMemorySnapshot {
    uint32_t psramAllocatedBytes = 0U;
    uint32_t psramUserBytes = 0U;
    uint32_t psramFreeBytes = 0U;
    uint32_t psramLargestBlock = 0U;
    uint32_t psramBlocks = 0U;
    uint32_t psramAllocationFailures = 0U;
    // Tracked since beginMemoryFootprintTracking(), including initialization.
    uint32_t psramPeakUserBytes = 0U;
    uint32_t psramMinimumFreeBytes = 0U;
    bool trackerReady = false;
    bool trackerOverflow = false;
    bool psramLargestBlockValid = false;
};

/** Starts the Teensy stack watermark before product initialization. */
void beginMemoryFootprintTracking();
/**
 * Keeps an exact, bounded view of the Teensy PSRAM allocator without scanning
 * the complete pool. Calls must bracket every product-owned extmem allocation.
 */
void trackExtmemAllocation(void* ptr);
void trackExtmemFree(void* ptr);
void trackExtmemAllocationFailure();
/** Returns one allocation-free snapshot of the bounded native PSRAM mirror. */
DynamicMemorySnapshot dynamicMemorySnapshot();
/** Emits one allocation-free PSRAM free/largest-block sample. */
void recordDynamicMemorySample(const char* label);
enum class MemoryReportSection : uint8_t {
    LVGL, PSRAM, RAM2_HEAP, RAM2_ALLOCATOR, RAM1_STACK, COUNT
};
/** Foreground-only: inspect and log one section, yielding between runtime calls. */
void logMemoryFootprintSection(const char* phase, MemoryReportSection section);
void logMemoryFootprint(const char* phase);
#endif

}  // namespace core::diagnostics
