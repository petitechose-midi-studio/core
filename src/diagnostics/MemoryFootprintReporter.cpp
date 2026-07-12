#include "diagnostics/MemoryFootprintReporter.hpp"

#include <cstdint>

#include <config/PlatformCompat.hpp>

#if OC_ENABLE_STATS
#include <lvgl.h>
#include <oc/log/Log.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <malloc.h>
#include <smalloc.h>

extern "C" {
extern unsigned long _ebss;
extern unsigned long _heap_start;
extern unsigned long _heap_end;
extern char* __brkval;
}
#endif
#endif

namespace core::diagnostics {

#if OC_ENABLE_STATS
FLASHMEM void logMemoryFootprint(const char* phase) {
    lv_mem_monitor_t lvgl{};
    lv_mem_monitor(&lvgl);
    OC_LOG_INFO(
        "[Perf][Memory][LVGL] phase={} total={}B free={}B maxUsed={}B used={}pct frag={}pct",
        phase ? phase : "unknown",
        static_cast<uint32_t>(lvgl.total_size),
        static_cast<uint32_t>(lvgl.free_size),
        static_cast<uint32_t>(lvgl.max_used),
        lvgl.used_pct,
        lvgl.frag_pct
    );

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    size_t allocated = 0;
    size_t user = 0;
    size_t free = 0;
    int blocks = 0;
    const int extmemStatus = sm_malloc_stats_pool(
        &extmem_smalloc_pool,
        &allocated,
        &user,
        &free,
        &blocks
    );
    OC_LOG_INFO(
        "[Perf][Memory][PSRAM] phase={} status={} allocated={}B user={}B free={}B blocks={}",
        phase ? phase : "unknown",
        extmemStatus,
        static_cast<uint32_t>(allocated),
        static_cast<uint32_t>(user),
        static_cast<uint32_t>(free),
        blocks
    );

    const uintptr_t heapStart = reinterpret_cast<uintptr_t>(&_heap_start);
    const uintptr_t heapEnd = reinterpret_cast<uintptr_t>(&_heap_end);
    const uintptr_t heapBreak = reinterpret_cast<uintptr_t>(__brkval);
    const bool heapRangeValid =
        heapEnd >= heapStart && heapBreak >= heapStart && heapBreak <= heapEnd;
    const uint32_t heapCapacity = static_cast<uint32_t>(
        heapEnd >= heapStart ? heapEnd - heapStart : 0
    );
    const uint32_t heapHighWater = static_cast<uint32_t>(
        heapRangeValid ? heapBreak - heapStart : 0
    );
    const uint32_t heapTailFree = static_cast<uint32_t>(
        heapRangeValid ? heapEnd - heapBreak : 0
    );
    OC_LOG_INFO(
        "[Perf][Memory][RAM2Heap] phase={} capacity={}B highWater={}B tailFree={}B",
        phase ? phase : "unknown",
        heapCapacity,
        heapHighWater,
        heapTailFree
    );

    const struct mallinfo heapInfo = mallinfo();
    OC_LOG_INFO(
        "[Perf][Memory][RAM2Allocator] phase={} arena={}B used={}B free={}B freeChunks={} topFree={}B",
        phase ? phase : "unknown",
        static_cast<uint32_t>(heapInfo.arena),
        static_cast<uint32_t>(heapInfo.uordblks),
        static_cast<uint32_t>(heapInfo.fordblks),
        static_cast<uint32_t>(heapInfo.ordblks),
        static_cast<uint32_t>(heapInfo.keepcost)
    );

    uintptr_t stackPointer = 0;
    asm volatile("mov %0, sp" : "=r"(stackPointer));
    const uintptr_t ram1StaticEnd = reinterpret_cast<uintptr_t>(&_ebss);
    OC_LOG_INFO(
        "[Perf][Memory][RAM1Stack] phase={} currentGap={}B",
        phase ? phase : "unknown",
        static_cast<uint32_t>(
            stackPointer >= ram1StaticEnd ? stackPointer - ram1StaticEnd : 0
        )
    );
#endif
}
#endif

}  // namespace core::diagnostics
