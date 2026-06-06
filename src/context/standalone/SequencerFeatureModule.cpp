#include "context/standalone/SequencerFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/PatternPitchSettingsOverlayPresenter.hpp"
#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"
#include "context/standalone/SequencerOverlayPresenter.hpp"
#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/PatternPitchSettingsDomainServices.hpp"
#include "handler/sequencer/PatternPitchSettingsHandler.hpp"
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
#if defined(MS_UX_RECORDER)
    ,
    core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
)
#if defined(MS_UX_RECORDER)
    : property_selector_ux_surface_(stateRefs.activeView, stateRefs.sequencer),
      quick_controls_ux_surface_(stateRefs.activeView, stateRefs.sequencer),
      structure_ux_surface_(
          stateRefs.activeView,
          stateRefs.structureNavigationFocus,
          stateRefs.trackNavigation,
          stateRefs.structureClipboard,
          stateRefs.sequencer,
          stateRefs.sequencerTracks,
          &structure_ux_trace_state_
      ),
      step_edit_ux_surface_(stateRefs.activeView, stateRefs.sequencer),
      step_grid_ux_surface_(stateRefs.activeView, stateRefs.sequencer)
#endif
{
#if defined(MS_UX_RECORDER)
    if (uxRegistry) {
        uxRegistry->add(
            step_edit_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_STEP_EDIT
        );
        uxRegistry->add(
            property_selector_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_PROPERTY_SELECTOR
        );
        uxRegistry->add(
            quick_controls_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_QUICK_CONTROLS
        );
        uxRegistry->add(
            structure_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_STRUCTURE
        );
        uxRegistry->add(
            step_grid_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_STEP_GRID
        );
    }
#endif

    const auto sequencerViewScopeId = oc::ui::lvgl::scopeID(sequencerViewScope);
    encoder_sync_ = core::app::makeExtmemUnique<SequencerEncoderSyncCoordinator>(
        SequencerEncoderSyncCoordinator::StateRefs{
            stateRefs.overlays,
            stateRefs.activeView,
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
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

    pattern_pitch_settings_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(sequencerViewScope);
    overlays.registerCleanup(
        core::ui::OverlayType::PATTERN_PITCH_SETTINGS,
        oc::ui::lvgl::scopeID(pattern_pitch_settings_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    pattern_pitch_settings_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(sequencerViewScope);
    overlays.registerCleanup(
        core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR,
        oc::ui::lvgl::scopeID(pattern_pitch_settings_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    presenter_ = core::app::makeExtmemUnique<SequencerOverlayPresenter>(
        SequencerOverlayPresenter::StateRefs{
            stateRefs.sequencer,
        },
        *step_edit_overlay_
    );
    presenter_->bind();
    pattern_pitch_settings_presenter_ =
        core::app::makeExtmemUnique<PatternPitchSettingsOverlayPresenter>(
            PatternPitchSettingsOverlayPresenter::StateRefs{
                stateRefs.patternPitchSettings,
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
            },
            *pattern_pitch_settings_overlay_,
            *pattern_pitch_settings_selector_overlay_
        );
    pattern_pitch_settings_presenter_->bind();
    encoder_sync_->bind();

    step_handler_ = core::app::makeExtmemUnique<core::handler::SequencerStepHandler>(
        core::handler::SequencerStepHandler::StateRefs{
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
            stateRefs.structureNavigationFocus,
            stateRefs.trackNavigation,
            stateRefs.structureClipboard,
            sharedTracks,
            stateRefs.history,
        },
        encoders,
        buttons,
        sequencerViewScopeId
#if defined(MS_UX_RECORDER)
        ,
        &structure_ux_trace_state_
#endif
    );
    quick_controls_handler_ =
        core::app::makeExtmemUnique<core::handler::SequencerPatternQuickControlsHandler>(
            core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                stateRefs.overlays,
                stateRefs.sequencer,
                stateRefs.trackNavigation,
                stateRefs.history,
            },
            encoders,
            buttons,
            sequencerViewScopeId
        );
    step_edit_handler_ = core::app::makeExtmemUnique<core::handler::SequencerStepEditHandler>(
        core::handler::SequencerStepEditHandler::StateRefs{
            stateRefs.overlays,
            stateRefs.sequencer,
            stateRefs.trackNavigation,
            stateRefs.history,
        },
        overlays,
        encoders,
        buttons,
        sequencerViewScopeId,
        oc::ui::lvgl::scopeID(step_edit_overlay_->getElement())
    );
    property_selector_handler_ =
        core::app::makeExtmemUnique<core::handler::SequencerPropertySelectorHandler>(
            core::handler::SequencerPropertySelectorHandler::StateRefs{
                stateRefs.overlays,
                stateRefs.sequencer,
                stateRefs.trackNavigation,
                stateRefs.history,
            },
            encoders,
            buttons,
            sequencerViewScopeId,
            oc::time::millis
        );
    pattern_pitch_settings_handler_ =
        core::app::makeExtmemUnique<core::handler::PatternPitchSettingsHandler>(
            core::handler::PatternPitchSettingsHandler::StateRefs{
                stateRefs.patternPitchSettings,
                stateRefs.sequencer,
                stateRefs.history,
            },
            core::handler::PatternPitchSettingsDomainServices{
                core::handler::PatternPitchSettingsDomainServices::StateRefs{
                    stateRefs.sequencer,
                    stateRefs.sequencerTracks,
                }
            },
            overlays,
            encoders,
            buttons,
            sequencerViewScopeId,
            oc::ui::lvgl::scopeID(pattern_pitch_settings_overlay_->getElement()),
            oc::ui::lvgl::scopeID(pattern_pitch_settings_selector_overlay_->getElement())
        );
    macro_property_handler_ =
        core::app::makeExtmemUnique<core::handler::SequencerMacroPropertyHandler>(
            core::handler::SequencerMacroPropertyHandler::StateRefs{
                stateRefs.overlays,
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
                stateRefs.trackNavigation,
                stateRefs.history,
            },
            encoders,
            sequencerViewScopeId,
            oc::time::millis
        );
}

FLASHMEM SequencerFeatureModule::~SequencerFeatureModule() = default;

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
