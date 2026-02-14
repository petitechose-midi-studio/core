#pragma once

/**
 * @file ViewTypes.hpp
 * @brief Top-level view types for standalone mode
 */

#include <cstdint>

namespace core::ui {

enum class ViewType : uint8_t {
    MACRO = 0,
    SEQUENCER,
    COUNT
};

}  // namespace core::ui
