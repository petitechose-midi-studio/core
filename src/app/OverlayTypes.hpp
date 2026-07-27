#pragma once

#include <cstdint>

namespace core::ui {

enum class OverlayType : uint8_t {
    NONE = 0,
    MACRO_EDIT,
    MACRO_EDIT_SELECTOR,
    MACRO_AUTOMATION,
    VIEW_SELECTOR,
    SEQ_STEP_EDIT,
    SEQ_STEP_PRESET,
    SEQ_CC_LANE,
    DEVICE_SETTINGS_SELECTOR,
    SEQUENCER_SETTINGS,
    SEQUENCER_SETTINGS_SELECTOR,
    PATTERN_PITCH_SETTINGS,
    PATTERN_PITCH_SETTINGS_SELECTOR,
    SEQ_PATTERN_EDIT,
    SEQ_TRACK_EDIT,
    COUNT
};

}  // namespace core::ui
