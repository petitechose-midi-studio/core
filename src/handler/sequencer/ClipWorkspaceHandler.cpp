#include "handler/sequencer/ClipWorkspaceHandler.hpp"

#include <algorithm>
#include <cmath>

#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/ProjectTrackEditorHandler.hpp"
#include "state/shared/NormalizedValue.hpp"
#include "handler/sequencer/SequencerStructureNavigationWorkflow.hpp"
#include "state/StatusBarState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerUiState.hpp"

namespace core::handler {

namespace seq = core::state::sequencer;

namespace {

FLASHMEM bool setNormalizedBehaviorValue(
    seq::SequencerLauncherBehavior& behavior,
    seq::ClipWorkspaceQuickAction action,
    float normalized
) {
    namespace input = core::state::normalized;
    switch (action) {
        case seq::ClipWorkspaceQuickAction::LENGTH:
            behavior.length = static_cast<uint8_t>(input::normalizedToIndex(
                normalized,
                static_cast<int>(seq::SequencerLauncherBehavior::MAX_LENGTH) + 1
            ));
            return true;
        case seq::ClipWorkspaceQuickAction::FOLLOW:
            behavior.follow = seq::sequencerLauncherFollowChoiceAt(
                static_cast<uint8_t>(input::normalizedToIndex(
                    normalized,
                    seq::sequencerLauncherFollowChoiceCount()
                ))
            );
            return true;
        case seq::ClipWorkspaceQuickAction::QUANTIZE:
            behavior.quantization = static_cast<
                seq::SequencerLauncherFollowQuantization>(
                    input::normalizedToIndex(normalized, 3));
            return true;
        case seq::ClipWorkspaceQuickAction::EDIT:
        case seq::ClipWorkspaceQuickAction::COUNT:
            return false;
    }
    return false;
}

}  // namespace

FLASHMEM ClipWorkspaceHandler::ClipWorkspaceHandler(
    Refs refs,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId
)
    : refs_(refs), encoders_(encoders), buttons_(buttons),
      scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void ClipWorkspaceHandler::attachTrackEditorHandler(
    ProjectTrackEditorHandler& handler
) {
    track_editor_handler_ = &handler;
}

FLASHMEM void ClipWorkspaceHandler::attachTrackNavigationWorkflow(
    SequencerStructureNavigationWorkflow& navigation
) {
    navigation_workflow_ = &navigation;
}

FLASHMEM void ClipWorkspaceHandler::update() {
    // An editor can take ownership before the physical release. Retire the
    // launcher latch once the button is physically up so it cannot swallow a
    // later gesture.
    if (!buttons_.isPressed(Config::ButtonID::LEFT_CENTER) &&
        release_latch_.isArmed(Config::ButtonID::LEFT_CENTER)) {
        (void)release_latch_.consume(Config::ButtonID::LEFT_CENTER);
    }

    if (!buttons_.isPressed(Config::ButtonID::BOTTOM_LEFT) &&
        refs_.clipWorkspace.stopLayerActive) {
        endStopLayer();
    }

    // The focus signal is shared with Macro and Project. An inactive Clips
    // matrix must never overwrite the context owned by the visible view.
    auto& workspace = refs_.clipWorkspace;
    const uint32_t nowMs = core::time_compat::millis();
    workspace.updateFeedback(nowMs);
    if (workspace.removePending()) finishPendingRemove();
    if (refs_.activeView.get() != core::ui::ViewType::CLIPS) {
        if (horizontal_navigation_gesture_.active()) {
            horizontal_navigation_gesture_.cancel();
        }
        if (quick_selector_gesture_.active()) quick_selector_gesture_.cancel();
        workspace.clearQuickControl();
        workspace.setStopLayer(false);
        return;
    }
    if (workspace.quickPropertyArmed &&
        (workspace.focusArea != workspace.quickTargetFocus ||
         workspace.focusedSlot != workspace.quickTargetSlot ||
         (workspace.quickTargetFocus == seq::ClipWorkspaceFocus::CLIP &&
          (workspace.focusedTrack != workspace.quickTargetTrack ||
           !refs_.sequencerClips.isOccupied({
               workspace.quickTargetTrack,
               workspace.quickTargetSlot,
           }))) ||
         (workspace.quickTargetFocus == seq::ClipWorkspaceFocus::SCENE &&
          !refs_.sequencerClips.sceneUsed(workspace.quickTargetSlot)))) {
        workspace.clearQuickControl();
    }
    if (trackSelectionActive()) {
        refs_.clipWorkspace.focusTrackHeader(
            refs_.trackNavigation.selection.cursorIndex.get()
        );
    }
    if (refs_.clipWorkspace.matrixVisible()) {
        syncNavigationFocus();
    }
}

FLASHMEM bool ClipWorkspaceHandler::trackSelectionActive() const {
    return refs_.trackNavigation.selection.active.get() &&
        refs_.trackNavigation.selection.scope.get() ==
            core::state::StructureSelectionScope::TRACK;
}

FLASHMEM bool ClipWorkspaceHandler::trackHeaderAvailable() const {
    return matrixAvailable() &&
        refs_.clipWorkspace.trackHeaderFocused() &&
        refs_.clipWorkspace.operation ==
            seq::ClipWorkspaceOperation::BROWSE;
}

FLASHMEM bool ClipWorkspaceHandler::matrixAvailable() const {
    return refs_.clipWorkspace.matrixVisible() &&
        !refs_.trackPaste.navigationBlocked() &&
        !refs_.trackNavigation.hold.active() &&
        !refs_.clipWorkspace.editorActive() &&
        !refs_.overlays.hasVisible() &&
        !refs_.drumSequencer.pickerVisible() &&
        !trackSelectionActive();
}

FLASHMEM bool ClipWorkspaceHandler::editorAvailable() const {
    return refs_.clipWorkspace.matrixVisible() &&
        refs_.clipWorkspace.editorActive() &&
        !refs_.overlays.hasVisible() &&
        !refs_.drumSequencer.pickerVisible() &&
        !trackSelectionActive();
}

FLASHMEM bool ClipWorkspaceHandler::horizontalNavigationAvailable() const {
    return matrixAvailable() &&
        !refs_.clipWorkspace.stopLayerActive &&
        !refs_.clipWorkspace.removePending();
}

FLASHMEM bool ClipWorkspaceHandler::quickSelectorAvailable() const {
    const auto& ui = refs_.clipWorkspace;
    return matrixAvailable() && !ui.selectionActive() &&
        !ui.quickSelectorVisible &&
        (focusedClipAvailable() ||
         (ui.sceneFocused() && refs_.sequencerClips.sceneUsed(ui.focusedSlot)));
}

FLASHMEM bool ClipWorkspaceHandler::directPatternAvailable() const {
    const auto& ui = refs_.clipWorkspace;
    if (!matrixAvailable() || !ui.clipFocused() || ui.selectionActive() ||
        !refs_.sequencerTracks.isTrackEnabled(ui.focusedTrack)) {
        return false;
    }
    return refs_.sequencerClips.slotKind({ui.focusedTrack, ui.focusedSlot}) !=
        seq::SequencerLauncherSlotKind::STOP;
}

FLASHMEM bool ClipWorkspaceHandler::operationBackAvailable() const {
    return matrixAvailable() && refs_.clipWorkspace.selectionActive();
}

FLASHMEM bool ClipWorkspaceHandler::focusedClipAvailable() const {
    const auto& ui = refs_.clipWorkspace;
    return matrixAvailable() && ui.clipFocused() && !ui.selectionActive() &&
        refs_.sequencerClips.isOccupied({ui.focusedTrack, ui.focusedSlot});
}

FLASHMEM void ClipWorkspaceHandler::setupBindings() {
    static_assert(
        Config::MACRO_COUNT == seq::ClipWorkspaceUiState::MACRO_TARGET_COUNT
    );
    for (uint8_t index = 0U; index < Config::MACRO_COUNT; ++index) {
        buttons_.button(Config::MACRO_BUTTONS[index])
            .press()
            .scope(scope_id_)
            .when([this]() { return matrixAvailable(); })
            .then([this, index]() { launchVisible(index); });
    }

    buttons_.button(Config::ButtonID::NAV)
        .press()
        .scope(scope_id_)
        .priority(127)
        .when([this]() {
            return (horizontalNavigationAvailable() &&
                    !quick_selector_gesture_.active()) ||
                (matrixAvailable() &&
                 refs_.clipWorkspace.stopLayerActive);
        })
        .then([this]() {
            if (refs_.clipWorkspace.stopLayerActive) {
                stopFocusedTrack();
                release_latch_.arm(Config::ButtonID::NAV);
                return;
            }
            beginHorizontalNavigation();
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return quick_selector_gesture_.active() ||
                horizontal_navigation_gesture_.active() ||
                matrixAvailable() || editorAvailable();
        })
        .then([this](float delta) {
            if (quick_selector_gesture_.active()) moveQuickSelector(delta);
            else if (editorAvailable()) edit(delta);
            else moveHorizontal(delta);
        });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            const auto& ui = refs_.clipWorkspace;
            return (editorAvailable() &&
                    ui.editor != seq::ClipWorkspaceEditor::SLOT_ACTION) ||
                matrixAvailable();
        })
        .then([this](float value) {
            if (editorAvailable()) editEditorValue(value);
            else if (refs_.clipWorkspace.quickPropertyArmed) {
                editQuickProperty(value);
            } else {
                move(value);
            }
        });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .priority(127)
        .when([this]() {
            return matrixAvailable() &&
                horizontal_navigation_gesture_.active() &&
                !horizontal_navigation_gesture_.turned();
        })
        .then([this]() {
            horizontal_navigation_gesture_.cancel();
            release_latch_.arm(Config::ButtonID::NAV);
            if (trackHeaderAvailable()) beginTrackSelection();
            else if (focusedClipAvailable()) selectFocused();
            // External editors take ownership before the physical release,
            // so the launcher cannot consume that release in its own scope.
            // Clear the local latch now to avoid swallowing the next NAV tap
            // after the overlay closes. Inline launcher editors stay scoped
            // here and consume their release normally.
            if (refs_.overlays.hasVisible()) {
                (void)release_latch_.consume(Config::ButtonID::NAV);
            }
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .priority(127)
        .when([this]() {
            return horizontal_navigation_gesture_.active() ||
                matrixAvailable() || editorAvailable() ||
                release_latch_.isArmed(Config::ButtonID::NAV);
        })
        .then([this]() {
            if (horizontal_navigation_gesture_.active()) {
                releaseHorizontalNavigation();
                return;
            }
            if (release_latch_.consume(Config::ButtonID::NAV)) return;
            if (editorAvailable()) {
                if (refs_.clipWorkspace.editor ==
                    seq::ClipWorkspaceEditor::SLOT_ACTION) {
                    confirmSlotAction();
                }
                return;
            }
            openFocused();
        });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .scope(scope_id_)
        .priority(120)
        .when([this]() { return quickSelectorAvailable(); })
        .then([this]() { beginQuickSelector(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return quick_selector_gesture_.active() ||
                (matrixAvailable() &&
                    (refs_.clipWorkspace.selectionActive() ||
                     focusedClipAvailable() ||
                     (trackHeaderAvailable() &&
                      !refs_.trackPaste.detailsAvailable()))) ||
                release_latch_.isArmed(Config::ButtonID::LEFT_CENTER);
        })
        .then([this]() {
            if (quick_selector_gesture_.active()) {
                releaseQuickSelector();
                return;
            }
            if (release_latch_.consume(Config::ButtonID::LEFT_CENTER)) return;
            if (refs_.clipWorkspace.selectionActive()) {
                beginMove();
                return;
            }
            if (trackHeaderAvailable()) openFocusedEditor();
        });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return directPatternAvailable() || trackHeaderAvailable();
        })
        .then([this]() {
            if (trackHeaderAvailable()) {
                toggleTrackSolo();
            } else {
                openFocusedPattern();
            }
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return operationBackAvailable() || editorAvailable() ||
                (matrixAvailable() && refs_.clipWorkspace.quickPropertyArmed);
        })
        .then([this]() {
            if (editorAvailable()) {
                (void)refs_.clipWorkspace.closeEditor();
            } else {
                back();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return matrixAvailable() &&
                refs_.clipWorkspace.selectionActive() &&
                !refs_.clipWorkspace.placementActive();
        })
        .then([this]() { beginRemove(core::time_compat::millis()); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return matrixAvailable() &&
                refs_.clipWorkspace.removeHoldActive;
        })
        .then([this]() { applyRemove(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return matrixAvailable() &&
                refs_.clipWorkspace.removeHoldActive;
        })
        .then([this]() { endRemove(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return matrixAvailable() &&
                refs_.clipWorkspace.selectionActive();
        })
        .then([this]() { applyOrBeginDuplicate(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return matrixAvailable() &&
                !refs_.clipWorkspace.selectionActive();
        })
        .then([this]() { beginStopLayer(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return refs_.clipWorkspace.stopLayerActive;
        })
        .then([this]() { endStopLayer(); });
}

FLASHMEM void ClipWorkspaceHandler::beginHorizontalNavigation() {
    horizontal_navigation_gesture_.press();
}

FLASHMEM void ClipWorkspaceHandler::moveHorizontal(float delta) {
    if (refs_.trackPaste.navigationBlocked()) return;
    const bool hasTurn = nav::hasTurnDelta(delta);
    if (!hasTurn ||
        (horizontal_navigation_gesture_.active() &&
         !horizontal_navigation_gesture_.turn(true))) {
        return;
    }
    auto& ui = refs_.clipWorkspace;
    ui.clearQuickControl();
    const uint16_t navigableTracks = ui.placementActive()
        ? seq::compatibleSequencerClipSelectionTrackMask(
            refs_.sequencerTracks,
            ui.selectedClipMasks,
            ui.sourceTrack)
        : refs_.sharedTrackEnabledMask.get();
    const int steps = nav::turnSteps(delta);
    const int direction = steps < 0 ? -1 : 1;
    for (int step = 0; step < std::abs(steps); ++step) {
        ui.moveHorizontal(direction, navigableTracks);
    }
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::releaseHorizontalNavigation() {
    const auto release = horizontal_navigation_gesture_.release();
    if (release == PressHoldTurnReleaseGesture::Release::TAP) openFocused();
}

FLASHMEM void ClipWorkspaceHandler::beginQuickSelector() {
    quick_selector_gesture_.press();
    refs_.clipWorkspace.showQuickSelector();
}

FLASHMEM void ClipWorkspaceHandler::moveQuickSelector(float delta) {
    if (!quick_selector_gesture_.turn(nav::hasTurnDelta(delta))) return;
    refs_.clipWorkspace.moveQuickAction(nav::turnSteps(delta));
}

FLASHMEM void ClipWorkspaceHandler::releaseQuickSelector() {
    auto& ui = refs_.clipWorkspace;
    const auto action = ui.quickAction;
    const auto release = quick_selector_gesture_.release();
    if (release == PressHoldTurnReleaseGesture::Release::NONE) {
        ui.clearQuickControl();
        return;
    }
    if (action == seq::ClipWorkspaceQuickAction::EDIT) {
        ui.clearQuickControl();
        openFocusedEditor();
        return;
    }
    ui.armQuickProperty();
}

FLASHMEM void ClipWorkspaceHandler::openFocusedPattern() {
    if (!directPatternAvailable()) return;
    auto& ui = refs_.clipWorkspace;
    const seq::SequencerClipAddress address{
        ui.focusedTrack,
        ui.focusedSlot,
    };
    if (!refs_.sequencerClips.isOccupied(address) &&
        !refs_.ops.createClip(refs_.ops.context, address)) {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    if (!enterClip(address)) {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
    }
}

FLASHMEM void ClipWorkspaceHandler::move(float delta) {
    if (!matrixAvailable() || !nav::hasTurnDelta(delta) ||
        refs_.clipWorkspace.removePending()) {
        return;
    }
    auto& ui = refs_.clipWorkspace;
    if (ui.stopLayerActive) return;
    ui.clearQuickControl();
    const int steps = nav::turnSteps(delta);
    const int direction = steps < 0 ? -1 : 1;
    for (int step = 0; step < std::abs(steps); ++step) {
        ui.moveVertical(direction, lastNavigableScene());
    }
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::editQuickProperty(float normalized) {
    if (!matrixAvailable()) return;
    auto& ui = refs_.clipWorkspace;
    if (!ui.quickPropertyArmed) return;
    const seq::SequencerClipAddress address{
        ui.quickTargetTrack,
        ui.quickTargetSlot,
    };
    const bool sceneTarget =
        ui.quickTargetFocus == seq::ClipWorkspaceFocus::SCENE;
    if ((!sceneTarget && !refs_.sequencerClips.isOccupied(address)) ||
        (sceneTarget && !refs_.sequencerClips.sceneUsed(ui.quickTargetSlot))) {
        ui.clearQuickControl();
        return;
    }

    auto behavior = sceneTarget
        ? refs_.sequencerClips.sceneBehavior(ui.quickTargetSlot)
        : refs_.sequencerClips.clipBehavior(address);
    if (!setNormalizedBehaviorValue(
            behavior,
            ui.quickAction,
            normalized)) {
        return;
    }

    const bool accepted = sceneTarget
        ? behavior == refs_.sequencerClips.sceneBehavior(ui.quickTargetSlot) ||
            refs_.ops.setSceneBehavior(refs_.ops.context, ui.quickTargetSlot, behavior)
        : behavior == refs_.sequencerClips.clipBehavior(address) ||
            refs_.ops.setClipBehavior(refs_.ops.context, address, behavior);
    if (accepted) {
        showFeedback(seq::ClipWorkspaceFeedback::NONE);
        ui.bump();
    } else {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        ui.clearQuickControl();
    }
}

FLASHMEM void ClipWorkspaceHandler::edit(float delta) {
    if (!editorAvailable() || !nav::hasTurnDelta(delta)) return;
    const int direction = nav::turnSteps(delta);
    auto& ui = refs_.clipWorkspace;
    if (ui.editor == seq::ClipWorkspaceEditor::SLOT_ACTION) {
        ui.moveSlotAction(direction);
    } else {
        ui.moveEditorField(direction);
    }
}

FLASHMEM void ClipWorkspaceHandler::editEditorValue(float normalized) {
    if (!editorAvailable()) return;
    auto& ui = refs_.clipWorkspace;
    if (ui.editor == seq::ClipWorkspaceEditor::SLOT_ACTION) return;

    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    const bool sceneTarget =
        ui.editor == seq::ClipWorkspaceEditor::SCENE_BEHAVIOR;
    auto behavior = sceneTarget
        ? refs_.sequencerClips.sceneBehavior(ui.focusedSlot)
        : refs_.sequencerClips.clipBehavior(address);
    if (!setNormalizedBehaviorValue(
            behavior,
            seq::clipWorkspaceQuickActionFor(ui.editorField),
            normalized)) {
        return;
    }

    const bool accepted = sceneTarget
        ? behavior == refs_.sequencerClips.sceneBehavior(ui.focusedSlot) ||
            refs_.ops.setSceneBehavior(refs_.ops.context, ui.focusedSlot, behavior)
        : behavior == refs_.sequencerClips.clipBehavior(address) ||
            refs_.ops.setClipBehavior(refs_.ops.context, address, behavior);
    if (!accepted) {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }

    ui.setEditorValues(
        behavior.length,
        static_cast<uint8_t>(behavior.follow),
        static_cast<uint8_t>(behavior.quantization)
    );
    showFeedback(seq::ClipWorkspaceFeedback::NONE);
}

FLASHMEM void ClipWorkspaceHandler::moveViewport(int direction) {
    if (!matrixAvailable() || direction == 0) return;
    refs_.clipWorkspace.moveViewport(direction);
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::selectFocused() {
    if (!matrixAvailable()) return;
    auto& ui = refs_.clipWorkspace;
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    if (ui.operation != seq::ClipWorkspaceOperation::BROWSE ||
        !refs_.sequencerClips.isOccupied(address)) {
        return;
    }
    ui.beginSelection(address.track, address.slot);
}

FLASHMEM void ClipWorkspaceHandler::beginTrackSelection() {
    if (!trackHeaderAvailable() || navigation_workflow_ == nullptr) return;
    const uint8_t track = refs_.clipWorkspace.focusedTrack;
    if (!refs_.sequencerTracks.isTrackEnabled(track)) return;
    refs_.trackNavigation.previewAddSlot.set(false);
    refs_.trackNavigation.syncPreviewTrack(track);
    refs_.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    navigation_workflow_->enterSelectionModeForCurrentFocus();
}

FLASHMEM void ClipWorkspaceHandler::launchVisible(uint8_t macroIndex) {
    if (!matrixAvailable() ||
        macroIndex >= seq::ClipWorkspaceUiState::MACRO_TARGET_COUNT) {
        return;
    }
    auto& ui = refs_.clipWorkspace;
    if (ui.placementActive()) return;
    const auto target = ui.macroTarget(macroIndex);
    if (ui.stopLayerActive) {
        if (target.focus == seq::ClipWorkspaceFocus::CLIP) {
            const auto telemetry = refs_.sequencerClipLaunches.telemetry(
                target.track
            );
            if (!telemetry.stopped && telemetry.activeSlot == target.slot) {
                stopTrack(target.track, false);
            }
        }
        return;
    }
    if (target.focus == seq::ClipWorkspaceFocus::SCENE) {
        if (ui.selectionActive()) return;
        ui.focusScene(target.slot);
        syncNavigationFocus();
        launchScene(target.slot);
        return;
    }
    const seq::SequencerClipAddress address{target.track, target.slot};
    ui.focus(address.track, address.slot);
    syncNavigationFocus();
    if (ui.operation == seq::ClipWorkspaceOperation::SELECT) {
        if (refs_.sequencerClips.isOccupied(address)) {
            ui.toggleSelection(address.track, address.slot);
        }
        return;
    }
    const auto kind = refs_.sequencerClips.slotKind(address);
    bool accepted = false;
    if (kind == seq::SequencerLauncherSlotKind::CLIP) {
        accepted = refs_.ops.requestClipLaunch(refs_.ops.context, address, seq::SequencerClipLaunchQuantization::BAR);
    } else if (kind == seq::SequencerLauncherSlotKind::STOP) {
        accepted = refs_.ops.requestTrackStop(refs_.ops.context, 
            address.track,
            seq::SequencerClipLaunchQuantization::BAR
        );
    } else {
        showFeedback(seq::ClipWorkspaceFeedback::NONE);
        return;
    }
    showFeedback(
        accepted
            ? seq::ClipWorkspaceFeedback::NONE
            : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::openFocused() {
    if (!matrixAvailable()) return;
    auto& ui = refs_.clipWorkspace;
    if (ui.operation == seq::ClipWorkspaceOperation::SELECT) {
        const seq::SequencerClipAddress address{
            ui.focusedTrack,
            ui.focusedSlot,
        };
        if (ui.clipFocused() && refs_.sequencerClips.isOccupied(address)) {
            ui.toggleSelection(address.track, address.slot);
        }
        return;
    }
    if (ui.trackHeaderFocused()) {
        syncNavigationFocus();
        if (!refs_.sequencerTracks.isTrackEnabled(ui.focusedTrack)) {
            refs_.trackNavigation.syncPreviewTrack(ui.focusedTrack);
            refs_.trackNavigation.previewAddSlot.set(true);
            refs_.navigationFocus.set(
                core::state::StructureNavigationFocus::TRACK
            );
            refs_.drumSequencer.openTypePicker(ui.focusedTrack);
            return;
        }
        toggleTrackMute();
        return;
    }
    if (ui.sceneFocused()) {
        if (ui.focusedSlot == lastNavigableScene() &&
            !refs_.sequencerClips.sceneUsed(ui.focusedSlot)) {
            const uint16_t enabledMask = refs_.sharedTrackEnabledMask.get();
            for (uint8_t track = 0U;
                 track < seq::SequencerClipGridState::TRACK_COUNT;
                 ++track) {
                if ((enabledMask & static_cast<uint16_t>(1U << track)) == 0U) {
                    continue;
                }
                ui.focus(track, ui.focusedSlot);
                ui.openEditor(seq::ClipWorkspaceEditor::SLOT_ACTION);
                syncNavigationFocus();
                return;
            }
            showFeedback(seq::ClipWorkspaceFeedback::FAILED);
            return;
        }
        launchScene(ui.focusedSlot);
        return;
    }
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    if (ui.placementActive()) return;
    if (!refs_.sequencerTracks.isTrackEnabled(address.track)) {
        ui.focus(address.track, 0U);
        refs_.trackNavigation.syncPreviewTrack(address.track);
        refs_.trackNavigation.previewAddSlot.set(true);
        refs_.navigationFocus.set(
            core::state::StructureNavigationFocus::TRACK
        );
        refs_.drumSequencer.openTypePicker(address.track);
        return;
    }
    const auto kind = refs_.sequencerClips.slotKind(address);
    if (kind == seq::SequencerLauncherSlotKind::STOP) {
        stopTrack(address.track, false);
        return;
    }
    if (kind == seq::SequencerLauncherSlotKind::EMPTY) {
        ui.openEditor(seq::ClipWorkspaceEditor::SLOT_ACTION);
        return;
    }
    showFeedback(
        refs_.ops.requestClipLaunch(refs_.ops.context, address, seq::SequencerClipLaunchQuantization::BAR)
            ? seq::ClipWorkspaceFeedback::NONE
            : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::openFocusedEditor() {
    if (!matrixAvailable()) return;
    auto& ui = refs_.clipWorkspace;
    ui.clearQuickControl();
    if (ui.trackHeaderFocused()) {
        if (refs_.sequencerTracks.isTrackEnabled(ui.focusedTrack) &&
            track_editor_handler_ != nullptr && selectTrack(ui.focusedTrack)) {
            (void)track_editor_handler_->openActiveTrack();
        }
        return;
    }
    if (ui.sceneFocused()) {
        const auto behavior = refs_.sequencerClips.sceneBehavior(ui.focusedSlot);
        ui.openEditor(
            seq::ClipWorkspaceEditor::SCENE_BEHAVIOR,
            behavior.length,
            static_cast<uint8_t>(behavior.follow),
            static_cast<uint8_t>(behavior.quantization)
        );
        return;
    }
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    if (refs_.sequencerClips.slotKind(address) ==
        seq::SequencerLauncherSlotKind::CLIP) {
        const auto behavior = refs_.sequencerClips.clipBehavior(address);
        ui.openEditor(
            seq::ClipWorkspaceEditor::CLIP_BEHAVIOR,
            behavior.length,
            static_cast<uint8_t>(behavior.follow),
            static_cast<uint8_t>(behavior.quantization)
        );
    } else {
        ui.openEditor(seq::ClipWorkspaceEditor::SLOT_ACTION);
        ui.slotAction = refs_.sequencerClips.isStop(address)
            ? seq::ClipWorkspaceSlotAction::CLEAR
            : seq::ClipWorkspaceSlotAction::CREATE_CLIP;
        ui.bump();
    }
}

FLASHMEM void ClipWorkspaceHandler::confirmSlotAction() {
    if (!editorAvailable() || refs_.clipWorkspace.editor !=
        seq::ClipWorkspaceEditor::SLOT_ACTION) {
        return;
    }
    auto& ui = refs_.clipWorkspace;
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    bool accepted = false;
    switch (ui.slotAction) {
        case seq::ClipWorkspaceSlotAction::CREATE_CLIP:
            accepted = refs_.sequencerClips.isStop(address)
                ? false
                : refs_.ops.createClip(refs_.ops.context, address);
            break;
        case seq::ClipWorkspaceSlotAction::SET_STOP:
            accepted = refs_.ops.setStopSlot(refs_.ops.context, address, true);
            break;
        case seq::ClipWorkspaceSlotAction::CLEAR:
            accepted = refs_.ops.setStopSlot(refs_.ops.context, address, false);
            break;
        case seq::ClipWorkspaceSlotAction::COUNT:
            break;
    }
    if (accepted) {
        (void)ui.closeEditor();
        showFeedback(seq::ClipWorkspaceFeedback::NONE);
    } else {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
    }
}

FLASHMEM uint8_t ClipWorkspaceHandler::lastNavigableScene() const {
    return refs_.sequencerClips.lastNavigableScene();
}

FLASHMEM void ClipWorkspaceHandler::launchScene(uint8_t slot) {
    showFeedback(
        refs_.ops.requestSceneLaunch(refs_.ops.context, slot, seq::SequencerClipLaunchQuantization::BAR)
            ? seq::ClipWorkspaceFeedback::NONE
            : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::stopTrack(
    uint8_t track,
    bool immediate
) {
    showFeedback(
        refs_.ops.requestTrackStop(refs_.ops.context, 
            track,
            immediate
                ? seq::SequencerClipLaunchQuantization::IMMEDIATE
                : seq::SequencerClipLaunchQuantization::BAR
        ) ? seq::ClipWorkspaceFeedback::NONE
          : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::stopFocusedTrack() {
    const auto& ui = refs_.clipWorkspace;
    if (!ui.stopLayerActive || ui.sceneFocused() ||
        !refs_.sequencerTracks.isTrackEnabled(ui.focusedTrack)) {
        return;
    }
    const auto telemetry = refs_.sequencerClipLaunches.telemetry(
        ui.focusedTrack
    );
    if (!telemetry.stopped) stopTrack(ui.focusedTrack, false);
}

FLASHMEM void ClipWorkspaceHandler::beginStopLayer() {
    auto& ui = refs_.clipWorkspace;
    if (!matrixAvailable() || ui.selectionActive()) return;
    if (horizontal_navigation_gesture_.active()) {
        horizontal_navigation_gesture_.cancel();
    }
    ui.setStopLayer(true);
}

FLASHMEM void ClipWorkspaceHandler::endStopLayer() {
    refs_.clipWorkspace.setStopLayer(false);
}

FLASHMEM void ClipWorkspaceHandler::toggleTrackMute() {
    if (!trackHeaderAvailable()) return;
    const uint8_t track = refs_.clipWorkspace.focusedTrack;
    if (!refs_.sequencerTracks.isTrackEnabled(track)) return;
    auto domain = refs_.projectTrackDomain;
    showFeedback(
        domain.setMuted(
            track,
            !core::state::project::projectTrackMuted(
                refs_.projectTracks,
                track
            )
        ) ? seq::ClipWorkspaceFeedback::NONE
          : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::toggleTrackSolo() {
    if (!trackHeaderAvailable()) return;
    const uint8_t track = refs_.clipWorkspace.focusedTrack;
    if (!refs_.sequencerTracks.isTrackEnabled(track)) return;
    auto domain = refs_.projectTrackDomain;
    showFeedback(
        domain.setSoloed(
            track,
            !core::state::project::projectTrackSoloed(
                refs_.projectTracks,
                track
            )
        ) ? seq::ClipWorkspaceFeedback::NONE
          : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::showFeedback(
    seq::ClipWorkspaceFeedback feedback
) {
    refs_.clipWorkspace.setFeedback(
        feedback,
        core::time_compat::millis()
    );
}

FLASHMEM seq::SequencerClipAddress
ClipWorkspaceHandler::sourceAddress() const {
    const auto& ui = refs_.clipWorkspace;
    return {ui.sourceTrack, ui.sourceSlot};
}

FLASHMEM void ClipWorkspaceHandler::beginMove() {
    auto& ui = refs_.clipWorkspace;
    if (!matrixAvailable() ||
        ui.operation != seq::ClipWorkspaceOperation::SELECT) {
        return;
    }
    int8_t trackOffset = 0;
    int8_t slotOffset = 0;
    if (!seq::canMoveSequencerClipSelectionNow(
            refs_.sequencerClips,
            refs_.sequencerClipLaunches,
            ui.selectedClipMasks,
            refs_.statusBar.playing.get()) ||
        !seq::firstSequencerClipSelectionMoveOffset(
            refs_.sequencerClips,
            refs_.sequencerTracks,
            refs_.sequencer,
            ui.selectedClipMasks,
            trackOffset,
            slotOffset)) {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    const auto source = sourceAddress();
    ui.beginPlacement(
        seq::ClipWorkspaceOperation::MOVE_DESTINATION,
        source.track,
        source.slot
    );
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::applyOrBeginDuplicate() {
    auto& ui = refs_.clipWorkspace;
    if (!matrixAvailable()) return;
    if (ui.operation == seq::ClipWorkspaceOperation::SELECT) {
        if (ui.selectedCount() != 1U) {
            showFeedback(seq::ClipWorkspaceFeedback::FAILED);
            return;
        }
        const auto source = sourceAddress();
        seq::SequencerClipAddress destination{};
        if (!seq::firstSequencerClipTransferDestination(
                refs_.sequencerClips,
                refs_.sequencerTracks,
                source,
                seq::SequencerClipStructureAction::DUPLICATE_CLIP,
                destination)) {
            showFeedback(seq::ClipWorkspaceFeedback::FAILED);
            return;
        }
        ui.beginPlacement(
            seq::ClipWorkspaceOperation::DUPLICATE_DESTINATION,
            destination.track,
            destination.slot
        );
        syncNavigationFocus();
        return;
    }
    if (!ui.placementActive()) return;
    const auto source = sourceAddress();
    const seq::SequencerClipAddress destination{
        ui.focusedTrack,
        ui.focusedSlot,
    };
    const auto operation = ui.operation;
    const bool changed = operation ==
            seq::ClipWorkspaceOperation::MOVE_DESTINATION
        ? refs_.ops.moveClips(refs_.ops.context, 
            ui.selectedClipMasks,
            static_cast<int8_t>(
                static_cast<int>(destination.track) - source.track),
            static_cast<int8_t>(
                static_cast<int>(destination.slot) - source.slot))
        : refs_.ops.duplicateClip(refs_.ops.context, source, destination);
    if (!changed) {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    ui.completeOperation(
        destination.track,
        destination.slot,
        operation == seq::ClipWorkspaceOperation::MOVE_DESTINATION
            ? seq::ClipWorkspaceFeedback::MOVED
            : seq::ClipWorkspaceFeedback::DUPLICATED,
        core::time_compat::millis()
    );
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::beginRemove(uint32_t nowMs) {
    auto& ui = refs_.clipWorkspace;
    if (!matrixAvailable() ||
        ui.operation != seq::ClipWorkspaceOperation::SELECT ||
        ui.selectedCount() != 1U) {
        return;
    }
    const auto source = sourceAddress();
    if (!refs_.sequencerClips.isOccupied(source)) {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    ui.beginRemoveHold(nowMs);
}

FLASHMEM void ClipWorkspaceHandler::applyRemove() {
    auto& ui = refs_.clipWorkspace;
    if (!matrixAvailable() || !ui.removeHoldActive ||
        ui.operation != seq::ClipWorkspaceOperation::SELECT) {
        return;
    }
    const auto source = sourceAddress();
    const bool playing = refs_.statusBar.playing.get();
    if (seq::canDeleteSequencerClip(
            refs_.sequencerClips,
            refs_.sequencerClipLaunches,
            source,
            playing)) {
        if (!refs_.ops.deleteClip(refs_.ops.context, source)) {
            ui.clearRemoveHold();
            showFeedback(seq::ClipWorkspaceFeedback::FAILED);
            return;
        }
        ui.completeOperation(
            source.track,
            source.slot,
            seq::ClipWorkspaceFeedback::REMOVED,
            core::time_compat::millis()
        );
        syncNavigationFocus();
        return;
    }
    if (!seq::canRequestSequencerClipDelete(
            refs_.sequencerClips,
            refs_.sequencerClipLaunches,
            source,
            playing) ||
        !refs_.ops.requestTrackStop(refs_.ops.context, 
            source.track,
            seq::SequencerClipLaunchQuantization::IMMEDIATE)) {
        ui.clearRemoveHold();
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    ui.beginPendingRemoval();
}

FLASHMEM void ClipWorkspaceHandler::endRemove() {
    if (!matrixAvailable()) return;
    refs_.clipWorkspace.clearRemoveHold();
}

FLASHMEM void ClipWorkspaceHandler::finishPendingRemove() {
    auto& ui = refs_.clipWorkspace;
    if (!ui.removePending()) return;
    const auto source = sourceAddress();
    const uint16_t trackBit = static_cast<uint16_t>(1U << source.track);
    if (refs_.sequencerClipLaunches.references(source) ||
        (refs_.sequencerClipLaunches.pendingTrackMask() & trackBit) != 0U ||
        (refs_.sequencerClipLaunches.stagedTrackMask() & trackBit) != 0U) {
        return;
    }
    if (!refs_.ops.deleteClip(refs_.ops.context, source)) {
        (void)ui.backOperation();
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    ui.completeOperation(
        source.track,
        source.slot,
        seq::ClipWorkspaceFeedback::REMOVED,
        core::time_compat::millis()
    );
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::back() {
    if (refs_.clipWorkspace.quickPropertyArmed) {
        refs_.clipWorkspace.clearQuickControl();
        return;
    }
    if (operationBackAvailable()) {
        (void)refs_.clipWorkspace.backOperation();
        syncNavigationFocus();
    }
}

FLASHMEM bool ClipWorkspaceHandler::enterClip(
    seq::SequencerClipAddress address
) {
    if (!selectClipForEditing(address)) return false;
    refs_.clipWorkspace.enterPattern(address.track, address.slot);
    return true;
}

FLASHMEM bool ClipWorkspaceHandler::selectClipForEditing(
    seq::SequencerClipAddress address
) {
    if (!selectTrack(address.track)) return false;
    if (!refs_.sequencerClips.isResident(address) &&
        !refs_.ops.switchClipForEditing(refs_.ops.context, address)) {
        return false;
    }
    refs_.navigationFocus.set(
        core::state::StructureNavigationFocus::PAGE
    );
    return true;
}

FLASHMEM bool ClipWorkspaceHandler::selectTrack(uint8_t track) {
    const uint16_t enabledMask = refs_.sharedTrackEnabledMask.get();
    if (refs_.sharedTrackActive.get() != track) {
        (void)refs_.ops.setSharedTrackState(refs_.ops.context, enabledMask, track);
    }
    if (refs_.sharedTrackActive.get() != track) return false;
    refs_.trackNavigation.previewAddSlot.set(false);
    refs_.trackNavigation.syncPreviewTrack(track);
    return true;
}

FLASHMEM void ClipWorkspaceHandler::syncNavigationFocus() {
    const auto& workspace = refs_.clipWorkspace;
    const bool trackHeader = workspace.trackHeaderFocused();
    refs_.trackNavigation.previewAddSlot.set(
        trackHeader &&
        !refs_.sequencerTracks.isTrackEnabled(workspace.focusedTrack)
    );
    if (trackHeader) {
        refs_.trackNavigation.syncPreviewTrack(workspace.focusedTrack);
    }
    refs_.navigationFocus.set(
        trackHeader
            ? core::state::StructureNavigationFocus::TRACK
            : core::state::StructureNavigationFocus::PAGE
    );
}

}  // namespace core::handler
