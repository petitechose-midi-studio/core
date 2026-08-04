#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef MS_DEVICE_SUPPORT_LVGL_MEMORY_POOL_SIZE_BYTES
#define MS_DEVICE_SUPPORT_LVGL_MEMORY_POOL_SIZE_BYTES 4096000U
#endif

#ifdef __cplusplus
namespace ms::device_support::v1 {

inline constexpr size_t LVGL_MEMORY_POOL_SIZE_BYTES =
    MS_DEVICE_SUPPORT_LVGL_MEMORY_POOL_SIZE_BYTES;

}  // namespace ms::device_support::v1

extern "C" {
#endif

uint8_t* getLvglMemoryPool(size_t size);

#ifdef __cplusplus
}
#endif
