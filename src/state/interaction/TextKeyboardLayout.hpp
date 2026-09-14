#pragma once

#include <cstddef>
#include <cstdint>

namespace core::state::interaction {

struct TextKeyboardCell {
    const char* label = "";
    char character = '\0';
    uint8_t row = 0;
    uint8_t column = 0;
    uint8_t columnSpan = 1;
};

inline constexpr uint8_t TEXT_KEYBOARD_COLUMN_COUNT = 10;
inline constexpr uint8_t TEXT_KEYBOARD_ROW_COUNT = 4;
inline constexpr uint8_t TEXT_KEYBOARD_CELL_COUNT = 39;
inline constexpr uint8_t TEXT_KEYBOARD_DEFAULT_INDEX = 10;  // q

const TextKeyboardCell& textKeyboardCellAt(uint8_t index);
char textKeyboardCharacterAt(uint8_t index, bool shiftActive);
uint8_t textKeyboardCellCount();
uint8_t textKeyboardMoveColumn(uint8_t currentIndex, int delta);
uint8_t textKeyboardMoveRow(uint8_t currentIndex, int delta);
bool textKeyboardAppend(
    char* buffer,
    size_t capacity,
    char character
);
bool textKeyboardBackspace(char* buffer);
bool textKeyboardClear(char* buffer);

/** RAW OPT positions shared by Project, Track and Drum Lane name editors.
 * Positive motion moves up; fractional rows survive until a full row is reached.
 * Reset together with the encoder position. The owner notifies only on movement
 * and retains responsibility for text, validation, publication and closing.
 */
struct TextKeyboardRowInput {
    float position = 0.0f;
    float remainder = 0.0f;

    bool move(uint8_t& keyIndex, float rawPosition);
    bool operator==(const TextKeyboardRowInput& other) const {
        return position == other.position && remainder == other.remainder;
    }
};

static_assert(sizeof(TextKeyboardRowInput) == 2U * sizeof(float));

}  // namespace core::state::interaction
