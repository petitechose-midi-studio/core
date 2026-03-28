#include "context/standalone/SequencerFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"
#include "context/standalone/SequencerOverlayPresenter.hpp"
#include "handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "handler/sequencer/SequencerPropertySelectorHandler.hpp"
#include "handler/sequencer/SequencerRangeActionHandler.hpp"
#include "handler/sequencer/SequencerStepEditHandler.hpp"
#include "handler/sequencer/SequencerStepHandler.hpp"

namespace core::context::standalone {

FLASHMEM SequencerFeatureModule::SequencerFeatureModule(
    core::state::CoreState& state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* sequencerViewScope
) {
    encoder_sync_ = std::make_unique<SequencerEncoderSyncCoordinator>(state, encoders);
    step_edit_overlay_ = std::make_unique<ms::ui::VirtualListKeyValueOverlay>(sequencerViewScope);
    overlays.registerCleanup(
        core::ui::OverlayType::SEQ_STEP_EDIT,
        oc::ui::lvgl::scopeID(step_edit_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    presenter_ = std::make_unique<SequencerOverlayPresenter>(state, *step_edit_overlay_);
    presenter_->bind();
    encoder_sync_->bind();

    step_handler_ = std::make_unique<core::handler::SequencerStepHandler>(
        state,
        encoders,
        buttons,
        sequencerViewScope
    );
    range_action_handler_ = std::make_unique<core::handler::SequencerRangeActionHandler>(
        state,
        encoders,
        buttons,
        sequencerViewScope
    );
    quick_controls_handler_ =
        std::make_unique<core::handler::SequencerPatternQuickControlsHandler>(
            state,
            encoders,
            buttons,
            sequencerViewScope
        );
    step_edit_handler_ = std::make_unique<core::handler::SequencerStepEditHandler>(
        state,
        overlays,
        encoders,
        buttons,
        sequencerViewScope,
        step_edit_overlay_->getElement()
    );
    property_selector_handler_ =
        std::make_unique<core::handler::SequencerPropertySelectorHandler>(
            state,
            encoders,
            buttons,
            sequencerViewScope
        );
    macro_property_handler_ =
        std::make_unique<core::handler::SequencerMacroPropertyHandler>(
            state,
            encoders,
            sequencerViewScope
        );
}

SequencerFeatureModule::~SequencerFeatureModule() = default;

void SequencerFeatureModule::resetEncoderSync() {
    if (encoder_sync_) {
        encoder_sync_->reset();
    }
}

void SequencerFeatureModule::syncEncodersNow() {
    if (encoder_sync_) {
        encoder_sync_->syncNow();
    }
}

}  // namespace core::context::standalone
