#pragma once

/**
 * @file MacroValueHandler.hpp
 * @brief Handles encoder input for macro controls
 *
 * Binds encoders to macro state and sends MIDI CC output.
 * Uses page configuration for CC/channel mapping.
 */

#include <cstdint>
#include <array>

#include <oc/api/EncoderAPI.hpp>
#include <oc/api/MidiAPI.hpp>
#include <oc/api/ButtonAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include <config/InputIDs.hpp>
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/MacroEditState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

namespace core::handler {

/**
 * @brief Encoder input handler for standalone macros
 *
 * Handles encoder turns → updates state → sends MIDI CC.
 * Uses page configuration for CC/channel mapping.
 * Bindings are scoped to the provided LVGL element.
 */
class MacroValueHandler {
public:
    struct StateRefs {
        core::state::macro::MacroUiState& macroUi;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::MacroEditState& macroEdit;
    };

    MacroValueHandler(StateRefs state,
                      MacroPerformanceDomainServices services,
                      oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                      oc::api::EncoderAPI& encoders,
                      oc::api::ButtonAPI& buttons,
                      oc::api::MidiAPI& midi,
                      oc::type::ScopeID scopeId);

    ~MacroValueHandler() = default;

    MacroValueHandler(const MacroValueHandler&) = delete;
    MacroValueHandler& operator=(const MacroValueHandler&) = delete;

private:
    void setupBindings();
    void handleValueChange(uint8_t index, float value);
    bool shouldHandleTurns() const;
    bool shouldHandleAutomationRecordPress() const;
    bool shouldHandleAutomationRestorePress() const;
    bool shouldIgnorePostRecordTurn(uint8_t index, uint32_t nowMs) const;
    bool shouldStartAutomationRecording(uint8_t index) const;
    bool ensureActiveSlot(uint8_t index);
    void restoreAutomation(uint8_t index);
    void handleConfigChange(uint8_t index, float value);

    core::state::macro::MacroUiState& macro_ui_;
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::MacroEditState& macro_edit_;
    MacroPerformanceDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::api::MidiAPI& midi_;
    oc::type::ScopeID scope_id_ = 0;
    std::array<bool, core::state::macro::MACRO_COUNT> macro_button_held_{};
    std::array<uint32_t, core::state::macro::MACRO_COUNT> macro_button_pressed_at_ms_{};
    std::array<uint32_t, core::state::macro::MACRO_COUNT> post_record_guard_until_ms_{};
};

}  // namespace core::handler
