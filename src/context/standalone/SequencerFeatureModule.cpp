#include "context/standalone/SequencerFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"
#include "context/standalone/SequencerOverlayPresenter.hpp"
#include "handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "handler/sequencer/SequencerPropertySelectorHandler.hpp"
#include "handler/sequencer/SequencerRangeActionHandler.hpp"
#include "handler/sequencer/SequencerStepEditHandler.hpp"
#include "handler/sequencer/SequencerStepHandler.hpp"
#include "handler/sequencer/SequencerTrackSelectorHandler.hpp"

namespace core::context::standalone {

FLASHMEM SequencerFeatureModule::SequencerFeatureModule(
    StateRefs stateRefs,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* sequencerViewScope
) {
    const auto sequencerViewScopeId = oc::ui::lvgl::scopeID(sequencerViewScope);
    encoder_sync_ = std::make_unique<SequencerEncoderSyncCoordinator>(
        SequencerEncoderSyncCoordinator::StateRefs{
            stateRefs.overlays,
            stateRefs.activeView,
            stateRefs.sequencer,
        },
        encoders
    );
    step_edit_overlay_ = std::make_unique<ms::ui::VirtualListKeyValueOverlay>(sequencerViewScope);
    overlays.registerCleanup(
        core::ui::OverlayType::SEQ_STEP_EDIT,
        oc::ui::lvgl::scopeID(step_edit_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    presenter_ = std::make_unique<SequencerOverlayPresenter>(
        SequencerOverlayPresenter::StateRefs{
            stateRefs.sequencer,
        },
        *step_edit_overlay_
    );
    presenter_->bind();
    encoder_sync_->bind();

    step_handler_ = std::make_unique<core::handler::SequencerStepHandler>(
        core::handler::SequencerStepHandler::StateRefs{
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
        },
        encoders,
        buttons,
        sequencerViewScopeId
    );
    range_action_handler_ = std::make_unique<core::handler::SequencerRangeActionHandler>(
        core::handler::SequencerRangeActionHandler::StateRefs{
            stateRefs.overlays,
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
        },
        encoders,
        buttons,
        sequencerViewScopeId
    );
    track_selector_handler_ = std::make_unique<core::handler::SequencerTrackSelectorHandler>(
        core::handler::SequencerTrackSelectorHandler::StateRefs{
            stateRefs.overlays,
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
        },
        encoders,
        buttons,
        sequencerViewScopeId
    );
    quick_controls_handler_ =
        std::make_unique<core::handler::SequencerPatternQuickControlsHandler>(
            core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                stateRefs.overlays,
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
            },
            encoders,
            buttons,
            sequencerViewScopeId
        );
    step_edit_handler_ = std::make_unique<core::handler::SequencerStepEditHandler>(
        core::handler::SequencerStepEditHandler::StateRefs{
            stateRefs.overlays,
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
        },
        overlays,
        encoders,
        buttons,
        sequencerViewScopeId,
        oc::ui::lvgl::scopeID(step_edit_overlay_->getElement())
    );
    property_selector_handler_ =
        std::make_unique<core::handler::SequencerPropertySelectorHandler>(
            core::handler::SequencerPropertySelectorHandler::StateRefs{
                stateRefs.overlays,
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
            },
            encoders,
            buttons,
            sequencerViewScopeId
        );
    macro_property_handler_ =
        std::make_unique<core::handler::SequencerMacroPropertyHandler>(
            core::handler::SequencerMacroPropertyHandler::StateRefs{
                stateRefs.overlays,
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
            },
            encoders,
            sequencerViewScopeId,
            oc::time::millis
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
