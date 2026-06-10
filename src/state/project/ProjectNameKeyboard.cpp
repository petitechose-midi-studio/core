#include "state/project/ProjectNameKeyboard.hpp"

#include <array>

#include <config/PlatformCompat.hpp>

namespace core::state::project {

namespace {

constexpr std::array<ProjectNameKeyboardCell, PROJECT_NAME_KEYBOARD_CELL_COUNT> CELLS{{
    {"1", '1', 0, 0, 1},
    {"2", '2', 0, 1, 1},
    {"3", '3', 0, 2, 1},
    {"4", '4', 0, 3, 1},
    {"5", '5', 0, 4, 1},
    {"6", '6', 0, 5, 1},
    {"7", '7', 0, 6, 1},
    {"8", '8', 0, 7, 1},
    {"9", '9', 0, 8, 1},
    {"0", '0', 0, 9, 1},

    {"q", 'q', 1, 0, 1},
    {"w", 'w', 1, 1, 1},
    {"e", 'e', 1, 2, 1},
    {"r", 'r', 1, 3, 1},
    {"t", 't', 1, 4, 1},
    {"y", 'y', 1, 5, 1},
    {"u", 'u', 1, 6, 1},
    {"i", 'i', 1, 7, 1},
    {"o", 'o', 1, 8, 1},
    {"p", 'p', 1, 9, 1},

    {"a", 'a', 2, 0, 1},
    {"s", 's', 2, 1, 1},
    {"d", 'd', 2, 2, 1},
    {"f", 'f', 2, 3, 1},
    {"g", 'g', 2, 4, 1},
    {"h", 'h', 2, 5, 1},
    {"j", 'j', 2, 6, 1},
    {"k", 'k', 2, 7, 1},
    {"l", 'l', 2, 8, 1},

    {"z", 'z', 3, 0, 1},
    {"x", 'x', 3, 1, 1},
    {"c", 'c', 3, 2, 1},
    {"v", 'v', 3, 3, 1},
    {"b", 'b', 3, 4, 1},
    {"n", 'n', 3, 5, 1},
    {"m", 'm', 3, 6, 1},
    {"-", '-', 3, 7, 1},
    {".", '.', 3, 8, 1},
}};

FLASHMEM uint8_t clampIndex(uint8_t index) {
    return index < CELLS.size() ? index : PROJECT_NAME_KEYBOARD_DEFAULT_INDEX;
}

FLASHMEM uint8_t rowStart(uint8_t row) {
    for (uint8_t i = 0; i < CELLS.size(); ++i) {
        if (CELLS[i].row == row) return i;
    }
    return PROJECT_NAME_KEYBOARD_DEFAULT_INDEX;
}

FLASHMEM uint8_t nearestInRow(uint8_t row, uint8_t targetColumn) {
    uint8_t best = rowStart(row);
    uint8_t bestDistance = 255;
    for (uint8_t i = 0; i < CELLS.size(); ++i) {
        const auto& cell = CELLS[i];
        if (cell.row != row) continue;
        const uint8_t center = static_cast<uint8_t>(cell.column + (cell.columnSpan - 1U) / 2U);
        const uint8_t distance = center > targetColumn ? center - targetColumn : targetColumn - center;
        if (distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

}  // namespace

FLASHMEM const ProjectNameKeyboardCell& projectNameKeyboardCellAt(uint8_t index) {
    return CELLS[clampIndex(index)];
}

FLASHMEM uint8_t projectNameKeyboardCellCount() {
    return static_cast<uint8_t>(CELLS.size());
}

FLASHMEM uint8_t projectNameKeyboardMoveColumn(uint8_t currentIndex, int delta) {
    if (delta == 0) return clampIndex(currentIndex);
    const uint8_t current = clampIndex(currentIndex);
    int next = static_cast<int>(current) + delta;
    const int count = static_cast<int>(CELLS.size());
    while (next < 0) next += count;
    next %= count;
    return static_cast<uint8_t>(next);
}

FLASHMEM uint8_t projectNameKeyboardMoveRow(uint8_t currentIndex, int delta) {
    if (delta == 0) return clampIndex(currentIndex);
    const uint8_t current = clampIndex(currentIndex);
    int nextRow = static_cast<int>(CELLS[current].row) + delta;
    while (nextRow < 0) nextRow += PROJECT_NAME_KEYBOARD_ROW_COUNT;
    nextRow %= PROJECT_NAME_KEYBOARD_ROW_COUNT;
    return nearestInRow(static_cast<uint8_t>(nextRow), CELLS[current].column);
}

}  // namespace core::state::project
