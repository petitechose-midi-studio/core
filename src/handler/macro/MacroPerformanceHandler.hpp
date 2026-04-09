#pragma once

#include <array>
#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "handler/macro/MacroDomainServices.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

class MacroPerformanceHandler {
public:
    struct StateRefs {
        core::state::macro::MacroUiState& macroUi;
        core::state::macro::MacroPagesState& pages;
    };

    MacroPerformanceHandler(StateRefs state,
                            MacroDomainServices services,
                            oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                            oc::api::EncoderAPI& encoders,
                            oc::api::ButtonAPI& buttons,
                            oc::type::ScopeID scopeId);

    ~MacroPerformanceHandler() = default;

    MacroPerformanceHandler(const MacroPerformanceHandler&) = delete;
    MacroPerformanceHandler& operator=(const MacroPerformanceHandler&) = delete;

private:
    void setupBindings();
    void activateClutch();
    void deactivateClutch();
    void openQuickControls();
    void closeQuickControlsApply();
    void closeQuickControlsCancel();
    void navigateQuickControls(float delta);
    void setFocusedQuickControlValue(float normalized);
    void openPageSelector();
    void navigatePageSelector(float delta);
    void toggleSelectedPageEnabled();
    void closePageSelectorApplyIfReleased();
    void closePageSelectorCancel();
    void navigateProperty(float delta);
    void movePage(float delta);
    void moveTrack(float delta);
    void toggleActiveTrackEnabled();
    void configureMacroEncoders();
    void configureValueEncoders();
    void configureDiscreteEncoders(uint8_t discreteSteps);
    void configureQuickControlEncoder();
    void resetQuickControlsState();
    void configureNormalizedEncoder(Config::EncoderID id);
    void configureDiscreteEncoder(Config::EncoderID id, uint8_t discreteSteps);
    int currentCcOffsetMin() const;
    int currentCcOffsetMax() const;
    float offsetToNormalized(int offset) const;
    int normalizedToOffset(float normalized) const;
    void applyCcOffsetFromSnapshot(int offset) const;
    void applyGlobalChannel(uint8_t channel) const;

    core::state::macro::MacroUiState& macro_ui_;
    core::state::macro::MacroPagesState& pages_;
    MacroDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    bool nav_modifier_used_ = false;
    bool left_center_held_ = false;
    bool left_bottom_held_ = false;
    uint8_t quick_snapshot_page_ = 0;
    std::array<core::state::macro::MacroConfig, Config::MACRO_COUNT> quick_snapshot_configs_{};
    uint8_t page_selector_snapshot_page_ = 0;
    uint8_t page_selector_snapshot_enabled_mask_ = 0xFF;
};

}  // namespace core::handler
