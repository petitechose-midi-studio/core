#pragma once

#include <memory>
#include <new>
#include <limits>
#include <type_traits>
#include <utility>

#include <config/PlatformCompat.hpp>

#if OC_ENABLE_STATS
#include "diagnostics/MemoryFootprintReporter.hpp"
#endif

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <smalloc.h>
#endif

namespace core::app {

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
/**
 * Strict PSRAM allocator. Teensy's public extmem_malloc() falls back to the
 * internal heap; transaction and retained EXTMEM owners must fail instead.
 */
inline void* allocateExtmemStrict(std::size_t bytes) noexcept {
    if (bytes == 0U || extmem_smalloc_pool.pool == nullptr) return nullptr;
    return sm_malloc_pool(&extmem_smalloc_pool, bytes);
}

inline void freeExtmemStrict(void* ptr) noexcept {
    if (ptr == nullptr) return;
    sm_free_pool(&extmem_smalloc_pool, ptr);
}
#endif

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
namespace testing {

inline std::size_t extmemAllocationFailureOrdinal = 0U;
inline std::size_t extmemAllocationAttempt = 0U;

inline void resetExtmemAllocationFailure() {
    extmemAllocationFailureOrdinal = 0U;
    extmemAllocationAttempt = 0U;
}

inline void failExtmemAllocationOn(std::size_t ordinal) {
    extmemAllocationFailureOrdinal = ordinal;
    extmemAllocationAttempt = 0U;
}

inline bool consumeExtmemAllocationFailure() {
    if (extmemAllocationFailureOrdinal == 0U) return false;
    ++extmemAllocationAttempt;
    if (extmemAllocationAttempt != extmemAllocationFailureOrdinal) return false;
    extmemAllocationFailureOrdinal = 0U;
    return true;
}

class ScopedExtmemAllocationFailure {
public:
    explicit ScopedExtmemAllocationFailure(std::size_t ordinal) {
        failExtmemAllocationOn(ordinal);
    }

    ~ScopedExtmemAllocationFailure() {
        resetExtmemAllocationFailure();
    }

    ScopedExtmemAllocationFailure(const ScopedExtmemAllocationFailure&) = delete;
    ScopedExtmemAllocationFailure& operator=(const ScopedExtmemAllocationFailure&) = delete;
};

}  // namespace testing
#endif

template <typename T>
struct ExtmemDeleter {
    void operator()(T* ptr) const noexcept {
        if (!ptr) return;
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
        ptr->~T();
#if OC_ENABLE_STATS
        core::diagnostics::trackExtmemFree(ptr);
#endif
        freeExtmemStrict(ptr);
#else
        delete ptr;
#endif
    }
};

template <typename T>
using ExtmemUniquePtr = std::unique_ptr<T, ExtmemDeleter<T>>;

template <typename T>
struct ExtmemArrayDeleter {
    void operator()(T* ptr) const noexcept {
        if (!ptr) return;
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#if OC_ENABLE_STATS
        core::diagnostics::trackExtmemFree(ptr);
#endif
        freeExtmemStrict(ptr);
#else
        delete[] ptr;
#endif
    }
};

template <typename T>
using ExtmemUniqueArray = std::unique_ptr<T[], ExtmemArrayDeleter<T>>;

template <typename T, typename... Args>
ExtmemUniquePtr<T> makeExtmemUnique(Args&&... args) {
#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    if (testing::consumeExtmemAllocationFailure()) return ExtmemUniquePtr<T>(nullptr);
#endif
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    void* memory = allocateExtmemStrict(sizeof(T));
    if (!memory) return ExtmemUniquePtr<T>(nullptr);
#if OC_ENABLE_STATS
    core::diagnostics::trackExtmemAllocation(memory);
#endif
    return ExtmemUniquePtr<T>(new(memory) T(std::forward<Args>(args)...));
#else
    return ExtmemUniquePtr<T>(new T(std::forward<Args>(args)...));
#endif
}

/**
 * Allocates and copy-constructs one trivial PSRAM object without first
 * value-initializing its complete storage.
 *
 * Large transaction snapshots must not pay for a redundant zero fill before
 * immediately copying every byte from their live source.
 */
template <typename T>
ExtmemUniquePtr<T> makeExtmemUniqueCopy(const T& source) {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_copy_constructible_v<T>);
#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    if (testing::consumeExtmemAllocationFailure()) return ExtmemUniquePtr<T>(nullptr);
#endif
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    void* memory = allocateExtmemStrict(sizeof(T));
    if (!memory) return ExtmemUniquePtr<T>(nullptr);
#if OC_ENABLE_STATS
    core::diagnostics::trackExtmemAllocation(memory);
#endif
    return ExtmemUniquePtr<T>(new(memory) T(source));
#else
    return ExtmemUniquePtr<T>(new T(source));
#endif
}

/**
 * Allocates storage for a trivial object whose complete contents will be
 * overwritten before the first read.
 *
 * Unlike makeExtmemUnique<T>(), this deliberately uses default-initialization
 * and therefore avoids clearing large byte buffers in PSRAM.
 */
template <typename T>
ExtmemUniquePtr<T> makeExtmemUniqueForOverwrite() {
    static_assert(std::is_trivially_default_constructible_v<T>);
    static_assert(std::is_trivially_destructible_v<T>);
#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    if (testing::consumeExtmemAllocationFailure()) return ExtmemUniquePtr<T>(nullptr);
#endif
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    void* memory = allocateExtmemStrict(sizeof(T));
    if (!memory) return ExtmemUniquePtr<T>(nullptr);
#if OC_ENABLE_STATS
    core::diagnostics::trackExtmemAllocation(memory);
#endif
    return ExtmemUniquePtr<T>(new(memory) T);
#else
    return ExtmemUniquePtr<T>(new T);
#endif
}

/**
 * Allocates an exact-length trivial array in PSRAM for a cold transaction.
 * Every element must be overwritten before read; no maximum-capacity buffer
 * is retained when the live payload contains only a handful of entries.
 */
template <typename T>
ExtmemUniqueArray<T> makeExtmemUniqueArrayForOverwrite(std::size_t count) {
    static_assert(std::is_default_constructible_v<T>);
    static_assert(std::is_trivially_destructible_v<T>);
    if (count == 0U || count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        return ExtmemUniqueArray<T>(nullptr);
    }
#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    if (testing::consumeExtmemAllocationFailure()) return ExtmemUniqueArray<T>(nullptr);
#endif
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    void* memory = allocateExtmemStrict(sizeof(T) * count);
    if (!memory) return ExtmemUniqueArray<T>(nullptr);
#if OC_ENABLE_STATS
    core::diagnostics::trackExtmemAllocation(memory);
#endif
    auto* values = static_cast<T*>(memory);
    for (std::size_t index = 0; index < count; ++index) {
        new(values + index) T;
    }
    return ExtmemUniqueArray<T>(values);
#else
    return ExtmemUniqueArray<T>(new T[count]);
#endif
}

}  // namespace core::app
