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

#include "diagnostics/StorageQualificationProbe.hpp"
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
#include "state/sequencer/SequencerSnapshotOps.hpp"
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

constexpr uint32_t stepToggleQualificationDetail(
    uint8_t key,
    uint8_t activeTrack,
    sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan
) {
    return static_cast<uint32_t>(key) |
           (static_cast<uint32_t>(activeTrack) << 8U) |
           (static_cast<uint32_t>(payloadPlan) << 16U);
}

void recordStepToggleQualification(
    sequencer::SequencerPreparedPatternEditOwner owner,
    core::diagnostics::storage_qualification::PhaseKind phase,
    uint8_t result,
    uint32_t detail
) {
    if (owner != sequencer::SequencerPreparedPatternEditOwner::StepToggle) return;
    core::diagnostics::storage_qualification::recordWriter(
        core::diagnostics::storage_qualification::OperationKind::StepToggle,
        phase,
        result,
        detail
    );
}

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

FLASHMEM void CoreState::clearPreparedSequencerPatternEditWithoutLiveRestore_() {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const bool restoreGenericMutation = pending.genericMutationPendingAtBegin;
    consumePendingSequencerMutation_();
    if (restoreGenericMutation) {
        auto* coalescer = sequencerDomain_.mutationCoalescer.get();
        if (coalescer != nullptr) coalescer->markChanged();
    }
    pending.clear();
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
        result.outcome = Outcome::HistoryUnavailable;
        return result;
    }

    if (sequencer.stepContentDraft.active.get()) {
        sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::PROJECT_LOAD);
        result.outcome = Outcome::Blocked;
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
        result.outcome = Outcome::ResourceUnavailable;
        return result;
    }

    auto stagedBank = core::app::makeExtmemUnique<sequencer::SequencerTrackBankState>();
    if (!stagedBank) {
        result.outcome = Outcome::ResourceUnavailable;
        return result;
    }
    auto stagedActive = core::app::makeExtmemUnique<sequencer::SequencerState>();
    if (!stagedActive) {
        result.outcome = Outcome::ResourceUnavailable;
        return result;
    }

    if (!sequencer::populatePreparedHistoryFullBankStaging(
            sequencerTracks, sequencer, change->before, *stagedBank, *stagedActive)) {
        // Source topology was validated above and the staging roots already
        // exist. The remaining fallible work is payload cloning into PSRAM.
        result.outcome = Outcome::ResourceUnavailable;
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
        result.outcome = stagedMutation.changed ? Outcome::HistoryUnavailable : Outcome::Blocked;
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

FLASHMEM bool CoreState::canRecordSequencerStructureHistory(
    const sequencer::SequencerHistoryTrackStructureChange& change) const {
    return !sequencer.stepContentDraft.active.get() && sequencerHistory.canRecordStructure(change);
}

FLASHMEM void CoreState::commitAdmittedSequencerStructureHistory(
    sequencer::SequencerHistoryTrackStructureChangePtr change
) noexcept {
    const bool directMacroTrackAction = change && change->macroStructure &&
        change->macroStructure->affectedTrackIndex != sequencer::
            SequencerHistoryMacroTrackStructurePayload::
                INVALID_AFFECTED_TRACK;
    sequencerHistory.commitAdmittedStructure(std::move(change));
    publishPreparedSequencerMutation(!directMacroTrackAction);
}

FLASHMEM void CoreState::publishPreparedSequencerMutation(
    bool notifyProjectNavigation
) {
    // The prepared transaction already performed the coalescer action's
    // editor-to-bank synchronization. Cancel only this coalescer's queued
    // callbacks (including later entries in an active notification wave)
    // and consume an already-armed mark before publishing directly.
    consumePendingSequencerMutation_();
    if (notifyProjectNavigation) {
        markProjectMutated();
    } else {
        markProjectDurableMutation_();
    }
}

FLASHMEM sequencer::SequencerHistoryOpenOutcome
CoreState::beginOrContinueSequencerPatternHistoryCoalescing(
    uint8_t step, sequencer::StepProperty property, uint32_t nowMs,
    sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan, bool stateProperty) {
    using Outcome = sequencer::SequencerHistoryOpenOutcome;
    if (step >= sequencer::SequencerPatternState::MAX_STEPS) { return Outcome::Blocked; }

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();

    if (pending.matchesStepProperty(activeTrack, step, property, stateProperty)) {
        // The stable grouping key intentionally excludes storage policy. A
        // plan drift is a caller-classification bug; reject it atomically
        // rather than splitting one 500 ms gesture into two Undo entries.
        if (pending.payloadPlan != payloadPlan) return Outcome::Blocked;
        if (!pending.sealed ||
            !pending.preparedPatternChange ||
            !pending.preparedPatternChange->preparedPayloadOwnerProofMatches(
                sequencer.pattern) ||
            !sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks,
                                                                  pending.synchronization)) {
            return Outcome::HistoryUnavailable;
        }
        consumePendingSequencerMutation_(&pending.genericMutationPendingAtBegin);
        pending.sealed = false;
        pending.lastTouchedMs = nowMs;
        return Outcome::Continued;
    }

    if (pending.pending) {
        const auto outcome = commitSequencerPatternHistoryCoalescing_();
        if (outcome == SequencerPatternHistoryCommitOutcome::Failed) { return Outcome::HistoryUnavailable; }
    }

    sequencer::SequencerHistoryGraphPtr prospectiveGraph;
    auto change = sequencer::prepareHistoryPatternChangeBefore(
        sequencerTracks, sequencer, activeTrack, payloadPlan, prospectiveGraph);
    if (!change || !sequencer::reservePreparedHistoryPatternAfter(sequencerTracks, sequencer,
                                                                  *change, payloadPlan)) {
        return Outcome::ResourceUnavailable;
    }

    sequencer::SequencerPreparedActiveTrackSynchronization synchronization;
    if (!sequencer::reservePreparedActiveTrackSynchronization(
            sequencerTracks, sequencer, activeTrack, payloadPlan, synchronization)) {
        return Outcome::ResourceUnavailable;
    }
    if (activeTrack != sequencerTracks.activeTrackIndex() ||
        !sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks, synchronization)) {
        return Outcome::HistoryUnavailable;
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
            return Outcome::HistoryUnavailable;
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
    return Outcome::Started;
}

FLASHMEM sequencer::SequencerPreparedPatternEditBeginOutcome
CoreState::beginOrContinueSequencerPreparedPatternEdit(
    sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
    sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
    sequencer::SequencerHistoryDescriptor descriptor, bool compactGraphOnSeal) {
    using Outcome = sequencer::SequencerPreparedPatternEditBeginOutcome;

    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    const uint32_t qualificationDetail =
        stepToggleQualificationDetail(key, activeTrack, payloadPlan);
    recordStepToggleQualification(
        owner,
        core::diagnostics::storage_qualification::PhaseKind::Begin,
        0U,
        qualificationDetail
    );
    const auto finish = [owner, qualificationDetail](Outcome outcome) {
        const bool accepted = outcome == Outcome::Started ||
                              outcome == Outcome::Continued;
        recordStepToggleQualification(
            owner,
            accepted
                ? core::diagnostics::storage_qualification::PhaseKind::Admit
                : core::diagnostics::storage_qualification::PhaseKind::Cancel,
            static_cast<uint8_t>(outcome),
            qualificationDetail
        );
        return outcome;
    };

    switch (owner) {
        case sequencer::SequencerPreparedPatternEditOwner::PatternPitch:
        case sequencer::SequencerPreparedPatternEditOwner::PropertySelector:
        case sequencer::SequencerPreparedPatternEditOwner::StepContent:
        case sequencer::SequencerPreparedPatternEditOwner::StepEditSession:
        case sequencer::SequencerPreparedPatternEditOwner::StepToggle:
        case sequencer::SequencerPreparedPatternEditOwner::PatternEditor:
        case sequencer::SequencerPreparedPatternEditOwner::PageStructure:
        case sequencer::SequencerPreparedPatternEditOwner::QuickControls: break;
        default: return finish(Outcome::Blocked);
    }

    auto& pending = sequencerDomain_.coalescedPatternHistory;
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
            return finish(Outcome::HistoryUnavailable);
        }
        consumePendingSequencerMutation_(&pending.genericMutationPendingAtBegin);
        pending.sealed = false;
        return finish(Outcome::Continued);
    }

    if (pending.pending) {
        const auto outcome = commitSequencerPatternHistoryCoalescing_();
        if (outcome == SequencerPatternHistoryCommitOutcome::Failed) {
            return finish(Outcome::HistoryUnavailable);
        }
    }

    sequencer::SequencerHistoryGraphPtr prospectiveGraph;
    auto change = sequencer::prepareHistoryPatternChangeBefore(
        sequencerTracks, sequencer, activeTrack, payloadPlan, prospectiveGraph, descriptor);
    if (!change || !sequencer::reservePreparedHistoryPatternAfter(sequencerTracks, sequencer,
                                                                  *change, payloadPlan)) {
        return finish(Outcome::ResourceUnavailable);
    }

    sequencer::SequencerPreparedActiveTrackSynchronization synchronization;
    if (!sequencer::reservePreparedActiveTrackSynchronization(
            sequencerTracks, sequencer, activeTrack, payloadPlan, synchronization)) {
        return finish(Outcome::ResourceUnavailable);
    }
    if (activeTrack != sequencerTracks.activeTrackIndex() ||
        !sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks, synchronization)) {
        return finish(Outcome::HistoryUnavailable);
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
            return finish(Outcome::HistoryUnavailable);
        }
        sequencer.pattern.graph = std::move(prospectiveGraph);
        pending.prospectiveGraphInstalled = true;
    }
    pending.preparedPatternChange->setPreparedPayloadOwnerProof(sequencer.pattern);
    consumePendingSequencerMutation_(&pending.genericMutationPendingAtBegin);
    return finish(Outcome::Started);
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
        !pending.preparedPatternChange->preparedPayloadOwnerProofMatches(
            sequencer.pattern) ||
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
    const uint32_t qualificationDetail = stepToggleQualificationDetail(
        key,
        sequencerTracks.activeTrackIndex(),
        pending.payloadPlan
    );
    const auto finish = [owner, qualificationDetail](Outcome outcome) {
        recordStepToggleQualification(
            owner,
            outcome == Outcome::Sealed
                ? core::diagnostics::storage_qualification::PhaseKind::End
                : core::diagnostics::storage_qualification::PhaseKind::Cancel,
            static_cast<uint8_t>(outcome),
            qualificationDetail
        );
        return outcome;
    };
    if (!pending.pending ||
        pending.kind != SequencerDomainState::CoalescedPatternHistory::Kind::PreparedFamily ||
        pending.familyOwner != owner || pending.familyKey != key ||
        pending.sealed || !pending.preparedPatternChange) {
        return finish(Outcome::Failed);
    }
    const bool synchronizationMatches =
        sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks,
                                                              pending.synchronization);
    const bool payloadOwnerProofMatches =
        pending.preparedPatternChange->preparedPayloadOwnerProofMatches(sequencer.pattern);
    if (!synchronizationMatches ||
        !payloadOwnerProofMatches) {
        if (rollbackPreparedSequencerPatternEdit_()) return finish(Outcome::FailedClosed);
        OC_LOG_ERROR(kPreparedFamilyIdentityRollbackFailed);
        return finish(Outcome::Failed);
    }

    if (!mutationChanged) {
        if (pending.hasChange) {
            consumePendingSequencerMutation_();
            pending.sealed = true;
            return finish(Outcome::Sealed);
        }
        // False means no musical delta, not necessarily no preparatory write:
        // graph-backed setters can enable the prospective owner before their
        // final equality check. Roll back the complete prepared state.
        return finish(
            rollbackPreparedSequencerPatternEdit_() ? Outcome::Cleared : Outcome::Failed
        );
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
        return finish(finishSequencerPreparedPatternEdit_(descriptor, nullptr, false));
    }
    if (pending.graphCompactionRequested()) {
        return finish(sealSequencerPreparedPatternEditWithGraphCompaction_(descriptor));
    }

    return finish(finishSequencerPreparedPatternEdit_(descriptor, nullptr, false));
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

FLASHMEM sequencer::SequencerHistoryOpenOutcome
CoreState::beginOrContinueSequencerCcLaneEventHistoryCoalescing(
    uint8_t lane, uint8_t step, int32_t beforeValue, int32_t afterValue,
    const sequencer::SequencerCcLaneBank* afterBank, uint32_t nowMs) {
    using Outcome = sequencer::SequencerHistoryOpenOutcome;
    if (lane >= sequencer::SequencerCcLaneBank::MAX_LANES ||
        step >= sequencer::SequencerCcLaneBank::MAX_STEPS || beforeValue < -1 ||
        beforeValue > 127 || afterValue < 0 || afterValue > 127 || afterBank == nullptr ||
        !afterBank->lanes[lane].occupied || !afterBank->lanes[lane].activeMask.test(step) ||
        afterBank->lanes[lane].values[step] != afterValue) {
        return Outcome::Blocked;
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
            return Outcome::HistoryUnavailable;
        }
        change->descriptor.afterValue = afterValue;
        const bool noChange = sequencer::sameMusicalHistorySnapshot(change->before, change->after);
        if (!noChange && !sequencerHistory.canRecordPattern(*change)) {
            (void)sequencer::applyHistorySnapshotToEditor(sequencer, change->before);
            pending.clear();
            return Outcome::HistoryUnavailable;
        }
        pending.lastTouchedMs = nowMs;
        return Outcome::Continued;
    }

    if (pending.pending) {
        const auto outcome = commitSequencerPatternHistoryCoalescing_();
        if (outcome == SequencerPatternHistoryCommitOutcome::Failed) { return Outcome::HistoryUnavailable; }
    }

    auto change = core::app::makeExtmemUnique<sequencer::SequencerHistoryPatternChange>();
    if (!change) {
        pending.clear();
        return Outcome::ResourceUnavailable;
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
    if (!sequencer::captureHistorySnapshot(sequencer, change->before) || !captureAfter(*change)) {
        pending.clear();
        return Outcome::ResourceUnavailable;
    }
    if (!sequencerHistory.canRecordPattern(*change)) {
        pending.clear();
        return Outcome::HistoryUnavailable;
    }

    pending.clear();
    pending.pending = true;
    pending.kind = SequencerDomainState::CoalescedPatternHistory::Kind::CcLaneEvent;
    pending.activeTrack = activeTrack;
    pending.step = step;
    pending.lane = lane;
    pending.lastTouchedMs = nowMs;
    pending.preparedCcLaneChange = std::move(change);
    return Outcome::Started;
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
        !pending.preparedPatternChange->preparedPayloadOwnerProofMatches(
            activeTarget ? sequencer.pattern : sequencerTracks.track(targetTrack)) ||
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
    const uint32_t qualificationDetail = stepToggleQualificationDetail(
        pending.familyKey,
        pending.activeTrack,
        pending.payloadPlan
    );
    if (!pending.pending ||
        pending.kind != SequencerDomainState::CoalescedPatternHistory::Kind::PreparedFamily ||
        pending.familyOwner != owner) {
        recordStepToggleQualification(
            owner,
            core::diagnostics::storage_qualification::PhaseKind::Cancel,
            static_cast<uint8_t>(Outcome::NoPending),
            qualificationDetail
        );
        return Outcome::NoPending;
    }
    const auto outcome = commitSequencerPatternHistoryCoalescing_();
    recordStepToggleQualification(
        owner,
        outcome == Outcome::Committed
            ? core::diagnostics::storage_qualification::PhaseKind::Complete
            : core::diagnostics::storage_qualification::PhaseKind::Cancel,
        static_cast<uint8_t>(outcome),
        qualificationDetail
    );
    return outcome;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM sequencer::SequencerPreparedPatternEditCommitOutcome
CoreState::applySequencerPreparedQuickControlsEdit(
    uint8_t key,
    sequencer::SequencerHistoryDescriptor descriptor
) {
    using CommitOutcome = sequencer::SequencerPreparedPatternEditCommitOutcome;
    using Owner = sequencer::SequencerPreparedPatternEditOwner;

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    auto* draft = sequencer.quickControlsDraft.pattern();
    if (!pending.pending ||
        pending.kind !=
            SequencerDomainState::CoalescedPatternHistory::Kind::PreparedFamily ||
        pending.activeTrack != activeTrack ||
        pending.familyOwner != Owner::QuickControls || pending.familyKey != key ||
        pending.sealed || pending.hasChange ||
        pending.payloadPlan !=
            sequencer::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload ||
        pending.graphCompactionRequested() || pending.prospectiveGraphInstalled ||
        !pending.preparedPatternChange || draft == nullptr) {
        return CommitOutcome::Failed;
    }

    auto& change = *pending.preparedPatternChange;
    const bool exactLiveBefore =
        change.storage == sequencer::SequencerHistoryPatternStorage::FullGraph &&
        change.trackIndex == activeTrack &&
        change.descriptor.trackIndex == activeTrack &&
        sequencer::preparedActiveTrackSynchronizationMatches(
            sequencerTracks,
            pending.synchronization
        ) &&
        change.preparedPayloadOwnerProofMatches(sequencer.pattern) &&
        sequencer::liveHistoryPatternSnapshotMatches(
            sequencer.pattern,
            change.before
        );
    const bool candidateOwnerShapeMatches =
        change.preparedGraphOwnerProofPresent() == (draft->graph != nullptr) &&
        change.preparedCcLaneOwnerProofPresent() == (draft->ccLanes != nullptr);
    if (!exactLiveBefore || !candidateOwnerShapeMatches) {
        clearPreparedSequencerPatternEditWithoutLiveRestore_();
        return CommitOutcome::Failed;
    }

    const bool afterCaptured =
        sequencer::captureDetachedHistorySnapshotUsingReservedStorage(
            *draft,
            sequencer.focusedStep.get(),
            change.after
        );
    const bool synchronizationCaptured = afterCaptured &&
        sequencer::refreshPreparedActiveTrackSynchronizationUsingReservedStorage(
            sequencerTracks,
            *draft,
            pending.synchronization
        );
    if (!afterCaptured || !synchronizationCaptured) {
        clearPreparedSequencerPatternEditWithoutLiveRestore_();
        return CommitOutcome::Failed;
    }

    descriptor.trackIndex = activeTrack;
    change.descriptor = descriptor;
    if (sequencer::sameMusicalHistorySnapshot(change.before, change.after)) {
        clearPreparedSequencerPatternEditWithoutLiveRestore_();
        return CommitOutcome::NoChange;
    }
    if (!sequencerHistory.canRecordPattern(change)) {
        clearPreparedSequencerPatternEditWithoutLiveRestore_();
        return CommitOutcome::Failed;
    }

    // First live musical write: every candidate/history/bank owner has already
    // been captured and admitted. The draft takes the obsolete live payload so
    // both publication and any invariant unwind remain allocation-free.
    const sequencer::SequencerPatternSnapshot beforeFlat = change.before.flat;
    const uint32_t beforeCcLaneRevision = change.before.ccLaneRevision;
    sequencer.quickControlsDraft.suspendPreview();
    std::swap(sequencer.pattern.graph, draft->graph);
    std::swap(sequencer.pattern.ccLanes, draft->ccLanes);
    sequencer::applySnapshotToEditorPreservingGraph(sequencer, change.after.flat);
    sequencer::synchronizeHistoryPatternRevisionSignals(
        sequencer.pattern,
        change.after.flat,
        change.after.ccLaneRevision
    );
    change.setPreparedPayloadOwnerProof(sequencer.pattern);
    consumePendingSequencerMutation_();
    pending.hasChange = true;
    pending.sealed = true;

    const auto commitOutcome = commitSequencerPatternHistoryCoalescing_();
    if (commitOutcome == CommitOutcome::Committed) return commitOutcome;

    std::swap(sequencer.pattern.graph, draft->graph);
    std::swap(sequencer.pattern.ccLanes, draft->ccLanes);
    sequencer::applySnapshotToEditorPreservingGraph(sequencer, beforeFlat);
    sequencer::synchronizeHistoryPatternRevisionSignals(
        sequencer.pattern,
        beforeFlat,
        beforeCcLaneRevision
    );
    sequencer.quickControlsDraft.resumePreview();
    clearPreparedSequencerPatternEditWithoutLiveRestore_();
    return CommitOutcome::Failed;
}

FLASHMEM sequencer::SequencerPreparedPatternEditAbortOutcome
CoreState::abortSequencerPreparedPatternEdit(
    sequencer::SequencerPreparedPatternEditOwner owner,
    uint8_t key
) {
    using Outcome = sequencer::SequencerPreparedPatternEditAbortOutcome;

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) return Outcome::NoPending;
    if (pending.kind != SequencerDomainState::CoalescedPatternHistory::Kind::PreparedFamily ||
        pending.familyOwner != owner || pending.familyKey != key) {
        return Outcome::Failed;
    }
    if (owner == sequencer::SequencerPreparedPatternEditOwner::QuickControls &&
        pending.payloadPlan ==
            sequencer::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload &&
        !pending.graphCompactionRequested() && !pending.sealed &&
        !pending.hasChange && !pending.prospectiveGraphInstalled &&
        pending.preparedPatternChange &&
        sequencer::preparedActiveTrackSynchronizationMatches(sequencerTracks,
                                                              pending.synchronization) &&
        pending.preparedPatternChange->preparedPayloadOwnerProofMatches(
            sequencer.pattern) &&
        sequencer::liveHistoryPatternSnapshotMatches(
            sequencer.pattern,
            pending.preparedPatternChange->before)) {
        clearPreparedSequencerPatternEditWithoutLiveRestore_();
        return Outcome::Aborted;
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
