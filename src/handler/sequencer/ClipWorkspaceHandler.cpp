#include "handler/sequencer/ClipWorkspaceHandler.hpp"

#include <algorithm>

#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/ProjectTrackEditorHandler.hpp"
#include "handler/sequencer/SequencerStructureNavigationWorkflow.hpp"

namespace core::handler {

namespace seq = core::state::sequencer;

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

    // The focus signal is shared with Macro and Project. An inactive Clips
    // matrix must never overwrite the context owned by the visible view.
    auto& workspace = core_.sequencer.clipWorkspace;
    workspace.updateQuickFeedback(core::time_compat::millis());
    if (core_.activeView.get() != core::ui::ViewType::CLIPS) {
        if (horizontal_navigation_gesture_.active()) {
            horizontal_navigation_gesture_.cancel();
        }
        if (quick_selector_gesture_.active()) quick_selector_gesture_.cancel();
        workspace.clearQuickControl();
        return;
    }
    if (workspace.quickPropertyArmed &&
        (workspace.focusedTrack != workspace.quickTargetTrack ||
         workspace.focusedSlot != workspace.quickTargetSlot ||
         !core_.sequencerClips.isOccupied({
             workspace.quickTargetTrack,
             workspace.quickTargetSlot,
         }))) {
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
        (!core_.sequencer.clipWorkspace.selectionActive() ||
         core_.sequencer.clipWorkspace.placementActive());
}

FLASHMEM bool ClipWorkspaceHandler::quickSelectorAvailable() const {
    return focusedClipAvailable() &&
        !core_.sequencer.clipWorkspace.quickSelectorVisible;
}

FLASHMEM bool ClipWorkspaceHandler::directPatternAvailable() const {
    return focusedClipAvailable();
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
    for (uint8_t index = 0U; index < Config::MACRO_COUNT; ++index) {
        buttons_.button(Config::MACRO_BUTTONS[index])
            .release()
            .scope(scope_id_)
            .when([this]() { return matrixAvailable(); })
            .then([this, index]() { launchVisible(index); });
    }

    buttons_.button(Config::ButtonID::NAV)
        .press()
        .scope(scope_id_)
        .priority(127)
        .when([this]() {
            return horizontalNavigationAvailable() &&
                !quick_selector_gesture_.active();
        })
        .then([this]() { beginHorizontalNavigation(); });

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
            else if (horizontal_navigation_gesture_.active()) {
                moveHorizontal(delta);
            } else if (editorAvailable()) edit(delta);
            else move(delta);
        });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return matrixAvailable() &&
                core_.sequencer.clipWorkspace.quickPropertyArmed;
        })
        .then([this](float delta) { editQuickProperty(delta); });

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
            else openFocusedEditor();
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
            if (editorAvailable()) applyEditor();
            else openFocused();
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
        .when([this]() { return directPatternAvailable(); })
        .then([this]() { openFocusedPattern(); });

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
        .release()
        .scope(scope_id_)
        .when([this]() {
            return matrixAvailable() &&
                core_.sequencer.clipWorkspace.clipFocused() &&
                !core_.sequencer.clipWorkspace.selectionActive();
        })
        .then([this]() { moveViewport(-1); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return matrixAvailable() &&
                core_.sequencer.clipWorkspace.clipFocused() &&
                !core_.sequencer.clipWorkspace.selectionActive();
        })
        .then([this]() { moveViewport(1); });
}

FLASHMEM void ClipWorkspaceHandler::beginHorizontalNavigation() {
    horizontal_navigation_gesture_.press();
}

FLASHMEM void ClipWorkspaceHandler::moveHorizontal(float delta) {
    if (!horizontal_navigation_gesture_.turn(nav::hasTurnDelta(delta))) return;
    auto& ui = core_.sequencer.clipWorkspace;
    ui.clearQuickControl();
    const uint16_t navigableTracks = ui.placementActive()
        ? seq::compatibleSequencerClipTrackMask(
            core_.sequencerClips,
            core_.sequencerTracks,
            sourceAddress())
        : core_.currentSharedTrackEnabledMask();
    ui.moveHorizontal(
        nav::turnStep(delta),
        navigableTracks
    );
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
    core_.sequencer.clipWorkspace.moveQuickAction(nav::turnStep(delta));
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
    if (!enterClip(address)) {
        ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
    }
}

FLASHMEM void ClipWorkspaceHandler::move(float delta) {
    if (!matrixAvailable() || !nav::hasTurnDelta(delta)) return;
    core_.sequencer.clipWorkspace.clearQuickControl();
    core_.sequencer.clipWorkspace.moveVertical(
        nav::turnStep(delta),
        lastNavigableScene()
    );
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::editQuickProperty(float delta) {
    if (!matrixAvailable() || !nav::hasTurnDelta(delta)) return;
    auto& ui = core_.sequencer.clipWorkspace;
    if (!ui.quickPropertyArmed) return;
    const seq::SequencerClipAddress address{
        ui.quickTargetTrack,
        ui.quickTargetSlot,
    };
    if (!core_.sequencerClips.isOccupied(address)) {
        ui.clearQuickControl();
        return;
    }

    auto behavior = core_.sequencerClips.clipBehavior(address);
    const int direction = nav::turnStep(delta);
    switch (ui.quickAction) {
        case seq::ClipWorkspaceQuickAction::LENGTH:
            behavior.length = static_cast<uint8_t>(std::clamp(
                static_cast<int>(behavior.length) + direction,
                0,
                static_cast<int>(seq::SequencerLauncherBehavior::MAX_LENGTH)
            ));
            break;
        case seq::ClipWorkspaceQuickAction::FOLLOW:
            behavior.follow = seq::stepSequencerLauncherFollowChoice(
                behavior.follow,
                direction
            );
            break;
        case seq::ClipWorkspaceQuickAction::QUANTIZE: {
            const int value = std::clamp(
                static_cast<int>(behavior.quantization) + direction,
                0,
                2
            );
            behavior.quantization = static_cast<
                seq::SequencerLauncherFollowQuantization>(value);
            break;
        }
        case seq::ClipWorkspaceQuickAction::EDIT:
        case seq::ClipWorkspaceQuickAction::COUNT:
            return;
    }

    if (behavior == core_.sequencerClips.clipBehavior(address) ||
        core_.setSequencerClipBehavior(address, behavior)) {
        ui.setFeedback(seq::ClipWorkspaceFeedback::NONE);
        ui.showQuickFeedback(core::time_compat::millis());
    } else {
        ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
        ui.clearQuickControl();
    }
}

FLASHMEM void ClipWorkspaceHandler::edit(float delta) {
    if (!editorAvailable() || !nav::hasTurnDelta(delta)) return;
    const int direction = nav::turnStep(delta);
    auto& ui = core_.sequencer.clipWorkspace;
    if (!buttons_.isPressed(Config::ButtonID::LEFT_CENTER)) {
        if (ui.editor == seq::ClipWorkspaceEditor::SLOT_ACTION) {
            ui.moveSlotAction(direction);
        } else {
            ui.moveEditorField(direction);
        }
        return;
    }

    release_latch_.arm(Config::ButtonID::LEFT_CENTER);

    uint8_t length = ui.editorLength;
    uint8_t followChoice = ui.editorFollowChoice;
    uint8_t quantization = ui.editorQuantization;
    switch (ui.editorField) {
        case seq::ClipWorkspaceBehaviorField::LENGTH:
            length = static_cast<uint8_t>(std::clamp(
                static_cast<int>(length) + (direction < 0 ? -1 : 1),
                0,
                static_cast<int>(seq::SequencerLauncherBehavior::MAX_LENGTH)
            ));
            break;
        case seq::ClipWorkspaceBehaviorField::FOLLOW:
            followChoice = static_cast<uint8_t>(
                seq::stepSequencerLauncherFollowChoice(
                    static_cast<seq::SequencerLauncherFollowChoice>(
                        followChoice
                    ),
                    direction
                )
            );
            break;
        case seq::ClipWorkspaceBehaviorField::QUANTIZE:
            quantization = static_cast<uint8_t>(std::clamp(
                static_cast<int>(quantization) +
                    (direction < 0 ? -1 : 1),
                0,
                2
            ));
            break;
        case seq::ClipWorkspaceBehaviorField::COUNT:
            break;
    }
    ui.setEditorValues(length, followChoice, quantization);
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

FLASHMEM seq::SequencerClipAddress
ClipWorkspaceHandler::visibleAddress(uint8_t macroIndex) const {
    const auto& ui = core_.sequencer.clipWorkspace;
    return {
        static_cast<uint8_t>(ui.firstVisibleTrack +
            (macroIndex % seq::ClipWorkspaceUiState::VISIBLE_TRACKS)),
        static_cast<uint8_t>(ui.macroBankFirstSlot() +
            (macroIndex / seq::ClipWorkspaceUiState::VISIBLE_TRACKS)),
    };
}

FLASHMEM void ClipWorkspaceHandler::launchVisible(uint8_t macroIndex) {
    if (!matrixAvailable() || macroIndex >= Config::MACRO_COUNT) return;
    auto& ui = core_.sequencer.clipWorkspace;
    if (ui.placementActive()) return;
    const auto address = visibleAddress(macroIndex);
    ui.focus(address.track, address.slot);
    syncNavigationFocus();
    if (ui.operation == seq::ClipWorkspaceOperation::SELECT) {
        if (core_.sequencerClips.isOccupied(address)) {
            ui.beginSelection(address.track, address.slot);
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
        ui.setFeedback(seq::ClipWorkspaceFeedback::NONE);
        return;
    }
    ui.setFeedback(accepted
        ? seq::ClipWorkspaceFeedback::NONE
        : seq::ClipWorkspaceFeedback::FAILED);
}

FLASHMEM void ClipWorkspaceHandler::openFocused() {
    if (!matrixAvailable()) return;
    auto& ui = core_.sequencer.clipWorkspace;
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
        stopTrack(ui.focusedTrack, true);
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
            ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
            return;
        }
        launchScene(ui.focusedSlot);
        return;
    }
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    if (ui.operation == seq::ClipWorkspaceOperation::SELECT) {
        if (core_.sequencerClips.isOccupied(address)) {
            ui.beginSelection(address.track, address.slot);
        }
        return;
    }
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
    ui.setFeedback(core_.requestSequencerClipLaunch(address)
        ? seq::ClipWorkspaceFeedback::NONE
        : seq::ClipWorkspaceFeedback::FAILED);
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

FLASHMEM void ClipWorkspaceHandler::applyEditor() {
    if (!editorAvailable()) return;
    auto& ui = core_.sequencer.clipWorkspace;
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    bool accepted = false;
    if (ui.editor == seq::ClipWorkspaceEditor::SLOT_ACTION) {
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
    } else {
        const seq::SequencerLauncherBehavior behavior{
            .length = ui.editorLength,
            .follow = static_cast<seq::SequencerLauncherFollowChoice>(
                ui.editorFollowChoice
            ),
            .quantization = static_cast<
                seq::SequencerLauncherFollowQuantization>(
                    std::min<uint8_t>(ui.editorQuantization, 2U))
        };
        if (ui.editor == seq::ClipWorkspaceEditor::CLIP_BEHAVIOR) {
            accepted = core_.sequencerClips.clipBehavior(address) == behavior ||
                core_.setSequencerClipBehavior(address, behavior);
        } else if (ui.editor == seq::ClipWorkspaceEditor::SCENE_BEHAVIOR) {
            accepted = core_.sequencerClips.sceneBehavior(ui.focusedSlot) ==
                    behavior ||
                core_.setSequencerSceneBehavior(ui.focusedSlot, behavior);
        }
    }
    if (accepted) {
        (void)ui.closeEditor();
        ui.setFeedback(seq::ClipWorkspaceFeedback::NONE);
    } else {
        ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
    }
}

FLASHMEM uint8_t ClipWorkspaceHandler::lastNavigableScene() const {
    return core_.sequencerClips.lastNavigableScene();
}

FLASHMEM void ClipWorkspaceHandler::launchScene(uint8_t slot) {
    auto& ui = core_.sequencer.clipWorkspace;
    ui.setFeedback(core_.requestSequencerSceneLaunch(slot)
        ? seq::ClipWorkspaceFeedback::NONE
        : seq::ClipWorkspaceFeedback::FAILED);
}

FLASHMEM void ClipWorkspaceHandler::stopTrack(
    uint8_t track,
    bool immediate
) {
    auto& ui = core_.sequencer.clipWorkspace;
    ui.setFeedback(core_.requestSequencerTrackStop(
        track,
        immediate
            ? seq::SequencerClipLaunchQuantization::IMMEDIATE
            : seq::SequencerClipLaunchQuantization::BAR
    ) ? seq::ClipWorkspaceFeedback::NONE
      : seq::ClipWorkspaceFeedback::FAILED);
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
    const auto source = sourceAddress();
    seq::SequencerClipAddress destination{};
    if (core_.sequencerClipLaunches.references(source) ||
        !seq::firstSequencerClipTransferDestination(
            core_.sequencerClips,
            core_.sequencerTracks,
            source,
            seq::SequencerClipStructureAction::MOVE,
            destination)) {
        ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    ui.beginPlacement(
        seq::ClipWorkspaceOperation::MOVE_DESTINATION,
        destination.track,
        destination.slot
    );
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::applyOrBeginDuplicate() {
    auto& ui = core_.sequencer.clipWorkspace;
    if (!matrixAvailable()) return;
    if (ui.operation == seq::ClipWorkspaceOperation::SELECT) {
        const auto source = sourceAddress();
        seq::SequencerClipAddress destination{};
        if (!seq::firstSequencerClipTransferDestination(
                core_.sequencerClips,
                core_.sequencerTracks,
                source,
                seq::SequencerClipStructureAction::DUPLICATE_CLIP,
                destination)) {
            ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
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
        ? core_.moveSequencerClip(source, destination)
        : core_.duplicateSequencerClip(source, destination);
    if (!changed) {
        ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    ui.completeOperation(
        destination.track,
        destination.slot,
        operation == seq::ClipWorkspaceOperation::MOVE_DESTINATION
            ? seq::ClipWorkspaceFeedback::MOVED
            : seq::ClipWorkspaceFeedback::DUPLICATED
    );
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::beginRemove(uint32_t nowMs) {
    auto& ui = core_.sequencer.clipWorkspace;
    if (!matrixAvailable() ||
        ui.operation != seq::ClipWorkspaceOperation::SELECT) {
        return;
    }
    const auto source = sourceAddress();
    if (core_.sequencerClips.isResident(source) ||
        core_.sequencerClipLaunches.references(source)) {
        ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
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
    if (!core_.deleteSequencerClip(source)) {
        ui.clearRemoveHold();
        ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    ui.completeOperation(
        source.track,
        source.slot,
        seq::ClipWorkspaceFeedback::REMOVED
    );
    syncNavigationFocus();
}

FLASHMEM void ClipWorkspaceHandler::endRemove() {
    if (!matrixAvailable()) return;
    core_.sequencer.clipWorkspace.clearRemoveHold();
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
