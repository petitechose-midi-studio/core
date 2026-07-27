#pragma once

#include <cstdint>

namespace core::state::project {

struct ProjectNameKeyboardCell {
    const char* label = "";
    char character = '\0';
    uint8_t row = 0;
    uint8_t column = 0;
    uint8_t columnSpan = 1;
};

inline constexpr uint8_t PROJECT_NAME_KEYBOARD_COLUMN_COUNT = 10;
inline constexpr uint8_t PROJECT_NAME_KEYBOARD_ROW_COUNT = 4;
inline constexpr uint8_t PROJECT_NAME_KEYBOARD_CELL_COUNT = 39;
inline constexpr uint8_t PROJECT_NAME_KEYBOARD_DEFAULT_INDEX = 10;  // q

const ProjectNameKeyboardCell& projectNameKeyboardCellAt(uint8_t index);
uint8_t projectNameKeyboardCellCount();
uint8_t projectNameKeyboardMoveColumn(uint8_t currentIndex, int delta);
uint8_t projectNameKeyboardMoveRow(uint8_t currentIndex, int delta);

}  // namespace core::state::project
