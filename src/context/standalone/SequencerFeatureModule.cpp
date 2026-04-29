#include "context/standalone/SequencerFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"
#include "context/standalone/SequencerOverlayPresenter.hpp"
#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "handler/sequencer/SequencerPropertySelectorHandler.hpp"
#include "handler/sequencer/SequencerStepEditHandler.hpp"
#include "handler/sequencer/SequencerStepHandler.hpp"

namespace core::context::standalone {

FLASHMEM SequencerFeatureModule::SequencerFeatureModule(
    StateRefs stateRefs,
    core::handler::SharedTrackDomainServices sharedTracks,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* sequencerViewScope
) {
    const auto sequencerViewScopeId = oc::ui::lvgl::scopeID(sequencerViewScope);
    encoder_sync_ = core::app::makeExtmemUnique<SequencerEncoderSyncCoordinator>(
        SequencerEncoderSyncCoordinator::StateRefs{
            stateRefs.overlays,
            stateRefs.activeView,
            stateRefs.sequencer,
        },
        encoders
    );
    step_edit_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(sequencerViewScope);
    overlays.registerCleanup(
        core::ui::OverlayType::SEQ_STEP_EDIT,
        oc::ui::lvgl::scopeID(step_edit_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    presenter_ = core::app::makeExtmemUnique<SequencerOverlayPresenter>(
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
            stateRefs.structureNavigationFocus,
            stateRefs.trackNavigation,
            stateRefs.structureClipboard,
            sharedTracks,
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
                stateRefs.trackNavigation,
            },
            encoders,
            buttons,
            sequencerViewScopeId
        );
    step_edit_handler_ = std::make_unique<core::handler::SequencerStepEditHandler>(
        core::handler::SequencerStepEditHandler::StateRefs{
            stateRefs.overlays,
            stateRefs.sequencer,
            stateRefs.trackNavigation,
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
                stateRefs.trackNavigation,
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
                stateRefs.trackNavigation,
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
