#include "LVGLMemory.hpp"

#include <Arduino.h>
#include <lvgl.h>

EXTMEM static uint8_t lvgl_memory_pool[LVGL_MEMORY_POOL_SIZE];

extern "C" {
uint8_t* getLvglMemoryPool(size_t size) {
    (void)size;
    return lvgl_memory_pool;
}
}