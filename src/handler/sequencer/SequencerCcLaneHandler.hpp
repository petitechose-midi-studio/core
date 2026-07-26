#pragma once

#include <array>
#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/sequencer/SequencerCcLaneWorkflow.hpp"

namespace core::handler {

class SequencerPropertySelectorHandler;

/** Hardware bindings for the CC-lane semantic workflow. */
class SequencerCcLaneHandler {
public:
    using NowProvider = uint32_t (*)();

    SequencerCcLaneHandler(
        core::state::sequencer::SequencerState& sequencer,
        SequencerCcLaneWorkflow& workflow,
        SequencerPropertySelectorHandler& propertySelector,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID viewScope,
        oc::type::ScopeID overlayScope,
        NowProvider nowProvider
    );

    void update(uint32_t nowMs);

private:
    void setupBindings();
    [[nodiscard]] bool mainGridOwnsInput() const;
    [[nodiscard]] bool ccOverlayOwnsInput() const;
    void syncOverlayVisibility();
    void syncOptEncoderContract();
    void syncMacroEncoderContract();
    void updateMacroButtonGestures(uint32_t nowMs);
    void updateNavButtonGesture(uint32_t nowMs);
    void beginNavButtonTracking(uint32_t nowMs);
    void resetNavButtonTracking();
    void beginMacroButtonTracking(uint8_t indexInWindow, uint32_t nowMs);
    bool configureTransitionEncoder(uint8_t indexInWindow);
    void invalidateMacroEncoderContract();
    void configureDirectionalOpt();
    void recenterDirectionalOpt();
    void onNavTurn(float delta);
    void onOptTurn(float normalized);
    void onNavRelease();
    void executeNavTap();
    void onMacroTurn(uint8_t indexInWindow, float normalized);
    void onMacroRelease(uint8_t indexInWindow);
    void onMacroLongPress(uint8_t indexInWindow);
    void onActionPress(core::state::sequencer::SequencerCcLaneActionSlot slot);
    void onActionRelease(core::state::sequencer::SequencerCcLaneActionSlot slot);
    void back();
    void openPropertyGrammar();
    [[nodiscard]] uint32_t now() const;

    core::state::sequencer::SequencerState& sequencer_;
    SequencerCcLaneWorkflow& workflow_;
    SequencerPropertySelectorHandler& property_selector_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID view_scope_ = 0;
    oc::type::ScopeID overlay_scope_ = 0;
    NowProvider now_provider_ = nullptr;
    bool opt_directional_configured_ = false;
    bool macro_encoders_configured_ = false;
    uint32_t synced_lane_revision_ = 0xFFFFFFFFU;
    uint8_t synced_window_start_ = 0xFF;
    std::array<uint32_t, 8> macro_press_started_at_ms_{};
    uint8_t macro_button_down_mask_ = 0;
    uint8_t macro_button_long_mask_ = 0;
    uint8_t macro_button_turn_mask_ = 0;
    uint8_t transition_encoder_mask_ = 0;
    uint32_t nav_press_started_at_ms_ = 0;
    bool nav_button_tracked_ = false;
    bool nav_button_long_ = false;
    bool nav_button_turned_ = false;
};

}  // namespace core::handler
