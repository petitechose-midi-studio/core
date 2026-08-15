#include "state/interaction/TextKeyboardLayout.hpp"

#include <array>
#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::state::interaction {

namespace {

constexpr std::array<TextKeyboardCell, TEXT_KEYBOARD_CELL_COUNT> CELLS PROGMEM{{
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
    {"SPC", ' ', 3, 9, 1},
}};

FLASHMEM uint8_t clampIndex(uint8_t index) {
    return index < CELLS.size() ? index : TEXT_KEYBOARD_DEFAULT_INDEX;
}

FLASHMEM uint8_t rowStart(uint8_t row) {
    for (uint8_t i = 0; i < CELLS.size(); ++i) {
        if (CELLS[i].row == row) return i;
    }
    return TEXT_KEYBOARD_DEFAULT_INDEX;
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

FLASHMEM const TextKeyboardCell& textKeyboardCellAt(uint8_t index) {
    return CELLS[clampIndex(index)];
}

FLASHMEM char textKeyboardCharacterAt(uint8_t index, bool shiftActive) {
    char character = textKeyboardCellAt(index).character;
    if (shiftActive && character >= 'a' && character <= 'z') {
        character = static_cast<char>(character - 'a' + 'A');
    }
    return character;
}

FLASHMEM uint8_t textKeyboardCellCount() {
    return static_cast<uint8_t>(CELLS.size());
}

FLASHMEM uint8_t textKeyboardMoveColumn(uint8_t currentIndex, int delta) {
    if (delta == 0) return clampIndex(currentIndex);
    const uint8_t current = clampIndex(currentIndex);
    int next = static_cast<int>(current) + delta;
    const int count = static_cast<int>(CELLS.size());
    while (next < 0) next += count;
    next %= count;
    return static_cast<uint8_t>(next);
}

FLASHMEM uint8_t textKeyboardMoveRow(uint8_t currentIndex, int delta) {
    if (delta == 0) return clampIndex(currentIndex);
    const uint8_t current = clampIndex(currentIndex);
    int nextRow = static_cast<int>(CELLS[current].row) + delta;
    while (nextRow < 0) nextRow += TEXT_KEYBOARD_ROW_COUNT;
    nextRow %= TEXT_KEYBOARD_ROW_COUNT;
    return nearestInRow(static_cast<uint8_t>(nextRow), CELLS[current].column);
}

FLASHMEM bool textKeyboardAppend(
    char* buffer,
    size_t capacity,
    char character
) {
    if (!buffer || capacity < 2U || character == '\0') return false;
    const size_t length = std::strlen(buffer);
    if (length >= capacity - 1U) return false;
    buffer[length] = character;
    buffer[length + 1U] = '\0';
    return true;
}

FLASHMEM bool textKeyboardBackspace(char* buffer) {
    if (!buffer) return false;
    const size_t length = std::strlen(buffer);
    if (length == 0U) return false;
    buffer[length - 1U] = '\0';
    return true;
}

FLASHMEM bool textKeyboardClear(char* buffer) {
    if (!buffer || buffer[0] == '\0') return false;
    buffer[0] = '\0';
    return true;
}

}  // namespace core::state::interaction
