#pragma once

/**
 * @file LvglMemory.hpp
 * @brief LVGL memory allocation configuration
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t* getLvglMemoryPool(size_t size);

#ifdef __cplusplus
}
#endif
