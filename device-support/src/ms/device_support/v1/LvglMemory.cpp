#include <ms/device_support/v1/LvglMemory.hpp>

#include <cstddef>
#include <cstdint>

#include <config/PlatformCompat.hpp>

namespace {

EXTMEM std::uint8_t lvgl_memory_pool[
    ms::device_support::v1::LVGL_MEMORY_POOL_SIZE_BYTES];

static_assert(
    sizeof(lvgl_memory_pool) ==
    ms::device_support::v1::LVGL_MEMORY_POOL_SIZE_BYTES);

}  // namespace

extern "C" std::uint8_t* getLvglMemoryPool(std::size_t size) {
    (void)size;
    return lvgl_memory_pool;
}
