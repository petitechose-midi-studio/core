#include "handler/sequencer/SequencerStructureTrackTransferTransaction.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace {

using Graph = oc::note::sequencer::StepSequencerGraph;
using GraphPtr = PreparedSequencerTrackTransfer::GraphPtr;
using PatternSnapshot = core::state::sequencer::SequencerPatternSnapshot;
using TrackBank = core::state::sequencer::SequencerTrackBankState;

struct SourcePayload {
    const PatternSnapshot* snapshot = nullptr;
    const Graph* graph = nullptr;
};

FLASHMEM SourcePayload sourcePayload(
    const core::state::StructureClipboardState& clipboard,
    const core::state::ClipboardTransferPlanEntry& entry
) {
    if (clipboard.kind.get() == core::state::StructureClipboardKind::SEQUENCER_TRACK) {
        if (entry.clipboardIndex != 0 ||
            clipboard.sequencerTrackSource != entry.sourceTrack) {
            return {};
        }
        return {&clipboard.sequencerTrack, clipboard.sequencerGraph.get()};
    }

    if (clipboard.kind.get() !=
        core::state::StructureClipboardKind::SEQUENCER_TRACK_SELECTION) {
        return {};
    }
    const auto* selection = clipboard.sequencerTrackSelection.get();
    if (selection == nullptr || entry.clipboardIndex >= selection->count) return {};
    const auto& source = selection->tracks[entry.clipboardIndex];
    if (!source.valid || source.sourceTrack != entry.sourceTrack) return {};
    return {&source.snapshot, source.graph.get()};
}

FLASHMEM bool copyGraphIntoReservedStorage(GraphPtr& destination, const Graph* source) {
    if (source == nullptr || !source->enabled) {
        destination.reset();
        return true;
    }
    if (!destination) return false;
    *destination = *source;
    return true;
}

FLASHMEM uint8_t historyMidiChannel(uint8_t destinationChannel) {
    // Preserve the route identity exactly, including the current >15 sentinel
    // used for an explicitly unassigned destination.
    return destinationChannel;
}

FLASHMEM uint8_t sanitizedLength(uint8_t length) {
    return length == 0 || length > core::state::sequencer::SequencerPatternState::MAX_STEPS
        ? core::state::sequencer::SequencerPatternState::DEFAULT_LENGTH
        : length;
}

FLASHMEM bool sameStableProjection(
    const core::state::ClipboardTransferPlan& prepared,
    const core::state::ClipboardTransferPlan& live
) {
    if (!live.canCommit() ||
        prepared.payloadKind != live.payloadKind ||
        prepared.clipboardRevision != live.clipboardRevision ||
        prepared.sourceCount != live.sourceCount ||
        prepared.count != live.count ||
        prepared.sourceMask != live.sourceMask ||
        prepared.targetMask != live.targetMask ||
        prepared.createMask != live.createMask ||
        prepared.overwriteMask != live.overwriteMask ||
        prepared.firstTarget != live.firstTarget ||
        prepared.lastTarget != live.lastTarget) {
        return false;
    }

    for (uint8_t i = 0; i < prepared.count; ++i) {
        const auto& lhs = prepared.entries[i];
        const auto& rhs = live.entries[i];
        if (lhs.clipboardIndex != rhs.clipboardIndex ||
            lhs.sourceTrack != rhs.sourceTrack ||
            lhs.targetTrack != rhs.targetTrack ||
            lhs.targetMuted != rhs.targetMuted ||
            lhs.targetKind != rhs.targetKind) {
            return false;
        }
    }
    return true;
}

FLASHMEM void updateCommitTimeRoutes(
    PreparedSequencerTrackTransfer& prepared,
    const core::state::ClipboardTransferPlan& livePlan
) {
    for (uint8_t i = 0; i < prepared.plan.count; ++i) {
        auto& destination = prepared.plan.entries[i];
        const auto& live = livePlan.entries[i];
        destination.targetMidiChannel = live.targetMidiChannel;
        destination.targetRouteValid = live.targetRouteValid;
        prepared.history->after.tracks[destination.targetTrack].flat.midiChannel =
            historyMidiChannel(live.targetMidiChannel);
    }
    prepared.plan.availability = livePlan.availability;
    prepared.plan.reason = livePlan.reason;
}

FLASHMEM SequencerTrackTransferResult resultFromPrepared(
    SequencerTrackTransferStatus status,
    const PreparedSequencerTrackTransfer& prepared
) {
    return {status, prepared.plan};
}

}  // namespace

FLASHMEM PreparedSequencerTrackTransfer prepareSequencerTrackTransfer(
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    const SharedTrackDomainServices& sharedTracks,
    const SequencerHistoryDomainServices& history,
    uint8_t targetTrack,
    uint16_t pendingTrackMask
) {
    PreparedSequencerTrackTransfer prepared;
    prepared.pendingTrackMask = pendingTrackMask;
    prepared.plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        targetTrack,
        pendingTrackMask,
        &sequencer
    );
    if (!prepared.plan.canCommit()) return prepared;

    if (!sharedTracks.canPublishPreparedSequencerState()) {
        prepared.status = SequencerTrackTransferStatus::PUBLICATION_UNAVAILABLE;
        prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
        return prepared;
    }

    prepared.initialEnabledMask = tracks.currentEnabledMask();
    prepared.initialMutedMask = tracks.currentMutedMask();
    prepared.previousActiveTrack = tracks.activeTrackIndex();
    prepared.previousActiveMidiChannel = sequencer.pattern.midiChannel.get();
    if (sharedTracks.enabledMask() != prepared.initialEnabledMask ||
        sharedTracks.activeTrack() != prepared.previousActiveTrack) {
        prepared.status = SequencerTrackTransferStatus::INCONSISTENT_STATE;
        prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
        return prepared;
    }

    prepared.nextEnabledMask = static_cast<uint16_t>(
        prepared.initialEnabledMask | prepared.plan.targetMask
    );
    prepared.historyMask = static_cast<uint16_t>(
        prepared.plan.targetMask |
        sequencerStructureHistoryTrackBit(prepared.previousActiveTrack)
    );
    prepared.history = captureSequencerTrackStructureHistoryBefore(
        tracks,
        sequencer,
        prepared.historyMask
    );
    if (!prepared.history) {
        prepared.status = SequencerTrackTransferStatus::ALLOCATION_UNAVAILABLE;
        prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
        prepared.plan.reason = core::state::ClipboardTransferReason::ALLOCATION_UNAVAILABLE;
        return prepared;
    }
    prepared.history->preserveDestinationBindingsMask = prepared.plan.targetMask;

    auto& after = prepared.history->after;
    after.enabledMask = prepared.nextEnabledMask;
    after.mutedMask = prepared.initialMutedMask;
    after.activeTrack = prepared.plan.firstTarget;
    after.capturedTrackMask = prepared.history->before.capturedTrackMask;

    const SourcePayload firstSource = sourcePayload(clipboard, prepared.plan.entries[0]);
    if (firstSource.snapshot == nullptr) {
        prepared.status = SequencerTrackTransferStatus::STALE;
        return prepared;
    }
    const uint8_t firstLength = sanitizedLength(firstSource.snapshot->length);
    after.focusedStep = static_cast<uint8_t>(std::min<uint16_t>(
        prepared.history->before.focusedStep,
        static_cast<uint16_t>(firstLength - 1U)
    ));
    after.page = static_cast<uint8_t>(
        after.focusedStep / core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );

    const uint16_t previousActiveBit = sequencerStructureHistoryTrackBit(
        prepared.previousActiveTrack
    );
    if ((prepared.plan.targetMask & previousActiveBit) == 0) {
        const auto& beforeActive =
            prepared.history->before.tracks[prepared.previousActiveTrack];
        auto& afterActive = after.tracks[prepared.previousActiveTrack];
        afterActive.flat = beforeActive.flat;
        afterActive.focusedStep = after.focusedStep;
        if (!copyGraphIntoReservedStorage(afterActive.graph, beforeActive.graph.get()) ||
            !core::state::cloneSequencerGraph(
                prepared.outgoingActiveGraph,
                beforeActive.graph.get()
            )) {
            prepared.status = SequencerTrackTransferStatus::ALLOCATION_UNAVAILABLE;
            prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
            prepared.plan.reason = core::state::ClipboardTransferReason::ALLOCATION_UNAVAILABLE;
            return prepared;
        }
    }

    for (uint8_t i = 0; i < prepared.plan.count; ++i) {
        const auto& destination = prepared.plan.entries[i];
        const SourcePayload source = sourcePayload(clipboard, destination);
        if (source.snapshot == nullptr) {
            prepared.status = SequencerTrackTransferStatus::STALE;
            return prepared;
        }

        auto& afterTrack = after.tracks[destination.targetTrack];
        afterTrack.flat = *source.snapshot;
        afterTrack.flat.midiChannel = historyMidiChannel(destination.targetMidiChannel);
        afterTrack.focusedStep = after.focusedStep;
        if (!copyGraphIntoReservedStorage(afterTrack.graph, source.graph) ||
            !core::state::cloneSequencerGraph(prepared.bankGraphs[i], source.graph)) {
            prepared.status = SequencerTrackTransferStatus::ALLOCATION_UNAVAILABLE;
            prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
            prepared.plan.reason = core::state::ClipboardTransferReason::ALLOCATION_UNAVAILABLE;
            return prepared;
        }
    }

    if (!core::state::cloneSequencerGraph(prepared.editorGraph, firstSource.graph)) {
        prepared.status = SequencerTrackTransferStatus::ALLOCATION_UNAVAILABLE;
        prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
        prepared.plan.reason = core::state::ClipboardTransferReason::ALLOCATION_UNAVAILABLE;
        return prepared;
    }

    prepared.history->descriptor = makeSequencerTrackStructureHistoryDescriptor(
        prepared.history->before,
        prepared.history->after
    );
    if (core::state::sequencer::sameMusicalHistoryStructureSnapshot(
            prepared.history->before,
            prepared.history->after
        )) {
        prepared.status = SequencerTrackTransferStatus::NO_CHANGE;
        return prepared;
    }
    if (!history.canRecordStructure(*prepared.history)) {
        prepared.status = SequencerTrackTransferStatus::HISTORY_UNAVAILABLE;
        prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
        prepared.plan.reason = core::state::ClipboardTransferReason::HISTORY_UNAVAILABLE;
        return prepared;
    }

    prepared.status = SequencerTrackTransferStatus::READY;
    return prepared;
}

FLASHMEM SequencerTrackTransferResult commitPreparedSequencerTrackTransfer(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    const SharedTrackDomainServices& sharedTracks,
    const SequencerHistoryDomainServices& history,
    PreparedSequencerTrackTransfer prepared
) {
    if (!prepared.ready()) {
        return resultFromPrepared(prepared.status, prepared);
    }
    if (tracks.currentEnabledMask() != prepared.initialEnabledMask ||
        tracks.currentMutedMask() != prepared.initialMutedMask ||
        tracks.activeTrackIndex() != prepared.previousActiveTrack ||
        sharedTracks.enabledMask() != prepared.initialEnabledMask ||
        sharedTracks.activeTrack() != prepared.previousActiveTrack) {
        return resultFromPrepared(SequencerTrackTransferStatus::STALE, prepared);
    }

    const uint16_t previousActiveBit = sequencerStructureHistoryTrackBit(
        prepared.previousActiveTrack
    );
    if ((prepared.plan.targetMask & previousActiveBit) == 0 &&
        sequencer.pattern.midiChannel.get() != prepared.previousActiveMidiChannel) {
        // The outgoing editor route is part of the before snapshot. A change
        // between prepare and commit requires a fresh preparation so Undo can
        // never restore an older binding.
        return resultFromPrepared(SequencerTrackTransferStatus::STALE, prepared);
    }

    const auto livePlan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        prepared.plan.firstTarget,
        prepared.pendingTrackMask,
        &sequencer
    );
    if (!sameStableProjection(prepared.plan, livePlan)) {
        return resultFromPrepared(SequencerTrackTransferStatus::STALE, prepared);
    }

    // Destination routing is an instant-T binding. Refresh only those fields
    // after validating that the payload, target topology and mute ownership did
    // not change since preparation, then repeat the exact history admission.
    updateCommitTimeRoutes(prepared, livePlan);
    if (core::state::sequencer::sameMusicalHistoryStructureSnapshot(
            prepared.history->before,
            prepared.history->after
        )) {
        return resultFromPrepared(SequencerTrackTransferStatus::NO_CHANGE, prepared);
    }
    if (!history.canRecordStructure(*prepared.history)) {
        prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
        prepared.plan.reason = core::state::ClipboardTransferReason::HISTORY_UNAVAILABLE;
        return resultFromPrepared(SequencerTrackTransferStatus::HISTORY_UNAVAILABLE, prepared);
    }

    if ((prepared.plan.targetMask & previousActiveBit) == 0) {
        auto& outgoing = tracks.track(prepared.previousActiveTrack);
        outgoing.midiChannel.set(prepared.previousActiveMidiChannel);
        core::state::sequencer::installTrackContentSnapshotWithOwnedGraph(
            outgoing,
            prepared.history->before.tracks[prepared.previousActiveTrack].flat,
            std::move(prepared.outgoingActiveGraph)
        );
    }

    for (uint8_t i = 0; i < prepared.plan.count; ++i) {
        const auto& destination = prepared.plan.entries[i];
        const SourcePayload source = sourcePayload(clipboard, destination);
        // Payload identity was checked immediately above and the clipboard is
        // immutable during this synchronous commit.
        auto& target = tracks.track(destination.targetTrack);
        target.midiChannel.set(destination.targetMidiChannel);
        core::state::sequencer::installTrackContentSnapshotWithOwnedGraph(
            target,
            *source.snapshot,
            std::move(prepared.bankGraphs[i])
        );
    }

    const auto& firstDestination = prepared.plan.entries[0];
    const SourcePayload firstSource = sourcePayload(clipboard, firstDestination);
    sequencer.pattern.midiChannel.set(firstDestination.targetMidiChannel);
    core::state::sequencer::installTrackContentSnapshotToEditorWithOwnedGraph(
        sequencer,
        *firstSource.snapshot,
        std::move(prepared.editorGraph)
    );
    core::state::sequencer::resetTransientTrackState(sequencer);
    sequencer.focusedStep.set(prepared.history->after.focusedStep);
    sequencer.page.set(prepared.history->after.page);

    sharedTracks.publishPreparedSequencerState(
        prepared.nextEnabledMask,
        prepared.plan.firstTarget
    );
    history.recordPreparedStructure(std::move(prepared.history));
    return resultFromPrepared(SequencerTrackTransferStatus::APPLIED, prepared);
}

FLASHMEM SequencerTrackTransferResult executeSequencerTrackTransfer(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    const SharedTrackDomainServices& sharedTracks,
    const SequencerHistoryDomainServices& history,
    uint8_t targetTrack,
    uint16_t pendingTrackMask
) {
    auto prepared = prepareSequencerTrackTransfer(
        tracks,
        sequencer,
        clipboard,
        sharedTracks,
        history,
        targetTrack,
        pendingTrackMask
    );
    return commitPreparedSequencerTrackTransfer(
        tracks,
        sequencer,
        clipboard,
        sharedTracks,
        history,
        std::move(prepared)
    );
}

}  // namespace core::handler
