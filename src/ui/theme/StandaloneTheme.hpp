#pragma once

#include <cstdint>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

namespace standalone::theme {

using namespace oc::ui::lvgl;

// =============================================================================
// Colors
// =============================================================================
namespace Color {

// Macro colors (from BaseTheme)
constexpr uint32_t MACRO_1 = BaseTheme::Color::MACRO_1_RED;
constexpr uint32_t MACRO_2 = BaseTheme::Color::MACRO_2_ORANGE;
constexpr uint32_t MACRO_3 = BaseTheme::Color::MACRO_3_YELLOW;
constexpr uint32_t MACRO_4 = BaseTheme::Color::MACRO_4_GREEN;
constexpr uint32_t MACRO_5 = BaseTheme::Color::MACRO_5_CYAN;
constexpr uint32_t MACRO_6 = BaseTheme::Color::MACRO_6_BLUE;
constexpr uint32_t MACRO_7 = BaseTheme::Color::MACRO_7_PURPLE;
constexpr uint32_t MACRO_8 = BaseTheme::Color::MACRO_8_PINK;

// Base colors
constexpr uint32_t BACKGROUND = BaseTheme::Color::BACKGROUND;
constexpr uint32_t INACTIVE = BaseTheme::Color::INACTIVE;
constexpr uint32_t ACTIVE = BaseTheme::Color::ACTIVE;

// Text
constexpr uint32_t TEXT_PRIMARY = BaseTheme::Color::TEXT_PRIMARY;
constexpr uint32_t TEXT_SECONDARY = BaseTheme::Color::TEXT_SECONDARY;

// Knob
constexpr uint32_t KNOB_BACKGROUND = BaseTheme::Color::KNOB_BACKGROUND;
constexpr uint32_t KNOB_VALUE = BaseTheme::Color::KNOB_VALUE;
constexpr uint32_t KNOB_TRACK = BaseTheme::Color::KNOB_TRACK;

// MIDI indicators
constexpr uint32_t MIDI_INACTIVE = 0x404040;
constexpr uint32_t MIDI_IN_ACTIVE = 0xFFCC00;   // Yellow
constexpr uint32_t MIDI_OUT_ACTIVE = 0xFF8800;  // Orange

// Transport
constexpr uint32_t PLAY_ACTIVE = MACRO_5;       // Cyan
constexpr uint32_t PLAY_INACTIVE = INACTIVE;
constexpr uint32_t BEAT_PULSE = 0x0088FF;       // Blue

// Macro config labels (2 base colors, use opacity for prefix)
constexpr uint32_t MACRO_CH_COLOR = 0xCC7777;    // Muted red
constexpr uint32_t MACRO_CC_COLOR = 0x77CC77;    // Muted green
constexpr lv_opa_t MACRO_PREFIX_OPA = LV_OPA_60; // Prefix opacity (dim)

using BaseTheme::Color::getMacroColor;

}  // namespace Color

// =============================================================================
// Layout
// =============================================================================
namespace Layout {

constexpr int16_t SCREEN_WIDTH = 320;
constexpr int16_t SCREEN_HEIGHT = 240;

constexpr int16_t TOP_BAR_HEIGHT = 20;
constexpr int16_t TRANSPORT_BAR_HEIGHT = 20;

constexpr int16_t PARAMETER_GRID_COLS = 4;
constexpr int16_t PARAMETER_GRID_ROWS = 2;

constexpr int16_t INDICATOR_SIZE = 12;  // Same as plugin-bitwig

constexpr int16_t PAD_SM = 4;
constexpr int16_t PAD_MD = 6;
constexpr int16_t GAP_SM = 4;
constexpr int16_t GAP_MD = 6;

}  // namespace Layout

// =============================================================================
// Timing
// =============================================================================
namespace Timing {

constexpr uint32_t MIDI_BLINK_MS = 80;
constexpr uint32_t BEAT_PULSE_MS = 100;

}  // namespace Timing

}  // namespace standalone::theme
