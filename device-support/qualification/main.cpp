#include <cstddef>
#include <cstdint>

#include <lv_conf.h>
#include <ms/device_support/v1/Buffers.hpp>
#include <ms/device_support/v1/Hardware.hpp>
#include <ms/device_support/v1/InputConfig.hpp>
#include <ms/device_support/v1/LvglMemory.hpp>
#include <ms/device_support/v1/Version.hpp>

namespace device = ms::device_support::v1;

void setup() {
    using Provider = std::uint8_t* (*)(std::size_t);
    Provider volatile provider = &getLvglMemoryPool;
    volatile std::uint8_t* pool =
        provider(device::LVGL_MEMORY_POOL_SIZE_BYTES);

    pool[0] = 0;
    pool[device::LVGL_MEMORY_POOL_SIZE_BYTES - 1] = 0;
    static_cast<volatile std::uint16_t*>(device::buffers::framebuffer)[0] = 0;
    static_cast<volatile std::uint8_t*>(device::buffers::diff1)[0] = 0;
    static_cast<volatile std::uint8_t*>(device::buffers::diff2)[0] = 0;
    static_cast<volatile std::uint16_t*>(device::buffers::lvgl)[0] = 0;
}

void loop() {}
