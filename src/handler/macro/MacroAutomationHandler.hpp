#pragma once

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/macro/MacroEditDomainServices.hpp"
#include "state/MacroEditState.hpp"

namespace core::handler {

class MacroAutomationHandler {
public:
    using NowProvider = uint32_t (*)();

    struct StateRefs {
        core::state::MacroEditState& macroEdit;
        core::state::macro::MacroPagesState& pages;
    };

    MacroAutomationHandler(
        StateRefs state,
        MacroEditDomainServices services,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID automationScope,
        NowProvider nowProvider
    );

    MacroAutomationHandler(const MacroAutomationHandler&) = delete;
    MacroAutomationHandler& operator=(const MacroAutomationHandler&) = delete;

    void update(uint32_t nowMs);

private:
    void setupBindings();

    bool active() const;
    bool automationDetailActive() const;
    bool modulationDetailActive() const;
    bool conversionPreviewActive() const;
    bool lfoAuditionActive() const;
    uint8_t macroIndex() const;
    void moveFocus(float delta);
    void editFocusedValue(float normalized);
    void configureOptForFocusedRow();
    void setCoarseEditActive(bool active);
    void backToMacroEdit();
    void toggleFocusedPlayback();
    void copyFocusedSource();
    void beginBottomLeftAction();
    void releaseBottomLeftAction();
    void beginBottomRightAction();
    void releaseBottomRightAction();
    void commitGuardedAction(uint32_t nowMs);
    void activateFocusedRow();
    void openConversionPreview();
    void selectConversionPolicy(float delta);
    bool applyConversion(bool overwriteGesture);
    bool startLfoAudition();
    bool cancelLfoAudition();
    bool applyLfoAudition();

    core::state::MacroEditState& macro_edit_;
    core::state::macro::MacroPagesState& pages_;
    MacroEditDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID automation_scope_ = 0;
    NowProvider now_provider_ = nullptr;
    core::state::MacroEditFlowPhase observed_flow_phase_ =
        core::state::MacroEditFlowPhase::CLOSED;
    bool coarse_edit_active_ = false;
};

}  // namespace core::handler
