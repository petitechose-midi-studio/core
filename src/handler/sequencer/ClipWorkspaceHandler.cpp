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
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"

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
    StateRefs state,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId
)
    : core_(state.core), navigation_focus_(state.navigationFocus),
      overlays_(state.overlays), encoders_(encoders), buttons_(buttons),
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
        core_.sequencer.clipWorkspace.stopLayerActive) {
        endStopLayer();
    }

    // The focus signal is shared with Macro and Project. An inactive Clips
    // matrix must never overwrite the context owned by the visible view.
    auto& workspace = core_.sequencer.clipWorkspace;
    const uint32_t nowMs = core::time_compat::millis();
    workspace.updateQuickFeedback(nowMs);
    workspace.updateFeedback(nowMs);
    if (workspace.removePending()) finishPendingRemove();
    if (core_.activeView.get() != core::ui::ViewType::CLIPS) {
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
           !core_.sequencerClips.isOccupied({
               workspace.quickTargetTrack,
               workspace.quickTargetSlot,
           }))) ||
         (workspace.quickTargetFocus == seq::ClipWorkspaceFocus::SCENE &&
          !core_.sequencerClips.sceneUsed(workspace.quickTargetSlot)))) {
        workspace.clearQuickControl();
    }
    if (trackSelectionActive()) {
        core_.sequencer.clipWorkspace.focusTrackHeader(
            core_.trackNavigation.selection.cursorIndex.get()
        );
    }
    if (core_.sequencer.clipWorkspace.matrixVisible()) {
        syncNavigationFocus();
    }
}

FLASHMEM bool ClipWorkspaceHandler::trackSelectionActive() const {
    return core_.trackNavigation.selection.active.get() &&
        core_.trackNavigation.selection.scope.get() ==
            core::state::StructureSelectionScope::TRACK;
}

FLASHMEM bool ClipWorkspaceHandler::trackHeaderAvailable() const {
    return matrixAvailable() &&
        core_.sequencer.clipWorkspace.trackHeaderFocused() &&
        core_.sequencer.clipWorkspace.operation ==
            seq::ClipWorkspaceOperation::BROWSE;
}

FLASHMEM bool ClipWorkspaceHandler::matrixAvailable() const {
    return core_.sequencer.clipWorkspace.matrixVisible() &&
        !core_.trackNavigation.hold.active() &&
        !core_.sequencer.clipWorkspace.editorActive() &&
        !overlays_.hasVisible() &&
        !core_.sequencer.drumSequencer.pickerVisible() &&
        !trackSelectionActive();
}

FLASHMEM bool ClipWorkspaceHandler::editorAvailable() const {
    return core_.sequencer.clipWorkspace.matrixVisible() &&
        core_.sequencer.clipWorkspace.editorActive() &&
        !overlays_.hasVisible() &&
        !core_.sequencer.drumSequencer.pickerVisible() &&
        !trackSelectionActive();
}

FLASHMEM bool ClipWorkspaceHandler::horizontalNavigationAvailable() const {
    return matrixAvailable() &&
        !core_.sequencer.clipWorkspace.stopLayerActive &&
        !core_.sequencer.clipWorkspace.removePending();
}

FLASHMEM bool ClipWorkspaceHandler::quickSelectorAvailable() const {
    const auto& ui = core_.sequencer.clipWorkspace;
    return matrixAvailable() && !ui.selectionActive() &&
        !ui.quickSelectorVisible &&
        (focusedClipAvailable() ||
         (ui.sceneFocused() && core_.sequencerClips.sceneUsed(ui.focusedSlot)));
}

FLASHMEM bool ClipWorkspaceHandler::directPatternAvailable() const {
    const auto& ui = core_.sequencer.clipWorkspace;
    if (!matrixAvailable() || !ui.clipFocused() || ui.selectionActive() ||
        !core_.sequencerTracks.isTrackEnabled(ui.focusedTrack)) {
        return false;
    }
    return core_.sequencerClips.slotKind({ui.focusedTrack, ui.focusedSlot}) !=
        seq::SequencerLauncherSlotKind::STOP;
}

FLASHMEM bool ClipWorkspaceHandler::operationBackAvailable() const {
    return matrixAvailable() && core_.sequencer.clipWorkspace.selectionActive();
}

FLASHMEM bool ClipWorkspaceHandler::focusedClipAvailable() const {
    const auto& ui = core_.sequencer.clipWorkspace;
    return matrixAvailable() && ui.clipFocused() && !ui.selectionActive() &&
        core_.sequencerClips.isOccupied({ui.focusedTrack, ui.focusedSlot});
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
                 core_.sequencer.clipWorkspace.stopLayerActive);
        })
        .then([this]() {
            if (core_.sequencer.clipWorkspace.stopLayerActive) {
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
            const auto& ui = core_.sequencer.clipWorkspace;
            return (editorAvailable() &&
                    ui.editor != seq::ClipWorkspaceEditor::SLOT_ACTION) ||
                matrixAvailable();
        })
        .then([this](float value) {
            if (editorAvailable()) editEditorValue(value);
            else if (core_.sequencer.clipWorkspace.quickPropertyArmed) {
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
            if (overlays_.hasVisible()) {
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
                if (core_.sequencer.clipWorkspace.editor ==
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
                    (core_.sequencer.clipWorkspace.selectionActive() ||
                     focusedClipAvailable() || trackHeaderAvailable())) ||
                release_latch_.isArmed(Config::ButtonID::LEFT_CENTER);
        })
        .then([this]() {
            if (quick_selector_gesture_.active()) {
                releaseQuickSelector();
                return;
            }
            if (release_latch_.consume(Config::ButtonID::LEFT_CENTER)) return;
            if (core_.sequencer.clipWorkspace.selectionActive()) {
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
            return operationBackAvailable() || editorAvailable();
        })
        .then([this]() {
            if (editorAvailable()) {
                (void)core_.sequencer.clipWorkspace.closeEditor();
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
                core_.sequencer.clipWorkspace.selectionActive() &&
                !core_.sequencer.clipWorkspace.placementActive();
        })
        .then([this]() { beginRemove(core::time_compat::millis()); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return matrixAvailable() &&
                core_.sequencer.clipWorkspace.removeHoldActive;
        })
        .then([this]() { applyRemove(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return matrixAvailable() &&
                core_.sequencer.clipWorkspace.removeHoldActive;
        })
        .then([this]() { endRemove(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return matrixAvailable() &&
                core_.sequencer.clipWorkspace.selectionActive();
        })
        .then([this]() { applyOrBeginDuplicate(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return matrixAvailable() &&
                !core_.sequencer.clipWorkspace.selectionActive();
        })
        .then([this]() { beginStopLayer(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return core_.sequencer.clipWorkspace.stopLayerActive;
        })
        .then([this]() { endStopLayer(); });
}

FLASHMEM void ClipWorkspaceHandler::beginHorizontalNavigation() {
    horizontal_navigation_gesture_.press();
}

FLASHMEM void ClipWorkspaceHandler::moveHorizontal(float delta) {
    const bool hasTurn = nav::hasTurnDelta(delta);
    if (!hasTurn ||
        (horizontal_navigation_gesture_.active() &&
         !horizontal_navigation_gesture_.turn(true))) {
        return;
    }
    auto& ui = core_.sequencer.clipWorkspace;
    ui.clearQuickControl();
    const uint16_t navigableTracks = ui.placementActive()
        ? seq::compatibleSequencerClipSelectionTrackMask(
            core_.sequencerTracks,
            ui.selectedClipMasks,
            ui.sourceTrack)
        : core_.currentSharedTrackEnabledMask();
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
    core_.sequencer.clipWorkspace.showQuickSelector();
}

FLASHMEM void ClipWorkspaceHandler::moveQuickSelector(float delta) {
    if (!quick_selector_gesture_.turn(nav::hasTurnDelta(delta))) return;
    core_.sequencer.clipWorkspace.moveQuickAction(nav::turnSteps(delta));
}

FLASHMEM void ClipWorkspaceHandler::releaseQuickSelector() {
    auto& ui = core_.sequencer.clipWorkspace;
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
    ui.armQuickProperty(core::time_compat::millis());
}

FLASHMEM void ClipWorkspaceHandler::openFocusedPattern() {
    if (!directPatternAvailable()) return;
    auto& ui = core_.sequencer.clipWorkspace;
    const seq::SequencerClipAddress address{
        ui.focusedTrack,
        ui.focusedSlot,
    };
    if (!core_.sequencerClips.isOccupied(address) &&
        !core_.createSequencerClip(address)) {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    if (!enterClip(address)) {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
    }
}

FLASHMEM void ClipWorkspaceHandler::move(float delta) {
    if (!matrixAvailable() || !nav::hasTurnDelta(delta) ||
        core_.sequencer.clipWorkspace.removePending()) {
        return;
    }
    auto& ui = core_.sequencer.clipWorkspace;
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
    auto& ui = core_.sequencer.clipWorkspace;
    if (!ui.quickPropertyArmed) return;
    const seq::SequencerClipAddress address{
        ui.quickTargetTrack,
        ui.quickTargetSlot,
    };
    const bool sceneTarget =
        ui.quickTargetFocus == seq::ClipWorkspaceFocus::SCENE;
    if ((!sceneTarget && !core_.sequencerClips.isOccupied(address)) ||
        (sceneTarget && !core_.sequencerClips.sceneUsed(ui.quickTargetSlot))) {
        ui.clearQuickControl();
        return;
    }

    auto behavior = sceneTarget
        ? core_.sequencerClips.sceneBehavior(ui.quickTargetSlot)
        : core_.sequencerClips.clipBehavior(address);
    if (!setNormalizedBehaviorValue(
            behavior,
            ui.quickAction,
            normalized)) {
        return;
    }

    const bool accepted = sceneTarget
        ? behavior == core_.sequencerClips.sceneBehavior(ui.quickTargetSlot) ||
            core_.setSequencerSceneBehavior(ui.quickTargetSlot, behavior)
        : behavior == core_.sequencerClips.clipBehavior(address) ||
            core_.setSequencerClipBehavior(address, behavior);
    if (accepted) {
        const uint32_t nowMs = core::time_compat::millis();
        showFeedback(seq::ClipWorkspaceFeedback::NONE);
        ui.showQuickFeedback(nowMs);
    } else {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        ui.clearQuickControl();
    }
}

FLASHMEM void ClipWorkspaceHandler::edit(float delta) {
    if (!editorAvailable() || !nav::hasTurnDelta(delta)) return;
    const int direction = nav::turnSteps(delta);
    auto& ui = core_.sequencer.clipWorkspace;
    if (ui.editor == seq::ClipWorkspaceEditor::SLOT_ACTION) {
        ui.moveSlotAction(direction);
    } else {
        ui.moveEditorField(direction);
    }
}

FLASHMEM void ClipWorkspaceHandler::editEditorValue(float normalized) {
    if (!editorAvailable()) return;
    auto& ui = core_.sequencer.clipWorkspace;
    if (ui.editor == seq::ClipWorkspaceEditor::SLOT_ACTION) return;

    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    const bool sceneTarget =
        ui.editor == seq::ClipWorkspaceEditor::SCENE_BEHAVIOR;
    auto behavior = sceneTarget
        ? core_.sequencerClips.sceneBehavior(ui.focusedSlot)
        : core_.sequencerClips.clipBehavior(address);
    if (!setNormalizedBehaviorValue(
            behavior,
            seq::clipWorkspaceQuickActionFor(ui.editorField),
            normalized)) {
        return;
    }

    const bool accepted = sceneTarget
        ? behavior == core_.sequencerClips.sceneBehavior(ui.focusedSlot) ||
            core_.setSequencerSceneBehavior(ui.focusedSlot, behavior)
        : behavior == core_.sequencerClips.clipBehavior(address) ||
            core_.setSequencerClipBehavior(address, behavior);
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
    core_.sequencer.clipWorkspace.moveViewport(direction);
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::selectFocused() {
    if (!matrixAvailable()) return;
    auto& ui = core_.sequencer.clipWorkspace;
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    if (ui.operation != seq::ClipWorkspaceOperation::BROWSE ||
        !core_.sequencerClips.isOccupied(address)) {
        return;
    }
    ui.beginSelection(address.track, address.slot);
}

FLASHMEM void ClipWorkspaceHandler::beginTrackSelection() {
    if (!trackHeaderAvailable() || navigation_workflow_ == nullptr) return;
    const uint8_t track = core_.sequencer.clipWorkspace.focusedTrack;
    if (!core_.sequencerTracks.isTrackEnabled(track)) return;
    core_.trackNavigation.previewAddSlot.set(false);
    core_.trackNavigation.syncPreviewTrack(track);
    navigation_focus_.set(core::state::StructureNavigationFocus::TRACK);
    navigation_workflow_->enterSelectionModeForCurrentFocus();
}

FLASHMEM void ClipWorkspaceHandler::launchVisible(uint8_t macroIndex) {
    if (!matrixAvailable() ||
        macroIndex >= seq::ClipWorkspaceUiState::MACRO_TARGET_COUNT) {
        return;
    }
    auto& ui = core_.sequencer.clipWorkspace;
    if (ui.placementActive()) return;
    const auto target = ui.macroTarget(macroIndex);
    if (ui.stopLayerActive) {
        if (target.focus == seq::ClipWorkspaceFocus::CLIP) {
            const auto telemetry = core_.sequencerClipLaunches.telemetry(
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
        if (core_.sequencerClips.isOccupied(address)) {
            ui.toggleSelection(address.track, address.slot);
        }
        return;
    }
    const auto kind = core_.sequencerClips.slotKind(address);
    bool accepted = false;
    if (kind == seq::SequencerLauncherSlotKind::CLIP) {
        accepted = core_.requestSequencerClipLaunch(address);
    } else if (kind == seq::SequencerLauncherSlotKind::STOP) {
        accepted = core_.requestSequencerTrackStop(
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
    auto& ui = core_.sequencer.clipWorkspace;
    if (ui.operation == seq::ClipWorkspaceOperation::SELECT) {
        const seq::SequencerClipAddress address{
            ui.focusedTrack,
            ui.focusedSlot,
        };
        if (ui.clipFocused() && core_.sequencerClips.isOccupied(address)) {
            ui.toggleSelection(address.track, address.slot);
        }
        return;
    }
    if (ui.trackHeaderFocused()) {
        syncNavigationFocus();
        if (!core_.sequencerTracks.isTrackEnabled(ui.focusedTrack)) {
            core_.trackNavigation.syncPreviewTrack(ui.focusedTrack);
            core_.trackNavigation.previewAddSlot.set(true);
            navigation_focus_.set(
                core::state::StructureNavigationFocus::TRACK
            );
            core_.sequencer.drumSequencer.openTypePicker(ui.focusedTrack);
            return;
        }
        toggleTrackMute();
        return;
    }
    if (ui.sceneFocused()) {
        if (ui.focusedSlot == lastNavigableScene() &&
            !core_.sequencerClips.sceneUsed(ui.focusedSlot)) {
            const uint16_t enabledMask = core_.currentSharedTrackEnabledMask();
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
    if (!core_.sequencerTracks.isTrackEnabled(address.track)) {
        ui.focus(address.track, 0U);
        core_.trackNavigation.syncPreviewTrack(address.track);
        core_.trackNavigation.previewAddSlot.set(true);
        navigation_focus_.set(
            core::state::StructureNavigationFocus::TRACK
        );
        core_.sequencer.drumSequencer.openTypePicker(address.track);
        return;
    }
    const auto kind = core_.sequencerClips.slotKind(address);
    if (kind == seq::SequencerLauncherSlotKind::STOP) {
        stopTrack(address.track, false);
        return;
    }
    if (kind == seq::SequencerLauncherSlotKind::EMPTY) {
        ui.openEditor(seq::ClipWorkspaceEditor::SLOT_ACTION);
        return;
    }
    showFeedback(
        core_.requestSequencerClipLaunch(address)
            ? seq::ClipWorkspaceFeedback::NONE
            : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::openFocusedEditor() {
    if (!matrixAvailable()) return;
    auto& ui = core_.sequencer.clipWorkspace;
    ui.clearQuickControl();
    if (ui.trackHeaderFocused()) {
        if (core_.sequencerTracks.isTrackEnabled(ui.focusedTrack) &&
            track_editor_handler_ != nullptr && selectTrack(ui.focusedTrack)) {
            (void)track_editor_handler_->openActiveTrack();
        }
        return;
    }
    if (ui.sceneFocused()) {
        const auto behavior = core_.sequencerClips.sceneBehavior(ui.focusedSlot);
        ui.openEditor(
            seq::ClipWorkspaceEditor::SCENE_BEHAVIOR,
            behavior.length,
            static_cast<uint8_t>(behavior.follow),
            static_cast<uint8_t>(behavior.quantization)
        );
        return;
    }
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    if (core_.sequencerClips.slotKind(address) ==
        seq::SequencerLauncherSlotKind::CLIP) {
        const auto behavior = core_.sequencerClips.clipBehavior(address);
        ui.openEditor(
            seq::ClipWorkspaceEditor::CLIP_BEHAVIOR,
            behavior.length,
            static_cast<uint8_t>(behavior.follow),
            static_cast<uint8_t>(behavior.quantization)
        );
    } else {
        ui.openEditor(seq::ClipWorkspaceEditor::SLOT_ACTION);
        ui.slotAction = core_.sequencerClips.isStop(address)
            ? seq::ClipWorkspaceSlotAction::CLEAR
            : seq::ClipWorkspaceSlotAction::CREATE_CLIP;
        ui.bump();
    }
}

FLASHMEM void ClipWorkspaceHandler::confirmSlotAction() {
    if (!editorAvailable() || core_.sequencer.clipWorkspace.editor !=
        seq::ClipWorkspaceEditor::SLOT_ACTION) {
        return;
    }
    auto& ui = core_.sequencer.clipWorkspace;
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    bool accepted = false;
    switch (ui.slotAction) {
        case seq::ClipWorkspaceSlotAction::CREATE_CLIP:
            accepted = core_.sequencerClips.isStop(address)
                ? false
                : core_.createSequencerClip(address);
            break;
        case seq::ClipWorkspaceSlotAction::SET_STOP:
            accepted = core_.setSequencerStopSlot(address, true);
            break;
        case seq::ClipWorkspaceSlotAction::CLEAR:
            accepted = core_.setSequencerStopSlot(address, false);
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
    return core_.sequencerClips.lastNavigableScene();
}

FLASHMEM void ClipWorkspaceHandler::launchScene(uint8_t slot) {
    showFeedback(
        core_.requestSequencerSceneLaunch(slot)
            ? seq::ClipWorkspaceFeedback::NONE
            : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::stopTrack(
    uint8_t track,
    bool immediate
) {
    showFeedback(
        core_.requestSequencerTrackStop(
            track,
            immediate
                ? seq::SequencerClipLaunchQuantization::IMMEDIATE
                : seq::SequencerClipLaunchQuantization::BAR
        ) ? seq::ClipWorkspaceFeedback::NONE
          : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::stopFocusedTrack() {
    const auto& ui = core_.sequencer.clipWorkspace;
    if (!ui.stopLayerActive || ui.sceneFocused() ||
        !core_.sequencerTracks.isTrackEnabled(ui.focusedTrack)) {
        return;
    }
    const auto telemetry = core_.sequencerClipLaunches.telemetry(
        ui.focusedTrack
    );
    if (!telemetry.stopped) stopTrack(ui.focusedTrack, false);
}

FLASHMEM void ClipWorkspaceHandler::beginStopLayer() {
    auto& ui = core_.sequencer.clipWorkspace;
    if (!matrixAvailable() || ui.selectionActive()) return;
    if (horizontal_navigation_gesture_.active()) {
        horizontal_navigation_gesture_.cancel();
    }
    ui.setStopLayer(true);
}

FLASHMEM void ClipWorkspaceHandler::endStopLayer() {
    core_.sequencer.clipWorkspace.setStopLayer(false);
}

FLASHMEM void ClipWorkspaceHandler::toggleTrackMute() {
    if (!trackHeaderAvailable()) return;
    const uint8_t track = core_.sequencer.clipWorkspace.focusedTrack;
    if (!core_.sequencerTracks.isTrackEnabled(track)) return;
    auto domain = core::state::project::
        ProjectTrackDomainServices::fromCoreState(core_);
    showFeedback(
        domain.setMuted(
            track,
            !core::state::project::projectTrackMuted(
                core_.projectTracks,
                track
            )
        ) ? seq::ClipWorkspaceFeedback::NONE
          : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::toggleTrackSolo() {
    if (!trackHeaderAvailable()) return;
    const uint8_t track = core_.sequencer.clipWorkspace.focusedTrack;
    if (!core_.sequencerTracks.isTrackEnabled(track)) return;
    auto domain = core::state::project::
        ProjectTrackDomainServices::fromCoreState(core_);
    showFeedback(
        domain.setSoloed(
            track,
            !core::state::project::projectTrackSoloed(
                core_.projectTracks,
                track
            )
        ) ? seq::ClipWorkspaceFeedback::NONE
          : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::showFeedback(
    seq::ClipWorkspaceFeedback feedback
) {
    core_.sequencer.clipWorkspace.setFeedback(
        feedback,
        core::time_compat::millis()
    );
}

FLASHMEM seq::SequencerClipAddress
ClipWorkspaceHandler::sourceAddress() const {
    const auto& ui = core_.sequencer.clipWorkspace;
    return {ui.sourceTrack, ui.sourceSlot};
}

FLASHMEM void ClipWorkspaceHandler::beginMove() {
    auto& ui = core_.sequencer.clipWorkspace;
    if (!matrixAvailable() ||
        ui.operation != seq::ClipWorkspaceOperation::SELECT) {
        return;
    }
    int8_t trackOffset = 0;
    int8_t slotOffset = 0;
    if (!seq::canMoveSequencerClipSelectionNow(
            core_.sequencerClips,
            core_.sequencerClipLaunches,
            ui.selectedClipMasks,
            core_.statusBar.playing.get()) ||
        !seq::firstSequencerClipSelectionMoveOffset(
            core_.sequencerClips,
            core_.sequencerTracks,
            core_.sequencer,
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
    auto& ui = core_.sequencer.clipWorkspace;
    if (!matrixAvailable()) return;
    if (ui.operation == seq::ClipWorkspaceOperation::SELECT) {
        if (ui.selectedCount() != 1U) {
            showFeedback(seq::ClipWorkspaceFeedback::FAILED);
            return;
        }
        const auto source = sourceAddress();
        seq::SequencerClipAddress destination{};
        if (!seq::firstSequencerClipTransferDestination(
                core_.sequencerClips,
                core_.sequencerTracks,
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
        ? core_.moveSequencerClips(
            ui.selectedClipMasks,
            static_cast<int8_t>(
                static_cast<int>(destination.track) - source.track),
            static_cast<int8_t>(
                static_cast<int>(destination.slot) - source.slot))
        : core_.duplicateSequencerClip(source, destination);
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
    auto& ui = core_.sequencer.clipWorkspace;
    if (!matrixAvailable() ||
        ui.operation != seq::ClipWorkspaceOperation::SELECT ||
        ui.selectedCount() != 1U) {
        return;
    }
    const auto source = sourceAddress();
    if (!core_.sequencerClips.isOccupied(source)) {
        showFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    ui.beginRemoveHold(nowMs);
}

FLASHMEM void ClipWorkspaceHandler::applyRemove() {
    auto& ui = core_.sequencer.clipWorkspace;
    if (!matrixAvailable() || !ui.removeHoldActive ||
        ui.operation != seq::ClipWorkspaceOperation::SELECT) {
        return;
    }
    const auto source = sourceAddress();
    const bool playing = core_.statusBar.playing.get();
    if (seq::canDeleteSequencerClip(
            core_.sequencerClips,
            core_.sequencerClipLaunches,
            source,
            playing)) {
        if (!core_.deleteSequencerClip(source)) {
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
            core_.sequencerClips,
            core_.sequencerClipLaunches,
            source,
            playing) ||
        !core_.requestSequencerTrackStop(
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
    core_.sequencer.clipWorkspace.clearRemoveHold();
}

FLASHMEM void ClipWorkspaceHandler::finishPendingRemove() {
    auto& ui = core_.sequencer.clipWorkspace;
    if (!ui.removePending()) return;
    const auto source = sourceAddress();
    const uint16_t trackBit = static_cast<uint16_t>(1U << source.track);
    if (core_.sequencerClipLaunches.references(source) ||
        (core_.sequencerClipLaunches.pendingTrackMask() & trackBit) != 0U ||
        (core_.sequencerClipLaunches.stagedTrackMask() & trackBit) != 0U) {
        return;
    }
    if (!core_.deleteSequencerClip(source)) {
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
    if (operationBackAvailable()) {
        (void)core_.sequencer.clipWorkspace.backOperation();
        syncNavigationFocus();
    }
}

FLASHMEM bool ClipWorkspaceHandler::enterClip(
    seq::SequencerClipAddress address
) {
    if (!selectClipForEditing(address)) return false;
    core_.sequencer.clipWorkspace.enterPattern(address.track, address.slot);
    return true;
}

FLASHMEM bool ClipWorkspaceHandler::selectClipForEditing(
    seq::SequencerClipAddress address
) {
    if (!selectTrack(address.track)) return false;
    if (!core_.sequencerClips.isResident(address) &&
        !core_.switchSequencerClipForEditing(address)) {
        return false;
    }
    navigation_focus_.set(
        core::state::StructureNavigationFocus::PAGE
    );
    return true;
}

FLASHMEM bool ClipWorkspaceHandler::selectTrack(uint8_t track) {
    const uint16_t enabledMask = core_.currentSharedTrackEnabledMask();
    if (core_.currentSharedActiveTrack() != track) {
        (void)core_.setSharedTrackState(enabledMask, track);
    }
    if (core_.currentSharedActiveTrack() != track) return false;
    core_.trackNavigation.previewAddSlot.set(false);
    core_.trackNavigation.syncPreviewTrack(track);
    return true;
}

FLASHMEM void ClipWorkspaceHandler::syncNavigationFocus() {
    const auto& workspace = core_.sequencer.clipWorkspace;
    const bool trackHeader = workspace.trackHeaderFocused();
    core_.trackNavigation.previewAddSlot.set(
        trackHeader &&
        !core_.sequencerTracks.isTrackEnabled(workspace.focusedTrack)
    );
    if (trackHeader) {
        core_.trackNavigation.syncPreviewTrack(workspace.focusedTrack);
    }
    navigation_focus_.set(
        trackHeader
            ? core::state::StructureNavigationFocus::TRACK
            : core::state::StructureNavigationFocus::PAGE
    );
}

}  // namespace core::handler
