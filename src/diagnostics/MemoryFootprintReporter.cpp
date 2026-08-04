#include "diagnostics/MemoryFootprintReporter.hpp"

#include <algorithm>
#include <cstdint>
#include <new>

#include <config/PlatformCompat.hpp>

#if OC_ENABLE_STATS
#include "diagnostics/PsramSpanTracker.hpp"

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
    uint32_t minimumPsramFree = UINT32_MAX;
    uint32_t minimumPsramLargestBlock = UINT32_MAX;
    uint32_t maximumPsramUser = 0U;
    uint32_t maximumLvglUsed = 0U;
    uint8_t maximumLvglFragmentation = 0U;
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    detail::PsramSpanTracker psramTracker{};
    detail::PsramTrackerSnapshot psramPublished{};
    uint16_t psramFallbackLive = 0U;
    uint16_t psramFallbackPeak = 0U;
    uint32_t psramFallbackTotal = 0U;
    uint32_t psramAllocationFailures = 0U;
    uintptr_t stackWatermarkLow = 0U;
    uintptr_t stackWatermarkHigh = 0U;
#endif
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(MemoryHighWater) <= 128U);
EXTMEM detail::PsramSpanTable psramSpanTable;
#endif

alignas(MemoryHighWater)
DMAMEM uint8_t memoryHighWaterStorage[sizeof(MemoryHighWater)];

MemoryHighWater& highWater() {
    static auto* state = new (memoryHighWaterStorage) MemoryHighWater;
    return *state;
}

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
constexpr uint32_t STACK_WATERMARK_PATTERN = UINT32_C(0xA55A3CC3);
// Teensyduino installs a 32-byte NOACCESS MPU region at _ebss to trap stack
// overflow. The watermark must begin after that guard, never on _ebss itself.
constexpr uintptr_t STACK_MPU_GUARD_BYTES = 32U;
constexpr uintptr_t STACK_WATERMARK_SAFETY_BYTES = 1024U;
constexpr size_t PSRAM_ALLOCATION_OVERHEAD = HEADER_SZ * 2U;

constexpr uintptr_t stackWatermarkLowAddress(uintptr_t ebss) {
    return (
        ebss + STACK_MPU_GUARD_BYTES + alignof(uint32_t) - 1U
    ) & ~(static_cast<uintptr_t>(alignof(uint32_t) - 1U));
}

static_assert(
    stackWatermarkLowAddress(UINT32_C(0x20014B80)) ==
        UINT32_C(0x20014BA0),
    "RAM1 watermark must skip the Teensy MPU stack guard"
);

uint32_t boundedU32(size_t value) {
    return static_cast<uint32_t>(std::min<size_t>(value, UINT32_MAX));
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
    detail::PsramSpan& out
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
        boundedU32(begin - poolBegin),
        boundedU32(end - poolBegin),
        boundedU32(header->usz),
    };
    return true;
}

void updatePsramHighWater(
    MemoryHighWater& state,
    const detail::PsramTrackerSnapshot& snapshot
) {
    if (!snapshot.ready) return;
    const uint32_t freeBytes = snapshot.poolBytes >= snapshot.allocatedBytes
        ? snapshot.poolBytes - snapshot.allocatedBytes
        : 0U;
    state.minimumPsramFree = std::min(
        state.minimumPsramFree,
        freeBytes
    );
    if (snapshot.largestBlockValid) {
        state.minimumPsramLargestBlock = std::min(
            state.minimumPsramLargestBlock,
            snapshot.largestBlock
        );
    }
    state.maximumPsramUser = std::max(
        state.maximumPsramUser,
        snapshot.userBytes
    );
}

void publishPsramTracker(MemoryHighWater& state) {
    const auto snapshot = state.psramTracker.snapshot();
    updatePsramHighWater(state, snapshot);
    oc::realtime::InterruptGuard lock;
    state.psramPublished.poolBytes = snapshot.poolBytes;
    state.psramPublished.allocatedBytes = snapshot.allocatedBytes;
    state.psramPublished.userBytes = snapshot.userBytes;
    state.psramPublished.largestBlock = snapshot.largestBlock;
    state.psramPublished.blockCount = snapshot.blockCount;
    state.psramPublished.ready = snapshot.ready;
    state.psramPublished.overflow = snapshot.overflow;
    state.psramPublished.largestBlockValid = snapshot.largestBlockValid;
}

void bootstrapPsramTracker(MemoryHighWater& state) {
    if (extmem_smalloc_pool.pool == nullptr ||
        extmem_smalloc_pool.pool_size <= PSRAM_ALLOCATION_OVERHEAD) {
        return;
    }
    state.psramTracker.reset(
        boundedU32(extmem_smalloc_pool.pool_size),
        boundedU32(PSRAM_ALLOCATION_OVERHEAD)
    );
    auto* const base = static_cast<char*>(extmem_smalloc_pool.pool);
    auto* const end = base + extmem_smalloc_pool.pool_size;
    auto* cursor = reinterpret_cast<struct smalloc_hdr*>(base);
    while (reinterpret_cast<char*>(cursor) < end) {
        if (!smalloc_is_alloc(&extmem_smalloc_pool, cursor)) {
            ++cursor;
            continue;
        }
        detail::PsramSpan span{};
        if (!psramAllocationSpan(HEADER_TO_USER(cursor), span)) {
            state.psramTracker.markOverflow();
            break;
        }
        (void)state.psramTracker.insert(psramSpanTable, span);
        cursor = reinterpret_cast<struct smalloc_hdr*>(
            base + span.endOffset
        );
    }
    publishPsramTracker(state);
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
    const uintptr_t low = stackWatermarkLowAddress(
        reinterpret_cast<uintptr_t>(&_ebss)
    );
    const uintptr_t stackPointer = currentStackPointer();
    const uintptr_t high = stackPointer >
            low + STACK_WATERMARK_SAFETY_BYTES
        ? (stackPointer - STACK_WATERMARK_SAFETY_BYTES) &
            ~(static_cast<uintptr_t>(alignof(uint32_t) - 1U))
        : low;
    if (high <= low) return;

    {
        oc::realtime::InterruptGuard lock;
        auto* cursor = reinterpret_cast<volatile uint32_t*>(low);
        auto* const end = reinterpret_cast<volatile uint32_t*>(high);
        while (cursor < end) {
            *cursor++ = STACK_WATERMARK_PATTERN;
        }
        state.stackWatermarkLow = low;
        state.stackWatermarkHigh = high;
    }
#endif
}

void trackExtmemAllocation(void* ptr) {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    auto& state = highWater();
    if (!state.psramTracker.ready() || ptr == nullptr) return;
    detail::PsramSpan span{};
    if (!psramAllocationSpan(ptr, span)) {
        if (withinPsramPool(ptr)) {
            state.psramTracker.markOverflow();
            publishPsramTracker(state);
        } else {
            oc::realtime::InterruptGuard lock;
            ++state.psramFallbackLive;
            ++state.psramFallbackTotal;
            state.psramFallbackPeak = std::max(
                state.psramFallbackPeak,
                state.psramFallbackLive
            );
        }
        return;
    }

    (void)state.psramTracker.insert(psramSpanTable, span);
    publishPsramTracker(state);
#else
    (void)ptr;
#endif
}

void trackExtmemFree(void* ptr) {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    auto& state = highWater();
    if (!state.psramTracker.ready() || ptr == nullptr) return;
    detail::PsramSpan span{};
    if (!psramAllocationSpan(ptr, span)) {
        if (withinPsramPool(ptr)) {
            state.psramTracker.markOverflow();
            publishPsramTracker(state);
        } else {
            oc::realtime::InterruptGuard lock;
            if (state.psramFallbackLive > 0U) {
                --state.psramFallbackLive;
            }
        }
        return;
    }

    (void)state.psramTracker.remove(psramSpanTable, span);
    publishPsramTracker(state);
#else
    (void)ptr;
#endif
}

FLASHMEM void trackExtmemAllocationFailure() {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    auto& state = highWater();
    oc::realtime::InterruptGuard lock;
    if (state.psramAllocationFailures != UINT32_MAX) {
        ++state.psramAllocationFailures;
    }
#endif
}

FLASHMEM DynamicMemorySnapshot dynamicMemorySnapshot() {
    DynamicMemorySnapshot snapshot{};
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    auto& state = highWater();
    uint32_t poolBytes = 0U;
    {
        oc::realtime::InterruptGuard lock;
        poolBytes = state.psramPublished.poolBytes;
        snapshot.psramAllocatedBytes = state.psramPublished.allocatedBytes;
        snapshot.psramUserBytes = state.psramPublished.userBytes;
        snapshot.psramLargestBlock = state.psramPublished.largestBlock;
        snapshot.psramBlocks = state.psramPublished.blockCount;
        snapshot.psramAllocationFailures = state.psramAllocationFailures;
        snapshot.trackerReady = state.psramPublished.ready;
        snapshot.trackerOverflow = state.psramPublished.overflow;
        snapshot.psramLargestBlockValid =
            state.psramPublished.largestBlockValid;
    }
    snapshot.psramFreeBytes = poolBytes >= snapshot.psramAllocatedBytes
        ? poolBytes - snapshot.psramAllocatedBytes
        : 0U;
#endif
    return snapshot;
}

FLASHMEM void recordDynamicMemorySample(const char* label) {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    auto& state = highWater();
    uint32_t freeBytes = 0U;
    uint32_t largestBlock = 0U;
    {
        oc::realtime::InterruptGuard lock;
        freeBytes = state.psramPublished.poolBytes >=
                state.psramPublished.allocatedBytes
            ? state.psramPublished.poolBytes -
                state.psramPublished.allocatedBytes
            : 0U;
        largestBlock = state.psramPublished.largestBlock;
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
    const auto memory = dynamicMemorySnapshot();
    const uint32_t allocated = memory.psramAllocatedBytes;
    const uint32_t user = memory.psramUserBytes;
    const uint32_t free = memory.psramFreeBytes;
    const uint32_t largest = memory.psramLargestBlock;
    const uint32_t blocks = memory.psramBlocks;
    uint32_t peakUser = 0U;
    uint32_t lowFree = 0U;
    uint32_t lowLargest = 0U;
    uint16_t fallbackLive = 0U;
    uint16_t fallbackPeak = 0U;
    uint32_t fallbackTotal = 0U;
    {
        oc::realtime::InterruptGuard lock;
        peakUser = high.maximumPsramUser;
        lowFree = high.minimumPsramFree;
        lowLargest = high.minimumPsramLargestBlock;
        fallbackLive = high.psramFallbackLive;
        fallbackPeak = high.psramFallbackPeak;
        fallbackTotal = high.psramFallbackTotal;
    }
    const int trackerStatus = !memory.trackerReady
        ? -1
        : (memory.trackerOverflow || !memory.psramLargestBlockValid ? -2 : 1);
    OC_LOG_INFO(
        "[Perf][Memory][PSRAM] phase={} status={} allocated={}B user={}B free={}B largest={}B largestValid={} blocks={} peakUser={}B lowFree={}B lowLargest={}B fallback(live/peak/total)={}/{}/{}",
        phase ? phase : "unknown",
        trackerStatus,
        allocated,
        user,
        free,
        largest,
        memory.psramLargestBlockValid ? 1U : 0U,
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
    const uintptr_t ram1StaticEnd =
        reinterpret_cast<uintptr_t>(&_ebss) + STACK_MPU_GUARD_BYTES;
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
