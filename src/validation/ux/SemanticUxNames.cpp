#include "validation/ux/SemanticUxNames.hpp"

#include <config/PlatformCompat.hpp>

#include "config/InputIDs.hpp"

namespace core::validation::ux {

FLASHMEM const char* buttonName(oc::type::ButtonID id) {
    switch (static_cast<Config::ButtonID>(id)) {
        case Config::ButtonID::LEFT_TOP:
            return "LEFT_TOP";
        case Config::ButtonID::LEFT_CENTER:
            return "LEFT_CENTER";
        case Config::ButtonID::LEFT_BOTTOM:
            return "LEFT_BOTTOM";
        case Config::ButtonID::BOTTOM_LEFT:
            return "BOTTOM_LEFT";
        case Config::ButtonID::BOTTOM_CENTER:
            return "BOTTOM_CENTER";
        case Config::ButtonID::BOTTOM_RIGHT:
            return "BOTTOM_RIGHT";
        case Config::ButtonID::MACRO_1:
            return "MACRO_1";
        case Config::ButtonID::MACRO_2:
            return "MACRO_2";
        case Config::ButtonID::MACRO_3:
            return "MACRO_3";
        case Config::ButtonID::MACRO_4:
            return "MACRO_4";
        case Config::ButtonID::MACRO_5:
            return "MACRO_5";
        case Config::ButtonID::MACRO_6:
            return "MACRO_6";
        case Config::ButtonID::MACRO_7:
            return "MACRO_7";
        case Config::ButtonID::MACRO_8:
            return "MACRO_8";
        case Config::ButtonID::NAV:
            return "NAV";
        default:
            return "UNKNOWN_BUTTON";
    }
}

FLASHMEM const char* encoderName(oc::type::EncoderID id) {
    switch (static_cast<Config::EncoderID>(id)) {
        case Config::EncoderID::MACRO_1:
            return "MACRO_1";
        case Config::EncoderID::MACRO_2:
            return "MACRO_2";
        case Config::EncoderID::MACRO_3:
            return "MACRO_3";
        case Config::EncoderID::MACRO_4:
            return "MACRO_4";
        case Config::EncoderID::MACRO_5:
            return "MACRO_5";
        case Config::EncoderID::MACRO_6:
            return "MACRO_6";
        case Config::EncoderID::MACRO_7:
            return "MACRO_7";
        case Config::EncoderID::MACRO_8:
            return "MACRO_8";
        case Config::EncoderID::NAV:
            return "NAV";
        case Config::EncoderID::OPT:
            return "OPT";
        default:
            return "UNKNOWN_ENCODER";
    }
}

FLASHMEM const char* buttonGestureName(oc::core::input::ButtonBindingType type) {
    switch (type) {
        case oc::core::input::ButtonBindingType::PRESS:
            return "press";
        case oc::core::input::ButtonBindingType::RELEASE:
            return "release";
        case oc::core::input::ButtonBindingType::LONG_PRESS:
            return "long_press";
        case oc::core::input::ButtonBindingType::DOUBLE_TAP:
            return "double_tap";
        case oc::core::input::ButtonBindingType::COMBO:
            return "combo";
        default:
            return "unknown_button_gesture";
    }
}

FLASHMEM const char* encoderGestureName(oc::core::input::EncoderBindingType type) {
    switch (type) {
        case oc::core::input::EncoderBindingType::TURN:
            return "turn";
        case oc::core::input::EncoderBindingType::TURN_WHILE_PRESSED:
            return "turn_while_pressed";
        default:
            return "unknown_encoder_gesture";
    }
}

FLASHMEM const char* viewName(core::ui::ViewType view) {
    switch (view) {
        case core::ui::ViewType::MACRO:
            return "macro";
        case core::ui::ViewType::SEQUENCER:
            return "sequencer";
        case core::ui::ViewType::PROJECT:
            return "project";
        case core::ui::ViewType::DEVICE_SETTINGS:
            return "device_settings";
        default:
            return "unknown_view";
    }
}

FLASHMEM const char* overlayName(core::ui::OverlayType overlay) {
    switch (overlay) {
        case core::ui::OverlayType::NONE:
            return "none";
        case core::ui::OverlayType::PAGE_SELECTOR:
            return "page_selector";
        case core::ui::OverlayType::MACRO_EDIT:
            return "macro_edit";
        case core::ui::OverlayType::MACRO_EDIT_SELECTOR:
            return "macro_edit_selector";
        case core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR:
            return "macro_edit_macro_selector";
        case core::ui::OverlayType::MACRO_AUTOMATION:
            return "macro_automation";
        case core::ui::OverlayType::VIEW_SELECTOR:
            return "view_selector";
        case core::ui::OverlayType::SEQ_STEP_EDIT:
            return "seq_step_edit";
        case core::ui::OverlayType::SEQ_STEP_PRESET:
            return "seq_step_preset";
        case core::ui::OverlayType::SEQ_CC_LANE:
            return "seq_cc_lane";
        case core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR:
            return "device_settings_selector";
        case core::ui::OverlayType::SEQUENCER_SETTINGS:
            return "sequencer_settings";
        case core::ui::OverlayType::SEQUENCER_SETTINGS_SELECTOR:
            return "sequencer_settings_selector";
        case core::ui::OverlayType::PATTERN_PITCH_SETTINGS:
            return "pattern_pitch_settings";
        case core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR:
            return "pattern_pitch_settings_selector";
        case core::ui::OverlayType::DATA_MANAGER:
            return "data_manager";
        case core::ui::OverlayType::DATA_MANAGER_DIALOG:
            return "data_manager_dialog";
        default:
            return "unknown_overlay";
    }
}

}  // namespace core::validation::ux
