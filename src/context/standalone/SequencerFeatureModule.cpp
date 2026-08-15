#include "context/standalone/SequencerFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/api/MidiAPI.hpp>
#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/PatternPitchSettingsOverlayPresenter.hpp"
#include "context/standalone/OverlayPresentationRegistry.hpp"
#include "context/standalone/ProjectTrackEditorPresenter.hpp"
#include "context/standalone/DrumLaneEditorPresenter.hpp"
#include "context/standalone/SequencerEncoderSyncCoordinator.hpp"
#include "context/standalone/SequencerCcLaneOverlayPresenter.hpp"
#include "context/standalone/SequencerOverlayPresenter.hpp"
#include "context/standalone/SequencerPatternEditorPresenter.hpp"
#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/PatternPitchSettingsDomainServices.hpp"
#include "handler/sequencer/PatternPitchSettingsHandler.hpp"
#include "handler/sequencer/ProjectTrackEditorHandler.hpp"
#include "handler/sequencer/DrumLaneEditorHandler.hpp"
#include "handler/sequencer/SequencerCcLaneDomainServices.hpp"
#include "handler/sequencer/SequencerCcLaneHandler.hpp"
#include "handler/sequencer/SequencerCcLaneWorkflow.hpp"
#include "handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "handler/sequencer/SequencerPatternEditorHandler.hpp"
#include "handler/sequencer/SequencerPropertySelectorHandler.hpp"
#include "handler/sequencer/SequencerStepEditHandler.hpp"
#include "handler/sequencer/SequencerStepContentHandler.hpp"
#include "handler/sequencer/SequencerStepHandler.hpp"
#include "ui/sequencer/SequencerPatternEditorOverlay.hpp"
#include "ui/sequencer/SequencerChordVoiceRail.hpp"
#include "ui/sequencer/SequencerStepEditOverlay.hpp"
#include "ui/interaction/TextKeyboardView.hpp"
#include "ui/project/ProjectTrackEditorOverlay.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone {

FLASHMEM SequencerFeatureModule::SequencerFeatureModule(
    StateRefs stateRefs,
    core::handler::SharedTrackDomainServices sharedTracks,
    core::state::project::ProjectTrackDomainServices trackDomain,
    core::handler::SequencerStepPresetDomainServices stepPresets,
    core::handler::SequencerChordPresetDomainServices chordPresets,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    OverlayPresentationRegistry& overlayPresentations,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::api::MidiAPI& midi,
    lv_obj_t* overlayRoot,
    lv_obj_t* sequencerViewScope
#if defined(MS_UX_RECORDER)
    ,
    core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
)
#if defined(MS_UX_RECORDER)
      : track_editor_ux_surface_(
          stateRefs.activeView,
          stateRefs.projectTrackEditor,
          stateRefs.projectTracks
      ),
      property_selector_ux_surface_(
          stateRefs.activeView,
          stateRefs.structureNavigationFocus,
          stateRefs.trackNavigation,
          stateRefs.sequencer
      ),
      preset_library_ux_surface_(stateRefs.sequencer, stateRefs.trackActivations),
      cc_lane_ux_surface_(
          stateRefs.sequencer,
          stateRefs.sequencerTracks,
          stateRefs.projectNavigation,
          stateRefs.projectTracks,
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
          stateRefs.projectTracks,
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
      drum_lane_editor_ux_surface_(
          stateRefs.activeView,
          stateRefs.sequencer
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
    if (uxRegistry &&
        (!uxRegistry->add(
            track_editor_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_TRACK_EDIT
        ) ||
         !uxRegistry->add(
            preset_library_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_PRESET_LIBRARY
        ) ||
         !uxRegistry->add(
            cc_lane_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_CC_LANE
        ) ||
         !uxRegistry->add(
            step_edit_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_STEP_EDIT
        ) ||
         !uxRegistry->add(
            drum_lane_editor_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_DRUM_LANE_EDIT
        ) ||
         !uxRegistry->add(
            property_selector_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_PROPERTY_SELECTOR
        ) ||
         !uxRegistry->add(
            quick_controls_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_QUICK_CONTROLS
        ) ||
         !uxRegistry->add(
            structure_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_STRUCTURE
        ) ||
         !uxRegistry->add(
            step_grid_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_STEP_GRID
        ))) return;
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
    pattern_randomize_session_ = core::app::makeExtmemUnique<
        core::state::sequencer::SequencerPatternRandomizeSession>();
    if (!pattern_randomize_session_) return;
#if defined(MS_UX_RECORDER)
    pattern_editor_ux_surface_ = core::app::makeExtmemUnique<
        core::context::standalone::ux::SequencerPatternEditorUxSurface>(
            stateRefs.activeView,
            stateRefs.sequencer,
            *pattern_randomize_session_
        );
    if (!pattern_editor_ux_surface_) return;
    if (uxRegistry &&
        !uxRegistry->add(
            *pattern_editor_ux_surface_,
            core::context::standalone::ux::priority::SEQUENCER_PATTERN_EDIT
        )) return;
#endif
    pattern_editor_overlay_ =
        core::app::makeExtmemUnique<core::ui::SequencerPatternEditorOverlay>(
            overlayRoot
        );
    if (!pattern_editor_overlay_ || !pattern_editor_overlay_->getElement()) return;
    pattern_editor_action_strip_ =
        core::app::makeExtmemUnique<core::ui::ContextActionStrip>(
            pattern_editor_overlay_->getElement(),
            core::ui::ContextActionStripOrientation::HORIZONTAL
        );
    if (!pattern_editor_action_strip_ ||
        !pattern_editor_action_strip_->getElement()) return;
    if (auto* strip = pattern_editor_action_strip_->getElement()) {
        lv_obj_add_flag(strip, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(
            strip,
            LV_ALIGN_BOTTOM_MID,
            0,
            -::standalone::theme::layout::TRANSPORT_BAR_HEIGHT
        );
        lv_obj_move_foreground(strip);
    }
    track_editor_overlay_ = core::app::makeExtmemUnique<
        core::ui::project::ProjectTrackEditorOverlay>(overlayRoot);
    if (!track_editor_overlay_ || !track_editor_overlay_->getElement()) return;
    track_editor_action_strip_ =
        core::app::makeExtmemUnique<core::ui::ContextActionStrip>(
            track_editor_overlay_->getElement(),
            core::ui::ContextActionStripOrientation::HORIZONTAL
        );
    if (!track_editor_action_strip_ ||
        !track_editor_action_strip_->getElement()) return;
    if (auto* strip = track_editor_action_strip_->getElement()) {
        lv_obj_add_flag(strip, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(
            strip,
            LV_ALIGN_BOTTOM_MID,
            0,
            -::standalone::theme::layout::TRANSPORT_BAR_HEIGHT
        );
        lv_obj_move_foreground(strip);
    }
    step_edit_overlay_ =
        core::app::makeExtmemUnique<core::ui::SequencerStepEditOverlay>(overlayRoot);
    if (!step_edit_overlay_ || !step_edit_overlay_->getElement()) return;
    drum_lane_name_keyboard_ = core::app::makeExtmemUnique<
        core::ui::interaction::TextKeyboardView>(
            step_edit_overlay_->getElement()
        );
    if (!drum_lane_name_keyboard_ || !drum_lane_name_keyboard_->valid()) return;
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
    preset_library_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(overlayRoot);
    if (!preset_library_overlay_ || !preset_library_overlay_->getElement()) return;
    // Step Preset is a decision surface, not a contextual peek: the sequencer
    // grid and its placeholder widgets must not compete with names, impact,
    // compatibility, or transient operation feedback.
    lv_obj_set_style_bg_opa(
        preset_library_overlay_->getElement(),
        LV_OPA_COVER,
        LV_PART_MAIN
    );
    if (lv_obj_get_style_bg_opa(
            preset_library_overlay_->getElement(),
            LV_PART_MAIN
        ) != LV_OPA_COVER) {
        return;
    }
    preset_library_action_strip_ = core::app::makeExtmemUnique<core::ui::ContextActionStrip>(
        preset_library_overlay_->getElement(),
        core::ui::ContextActionStripOrientation::HORIZONTAL
    );
    if (!preset_library_action_strip_ || !preset_library_action_strip_->getElement()) return;
    if (auto* strip = preset_library_action_strip_->getElement()) {
        lv_obj_add_flag(strip, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(
            strip,
            LV_ALIGN_BOTTOM_MID,
            0,
            -::standalone::theme::layout::TRANSPORT_BAR_HEIGHT
        );
        lv_obj_move_foreground(strip);
    }
    preset_library_chord_voice_rail_ =
        core::app::makeExtmemUnique<core::ui::SequencerChordVoiceRail>();
    if (!preset_library_chord_voice_rail_) return;
    preset_library_chord_voice_rail_->create(
        preset_library_overlay_->getElement()
    );
    if (auto* rail = preset_library_chord_voice_rail_->element()) {
        lv_obj_add_flag(rail, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_width(rail, LV_PCT(90));
        lv_obj_align(rail, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_move_foreground(rail);
    } else {
        return;
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
        core::ui::OverlayType::SEQ_PATTERN_EDIT,
        pattern_editor_overlay_->getElement()
    )) return;
    if (!registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::SEQ_TRACK_EDIT,
        track_editor_overlay_->getElement()
    )) return;
    if (!registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::SEQ_STEP_EDIT,
        step_edit_overlay_->getElement()
    )) return;
    if (!registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::PRESET_LIBRARY,
        preset_library_overlay_->getElement()
    )) return;
    if (!registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::SEQ_CC_LANE,
        cc_lane_overlay_->getElement()
    )) return;
    if (!registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::SEQ_DRUM_LANE_EDIT,
        step_edit_overlay_->getElement(),
        0,
        oc::ui::lvgl::scopeID(drum_lane_name_keyboard_->getElement())
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
        *preset_library_overlay_,
        *preset_library_action_strip_,
        *preset_library_chord_voice_rail_
    );
    if (!presenter_ || !presenter_->bind()) return;
    pattern_editor_presenter_ =
        core::app::makeExtmemUnique<SequencerPatternEditorPresenter>(
            SequencerPatternEditorPresenter::StateRefs{
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
                *pattern_randomize_session_,
            },
            *pattern_editor_overlay_,
            *pattern_editor_action_strip_
        );
    if (!pattern_editor_presenter_ || !pattern_editor_presenter_->bind()) return;
    track_editor_presenter_ =
        core::app::makeExtmemUnique<ProjectTrackEditorPresenter>(
            ProjectTrackEditorPresenter::StateRefs{
                stateRefs.projectTrackEditor,
                stateRefs.projectTracks,
                stateRefs.sharedTrackEnabledMask,
                stateRefs.sharedTrackActive,
            },
            *track_editor_overlay_,
            *track_editor_action_strip_
        );
    if (!track_editor_presenter_ || !track_editor_presenter_->bind()) return;
    cc_lane_presenter_ =
        core::app::makeExtmemUnique<SequencerCcLaneOverlayPresenter>(
            SequencerCcLaneOverlayPresenter::StateRefs{
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
                stateRefs.projectNavigation,
                stateRefs.projectTracks,
                *stateRefs.statusBar,
                stateRefs.midiCcCoordinator,
            },
            *cc_lane_overlay_,
            *cc_lane_action_strip_
    );
    if (!cc_lane_presenter_ || !cc_lane_presenter_->bind()) return;
    drum_lane_editor_presenter_ =
        core::app::makeExtmemUnique<DrumLaneEditorPresenter>(
            stateRefs.sequencer,
            *step_edit_overlay_,
            *drum_lane_name_keyboard_,
            *step_edit_action_strip_
        );
    if (!drum_lane_editor_presenter_ ||
        !drum_lane_editor_presenter_->bind()) return;
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
            stateRefs.projectTracks,
            trackDomain,
            stateRefs.structureClipboard,
            sharedTracks,
            stateRefs.history,
            *stateRefs.macroPages,
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
    pattern_editor_handler_ =
        core::app::makeExtmemUnique<core::handler::SequencerPatternEditorHandler>(
            core::handler::SequencerPatternEditorHandler::StateRefs{
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
                *pattern_randomize_session_,
                stateRefs.history,
            },
            overlays,
            encoders,
            buttons,
            oc::ui::lvgl::scopeID(pattern_editor_overlay_->getElement())
        );
    track_editor_handler_ =
        core::app::makeExtmemUnique<core::handler::ProjectTrackEditorHandler>(
            core::handler::ProjectTrackEditorHandler::StateRefs{
                stateRefs.projectTrackEditor,
                stateRefs.projectTracks,
                stateRefs.sequencerTracks,
                sharedTracks,
                trackDomain,
                stateRefs.history,
            },
            overlays,
            encoders,
            buttons,
            oc::ui::lvgl::scopeID(track_editor_overlay_->getElement())
        );
    step_edit_handler_ = core::app::makeExtmemUnique<core::handler::SequencerStepEditHandler>(
        core::handler::SequencerStepEditHandler::StateRefs{
            stateRefs.overlays,
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
            stateRefs.structureClipboard,
            stateRefs.trackNavigation,
            stateRefs.patternPitchSettings,
            stateRefs.structureNavigationFocus,
            stateRefs.history,
            stepPresets,
            chordPresets,
        },
        overlays,
        encoders,
        buttons,
        sequencerViewScopeId,
        oc::ui::lvgl::scopeID(step_edit_overlay_->getElement()),
        oc::ui::lvgl::scopeID(preset_library_overlay_->getElement())
    );
    drum_lane_editor_handler_ =
        core::app::makeExtmemUnique<core::handler::DrumLaneEditorHandler>(
            stateRefs.sequencer,
            stateRefs.history,
            overlays,
            encoders,
            buttons,
            oc::ui::lvgl::scopeID(drum_lane_name_keyboard_->getElement()),
            stateRefs.statusBar != nullptr
                ? core::handler::DrumLaneAuditionServices::fromMidi(
                      midi,
                      stateRefs.projectTracks,
                      *stateRefs.statusBar
                  )
                : core::handler::DrumLaneAuditionServices{}
        );
    if (!drum_lane_editor_handler_) return;
    if (!step_handler_ || !step_edit_handler_ || !pattern_editor_handler_ ||
        !track_editor_handler_) return;
    step_handler_->attachStepEditHandler(*step_edit_handler_);
    step_handler_->attachPatternEditorHandler(*pattern_editor_handler_);
    step_handler_->attachTrackEditorHandler(*track_editor_handler_);
    step_handler_->attachDrumLaneEditorHandler(*drum_lane_editor_handler_);
    step_content_handler_ =
        core::app::makeExtmemUnique<core::handler::SequencerStepContentHandler>(
            core::handler::SequencerStepContentHandler::StateRefs{
                stateRefs.overlays,
                stateRefs.sequencer,
                stateRefs.trackNavigation,
                stateRefs.structureNavigationFocus,
            },
            *step_edit_handler_,
            encoders,
            buttons,
            sequencerViewScopeId
        );
    cc_lane_workflow_ = core::app::makeExtmemUnique<core::handler::SequencerCcLaneWorkflow>(
        core::handler::SequencerCcLaneWorkflow::StateRefs{
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
            stateRefs.projectNavigation,
            stateRefs.history,
            *stateRefs.statusBar,
            stateRefs.midiCcCoordinator,
        },
        core::handler::SequencerCcLaneDomainServices{
            core::handler::SequencerCcLaneDomainServices::StateRefs{
                stateRefs.sequencer,
                stateRefs.sequencerTracks,
                stateRefs.projectTracks,
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
                stateRefs.sequencerTracks,
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
        sequencerViewScopeId,
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
    valid_ = step_handler_ && quick_controls_handler_ && pattern_editor_handler_ &&
             track_editor_handler_ && track_editor_presenter_ &&
             drum_lane_editor_handler_ && drum_lane_editor_presenter_ &&
             step_edit_handler_ &&
             step_content_handler_ &&
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
    if (pattern_editor_handler_) {
        pattern_editor_handler_->update(nowMs);
    }
    if (track_editor_handler_) {
        track_editor_handler_->update(nowMs);
    }
    if (track_editor_presenter_) {
        track_editor_presenter_->update();
    }
    if (drum_lane_editor_handler_) {
        drum_lane_editor_handler_->update(nowMs);
    }
    if (drum_lane_editor_presenter_) {
        drum_lane_editor_presenter_->update();
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
