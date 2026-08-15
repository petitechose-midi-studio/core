#include "handler/project/ProjectHandlerInternals.hpp"

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <oc/time/Time.hpp>

#include "handler/common/ModulatorNavigationWorkflow.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::handler {

using namespace project_handler_internal;

namespace {

const char FEEDBACK_PREVIEW_PENDING[] PROGMEM = "Preview - Apply or Back";

}  // namespace

FLASHMEM ProjectHandler::ProjectHandler(StateRefs state,
                                        DeviceSettingsDomainServices deviceSettings,
                                        ProjectScaleSettingsDomainServices scaleSettings,
                                        MacroEditDomainServices macroEditServices,
                                        oc::api::EncoderAPI& encoders,
                                        oc::api::ButtonAPI& buttons,
                                        oc::type::ScopeID projectViewScope,
                                        uint32_t (*timeProvider)())
    : overlays_(state.overlays)
    , active_view_(state.activeView)
    , navigation_(state.navigation)
    , project_tracks_(state.projectTracks)
    , track_domain_(state.trackDomain)
    , status_bar_(state.statusBar)
    , device_settings_(deviceSettings)
    , pages_(state.pages)
    , macro_ui_(state.macroUi)
    , macros_(state.macros)
    , macro_edit_(state.macroEdit)
    , config_revision_(state.configRevision)
    , macro_history_(state.macroHistory)
    , settings_history_(state.settingsHistory)
    , recorded_shape_capture_(
          ProjectRecordedShapeCaptureWorkflow::StateRefs{
              pages_,
              macro_ui_,
              status_bar_,
              macro_history_,
          },
          ProjectRecordedShapeCaptureWorkflow::Operations{
              .context = this,
              .auditionContext = this,
              .markProjectMutated = &ProjectHandler::markRecordedShapeMutation,
              .publishAudition = &ProjectHandler::publishRecordedShapeAudition,
              .clearAudition = &ProjectHandler::clearRecordedShapeAudition,
          }
      )
    , clipboard_(state.clipboard)
    , history_(state.history)
    , lifecycle_(state.lifecycle)
    , scale_settings_(scaleSettings)
    , macro_edit_services_(macroEditServices)
    , encoders_(encoders)
    , buttons_(buttons)
    , project_view_scope_(projectViewScope)
    , time_provider_(timeProvider ? timeProvider : oc::time::millis) {
    setupBindings();
}


FLASHMEM bool ProjectHandler::canHandleProjectInput() const {
    return !overlays_.hasVisible();
}

FLASHMEM bool ProjectHandler::projectConfirmationActive() const {
    return core::state::project::projectNavigationInProjectConfirmation(navigation_);
}

FLASHMEM bool ProjectHandler::physicalHoldActive() const {
    return canHandleProjectInput() && !projectConfirmationActive() &&
           navigation_.physicalHoldActive.get();
}

FLASHMEM bool ProjectHandler::regularProjectInputActive() const {
    return canHandleProjectInput() && !navigation_.physicalHoldActive.get();
}

FLASHMEM void ProjectHandler::enterPhysicalHoldLayer() {
    core::state::macro::MacroAutomationSlotAddress address{};
    if (modulatorAuditionAddress(address)) {
        navigation_.setLifecycleFeedback(FEEDBACK_PREVIEW_PENDING);
        return;
    }
    navigation_.physicalHoldActive.set(true);
}

FLASHMEM void ProjectHandler::leavePhysicalHoldLayer() {
    navigation_.physicalHoldActive.set(false);
    syncFocusedEncoder();
}


void ProjectHandler::update(uint32_t nowMs) {
    pollPendingProjectCatalog();
    if (settings_gesture_commit_deadline_ms_ != 0U &&
        (active_view_.get() != core::ui::ViewType::PROJECT ||
         static_cast<int32_t>(
             nowMs - settings_gesture_commit_deadline_ms_
         ) >= 0)) {
        endProjectSettingsGesture();
    }
    if (routing_gesture_track_ !=
        core::state::project::PROJECT_TRACK_COUNT) {
        const bool contextChanged =
            active_view_.get() != core::ui::ViewType::PROJECT ||
            navigation_.currentNode.get() !=
                core::state::project::ProjectNodeId::ROUTING_ROOT ||
            navigation_.focusedRow.get() != routing_gesture_track_;
        const bool idleElapsed = static_cast<int32_t>(
            nowMs - routing_gesture_commit_deadline_ms_
        ) >= 0;
        if (contextChanged || idleElapsed) {
            commitPendingRoutingGesture();
        }
    }
    if (recorded_shape_capture_button_active_) {
        const bool validCaptureContext =
            active_view_.get() == core::ui::ViewType::MODULATORS &&
            canHandleProjectInput() && focusedRecordedShapeRecord();
        if (!validCaptureContext) {
            (void)cancelRecordedShapeCapture("Record cancelled");
        } else {
            (void)recorded_shape_capture_.sample(nowMs);
            syncRecordedShapeCaptureRevision();
        }
    }
    const bool modulatorContext =
        active_view_.get() == core::ui::ViewType::MODULATORS &&
        canHandleProjectInput() &&
        !pages_.control.audition.active() &&
        !navigation_.physicalHoldActive.get() &&
        (navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATORS_ROOT ||
         navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
         navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS ||
         navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS);
    const bool bottomLeftPressed = buttons_.isPressed(Config::ButtonID::BOTTOM_LEFT);
    if (modulatorContext && bottomLeftPressed &&
        !modulator_bottom_left_was_pressed_) {
        beginModulatorBottomLeft();
    } else if (modulator_bottom_left_was_pressed_ && !bottomLeftPressed) {
        releaseModulatorBottomLeft();
    } else if (!modulatorContext && modulator_bottom_left_was_pressed_) {
        navigation_.guardedModulator = {};
        navigation_.guardedModulationBinding = {};
        navigation_.modulatorGuard.set({});
    }
    modulator_bottom_left_was_pressed_ = modulatorContext && bottomLeftPressed;

    const bool clipboardContext =
        active_view_.get() == core::ui::ViewType::MODULATORS &&
        canHandleProjectInput() && !pages_.control.audition.active() &&
        !navigation_.physicalHoldActive.get() &&
        (navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATORS_ROOT ||
         navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
         navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS ||
         navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS);
    const bool bottomRightPressed =
        buttons_.isPressed(Config::ButtonID::BOTTOM_RIGHT);
    if (clipboardContext && bottomRightPressed &&
        !modulator_bottom_right_was_pressed_) {
        beginModulatorBottomRight();
    } else if (modulator_bottom_right_was_pressed_ && !bottomRightPressed) {
        releaseModulatorBottomRight();
    } else if (!clipboardContext && modulator_bottom_right_was_pressed_) {
        navigation_.guardedClipboardModulator = {};
        navigation_.modulatorClipboardPasteAvailable = false;
        navigation_.modulatorClipboardGuard.set({});
    }
    modulator_bottom_right_was_pressed_ = clipboardContext && bottomRightPressed;

    auto guard = navigation_.modulatorGuard.get();
    if (guard.phase == core::state::contextual::GuardedActionPhase::PRESSED &&
        (nowMs - guard.pressedAtMs) >= Config::Timing::LATCH_THRESHOLD_MS) {
        (void)core::state::contextual::armGuardedAction(
            guard,
            guard.pressedAtMs
        );
        navigation_.modulatorGuard.set(guard);
    }
    auto clipboardGuard = navigation_.modulatorClipboardGuard.get();
    if (clipboardGuard.phase ==
            core::state::contextual::GuardedActionPhase::PRESSED &&
        (nowMs - clipboardGuard.pressedAtMs) >=
            Config::Timing::LATCH_THRESHOLD_MS) {
        (void)core::state::contextual::armGuardedAction(
            clipboardGuard,
            clipboardGuard.pressedAtMs
        );
        navigation_.modulatorClipboardGuard.set(clipboardGuard);
    }
}

FLASHMEM bool ProjectHandler::resetProject() {
    commitPendingRoutingGesture();
    endProjectSettingsGesture();
    (void)cancelRecordedShapeCapture();
    const auto result = lifecycle_.resetMusicalProject();
    if (!result.success()) {
        navigation_.setLifecycleFeedback(
            projectLifecycleFailureLabel(result.status, "Reset failed")
        );
        OC_LOG_WARN("[Project] reset failed status={}",
                    static_cast<unsigned>(result.status));
        return false;
    }
    return true;
}

FLASHMEM void ProjectHandler::back() {
    pending_project_catalog_action_ = PendingProjectCatalogAction::NONE;
    commitPendingRoutingGesture();
    endProjectSettingsGesture();
    macro_history_.endCoalescing();
    if (cancelRecordedShapeCapture("Record cancelled")) return;
    if (cancelDestinationPickerAudition()) {
        syncFocusedEncoder();
        return;
    }
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        using Level = core::state::project::ModulatorDestinationPickerLevel;
        if (navigation_.destinationPickerLevel == Level::MACRO) {
            navigation_.destinationPickerLevel = Level::PAGE;
            navigation_.focusedRow.set(
                core::state::project::modulators::destinationPickerPageRow(
                    pages_,
                    navigation_.destinationPickerTrack,
                    navigation_.destinationPickerPage
                )
            );
            navigation_.notifyContentChanged();
            syncFocusedEncoder();
            return;
        }
        if (navigation_.destinationPickerLevel == Level::PAGE) {
            navigation_.destinationPickerLevel = Level::TRACK;
            navigation_.focusedRow.set(
                core::state::project::modulators::destinationPickerTrackRow(
                    pages_,
                    navigation_.destinationPickerTrack
                )
            );
            navigation_.notifyContentChanged();
            syncFocusedEncoder();
            return;
        }
    }
    if (modulator_navigation::shouldReturnToMacroOnBack(navigation_) &&
        modulator_navigation::returnToMacro(
            {
                overlays_,
                active_view_,
                navigation_,
                macro_edit_,
                pages_,
                project_tracks_,
            },
            time_provider_()
        )) {
        return;
    }
    core::state::project::backProjectNavigation(navigation_);
    syncFocusedEncoder();
}

}  // namespace core::handler
