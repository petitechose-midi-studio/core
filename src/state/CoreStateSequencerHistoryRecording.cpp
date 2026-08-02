#include <cstdint>
#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <new>
#include <oc/log/Log.hpp>
#include <oc/state/NotificationQueue.hpp>
#include <oc/time/Time.hpp>
#include <utility>

#include "state/CoreState.hpp"

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "macro/MacroWorkflow.hpp"
#include "midi/MidiUtils.hpp"
#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerProjectScaleOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/shared/SharedTrackCoordinator.hpp"

namespace core::state {

namespace {

const char kStepRollbackFailed[] PROGMEM =
    "[CoreState] Failed allocation-free Step coalescer rollback";
const char kStepAdmissionRollbackFailed[] PROGMEM =
    "[CoreState] Failed allocation-free Step admission rollback";
const char kPreparedFamilyIdentityRollbackFailed[] PROGMEM =
    "[CoreState] Failed allocation-free prepared-family identity rollback";
const char kPreparedGraphCompactionRollbackFailed[] PROGMEM =
    "[CoreState] Failed allocation-free prepared Graph compaction rollback";
const char kPreparedFamilyRollbackFailed[] PROGMEM =
    "[CoreState] Failed allocation-free prepared-family rollback";
const char kFamilyAdmissionRollbackFailed[] PROGMEM =
    "[CoreState] Failed allocation-free family admission rollback";

FLASHMEM bool hasCanonicalCcPayload(
    const sequencer::SequencerPatternState& pattern
) {
    const auto* lanes = sequencer::sequencerCcLaneView(pattern);
    return lanes != nullptr && sequencer::sequencerCcLaneCount(*lanes) != 0U;
}

FLASHMEM bool preparedFullBankSourceTopologyMatches(
    const sequencer::SequencerTrackBankState& bank,
    const sequencer::SequencerState& active,
    const sequencer::SequencerHistoryTrackBankSnapshot& before
) {
    const uint8_t activeTrack = before.flat.activeTrack;
    if (activeTrack >= sequencer::SequencerTrackBankState::TRACK_COUNT ||
        bank.activeTrackIndex() != activeTrack ||
        bank.currentEnabledMask() != before.flat.enabledMask ||
        (sequencer::graphView(active.pattern) != nullptr) !=
            static_cast<bool>(before.editorGraph) ||
        hasCanonicalCcPayload(active.pattern) !=
            static_cast<bool>(before.editorCcLanes)) {
        return false;
    }

    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        if (i == activeTrack) continue;
        const auto& pattern = bank.track(i);
        if ((sequencer::graphView(pattern) != nullptr) !=
                static_cast<bool>(before.bankGraphs[i]) ||
            hasCanonicalCcPayload(pattern) !=
                static_cast<bool>(before.bankCcLanes[i])) {
            return false;
        }
    }
    return true;
}

FLASHMEM int32_t
sequencerHistoryValueForProperty(const sequencer::SequencerHistoryPatternSnapshot& snapshot,
                                 uint8_t step, sequencer::StepProperty property) {
    if (step >= sequencer::SequencerPatternState::MAX_STEPS) { return 0; }

    switch (property) {
        case sequencer::StepProperty::NOTE: return snapshot.flat.note[step];
        case sequencer::StepProperty::VELOCITY: return snapshot.flat.velocity[step];
        case sequencer::StepProperty::GATE: return snapshot.flat.gate[step];
        case sequencer::StepProperty::NUDGE: return snapshot.flat.nudge[step];
        case sequencer::StepProperty::PROBABILITY: return snapshot.flat.probability[step];
        default: return 0;
    }
}

FLASHMEM sequencer::SequencerHistoryDescriptor makeStepPropertyHistoryDescriptor(
    uint8_t track, uint8_t step, sequencer::StepProperty property,
    const sequencer::SequencerHistoryPatternSnapshot& before,
    const sequencer::SequencerHistoryPatternSnapshot& after) {
    const int32_t beforeValue = sequencerHistoryValueForProperty(before, step, property);
    const int32_t afterValue = sequencerHistoryValueForProperty(after, step, property);
    if (beforeValue == afterValue) {
        return sequencer::SequencerHistoryDescriptor{
            .kind = sequencer::SequencerHistoryActionKind::StepEdit,
            .trackIndex = track,
            .stepIndex = step,
            .property = property,
            .hasValue = false,
        };
    }

    return sequencer::SequencerHistoryDescriptor{
        .kind = sequencer::SequencerHistoryActionKind::StepPropertyEdit,
        .trackIndex = track,
        .stepIndex = step,
        .property = property,
        .hasValue = true,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
}

FLASHMEM sequencer::SequencerHistoryDescriptor makeStepStateHistoryDescriptor(
    uint8_t track, uint8_t step, const sequencer::SequencerHistoryPatternSnapshot& before,
    const sequencer::SequencerHistoryPatternSnapshot& after) {
    const bool beforeEnabled = before.flat.enabledMask.test(step);
    const bool afterEnabled = after.flat.enabledMask.test(step);
    return sequencer::SequencerHistoryDescriptor{
        .kind = sequencer::SequencerHistoryActionKind::StepToggle,
        .trackIndex = track,
        .stepIndex = step,
        .property = sequencer::StepProperty::NOTE,
        .hasValue = beforeEnabled != afterEnabled,
        .beforeValue = beforeEnabled ? 1 : 0,
        .afterValue = afterEnabled ? 1 : 0,
    };
}

}  // namespace

FLASHMEM void SequencerDomainState::CoalescedPatternHistory::clear() {
    pending = false;
    kind = Kind::StepProperty;
    activeTrack = 0;
    step = 0;
    property = sequencer::StepProperty::NOTE;
    stateProperty = false;
    lane = sequencer::SequencerHistoryDescriptor::INVALID_INDEX;
    lastTouchedMs = 0;
    payloadPlan = sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly;
    sealed = false;
    hasChange = false;
    prospectiveGraphInstalled = false;
    genericMutationPendingAtBegin = false;
    graphCompaction = GraphCompactionState::Disabled;
    preparedPatternChange.reset();
    synchronization.reset();
    preparedCcLaneChange.reset();
}

FLASHMEM void CoreState::consumePendingSequencerMutation_(bool* priorMutation) {
    auto* coalescer = sequencerDomain_.mutationCoalescer.get();
    if (coalescer == nullptr) return;

    const bool hadArmedMutation = priorMutation != nullptr && coalescer->hasPendingChanges();
    std::size_t queuedBefore = 0U;
    if (priorMutation != nullptr) {
        queuedBefore = oc::state::NotificationQueue::instance().pendingCount();
    }

    coalescer->consumePendingChangesWithoutAction();

    if (priorMutation != nullptr) {
        const bool hadQueuedMutation =
            oc::state::NotificationQueue::instance().pendingCount() < queuedBefore;
        *priorMutation = *priorMutation || hadArmedMutation || hadQueuedMutation;
    }
}

FLASHMEM bool CoreState::rollbackPreparedSequencerPatternEdit_() {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending || !pending.preparedPatternChange ||
        !sequencer::restorePreparedHistoryPatternBefore(
            sequencerTracks,
            sequencer,
            *pending.preparedPatternChange,
            pending.prospectiveGraphInstalled)) {
        return false;
    }

    const bool restoreGenericMutation = pending.genericMutationPendingAtBegin;
    consumePendingSequencerMutation_();
    if (restoreGenericMutation) {
        auto* coalescer = sequencerDomain_.mutationCoalescer.get();
        if (coalescer != nullptr) coalescer->markChanged();
    }
    pending.clear();
    return true;
}

FLASHMEM CoreState::SequencerPatternHistoryCommitOutcome
CoreState::abandonUnsafeSequencerPatternHistory_(const char* reason) {
    OC_LOG_ERROR(
        "[CoreState] Coalesced Sequencer history unavailable ({}); "
        "clearing Project history boundary",
        reason);
    if (!clearProjectHistory()) {
        OC_LOG_ERROR("[CoreState] Failed to close Project history boundary");
    }
    return SequencerPatternHistoryCommitOutcome::Failed;
}

FLASHMEM bool CoreState::recordSequencerPatternHistory(
    sequencer::SequencerHistoryPatternSnapshot before,
    sequencer::SequencerHistoryPatternSnapshot after,
    sequencer::SequencerHistoryDescriptor descriptor,
    sequencer::SequencerHistoryPatternStorage storage) {
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    uint8_t targetTrack = activeTrack;
    if (descriptor.trackIndex == sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        descriptor.trackIndex = activeTrack;
    } else {
        targetTrack = sequencer::SequencerTrackBankState::clampTrackIndex(descriptor.trackIndex);
        descriptor.trackIndex = targetTrack;
    }

    const bool recorded = storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
                              ? sequencerHistory.recordFlatPattern(targetTrack, std::move(before),
                                                                   std::move(after), descriptor)
                              : sequencerHistory.recordPattern(targetTrack, std::move(before),
                                                               std::move(after), descriptor);
    if (!recorded) { return false; }

    const bool synchronized =
        storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
            ? sequencer::storeActiveTrackPreservingGraph(sequencerTracks, sequencer)
            : sequencer::storeActiveTrack(sequencerTracks, sequencer);
    if (!synchronized) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph after history");
    }
    markProjectMutated();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerPatternHistory(
    sequencer::SequencerHistoryPatternChangePtr change) {
    if (!change) return false;

    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    const uint8_t targetTrack =
        change->descriptor.trackIndex == sequencer::SequencerHistoryDescriptor::INVALID_INDEX
            ? activeTrack
            : sequencer::SequencerTrackBankState::clampTrackIndex(change->descriptor.trackIndex);
    change->trackIndex = targetTrack;
    change->descriptor.trackIndex = targetTrack;
    const auto storage = change->storage;
    if (!sequencerHistory.recordPattern(std::move(change))) return false;

    const bool synchronized =
        storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
            ? sequencer::storeActiveTrackPreservingGraph(sequencerTracks, sequencer)
            : sequencer::storeActiveTrack(sequencerTracks, sequencer);
    if (!synchronized) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph after history");
    }
    markProjectMutated();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerBankHistory(
    sequencer::SequencerHistoryTrackBankSnapshot before,
    sequencer::SequencerHistoryTrackBankSnapshot after,
    sequencer::SequencerHistoryDescriptor descriptor) {
    if (!sequencerHistory.recordFullBank(std::move(before), std::move(after), descriptor)) {
        return false;
    }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerBankHistory(
    sequencer::SequencerHistoryFullBankChangePtr change) {
    if (!sequencerHistory.recordFullBank(std::move(change))) { return false; }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::canRecordSequencerBankHistory(
    const sequencer::SequencerHistoryFullBankChange& change) const {
    // FullBank replay rejects while a Step Draft is active, including
    // content-only changes with unchanged topology. Keep admission identical
    // so History can never publish an `after` snapshot that cannot be applied.
    return !sequencer.stepContentDraft.active.get() && sequencerHistory.canRecordFullBank(change);
}

FLASHMEM void CoreState::recordPreparedSequencerBankHistory(
    sequencer::SequencerHistoryFullBankChangePtr change) {
    if (!change || !canRecordSequencerBankHistory(*change)) return;
    const uint16_t enabledMask = change->after.flat.enabledMask;
    const uint8_t activeTrack = change->after.flat.activeTrack;
    if (!publishPreparedSequencerTrackState(enabledMask, activeTrack)) return;
    sequencerHistory.recordPreparedFullBank(std::move(change));
    publishPreparedSequencerMutation();
}

FLASHMEM sequencer::SequencerPreparedFullBankEditResult
CoreState::applyPreparedProjectScaleChoice(
    sequencer::SequencerPreparedFullBankEditOwner owner,
    uint8_t row,
    int choiceIndex
) {
    using Owner = sequencer::SequencerPreparedFullBankEditOwner;
    using Outcome = sequencer::SequencerPreparedFullBankEditOutcome;

    sequencer::SequencerPreparedFullBankEditResult result{};
    if (owner != Owner::ProjectScale && owner != Owner::SequencerSettingsScale) {
        return result;
    }

    const auto choice = sequencer::resolveProjectScaleChoice(
        sequencerTracks.projectScaleSettings(), row, choiceIndex);
    if (!choice.valid) return result;

    if (owner == Owner::ProjectScale && !choice.changes) {
        result.outcome = Outcome::NoChange;
        return result;
    }

    if (commitSequencerPatternHistoryCoalescing_() ==
        SequencerPatternHistoryCommitOutcome::Failed) {
        return result;
    }

    if (sequencer.stepContentDraft.active.get()) {
        sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::PROJECT_LOAD);
        return result;
    }

    if (!choice.changes) {
        result.outcome = Outcome::NoChange;
        return result;
    }

    auto change = sequencer::prepareHistoryFullBankChangeBefore(
        sequencerTracks,
        sequencer,
        sequencer::SequencerHistoryDescriptor{
            .kind = sequencer::SequencerHistoryActionKind::ProjectScaleSettings,
        }
    );
    if (!change ||
        !sequencer::reservePreparedHistoryFullBankAfter(
            sequencerTracks, sequencer, *change)) {
        return result;
    }

    auto stagedBank = core::app::makeExtmemUnique<sequencer::SequencerTrackBankState>();
    if (!stagedBank) return result;
    auto stagedActive = core::app::makeExtmemUnique<sequencer::SequencerState>();
    if (!stagedActive) return result;

    if (!sequencer::populatePreparedHistoryFullBankStaging(
            sequencerTracks, sequencer, change->before, *stagedBank, *stagedActive)) {
        return result;
    }

    const auto stagedMutation = sequencer::applyProjectScaleTransition(
        *stagedBank, *stagedActive, choice.target);
    if (!stagedMutation.changed ||
        !sequencer::capturePreparedHistoryFullBankAfterUsingReservedStorage(
            *stagedBank, *stagedActive, *change) ||
        !sequencerHistory.canRecordFullBank(*change) ||
        !preparedFullBankSourceTopologyMatches(
            sequencerTracks, sequencer, change->before)) {
        return result;
    }

    result.projection = stagedMutation.projection;

    // No fallible operation is permitted beyond this boundary. The same
    // presence-preserving state operation runs on the still-unchanged live
    // owners, followed immediately by the already-admitted ownership transfer.
    (void)sequencer::applyProjectScaleTransition(
        sequencerTracks, sequencer, choice.target);
    sequencerHistory.commitAdmittedFullBank(std::move(change));
    publishPreparedSequencerMutation();

    result.outcome = Outcome::Committed;
    return result;
}

FLASHMEM bool CoreState::recordSequencerStructureHistory(
    sequencer::SequencerHistoryTrackStructureChangePtr change) {
    if (!sequencerHistory.recordStructure(std::move(change))) { return false; }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::canRecordSequencerStructureHistory(
    const sequencer::SequencerHistoryTrackStructureChange& change) const {
    return !sequencer.stepContentDraft.active.get() && sequencerHistory.canRecordStructure(change);
}

FLASHMEM void CoreState::recordPreparedSequencerStructureHistory(
    sequencer::SequencerHistoryTrackStructureChangePtr change) {
    if (!change || !canRecordSequencerStructureHistory(*change)) return;
    const uint16_t enabledMask = change->after.enabledMask;
    const uint8_t activeTrack = change->after.activeTrack;
    if (!publishPreparedSequencerTrackState(enabledMask, activeTrack)) return;
    sequencerHistory.recordPreparedStructure(std::move(change));
    publishPreparedSequencerMutation();
}

FLASHMEM void CoreState::commitAdmittedSequencerStructureHistory(
    sequencer::SequencerHistoryTrackStructureChangePtr change) {
    sequencerHistory.commitAdmittedStructure(std::move(change));
    publishPreparedSequencerMutation();
}

FLASHMEM void CoreState::publishPreparedSequencerMutation() {
    // The prepared transaction already performed the coalescer action's
    // editor-to-bank synchronization. Cancel only this coalescer's queued
    // callbacks (including later entries in an active notification wave)
    // and consume an already-armed mark before publishing directly.
    consumePendingSequencerMutation_();
    markProjectMutated();
}

FLASHMEM bool CoreState::beginOrContinueSequencerPatternHistoryCoalescing(
    uint8_t step, sequencer::StepProperty property, uint32_t nowMs,
    sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan, bool stateProperty) {
    if (step >= sequencer::SequencerPatternState::MAX_STEPS) { return false; }

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();

    if (pending.matchesStepProperty(activeTrack, step, property, stateProperty)) {
        // The stable grouping key intentionally excludes storage policy. A
        // plan drift is a caller-classification bug; reject it atomically
        // rather than splitting one 500 ms gesture into two Undo entries.
        if (pending.payloadPlan != payloadPlan || !pending.sealed ||
            !pending.preparedPatternChange ||
            !sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks,
                                                                  pending.synchronization)) {
            return false;
        }
        consumePendingSequencerMutation_(&pending.genericMutationPendingAtBegin);
        pending.sealed = false;
        pending.lastTouchedMs = nowMs;
        return true;
    }

    if (pending.pending) {
        const auto outcome = commitSequencerPatternHistoryCoalescing_();
        if (outcome == SequencerPatternHistoryCommitOutcome::Failed) { return false; }
    }

    sequencer::SequencerHistoryGraphPtr prospectiveGraph;
    auto change = sequencer::prepareHistoryPatternChangeBefore(
        sequencerTracks, sequencer, activeTrack, payloadPlan, prospectiveGraph);
    if (!change || !sequencer::reservePreparedHistoryPatternAfter(sequencerTracks, sequencer,
                                                                  *change, payloadPlan)) {
        return false;
    }

    sequencer::SequencerPreparedActiveTrackSynchronization synchronization;
    if (!sequencer::reservePreparedActiveTrackSynchronization(
            sequencerTracks, sequencer, activeTrack, payloadPlan, synchronization) ||
        activeTrack != sequencerTracks.activeTrackIndex() ||
        !sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks, synchronization)) {
        return false;
    }

    pending.clear();
    pending.pending = true;
    pending.kind = SequencerDomainState::CoalescedPatternHistory::Kind::StepProperty;
    pending.activeTrack = activeTrack;
    pending.step = step;
    pending.property = property;
    pending.stateProperty = stateProperty;
    pending.lastTouchedMs = nowMs;
    pending.payloadPlan = payloadPlan;
    pending.sealed = false;
    pending.hasChange = false;
    pending.preparedPatternChange = std::move(change);
    pending.synchronization = std::move(synchronization);

    if (prospectiveGraph) {
        if (sequencer.pattern.graph) {
            pending.clear();
            return false;
        }
        sequencer.pattern.graph = std::move(prospectiveGraph);
        pending.prospectiveGraphInstalled = true;
    }
    pending.preparedPatternChange->setPreparedPayloadOwnerProof(sequencer.pattern);
    // Preparation is now irrevocably successful but the caller has not yet
    // performed its live mutation. Isolate any earlier generic Sequencer mark
    // (including a still-queued callback) so Step publication can subsume it,
    // or restore it if this gesture later proves to be a no-op/net return.
    consumePendingSequencerMutation_(&pending.genericMutationPendingAtBegin);
    return true;
}

FLASHMEM sequencer::SequencerPreparedPatternEditBeginOutcome
CoreState::beginOrContinueSequencerPreparedPatternEdit(
    sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
    sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
    sequencer::SequencerHistoryDescriptor descriptor, bool compactGraphOnSeal) {
    using Outcome = sequencer::SequencerPreparedPatternEditBeginOutcome;

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    descriptor.trackIndex = activeTrack;

    if (pending.matchesPreparedFamily(activeTrack, owner, key) &&
        pending.payloadPlan == payloadPlan &&
        pending.graphCompactionRequested() == compactGraphOnSeal) {
        if (!pending.sealed || !pending.preparedPatternChange ||
            !pending.preparedPatternChange->preparedPayloadOwnerProofMatches(sequencer.pattern) ||
            !sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks,
                                                                  pending.synchronization) ||
            !sequencer::preparedHistoryPatternAfterMatchesTrack(
                sequencerTracks, sequencer, activeTrack, pending.preparedPatternChange->after,
                pending.preparedPatternChange->storage)) {
            return Outcome::Failed;
        }
        consumePendingSequencerMutation_(&pending.genericMutationPendingAtBegin);
        pending.sealed = false;
        return Outcome::Continued;
    }

    if (pending.pending) {
        const auto outcome = commitSequencerPatternHistoryCoalescing_();
        if (outcome == SequencerPatternHistoryCommitOutcome::Failed) { return Outcome::Failed; }
    }

    sequencer::SequencerHistoryGraphPtr prospectiveGraph;
    auto change = sequencer::prepareHistoryPatternChangeBefore(
        sequencerTracks, sequencer, activeTrack, payloadPlan, prospectiveGraph, descriptor);
    if (!change || !sequencer::reservePreparedHistoryPatternAfter(sequencerTracks, sequencer,
                                                                  *change, payloadPlan)) {
        return Outcome::Failed;
    }

    sequencer::SequencerPreparedActiveTrackSynchronization synchronization;
    if (!sequencer::reservePreparedActiveTrackSynchronization(
            sequencerTracks, sequencer, activeTrack, payloadPlan, synchronization) ||
        activeTrack != sequencerTracks.activeTrackIndex() ||
        !sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks, synchronization)) {
        return Outcome::Failed;
    }

    const bool preserveEmptyPageCcOwner =
        owner == sequencer::SequencerPreparedPatternEditOwner::PageStructure &&
        change->storage == sequencer::SequencerHistoryPatternStorage::FullGraph &&
        sequencer.pattern.ccLanes != nullptr &&
        !hasCanonicalCcPayload(sequencer.pattern);
    if (preserveEmptyPageCcOwner) {
        // LOCK-P reserves no CC payload for an allocated owner with zero
        // occupied lanes. Page actions never mutate it, so this existing bit
        // records a preserve-live policy without growing the Change ABI.
        change->before.ccLanesCaptured = false;
    }

    pending.clear();
    pending.pending = true;
    pending.kind = SequencerDomainState::CoalescedPatternHistory::Kind::PreparedFamily;
    pending.activeTrack = activeTrack;
    pending.familyOwner = owner;
    pending.familyKey = key;
    pending.payloadPlan = payloadPlan;
    pending.sealed = false;
    pending.hasChange = false;
    pending.graphCompaction = compactGraphOnSeal
        ? SequencerDomainState::CoalescedPatternHistory::
              GraphCompactionState::SealPending
        : SequencerDomainState::CoalescedPatternHistory::
              GraphCompactionState::Disabled;
    pending.preparedPatternChange = std::move(change);
    pending.synchronization = std::move(synchronization);

    if (prospectiveGraph) {
        if (sequencer.pattern.graph) {
            pending.clear();
            return Outcome::Failed;
        }
        sequencer.pattern.graph = std::move(prospectiveGraph);
        pending.prospectiveGraphInstalled = true;
    }
    pending.preparedPatternChange->setPreparedPayloadOwnerProof(sequencer.pattern);
    consumePendingSequencerMutation_(&pending.genericMutationPendingAtBegin);
    return Outcome::Started;
}

FLASHMEM bool CoreState::sequencerPreparedPatternEditReady(
    sequencer::SequencerPreparedPatternEditOwner owner,
    uint8_t key,
    uint8_t expectedTrack
) const {
    const auto& pending = sequencerDomain_.coalescedPatternHistory;
    return expectedTrack < sequencer::SequencerTrackBankState::TRACK_COUNT &&
           expectedTrack == sequencerTracks.activeTrackIndex() &&
           pending.matchesPreparedFamily(expectedTrack, owner, key) &&
           !pending.sealed && !pending.hasChange &&
           pending.preparedPatternChange &&
           pending.preparedPatternChange->preparedPayloadOwnerProofMatches(
               sequencer.pattern) &&
           sequencer::preparedActiveTrackSynchronizationMatches(
            sequencerTracks, pending.synchronization);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM sequencer::SequencerPreparedPatternGraphPrecompactionOutcome
CoreState::precompactSequencerPreparedPatternEditGraph(
    sequencer::SequencerPreparedPatternEditOwner owner,
    uint8_t key,
    uint8_t expectedTrack,
    sequencer::SequencerPreparedGraphContentPath& contentPath
) {
    using Outcome = sequencer::SequencerPreparedPatternGraphPrecompactionOutcome;
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!sequencerPreparedPatternEditReady(owner, key, expectedTrack) ||
        pending.graphCompaction != SequencerDomainState::CoalescedPatternHistory::
            GraphCompactionState::SealPending ||
        !contentPath.valid || !pending.preparedPatternChange ||
        pending.preparedPatternChange->storage !=
            sequencer::SequencerHistoryPatternStorage::FullGraph ||
        sequencer::graphView(sequencer.pattern) == nullptr ||
        !pending.preparedPatternChange->after.graph) {
        return Outcome::Failed;
    }

    // This full remap is deliberately isolated in the existing cold 1.2 KiB
    // stack class. The caller keeps only the four-frame active path.
    sequencer::SequencerGraphCompactionRemap remap;
    const auto compaction = sequencer::compactGraphUsingReservedStorage(
        sequencer.pattern,
        *pending.preparedPatternChange->after.graph,
        remap);
    if (!compaction.ok ||
        !sequencer::remapPreparedSequencerGraphContentPath(
            contentPath, remap, compaction.compacted)) {
        return Outcome::Failed;
    }

    pending.graphCompaction = compaction.compacted
        ? SequencerDomainState::CoalescedPatternHistory::
              GraphCompactionState::Precompacted
        : SequencerDomainState::CoalescedPatternHistory::
              GraphCompactionState::PrecompactedUnchanged;
    return compaction.compacted ? Outcome::Compacted : Outcome::Unchanged;
}

FLASHMEM bool CoreState::sealSequencerPatternHistoryCoalescing(bool mutationChanged) {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending ||
        pending.kind != SequencerDomainState::CoalescedPatternHistory::Kind::StepProperty ||
        pending.sealed || !pending.preparedPatternChange ||
        pending.activeTrack != sequencerTracks.activeTrackIndex() ||
        !sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks,
                                                              pending.synchronization)) {
        return false;
    }

    if (!mutationChanged) {
        if (pending.hasChange) {
            // A prior changed seal owns the generic mutation mark and will
            // publish it at the prepared 500 ms boundary.
            consumePendingSequencerMutation_();
            pending.sealed = true;
            return true;
        }

        // A setter may allocate/enable a prospective Graph before discovering
        // that its quantized value is unchanged. Restore the exact prepared
        // Before state instead of assuming a false return made no live writes.
        return rollbackPreparedSequencerPatternEdit_();
    }

    auto& change = *pending.preparedPatternChange;
    if (pending.prospectiveGraphInstalled && sequencer.pattern.graph &&
        !sequencer.pattern.graph->enabled) {
        sequencer.pattern.graph.reset();
        // The transaction still introduced this owner relative to Before.
        // Keep that rollback fact, but rebind the live proof before any
        // fallible capture/admission step.
        change.setPreparedPayloadOwnerProof(sequencer.pattern);
    }
    if (!sequencer::capturePreparedHistoryPatternAfterUsingReservedStorage(sequencerTracks,
                                                                           sequencer, change) ||
        !sequencer::refreshPreparedActiveTrackSynchronizationUsingReservedStorage(
            sequencerTracks, sequencer, pending.synchronization)) {
        if (!rollbackPreparedSequencerPatternEdit_()) { OC_LOG_ERROR(kStepRollbackFailed); }
        return false;
    }

    change.descriptor =
        pending.stateProperty
            ? makeStepStateHistoryDescriptor(pending.activeTrack, pending.step, change.before,
                                             change.after)
            : makeStepPropertyHistoryDescriptor(pending.activeTrack, pending.step, pending.property,
                                                change.before, change.after);

    if (sequencer::sameMusicalHistorySnapshot(change.before, change.after)) {
        // The musical bytes returned to Before, but setters may have advanced
        // editor-only revision counters on the round trip. The generic
        // coalescer is intentionally cancelled below, so restore those exact
        // counters and keep the still-unpublished bank byte-coherent. A Graph
        // created prospectively for this session is not part of Before and
        // must not survive an otherwise exact net return.
        if (pending.prospectiveGraphInstalled && !change.before.graph && sequencer.pattern.graph) {
            sequencer.pattern.graph.reset();
        }
        sequencer::synchronizeHistoryPatternRevisionSignals(sequencer.pattern, change.before.flat,
                                                            change.before.ccLaneRevision);
        // Cancel callbacks from the musical round trip. If the generic
        // coalescer already owned an earlier mutation, re-arm that independent
        // obligation after cancellation; a changed prepared commit would have
        // subsumed it, but this net-zero transaction publishes nothing.
        const bool restoreGenericMutation = pending.genericMutationPendingAtBegin;
        consumePendingSequencerMutation_();
        if (restoreGenericMutation) {
            auto* coalescer = sequencerDomain_.mutationCoalescer.get();
            if (coalescer != nullptr) coalescer->markChanged();
        }
        pending.clear();
        return true;
    }
    if (!sequencerHistory.canRecordPattern(change)) {
        if (!rollbackPreparedSequencerPatternEdit_()) {
            OC_LOG_ERROR(kStepAdmissionRollbackFailed);
        }
        return false;
    }

    consumePendingSequencerMutation_();
    pending.hasChange = true;
    pending.sealed = true;
    return true;
}

FLASHMEM sequencer::SequencerPreparedPatternEditSealOutcome
CoreState::sealSequencerPreparedPatternEdit(sequencer::SequencerPreparedPatternEditOwner owner,
                                            uint8_t key, bool mutationChanged,
                                            sequencer::SequencerHistoryDescriptor descriptor) {
    using Outcome = sequencer::SequencerPreparedPatternEditSealOutcome;

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending ||
        pending.kind != SequencerDomainState::CoalescedPatternHistory::Kind::PreparedFamily ||
        pending.familyOwner != owner || pending.familyKey != key ||
        pending.sealed || !pending.preparedPatternChange) {
        return Outcome::Failed;
    }
    if (!sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks,
                                                              pending.synchronization) ||
        !pending.preparedPatternChange->preparedPayloadOwnerProofMatches(sequencer.pattern)) {
        if (rollbackPreparedSequencerPatternEdit_()) return Outcome::FailedClosed;
        OC_LOG_ERROR(kPreparedFamilyIdentityRollbackFailed);
        return Outcome::Failed;
    }

    if (!mutationChanged) {
        if (pending.hasChange) {
            consumePendingSequencerMutation_();
            pending.sealed = true;
            return Outcome::Sealed;
        }
        // False means no musical delta, not necessarily no preparatory write:
        // graph-backed setters can enable the prospective owner before their
        // final equality check. Roll back the complete prepared state.
        return rollbackPreparedSequencerPatternEdit_() ? Outcome::Cleared : Outcome::Failed;
    }

    if (pending.prospectiveGraphInstalled && sequencer.pattern.graph &&
        !sequencer.pattern.graph->enabled) {
        sequencer.pattern.graph.reset();
        // `prospectiveGraphInstalled` describes Before ownership, not current
        // presence. Preserve it so every later failure can still roll back.
        pending.preparedPatternChange->setPreparedPayloadOwnerProof(sequencer.pattern);
    }
    if (pending.graphWasPrecompacted()) {
        // The reclaim phase already compacted after every planned destination
        // reference was released. The remaining mutation is append-only; its
        // small remapped UI path is published by the caller only after commit.
        return finishSequencerPreparedPatternEdit_(descriptor, nullptr, false);
    }
    if (pending.graphCompactionRequested()) {
        return sealSequencerPreparedPatternEditWithGraphCompaction_(descriptor);
    }

    return finishSequencerPreparedPatternEdit_(descriptor, nullptr, false);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM sequencer::SequencerPreparedPatternEditSealOutcome
CoreState::sealSequencerPreparedPatternEditWithGraphCompaction_(
    sequencer::SequencerHistoryDescriptor descriptor) {
    using Outcome = sequencer::SequencerPreparedPatternEditSealOutcome;

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending ||
        pending.graphCompaction != SequencerDomainState::CoalescedPatternHistory::
            GraphCompactionState::SealPending ||
        pending.sealed ||
        !pending.preparedPatternChange) {
        return Outcome::Failed;
    }

    auto& change = *pending.preparedPatternChange;
    if (change.storage != sequencer::SequencerHistoryPatternStorage::FullGraph ||
        sequencer::graphView(sequencer.pattern) == nullptr || !change.after.graph) {
        if (rollbackPreparedSequencerPatternEdit_()) return Outcome::FailedClosed;
        OC_LOG_ERROR(kPreparedGraphCompactionRollbackFailed);
        return Outcome::Failed;
    }

    // Keep this 1,216-byte remap out of the common Flat/family seal frame.
    // The explicit noinline boundary is a RAM1 stack contract on Teensy.
    sequencer::SequencerGraphCompactionRemap compactionRemap;
    const auto compaction = sequencer::compactGraphUsingReservedStorage(
        sequencer.pattern, *change.after.graph, compactionRemap);
    if (!compaction.ok) {
        if (rollbackPreparedSequencerPatternEdit_()) return Outcome::FailedClosed;
        OC_LOG_ERROR(kPreparedGraphCompactionRollbackFailed);
        return Outcome::Failed;
    }

    return finishSequencerPreparedPatternEdit_(
        descriptor, &compactionRemap, compaction.compacted);
}

FLASHMEM sequencer::SequencerPreparedPatternEditSealOutcome
CoreState::finishSequencerPreparedPatternEdit_(
    sequencer::SequencerHistoryDescriptor descriptor,
    const sequencer::SequencerGraphCompactionRemap* compactionRemap, bool graphCompacted) {
    using Outcome = sequencer::SequencerPreparedPatternEditSealOutcome;

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending || pending.sealed || !pending.preparedPatternChange) {
        return Outcome::Failed;
    }
    auto& change = *pending.preparedPatternChange;
    const bool preserveEmptyPageCcOwner =
        pending.familyOwner ==
            sequencer::SequencerPreparedPatternEditOwner::PageStructure &&
        change.storage == sequencer::SequencerHistoryPatternStorage::FullGraph &&
        !change.before.ccLanesCaptured;
    const bool afterCaptured =
        sequencer::capturePreparedHistoryPatternAfterUsingReservedStorage(
            sequencerTracks, sequencer, change);
    const bool synchronizationCaptured = afterCaptured &&
        sequencer::refreshPreparedActiveTrackSynchronizationUsingReservedStorage(
            sequencerTracks, sequencer, pending.synchronization);
    const auto& bankTarget = sequencerTracks.track(pending.activeTrack);
    const bool emptyCcOwnerStillExact = !preserveEmptyPageCcOwner ||
        (sequencer.pattern.ccLanes != nullptr &&
         !hasCanonicalCcPayload(sequencer.pattern) &&
         !hasCanonicalCcPayload(bankTarget) &&
         change.after.ccLanes == nullptr &&
         change.after.ccLaneRevision == change.before.ccLaneRevision &&
         change.preparedCcLaneOwnerProofMatches(sequencer.pattern));
    if (!afterCaptured || !synchronizationCaptured ||
        !emptyCcOwnerStillExact) {
        if (rollbackPreparedSequencerPatternEdit_()) return Outcome::FailedClosed;
        OC_LOG_ERROR(kPreparedFamilyRollbackFailed);
        return Outcome::Failed;
    }
    if (preserveEmptyPageCcOwner) {
        change.after.ccLanesCaptured = false;
    }

    descriptor.trackIndex = pending.activeTrack;
    change.descriptor = descriptor;

    if (sequencer::sameMusicalHistorySnapshot(change.before, change.after)) {
        if (pending.prospectiveGraphInstalled && !change.before.graph && sequencer.pattern.graph) {
            sequencer.pattern.graph.reset();
        }
        sequencer::synchronizeHistoryPatternRevisionSignals(sequencer.pattern, change.before.flat,
                                                            change.before.ccLaneRevision);
        if (compactionRemap != nullptr) {
            sequencer::finalizePreparedSequencerGraphMutation(sequencer, *compactionRemap,
                                                              graphCompacted);
        }
        const bool restoreGenericMutation = pending.genericMutationPendingAtBegin;
        consumePendingSequencerMutation_();
        if (restoreGenericMutation) {
            auto* coalescer = sequencerDomain_.mutationCoalescer.get();
            if (coalescer != nullptr) coalescer->markChanged();
        }
        pending.clear();
        return Outcome::Cleared;
    }
    if (!sequencerHistory.canRecordPattern(change)) {
        if (rollbackPreparedSequencerPatternEdit_()) return Outcome::FailedClosed;
        OC_LOG_ERROR(kFamilyAdmissionRollbackFailed);
        return Outcome::Failed;
    }

    if (compactionRemap != nullptr) {
        sequencer::finalizePreparedSequencerGraphMutation(sequencer, *compactionRemap,
                                                          graphCompacted);
    }
    // Finalization may clamp watched page/focus signals. Consume only after it
    // so the prepared publication subsumes every mutation caused by this edit.
    consumePendingSequencerMutation_();
    pending.hasChange = true;
    pending.sealed = true;
    // The central seal may intentionally release an unused prospective Graph.
    // Rebind the exact proof only after capture/admission has succeeded.
    pending.preparedPatternChange->setPreparedPayloadOwnerProof(sequencer.pattern);
    return Outcome::Sealed;
}

FLASHMEM bool CoreState::beginOrContinueSequencerCcLaneEventHistoryCoalescing(
    uint8_t lane, uint8_t step, int32_t beforeValue, int32_t afterValue,
    const sequencer::SequencerCcLaneBank* afterBank, uint32_t nowMs) {
    if (lane >= sequencer::SequencerCcLaneBank::MAX_LANES ||
        step >= sequencer::SequencerCcLaneBank::MAX_STEPS || beforeValue < -1 ||
        beforeValue > 127 || afterValue < 0 || afterValue > 127 || afterBank == nullptr ||
        !afterBank->lanes[lane].occupied || !afterBank->lanes[lane].activeMask.test(step) ||
        afterBank->lanes[lane].values[step] != afterValue) {
        return false;
    }

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    const auto captureAfter = [this, afterBank](sequencer::SequencerHistoryPatternChange& change) {
        if (!sequencer::captureHistorySnapshotUsingReservedGraph(sequencer, change.after) ||
            !sequencer::captureSequencerCcLaneBankUsingReservedStorage(afterBank,
                                                                       change.after.ccLanes)) {
            return false;
        }
        change.after.ccLanesCaptured = true;
        return true;
    };

    if (pending.matchesCcLaneEvent(activeTrack, lane, step)) {
        auto* change = pending.preparedCcLaneChange.get();
        if (change == nullptr || !captureAfter(*change)) {
            if (change != nullptr) {
                (void)sequencer::applyHistorySnapshotToEditor(sequencer, change->before);
            }
            pending.clear();
            return false;
        }
        change->descriptor.afterValue = afterValue;
        const bool noChange = sequencer::sameMusicalHistorySnapshot(change->before, change->after);
        if (!noChange && !sequencerHistory.canRecordPattern(*change)) {
            (void)sequencer::applyHistorySnapshotToEditor(sequencer, change->before);
            pending.clear();
            return false;
        }
        pending.lastTouchedMs = nowMs;
        return true;
    }

    if (pending.pending) {
        const auto outcome = commitSequencerPatternHistoryCoalescing_();
        if (outcome == SequencerPatternHistoryCommitOutcome::Failed) { return false; }
    }

    auto change = core::app::makeExtmemUnique<sequencer::SequencerHistoryPatternChange>();
    if (!change) {
        pending.clear();
        return false;
    }
    change->trackIndex = activeTrack;
    change->storage = sequencer::SequencerHistoryPatternStorage::FullGraph;
    change->descriptor = {
        .kind = sequencer::SequencerHistoryActionKind::CcLaneEventEdit,
        .trackIndex = activeTrack,
        .laneIndex = lane,
        .stepIndex = step,
        .hasValue = true,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
    if (!sequencer::captureHistorySnapshot(sequencer, change->before) || !captureAfter(*change) ||
        !sequencerHistory.canRecordPattern(*change)) {
        pending.clear();
        return false;
    }

    pending.clear();
    pending.pending = true;
    pending.kind = SequencerDomainState::CoalescedPatternHistory::Kind::CcLaneEvent;
    pending.activeTrack = activeTrack;
    pending.step = step;
    pending.lane = lane;
    pending.lastTouchedMs = nowMs;
    pending.preparedCcLaneChange = std::move(change);
    return true;
}

FLASHMEM CoreState::SequencerPatternHistoryCommitOutcome
CoreState::commitSequencerPatternHistoryCoalescing_() {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) { return SequencerPatternHistoryCommitOutcome::NoPending; }

    const uint8_t targetTrack = pending.activeTrack;
    const bool ccLaneEvent =
        pending.kind == SequencerDomainState::CoalescedPatternHistory::Kind::CcLaneEvent;
    if (ccLaneEvent) {
        auto change = std::move(pending.preparedCcLaneChange);
        if (!change) {
            pending.clear();
            return abandonUnsafeSequencerPatternHistory_("prepared CC Lane entry missing");
        }
        if (sequencer::sameMusicalHistorySnapshot(change->before, change->after)) {
            pending.clear();
            return SequencerPatternHistoryCommitOutcome::NoChange;
        }
        if (!sequencerHistory.canRecordPattern(*change)) {
            const bool restored = sequencer::applyHistorySnapshotToTrack(
                sequencerTracks, sequencer, targetTrack, change->before);
            pending.clear();
            return restored ? SequencerPatternHistoryCommitOutcome::Failed
                            : abandonUnsafeSequencerPatternHistory_("CC Lane rollback failed");
        }

        pending.clear();
        sequencerHistory.recordPreparedPattern(std::move(change));
        markSequencerProjectMutated_();
        return SequencerPatternHistoryCommitOutcome::Committed;
    }

    const bool preparedFamily =
        pending.kind == SequencerDomainState::CoalescedPatternHistory::Kind::PreparedFamily;
    const bool activeTarget = targetTrack == sequencerTracks.activeTrackIndex();
    if (!pending.sealed || !pending.preparedPatternChange ||
        (preparedFamily && !pending.preparedPatternChange->preparedPayloadOwnerProofMatches(
                               targetTrack == sequencerTracks.activeTrackIndex()
                                   ? sequencer.pattern
                                   : sequencerTracks.track(targetTrack))) ||
        !sequencer::preparedHistoryPatternAfterMatchesTrack(
            sequencerTracks, sequencer, targetTrack, pending.preparedPatternChange->after,
            pending.preparedPatternChange->storage) ||
        (activeTarget && !sequencer::preparedActiveTrackSynchronizationMatches(
                             sequencerTracks, pending.synchronization)) ||
        (!activeTarget &&
         (!preparedFamily ||
          pending.familyOwner != sequencer::SequencerPreparedPatternEditOwner::PatternEditor))) {
        return SequencerPatternHistoryCommitOutcome::Failed;
    }

    auto& targetPattern = activeTarget ? sequencer.pattern : sequencerTracks.track(targetTrack);
    sequencer::synchronizeHistoryPatternRevisionSignals(
        targetPattern, pending.preparedPatternChange->after.flat,
        pending.preparedPatternChange->after.ccLaneRevision);

    auto change = std::move(pending.preparedPatternChange);
    auto synchronization = std::move(pending.synchronization);
    pending.clear();
    change->clearPreparedPayloadOwnerProof();

    if (activeTarget) {
        sequencer::publishPreparedActiveTrackSynchronization(
            sequencerTracks, sequencer, change->after, std::move(synchronization));
    }
    sequencerHistory.recordPreparedPattern(std::move(change));
    if (activeTarget) {
        publishPreparedSequencerMutation();
        (void)refreshSharedTrackStateFromSequencer();
    } else {
        // A lower-level Track switch may already have armed a distinct
        // mutation for the new active Track. Publishing the sealed old owner
        // must not consume that later obligation.
        markProjectMutated();
    }
    return SequencerPatternHistoryCommitOutcome::Committed;
}

FLASHMEM sequencer::SequencerPreparedPatternEditCommitOutcome
CoreState::commitSequencerPreparedPatternEdit(sequencer::SequencerPreparedPatternEditOwner owner) {
    using Outcome = sequencer::SequencerPreparedPatternEditCommitOutcome;

    const auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending ||
        pending.kind != SequencerDomainState::CoalescedPatternHistory::Kind::PreparedFamily ||
        pending.familyOwner != owner) {
        return Outcome::NoPending;
    }
    return commitSequencerPatternHistoryCoalescing_();
}

FLASHMEM sequencer::SequencerPreparedPatternEditAbortOutcome
CoreState::abortSequencerPreparedPatternEdit(
    sequencer::SequencerPreparedPatternEditOwner owner,
    uint8_t key
) {
    using Outcome = sequencer::SequencerPreparedPatternEditAbortOutcome;

    const auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) return Outcome::NoPending;
    if (pending.kind != SequencerDomainState::CoalescedPatternHistory::Kind::PreparedFamily ||
        pending.familyOwner != owner || pending.familyKey != key) {
        return Outcome::Failed;
    }
    return rollbackPreparedSequencerPatternEdit_() ? Outcome::Aborted : Outcome::Failed;
}

FLASHMEM sequencer::SequencerPatternHistoryCommitOutcome
CoreState::commitSequencerPatternHistoryCoalescingOutcome() {
    return commitSequencerPatternHistoryCoalescing_();
}

FLASHMEM bool CoreState::commitSequencerPatternHistoryCoalescing() {
    return commitSequencerPatternHistoryCoalescingOutcome() ==
           SequencerPatternHistoryCommitOutcome::Committed;
}

FLASHMEM bool CoreState::updateSequencerPatternHistoryCoalescing(uint32_t nowMs) {
    const auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) { return false; }
    if (pending.kind == SequencerDomainState::CoalescedPatternHistory::Kind::PreparedFamily) {
        return false;
    }

    const uint32_t idleMs =
        pending.kind == SequencerDomainState::CoalescedPatternHistory::Kind::CcLaneEvent
            ? SequencerDomainState::COALESCED_CC_LANE_HISTORY_IDLE_MS
            : SequencerDomainState::COALESCED_PATTERN_HISTORY_IDLE_MS;
    if (static_cast<uint32_t>(nowMs - pending.lastTouchedMs) < idleMs) { return false; }

    return commitSequencerPatternHistoryCoalescing();
}

bool CoreState::hasPendingSequencerPatternHistoryCoalescing() const {
    return sequencerDomain_.coalescedPatternHistory.pending;
}

}  // namespace core::state
