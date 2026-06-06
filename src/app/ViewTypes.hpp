#pragma once

#include <cstdint>

namespace core::ui {

enum class ViewType : uint8_t {
    MACRO = 0,
    SEQUENCER,
    PROJECT,
    DEVICE_SETTINGS,
    COUNT
};

}  // namespace core::ui
