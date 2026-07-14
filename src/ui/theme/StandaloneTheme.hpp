#pragma once

/**
 * @file StandaloneTheme.hpp
 * @brief Standalone mode theme configuration
 */

#include <array>
#include <cstdint>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

namespace standalone::theme {

using namespace oc::ui::lvgl;

// =============================================================================
// Colors
// =============================================================================
namespace color {

// Macro colors (from BaseTheme)
constexpr uint32_t MACRO_1 = base_theme::color::MACRO_1_RED;
constexpr uint32_t MACRO_2 = base_theme::color::MACRO_2_ORANGE;
constexpr uint32_t MACRO_3 = base_theme::color::MACRO_3_YELLOW;
constexpr uint32_t MACRO_4 = base_theme::color::MACRO_4_GREEN;
constexpr uint32_t MACRO_5 = base_theme::color::MACRO_5_CYAN;
constexpr uint32_t MACRO_6 = base_theme::color::MACRO_6_BLUE;
constexpr uint32_t MACRO_7 = base_theme::color::MACRO_7_PURPLE;
constexpr uint32_t MACRO_8 = base_theme::color::MACRO_8_PINK;

// Base colors
constexpr uint32_t BACKGROUND = base_theme::color::BACKGROUND;
constexpr uint32_t INACTIVE = base_theme::color::INACTIVE;
constexpr uint32_t ACTIVE = base_theme::color::ACTIVE;

// Text
constexpr uint32_t TEXT_PRIMARY = base_theme::color::TEXT_PRIMARY;
constexpr uint32_t TEXT_SECONDARY = base_theme::color::TEXT_SECONDARY;

// Knob
constexpr uint32_t KNOB_BACKGROUND = base_theme::color::KNOB_BACKGROUND;
constexpr uint32_t KNOB_VALUE = base_theme::color::KNOB_VALUE;
constexpr uint32_t KNOB_TRACK = base_theme::color::KNOB_TRACK;

// MIDI indicators
constexpr uint32_t MIDI_INACTIVE = 0x404040;
constexpr uint32_t MIDI_IN_ACTIVE = 0xFFCC00;   // Yellow
constexpr uint32_t MIDI_OUT_ACTIVE = 0xFF8800;  // Orange

// Transport
constexpr uint32_t PLAY_ACTIVE = MACRO_5;       // Cyan
constexpr uint32_t PLAY_INACTIVE = INACTIVE;
constexpr uint32_t BEAT_PULSE = 0x0088FF;       // Blue

// Sequencer semantic palette.
// Muted tones for persistent musical UI: use mostly as borders/icons/text,
// with low-opacity fills only for selected or active states.
constexpr uint32_t STEP_STATE = 0x7FA7C7;          // Blue-gray trigger state
constexpr uint32_t STEP_CHANCE = 0xB886A8;         // Muted mauve trigger chance
constexpr uint32_t STEP_PITCH = 0xD3B16E;          // Desaturated amber
constexpr uint32_t STEP_VELOCITY = 0x6EAD9A;       // Soft teal-green
constexpr uint32_t STEP_GATE = 0xB8AE63;           // Muted olive yellow
constexpr uint32_t STEP_NUDGE = 0xBE8172;          // Desaturated coral
constexpr uint32_t STEP_CHORD = 0x9D8BC0;          // Muted lavender harmony layer
constexpr uint32_t STEP_CHORD_MODE = 0xA393C5;     // Harmony source / inheritance
constexpr uint32_t STEP_CHORD_VOICE = 0x8F9CC5;    // Harmony voice count
constexpr uint32_t STEP_CHORD_COLOR = 0xB88EAD;    // Harmony tone color
constexpr uint32_t STEP_CHORD_SHAPE = 0x9B86BD;    // Harmony voicing shape
constexpr uint32_t STEP_CHORD_SPREAD = 0xA2A66E;   // Harmony voice spacing
constexpr uint32_t STEP_CHORD_STRUM = 0x9CA877;    // Harmony voice timing
constexpr uint32_t STEP_CHORD_VELOCITY = STEP_VELOCITY;  // Harmony voice dynamics
constexpr uint32_t STEP_MICRO_SEQUENCE = 0x6FAE99; // Muted teal
constexpr uint32_t STEP_CYCLE_STATE = 0xB99A58;    // Muted ochre
constexpr uint32_t STEP_LENGTH = 0x8BA8BE;         // Muted steel blue
constexpr uint32_t STEP_OFFSET = STEP_NUDGE;       // Shared with temporal displacement
constexpr uint32_t STEP_DIVISION = 0xA8A66B;       // Muted clock olive
constexpr uint32_t STEP_SWING = 0x9CA877;          // Soft desaturated swing olive
constexpr uint32_t STEP_PATTERN_NUDGE = STEP_NUDGE;

// Macro config labels (2 base colors, use opacity for prefix)
constexpr uint32_t MACRO_CH_COLOR = 0xCC7777;    // Muted red
constexpr uint32_t MACRO_CC_COLOR = 0x77CC77;    // Muted green
constexpr uint32_t MACRO_AUTOMATION = 0xCC7777;         // Muted red automation lane
constexpr uint32_t MACRO_AUTOMATION_RECORDING = 0xE06A6A;  // Stronger red recording lane
constexpr uint32_t MACRO_AUTOMATION_MANUAL = 0xD1A35A;  // Muted amber manual override
constexpr uint32_t MACRO_MODULATION = 0x6FAE99;         // Relative modulation / teal
constexpr uint32_t MACRO_PAUSED = 0x8BA8BE;             // Stored but depth-paused
constexpr uint32_t MACRO_SUSPENDED = 0xD1A35A;          // Explicit Resume required
constexpr uint32_t MACRO_CONFLICT = 0xD1A35A;           // Winner/loser warning
constexpr lv_opa_t MACRO_PREFIX_OPA = LV_OPA_60; // Prefix opacity (dim)

using base_theme::color::getMacroColor;

constexpr std::array<uint32_t, 8> TRACK_COLORS = {
    MACRO_1, MACRO_2, MACRO_3, MACRO_4, MACRO_5, MACRO_6, MACRO_7, MACRO_8,
};

constexpr uint32_t trackColor(uint8_t index) {
    return TRACK_COLORS[index % TRACK_COLORS.size()];
}

}  // namespace color

// =============================================================================
// Layout
// =============================================================================
namespace layout {

constexpr int16_t SCREEN_WIDTH = 320;
constexpr int16_t SCREEN_HEIGHT = 240;

constexpr int16_t TOP_BAR_HEIGHT = 20;
constexpr int16_t TRANSPORT_BAR_HEIGHT = 20;
constexpr int16_t CONTEXT_ACTION_STRIP_HEIGHT = 20;

constexpr int16_t PARAMETER_GRID_COLS = 4;
constexpr int16_t PARAMETER_GRID_ROWS = 2;

constexpr int16_t INDICATOR_SIZE = 12;  // Same as plugin-bitwig

constexpr int16_t PAD_SM = 4;
constexpr int16_t PAD_MD = 6;
constexpr int16_t GAP_SM = 4;
constexpr int16_t GAP_MD = 6;

}  // namespace layout

// =============================================================================
// Timing
// =============================================================================
namespace timing {

constexpr uint32_t MIDI_BLINK_MS = 80;
constexpr uint32_t BEAT_PULSE_MS = 100;

}  // namespace timing

}  // namespace standalone::theme
