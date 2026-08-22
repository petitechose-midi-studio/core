#include "handler/sequencer/ClipWorkspaceHandler.hpp"

#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerPatternEditorHandler.hpp"

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

FLASHMEM void ClipWorkspaceHandler::attachPatternEditorHandler(
    SequencerPatternEditorHandler& handler
) {
    pattern_editor_handler_ = &handler;
}

FLASHMEM bool ClipWorkspaceHandler::matrixAvailable() const {
    return core_.sequencer.clipWorkspace.matrixVisible() &&
        !overlays_.hasVisible() &&
        !core_.sequencer.drumSequencer.pickerVisible();
}

FLASHMEM bool ClipWorkspaceHandler::operationBackAvailable() const {
    return matrixAvailable() && core_.sequencer.clipWorkspace.selectionActive();
}

FLASHMEM bool ClipWorkspaceHandler::focusedClipAvailable() const {
    const auto& ui = core_.sequencer.clipWorkspace;
    return matrixAvailable() && !ui.selectionActive() &&
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

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return matrixAvailable(); })
        .then([this](float delta) { move(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return focusedClipAvailable(); })
        .then([this]() { selectFocused(); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() { return matrixAvailable(); })
        .then([this]() { openFocused(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .priority(120)
        .when([this]() {
            return matrixAvailable() &&
                (core_.sequencer.clipWorkspace.selectionActive() ||
                 focusedClipAvailable());
        })
        .then([this]() {
            if (core_.sequencer.clipWorkspace.selectionActive()) {
                beginMove();
                return;
            }
            if (pattern_editor_handler_ != nullptr && prepareFocusedEditor()) {
                (void)pattern_editor_handler_->openRegionFromCurrentPage();
            }
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .priority(120)
        .when([this]() { return operationBackAvailable(); })
        .then([this]() { back(); });

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
                !core_.sequencer.clipWorkspace.selectionActive();
        })
        .then([this]() { moveViewport(-1); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return matrixAvailable() &&
                !core_.sequencer.clipWorkspace.selectionActive();
        })
        .then([this]() { moveViewport(1); });
}

FLASHMEM void ClipWorkspaceHandler::move(float delta) {
    if (!matrixAvailable() || !nav::hasTurnDelta(delta)) return;
    core_.sequencer.clipWorkspace.move(nav::turnStep(delta));
}

FLASHMEM void ClipWorkspaceHandler::moveViewport(int direction) {
    if (!matrixAvailable() || direction == 0) return;
    core_.sequencer.clipWorkspace.moveViewport(direction);
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

FLASHMEM seq::SequencerClipAddress
ClipWorkspaceHandler::visibleAddress(uint8_t macroIndex) const {
    const auto& ui = core_.sequencer.clipWorkspace;
    return {
        static_cast<uint8_t>(ui.firstVisibleTrack +
            (macroIndex % seq::ClipWorkspaceUiState::VISIBLE_TRACKS)),
        static_cast<uint8_t>(ui.firstVisibleSlot +
            (macroIndex / seq::ClipWorkspaceUiState::VISIBLE_TRACKS)),
    };
}

FLASHMEM void ClipWorkspaceHandler::launchVisible(uint8_t macroIndex) {
    if (!matrixAvailable() || macroIndex >= Config::MACRO_COUNT) return;
    auto& ui = core_.sequencer.clipWorkspace;
    if (ui.placementActive()) return;
    const auto address = visibleAddress(macroIndex);
    ui.focus(address.track, address.slot);
    if (ui.operation == seq::ClipWorkspaceOperation::SELECT) {
        if (core_.sequencerClips.isOccupied(address)) {
            ui.beginSelection(address.track, address.slot);
        }
        return;
    }
    if (!core_.sequencerClips.isOccupied(address)) return;
    ui.setFeedback(
        core_.requestSequencerClipLaunch(address)
            ? seq::ClipWorkspaceFeedback::NONE
            : seq::ClipWorkspaceFeedback::FAILED
    );
}

FLASHMEM void ClipWorkspaceHandler::openFocused() {
    if (!matrixAvailable()) return;
    auto& ui = core_.sequencer.clipWorkspace;
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
    if (!core_.sequencerClips.isOccupied(address) &&
        !core_.createSequencerClip(address)) {
        ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    if (!enterClip(address)) {
        ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
    }
}

FLASHMEM seq::SequencerClipAddress
ClipWorkspaceHandler::sourceAddress() const {
    const auto& ui = core_.sequencer.clipWorkspace;
    return {ui.sourceTrack, ui.sourceSlot};
}

FLASHMEM uint8_t ClipWorkspaceHandler::firstEmptySlotAfter(
    seq::SequencerClipAddress source
) const {
    for (uint8_t offset = 1U;
         offset < seq::ClipWorkspaceUiState::SLOT_COUNT;
         ++offset) {
        const uint8_t slot = static_cast<uint8_t>(
            (source.slot + offset) %
            seq::ClipWorkspaceUiState::SLOT_COUNT
        );
        if (!core_.sequencerClips.isOccupied({source.track, slot})) {
            return slot;
        }
    }
    return seq::SequencerClipGridState::INVALID_SLOT;
}

FLASHMEM void ClipWorkspaceHandler::beginMove() {
    auto& ui = core_.sequencer.clipWorkspace;
    if (!matrixAvailable() ||
        ui.operation != seq::ClipWorkspaceOperation::SELECT) {
        return;
    }
    const auto source = sourceAddress();
    const uint8_t destination = firstEmptySlotAfter(source);
    if (core_.sequencerClipLaunches.references(source) ||
        destination == seq::SequencerClipGridState::INVALID_SLOT) {
        ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
        return;
    }
    ui.beginPlacement(
        seq::ClipWorkspaceOperation::MOVE_DESTINATION,
        destination
    );
}

FLASHMEM void ClipWorkspaceHandler::applyOrBeginDuplicate() {
    auto& ui = core_.sequencer.clipWorkspace;
    if (!matrixAvailable()) return;
    if (ui.operation == seq::ClipWorkspaceOperation::SELECT) {
        const auto source = sourceAddress();
        const uint8_t destination = firstEmptySlotAfter(source);
        if (destination == seq::SequencerClipGridState::INVALID_SLOT) {
            ui.setFeedback(seq::ClipWorkspaceFeedback::FAILED);
            return;
        }
        ui.beginPlacement(
            seq::ClipWorkspaceOperation::DUPLICATE_DESTINATION,
            destination
        );
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
}

FLASHMEM void ClipWorkspaceHandler::endRemove() {
    if (!matrixAvailable()) return;
    core_.sequencer.clipWorkspace.clearRemoveHold();
}

FLASHMEM bool ClipWorkspaceHandler::prepareFocusedEditor() {
    if (!focusedClipAvailable()) return false;
    const auto& ui = core_.sequencer.clipWorkspace;
    return selectClipForEditing({ui.focusedTrack, ui.focusedSlot});
}

FLASHMEM void ClipWorkspaceHandler::back() {
    if (operationBackAvailable()) {
        (void)core_.sequencer.clipWorkspace.backOperation();
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
    const uint16_t enabledMask = core_.currentSharedTrackEnabledMask();
    if (core_.currentSharedActiveTrack() != address.track) {
        (void)core_.setSharedTrackState(enabledMask, address.track);
    }
    if (core_.currentSharedActiveTrack() != address.track) return false;
    if (!core_.sequencerClips.isResident(address) &&
        !core_.switchSequencerClipForEditing(address)) {
        return false;
    }
    core_.trackNavigation.previewAddSlot.set(false);
    core_.trackNavigation.syncPreviewTrack(address.track);
    navigation_focus_.set(
        core::state::StructureNavigationFocus::PAGE
    );
    return true;
}

}  // namespace core::handler
