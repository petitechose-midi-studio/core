#pragma once

#include <array>
#include <cstdint>

namespace core::ui::sequencer::grid::palette {

// Data-encoding palette for chromatic note identity. These colors are not
// interaction roles: focus, live time and status remain owned by the product
// theme.
inline constexpr std::array<uint32_t, 12> CHROMATIC_NOTES = {
    0xF4F1DE,
    0xEAB69E,
    0xE07A5F,
    0xAA675E,
    0x73535C,
    0x3D405B,
    0x5F797A,
    0x81B29A,
    0xBABF94,
    0xF2CC8F,
    0xF3D8A9,
    0xF3E5C4,
};

inline constexpr uint32_t SELECTION_EMPTY = CHROMATIC_NOTES[7];
inline constexpr uint32_t SELECTION_GHOST = CHROMATIC_NOTES[9];
inline constexpr uint32_t SELECTION_OVERWRITE = CHROMATIC_NOTES[1];
inline constexpr uint32_t SELECTION_BLOCKED = CHROMATIC_NOTES[2];
inline constexpr uint32_t OUT_OF_SCALE = 0xFF6B6B;

}  // namespace core::ui::sequencer::grid::palette
