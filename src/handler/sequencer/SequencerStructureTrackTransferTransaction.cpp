#include "handler/sequencer/SequencerStructureTrackTransferTransaction.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/modulation/ProjectControlStructureTransferOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

FLASHMEM PreparedSequencerTrackTransfer::~PreparedSequencerTrackTransfer() {}

namespace {

using Graph = oc::note::sequencer::StepSequencerGraph;
using GraphPtr = PreparedSequencerTrackTransfer::GraphPtr;
using PatternSnapshot = core::state::sequencer::SequencerPatternSnapshot;
using TrackBank = core::state::sequencer::SequencerTrackBankState;

struct SourcePayload {
    const PatternSnapshot* snapshot = nullptr;
    const Graph* graph = nullptr;
    const core::state::sequencer::SequencerCcLaneBank* ccLanes = nullptr;
};

FLASHMEM SourcePayload sourcePayload(
    const core::state::StructureClipboardState& clipboard,
    const core::state::ClipboardTransferPlanEntry& entry
) {
    if (clipboard.kind.get() == core::state::StructureClipboardKind::SEQUENCER_TRACK) {
        if (entry.clipboardIndex != 0U ||
            clipboard.sequencerTrackSource != entry.sourceTrack) {
            return {};
        }
        return {
            &clipboard.sequencerTrack,
            clipboard.sequencerGraph.get(),
            clipboard.sequencerCcLanes.get(),
        };
    }

    if (clipboard.kind.get() !=
        core::state::StructureClipboardKind::SEQUENCER_TRACK_SELECTION) {
        return {};
    }
    const auto* selection = clipboard.sequencerTrackSelection.get();
    if (selection == nullptr ||
        entry.clipboardIndex >= selection->count) {
        return {};
    }
    const auto& source =
        selection->tracks[entry.clipboardIndex];
    if (!source.valid || source.sourceTrack != entry.sourceTrack) {
        return {};
    }
    return {
        &source.snapshot,
        source.graph.get(),
        source.ccLanes.get(),
    };
}

FLASHMEM const core::state::macro::MacroTrackData*
sourceMacroTrack(
    const core::state::StructureClipboardState& clipboard,
    const core::state::ClipboardTransferPlanEntry& entry
) {
    if (clipboard.kind.get() !=
        core::state::StructureClipboardKind::
            SEQUENCER_TRACK_SELECTION) {
        return nullptr;
    }
    const auto* selection =
        clipboard.sequencerTrackSelection.get();
    if (selection == nullptr ||
        entry.clipboardIndex >= selection->count) {
        return nullptr;
    }
    const auto& source =
        selection->tracks[entry.clipboardIndex];
    return source.valid &&
            source.sourceTrack == entry.sourceTrack
        ? &source.macroTrack
        : nullptr;
}

FLASHMEM bool prepareMacroStructureTransfer(
    const core::state::macro::MacroPagesState& pages,
    const core::state::StructureClipboardState& clipboard,
    PreparedSequencerTrackTransfer& prepared
) {
    const auto* selection =
        clipboard.sequencerTrackSelection.get();
    if (clipboard.kind.get() !=
            core::state::StructureClipboardKind::
                SEQUENCER_TRACK_SELECTION ||
        selection == nullptr || !selection->projectControl ||
        !core::state::sequencer::
            captureMacroTrackStructureHistoryBefore(
                pages,
                prepared.historyMask,
                *prepared.history
            )) {
        return false;
    }
    auto* payload = prepared.history->macroStructure.get();
    if (payload == nullptr || !payload->beforeControl ||
        !payload->afterControl) {
        return false;
    }
    for (uint8_t track = 0U;
         track < core::state::macro::TRACK_COUNT;
         ++track) {
        if ((payload->capturedTrackMask &
             sequencerStructureHistoryTrackBit(track)) == 0U) {
            continue;
        }
        payload->afterTracks[track] =
            payload->beforeTracks[track];
    }

    core::state::modulation::ProjectControlStructureTransferPlan
        transfer{};
    transfer.count = prepared.plan.count;
    for (uint8_t index = 0U;
         index < prepared.plan.count;
         ++index) {
        const auto& destination = prepared.plan.entries[index];
        const auto* macroTrack =
            sourceMacroTrack(clipboard, destination);
        if (macroTrack == nullptr) return false;
        payload->afterTracks[destination.targetTrack] =
            *macroTrack;
        transfer.entries[index] = {
            .sourceTrack = destination.sourceTrack,
            .targetTrack = destination.targetTrack,
            .wholeTrack = true,
        };
    }
    *payload->afterControl = *payload->beforeControl;
    if (!core::state::modulation::
            replaceProjectControlStructureInDomain(
                *payload->afterControl,
                *selection->projectControl,
                transfer
            )) {
        return false;
    }
    payload->afterCaptured = true;
    return true;
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

FLASHMEM uint8_t sanitizedLength(uint8_t length) {
    return length == 0 || length > core::state::sequencer::SequencerPatternState::MAX_STEPS
        ? core::state::sequencer::SequencerPatternState::DEFAULT_LENGTH
        : length;
}

FLASHMEM void rebaseIncomingCcLaneLifecycles(
    core::state::sequencer::SequencerCcLaneBank& incoming,
    const core::state::sequencer::SequencerCcLaneBank* destinationBefore
) {
    for (uint8_t lane = 0; lane < incoming.lanes.size(); ++lane) {
        auto& value = incoming.lanes[lane];
        if (!value.occupied) continue;
        const uint16_t destinationGeneration = destinationBefore != nullptr
            ? destinationBefore->lanes[lane].lifecycleGeneration
            : 0U;
        value.lifecycleGeneration =
            core::state::sequencer::nextSequencerCcLaneLifecycleGeneration(
                destinationGeneration
            );
    }
}

FLASHMEM bool sameStableProjection(
    const core::state::ClipboardTransferPlan& prepared,
    const core::state::ClipboardTransferPlan& live
) {
    return live.canCommit() &&
           core::state::sameSequencerTrackClipboardTransferIdentity(prepared, live);
}

FLASHMEM void updateCommitTimeRoutes(
    PreparedSequencerTrackTransfer& prepared,
    const core::state::ClipboardTransferPlan& livePlan
) {
    for (uint8_t index = 0; index < prepared.plan.count; ++index) {
        auto& destination = prepared.plan.entries[index];
        const auto& live = livePlan.entries[index];
        destination.targetMidiChannel = live.targetMidiChannel;
        destination.targetRouteValid = live.targetRouteValid;
    }
    if (prepared.plan.count > 0U) {
        prepared.plan.entry = prepared.plan.entries[0];
        prepared.plan.hasEntry = true;
    }
    prepared.plan.availability = livePlan.availability;
    prepared.plan.reason = livePlan.reason;
}

FLASHMEM SequencerTrackTransferResult resultFromPrepared(
    SequencerTrackTransferStatus status,
    const PreparedSequencerTrackTransfer& prepared
) {
    return {
        status,
        prepared.plan,
        prepared.activationBatch.generation,
        prepared.activationBatch.operationId,
    };
}

}  // namespace

FLASHMEM PreparedSequencerTrackTransfer prepareSequencerTrackTransfer(
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::project::ProjectTrackState& projectTracks,
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    const SharedTrackDomainServices& sharedTracks,
    const SequencerHistoryDomainServices& history,
    uint8_t targetTrack,
    uint16_t pendingTrackMask,
    core::state::sequencer::SequencerTrackActivationQueue* activationQueue,
    bool transportPlaying,
    core::state::macro::MacroPagesState* macroPages
) {
    PreparedSequencerTrackTransfer prepared;
    if (sequencer.stepContentDraft.active.get()) {
        prepared.status = SequencerTrackTransferStatus::INCONSISTENT_STATE;
        return prepared;
    }
    prepared.activationQueue = activationQueue;
    prepared.pendingTrackMask = static_cast<uint16_t>(
        pendingTrackMask |
        (activationQueue != nullptr ? activationQueue->pendingTrackMask() : 0)
    );
    prepared.plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        projectTracks,
        targetTrack,
        prepared.pendingTrackMask
    );
    if (!prepared.plan.canCommit()) return prepared;

    if (!sharedTracks.canPublishPreparedSequencerState()) {
        prepared.status = SequencerTrackTransferStatus::PUBLICATION_UNAVAILABLE;
        prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
        return prepared;
    }

    prepared.initialEnabledMask = tracks.currentEnabledMask();
    prepared.initialProjectMutedMask = projectTracks.authored.mutedMask;
    prepared.initialAudibleMask = core::state::project::audibleMask(
        projectTracks,
        prepared.initialEnabledMask
    );
    prepared.previousActiveTrack = tracks.activeTrackIndex();
    if (sharedTracks.enabledMask() != prepared.initialEnabledMask ||
        sharedTracks.activeTrack() != prepared.previousActiveTrack) {
        prepared.status = SequencerTrackTransferStatus::INCONSISTENT_STATE;
        prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
        return prepared;
    }

    prepared.nextEnabledMask = static_cast<uint16_t>(
        prepared.initialEnabledMask | prepared.plan.targetMask
    );
    prepared.nextAudibleMask = core::state::project::audibleMask(
        projectTracks,
        prepared.nextEnabledMask
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
    const bool globalTrackSelection =
        clipboard.kind.get() ==
            core::state::StructureClipboardKind::
                SEQUENCER_TRACK_SELECTION;
    if (globalTrackSelection &&
        (macroPages == nullptr ||
         !prepareMacroStructureTransfer(
             *macroPages,
             clipboard,
             prepared
         ))) {
        prepared.status =
            SequencerTrackTransferStatus::ALLOCATION_UNAVAILABLE;
        prepared.plan.availability =
            core::state::ClipboardTransferAvailability::DISABLED;
        prepared.plan.reason =
            core::state::ClipboardTransferReason::
                ALLOCATION_UNAVAILABLE;
        return prepared;
    }
    auto& after = prepared.history->after;
    after.enabledMask = prepared.nextEnabledMask;
    after.activeTrack = prepared.plan.firstTarget;
    after.capturedTrackMask = prepared.history->before.capturedTrackMask;

    const SourcePayload firstSource =
        sourcePayload(clipboard, prepared.plan.entries[0]);
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
        afterActive.ccLanesCaptured = true;
        if (!copyGraphIntoReservedStorage(afterActive.graph, beforeActive.graph.get()) ||
            !core::state::cloneSequencerGraph(
                prepared.outgoingActiveGraph,
                beforeActive.graph.get()
            ) ||
            !core::state::sequencer::cloneSequencerCcLaneBank(
                afterActive.ccLanes,
                beforeActive.ccLanes.get()
            ) ||
            !core::state::sequencer::cloneSequencerCcLaneBank(
                prepared.outgoingActiveCcLanes,
                beforeActive.ccLanes.get()
            )) {
            prepared.status = SequencerTrackTransferStatus::ALLOCATION_UNAVAILABLE;
            prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
            prepared.plan.reason = core::state::ClipboardTransferReason::ALLOCATION_UNAVAILABLE;
            return prepared;
        }
    }

    for (uint8_t index = 0; index < prepared.plan.count; ++index) {
        const auto& destination = prepared.plan.entries[index];
        const SourcePayload source =
            sourcePayload(clipboard, destination);
        if (source.snapshot == nullptr) {
            prepared.status = SequencerTrackTransferStatus::STALE;
            return prepared;
        }

        auto& afterTrack = after.tracks[destination.targetTrack];
        const auto* destinationCcLanes = prepared.history->before
            .tracks[destination.targetTrack].ccLanes.get();
        afterTrack.flat = *source.snapshot;
        afterTrack.focusedStep = after.focusedStep;
        afterTrack.ccLanesCaptured = true;
        if (!copyGraphIntoReservedStorage(
                afterTrack.graph,
                source.graph
            ) ||
            !core::state::cloneSequencerGraph(
                prepared.bankGraphAt(index),
                source.graph
            ) ||
            !core::state::sequencer::cloneSequencerCcLaneBank(
                afterTrack.ccLanes,
                source.ccLanes
            )) {
            prepared.status =
                SequencerTrackTransferStatus::ALLOCATION_UNAVAILABLE;
            prepared.plan.availability =
                core::state::ClipboardTransferAvailability::DISABLED;
            prepared.plan.reason =
                core::state::ClipboardTransferReason::
                    ALLOCATION_UNAVAILABLE;
            return prepared;
        }
        if (afterTrack.ccLanes) {
            rebaseIncomingCcLaneLifecycles(
                *afterTrack.ccLanes,
                destinationCcLanes
            );
        }
        // History.after and the live destination must own identical rebased
        // lane generations so an inherited hold cannot leak across Paste.
        if (!core::state::sequencer::cloneSequencerCcLaneBank(
                prepared.bankCcLanesAt(index),
                afterTrack.ccLanes.get()
            )) {
            prepared.status =
                SequencerTrackTransferStatus::ALLOCATION_UNAVAILABLE;
            prepared.plan.availability =
                core::state::ClipboardTransferAvailability::DISABLED;
            prepared.plan.reason =
                core::state::ClipboardTransferReason::
                    ALLOCATION_UNAVAILABLE;
            return prepared;
        }
    }

    const auto& firstAfter =
        after.tracks[prepared.plan.firstTarget];
    if (!core::state::cloneSequencerGraph(prepared.editorGraph, firstSource.graph) ||
        !core::state::sequencer::cloneSequencerCcLaneBank(
            prepared.editorCcLanes,
            firstAfter.ccLanes.get()
        )) {
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
        ) &&
        !core::state::sequencer::
            macroTrackStructureHistoryChanged(*prepared.history)) {
        prepared.status = SequencerTrackTransferStatus::NO_CHANGE;
        return prepared;
    }
    if (prepared.activationQueue != nullptr) {
        if (!prepared.activationQueue->prepare(
                prepared.plan.targetMask,
                prepared.initialAudibleMask,
                transportPlaying,
                prepared.activationBatch,
                core::state::sequencer::SequencerTrackActivationOrigin::TRACK_PASTE
            )) {
            prepared.status = SequencerTrackTransferStatus::STALE;
            prepared.plan.availability =
                core::state::ClipboardTransferAvailability::DISABLED;
            return prepared;
        }
        prepared.history->activation =
            core::state::sequencer::activationHistoryRef(prepared.activationBatch);
        prepared.history->activationBeforeAudibleMask =
            prepared.initialAudibleMask;
        prepared.history->activationAfterAudibleMask =
            prepared.nextAudibleMask;
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
    const core::state::project::ProjectTrackState& projectTracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    const SharedTrackDomainServices& sharedTracks,
    const SequencerHistoryDomainServices& history,
    PreparedSequencerTrackTransfer prepared,
    core::state::macro::MacroPagesState* macroPages
) {
    if (sequencer.stepContentDraft.active.get()) {
        sequencer.stepContentDraft.noteBlockedTransition(
            core::state::sequencer::
                SequencerStepContentDraftBlockedTransition::TRACK
        );
        return resultFromPrepared(
            SequencerTrackTransferStatus::INCONSISTENT_STATE,
            prepared
        );
    }
    if (!prepared.ready()) {
        return resultFromPrepared(prepared.status, prepared);
    }
    if (tracks.currentEnabledMask() != prepared.initialEnabledMask ||
        projectTracks.authored.mutedMask != prepared.initialProjectMutedMask ||
        core::state::project::audibleMask(
            projectTracks,
            prepared.initialEnabledMask
        ) != prepared.initialAudibleMask ||
        tracks.activeTrackIndex() != prepared.previousActiveTrack ||
        sharedTracks.enabledMask() != prepared.initialEnabledMask ||
        sharedTracks.activeTrack() != prepared.previousActiveTrack) {
        return resultFromPrepared(SequencerTrackTransferStatus::STALE, prepared);
    }
    const auto* macroStructure =
        prepared.history->macroStructure.get();
    if (macroStructure != nullptr &&
        (macroPages == nullptr ||
         !core::state::sequencer::
             liveMacroTrackStructureMatches(
                 *macroPages,
                 *macroStructure,
                 false
             ))) {
        return resultFromPrepared(
            SequencerTrackTransferStatus::STALE,
            prepared
        );
    }

    const uint16_t previousActiveBit = sequencerStructureHistoryTrackBit(
        prepared.previousActiveTrack
    );
    const auto livePlan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        projectTracks,
        prepared.plan.firstTarget,
        prepared.pendingTrackMask
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
        ) &&
        !core::state::sequencer::
            macroTrackStructureHistoryChanged(*prepared.history)) {
        return resultFromPrepared(SequencerTrackTransferStatus::NO_CHANGE, prepared);
    }
    if (!history.canRecordStructure(*prepared.history)) {
        prepared.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
        prepared.plan.reason = core::state::ClipboardTransferReason::HISTORY_UNAVAILABLE;
        return resultFromPrepared(SequencerTrackTransferStatus::HISTORY_UNAVAILABLE, prepared);
    }

    if (prepared.activationQueue != nullptr &&
        !prepared.activationQueue->armPrepared(prepared.activationBatch)) {
        return resultFromPrepared(SequencerTrackTransferStatus::STALE, prepared);
    }

    if (macroStructure != nullptr &&
        !core::state::sequencer::applyMacroTrackStructureHistory(
            *macroPages,
            *macroStructure,
            true
        )) {
        return resultFromPrepared(
            SequencerTrackTransferStatus::STALE,
            prepared
        );
    }

    if ((prepared.plan.targetMask & previousActiveBit) == 0) {
        auto& outgoing = tracks.track(prepared.previousActiveTrack);
        core::state::sequencer::installTrackContentSnapshotWithOwnedPayload(
            outgoing,
            prepared.history->before.tracks[prepared.previousActiveTrack].flat,
            std::move(prepared.outgoingActiveGraph),
            std::move(prepared.outgoingActiveCcLanes)
        );
    }

    for (uint8_t index = 0; index < prepared.plan.count; ++index) {
        const auto& destination = prepared.plan.entries[index];
        const SourcePayload source =
            sourcePayload(clipboard, destination);
        // Payload identity was checked immediately above and the clipboard is
        // immutable during this synchronous commit.
        auto& target = tracks.track(destination.targetTrack);
        core::state::sequencer::installTrackContentSnapshotWithOwnedPayload(
            target,
            *source.snapshot,
            std::move(prepared.bankGraphAt(index)),
            std::move(prepared.bankCcLanesAt(index))
        );
    }

    const auto& firstDestination = prepared.plan.entries[0];
    const SourcePayload firstSource =
        sourcePayload(clipboard, firstDestination);
    core::state::sequencer::installTrackContentSnapshotToEditorWithOwnedPayload(
        sequencer,
        *firstSource.snapshot,
        std::move(prepared.editorGraph),
        std::move(prepared.editorCcLanes)
    );
    core::state::sequencer::resetTransientTrackState(sequencer);
    sequencer.focusedStep.set(prepared.history->after.focusedStep);
    sequencer.page.set(prepared.history->after.page);

    sharedTracks.publishPreparedSequencerState(
        prepared.nextEnabledMask,
        prepared.plan.firstTarget
    );
    if (macroStructure != nullptr) {
        sharedTracks.reconcilePreparedMacroTrackTransfer(
            macroStructure->capturedTrackMask
        );
    }
    history.recordPreparedStructure(std::move(prepared.history));
    if (prepared.activationQueue != nullptr) {
        prepared.activationQueue->publishPrepared(prepared.activationBatch);
    }
    return resultFromPrepared(SequencerTrackTransferStatus::APPLIED, prepared);
}

FLASHMEM SequencerTrackTransferResult executeSequencerTrackTransfer(
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::project::ProjectTrackState& projectTracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    const SharedTrackDomainServices& sharedTracks,
    const SequencerHistoryDomainServices& history,
    uint8_t targetTrack,
    uint16_t pendingTrackMask,
    core::state::sequencer::SequencerTrackActivationQueue* activationQueue,
    bool transportPlaying,
    core::state::macro::MacroPagesState* macroPages
) {
    auto prepared = prepareSequencerTrackTransfer(
        tracks,
        projectTracks,
        sequencer,
        clipboard,
        sharedTracks,
        history,
        targetTrack,
        pendingTrackMask,
        activationQueue,
        transportPlaying,
        macroPages
    );
    return commitPreparedSequencerTrackTransfer(
        tracks,
        projectTracks,
        sequencer,
        clipboard,
        sharedTracks,
        history,
        std::move(prepared),
        macroPages
    );
}

}  // namespace core::handler
