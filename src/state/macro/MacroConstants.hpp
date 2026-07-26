#pragma once

#include <cstdint>

#include <config/InputIDs.hpp>

namespace core::state::macro {

static constexpr uint8_t PAGE_COUNT = 16;
static constexpr uint8_t TRACK_COUNT = 16;
static constexpr uint8_t MACRO_COUNT = Config::MACRO_COUNT;

constexpr uint8_t defaultMacroCc(uint8_t pageIndex, uint8_t macroIndex) {
    return static_cast<uint8_t>(
        static_cast<uint16_t>(pageIndex) * MACRO_COUNT + macroIndex
    );
}
static constexpr uint8_t PAGE_NAME_SIZE = 16;

}  // namespace core::state::macro
