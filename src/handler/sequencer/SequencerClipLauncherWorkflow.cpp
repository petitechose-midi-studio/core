#include "handler/sequencer/SequencerClipLauncherWorkflow.hpp"

#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler {

namespace seq = core::state::sequencer;

FLASHMEM SequencerClipLauncherWorkflow::SequencerClipLauncherWorkflow(
    StateRefs state
)
    : core_(state.core), navigation_focus_(state.navigationFocus),
      overlays_(state.overlays) {}

FLASHMEM bool SequencerClipLauncherWorkflow::launcherAvailable() const {
    return core_.sequencer.clipLauncher.launcherVisible() &&
        !overlays_.hasVisible() &&
        !core_.sequencer.drumSequencer.pickerVisible();
}

FLASHMEM bool SequencerClipLauncherWorkflow::patternBackAvailable() const {
    const auto& sequencer = core_.sequencer;
    return sequencer.clipLauncher.patternVisible() &&
        !overlays_.hasVisible() &&
        seq::isRootContentView(sequencer) &&
        navigation_focus_.get() ==
            core::state::StructureNavigationFocus::PAGE &&
        !sequencer.contextSelector.visible &&
        !sequencer.patternQuickControls.selecting.get() &&
        !sequencer.stepPropertyInlineSelector.selecting.get() &&
        !sequencer.stepContentSelector.selecting.get() &&
        !sequencer.structureUi.pageSelection.active.get() &&
        !sequencer.structureUi.stepSelection.active.get() &&
        !core_.trackNavigation.selection.active.get() &&
        !sequencer.drumSequencer.laneSelection.active &&
        !sequencer.drumSequencer.selectorVisible();
}

FLASHMEM bool SequencerClipLauncherWorkflow::operationBackAvailable() const {
    return launcherAvailable() && core_.sequencer.clipLauncher.selectionActive();
}

FLASHMEM bool SequencerClipLauncherWorkflow::focusedClipAvailable() const {
    const auto& ui = core_.sequencer.clipLauncher;
    return launcherAvailable() && !ui.selectionActive() &&
        core_.sequencerClips.isOccupied({ui.focusedTrack, ui.focusedSlot});
}

FLASHMEM void SequencerClipLauncherWorkflow::move(float delta) {
    if (!launcherAvailable() || !nav::hasTurnDelta(delta)) return;
    core_.sequencer.clipLauncher.move(nav::turnStep(delta));
}

FLASHMEM void SequencerClipLauncherWorkflow::selectFocused() {
    if (!launcherAvailable()) return;
    auto& ui = core_.sequencer.clipLauncher;
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    if (ui.operation != seq::SequencerClipLauncherOperation::BROWSE ||
        !core_.sequencerClips.isOccupied(address)) {
        return;
    }
    ui.beginSelection(address.track, address.slot);
}

FLASHMEM seq::SequencerClipAddress
SequencerClipLauncherWorkflow::visibleAddress(uint8_t macroIndex) const {
    const auto& ui = core_.sequencer.clipLauncher;
    return {
        static_cast<uint8_t>(ui.firstVisibleTrack +
            (macroIndex % seq::SequencerClipLauncherUiState::VISIBLE_TRACKS)),
        static_cast<uint8_t>(ui.firstVisibleSlot +
            (macroIndex / seq::SequencerClipLauncherUiState::VISIBLE_TRACKS)),
    };
}

FLASHMEM void SequencerClipLauncherWorkflow::launchVisible(uint8_t macroIndex) {
    if (!launcherAvailable() || macroIndex >= Config::MACRO_COUNT) return;
    auto& ui = core_.sequencer.clipLauncher;
    if (ui.placementActive()) return;
    const auto address = visibleAddress(macroIndex);
    ui.focus(address.track, address.slot);
    if (ui.operation == seq::SequencerClipLauncherOperation::SELECT) {
        if (core_.sequencerClips.isOccupied(address)) {
            ui.beginSelection(address.track, address.slot);
        }
        return;
    }
    if (!core_.sequencerClips.isOccupied(address)) return;
    ui.setFeedback(
        core_.requestSequencerClipLaunch(address)
            ? seq::SequencerClipLauncherFeedback::NONE
            : seq::SequencerClipLauncherFeedback::FAILED
    );
}

FLASHMEM void SequencerClipLauncherWorkflow::openFocused() {
    if (!launcherAvailable()) return;
    auto& ui = core_.sequencer.clipLauncher;
    const seq::SequencerClipAddress address{ui.focusedTrack, ui.focusedSlot};
    if (ui.operation == seq::SequencerClipLauncherOperation::SELECT) {
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
        ui.setFeedback(seq::SequencerClipLauncherFeedback::FAILED);
        return;
    }
    if (!enterClip(address)) {
        ui.setFeedback(seq::SequencerClipLauncherFeedback::FAILED);
    }
}

FLASHMEM seq::SequencerClipAddress
SequencerClipLauncherWorkflow::sourceAddress() const {
    const auto& ui = core_.sequencer.clipLauncher;
    return {ui.sourceTrack, ui.sourceSlot};
}

FLASHMEM uint8_t SequencerClipLauncherWorkflow::firstEmptySlotAfter(
    seq::SequencerClipAddress source
) const {
    for (uint8_t offset = 1U;
         offset < seq::SequencerClipLauncherUiState::SLOT_COUNT;
         ++offset) {
        const uint8_t slot = static_cast<uint8_t>(
            (source.slot + offset) %
            seq::SequencerClipLauncherUiState::SLOT_COUNT
        );
        if (!core_.sequencerClips.isOccupied({source.track, slot})) {
            return slot;
        }
    }
    return seq::SequencerClipGridState::INVALID_SLOT;
}

FLASHMEM void SequencerClipLauncherWorkflow::beginMove() {
    auto& ui = core_.sequencer.clipLauncher;
    if (!launcherAvailable() ||
        ui.operation != seq::SequencerClipLauncherOperation::SELECT) {
        return;
    }
    const auto source = sourceAddress();
    const uint8_t destination = firstEmptySlotAfter(source);
    if (core_.sequencerClipLaunches.references(source) ||
        destination == seq::SequencerClipGridState::INVALID_SLOT) {
        ui.setFeedback(seq::SequencerClipLauncherFeedback::FAILED);
        return;
    }
    ui.beginPlacement(
        seq::SequencerClipLauncherOperation::MOVE_DESTINATION,
        destination
    );
}

FLASHMEM void SequencerClipLauncherWorkflow::applyOrBeginDuplicate() {
    auto& ui = core_.sequencer.clipLauncher;
    if (!launcherAvailable()) return;
    if (ui.operation == seq::SequencerClipLauncherOperation::SELECT) {
        const auto source = sourceAddress();
        const uint8_t destination = firstEmptySlotAfter(source);
        if (destination == seq::SequencerClipGridState::INVALID_SLOT) {
            ui.setFeedback(seq::SequencerClipLauncherFeedback::FAILED);
            return;
        }
        ui.beginPlacement(
            seq::SequencerClipLauncherOperation::DUPLICATE_DESTINATION,
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
            seq::SequencerClipLauncherOperation::MOVE_DESTINATION
        ? core_.moveSequencerClip(source, destination)
        : core_.duplicateSequencerClip(source, destination);
    if (!changed) {
        ui.setFeedback(seq::SequencerClipLauncherFeedback::FAILED);
        return;
    }
    ui.completeOperation(
        destination.track,
        destination.slot,
        operation == seq::SequencerClipLauncherOperation::MOVE_DESTINATION
            ? seq::SequencerClipLauncherFeedback::MOVED
            : seq::SequencerClipLauncherFeedback::DUPLICATED
    );
}

FLASHMEM void SequencerClipLauncherWorkflow::beginRemove(uint32_t nowMs) {
    auto& ui = core_.sequencer.clipLauncher;
    if (!launcherAvailable() ||
        ui.operation != seq::SequencerClipLauncherOperation::SELECT) {
        return;
    }
    const auto source = sourceAddress();
    if (core_.sequencerClips.isResident(source) ||
        core_.sequencerClipLaunches.references(source)) {
        ui.setFeedback(seq::SequencerClipLauncherFeedback::FAILED);
        return;
    }
    ui.beginRemoveHold(nowMs);
}

FLASHMEM void SequencerClipLauncherWorkflow::applyRemove() {
    auto& ui = core_.sequencer.clipLauncher;
    if (!launcherAvailable() || !ui.removeHoldActive ||
        ui.operation != seq::SequencerClipLauncherOperation::SELECT) {
        return;
    }
    const auto source = sourceAddress();
    if (!core_.deleteSequencerClip(source)) {
        ui.clearRemoveHold();
        ui.setFeedback(seq::SequencerClipLauncherFeedback::FAILED);
        return;
    }
    ui.completeOperation(
        source.track,
        source.slot,
        seq::SequencerClipLauncherFeedback::REMOVED
    );
}

FLASHMEM void SequencerClipLauncherWorkflow::endRemove() {
    if (!launcherAvailable()) return;
    core_.sequencer.clipLauncher.clearRemoveHold();
}

FLASHMEM bool SequencerClipLauncherWorkflow::prepareFocusedEditor() {
    if (!focusedClipAvailable()) return false;
    const auto& ui = core_.sequencer.clipLauncher;
    return selectClipForEditing({ui.focusedTrack, ui.focusedSlot});
}

FLASHMEM void SequencerClipLauncherWorkflow::back() {
    if (operationBackAvailable()) {
        (void)core_.sequencer.clipLauncher.backOperation();
        return;
    }
    if (patternBackAvailable()) {
        (void)core_.sequencer.clipLauncher.returnToLauncher();
    }
}

FLASHMEM bool SequencerClipLauncherWorkflow::enterClip(
    seq::SequencerClipAddress address
) {
    if (!selectClipForEditing(address)) return false;
    core_.sequencer.clipLauncher.enterPattern(address.track, address.slot);
    return true;
}

FLASHMEM bool SequencerClipLauncherWorkflow::selectClipForEditing(
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
