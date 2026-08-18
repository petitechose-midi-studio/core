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

// Controller neutrals. Keep these explicit instead of deriving surfaces from
// text colors: the 320 x 240 display needs stable luminance steps more than
// translucent decoration.
constexpr uint32_t BACKGROUND = 0x000000;
constexpr uint32_t SURFACE_IDLE = 0x0C0E12;
constexpr uint32_t SURFACE_RAISED = 0x1B1F25;
constexpr uint32_t BORDER_SUBTLE = 0x353A42;
constexpr uint32_t BORDER_STRONG = 0x565B63;
constexpr uint32_t INACTIVE = 0x33363C;
constexpr uint32_t ACTIVE = 0xFFA51A;

// Text hierarchy. TEXT_SECONDARY must remain visibly distinct from primary;
// the BaseTheme value is intentionally not reused here because it is almost
// white and flattens compact editor layouts.
constexpr uint32_t TEXT_PRIMARY = 0xF3F4F6;
constexpr uint32_t TEXT_SECONDARY = 0xA1A5AB;
constexpr uint32_t TEXT_DISABLED = 0x565A61;

// Knob
constexpr uint32_t KNOB_BACKGROUND = base_theme::color::KNOB_BACKGROUND;
constexpr uint32_t KNOB_VALUE = base_theme::color::KNOB_VALUE;

// MIDI indicators
constexpr uint32_t MIDI_INACTIVE = 0x404040;
constexpr uint32_t MIDI_IN_ACTIVE = 0xFFCC00;   // Yellow

// Transport
constexpr uint32_t PLAY_ACTIVE = 0x49E4B0;      // Live mint
constexpr uint32_t PLAY_INACTIVE = INACTIVE;
constexpr uint32_t BEAT_PULSE = 0x0088FF;       // Blue

// Small semantic accents. Keep large surfaces neutral and use these colors on
// glyphs, notes and curves so the 320 x 240 display remains calm and legible.
constexpr uint32_t ROUTING = 0x39D8D0;
constexpr uint32_t RECORD_ACTIVE = 0xFF1748;
constexpr uint32_t MODULATION = 0xB875F2;

// Sequencer property families. Related properties intentionally share one
// authority instead of drifting through near-identical shades.
constexpr uint32_t STEP_STATE = 0x5BD21D;
constexpr uint32_t STEP_CHANCE = 0xE04BFF;
constexpr uint32_t STEP_PITCH = 0x22B8FF;
constexpr uint32_t STEP_VELOCITY = 0x50DF80;
constexpr uint32_t STEP_GATE = 0xFFD23F;
constexpr uint32_t STEP_NUDGE = 0xFF684F;
constexpr uint32_t STEP_CHORD = 0x9B6BFF;
constexpr uint32_t STEP_CHORD_MODE = STEP_CHORD;
constexpr uint32_t STEP_CHORD_FORMULA = STEP_CHORD;
constexpr uint32_t STEP_CHORD_SHAPE = STEP_CHORD;
constexpr uint32_t STEP_CHORD_INVERSION = STEP_CHORD;
constexpr uint32_t STEP_CHORD_VOICING = STEP_CHORD;
constexpr uint32_t STEP_CHORD_STRUM = STEP_CHORD;
constexpr uint32_t STEP_CHORD_VELOCITY = STEP_VELOCITY;
constexpr uint32_t STEP_MICRO_SEQUENCE = ROUTING;
constexpr uint32_t STEP_CYCLE_STATE = 0x4F8CFF;
constexpr uint32_t STEP_LENGTH = STEP_GATE;
constexpr uint32_t STEP_OFFSET = STEP_NUDGE;
constexpr uint32_t STEP_DIVISION = STEP_GATE;
constexpr uint32_t STEP_SWING = STEP_NUDGE;
constexpr uint32_t STEP_PATTERN_NUDGE = STEP_NUDGE;

// Macro domains reuse the same semantic authorities wherever they appear.
constexpr uint32_t MACRO_CH_COLOR = ROUTING;
constexpr uint32_t MACRO_CC_COLOR = ROUTING;
constexpr uint32_t MACRO_AUTOMATION = RECORD_ACTIVE;
constexpr uint32_t MACRO_AUTOMATION_RECORDING = RECORD_ACTIVE;
constexpr uint32_t MACRO_AUTOMATION_MANUAL = 0xD1A35A;  // Muted amber manual override
constexpr uint32_t MACRO_MODULATION = MODULATION;
constexpr uint32_t MACRO_PAUSED = 0x8BA8BE;             // Stored but depth-paused
constexpr uint32_t MACRO_SUSPENDED = 0xD1A35A;          // Suspended/waiting attention
constexpr uint32_t MACRO_CONFLICT = 0xD1A35A;           // Winner/loser warning
constexpr lv_opa_t MACRO_PREFIX_OPA = LV_OPA_60; // Prefix opacity (dim)

// Controller-wide interaction roles. Domain colors remain available for
// musical identity and data encoding, but must not carry these meanings.
constexpr uint32_t FOCUS_EDIT = ACTIVE;
constexpr uint32_t LIVE_TIME = PLAY_ACTIVE;
constexpr uint32_t CONTENT_ACTIVE = TEXT_PRIMARY;
constexpr uint32_t POSITIVE = MACRO_4;
constexpr uint32_t WARNING = MACRO_CONFLICT;
constexpr uint32_t DESTRUCTIVE = RECORD_ACTIVE;
constexpr uint32_t SECONDARY = TEXT_SECONDARY;
constexpr uint32_t DISABLED = TEXT_DISABLED;

using base_theme::color::getMacroColor;

// Musical identity palette. It follows the familiar Red -> Pink order used
// by persisted color indices, but is tuned independently from macro feedback
// colors so dense grids stay vivid without looking fluorescent.
constexpr std::array<uint32_t, 8> TRACK_COLORS = {
    0xF51B4B, 0xF08A24, 0xD6CF35, 0x5BD21D,
    0x28CDB4, 0x22A9E8, 0xA45CDD, 0xD936B4,
};

constexpr uint32_t trackColor(uint8_t index) {
    return TRACK_COLORS[index % TRACK_COLORS.size()];
}

}  // namespace color

// =============================================================================
// Layout
// =============================================================================
namespace layout {

constexpr int16_t TRANSPORT_BAR_HEIGHT = 20;
constexpr int16_t CONTEXT_ACTION_STRIP_HEIGHT = 20;

constexpr int16_t INDICATOR_SIZE = 12;  // Same as plugin-bitwig

constexpr int16_t PAD_SM = 4;
constexpr int16_t PAD_MD = 6;
constexpr int16_t GAP_SM = 4;
constexpr int16_t GAP_MD = 6;

// Stable geometry for compact interactive cards. Specialized panels, badges
// and data markers keep their own geometry.
constexpr int16_t INTERACTIVE_SURFACE_RADIUS = 3;
constexpr int16_t INTERACTIVE_SURFACE_BORDER_WIDTH = 1;

}  // namespace layout

// =============================================================================
// Timing
// =============================================================================
namespace timing {

constexpr uint32_t BEAT_PULSE_MS = 100;

}  // namespace timing

}  // namespace standalone::theme
