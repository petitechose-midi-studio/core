#include "context/standalone/SequencerFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/PatternPitchSettingsOverlayPresenter.hpp"
#include "context/standalone/OverlayPresentationRegistry.hpp"
#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"
#include "context/standalone/SequencerCcLaneOverlayPresenter.hpp"
#include "context/standalone/SequencerOverlayPresenter.hpp"
#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/PatternPitchSettingsDomainServices.hpp"
#include "handler/sequencer/PatternPitchSettingsHandler.hpp"
#include "handler/sequencer/SequencerCcLaneDomainServices.hpp"
#include "handler/sequencer/SequencerCcLaneHandler.hpp"
#include "handler/sequencer/SequencerCcLaneWorkflow.hpp"
#include "handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "handler/sequencer/SequencerPropertySelectorHandler.hpp"
#include "handler/sequencer/SequencerStepEditHandler.hpp"
#include "handler/sequencer/SequencerStepHandler.hpp"
#include "ui/sequencer/SequencerStepEditOverlay.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone {

FLASHMEM SequencerFeatureModule::SequencerFeatureModule(
    StateRefs stateRefs,
    core::handler::SharedTrackDomainServices sharedTracks,
    core::handler::SequencerStepPresetDomainServices stepPresets,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    OverlayPresentationRegistry& overlayPresentations,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* overlayRoot,
    lv_obj_t* sequencerViewScope
#if defined(MS_UX_RECORDER)
    ,
    core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
)
#if defined(MS_UX_RECORDER)
    : property_selector_ux_surface_(stateRefs.activeView, stateRefs.sequencer),
      step_preset_ux_surface_(stateRefs.sequencer, stateRefs.trackActivations),
      cc_lane_ux_surface_(
          stateRefs.sequencer,
          stateRefs.sequencerTracks,
          stateRefs.midiCcCoordinator
      ),
      quick_controls_ux_surface_(stateRefs.activeView, stateRefs.sequencer),
      structure_ux_surface_(
          stateRefs.activeView,
          stateRefs.structureNavigationFocus,
          stateRefs.trackNavigation,
          stateRefs.structureClipboard,
          stateRefs.sequencer,
          stateRefs.sequencerTracks,
          stateRefs.trackActivations,
          &structure_ux_trace_state_
      ),
      step_edit_ux_surface_(
          stateRefs.activeView,
          stateRefs.structureNavigationFocus,
          stateRefs.trackNavigation,
          stateRefs.sequencer,
          stateRefs.sequencerTracks
      ),
      step_grid_ux_surface_(
          stateRefs.activeView,
          stateRefs.structureNavigationFocus,
          stateRefs.trackNavigation,
          stateRefs.sequencer,
          stateRefs.sequencerTracks
      )
#endif
{
#if defined(MS_UX_RECORDER)
    if (uxRegistry) {
        uxRegistry->add(
            step_preset_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_STEP_PRESET
        );
        uxRegistry->add(
            cc_lane_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_CC_LANE
        );
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

    if (!overlayRoot || !sequencerViewScope || stateRefs.statusBar == nullptr) return;
    const auto sequencerViewScopeId = oc::ui::lvgl::scopeID(sequencerViewScope);
    if (sequencerViewScopeId == 0) return;
    encoder_sync_ = core::app::makeExtmemUnique<SequencerEncoderSyncCoordinator>(
        SequencerEncoderSyncCoordinator::StateRefs{
            stateRefs.overlays,
            stateRefs.activeView,
            stateRefs.structureNavigationFocus,
            stateRefs.trackNavigation,
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
        },
        encoders
    );
    if (!encoder_sync_) return;
    step_edit_overlay_ =
        core::app::makeExtmemUnique<core::ui::SequencerStepEditOverlay>(overlayRoot);
    if (!step_edit_overlay_ || !step_edit_overlay_->getElement()) return;
    step_edit_action_strip_ = core::app::makeExtmemUnique<core::ui::ContextActionStrip>(
        step_edit_overlay_->getElement(),
        core::ui::ContextActionStripOrientation::HORIZONTAL
    );
    if (!step_edit_action_strip_ || !step_edit_action_strip_->getElement()) return;
    if (auto* strip = step_edit_action_strip_->getElement()) {
        lv_obj_add_flag(strip, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(
            strip,
            LV_ALIGN_BOTTOM_MID,
            0,
            -::standalone::theme::layout::TRANSPORT_BAR_HEIGHT
        );
        lv_obj_move_foreground(strip);
    }
    step_preset_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(overlayRoot);
    if (!step_preset_overlay_ || !step_preset_overlay_->getElement()) return;
    // Step Preset is a decision surface, not a contextual peek: the sequencer
    // grid and its placeholder widgets must not compete with names, impact,
    // compatibility, or transient operation feedback.
    lv_obj_set_style_bg_opa(
        step_preset_overlay_->getElement(),
        LV_OPA_COVER,
        LV_PART_MAIN
    );
    if (lv_obj_get_style_bg_opa(
            step_preset_overlay_->getElement(),
            LV_PART_MAIN
        ) != LV_OPA_COVER) {
        return;
    }
    step_preset_action_strip_ = core::app::makeExtmemUnique<core::ui::ContextActionStrip>(
        step_preset_overlay_->getElement(),
        core::ui::ContextActionStripOrientation::HORIZONTAL
    );
    if (!step_preset_action_strip_ || !step_preset_action_strip_->getElement()) return;
    if (auto* strip = step_preset_action_strip_->getElement()) {
        lv_obj_add_flag(strip, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(
            strip,
            LV_ALIGN_BOTTOM_MID,
            0,
            -::standalone::theme::layout::TRANSPORT_BAR_HEIGHT
        );
        lv_obj_move_foreground(strip);
    }
    cc_lane_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(overlayRoot);
    if (!cc_lane_overlay_ || !cc_lane_overlay_->getElement()) return;
    cc_lane_action_strip_ = core::app::makeExtmemUnique<core::ui::ContextActionStrip>(
        cc_lane_overlay_->getElement(),
        core::ui::ContextActionStripOrientation::HORIZONTAL
    );
    if (!cc_lane_action_strip_ || !cc_lane_action_strip_->getElement()) return;
    if (auto* strip = cc_lane_action_strip_->getElement()) {
        lv_obj_add_flag(strip, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(
            strip,
            LV_ALIGN_BOTTOM_MID,
            0,
            -::standalone::theme::layout::TRANSPORT_BAR_HEIGHT
        );
        lv_obj_move_foreground(strip);
    }
    if (!registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::SEQ_STEP_EDIT,
        step_edit_overlay_->getElement()
    )) return;
    if (!registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::SEQ_STEP_PRESET,
        step_preset_overlay_->getElement()
    )) return;
    if (!registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::SEQ_CC_LANE,
        cc_lane_overlay_->getElement()
    )) return;

    pattern_pitch_settings_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(overlayRoot);
    if (!pattern_pitch_settings_overlay_ ||
        !pattern_pitch_settings_overlay_->getElement() || !registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::PATTERN_PITCH_SETTINGS,
        pattern_pitch_settings_overlay_->getElement()
    )) return;

    pattern_pitch_settings_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(overlayRoot);
    if (!pattern_pitch_settings_selector_overlay_ ||
        !pattern_pitch_settings_selector_overlay_->getElement() || !registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR,
        pattern_pitch_settings_selector_overlay_->getElement()
    )) return;

    presenter_ = core::app::makeExtmemUnique<SequencerOverlayPresenter>(
        SequencerOverlayPresenter::StateRefs{
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
            stateRefs.structureClipboard,
        },
        *step_edit_overlay_,
        *step_edit_action_strip_,
        *step_preset_overlay_,
        *step_preset_action_strip_
    );
    if (!presenter_ || !presenter_->bind()) return;
    cc_lane_presenter_ =
        core::app::makeExtmemUnique<SequencerCcLaneOverlayPresenter>(
            SequencerCcLaneOverlayPresenter::StateRefs{
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
                *stateRefs.statusBar,
            },
            *cc_lane_overlay_,
            *cc_lane_action_strip_
    );
    if (!cc_lane_presenter_ || !cc_lane_presenter_->bind()) return;
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
    if (!pattern_pitch_settings_presenter_ ||
        !pattern_pitch_settings_presenter_->bind() || !encoder_sync_->bind()) {
        return;
    }

    step_handler_ = core::app::makeExtmemUnique<core::handler::SequencerStepHandler>(
        core::handler::SequencerStepHandler::StateRefs{
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
            stateRefs.structureNavigationFocus,
            stateRefs.trackNavigation,
            stateRefs.projectNavigation,
            stateRefs.structureClipboard,
            sharedTracks,
            stateRefs.history,
            stateRefs.trackActivations,
            stateRefs.statusBar,
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
                stateRefs.structureNavigationFocus,
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
            stateRefs.sequencerTracks,
            stateRefs.structureClipboard,
            stateRefs.trackNavigation,
            stateRefs.structureNavigationFocus,
            stateRefs.history,
            stepPresets,
        },
        overlays,
        encoders,
        buttons,
        sequencerViewScopeId,
        oc::ui::lvgl::scopeID(step_edit_overlay_->getElement()),
        oc::ui::lvgl::scopeID(step_preset_overlay_->getElement())
    );
    cc_lane_workflow_ = core::app::makeExtmemUnique<core::handler::SequencerCcLaneWorkflow>(
        core::handler::SequencerCcLaneWorkflow::StateRefs{
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
            stateRefs.history,
            *stateRefs.statusBar,
        },
        core::handler::SequencerCcLaneDomainServices{
            core::handler::SequencerCcLaneDomainServices::StateRefs{
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
                stateRefs.macroPages,
            }
        }
    );
    if (!cc_lane_workflow_) return;
    property_selector_handler_ =
        core::app::makeExtmemUnique<core::handler::SequencerPropertySelectorHandler>(
            core::handler::SequencerPropertySelectorHandler::StateRefs{
                stateRefs.overlays,
                stateRefs.sequencer,
                stateRefs.trackNavigation,
                stateRefs.structureNavigationFocus,
                stateRefs.history,
            },
            encoders,
            buttons,
            sequencerViewScopeId,
            oc::time::millis,
            cc_lane_workflow_.get(),
            &overlays
        );
    if (!property_selector_handler_) return;
    cc_lane_handler_ = core::app::makeExtmemUnique<core::handler::SequencerCcLaneHandler>(
        stateRefs.sequencer,
        *cc_lane_workflow_,
        *property_selector_handler_,
        overlays,
        encoders,
        buttons,
        oc::ui::lvgl::scopeID(cc_lane_overlay_->getElement()),
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
                stateRefs.structureNavigationFocus,
                stateRefs.history,
            },
            encoders,
            buttons,
            sequencerViewScopeId,
            oc::time::millis
        );
    valid_ = step_handler_ && quick_controls_handler_ && step_edit_handler_ &&
             property_selector_handler_ && cc_lane_handler_ &&
             pattern_pitch_settings_handler_ &&
             macro_property_handler_;
}

FLASHMEM SequencerFeatureModule::~SequencerFeatureModule() = default;

void SequencerFeatureModule::update(uint32_t nowMs) {
    if (step_handler_) {
        step_handler_->update(nowMs);
    }
    if (step_edit_handler_) {
        step_edit_handler_->update(nowMs);
    }
    if (cc_lane_handler_) {
        cc_lane_handler_->update(nowMs);
    }
}

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
