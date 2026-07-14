#pragma once

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
        oc::type::ScopeID overlayScope,
        NowProvider nowProvider
    );

    void update(uint32_t nowMs);

private:
    void setupBindings();
    void syncOptEncoderContract();
    void configureDirectionalOpt();
    void recenterDirectionalOpt();
    void onNavTurn(float delta);
    void onOptTurn(float normalized);
    void onNavTap();
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
    oc::type::ScopeID overlay_scope_ = 0;
    NowProvider now_provider_ = nullptr;
    bool opt_directional_configured_ = false;
};

}  // namespace core::handler
