#include "handler/sequencer/SequencerPreparedTrackStructureTransaction.hpp"
#include "handler/sequencer/SequencerPreparedTrackStructurePlanValidation.hpp"

#include <cstring>
#include <new>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerStepContentDraftSession.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::handler {
namespace {

using Action = SequencerPreparedTrackStructureAction;
using MacroOutcome = SequencerPreparedTrackStructureMacroOutcome;
using Plan = SequencerPreparedTrackStructurePlan;
using PlanOutcome = SequencerPreparedTrackStructurePlanOutcome;
using Result = SequencerPreparedTrackStructureResult;
using Status = SequencerPreparedTrackStructureStatus;
using TrackBank = core::state::sequencer::SequencerTrackBankState;
using StructureChange =
    core::state::sequencer::SequencerHistoryTrackStructureChange;
using StructureChangePtr =
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr;

using prepared_track_structure_detail::actionIsValid;
using prepared_track_structure_detail::allowsTransientNoChange;
using prepared_track_structure_detail::isMacroAction;
using prepared_track_structure_detail::samePlan;
using prepared_track_structure_detail::trackBit;
using prepared_track_structure_detail::trackCount;
using prepared_track_structure_detail::validActionPlan;
using prepared_track_structure_detail::validOperations;

FLASHMEM Status statusForPlanOutcome(PlanOutcome outcome) noexcept {
    switch (outcome) {
        case PlanOutcome::Stale:
            return Status::Stale;
        case PlanOutcome::Invalid:
        default:
            return Status::Invalid;
    }
}

FLASHMEM Status statusForChronology(
    const core::state::sequencer::SequencerTrackStructureChronologyResult&
        chronology
) noexcept {
    using BoundaryStatus = core::state::sequencer::
        SequencerTrackStructureChronologyStatus;
    using PatternOutcome =
        core::state::sequencer::SequencerPatternHistoryCommitOutcome;
    switch (chronology.status) {
        case BoundaryStatus::MacroAuditionBlocked:
        case BoundaryStatus::ProjectTrackGestureBlocked:
            return Status::Stale;
        case BoundaryStatus::Opened:
            switch (chronology.predecessorPattern) {
                case PatternOutcome::NoPending:
                case PatternOutcome::NoChange:
                case PatternOutcome::Committed:
                    return Status::Prepared;
                case PatternOutcome::Failed:
                default:
                    return Status::HistoryUnavailable;
            }
        case BoundaryStatus::Unavailable:
        case BoundaryStatus::PatternFailed:
        default:
            return Status::HistoryUnavailable;
    }
}

FLASHMEM core::state::sequencer::SequencerTrackFlatSnapshotView flatView(
    const core::state::sequencer::SequencerHistoryPatternSnapshot& snapshot
) noexcept {
    return {&snapshot.flat, snapshot.ccLaneRevision};
}

FLASHMEM bool macroAfterRespectsAction(
    Action action,
    const Plan& plan,
    const core::state::sequencer::
        SequencerHistoryMacroTrackStructurePayload& payload
) noexcept {
    if (payload.capturedTrackMask != plan.macroCapturedTrackMask ||
        payload.affectedTrackIndex != plan.macroAffectedTrack ||
        payload.beforeControl == nullptr || payload.afterControl == nullptr) {
        return false;
    }
    for (uint8_t track = 0U;
         track < core::state::macro::TRACK_COUNT;
         ++track) {
        const bool captured =
            (payload.capturedTrackMask & trackBit(track)) != 0U;
        const bool target = track == plan.macroAffectedTrack;
        const bool mayChange = target && action != Action::MacroDelete;
        if ((!captured || !mayChange) &&
            std::memcmp(
                &payload.beforeTracks[track],
                &payload.afterTracks[track],
                sizeof(core::state::macro::MacroTrackData)
            ) != 0) {
            return false;
        }
    }
    return true;
}

FLASHMEM void normalizeEqualMacroAfterControl(
    core::state::sequencer::SequencerHistoryMacroTrackStructurePayload& payload
) noexcept {
    if (payload.beforeControl != nullptr && payload.afterControl != nullptr &&
        std::memcmp(
            payload.beforeControl.get(),
            payload.afterControl.get(),
            sizeof(core::state::modulation::ProjectControlDomainState)
        ) == 0) {
        payload.afterControl.reset();
    }
}

FLASHMEM core::state::sequencer::SequencerHistoryDescriptor descriptorFor(
    const StructureChange& change
) noexcept {
    const int32_t beforeCount = core::state::sequencer::
        sequencerHistoryEnabledTrackCount(change.before.enabledMask);
    const int32_t afterCount = core::state::sequencer::
        sequencerHistoryEnabledTrackCount(change.after.enabledMask);
    return {
        .kind = core::state::sequencer::
            SequencerHistoryActionKind::TrackStructure,
        .trackIndex = TrackBank::clampTrackIndex(change.after.activeTrack),
        .hasValue = beforeCount != afterCount,
        .beforeValue = beforeCount,
        .afterValue = afterCount,
    };
}

FLASHMEM bool semanticChange(const StructureChange& change) noexcept {
    return !core::state::sequencer::sameMusicalHistoryStructureSnapshot(
               change.before,
               change.after
           ) ||
           core::state::sequencer::macroTrackStructureHistoryChanged(change);
}

// The only Track Structure sink authorized past the immutable admission gate.
FLASHMEM void commitAdmittedStructure(
    const SequencerHistoryDomainServices& history,
    StructureChangePtr change
) noexcept {
    history.commitAdmittedStructure(std::move(change));
}

FLASHMEM Result result(Status status,
                       const core::state::sequencer::
                           SequencerTrackStructureChronologyResult& chronology) {
    return {status, chronology};
}

}  // namespace

FLASHMEM bool PreparedSequencerTrackStructureTransaction::
    captureOwnerIdentities_(
        const TrackBank& tracks,
        const core::state::sequencer::SequencerState& sequencer,
        uint16_t capturedMask,
        std::array<OwnerIdentity, 2U>& out,
        uint8_t& outCount
    ) noexcept {
    out = {};
    outCount = 0U;
    for (uint8_t track = 0U; track < TrackBank::TRACK_COUNT; ++track) {
        const uint16_t bit = trackBit(track);
        if ((capturedMask & bit) == 0U) continue;
        if (outCount >= out.size()) return false;
        const auto& live = track == tracks.activeTrackIndex()
            ? sequencer.pattern
            : tracks.track(track);
        out[outCount++] = {
            live.graph.get(),
            live.ccLanes.get(),
            track,
        };
    }
    return outCount == trackCount(capturedMask);
}

FLASHMEM bool PreparedSequencerTrackStructureTransaction::
    ownerIdentitiesMatch_(
        const TrackBank& tracks,
        const core::state::sequencer::SequencerState& sequencer,
        const std::array<OwnerIdentity, 2U>& expected,
        uint8_t count
    ) noexcept {
    if (count == 0U || count > expected.size()) return false;
    for (uint8_t index = 0U; index < count; ++index) {
        const auto& identity = expected[index];
        if (identity.track >= TrackBank::TRACK_COUNT) return false;
        const auto& live = identity.track == tracks.activeTrackIndex()
            ? sequencer.pattern
            : tracks.track(identity.track);
        if (live.graph.get() != identity.graph ||
            live.ccLanes.get() != identity.ccLanes) {
            return false;
        }
    }
    return true;
}

FLASHMEM PreparedSequencerTrackStructureTransaction::
    ~PreparedSequencerTrackStructureTransaction() = default;

FLASHMEM PreparedSequencerTrackStructureTransaction::
    PreparedSequencerTrackStructureTransaction(
        PreparedSequencerTrackStructureTransaction&& other
    ) noexcept
    : status_(other.status_),
      chronology_(other.chronology_),
      plan_(other.plan_),
      change_(std::move(other.change_)),
      topologyGuard_(other.topologyGuard_),
      activationGuard_(other.activationGuard_),
      settlementCheckpoint_(other.settlementCheckpoint_),
      tracks_(other.tracks_),
      sequencer_(other.sequencer_),
      macroPages_(other.macroPages_),
      activationQueue_(other.activationQueue_),
      sharedTracks_(other.sharedTracks_),
      history_(other.history_),
      execution_(other.execution_) {
    other.status_ = Status::Invalid;
    other.tracks_ = nullptr;
    other.sequencer_ = nullptr;
    other.macroPages_ = nullptr;
    other.activationQueue_ = nullptr;
}

FLASHMEM PreparedSequencerTrackStructureTransaction&
PreparedSequencerTrackStructureTransaction::operator=(
    PreparedSequencerTrackStructureTransaction&& other
) noexcept {
    if (this == &other) return *this;
    status_ = other.status_;
    chronology_ = other.chronology_;
    plan_ = other.plan_;
    change_ = std::move(other.change_);
    topologyGuard_ = other.topologyGuard_;
    activationGuard_ = other.activationGuard_;
    settlementCheckpoint_ = other.settlementCheckpoint_;
    tracks_ = other.tracks_;
    sequencer_ = other.sequencer_;
    macroPages_ = other.macroPages_;
    activationQueue_ = other.activationQueue_;
    sharedTracks_ = other.sharedTracks_;
    history_ = other.history_;
    execution_ = other.execution_;
    other.status_ = Status::Invalid;
    other.tracks_ = nullptr;
    other.sequencer_ = nullptr;
    other.macroPages_ = nullptr;
    other.activationQueue_ = nullptr;
    return *this;
}

FLASHMEM PreparedSequencerTrackStructureTransaction
prepareSequencerTrackStructureTransaction(
    SequencerPreparedTrackStructureStateRefs state,
    Action action,
    SequencerPreparedTrackStructureExecution execution
) {
    PreparedSequencerTrackStructureTransaction prepared(state, execution);
    prepared.plan_.action = action;
    if (state.sequencer.stepContentDraft.rejectTransitionIfActive(
            core::state::sequencer::
                SequencerStepContentDraftBlockedTransition::TRACK
        )) {
        prepared.status_ = Status::DraftBlocked;
        return prepared;
    }

    const auto* operations = execution.operations_;
    if (!actionIsValid(action) || !validOperations(operations, action)) {
        return prepared;
    }
    if (isMacroAction(action) && state.macroPages == nullptr) {
        prepared.status_ = Status::PublicationUnavailable;
        return prepared;
    }

    Plan initialPlan{};
    const PlanOutcome initialOutcome = operations->buildPlan(
        execution.context_,
        action,
        initialPlan
    );
    if (initialOutcome != PlanOutcome::Ready) {
        prepared.status_ = statusForPlanOutcome(initialOutcome);
        return prepared;
    }
    if (!validActionPlan(
            initialPlan,
            action,
            state.tracks,
            state.sequencer,
            state.sharedTracks,
            state.macroPages
        )) {
        return prepared;
    }

    prepared.chronology_ =
        state.history.openTrackStructureChronologyBoundary();
    prepared.status_ = statusForChronology(prepared.chronology_);
    if (prepared.status_ != Status::Prepared) return prepared;

    if (!state.sharedTracks.
            capturePreparedTrackStructureSettlementCheckpoint(
                prepared.settlementCheckpoint_
            )) {
        prepared.status_ = Status::PublicationUnavailable;
        return prepared;
    }

    Plan authoritativePlan{};
    const PlanOutcome authoritativeOutcome = operations->buildPlan(
        execution.context_,
        action,
        authoritativePlan
    );
    if (authoritativeOutcome != PlanOutcome::Ready ||
        !samePlan(initialPlan, authoritativePlan) ||
        !validActionPlan(
            authoritativePlan,
            action,
            state.tracks,
            state.sequencer,
            state.sharedTracks,
            state.macroPages
        )) {
        prepared.status_ = Status::Stale;
        return prepared;
    }
    prepared.plan_ = authoritativePlan;

    const uint16_t protectedTrackMask = static_cast<uint16_t>(
        prepared.plan_.affectedTrackMask |
        trackBit(prepared.plan_.beforeActiveTrack) |
        trackBit(prepared.plan_.afterActiveTrack)
    );
    if (!state.activationQueue.captureMutationGuard(
            protectedTrackMask,
            prepared.activationGuard_
        )) {
        prepared.status_ = Status::Stale;
        return prepared;
    }
    if (prepared.plan_.beforeActiveTrack == prepared.plan_.afterActiveTrack) {
        ::new (static_cast<void*>(
            &prepared.topologyGuard_.ownerIdentities
        )) decltype(prepared.topologyGuard_.ownerIdentities){};
        uint8_t ownerIdentityCount = 0U;
        if (!PreparedSequencerTrackStructureTransaction::
                captureOwnerIdentities_(
                state.tracks,
                state.sequencer,
                prepared.plan_.capturedTrackMask,
                prepared.topologyGuard_.ownerIdentities,
                ownerIdentityCount
            ) ||
            ownerIdentityCount != trackCount(
                prepared.plan_.capturedTrackMask
            )) {
            prepared.status_ = Status::Stale;
            return prepared;
        }
    }

    prepared.change_ = core::state::sequencer::
        prepareHistoryStructureChangeBefore(
            state.tracks,
            state.sequencer,
            prepared.plan_.capturedTrackMask
        );
    if (!prepared.change_) {
        prepared.status_ = Status::AllocationUnavailable;
        return prepared;
    }
    if (prepared.change_->before.enabledMask !=
            prepared.plan_.beforeEnabledMask ||
        prepared.change_->before.activeTrack !=
            prepared.plan_.beforeActiveTrack ||
        prepared.change_->before.focusedStep !=
            prepared.plan_.beforeFocusedStep ||
        prepared.change_->before.page != prepared.plan_.beforePage ||
        prepared.change_->before.capturedTrackMask !=
            prepared.plan_.capturedTrackMask ||
        !core::state::sequencer::liveHistoryStructureSnapshotMatches(
            state.tracks,
            state.sequencer,
            prepared.change_->before
        )) {
        prepared.status_ = Status::Stale;
        return prepared;
    }

    if (!core::state::sequencer::buildHistoryStructureSnapshotAfterFromBefore(
            *prepared.change_,
            prepared.plan_.afterEnabledMask,
            prepared.plan_.afterActiveTrack,
            prepared.plan_.afterFocusedStep,
            prepared.plan_.afterPage,
            prepared.plan_.canonicalResetTrackMask
        )) {
        prepared.status_ = Status::AllocationUnavailable;
        return prepared;
    }

    if (isMacroAction(action)) {
        if (state.macroPages == nullptr ||
            !core::state::sequencer::captureMacroTrackStructureHistoryBefore(
                *state.macroPages,
                prepared.plan_.macroCapturedTrackMask,
                *prepared.change_,
                prepared.plan_.macroAffectedTrack
            )) {
            prepared.status_ = Status::AllocationUnavailable;
            return prepared;
        }
        auto& payload = *prepared.change_->macroStructure;
        payload.afterTracks = payload.beforeTracks;
        *payload.afterControl = *payload.beforeControl;
        const MacroOutcome macroOutcome = operations->prepareMacroAfter(
            execution.context_,
            prepared.plan_,
            payload.afterTracks,
            *payload.afterControl
        );
        if (macroOutcome != MacroOutcome::Ready) {
            prepared.status_ = macroOutcome == MacroOutcome::Stale
                ? Status::Stale
                : Status::Invalid;
            return prepared;
        }
        if (!macroAfterRespectsAction(action, prepared.plan_, payload)) {
            prepared.status_ = Status::Invalid;
            return prepared;
        }
        if (!core::state::modulation::validProjectModulationDomain(
                payload.afterControl->modulation,
                payload.afterControl->curves,
                &payload.afterControl->automation
            )) {
            prepared.status_ = Status::Invalid;
            return prepared;
        }
        payload.afterCaptured = true;
        normalizeEqualMacroAfterControl(payload);
    }

    prepared.change_->descriptor = descriptorFor(*prepared.change_);
    if (!semanticChange(*prepared.change_)) {
        if (!allowsTransientNoChange(action)) {
            prepared.status_ = Status::Invalid;
            return prepared;
        }
        prepared.status_ = Status::Prepared;
        return prepared;
    }

    if (!state.history.canCommitAdmittedStructure(*prepared.change_)) {
        prepared.status_ = Status::HistoryUnavailable;
        return prepared;
    }
    if (!state.sharedTracks.canPublishPreparedSequencerState()) {
        prepared.status_ = Status::PublicationUnavailable;
        return prepared;
    }

    if (prepared.plan_.beforeActiveTrack !=
        prepared.plan_.afterActiveTrack) {
        const auto& beforeOutgoing = prepared.change_->before.tracks[
            prepared.plan_.beforeActiveTrack
        ];
        const auto& beforeIncoming = prepared.change_->before.tracks[
            prepared.plan_.afterActiveTrack
        ];
        const auto& afterOutgoing = prepared.change_->after.tracks[
            prepared.plan_.beforeActiveTrack
        ];
        const auto& afterIncoming = prepared.change_->after.tracks[
            prepared.plan_.afterActiveTrack
        ];
        if (!core::state::sequencer::prepareActiveTrackOwnerRotation(
                state.tracks,
                state.sequencer,
                prepared.plan_.afterActiveTrack,
                flatView(beforeOutgoing),
                flatView(beforeIncoming),
                flatView(afterOutgoing),
                flatView(afterIncoming),
                prepared.plan_.incomingOwnerPolicy,
                prepared.topologyGuard_.rotation
            )) {
            prepared.status_ = Status::Stale;
            return prepared;
        }
    }

    prepared.status_ = Status::Prepared;
    return prepared;
}

FLASHMEM Result commitPreparedSequencerTrackStructureTransaction(
    PreparedSequencerTrackStructureTransaction prepared
) {
    const auto chronology = prepared.chronology_;
    if (!prepared.ready()) {
        const Status terminalStatus = prepared.status_ == Status::Prepared
            ? Status::Invalid
            : prepared.status_;
        return result(terminalStatus, chronology);
    }

    if (prepared.sequencer_ != nullptr &&
        prepared.sequencer_->stepContentDraft.active.get()) {
        return result(Status::Stale, chronology);
    }

    const auto* operations = prepared.execution_.operations_;
    if (!validOperations(operations, prepared.plan_.action) ||
        prepared.tracks_ == nullptr || prepared.sequencer_ == nullptr ||
        prepared.activationQueue_ == nullptr || !prepared.change_) {
        return result(Status::Invalid, chronology);
    }

    Plan livePlan{};
    const PlanOutcome liveOutcome = operations->buildPlan(
        prepared.execution_.context_,
        prepared.plan_.action,
        livePlan
    );
    if (liveOutcome != PlanOutcome::Ready ||
        !samePlan(prepared.plan_, livePlan) ||
        !validActionPlan(
            livePlan,
            prepared.plan_.action,
            *prepared.tracks_,
            *prepared.sequencer_,
            prepared.sharedTracks_,
            prepared.macroPages_
        ) ||
        !operations->revalidate(
            prepared.execution_.context_,
            prepared.plan_,
            *prepared.change_
        ) ||
        !core::state::sequencer::liveHistoryStructureSnapshotMatches(
            *prepared.tracks_,
            *prepared.sequencer_,
            prepared.change_->before
        ) ||
        !prepared.sharedTracks_.
            preparedTrackStructureSettlementCheckpointMatches(
                prepared.settlementCheckpoint_
            )) {
        return result(Status::Stale, chronology);
    }

    const bool hasRotation = prepared.plan_.beforeActiveTrack !=
        prepared.plan_.afterActiveTrack;
    if (!hasRotation &&
        !PreparedSequencerTrackStructureTransaction::ownerIdentitiesMatch_(
            *prepared.tracks_,
            *prepared.sequencer_,
            prepared.topologyGuard_.ownerIdentities,
            trackCount(prepared.plan_.capturedTrackMask)
        )) {
        return result(Status::Stale, chronology);
    }

    const auto* macroPayload = prepared.change_->macroStructure.get();
    if (isMacroAction(prepared.plan_.action) &&
        (prepared.macroPages_ == nullptr || macroPayload == nullptr ||
         !core::state::sequencer::liveMacroTrackStructureMatches(
             *prepared.macroPages_,
             *macroPayload,
             false
         ))) {
        return result(Status::Stale, chronology);
    }
    if (!isMacroAction(prepared.plan_.action) && macroPayload != nullptr) {
        return result(Status::Invalid, chronology);
    }

    if (!semanticChange(*prepared.change_)) {
        if (prepared.sequencer_->stepContentDraft.active.get() ||
            !prepared.activationQueue_->mutationGuardMatches(
                prepared.activationGuard_
            )) {
            return result(Status::Stale, chronology);
        }
        operations->settleNoChange(
            prepared.execution_.context_,
            prepared.plan_
        );
        operations->settleSuccessful(
            prepared.execution_.context_,
            prepared.plan_
        );
        return result(Status::NoChange, chronology);
    }

    if (!prepared.sharedTracks_.canPublishPreparedSequencerState()) {
        return result(Status::PublicationUnavailable, chronology);
    }
    if (hasRotation &&
        !core::state::sequencer::preparedActiveTrackOwnerRotationMatches(
            *prepared.tracks_,
            *prepared.sequencer_,
            prepared.topologyGuard_.rotation
        )) {
        return result(Status::Stale, chronology);
    }
    if (!prepared.history_.canCommitAdmittedStructure(*prepared.change_)) {
        return result(Status::HistoryUnavailable, chronology);
    }
    if (prepared.sequencer_->stepContentDraft.active.get() ||
        !prepared.activationQueue_->mutationGuardMatches(
            prepared.activationGuard_
        )) {
        return result(Status::Stale, chronology);
    }

    // No recoverable branch, allocation or reconstructive rollback is allowed
    // beyond this point.
    if (hasRotation) {
        core::state::sequencer::rotateActiveTrackOwnersNoPublish(
            *prepared.tracks_,
            *prepared.sequencer_,
            prepared.topologyGuard_.rotation
        );
    }
    prepared.sequencer_->focusedStep.set(prepared.plan_.afterFocusedStep);
    prepared.sequencer_->page.set(prepared.plan_.afterPage);
    if (macroPayload != nullptr) {
        core::state::sequencer::
            commitAdmittedMacroTrackStructureHistoryAfter(
            *prepared.macroPages_,
            *macroPayload
        );
    }
    prepared.sharedTracks_.publishPreparedSequencerState(
        prepared.plan_.afterEnabledMask,
        prepared.plan_.afterActiveTrack
    );
    operations->reconcileCommitted(
        prepared.execution_.context_,
        prepared.plan_,
        *prepared.change_
    );
    commitAdmittedStructure(prepared.history_, std::move(prepared.change_));
    operations->settleSuccessful(
        prepared.execution_.context_,
        prepared.plan_
    );
    return result(Status::Committed, chronology);
}

FLASHMEM Result executeSequencerTrackStructureTransaction(
    SequencerPreparedTrackStructureStateRefs state,
    Action action,
    SequencerPreparedTrackStructureExecution execution
) {
    return commitPreparedSequencerTrackStructureTransaction(
        prepareSequencerTrackStructureTransaction(state, action, execution)
    );
}

}  // namespace core::handler
