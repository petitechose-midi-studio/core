#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "app/ExtmemAllocator.hpp"
#include "handler/macro/MacroStructureAutomationOps.hpp"
#include "handler/sequencer/SequencerPreparedTrackStructureTransaction.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "support/ProjectControlTestUtils.hpp"
#include "support/SequencerHistoryTransactionAssertions.hpp"

#if !defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
#error "This test requires native EXTMEM failure injection"
#endif

namespace allocation_trace {

constexpr std::size_t MAX_REQUESTS = 24U;
bool enabled = false;
std::array<std::size_t, MAX_REQUESTS> requests{};
std::array<void*, MAX_REQUESTS> livePointers{};
std::size_t count = 0U;
std::size_t liveCount = 0U;
bool overflow = false;

void record(void* pointer, std::size_t bytes) {
    if (!enabled) return;
    if (count >= requests.size()) {
        overflow = true;
        return;
    }
    requests[count] = bytes;
    livePointers[count] = pointer;
    ++count;
    ++liveCount;
}

void release(void* pointer) {
    if (!enabled || pointer == nullptr) return;
    for (std::size_t index = 0U; index < count; ++index) {
        if (livePointers[index] != pointer) continue;
        livePointers[index] = nullptr;
        assert(liveCount > 0U);
        --liveCount;
        return;
    }
}

class Scope {
public:
    Scope() {
        requests.fill(0U);
        livePointers.fill(nullptr);
        count = 0U;
        liveCount = 0U;
        overflow = false;
        enabled = true;
    }
    ~Scope() { enabled = false; }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}  // namespace allocation_trace

void* operator new(std::size_t bytes) {
    if (void* memory = std::malloc(bytes)) {
        allocation_trace::record(memory, bytes);
        return memory;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept {
    allocation_trace::release(memory);
    std::free(memory);
}
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete[](memory);
}

namespace {

namespace seq = core::state::sequencer;
namespace mac = core::state::macro;
namespace mod = core::state::modulation;
namespace tx_assert = test_support::sequencer_transaction;

using Action = core::handler::SequencerPreparedTrackStructureAction;
using Status = core::handler::SequencerPreparedTrackStructureStatus;
using PlanOutcome =
    core::handler::SequencerPreparedTrackStructurePlanOutcome;
using MacroOutcome =
    core::handler::SequencerPreparedTrackStructureMacroOutcome;
using Result = core::handler::SequencerPreparedTrackStructureResult;
using Plan = core::handler::SequencerPreparedTrackStructurePlan;
using Execution =
    core::handler::SequencerPreparedTrackStructureExecution;
using Prepared =
    core::handler::PreparedSequencerTrackStructureTransaction;
using StateRefs =
    core::handler::SequencerPreparedTrackStructureStateRefs;
using Shared = core::handler::SharedTrackDomainServices;
using Checkpoint =
    core::handler::PreparedTrackStructureSettlementCheckpoint;
using History = core::handler::SequencerHistoryDomainServices;
using Change = seq::SequencerHistoryTrackStructureChange;
using ChangePtr = seq::SequencerHistoryTrackStructureChangePtr;
using Chronology = seq::SequencerTrackStructureChronologyResult;
using ChronologyStatus = seq::SequencerTrackStructureChronologyStatus;
using PatternOutcome = seq::SequencerPatternHistoryCommitOutcome;
using TrackBank = seq::SequencerTrackBankState;
using Graph = oc::note::sequencer::StepSequencerGraph;
using CcBank = seq::SequencerCcLaneBank;
using MacroPayload = seq::SequencerHistoryMacroTrackStructurePayload;
using ControlDomain = mod::ProjectControlDomainState;

constexpr uint8_t kInvalidTrack = TrackBank::TRACK_COUNT;
constexpr uint8_t kInvalidMacroTrack =
    MacroPayload::INVALID_AFFECTED_TRACK;

static_assert(static_cast<uint8_t>(Action::SequencerCreate) == 0U);
static_assert(static_cast<uint8_t>(Action::SequencerRemoveCurrent) == 1U);
static_assert(static_cast<uint8_t>(Action::SequencerRemoveSelection) == 2U);
static_assert(static_cast<uint8_t>(Action::MacroDelete) == 3U);
static_assert(static_cast<uint8_t>(Action::MacroReset) == 4U);
static_assert(static_cast<uint8_t>(Action::MacroPaste) == 5U);
static_assert(static_cast<uint8_t>(Action::MacroCreate) == 6U);
static_assert(sizeof(Execution) == sizeof(void*) * 2U);
static_assert(sizeof(Plan) <= 40U);
static_assert(sizeof(Prepared) <= 512U);
static_assert(!std::is_copy_constructible_v<Prepared>);
static_assert(!std::is_copy_assignable_v<Prepared>);
static_assert(std::is_move_constructible_v<Prepared>);
static_assert(std::is_move_assignable_v<Prepared>);

constexpr std::size_t kArmAllocationHeaderBytes = 16U;
constexpr std::size_t kArmStructureChangeBytes = 27192U;
constexpr std::size_t kArmGraphBytes = 14792U;
constexpr std::size_t kArmCcBankBytes = 840U;
constexpr std::size_t kArmMacroPayloadBytes = 30860U;
constexpr std::size_t kArmControlDomainBytes = 159516U;
constexpr std::size_t kArmPairBytes =
    (kArmGraphBytes + kArmAllocationHeaderBytes) +
    (kArmCcBankBytes + kArmAllocationHeaderBytes);
constexpr std::size_t kArmMacroAddonBytes =
    (kArmMacroPayloadBytes + kArmAllocationHeaderBytes) +
    2U * (kArmControlDomainBytes + kArmAllocationHeaderBytes);
constexpr std::size_t kArmCreatePeak =
    (kArmStructureChangeBytes + kArmAllocationHeaderBytes) +
    3U * kArmPairBytes;
constexpr std::size_t kArmRemovePeak =
    (kArmStructureChangeBytes + kArmAllocationHeaderBytes) +
    4U * kArmPairBytes;
constexpr std::size_t kArmMacroT1Peak =
    (kArmStructureChangeBytes + kArmAllocationHeaderBytes) +
    2U * kArmPairBytes + kArmMacroAddonBytes;
constexpr std::size_t kArmMacroT2Peak =
    (kArmStructureChangeBytes + kArmAllocationHeaderBytes) +
    4U * kArmPairBytes + kArmMacroAddonBytes;

static_assert(kArmPairBytes == 15664U);
static_assert(kArmCreatePeak == 74200U);
static_assert(kArmRemovePeak == 89864U);
static_assert(kArmMacroT1Peak == 408476U);
static_assert(kArmMacroT2Peak == 439804U);

// Distinct from the logical LOCK-P h=16 accounting above: Teensy smalloc
// physically rounds payloads to 12-byte quanta and charges two 12-byte
// headers per span.
constexpr std::size_t kSmallocHeaderBytes = 12U;
constexpr std::size_t kSmallocHeadersPerSpan = 2U;
constexpr std::size_t roundUpSmalloc(std::size_t bytes) {
    return ((bytes + kSmallocHeaderBytes - 1U) / kSmallocHeaderBytes) *
        kSmallocHeaderBytes;
}
constexpr std::size_t smallocSpan(std::size_t payloadBytes) {
    return roundUpSmalloc(payloadBytes) +
        kSmallocHeadersPerSpan * kSmallocHeaderBytes;
}
constexpr std::size_t kSmallocPairBytes =
    smallocSpan(kArmGraphBytes) + smallocSpan(kArmCcBankBytes);
constexpr std::size_t kSmallocCreatePeak =
    smallocSpan(kArmStructureChangeBytes) + 3U * kSmallocPairBytes;
constexpr std::size_t kSmallocRemovePeak =
    smallocSpan(kArmStructureChangeBytes) + 4U * kSmallocPairBytes;
constexpr std::size_t kSmallocMacroAddon =
    smallocSpan(kArmMacroPayloadBytes) +
    2U * smallocSpan(kArmControlDomainBytes);
constexpr std::size_t kSmallocMacroT1Peak =
    smallocSpan(kArmStructureChangeBytes) +
    2U * kSmallocPairBytes + kSmallocMacroAddon;
constexpr std::size_t kSmallocMacroT2Peak =
    smallocSpan(kArmStructureChangeBytes) +
    4U * kSmallocPairBytes + kSmallocMacroAddon;

static_assert(smallocSpan(kArmControlDomainBytes) == 159540U);
static_assert(kSmallocCreatePeak == 74268U);
static_assert(kSmallocRemovePeak == 89952U);
static_assert(kSmallocMacroT1Peak == 408552U);
static_assert(kSmallocMacroT2Peak == 439920U);

enum class Call : uint8_t {
    Build = 0,
    Boundary,
    Checkpoint,
    MacroAfter,
    Admit,
    Revalidate,
    Publish,
    Reconcile,
    Commit,
    NoChange,
    Successful,
};

enum class MacroMode : uint8_t {
    Unchanged = 0,
    ChangeTarget,
    ChangeOther,
    ClearTargetControl,
    Invalid,
    Stale,
};

struct Script {
    TrackBank* tracks = nullptr;
    mac::MacroPagesState* macroPages = nullptr;
    oc::state::Signal<uint8_t, 8>* sharedActive = nullptr;
    oc::state::Signal<uint16_t, 16>* sharedMask = nullptr;

    std::array<Plan, 3U> plans{};
    std::array<PlanOutcome, 3U> planOutcomes{
        PlanOutcome::Ready,
        PlanOutcome::Ready,
        PlanOutcome::Ready,
    };
    Chronology chronology{
        ChronologyStatus::Opened,
        PatternOutcome::NoPending,
    };
    Checkpoint checkpoint{};
    MacroMode macroMode = MacroMode::Unchanged;
    bool checkpointAvailable = true;
    bool revalidateOutcome = true;
    std::array<bool, 2U> admitOutcomes{true, true};

    mutable std::array<Call, 32U> calls{};
    mutable std::size_t callCount = 0U;
    mutable std::size_t buildCount = 0U;
    mutable std::size_t boundaryCount = 0U;
    mutable std::size_t checkpointCount = 0U;
    mutable std::size_t macroAfterCount = 0U;
    mutable std::size_t admitCount = 0U;
    mutable std::size_t revalidateCount = 0U;
    mutable std::size_t publishCount = 0U;
    mutable std::size_t reconcileCount = 0U;
    mutable std::size_t commitCount = 0U;
    mutable std::size_t noChangeCount = 0U;
    mutable std::size_t successfulCount = 0U;
    mutable bool admittedAfterControl = false;
    ChangePtr committed{};

    void note(Call call) const {
        assert(callCount < calls.size());
        calls[callCount++] = call;
    }

    static PlanOutcome build(
        const void* context,
        Action,
        Plan& out
    ) noexcept {
        const auto& self = *static_cast<const Script*>(context);
        self.note(Call::Build);
        assert(self.buildCount < self.planOutcomes.size());
        const std::size_t index = self.buildCount;
        ++self.buildCount;
        const PlanOutcome outcome = self.planOutcomes[index];
        if (outcome == PlanOutcome::Ready) out = self.plans[index];
        return outcome;
    }

    static MacroOutcome prepareMacroAfter(
        const void* context,
        const Plan& plan,
        std::array<mac::MacroTrackData, mac::TRACK_COUNT>& afterTracks,
        ControlDomain& afterControl
    ) noexcept {
        const auto& self = *static_cast<const Script*>(context);
        self.note(Call::MacroAfter);
        ++self.macroAfterCount;
        switch (self.macroMode) {
            case MacroMode::Unchanged:
                return MacroOutcome::Ready;
            case MacroMode::ChangeTarget: {
                auto& value = afterTracks[plan.macroAffectedTrack]
                    .pages[0U].cc[0U];
                value = static_cast<uint8_t>((value + 1U) & 0x7FU);
                return MacroOutcome::Ready;
            }
            case MacroMode::ChangeOther: {
                const uint8_t other = static_cast<uint8_t>(
                    (plan.macroAffectedTrack + 1U) % mac::TRACK_COUNT
                );
                auto& value = afterTracks[other].pages[0U].cc[0U];
                value = static_cast<uint8_t>((value + 1U) & 0x7FU);
                return MacroOutcome::Ready;
            }
            case MacroMode::ClearTargetControl:
                if (!core::handler::macro_structure_automation_ops::
                        clearTracksInDomain(
                            afterControl,
                            static_cast<uint16_t>(
                                1U << plan.macroAffectedTrack
                            )
                        )) {
                    return MacroOutcome::Invalid;
                }
                return MacroOutcome::Ready;
            case MacroMode::Invalid:
                return MacroOutcome::Invalid;
            case MacroMode::Stale:
                return MacroOutcome::Stale;
        }
        return MacroOutcome::Invalid;
    }

    static bool revalidate(
        const void* context,
        const Plan&,
        const Change&
    ) noexcept {
        const auto& self = *static_cast<const Script*>(context);
        self.note(Call::Revalidate);
        ++self.revalidateCount;
        return self.revalidateOutcome;
    }

    static void reconcileCommitted(
        void* context,
        const Plan&,
        const Change&
    ) noexcept {
        auto& self = *static_cast<Script*>(context);
        self.note(Call::Reconcile);
        ++self.reconcileCount;
    }

    static void settleNoChange(void* context, const Plan&) noexcept {
        auto& self = *static_cast<Script*>(context);
        self.note(Call::NoChange);
        ++self.noChangeCount;
    }

    static void settleSuccessful(void* context, const Plan&) noexcept {
        auto& self = *static_cast<Script*>(context);
        self.note(Call::Successful);
        ++self.successfulCount;
    }

    static bool admit(const void* context, const Change& change) {
        const auto& self = *static_cast<const Script*>(context);
        self.note(Call::Admit);
        assert(self.admitCount < self.admitOutcomes.size());
        const std::size_t index = self.admitCount;
        ++self.admitCount;
        self.admittedAfterControl =
            change.macroStructure != nullptr &&
            change.macroStructure->afterControl != nullptr;
        return self.admitOutcomes[index];
    }

    static void commit(void* context, ChangePtr change) noexcept {
        auto& self = *static_cast<Script*>(context);
        self.note(Call::Commit);
        ++self.commitCount;
        assert(change);
        self.committed = std::move(change);
    }

    static Chronology boundary(void* context) {
        auto& self = *static_cast<Script*>(context);
        self.note(Call::Boundary);
        ++self.boundaryCount;
        return self.chronology;
    }

    static void publish(
        void* context,
        uint16_t enabledMask,
        uint8_t activeTrack
    ) noexcept {
        auto& self = *static_cast<Script*>(context);
        self.note(Call::Publish);
        ++self.publishCount;
        assert(self.tracks != nullptr);
        assert(self.sharedMask != nullptr);
        assert(self.sharedActive != nullptr);
        self.tracks->syncSharedTrackState(enabledMask, activeTrack);
        if (self.macroPages != nullptr) {
            self.macroPages->syncSharedTrackState(enabledMask, activeTrack);
        }
        if (self.sharedMask->get() != enabledMask) {
            self.sharedMask->set(enabledMask);
        }
        if (self.sharedActive->get() != activeTrack) {
            self.sharedActive->set(activeTrack);
        }
    }

    static bool captureCheckpoint(
        const void* context,
        Checkpoint& out
    ) noexcept {
        const auto& self = *static_cast<const Script*>(context);
        self.note(Call::Checkpoint);
        ++self.checkpointCount;
        out = self.checkpoint;
        return self.checkpointAvailable;
    }
};

constexpr Execution::Operations kExecutionOperations{
    .buildPlan = &Script::build,
    .prepareMacroAfter = &Script::prepareMacroAfter,
    .revalidate = &Script::revalidate,
    .reconcileCommitted = &Script::reconcileCommitted,
    .settleNoChange = &Script::settleNoChange,
    .settleSuccessful = &Script::settleSuccessful,
};

constexpr Execution::Operations kInvalidExecutionOperations{};

constexpr History::Operations kHistoryOperations{
    .canRecordStructure = &Script::admit,
    .commitAdmittedStructure = &Script::commit,
    .openTrackStructureChronologyBoundary = &Script::boundary,
};

constexpr History::Operations kHistoryWithoutCommitOperations{
    .canRecordStructure = &Script::admit,
    .openTrackStructureChronologyBoundary = &Script::boundary,
};

Plan basePlan(
    Action action,
    uint16_t beforeMask,
    uint8_t beforeActive,
    uint16_t afterMask,
    uint8_t afterActive,
    uint16_t affected,
    uint16_t captured
) {
    Plan plan{};
    plan.action = action;
    plan.beforeEnabledMask = beforeMask;
    plan.afterEnabledMask = afterMask;
    plan.affectedTrackMask = affected;
    plan.capturedTrackMask = captured;
    plan.beforeActiveTrack = beforeActive;
    plan.afterActiveTrack = afterActive;
    plan.beforeFocusedStep = 0U;
    plan.afterFocusedStep = 0U;
    plan.beforePage = 0U;
    plan.afterPage = 0U;
    plan.targetTrack = kInvalidTrack;
    plan.macroAffectedTrack = kInvalidMacroTrack;
    plan.incomingOwnerPolicy =
        seq::SequencerActiveTrackIncomingOwnerPolicy::Preserve;
    return plan;
}

Plan createPlan() {
    Plan plan = basePlan(Action::SequencerCreate, 0x0001U, 0U,
                         0x0003U, 1U, 0x0002U, 0x0003U);
    plan.targetTrack = 1U;
    plan.canonicalResetTrackMask = 0x0002U;
    plan.incomingOwnerPolicy =
        seq::SequencerActiveTrackIncomingOwnerPolicy::Reset;
    return plan;
}

Plan removeCurrentPlan() {
    Plan plan = basePlan(Action::SequencerRemoveCurrent, 0x0003U, 0U,
                         0x0002U, 1U, 0x0001U, 0x0003U);
    plan.targetTrack = 0U;
    return plan;
}

Plan removeSelectionPlan() {
    return basePlan(Action::SequencerRemoveSelection, 0x000FU, 1U,
                    0x0005U, 2U, 0x000AU, 0x0006U);
}

Plan macroDeletePlan() {
    Plan plan = basePlan(Action::MacroDelete, 0x0003U, 0U,
                         0x0002U, 1U, 0x0001U, 0x0003U);
    plan.targetTrack = 0U;
    plan.macroCapturedTrackMask = 0x0003U;
    plan.macroAffectedTrack = 0U;
    return plan;
}

Plan macroResetT1Plan() {
    Plan plan = basePlan(Action::MacroReset, 0x0001U, 0U,
                         0x0001U, 0U, 0x0001U, 0x0001U);
    plan.targetTrack = 0U;
    plan.macroCapturedTrackMask = 0x0001U;
    plan.macroAffectedTrack = 0U;
    return plan;
}

Plan macroResetT2Plan() {
    Plan plan = basePlan(Action::MacroReset, 0x0003U, 0U,
                         0x0003U, 0U, 0x0002U, 0x0003U);
    plan.targetTrack = 1U;
    plan.macroCapturedTrackMask = 0x0003U;
    plan.macroAffectedTrack = 1U;
    return plan;
}

Plan macroPasteT1Plan() {
    Plan plan = basePlan(Action::MacroPaste, 0x0001U, 0U,
                         0x0001U, 0U, 0x0001U, 0x0001U);
    plan.targetTrack = 0U;
    plan.macroCapturedTrackMask = 0x0001U;
    plan.macroAffectedTrack = 0U;
    return plan;
}

Plan macroPasteT2Plan() {
    Plan plan = basePlan(Action::MacroPaste, 0x0001U, 0U,
                         0x0003U, 1U, 0x0002U, 0x0003U);
    plan.targetTrack = 1U;
    plan.macroCapturedTrackMask = 0x0003U;
    plan.macroAffectedTrack = 1U;
    return plan;
}

Plan macroCreatePlan() {
    Plan plan = basePlan(Action::MacroCreate, 0x0001U, 0U,
                         0x0003U, 1U, 0x0002U, 0x0003U);
    plan.targetTrack = 1U;
    plan.macroCapturedTrackMask = 0x0003U;
    plan.macroAffectedTrack = 1U;
    return plan;
}

struct Harness {
    TrackBank tracks;
    seq::SequencerState sequencer;
    mac::MacroPagesState macros;
    seq::SequencerTrackActivationQueue activationQueue;
    oc::state::Signal<uint8_t, 8> sharedActive{0U};
    oc::state::Signal<uint16_t, 16> sharedMask{0x0001U};
    Script script;

    Harness() {
        script.tracks = &tracks;
        script.macroPages = &macros;
        script.sharedActive = &sharedActive;
        script.sharedMask = &sharedMask;
        script.checkpoint.projectModulatorNavigationFingerprint = 0x12345678ULL;
        script.checkpoint.manualOverrideRevision = 11U;
        script.checkpoint.manualOverrideRejectedActivationCount = 12U;
        script.checkpoint.controlAuthoredRevision = 13U;
        script.checkpoint.configRevision = 14U;
        script.checkpoint.automationEditRevision = 15U;
        script.checkpoint.runtimeProjectionRevision = 16U;
        script.checkpoint.manualOverrideMask = 0x0003U;
        script.checkpoint.projectNavigationRevision = 17U;
    }

    Execution execution() {
        return Execution::fromStaticOperations<kExecutionOperations>(&script);
    }

    Execution invalidExecution() {
        return Execution::fromStaticOperations<kInvalidExecutionOperations>(
            &script
        );
    }

    History history(bool withCommit = true) {
        return withCommit
            ? History::fromStaticOperations<kHistoryOperations>(&script)
            : History::fromStaticOperations<kHistoryWithoutCommitOperations>(
                  &script
              );
    }

    Shared shared(bool withPublisher = true, bool withCheckpoint = true) {
        return Shared{
            {sharedActive, sharedMask},
            {
                .context = &script,
                .publishPreparedSequencerState = withPublisher
                    ? &Script::publish
                    : nullptr,
                .capturePreparedTrackStructureSettlementCheckpoint =
                    withCheckpoint ? &Script::captureCheckpoint : nullptr,
            },
        };
    }

    StateRefs refs(
        bool withMacroPages = true,
        bool withPublisher = true,
        bool withCheckpoint = true,
        bool withHistoryCommit = true
    ) {
        return {
            tracks,
            sequencer,
            withMacroPages ? &macros : nullptr,
            activationQueue,
            shared(withPublisher, withCheckpoint),
            history(withHistoryCommit),
        };
    }
};

std::unique_ptr<Harness> makeHarness(
    const Plan& plan,
    MacroMode macroMode = MacroMode::Unchanged
) {
    auto harness = std::make_unique<Harness>();
    harness->tracks.syncSharedTrackState(
        plan.beforeEnabledMask,
        plan.beforeActiveTrack
    );
    harness->macros.syncSharedTrackState(
        plan.beforeEnabledMask,
        plan.beforeActiveTrack
    );
    harness->sharedMask.set(plan.beforeEnabledMask);
    harness->sharedActive.set(plan.beforeActiveTrack);
    harness->sequencer.focusedStep.set(plan.beforeFocusedStep);
    harness->sequencer.page.set(plan.beforePage);
    harness->script.plans.fill(plan);
    harness->script.macroMode = macroMode;
    return harness;
}

void installOwners(seq::SequencerPatternState& pattern, uint8_t tag) {
    pattern.graph = core::app::makeExtmemUnique<Graph>();
    pattern.ccLanes = core::app::makeExtmemUnique<CcBank>();
    assert(pattern.graph);
    assert(pattern.ccLanes);
    pattern.graph->enabled = true;
    pattern.note[0U] = static_cast<uint8_t>(48U + tag);
    pattern.velocity[0U] = static_cast<uint8_t>(80U + tag);
    pattern.stepDataRevision.set(100U + tag);
    pattern.graphRevision.set(200U + tag);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = static_cast<uint8_t>(70U + tag);
    assert(seq::createSequencerCcLane(
               *pattern.ccLanes,
               0U,
               draft
           ).changed());
    assert(seq::setSequencerCcLaneEvent(
               *pattern.ccLanes,
               0U,
               0U,
               static_cast<uint8_t>(90U + tag)
           ).changed());
    pattern.ccLaneRevision.set(pattern.ccLanes->revision);
}

void seedRequiredOwners(Harness& harness, const Plan& plan) {
    uint8_t tag = 1U;
    for (uint8_t track = 0U; track < TrackBank::TRACK_COUNT; ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((plan.capturedTrackMask & bit) == 0U) continue;
        auto& pattern = track == plan.beforeActiveTrack
            ? harness.sequencer.pattern
            : harness.tracks.track(track);
        installOwners(pattern, tag++);
    }
    if (plan.beforeActiveTrack != plan.afterActiveTrack) {
        installOwners(
            harness.tracks.track(plan.beforeActiveTrack),
            tag
        );
    }
}

void armActivation(
    seq::SequencerTrackActivationQueue& queue,
    uint16_t trackMask,
    bool transportPlaying = false
) {
    seq::SequencerTrackActivationBatch batch{};
    assert(queue.prepare(
        trackMask,
        0xFFFFU,
        transportPlaying,
        batch,
        seq::SequencerTrackActivationOrigin::TRACK_PASTE
    ));
    assert(queue.armPrepared(batch));
    queue.publishPrepared(batch);
}

void assertCalls(const Script& script, std::initializer_list<Call> expected) {
    assert(script.callCount == expected.size());
    std::size_t index = 0U;
    for (const Call call : expected) {
        assert(script.calls[index++] == call);
    }
}

void assertAllocationPrefix(
    const std::size_t* expected,
    std::size_t expectedSize,
    std::size_t prefixSize
) {
    assert(!allocation_trace::overflow);
    assert(prefixSize <= expectedSize);
    assert(allocation_trace::count == prefixSize);
    for (std::size_t index = 0U; index < prefixSize; ++index) {
        assert(allocation_trace::requests[index] == expected[index]);
    }
}

uint64_t byteHash(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

struct LiveProof {
    std::array<const Graph*, TrackBank::TRACK_COUNT + 1U> graphOwners{};
    std::array<const CcBank*, TrackBank::TRACK_COUNT + 1U> ccOwners{};
    std::array<uint64_t, TrackBank::TRACK_COUNT + 1U> flatHashes{};
    std::array<uint64_t, TrackBank::TRACK_COUNT + 1U> graphHashes{};
    std::array<uint64_t, TrackBank::TRACK_COUNT + 1U> ccHashes{};
    uint64_t macroTracksHash = 0U;
    uint64_t controlHash = 0U;
    uint16_t trackMask = 0U;
    uint16_t sharedMask = 0U;
    uint16_t macroMask = 0U;
    uint16_t activationMask = 0U;
    uint8_t trackActive = 0U;
    uint8_t sharedActive = 0U;
    uint8_t macroActive = 0U;
    uint8_t focusedStep = 0U;
    uint8_t page = 0U;
    uint32_t activationRevision = 0U;
    uint32_t controlAuthoredRevision = 0U;
};

uint64_t flatHash(const seq::SequencerPatternState& pattern) {
    seq::SequencerPatternSnapshot snapshot{};
    seq::captureSnapshot(pattern, snapshot);
    uint64_t hash = byteHash(&snapshot, sizeof(snapshot));
    const uint32_t ccRevision = pattern.ccLaneRevision.get();
    hash ^= byteHash(&ccRevision, sizeof(ccRevision));
    hash *= 1099511628211ULL;
    return hash;
}

void capturePatternProof(
    const seq::SequencerPatternState& pattern,
    std::size_t index,
    LiveProof& proof
) {
    proof.graphOwners[index] = pattern.graph.get();
    proof.ccOwners[index] = pattern.ccLanes.get();
    proof.flatHashes[index] = flatHash(pattern);
    proof.graphHashes[index] = pattern.graph
        ? byteHash(pattern.graph.get(), sizeof(Graph))
        : 0U;
    proof.ccHashes[index] = pattern.ccLanes
        ? byteHash(pattern.ccLanes.get(), sizeof(CcBank))
        : 0U;
}

LiveProof captureLiveProof(const Harness& harness) {
    LiveProof proof{};
    capturePatternProof(harness.sequencer.pattern, 0U, proof);
    for (uint8_t track = 0U; track < TrackBank::TRACK_COUNT; ++track) {
        capturePatternProof(
            harness.tracks.track(track),
            static_cast<std::size_t>(track) + 1U,
            proof
        );
    }
    proof.macroTracksHash = byteHash(
        harness.macros.tracks.data(),
        sizeof(harness.macros.tracks)
    );
    proof.controlHash = byteHash(
        &harness.macros.control.authored,
        sizeof(harness.macros.control.authored)
    );
    proof.trackMask = harness.tracks.currentEnabledMask();
    proof.sharedMask = harness.sharedMask.get();
    proof.macroMask = harness.macros.currentTrackEnabledMask();
    proof.activationMask = harness.activationQueue.pendingTrackMask();
    proof.trackActive = harness.tracks.activeTrackIndex();
    proof.sharedActive = harness.sharedActive.get();
    proof.macroActive = harness.macros.currentActiveTrack();
    proof.focusedStep = harness.sequencer.focusedStep.get();
    proof.page = harness.sequencer.page.get();
    proof.activationRevision =
        harness.activationQueue.telemetryRevision().get();
    proof.controlAuthoredRevision = harness.macros.control.authoredRevision;
    return proof;
}

void assertLiveProof(const Harness& harness, const LiveProof& expected) {
    const auto actual = captureLiveProof(harness);
    assert(actual.graphOwners == expected.graphOwners);
    assert(actual.ccOwners == expected.ccOwners);
    assert(actual.flatHashes == expected.flatHashes);
    assert(actual.graphHashes == expected.graphHashes);
    assert(actual.ccHashes == expected.ccHashes);
    assert(actual.macroTracksHash == expected.macroTracksHash);
    assert(actual.controlHash == expected.controlHash);
    assert(actual.trackMask == expected.trackMask);
    assert(actual.sharedMask == expected.sharedMask);
    assert(actual.macroMask == expected.macroMask);
    assert(actual.activationMask == expected.activationMask);
    assert(actual.trackActive == expected.trackActive);
    assert(actual.sharedActive == expected.sharedActive);
    assert(actual.macroActive == expected.macroActive);
    assert(actual.focusedStep == expected.focusedStep);
    assert(actual.page == expected.page);
    assert(actual.activationRevision == expected.activationRevision);
    assert(actual.controlAuthoredRevision == expected.controlAuthoredRevision);
}

bool macroAction(Action action) {
    return action == Action::MacroDelete ||
           action == Action::MacroReset ||
           action == Action::MacroPaste ||
           action == Action::MacroCreate;
}

void assertPlanEquals(const Plan& actual, const Plan& expected) {
    assert(actual.action == expected.action);
    assert(actual.beforeEnabledMask == expected.beforeEnabledMask);
    assert(actual.afterEnabledMask == expected.afterEnabledMask);
    assert(actual.affectedTrackMask == expected.affectedTrackMask);
    assert(actual.capturedTrackMask == expected.capturedTrackMask);
    assert(actual.canonicalResetTrackMask == expected.canonicalResetTrackMask);
    assert(actual.macroCapturedTrackMask == expected.macroCapturedTrackMask);
    assert(actual.beforeActiveTrack == expected.beforeActiveTrack);
    assert(actual.afterActiveTrack == expected.afterActiveTrack);
    assert(actual.beforeFocusedStep == expected.beforeFocusedStep);
    assert(actual.afterFocusedStep == expected.afterFocusedStep);
    assert(actual.beforePage == expected.beforePage);
    assert(actual.afterPage == expected.afterPage);
    assert(actual.targetTrack == expected.targetTrack);
    assert(actual.macroAffectedTrack == expected.macroAffectedTrack);
    assert(actual.incomingOwnerPolicy == expected.incomingOwnerPolicy);
}

void assertCommittedLifecycle(const Script& script, bool hasMacro) {
    if (hasMacro) {
        assertCalls(script, {
            Call::Build,
            Call::Boundary,
            Call::Checkpoint,
            Call::Build,
            Call::MacroAfter,
            Call::Admit,
            Call::Build,
            Call::Revalidate,
            Call::Checkpoint,
            Call::Admit,
            Call::Publish,
            Call::Reconcile,
            Call::Commit,
            Call::Successful,
        });
    } else {
        assertCalls(script, {
            Call::Build,
            Call::Boundary,
            Call::Checkpoint,
            Call::Build,
            Call::Admit,
            Call::Build,
            Call::Revalidate,
            Call::Checkpoint,
            Call::Admit,
            Call::Publish,
            Call::Reconcile,
            Call::Commit,
            Call::Successful,
        });
    }
    assert(script.buildCount == 3U);
    assert(script.boundaryCount == 1U);
    assert(script.checkpointCount == 2U);
    assert(script.macroAfterCount == (hasMacro ? 1U : 0U));
    assert(script.admitCount == 2U);
    assert(script.revalidateCount == 1U);
    assert(script.publishCount == 1U);
    assert(script.reconcileCount == 1U);
    assert(script.commitCount == 1U);
    assert(script.noChangeCount == 0U);
    assert(script.successfulCount == 1U);
    assert(script.committed);
}

void assertNoTail(const Script& script) {
    assert(script.publishCount == 0U);
    assert(script.reconcileCount == 0U);
    assert(script.commitCount == 0U);
    assert(script.successfulCount == 0U);
    assert(!script.committed);
}

void runSuccessfulAction(const Plan& plan, MacroMode macroMode) {
    auto harness = makeHarness(plan, macroMode);
    seedRequiredOwners(*harness, plan);

    const Result result =
        core::handler::executeSequencerTrackStructureTransaction(
            harness->refs(),
            plan.action,
            harness->execution()
        );

    assert(result.status == Status::Committed);
    assert(result.committed());
    assert(result.settled());
    assert(result.chronology.status == ChronologyStatus::Opened);
    assert(result.chronology.predecessorPattern == PatternOutcome::NoPending);
    assert(harness->tracks.currentEnabledMask() == plan.afterEnabledMask);
    assert(harness->tracks.activeTrackIndex() == plan.afterActiveTrack);
    assert(harness->sharedMask.get() == plan.afterEnabledMask);
    assert(harness->sharedActive.get() == plan.afterActiveTrack);
    assert(harness->macros.currentTrackEnabledMask() == plan.afterEnabledMask);
    assert(harness->macros.currentActiveTrack() == plan.afterActiveTrack);
    assert(harness->sequencer.focusedStep.get() == plan.afterFocusedStep);
    assert(harness->sequencer.page.get() == plan.afterPage);
    assertCommittedLifecycle(harness->script, macroAction(plan.action));
    assert(harness->script.committed->descriptor.kind ==
           seq::SequencerHistoryActionKind::TrackStructure);
    assert(harness->script.committed->descriptor.trackIndex ==
           TrackBank::clampTrackIndex(plan.afterActiveTrack));
    assert(harness->script.committed->descriptor.laneIndex ==
           seq::SequencerHistoryDescriptor::INVALID_INDEX);
    assert(harness->script.committed->descriptor.stepIndex ==
           seq::SequencerHistoryDescriptor::INVALID_INDEX);
    const int32_t beforeTrackCount =
        seq::sequencerHistoryEnabledTrackCount(plan.beforeEnabledMask);
    const int32_t afterTrackCount =
        seq::sequencerHistoryEnabledTrackCount(plan.afterEnabledMask);
    assert(harness->script.committed->descriptor.hasValue ==
           (beforeTrackCount != afterTrackCount));
    assert(harness->script.committed->descriptor.beforeValue ==
           beforeTrackCount);
    assert(harness->script.committed->descriptor.afterValue ==
           afterTrackCount);
    assert(seq::liveHistoryStructureSnapshotMatches(
        harness->tracks,
        harness->sequencer,
        harness->script.committed->after
    ));
    if (macroAction(plan.action)) {
        assert(harness->script.committed->macroStructure);
        assert(seq::liveMacroTrackStructureMatches(
            harness->macros,
            *harness->script.committed->macroStructure,
            true
        ));
    } else {
        assert(!harness->script.committed->macroStructure);
    }
}

void test_abi_plans_and_all_seven_actions_commit() {
    const std::array<Plan, 7U> plans{
        createPlan(),
        removeCurrentPlan(),
        removeSelectionPlan(),
        macroDeletePlan(),
        macroResetT2Plan(),
        macroPasteT2Plan(),
        macroCreatePlan(),
    };
    const std::array<MacroMode, 7U> macroModes{
        MacroMode::Unchanged,
        MacroMode::Unchanged,
        MacroMode::Unchanged,
        MacroMode::Unchanged,
        MacroMode::ChangeTarget,
        MacroMode::ChangeTarget,
        MacroMode::ChangeTarget,
    };

    assert(plans[0U].targetTrack == 1U);
    assert(plans[0U].canonicalResetTrackMask == 0x0002U);
    assert(plans[0U].incomingOwnerPolicy ==
           seq::SequencerActiveTrackIncomingOwnerPolicy::Reset);
    assert(plans[1U].beforeEnabledMask == 0x0003U);
    assert(plans[1U].afterEnabledMask == 0x0002U);
    assert(plans[2U].targetTrack == kInvalidTrack);
    assert(plans[2U].affectedTrackMask == 0x000AU);
    assert(plans[2U].capturedTrackMask == 0x0006U);
    assert(plans[3U].macroCapturedTrackMask == 0x0003U);
    assert(plans[4U].beforeActiveTrack == plans[4U].afterActiveTrack);
    assert(plans[5U].afterActiveTrack == 1U);
    assert(plans[6U].action == Action::MacroCreate);

    for (std::size_t index = 0U; index < plans.size(); ++index) {
        runSuccessfulAction(plans[index], macroModes[index]);
    }

    auto moveHarness = makeHarness(macroResetT1Plan(), MacroMode::ChangeTarget);
    seedRequiredOwners(*moveHarness, moveHarness->script.plans[0U]);
    Prepared first =
        core::handler::prepareSequencerTrackStructureTransaction(
            moveHarness->refs(),
            Action::MacroReset,
            moveHarness->execution()
        );
    assert(first.ready());
    Prepared second(std::move(first));
    assert(second.ready());
    const Result movedFromResult =
        core::handler::commitPreparedSequencerTrackStructureTransaction(
            std::move(first)
        );
    assert(movedFromResult.status == Status::Invalid);
    Prepared third = std::move(second);
    assert(third.ready());
    assertPlanEquals(third.plan(), macroResetT1Plan());
    const Result movedCommit =
        core::handler::commitPreparedSequencerTrackStructureTransaction(
            std::move(third)
        );
    assert(movedCommit.status == Status::Committed);
    assertCommittedLifecycle(moveHarness->script, true);

    auto sourceHarness = makeHarness(createPlan());
    auto replacedHarness = makeHarness(createPlan());
    seedRequiredOwners(*sourceHarness, createPlan());
    seedRequiredOwners(*replacedHarness, createPlan());
    {
        allocation_trace::Scope trace;
        Prepared source =
            core::handler::prepareSequencerTrackStructureTransaction(
                sourceHarness->refs(),
                Action::SequencerCreate,
                sourceHarness->execution()
            );
        Prepared replaced =
            core::handler::prepareSequencerTrackStructureTransaction(
                replacedHarness->refs(),
                Action::SequencerCreate,
                replacedHarness->execution()
            );
        assert(source.ready());
        assert(replaced.ready());
        const std::size_t liveBeforeAssignment = allocation_trace::liveCount;
        replaced = std::move(source);
        assert(source.status() == Status::Invalid);
        assert(!source.ready());
        assert(replaced.ready());
        assert(allocation_trace::liveCount < liveBeforeAssignment);
        const Result assignedCommit =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(replaced)
            );
        assert(assignedCommit.status == Status::Committed);
        assertCommittedLifecycle(sourceHarness->script, false);
        sourceHarness->script.committed.reset();
        assert(allocation_trace::liveCount == 0U);
    }

    std::cout << "[PASS] ABI, canonical plans and all seven actions commit\n";
}

constexpr std::array<std::size_t, 7U> kCreateRequests{
    sizeof(Change),
    sizeof(Graph), sizeof(CcBank),
    sizeof(Graph), sizeof(CcBank),
    sizeof(Graph), sizeof(CcBank),
};

constexpr std::array<std::size_t, 9U> kRemoveRequests{
    sizeof(Change),
    sizeof(Graph), sizeof(CcBank),
    sizeof(Graph), sizeof(CcBank),
    sizeof(Graph), sizeof(CcBank),
    sizeof(Graph), sizeof(CcBank),
};

constexpr std::array<std::size_t, 8U> kMacroT1Requests{
    sizeof(Change),
    sizeof(Graph), sizeof(CcBank),
    sizeof(Graph), sizeof(CcBank),
    sizeof(MacroPayload),
    sizeof(ControlDomain),
    sizeof(ControlDomain),
};

constexpr std::array<std::size_t, 12U> kMacroT2Requests{
    sizeof(Change),
    sizeof(Graph), sizeof(CcBank),
    sizeof(Graph), sizeof(CcBank),
    sizeof(Graph), sizeof(CcBank),
    sizeof(Graph), sizeof(CcBank),
    sizeof(MacroPayload),
    sizeof(ControlDomain),
    sizeof(ControlDomain),
};

template <std::size_t N>
void runFailureMatrix(
    const Plan& plan,
    MacroMode macroMode,
    const std::array<std::size_t, N>& expected
) {
    for (std::size_t ordinal = 1U; ordinal <= N; ++ordinal) {
        auto harness = makeHarness(plan, macroMode);
        seedRequiredOwners(*harness, plan);
        const LiveProof before = captureLiveProof(*harness);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            allocation_trace::Scope trace;
            const Result result =
                core::handler::executeSequencerTrackStructureTransaction(
                    harness->refs(),
                    plan.action,
                    harness->execution()
                );
            assert(result.status == Status::AllocationUnavailable);
            assert(!result.settled());
            assert(result.chronology.status == ChronologyStatus::Opened);
            if (allocation_trace::count != ordinal - 1U) {
                std::cerr
                    << "allocation-count mismatch action="
                    << static_cast<unsigned>(plan.action)
                    << " ordinal=" << ordinal
                    << " expected=" << (ordinal - 1U)
                    << " actual=" << allocation_trace::count
                    << " extmem-attempt="
                    << core::app::testing::extmemAllocationAttempt
                    << " armed="
                    << core::app::testing::extmemAllocationFailureOrdinal
                    << '\n';
                for (std::size_t index = 0U;
                     index < allocation_trace::count;
                     ++index) {
                    std::cerr << "  request[" << index << "]="
                              << allocation_trace::requests[index] << '\n';
                }
            }
            assertAllocationPrefix(
                expected.data(),
                expected.size(),
                ordinal - 1U
            );
            tx_assert::assertFailureConsumed(ordinal);
            assertLiveProof(*harness, before);
            assert(harness->script.publishCount == 0U);
            assert(harness->script.reconcileCount == 0U);
            assert(harness->script.commitCount == 0U);
            assert(harness->script.successfulCount == 0U);
            assert(!harness->script.committed);
            assert(allocation_trace::liveCount == 0U);
        }
        tx_assert::assertFailureInjectionReset();
    }

    auto harness = makeHarness(plan, macroMode);
    seedRequiredOwners(*harness, plan);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(N + 1U);
        allocation_trace::Scope trace;
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(),
                plan.action,
                harness->execution()
            );
        assert(result.status == Status::Committed);
        assertAllocationPrefix(expected.data(), expected.size(), N);
        tx_assert::assertMaxPlusOneStillArmed(N);
        assertCommittedLifecycle(harness->script, macroAction(plan.action));
        harness->script.committed.reset();
        assert(allocation_trace::liveCount == 0U);
    }
    tx_assert::assertFailureInjectionReset();
}

void test_exact_lock_p_failure_matrices_and_max_plus_one() {
    runFailureMatrix(
        createPlan(),
        MacroMode::Unchanged,
        kCreateRequests
    );
    runFailureMatrix(
        removeCurrentPlan(),
        MacroMode::Unchanged,
        kRemoveRequests
    );
    runFailureMatrix(
        macroResetT1Plan(),
        MacroMode::ChangeTarget,
        kMacroT1Requests
    );
    runFailureMatrix(
        macroPasteT2Plan(),
        MacroMode::ChangeTarget,
        kMacroT2Requests
    );

    std::cout
        << "[PASS] exact LOCK-P order, fail-Nth and max+1 matrices hold\n";
}

void test_commit_tail_allocates_nothing_and_leaves_failure_seam_unconsumed() {
    const Plan plan = macroPasteT2Plan();
    auto harness = makeHarness(plan, MacroMode::ChangeTarget);
    seedRequiredOwners(*harness, plan);
    Prepared prepared =
        core::handler::prepareSequencerTrackStructureTransaction(
            harness->refs(),
            plan.action,
            harness->execution()
        );
    assert(prepared.ready());
    assert(harness->script.buildCount == 2U);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        allocation_trace::Scope trace;
        const Result result =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(prepared)
            );
        assert(result.status == Status::Committed);
        assertAllocationPrefix(nullptr, 0U, 0U);
        tx_assert::assertMaxPlusOneStillArmed(0U);
    }
    tx_assert::assertFailureInjectionReset();
    assertCommittedLifecycle(harness->script, true);

    std::cout << "[PASS] prepared commit tail performs zero allocation\n";
}

void test_invalid_surfaces_and_draft_guards_are_typed() {
    {
        const Plan plan = createPlan();
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(),
                static_cast<Action>(0xFFU),
                harness->execution()
            );
        assert(result.status == Status::Invalid);
        assert(harness->script.callCount == 0U);
    }
    {
        const Plan plan = createPlan();
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(),
                plan.action,
                harness->invalidExecution()
            );
        assert(result.status == Status::Invalid);
        assert(harness->script.callCount == 0U);
    }
    {
        Plan invalid = createPlan();
        invalid.targetTrack = 2U;
        auto harness = makeHarness(invalid);
        seedRequiredOwners(*harness, createPlan());
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(),
                Action::SequencerCreate,
                harness->execution()
            );
        assert(result.status == Status::Invalid);
        assertCalls(harness->script, {Call::Build});
    }
    {
        const Plan plan = macroResetT1Plan();
        auto harness = makeHarness(plan, MacroMode::ChangeTarget);
        seedRequiredOwners(*harness, plan);
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(false),
                plan.action,
                harness->execution()
            );
        assert(prepared.status() == Status::PublicationUnavailable);
        assert(prepared.plan().action == Action::MacroReset);
        assert(harness->script.callCount == 0U);
    }
    {
        const Plan plan = createPlan();
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        harness->sequencer.stepContentDraft.active.set(true);
        const uint32_t beforeRevision =
            harness->sequencer.stepContentDraft.revision.get();
        allocation_trace::Scope trace;
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(),
                plan.action,
                harness->execution()
            );
        assert(result.status == Status::DraftBlocked);
        assertAllocationPrefix(nullptr, 0U, 0U);
        assert(harness->script.callCount == 0U);
        assert(harness->sequencer.stepContentDraft.failure ==
               seq::SequencerStepContentDraftFailure::TRANSITION_BLOCKED);
        assert(harness->sequencer.stepContentDraft.blockedTransition ==
               seq::SequencerStepContentDraftBlockedTransition::TRACK);
        assert(harness->sequencer.stepContentDraft.revision.get() ==
               beforeRevision + 1U);
    }
    {
        const Plan plan = createPlan();
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(),
                plan.action,
                harness->execution()
            );
        assert(prepared.ready());
        harness->sequencer.stepContentDraft.active.set(true);
        const uint32_t beforeRevision =
            harness->sequencer.stepContentDraft.revision.get();
        const Result result =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(prepared)
            );
        assert(result.status == Status::Stale);
        assert(harness->sequencer.stepContentDraft.failure ==
               seq::SequencerStepContentDraftFailure::NONE);
        assert(harness->sequencer.stepContentDraft.blockedTransition ==
               seq::SequencerStepContentDraftBlockedTransition::NONE);
        assert(harness->sequencer.stepContentDraft.revision.get() ==
               beforeRevision);
        assert(harness->script.buildCount == 2U);
        assert(harness->script.publishCount == 0U);
        assert(harness->script.commitCount == 0U);
    }

    std::cout << "[PASS] invalid surfaces and Draft guards are typed\n";
}

void test_typed_plan_and_macro_provider_outcomes_are_preserved() {
    struct PlanCase {
        PlanOutcome outcome;
        Status status;
    };
    for (const auto& item : std::array<PlanCase, 2U>{
             PlanCase{PlanOutcome::Invalid, Status::Invalid},
             PlanCase{PlanOutcome::Stale, Status::Stale},
         }) {
        const Plan plan = createPlan();
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        harness->script.planOutcomes[0U] = item.outcome;
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(result.status == item.status);
        assertCalls(harness->script, {Call::Build});
        assertNoTail(harness->script);
    }

    struct MacroCase {
        MacroMode mode;
        Status status;
    };
    for (const auto& item : std::array<MacroCase, 2U>{
             MacroCase{MacroMode::Invalid, Status::Invalid},
             MacroCase{MacroMode::Stale, Status::Stale},
         }) {
        const Plan plan = macroResetT1Plan();
        auto harness = makeHarness(plan, item.mode);
        seedRequiredOwners(*harness, plan);
        const LiveProof before = captureLiveProof(*harness);
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(result.status == item.status);
        assertNoTail(harness->script);
        assertLiveProof(*harness, before);
    }

    std::cout << "[PASS] typed plan and Macro provider outcomes hold\n";
}

void test_typed_chronology_matrix_is_preserved_exactly() {
    struct Case {
        Chronology chronology;
        Status expectedStatus;
        bool ready;
    };
    const std::array<Case, 8U> cases{
        Case{{ChronologyStatus::Unavailable, PatternOutcome::NoPending},
             Status::HistoryUnavailable, false},
        Case{{ChronologyStatus::MacroAuditionBlocked,
              PatternOutcome::NoPending}, Status::Stale, false},
        Case{{ChronologyStatus::ProjectTrackGestureBlocked,
              PatternOutcome::NoPending}, Status::Stale, false},
        Case{{ChronologyStatus::PatternFailed, PatternOutcome::Failed},
             Status::HistoryUnavailable, false},
        Case{{ChronologyStatus::Opened, PatternOutcome::Failed},
             Status::HistoryUnavailable, false},
        Case{{ChronologyStatus::Opened, PatternOutcome::NoPending},
             Status::Prepared, true},
        Case{{ChronologyStatus::Opened, PatternOutcome::NoChange},
             Status::Prepared, true},
        Case{{ChronologyStatus::Opened, PatternOutcome::Committed},
             Status::Prepared, true},
    };

    for (const auto& item : cases) {
        const Plan plan = createPlan();
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        harness->script.chronology = item.chronology;
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(),
                plan.action,
                harness->execution()
            );
        assert(prepared.status() == item.expectedStatus);
        assert(prepared.ready() == item.ready);
        assert(prepared.chronology().status == item.chronology.status);
        assert(prepared.chronology().predecessorPattern ==
               item.chronology.predecessorPattern);
        assert(harness->script.boundaryCount == 1U);
    }

    std::cout << "[PASS] typed chronology matrix is preserved exactly\n";
}

void test_provider_availability_and_both_admission_gates() {
    const Plan plan = createPlan();
    {
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        harness->script.checkpointAvailable = false;
        allocation_trace::Scope trace;
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(),
                plan.action,
                harness->execution()
            );
        assert(result.status == Status::PublicationUnavailable);
        assertAllocationPrefix(nullptr, 0U, 0U);
        assertCalls(harness->script, {
            Call::Build, Call::Boundary, Call::Checkpoint,
        });
    }
    {
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(true, true, true, false),
                plan.action,
                harness->execution()
            );
        assert(result.status == Status::HistoryUnavailable);
        assert(harness->script.admitCount == 0U);
        assert(harness->script.publishCount == 0U);
        assert(harness->script.commitCount == 0U);
    }
    {
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        harness->script.admitOutcomes = {false, true};
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(),
                plan.action,
                harness->execution()
            );
        assert(result.status == Status::HistoryUnavailable);
        assert(harness->script.admitCount == 1U);
        assert(harness->script.publishCount == 0U);
        assert(harness->script.commitCount == 0U);
    }
    {
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(true, false),
                plan.action,
                harness->execution()
            );
        assert(result.status == Status::PublicationUnavailable);
        assert(harness->script.admitCount == 1U);
        assert(harness->script.publishCount == 0U);
        assert(harness->script.commitCount == 0U);
    }
    {
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        harness->script.admitOutcomes = {true, false};
        const LiveProof before = captureLiveProof(*harness);
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(),
                plan.action,
                harness->execution()
            );
        assert(result.status == Status::HistoryUnavailable);
        assert(harness->script.admitCount == 2U);
        assert(harness->script.publishCount == 0U);
        assert(harness->script.commitCount == 0U);
        assertLiveProof(*harness, before);
    }

    std::cout << "[PASS] provider availability and admission gates hold\n";
}

void replaceGraphWithEqualOwner(seq::SequencerPatternState& pattern) {
    assert(pattern.graph);
    auto replacement = core::app::makeExtmemUnique<Graph>(*pattern.graph);
    assert(replacement);
    pattern.graph = std::move(replacement);
}

void test_plan_tokens_checkpoint_and_live_drift_abort_before_tail() {
    const Plan direct = createPlan();
    {
        auto harness = makeHarness(direct);
        seedRequiredOwners(*harness, direct);
        harness->script.plans[1U].afterPage = 1U;
        allocation_trace::Scope trace;
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), direct.action, harness->execution()
            );
        assert(result.status == Status::Stale);
        assertAllocationPrefix(nullptr, 0U, 0U);
        assert(harness->script.buildCount == 2U);
        assertNoTail(harness->script);
    }
    {
        auto harness = makeHarness(direct);
        seedRequiredOwners(*harness, direct);
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(), direct.action, harness->execution()
            );
        assert(prepared.ready());
        harness->script.plans[2U].afterPage = 1U;
        const Result result =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(prepared)
            );
        assert(result.status == Status::Stale);
        assert(harness->script.buildCount == 3U);
        assertNoTail(harness->script);
    }
    {
        auto harness = makeHarness(direct);
        seedRequiredOwners(*harness, direct);
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(), direct.action, harness->execution()
            );
        assert(prepared.ready());
        harness->script.revalidateOutcome = false;
        const Result result =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(prepared)
            );
        assert(result.status == Status::Stale);
        assert(harness->script.revalidateCount == 1U);
        assertNoTail(harness->script);
    }
    {
        auto harness = makeHarness(direct);
        seedRequiredOwners(*harness, direct);
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(), direct.action, harness->execution()
            );
        assert(prepared.ready());
        ++harness->script.checkpoint.configRevision;
        const Result result =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(prepared)
            );
        assert(result.status == Status::Stale);
        assert(harness->script.checkpointCount == 2U);
        assertNoTail(harness->script);
    }
    {
        auto harness = makeHarness(direct);
        seedRequiredOwners(*harness, direct);
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(), direct.action, harness->execution()
            );
        assert(prepared.ready());
        ++harness->sequencer.pattern.note[0U];
        const Result result =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(prepared)
            );
        assert(result.status == Status::Stale);
        assertNoTail(harness->script);
    }
    {
        const Plan plan = macroResetT2Plan();
        auto harness = makeHarness(plan, MacroMode::ChangeTarget);
        seedRequiredOwners(*harness, plan);
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(prepared.ready());
        replaceGraphWithEqualOwner(harness->tracks.track(1U));
        const Result result =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(prepared)
            );
        assert(result.status == Status::Stale);
        assertNoTail(harness->script);
    }
    {
        const Plan plan = macroResetT2Plan();
        auto harness = makeHarness(plan, MacroMode::ChangeTarget);
        seedRequiredOwners(*harness, plan);
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(prepared.ready());
        ++harness->macros.tracks[1U].pages[0U].cc[0U];
        const Result result =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(prepared)
            );
        assert(result.status == Status::Stale);
        assertNoTail(harness->script);
    }
    {
        const Plan plan = macroResetT2Plan();
        auto harness = makeHarness(plan, MacroMode::ChangeOther);
        seedRequiredOwners(*harness, plan);
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(result.status == Status::Invalid);
        assert(harness->script.macroAfterCount == 1U);
        assert(harness->script.admitCount == 0U);
        assertNoTail(harness->script);
    }

    std::cout << "[PASS] plan, token, checkpoint and live drifts abort\n";
}

void assertNoChangeLifecycle(const Script& script) {
    assertCalls(script, {
        Call::Build,
        Call::Boundary,
        Call::Checkpoint,
        Call::Build,
        Call::MacroAfter,
        Call::Build,
        Call::Revalidate,
        Call::Checkpoint,
        Call::NoChange,
        Call::Successful,
    });
    assert(script.buildCount == 3U);
    assert(script.boundaryCount == 1U);
    assert(script.checkpointCount == 2U);
    assert(script.macroAfterCount == 1U);
    assert(script.admitCount == 0U);
    assert(script.publishCount == 0U);
    assert(script.reconcileCount == 0U);
    assert(script.commitCount == 0U);
    assert(script.noChangeCount == 1U);
    assert(script.successfulCount == 1U);
    assert(!script.committed);
}

void test_no_change_and_macro_control_normalization_contracts() {
    const std::array<Plan, 2U> noChangePlans{
        macroResetT1Plan(),
        macroPasteT1Plan(),
    };
    for (const Plan& plan : noChangePlans) {
        auto harness = makeHarness(plan, MacroMode::Unchanged);
        seedRequiredOwners(*harness, plan);
        const LiveProof before = captureLiveProof(*harness);
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(result.status == Status::NoChange);
        assert(!result.committed());
        assert(result.settled());
        assertNoChangeLifecycle(harness->script);
        assertLiveProof(*harness, before);
    }

    {
        const Plan plan = macroResetT1Plan();
        auto harness = makeHarness(plan, MacroMode::ChangeTarget);
        seedRequiredOwners(*harness, plan);
        const uint32_t controlRevisionBefore =
            harness->macros.control.authoredRevision;
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(result.status == Status::Committed);
        assert(harness->script.committed->macroStructure);
        assert(!harness->script.committed->macroStructure->afterControl);
        assert(!harness->script.admittedAfterControl);
        assert(harness->macros.control.authoredRevision ==
               controlRevisionBefore);
        assert(seq::liveMacroTrackStructureMatches(
            harness->macros,
            *harness->script.committed->macroStructure,
            true
        ));
    }

    {
        const Plan plan = macroResetT1Plan();
        auto harness = makeHarness(plan, MacroMode::ClearTargetControl);
        seedRequiredOwners(*harness, plan);
        const mac::MacroAutomationSlotAddress address{
            .track = 0U,
            .page = 0U,
            .macro = 0U,
        };
        const mod::ModulatorId lfoId =
            test_support::project_control::addLocalLfo(
            harness->macros.control,
            address,
            "Track transaction LFO"
        );
        assert(mod::valid(lfoId));
        const uint32_t controlRevisionBefore =
            harness->macros.control.authoredRevision;
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(result.status == Status::Committed);
        assert(harness->script.committed->macroStructure);
        assert(harness->script.committed->macroStructure->afterControl);
        assert(harness->script.admittedAfterControl);
        assert(harness->macros.control.authoredRevision ==
               controlRevisionBefore + 1U);
        assert(seq::liveMacroTrackStructureMatches(
            harness->macros,
            *harness->script.committed->macroStructure,
            true
        ));
        assert(test_support::project_control::outputBindingCountAt(
            harness->macros.control,
            address
        ) == 0U);
    }

    std::cout
        << "[PASS] NoChange and equal/distinct Macro control normalization hold\n";
}

void test_activation_guard_rejects_collisions_and_any_late_queue_drift() {
    const Plan plan = createPlan();
    for (const bool playing : {false, true}) {
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        armActivation(harness->activationQueue, 0x0002U, playing);
        const uint16_t pendingBefore =
            harness->activationQueue.pendingTrackMask();
        const uint32_t revisionBefore =
            harness->activationQueue.telemetryRevision().get();
        allocation_trace::Scope trace;
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(result.status == Status::Stale);
        assertAllocationPrefix(nullptr, 0U, 0U);
        assert(harness->activationQueue.pendingTrackMask() == pendingBefore);
        assert(harness->activationQueue.telemetryRevision().get() ==
               revisionBefore);
        assertNoTail(harness->script);
    }

    {
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        armActivation(harness->activationQueue, 0x0004U);
        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(result.status == Status::Committed);
        assert(harness->activationQueue.pendingTrackMask() == 0x0004U);
        assertCommittedLifecycle(harness->script, false);
    }

    {
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(prepared.ready());
        armActivation(harness->activationQueue, 0x0004U);
        const Result result =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(prepared)
            );
        assert(result.status == Status::Stale);
        assert(harness->activationQueue.pendingTrackMask() == 0x0004U);
        assertNoTail(harness->script);
    }

    std::cout
        << "[PASS] activation guard collision and exact late drift hold\n";
}

struct ColdOwners {
    const Graph* graph = nullptr;
    const CcBank* cc = nullptr;
};

ColdOwners coldOwners(const seq::SequencerPatternState& pattern) {
    return {pattern.graph.get(), pattern.ccLanes.get()};
}

void test_create_reset_and_remove_preserve_rotate_exact_owners() {
    {
        const Plan plan = createPlan();
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        const ColdOwners editor = coldOwners(harness->sequencer.pattern);
        const ColdOwners outgoingScratch =
            coldOwners(harness->tracks.track(0U));
        const ColdOwners incoming = coldOwners(harness->tracks.track(1U));
        assert(editor.graph != outgoingScratch.graph);
        assert(editor.graph != incoming.graph);
        assert(outgoingScratch.graph != incoming.graph);
        assert(editor.cc != outgoingScratch.cc);
        assert(editor.cc != incoming.cc);
        assert(outgoingScratch.cc != incoming.cc);

        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(result.status == Status::Committed);
        assert(coldOwners(harness->tracks.track(0U)).graph == editor.graph);
        assert(coldOwners(harness->tracks.track(0U)).cc == editor.cc);
        assert(harness->sequencer.pattern.graph == nullptr);
        assert(harness->sequencer.pattern.ccLanes == nullptr);
        assert(coldOwners(harness->tracks.track(1U)).graph ==
               outgoingScratch.graph);
        assert(coldOwners(harness->tracks.track(1U)).cc ==
               outgoingScratch.cc);
        assert(seq::liveHistoryStructureSnapshotMatches(
            harness->tracks,
            harness->sequencer,
            harness->script.committed->after
        ));
    }

    {
        const Plan plan = removeCurrentPlan();
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        const ColdOwners editor = coldOwners(harness->sequencer.pattern);
        const ColdOwners outgoingScratch =
            coldOwners(harness->tracks.track(0U));
        const ColdOwners incoming = coldOwners(harness->tracks.track(1U));

        const Result result =
            core::handler::executeSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(result.status == Status::Committed);
        assert(coldOwners(harness->tracks.track(0U)).graph == editor.graph);
        assert(coldOwners(harness->tracks.track(0U)).cc == editor.cc);
        assert(harness->sequencer.pattern.graph.get() == incoming.graph);
        assert(harness->sequencer.pattern.ccLanes.get() == incoming.cc);
        assert(coldOwners(harness->tracks.track(1U)).graph ==
               outgoingScratch.graph);
        assert(coldOwners(harness->tracks.track(1U)).cc ==
               outgoingScratch.cc);
        assert(seq::liveHistoryStructureSnapshotMatches(
            harness->tracks,
            harness->sequencer,
            harness->script.committed->after
        ));
    }

    {
        const Plan plan = removeCurrentPlan();
        auto harness = makeHarness(plan);
        seedRequiredOwners(*harness, plan);
        Prepared prepared =
            core::handler::prepareSequencerTrackStructureTransaction(
                harness->refs(), plan.action, harness->execution()
            );
        assert(prepared.ready());
        replaceGraphWithEqualOwner(harness->tracks.track(0U));
        const Result result =
            core::handler::commitPreparedSequencerTrackStructureTransaction(
                std::move(prepared)
            );
        assert(result.status == Status::Stale);
        assertNoTail(harness->script);
    }

    std::cout
        << "[PASS] Reset/Preserve owner rotations and scratch drift hold\n";
}

}  // namespace

int main() {
    test_abi_plans_and_all_seven_actions_commit();
    test_exact_lock_p_failure_matrices_and_max_plus_one();
    test_commit_tail_allocates_nothing_and_leaves_failure_seam_unconsumed();
    test_invalid_surfaces_and_draft_guards_are_typed();
    test_typed_plan_and_macro_provider_outcomes_are_preserved();
    test_typed_chronology_matrix_is_preserved_exactly();
    test_provider_availability_and_both_admission_gates();
    test_plan_tokens_checkpoint_and_live_drift_abort_before_tail();
    test_no_change_and_macro_control_normalization_contracts();
    test_activation_guard_rejects_collisions_and_any_late_queue_drift();
    test_create_reset_and_remove_preserve_rotate_exact_owners();
    std::cout
        << "All SequencerPreparedTrackStructureTransaction tests passed\n";
    return 0;
}
