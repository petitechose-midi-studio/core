#pragma once

#include <memory>
#include <new>
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

}  // namespace core::app
