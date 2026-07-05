#pragma once

#include <cstdint>

#include <config/InputIDs.hpp>

namespace core::state::macro {

static constexpr uint8_t PAGE_COUNT = 16;
static constexpr uint8_t TRACK_COUNT = 16;
static constexpr uint8_t MACRO_COUNT = Config::MACRO_COUNT;
static constexpr uint8_t PAGE_NAME_SIZE = 16;

}  // namespace core::state::macro
