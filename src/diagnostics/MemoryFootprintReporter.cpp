#include "diagnostics/MemoryFootprintReporter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>

#include <config/PlatformCompat.hpp>

#if OC_ENABLE_STATS
#include <lvgl.h>
#include <oc/diagnostics/Performance.hpp>
#include <oc/log/Log.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <malloc.h>
#include <smalloc.h>
#include <smalloc_i.h>

#include <oc/realtime/InterruptGuard.hpp>

extern "C" {
extern unsigned long _ebss;
extern unsigned long _estack;
extern unsigned long _heap_start;
extern unsigned long _heap_end;
extern char* __brkval;
}
#endif
#endif

namespace core::diagnostics {

#if OC_ENABLE_STATS
namespace {

struct MemoryHighWater {
    static constexpr size_t PSRAM_SPAN_CAPACITY = 256U;

    struct PsramSpan {
        uintptr_t begin = 0U;
        uintptr_t end = 0U;
        uint32_t userBytes = 0U;
    };

    uint32_t minimumPsramFree = UINT32_MAX;
    uint32_t minimumPsramLargestBlock = UINT32_MAX;
    uint32_t maximumPsramUser = 0U;
    uint32_t maximumLvglUsed = 0U;
    uint8_t maximumLvglFragmentation = 0U;
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    std::array<PsramSpan, PSRAM_SPAN_CAPACITY> psramSpans{};
    uint16_t psramSpanCount = 0U;
    uint16_t psramFallbackLive = 0U;
    uint16_t psramFallbackPeak = 0U;
    uint32_t psramFallbackTotal = 0U;
    uint32_t psramPoolBytes = 0U;
    uint32_t psramAllocatedBytes = 0U;
    uint32_t psramUserBytes = 0U;
    uint32_t psramLargestBlock = 0U;
    bool psramTrackerReady = false;
    bool psramTrackerOverflow = false;
    uintptr_t stackWatermarkLow = 0U;
    uintptr_t stackWatermarkHigh = 0U;
#endif
};

alignas(MemoryHighWater)
DMAMEM uint8_t memoryHighWaterStorage[sizeof(MemoryHighWater)];

MemoryHighWater& highWater() {
    static auto* state = new (memoryHighWaterStorage) MemoryHighWater;
    return *state;
}

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
constexpr uint32_t STACK_WATERMARK_PATTERN = UINT32_C(0xA55A3CC3);
constexpr uintptr_t STACK_WATERMARK_SAFETY_BYTES = 1024U;
constexpr size_t PSRAM_ALLOCATION_OVERHEAD = HEADER_SZ * 2U;

uint32_t boundedU32(size_t value) {
    return static_cast<uint32_t>(std::min<size_t>(value, UINT32_MAX));
}

uint32_t allocatableBytes(uintptr_t begin, uintptr_t end) {
    if (end <= begin) return 0U;
    const size_t span = static_cast<size_t>(end - begin);
    return span > PSRAM_ALLOCATION_OVERHEAD
        ? boundedU32(span - PSRAM_ALLOCATION_OVERHEAD)
        : 0U;
}

bool withinPsramPool(const void* ptr) {
    if (ptr == nullptr || extmem_smalloc_pool.pool == nullptr) return false;
    const uintptr_t begin =
        reinterpret_cast<uintptr_t>(extmem_smalloc_pool.pool);
    const uintptr_t end = begin + extmem_smalloc_pool.pool_size;
    const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    return address >= begin && address < end;
}

bool psramAllocationSpan(
    const void* ptr,
    MemoryHighWater::PsramSpan& out
) {
    if (ptr == nullptr || extmem_smalloc_pool.pool == nullptr) return false;
    const uintptr_t poolBegin =
        reinterpret_cast<uintptr_t>(extmem_smalloc_pool.pool);
    const uintptr_t poolEnd = poolBegin + extmem_smalloc_pool.pool_size;
    const uintptr_t user = reinterpret_cast<uintptr_t>(ptr);
    if (user < poolBegin + HEADER_SZ || user >= poolEnd) return false;

    auto* const header = USER_TO_HEADER(ptr);
    if (!smalloc_is_alloc(&extmem_smalloc_pool, header)) return false;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(header);
    const uintptr_t end =
        begin + HEADER_SZ + header->rsz + HEADER_SZ;
    if (begin < poolBegin || end <= begin || end > poolEnd) return false;
    out = {
        .begin = begin,
        .end = end,
        .userBytes = boundedU32(header->usz),
    };
    return true;
}

void updatePsramDerived(MemoryHighWater& state) {
    state.psramLargestBlock = 0U;
    if (!state.psramTrackerReady || state.psramTrackerOverflow ||
        extmem_smalloc_pool.pool == nullptr) {
        return;
    }

    uintptr_t cursor = reinterpret_cast<uintptr_t>(
        extmem_smalloc_pool.pool
    );
    const uintptr_t end = cursor + extmem_smalloc_pool.pool_size;
    for (uint16_t index = 0U; index < state.psramSpanCount; ++index) {
        const auto& span = state.psramSpans[index];
        if (span.begin > cursor) {
            state.psramLargestBlock = std::max(
                state.psramLargestBlock,
                allocatableBytes(cursor, span.begin)
            );
        }
        cursor = std::max(cursor, span.end);
    }
    state.psramLargestBlock = std::max(
        state.psramLargestBlock,
        allocatableBytes(cursor, end)
    );
    const uint32_t freeBytes =
        state.psramPoolBytes >= state.psramAllocatedBytes
        ? state.psramPoolBytes - state.psramAllocatedBytes
        : 0U;
    state.minimumPsramFree = std::min(
        state.minimumPsramFree,
        freeBytes
    );
    state.minimumPsramLargestBlock = std::min(
        state.minimumPsramLargestBlock,
        state.psramLargestBlock
    );
    state.maximumPsramUser = std::max(
        state.maximumPsramUser,
        state.psramUserBytes
    );
}

bool insertPsramSpan(
    MemoryHighWater& state,
    const MemoryHighWater::PsramSpan& span
) {
    if (state.psramSpanCount >= state.psramSpans.size()) return false;
    uint16_t position = 0U;
    while (position < state.psramSpanCount &&
           state.psramSpans[position].begin < span.begin) {
        ++position;
    }
    if (position < state.psramSpanCount &&
        state.psramSpans[position].begin == span.begin) {
        return false;
    }
    for (uint16_t index = state.psramSpanCount;
         index > position;
         --index) {
        state.psramSpans[index] = state.psramSpans[index - 1U];
    }
    state.psramSpans[position] = span;
    ++state.psramSpanCount;
    return true;
}

bool removePsramSpan(
    MemoryHighWater& state,
    uintptr_t begin
) {
    uint16_t position = 0U;
    while (position < state.psramSpanCount &&
           state.psramSpans[position].begin != begin) {
        ++position;
    }
    if (position >= state.psramSpanCount) return false;
    for (uint16_t index = position + 1U;
         index < state.psramSpanCount;
         ++index) {
        state.psramSpans[index - 1U] = state.psramSpans[index];
    }
    --state.psramSpanCount;
    state.psramSpans[state.psramSpanCount] = {};
    return true;
}

void bootstrapPsramTracker(MemoryHighWater& state) {
    if (extmem_smalloc_pool.pool == nullptr ||
        extmem_smalloc_pool.pool_size <= PSRAM_ALLOCATION_OVERHEAD) {
        return;
    }
    state.psramPoolBytes = boundedU32(extmem_smalloc_pool.pool_size);
    auto* const base = static_cast<char*>(extmem_smalloc_pool.pool);
    auto* const end = base + extmem_smalloc_pool.pool_size;
    auto* cursor = reinterpret_cast<struct smalloc_hdr*>(base);
    while (reinterpret_cast<char*>(cursor) < end) {
        if (!smalloc_is_alloc(&extmem_smalloc_pool, cursor)) {
            ++cursor;
            continue;
        }
        MemoryHighWater::PsramSpan span{};
        if (!psramAllocationSpan(HEADER_TO_USER(cursor), span)) {
            state.psramTrackerOverflow = true;
            break;
        }
        state.psramAllocatedBytes += boundedU32(span.end - span.begin);
        state.psramUserBytes += span.userBytes;
        if (!insertPsramSpan(state, span)) {
            state.psramTrackerOverflow = true;
            break;
        }
        cursor = reinterpret_cast<struct smalloc_hdr*>(span.end);
    }
    state.psramTrackerReady = true;
    updatePsramDerived(state);
}

uintptr_t currentStackPointer() {
    uintptr_t stackPointer = 0U;
    asm volatile("mov %0, sp" : "=r"(stackPointer));
    return stackPointer;
}

uintptr_t stackHighWaterBoundary(const MemoryHighWater& state) {
    if (state.stackWatermarkLow == 0U ||
        state.stackWatermarkHigh <= state.stackWatermarkLow) {
        return 0U;
    }
    auto* cursor = reinterpret_cast<volatile uint32_t*>(
        state.stackWatermarkLow
    );
    auto* const end = reinterpret_cast<volatile uint32_t*>(
        state.stackWatermarkHigh
    );
    while (cursor < end && *cursor == STACK_WATERMARK_PATTERN) {
        ++cursor;
    }
    return reinterpret_cast<uintptr_t>(cursor);
}

#endif

}  // namespace

FLASHMEM void beginMemoryFootprintTracking() {
    highWater() = {};
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    auto& state = highWater();
    bootstrapPsramTracker(state);
    const uintptr_t low = (
        reinterpret_cast<uintptr_t>(&_ebss) + alignof(uint32_t) - 1U
    ) & ~(static_cast<uintptr_t>(alignof(uint32_t) - 1U));
    const uintptr_t stackPointer = currentStackPointer();
    const uintptr_t high = stackPointer >
            low + STACK_WATERMARK_SAFETY_BYTES
        ? (stackPointer - STACK_WATERMARK_SAFETY_BYTES) &
            ~(static_cast<uintptr_t>(alignof(uint32_t) - 1U))
        : low;
    if (high <= low) return;

    oc::realtime::InterruptGuard lock;
    auto* cursor = reinterpret_cast<volatile uint32_t*>(low);
    auto* const end = reinterpret_cast<volatile uint32_t*>(high);
    while (cursor < end) {
        *cursor++ = STACK_WATERMARK_PATTERN;
    }
    state.stackWatermarkLow = low;
    state.stackWatermarkHigh = high;
#endif
}

void trackExtmemAllocation(void* ptr) {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    auto& state = highWater();
    if (!state.psramTrackerReady || ptr == nullptr) return;
    MemoryHighWater::PsramSpan span{};
    if (!psramAllocationSpan(ptr, span)) {
        oc::realtime::InterruptGuard lock;
        if (withinPsramPool(ptr)) {
            state.psramTrackerOverflow = true;
        } else {
            ++state.psramFallbackLive;
            ++state.psramFallbackTotal;
            state.psramFallbackPeak = std::max(
                state.psramFallbackPeak,
                state.psramFallbackLive
            );
        }
        return;
    }

    oc::realtime::InterruptGuard lock;
    state.psramAllocatedBytes += boundedU32(span.end - span.begin);
    state.psramUserBytes += span.userBytes;
    if (!insertPsramSpan(state, span)) {
        state.psramTrackerOverflow = true;
    }
    updatePsramDerived(state);
#else
    (void)ptr;
#endif
}

void trackExtmemFree(void* ptr) {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    auto& state = highWater();
    if (!state.psramTrackerReady || ptr == nullptr) return;
    MemoryHighWater::PsramSpan span{};
    if (!psramAllocationSpan(ptr, span)) {
        oc::realtime::InterruptGuard lock;
        if (withinPsramPool(ptr)) {
            state.psramTrackerOverflow = true;
        } else if (state.psramFallbackLive > 0U) {
            --state.psramFallbackLive;
        }
        return;
    }

    oc::realtime::InterruptGuard lock;
    const uint32_t allocationBytes = boundedU32(span.end - span.begin);
    state.psramAllocatedBytes =
        state.psramAllocatedBytes >= allocationBytes
        ? state.psramAllocatedBytes - allocationBytes
        : 0U;
    state.psramUserBytes =
        state.psramUserBytes >= span.userBytes
        ? state.psramUserBytes - span.userBytes
        : 0U;
    if (!removePsramSpan(state, span.begin)) {
        state.psramTrackerOverflow = true;
    }
    updatePsramDerived(state);
#else
    (void)ptr;
#endif
}

FLASHMEM void recordDynamicMemorySample(const char* label) {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    auto& state = highWater();
    uint32_t freeBytes = 0U;
    uint32_t largestBlock = 0U;
    {
        oc::realtime::InterruptGuard lock;
        freeBytes = state.psramPoolBytes >= state.psramAllocatedBytes
            ? state.psramPoolBytes - state.psramAllocatedBytes
            : 0U;
        largestBlock = state.psramLargestBlock;
    }
    OC_PERF_RECORD(label, 0U, freeBytes, largestBlock);
#else
    (void)label;
#endif
}

FLASHMEM void logMemoryFootprint(const char* phase) {
    lv_mem_monitor_t lvgl{};
    lv_mem_monitor(&lvgl);
    auto& high = highWater();
    high.maximumLvglUsed = std::max(
        high.maximumLvglUsed,
        static_cast<uint32_t>(lvgl.max_used)
    );
    high.maximumLvglFragmentation = std::max(
        high.maximumLvglFragmentation,
        lvgl.frag_pct
    );
    OC_LOG_INFO(
        "[Perf][Memory][LVGL] phase={} total={}B free={}B maxUsed={}B used={}pct frag={}pct peakUsed={}B peakFrag={}pct",
        phase ? phase : "unknown",
        static_cast<uint32_t>(lvgl.total_size),
        static_cast<uint32_t>(lvgl.free_size),
        static_cast<uint32_t>(lvgl.max_used),
        lvgl.used_pct,
        lvgl.frag_pct,
        high.maximumLvglUsed,
        high.maximumLvglFragmentation
    );

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    uint32_t allocated = 0U;
    uint32_t user = 0U;
    uint32_t free = 0U;
    uint32_t largest = 0U;
    uint32_t peakUser = 0U;
    uint32_t lowFree = 0U;
    uint32_t lowLargest = 0U;
    uint16_t blocks = 0U;
    uint16_t fallbackLive = 0U;
    uint16_t fallbackPeak = 0U;
    uint32_t fallbackTotal = 0U;
    int trackerStatus = 0;
    {
        oc::realtime::InterruptGuard lock;
        allocated = high.psramAllocatedBytes;
        user = high.psramUserBytes;
        free = high.psramPoolBytes >= allocated
            ? high.psramPoolBytes - allocated
            : 0U;
        largest = high.psramLargestBlock;
        blocks = high.psramSpanCount;
        peakUser = high.maximumPsramUser;
        lowFree = high.minimumPsramFree;
        lowLargest = high.minimumPsramLargestBlock;
        fallbackLive = high.psramFallbackLive;
        fallbackPeak = high.psramFallbackPeak;
        fallbackTotal = high.psramFallbackTotal;
        trackerStatus = !high.psramTrackerReady
            ? -1
            : (high.psramTrackerOverflow ? -2 : 1);
    }
    OC_LOG_INFO(
        "[Perf][Memory][PSRAM] phase={} status={} allocated={}B user={}B free={}B largest={}B blocks={} peakUser={}B lowFree={}B lowLargest={}B fallback(live/peak/total)={}/{}/{}",
        phase ? phase : "unknown",
        trackerStatus,
        allocated,
        user,
        free,
        largest,
        blocks,
        peakUser,
        lowFree,
        lowLargest,
        fallbackLive,
        fallbackPeak,
        fallbackTotal
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

    const uintptr_t stackPointer = currentStackPointer();
    const uintptr_t ram1StaticEnd = reinterpret_cast<uintptr_t>(&_ebss);
    const uintptr_t stackTop = reinterpret_cast<uintptr_t>(&_estack);
    const uintptr_t highWaterBoundary = stackHighWaterBoundary(high);
    OC_LOG_INFO(
        "[Perf][Memory][RAM1Stack] phase={} currentGap={}B highWaterGap={}B peakUsed={}B watermarked={}",
        phase ? phase : "unknown",
        static_cast<uint32_t>(
            stackPointer >= ram1StaticEnd ? stackPointer - ram1StaticEnd : 0
        ),
        static_cast<uint32_t>(
            highWaterBoundary >= ram1StaticEnd
                ? highWaterBoundary - ram1StaticEnd
                : 0U
        ),
        static_cast<uint32_t>(
            highWaterBoundary > 0U && stackTop >= highWaterBoundary
                ? stackTop - highWaterBoundary
                : 0U
        ),
        highWaterBoundary > 0U ? 1U : 0U
    );
#endif
}
#endif

}  // namespace core::diagnostics
