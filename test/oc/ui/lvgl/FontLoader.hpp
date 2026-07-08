#pragma once

// Minimal font entry shape for native tests that include standalone UI props.

#include <cstddef>
#include <cstdint>

namespace oc::ui::lvgl::font {

struct Entry {
    const uint8_t* data = nullptr;
    size_t size = 0;
    const char* name = nullptr;
    bool external = false;
};

}  // namespace oc::ui::lvgl::font
