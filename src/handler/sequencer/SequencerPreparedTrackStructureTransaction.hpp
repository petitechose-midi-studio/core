#pragma once

#include <array>
#include <cstdint>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

struct SequencerPreparedTrackStructureStateRefs;
class PreparedSequencerTrackStructureTransaction;

/** Frozen direct Track Structure surface. Prepared transfer remains separate. */
enum class SequencerPreparedTrackStructureAction : uint8_t {
    SequencerCreate = 0,
    SequencerRemoveCurrent = 1,
    SequencerRemoveSelection = 2,
    MacroDelete = 3,
    MacroReset = 4,
    MacroPaste = 5,
    MacroCreate = 6,
};

enum class SequencerPreparedTrackStructureStatus : uint8_t {
    Invalid = 0,
    Prepared,
    Committed,
    NoChange,
    DraftBlocked,
    Stale,
    AllocationUnavailable,
    HistoryUnavailable,
    PublicationUnavailable,
};

enum class SequencerPreparedTrackStructurePlanOutcome : uint8_t {
    Invalid = 0,
    Stale,
    Ready,
};

enum class SequencerPreparedTrackStructureMacroOutcome : uint8_t {
    Invalid = 0,
    Stale,
    Ready,
};

struct SequencerPreparedTrackStructureResult {
    SequencerPreparedTrackStructureStatus status =
        SequencerPreparedTrackStructureStatus::Invalid;
    core::state::sequencer::SequencerTrackStructureChronologyResult chronology{};

    [[nodiscard]] bool committed() const {
        return status == SequencerPreparedTrackStructureStatus::Committed;
    }
    [[nodiscard]] bool settled() const {
        return committed() ||
            status == SequencerPreparedTrackStructureStatus::NoChange;
    }
};

/**
 * Scalar, action-specific topology plan rebuilt on both sides of chronology.
 * No state object, snapshot, Graph, CC bank or clipboard payload is copied.
 */
struct SequencerPreparedTrackStructurePlan {
    SequencerPreparedTrackStructureAction action =
        SequencerPreparedTrackStructureAction::SequencerCreate;
    uint16_t beforeEnabledMask = 0U;
    uint16_t afterEnabledMask = 0U;
    uint16_t affectedTrackMask = 0U;
    uint16_t capturedTrackMask = 0U;
    uint16_t canonicalResetTrackMask = 0U;
    uint16_t macroCapturedTrackMask = 0U;
    uint8_t beforeActiveTrack =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t afterActiveTrack =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t beforeFocusedStep = 0U;
    uint8_t afterFocusedStep = 0U;
    uint8_t beforePage = 0U;
    uint8_t afterPage = 0U;
    uint8_t targetTrack =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t macroAffectedTrack = core::state::sequencer::
        SequencerHistoryMacroTrackStructurePayload::INVALID_AFFECTED_TRACK;
    core::state::sequencer::SequencerActiveTrackIncomingOwnerPolicy
        incomingOwnerPolicy = core::state::sequencer::
            SequencerActiveTrackIncomingOwnerPolicy::Preserve;
};

static_assert(
    sizeof(SequencerPreparedTrackStructurePlan) <= 40U,
    "Track Structure plan must remain scalar and bounded"
);

using SequencerPreparedTrackStructureBuildPlanFn =
    SequencerPreparedTrackStructurePlanOutcome (*)(
        const void* context,
        SequencerPreparedTrackStructureAction action,
        SequencerPreparedTrackStructurePlan& out) noexcept;
using SequencerPreparedTrackStructurePrepareMacroAfterFn =
    SequencerPreparedTrackStructureMacroOutcome (*)(
        const void* context,
        const SequencerPreparedTrackStructurePlan& plan,
        std::array<core::state::macro::MacroTrackData,
                   core::state::macro::TRACK_COUNT>& afterTracks,
        core::state::modulation::ProjectControlDomainState& afterControl
    ) noexcept;
using SequencerPreparedTrackStructureRevalidateFn = bool (*)(
    const void* context,
    const SequencerPreparedTrackStructurePlan& plan,
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change
) noexcept;
using SequencerPreparedTrackStructureReconcileCommittedFn = void (*)(
    void* context,
    const SequencerPreparedTrackStructurePlan& plan,
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change
) noexcept;
using SequencerPreparedTrackStructureSettleNoChangeFn = void (*)(
    void* context,
    const SequencerPreparedTrackStructurePlan& plan
) noexcept;
using SequencerPreparedTrackStructureSettleSuccessfulFn = void (*)(
    void* context,
    const SequencerPreparedTrackStructurePlan& plan
) noexcept;

/** Static callback table; the execution facade remains exactly two pointers. */
class SequencerPreparedTrackStructureExecution {
public:
    struct Operations {
        SequencerPreparedTrackStructureBuildPlanFn buildPlan = nullptr;
        SequencerPreparedTrackStructurePrepareMacroAfterFn prepareMacroAfter =
            nullptr;
        SequencerPreparedTrackStructureRevalidateFn revalidate = nullptr;
        SequencerPreparedTrackStructureReconcileCommittedFn
            reconcileCommitted = nullptr;
        SequencerPreparedTrackStructureSettleNoChangeFn settleNoChange =
            nullptr;
        SequencerPreparedTrackStructureSettleSuccessfulFn settleSuccessful =
            nullptr;
    };

    template <const Operations& operations>
    static SequencerPreparedTrackStructureExecution fromStaticOperations(
        void* context
    ) {
        return SequencerPreparedTrackStructureExecution(context, &operations);
    }

private:
    friend class PreparedSequencerTrackStructureTransaction;
    friend PreparedSequencerTrackStructureTransaction
    prepareSequencerTrackStructureTransaction(
        SequencerPreparedTrackStructureStateRefs state,
        SequencerPreparedTrackStructureAction action,
        SequencerPreparedTrackStructureExecution execution);
    friend SequencerPreparedTrackStructureResult
    commitPreparedSequencerTrackStructureTransaction(
        PreparedSequencerTrackStructureTransaction prepared);

    SequencerPreparedTrackStructureExecution(
        void* context,
        const Operations* operations)
        : context_(context), operations_(operations) {}

    void* context_ = nullptr;
    const Operations* operations_ = nullptr;
};

static_assert(
    sizeof(SequencerPreparedTrackStructureExecution) == sizeof(void*) * 2U,
    "Track Structure execution facade must remain two pointers"
);

struct SequencerPreparedTrackStructureStateRefs {
    core::state::sequencer::SequencerTrackBankState& tracks;
    core::state::sequencer::SequencerState& sequencer;
    core::state::macro::MacroPagesState* macroPages;
    core::state::sequencer::SequencerTrackActivationQueue& activationQueue;
    SharedTrackDomainServices sharedTracks;
    SequencerHistoryDomainServices history;
};

/**
 * Move-only owner prepared before the first live write. Its only bulk owner is
 * the final PSRAM Structure Change; every other field is a scalar checkpoint.
 */
class PreparedSequencerTrackStructureTransaction {
public:
    PreparedSequencerTrackStructureTransaction() = delete;
    ~PreparedSequencerTrackStructureTransaction();
    PreparedSequencerTrackStructureTransaction(
        const PreparedSequencerTrackStructureTransaction&) = delete;
    PreparedSequencerTrackStructureTransaction& operator=(
        const PreparedSequencerTrackStructureTransaction&) = delete;
    PreparedSequencerTrackStructureTransaction(
        PreparedSequencerTrackStructureTransaction&&) noexcept;
    PreparedSequencerTrackStructureTransaction& operator=(
        PreparedSequencerTrackStructureTransaction&&) noexcept;

    [[nodiscard]] bool ready() const {
        return status_ == SequencerPreparedTrackStructureStatus::Prepared &&
            change_ != nullptr;
    }
    [[nodiscard]] SequencerPreparedTrackStructureStatus status() const {
        return status_;
    }
    [[nodiscard]] const core::state::sequencer::
        SequencerTrackStructureChronologyResult& chronology() const {
        return chronology_;
    }
    [[nodiscard]] const SequencerPreparedTrackStructurePlan& plan() const {
        return plan_;
    }

private:
    friend PreparedSequencerTrackStructureTransaction
    prepareSequencerTrackStructureTransaction(
        SequencerPreparedTrackStructureStateRefs state,
        SequencerPreparedTrackStructureAction action,
        SequencerPreparedTrackStructureExecution execution);
    friend SequencerPreparedTrackStructureResult
    commitPreparedSequencerTrackStructureTransaction(
        PreparedSequencerTrackStructureTransaction prepared);

    struct OwnerIdentity {
        const oc::note::sequencer::StepSequencerGraph* graph = nullptr;
        const core::state::sequencer::SequencerCcLaneBank* ccLanes = nullptr;
        uint8_t track =
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    };

    /** Rotation and same-active owner proof are mutually exclusive. */
    union TopologyGuard {
        core::state::sequencer::SequencerPreparedActiveTrackRotation rotation;
        std::array<OwnerIdentity, 2U> ownerIdentities;

        constexpr TopologyGuard() : rotation{} {}
    };

    static bool captureOwnerIdentities_(
        const core::state::sequencer::SequencerTrackBankState& tracks,
        const core::state::sequencer::SequencerState& sequencer,
        uint16_t capturedMask,
        std::array<OwnerIdentity, 2U>& out,
        uint8_t& outCount
    ) noexcept;
    static bool ownerIdentitiesMatch_(
        const core::state::sequencer::SequencerTrackBankState& tracks,
        const core::state::sequencer::SequencerState& sequencer,
        const std::array<OwnerIdentity, 2U>& expected,
        uint8_t count
    ) noexcept;

    SequencerPreparedTrackStructureStatus status_ =
        SequencerPreparedTrackStructureStatus::Invalid;
    core::state::sequencer::SequencerTrackStructureChronologyResult chronology_{};
    SequencerPreparedTrackStructurePlan plan_{};
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change_{};
    TopologyGuard topologyGuard_{};
    core::state::sequencer::SequencerTrackActivationMutationGuard
        activationGuard_{};
    PreparedTrackStructureSettlementCheckpoint settlementCheckpoint_{};
    core::state::sequencer::SequencerTrackBankState* tracks_ = nullptr;
    core::state::sequencer::SequencerState* sequencer_ = nullptr;
    core::state::macro::MacroPagesState* macroPages_ = nullptr;
    core::state::sequencer::SequencerTrackActivationQueue* activationQueue_ =
        nullptr;
    SharedTrackDomainServices sharedTracks_;
    SequencerHistoryDomainServices history_;
    SequencerPreparedTrackStructureExecution execution_;
    explicit PreparedSequencerTrackStructureTransaction(
        SequencerPreparedTrackStructureStateRefs state,
        SequencerPreparedTrackStructureExecution executionIn)
        : tracks_(&state.tracks),
          sequencer_(&state.sequencer),
          macroPages_(state.macroPages),
          activationQueue_(&state.activationQueue),
          sharedTracks_(state.sharedTracks),
          history_(state.history),
          execution_(executionIn) {}
};

static_assert(
    sizeof(PreparedSequencerTrackStructureTransaction) <= 512U,
    "prepared Track Structure owner exceeds its frame contract"
);

[[nodiscard]] PreparedSequencerTrackStructureTransaction
prepareSequencerTrackStructureTransaction(
    SequencerPreparedTrackStructureStateRefs state,
    SequencerPreparedTrackStructureAction action,
    SequencerPreparedTrackStructureExecution execution
);

[[nodiscard]] SequencerPreparedTrackStructureResult
commitPreparedSequencerTrackStructureTransaction(
    PreparedSequencerTrackStructureTransaction prepared
);

[[nodiscard]] SequencerPreparedTrackStructureResult
executeSequencerTrackStructureTransaction(
    SequencerPreparedTrackStructureStateRefs state,
    SequencerPreparedTrackStructureAction action,
    SequencerPreparedTrackStructureExecution execution
);

}  // namespace core::handler
