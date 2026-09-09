#pragma once
#include "../lvgl.h"

// Only the allocator monitor is simulated. The benchmark's actual bounded
// LVGL collector is linked and exercised with its real public frame hooks.
struct lv_mem_monitor_t {
    size_t total_size = 0, free_size = 0, free_biggest_size = 0, max_used = 0;
};
void lv_mem_monitor(lv_mem_monitor_t* monitor);
