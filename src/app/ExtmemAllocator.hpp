#pragma once

#include <memory>
#include <new>
#include <limits>
#include <type_traits>
#include <utility>

#include <config/PlatformCompat.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

namespace core::app {

template <typename T>
struct ExtmemDeleter {
    void operator()(T* ptr) const noexcept {
        if (!ptr) return;
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
        ptr->~T();
        extmem_free(ptr);
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
        extmem_free(ptr);
#else
        delete[] ptr;
#endif
    }
};

template <typename T>
using ExtmemUniqueArray = std::unique_ptr<T[], ExtmemArrayDeleter<T>>;

template <typename T, typename... Args>
ExtmemUniquePtr<T> makeExtmemUnique(Args&&... args) {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    void* memory = extmem_malloc(sizeof(T));
    if (!memory) return ExtmemUniquePtr<T>(nullptr);
    return ExtmemUniquePtr<T>(new(memory) T(std::forward<Args>(args)...));
#else
    return ExtmemUniquePtr<T>(new T(std::forward<Args>(args)...));
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
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    void* memory = extmem_malloc(sizeof(T));
    if (!memory) return ExtmemUniquePtr<T>(nullptr);
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
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    void* memory = extmem_malloc(sizeof(T) * count);
    if (!memory) return ExtmemUniqueArray<T>(nullptr);
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
