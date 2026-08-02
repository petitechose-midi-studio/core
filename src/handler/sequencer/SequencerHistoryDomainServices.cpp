#include "handler/sequencer/SequencerHistoryDomainServices.hpp"

#include <config/PlatformCompat.hpp>
#include <utility>

#include "state/CoreState.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"

namespace core::handler {

namespace {

[[noreturn]] FLASHMEM void failAdmittedStructureInvariant() {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_trap();
#else
    for (;;) {}
#endif
}

FLASHMEM bool recordPatternFromCoreState(
    void* context, core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor) {
    if (context == nullptr) { return false; }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->recordSequencerPatternHistory(std::move(before), std::move(after), descriptor);
}

FLASHMEM bool recordFlatPatternFromCoreState(
    void* context, core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor) {
    if (context == nullptr) { return false; }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->recordSequencerPatternHistory(
        std::move(before), std::move(after), descriptor,
        core::state::sequencer::SequencerHistoryPatternStorage::FlatOnly);
}

FLASHMEM bool recordPatternChangeFromCoreState(
    void* context, core::state::sequencer::SequencerHistoryPatternChangePtr change) {
    if (context == nullptr) return false;
    return static_cast<core::state::CoreState*>(context)->recordSequencerPatternHistory(
        std::move(change));
}

FLASHMEM bool canRecordPatternFromCoreState(
    void* context, const core::state::sequencer::SequencerHistoryPatternChange& change) {
    if (context == nullptr) return false;
    return static_cast<const core::state::CoreState*>(context)->sequencerHistory.canRecordPattern(
        change);
}

FLASHMEM void recordPreparedPatternFromCoreState(
    void* context, core::state::sequencer::SequencerHistoryPatternChangePtr change) {
    if (context == nullptr || !change) return;
    auto* state = static_cast<core::state::CoreState*>(context);
    // Public compatibility adapters remain defensive for callers that probe
    // admission and may still transfer on rejection. The sealed Core
    // coalescer publishes through its direct trusted path.
    if (!state->sequencerHistory.canRecordPattern(*change)) return;
    state->sequencerHistory.recordPreparedPattern(std::move(change));
    state->markProjectMutated();
}

FLASHMEM void recordPreparedSynchronizedPatternFromCoreState(
    void* context, core::state::sequencer::SequencerHistoryPatternChangePtr change) {
    if (context == nullptr || !change) return;
    auto* state = static_cast<core::state::CoreState*>(context);
    if (!state->sequencerHistory.canRecordPattern(*change)) return;
    state->sequencerHistory.recordPreparedPattern(std::move(change));
    state->publishPreparedSequencerMutation();
}

FLASHMEM bool recordFullBankFromCoreState(
    void* context, core::state::sequencer::SequencerHistoryFullBankChangePtr change) {
    if (context == nullptr) { return false; }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->recordSequencerBankHistory(std::move(change));
}

FLASHMEM bool canRecordFullBankFromCoreState(
    void* context, const core::state::sequencer::SequencerHistoryFullBankChange& change) {
    if (context == nullptr) return false;
    return static_cast<const core::state::CoreState*>(context)->canRecordSequencerBankHistory(
        change);
}

FLASHMEM void recordPreparedFullBankFromCoreState(
    void* context, core::state::sequencer::SequencerHistoryFullBankChangePtr change) {
    if (context == nullptr || !change) return;
    static_cast<core::state::CoreState*>(context)->recordPreparedSequencerBankHistory(
        std::move(change));
}

FLASHMEM bool recordStructureFromCoreState(
    void* context, core::state::sequencer::SequencerHistoryTrackStructureChangePtr change) {
    if (context == nullptr) { return false; }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->recordSequencerStructureHistory(std::move(change));
}

FLASHMEM bool canRecordStructureFromCoreState(
    const void* context,
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change) {
    if (context == nullptr) { return false; }

    const auto* state = static_cast<const core::state::CoreState*>(context);
    return state->canRecordSequencerStructureHistory(change);
}

FLASHMEM void recordPreparedStructureFromCoreState(
    void* context, core::state::sequencer::SequencerHistoryTrackStructureChangePtr change) {
    if (context == nullptr) return;
    auto* state = static_cast<core::state::CoreState*>(context);
    state->recordPreparedSequencerStructureHistory(std::move(change));
}

FLASHMEM void commitAdmittedStructureFromCoreState(
    void* context,
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
) noexcept {
    if (context == nullptr || !change) failAdmittedStructureInvariant();
    static_cast<core::state::CoreState*>(context)
        ->commitAdmittedSequencerStructureHistory(std::move(change));
}

FLASHMEM bool beginCoalescedPatternEditFromCoreState(
    void* context, uint8_t step, core::state::sequencer::StepProperty property, uint32_t nowMs,
    core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan, bool stateProperty) {
    if (context == nullptr) { return false; }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->beginOrContinueSequencerPatternHistoryCoalescing(step, property, nowMs,
                                                                   payloadPlan, stateProperty);
}

FLASHMEM bool sealCoalescedPatternEditFromCoreState(void* context, bool mutationChanged) {
    if (context == nullptr) { return false; }

    return static_cast<core::state::CoreState*>(context)->sealSequencerPatternHistoryCoalescing(
        mutationChanged);
}

FLASHMEM bool beginCoalescedCcLaneEventEditFromCoreState(
    void* context, uint8_t lane, uint8_t step, int32_t beforeValue, int32_t afterValue,
    const core::state::sequencer::SequencerCcLaneBank* afterBank, uint32_t nowMs) {
    if (context == nullptr) return false;
    return static_cast<core::state::CoreState*>(context)
        ->beginOrContinueSequencerCcLaneEventHistoryCoalescing(lane, step, beforeValue, afterValue,
                                                               afterBank, nowMs);
}

FLASHMEM core::state::sequencer::SequencerPatternHistoryCommitOutcome
commitCoalescedPatternEditFromCoreState(void* context) {
    using Outcome = core::state::sequencer::SequencerPatternHistoryCommitOutcome;
    if (context == nullptr) { return Outcome::Failed; }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->commitSequencerPatternHistoryCoalescingOutcome();
}

FLASHMEM core::state::sequencer::SequencerTrackStructureChronologyResult
openTrackStructureChronologyBoundaryFromCoreState(void* context) {
    if (context == nullptr) return {};
    return static_cast<core::state::CoreState*>(context)
        ->openSequencerTrackStructureChronologyBoundary();
}

FLASHMEM core::state::sequencer::SequencerPreparedPatternEditBeginOutcome
beginPreparedPatternEditFromCoreState(
    void* context, core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
    core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
    core::state::sequencer::SequencerHistoryDescriptor descriptor, bool compactGraphOnSeal) {
    using Outcome = core::state::sequencer::SequencerPreparedPatternEditBeginOutcome;
    if (context == nullptr) return Outcome::Failed;
    return static_cast<core::state::CoreState*>(context)
        ->beginOrContinueSequencerPreparedPatternEdit(owner, key, payloadPlan, descriptor,
                                                      compactGraphOnSeal);
}

FLASHMEM bool preparedPatternEditReadyFromCoreState(
    void* context,
    core::state::sequencer::SequencerPreparedPatternEditOwner owner,
    uint8_t key,
    uint8_t expectedTrack
) {
    if (context == nullptr) return false;
    return static_cast<const core::state::CoreState*>(context)
        ->sequencerPreparedPatternEditReady(owner, key, expectedTrack);
}

FLASHMEM core::state::sequencer::
    SequencerPreparedPatternGraphPrecompactionOutcome
precompactPreparedPatternEditGraphFromCoreState(
    void* context,
    core::state::sequencer::SequencerPreparedPatternEditOwner owner,
    uint8_t key,
    uint8_t expectedTrack,
    core::state::sequencer::SequencerPreparedGraphContentPath& contentPath
) {
    using Outcome = core::state::sequencer::
        SequencerPreparedPatternGraphPrecompactionOutcome;
    if (context == nullptr) return Outcome::Failed;
    return static_cast<core::state::CoreState*>(context)
        ->precompactSequencerPreparedPatternEditGraph(
            owner, key, expectedTrack, contentPath);
}

FLASHMEM core::state::sequencer::SequencerPreparedPatternEditSealOutcome
sealPreparedPatternEditFromCoreState(
    void* context, core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
    bool mutationChanged, core::state::sequencer::SequencerHistoryDescriptor descriptor) {
    using Outcome = core::state::sequencer::SequencerPreparedPatternEditSealOutcome;
    if (context == nullptr) return Outcome::Failed;
    return static_cast<core::state::CoreState*>(context)->sealSequencerPreparedPatternEdit(
        owner, key, mutationChanged, descriptor);
}

FLASHMEM core::state::sequencer::SequencerPreparedPatternEditCommitOutcome
commitPreparedPatternEditFromCoreState(
    void* context, core::state::sequencer::SequencerPreparedPatternEditOwner owner) {
    using Outcome = core::state::sequencer::SequencerPreparedPatternEditCommitOutcome;
    if (context == nullptr) return Outcome::Failed;
    return static_cast<core::state::CoreState*>(context)->commitSequencerPreparedPatternEdit(owner);
}

FLASHMEM core::state::sequencer::SequencerPreparedPatternEditAbortOutcome
abortPreparedPatternEditFromCoreState(
    void* context,
    core::state::sequencer::SequencerPreparedPatternEditOwner owner,
    uint8_t key
) {
    using Outcome = core::state::sequencer::SequencerPreparedPatternEditAbortOutcome;
    if (context == nullptr) return Outcome::Failed;
    return static_cast<core::state::CoreState*>(context)
        ->abortSequencerPreparedPatternEdit(owner, key);
}

FLASHMEM core::state::sequencer::SequencerPreparedFullBankEditResult
applyPreparedProjectScaleChoiceFromCoreState(
    void* context,
    core::state::sequencer::SequencerPreparedFullBankEditOwner owner,
    uint8_t row,
    int choiceIndex
) {
    if (context == nullptr) return {};
    return static_cast<core::state::CoreState*>(context)
        ->applyPreparedProjectScaleChoice(owner, row, choiceIndex);
}

}  // namespace

const SequencerHistoryDomainServices::Operations SequencerHistoryDomainServices::EMPTY_OPERATIONS
    PROGMEM{};

FLASHMEM SequencerHistoryDomainServices::SequencerHistoryDomainServices(
    void* context, const Operations* operations)
    : context_(context), operations_(operations) {}

FLASHMEM SequencerHistoryDomainServices
SequencerHistoryDomainServices::fromCoreState(core::state::CoreState& state) {
    static constexpr Operations operations PROGMEM{
        .recordPattern = recordPatternFromCoreState,
        .recordFlatPattern = recordFlatPatternFromCoreState,
        .recordPatternChange = recordPatternChangeFromCoreState,
        .canRecordPattern = canRecordPatternFromCoreState,
        .recordPreparedPattern = recordPreparedPatternFromCoreState,
        .recordPreparedSynchronizedPattern = recordPreparedSynchronizedPatternFromCoreState,
        .recordStructure = recordStructureFromCoreState,
        .canRecordStructure = canRecordStructureFromCoreState,
        .recordPreparedStructure = recordPreparedStructureFromCoreState,
        .commitAdmittedStructure = commitAdmittedStructureFromCoreState,
        .recordFullBank = recordFullBankFromCoreState,
        .canRecordFullBank = canRecordFullBankFromCoreState,
        .recordPreparedFullBank = recordPreparedFullBankFromCoreState,
        .beginCoalescedPatternEdit = beginCoalescedPatternEditFromCoreState,
        .sealCoalescedPatternEdit = sealCoalescedPatternEditFromCoreState,
        .beginCoalescedCcLaneEventEdit = beginCoalescedCcLaneEventEditFromCoreState,
        .commitCoalescedPatternEdit = commitCoalescedPatternEditFromCoreState,
        .openTrackStructureChronologyBoundary =
            openTrackStructureChronologyBoundaryFromCoreState,
        .beginPreparedPatternEdit = beginPreparedPatternEditFromCoreState,
        .preparedPatternEditReady = preparedPatternEditReadyFromCoreState,
        .precompactPreparedPatternEditGraph =
            precompactPreparedPatternEditGraphFromCoreState,
        .sealPreparedPatternEdit = sealPreparedPatternEditFromCoreState,
        .commitPreparedPatternEdit = commitPreparedPatternEditFromCoreState,
        .abortPreparedPatternEdit = abortPreparedPatternEditFromCoreState,
        .applyPreparedProjectScaleChoice = applyPreparedProjectScaleChoiceFromCoreState,
    };
    return fromStaticOperations<operations>(&state);
}

FLASHMEM bool SequencerHistoryDomainServices::recordPattern(
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor) const {
    return operations_->recordPattern != nullptr &&
           operations_->recordPattern(context_, std::move(before), std::move(after), descriptor);
}

FLASHMEM bool SequencerHistoryDomainServices::canRecordPattern(
    const core::state::sequencer::SequencerHistoryPatternChange& change) const {
    return operations_->canRecordPattern != nullptr &&
           operations_->recordPreparedPattern != nullptr &&
           operations_->canRecordPattern(context_, change);
}

FLASHMEM void SequencerHistoryDomainServices::recordPreparedPattern(
    core::state::sequencer::SequencerHistoryPatternChangePtr change) const {
    if (operations_->recordPreparedPattern == nullptr) return;
    operations_->recordPreparedPattern(context_, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::canRecordSynchronizedPattern(
    const core::state::sequencer::SequencerHistoryPatternChange& change) const {
    return operations_->canRecordPattern != nullptr &&
           operations_->recordPreparedSynchronizedPattern != nullptr &&
           operations_->canRecordPattern(context_, change);
}

FLASHMEM void SequencerHistoryDomainServices::recordPreparedSynchronizedPattern(
    core::state::sequencer::SequencerHistoryPatternChangePtr change) const {
    if (operations_->recordPreparedSynchronizedPattern == nullptr) return;
    operations_->recordPreparedSynchronizedPattern(context_, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::recordFlatPattern(
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor) const {
    return operations_->recordFlatPattern != nullptr &&
           operations_->recordFlatPattern(context_, std::move(before), std::move(after),
                                          descriptor);
}

FLASHMEM bool SequencerHistoryDomainServices::recordPattern(
    core::state::sequencer::SequencerHistoryPatternChangePtr change) const {
    return operations_->recordPatternChange != nullptr &&
           operations_->recordPatternChange(context_, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::recordStructure(
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change) const {
    return operations_->recordStructure != nullptr &&
           operations_->recordStructure(context_, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::canRecordStructure(
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change) const {
    return operations_->canRecordStructure != nullptr &&
           operations_->recordPreparedStructure != nullptr &&
           operations_->canRecordStructure(context_, change);
}

FLASHMEM void SequencerHistoryDomainServices::recordPreparedStructure(
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change) const {
    if (operations_->recordPreparedStructure == nullptr) return;
    operations_->recordPreparedStructure(context_, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::canCommitAdmittedStructure(
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change) const {
    return operations_->canRecordStructure != nullptr &&
           operations_->commitAdmittedStructure != nullptr &&
           operations_->canRecordStructure(context_, change);
}

FLASHMEM void SequencerHistoryDomainServices::commitAdmittedStructure(
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
) const noexcept {
    if (operations_->commitAdmittedStructure == nullptr || !change) {
        failAdmittedStructureInvariant();
    }
    operations_->commitAdmittedStructure(context_, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::recordFullBank(
    core::state::sequencer::SequencerHistoryFullBankChangePtr change) const {
    return operations_->recordFullBank != nullptr &&
           operations_->recordFullBank(context_, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::canRecordFullBank(
    const core::state::sequencer::SequencerHistoryFullBankChange& change) const {
    return operations_->canRecordFullBank != nullptr &&
           operations_->recordPreparedFullBank != nullptr &&
           operations_->canRecordFullBank(context_, change);
}

FLASHMEM void SequencerHistoryDomainServices::recordPreparedFullBank(
    core::state::sequencer::SequencerHistoryFullBankChangePtr change) const {
    if (operations_->recordPreparedFullBank == nullptr) return;
    operations_->recordPreparedFullBank(context_, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::beginCoalescedPatternEdit(
    uint8_t step, core::state::sequencer::StepProperty property, uint32_t nowMs,
    core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
    bool stateProperty) const {
    return operations_->beginCoalescedPatternEdit != nullptr &&
           operations_->beginCoalescedPatternEdit(context_, step, property, nowMs, payloadPlan,
                                                  stateProperty);
}

FLASHMEM bool SequencerHistoryDomainServices::sealCoalescedPatternEdit(bool mutationChanged) const {
    return operations_->sealCoalescedPatternEdit != nullptr &&
           operations_->sealCoalescedPatternEdit(context_, mutationChanged);
}

FLASHMEM bool SequencerHistoryDomainServices::beginCoalescedCcLaneEventEdit(
    uint8_t lane, uint8_t step, int32_t beforeValue, int32_t afterValue,
    const core::state::sequencer::SequencerCcLaneBank* afterBank, uint32_t nowMs) const {
    return operations_->beginCoalescedCcLaneEventEdit != nullptr &&
           operations_->beginCoalescedCcLaneEventEdit(context_, lane, step, beforeValue, afterValue,
                                                      afterBank, nowMs);
}

FLASHMEM core::state::sequencer::SequencerPatternHistoryCommitOutcome
SequencerHistoryDomainServices::commitCoalescedPatternEditOutcome() const {
    using Outcome = core::state::sequencer::SequencerPatternHistoryCommitOutcome;
    return operations_->commitCoalescedPatternEdit != nullptr
               ? operations_->commitCoalescedPatternEdit(context_)
               : Outcome::Failed;
}

FLASHMEM core::state::sequencer::SequencerTrackStructureChronologyResult
SequencerHistoryDomainServices::openTrackStructureChronologyBoundary() const {
    return operations_->openTrackStructureChronologyBoundary != nullptr
        ? operations_->openTrackStructureChronologyBoundary(context_)
        : core::state::sequencer::SequencerTrackStructureChronologyResult{};
}

FLASHMEM core::state::sequencer::SequencerPreparedPatternEditBeginOutcome
SequencerHistoryDomainServices::beginPreparedPatternEdit(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
    core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
    core::state::sequencer::SequencerHistoryDescriptor descriptor, bool compactGraphOnSeal) const {
    using Outcome = core::state::sequencer::SequencerPreparedPatternEditBeginOutcome;
    return operations_->beginPreparedPatternEdit != nullptr
               ? operations_->beginPreparedPatternEdit(context_, owner, key, payloadPlan,
                                                       descriptor, compactGraphOnSeal)
               : Outcome::Failed;
}

FLASHMEM bool SequencerHistoryDomainServices::preparedPatternEditReady(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner,
    uint8_t key,
    uint8_t expectedTrack
) const {
    return operations_->preparedPatternEditReady != nullptr &&
           operations_->preparedPatternEditReady(
               context_, owner, key, expectedTrack);
}

FLASHMEM core::state::sequencer::
    SequencerPreparedPatternGraphPrecompactionOutcome
SequencerHistoryDomainServices::precompactPreparedPatternEditGraph(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner,
    uint8_t key,
    uint8_t expectedTrack,
    core::state::sequencer::SequencerPreparedGraphContentPath& contentPath
) const {
    using Outcome = core::state::sequencer::
        SequencerPreparedPatternGraphPrecompactionOutcome;
    return operations_->precompactPreparedPatternEditGraph != nullptr
        ? operations_->precompactPreparedPatternEditGraph(
              context_, owner, key, expectedTrack, contentPath)
        : Outcome::Failed;
}

FLASHMEM core::state::sequencer::SequencerPreparedPatternEditSealOutcome
SequencerHistoryDomainServices::sealPreparedPatternEdit(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
    bool mutationChanged, core::state::sequencer::SequencerHistoryDescriptor descriptor) const {
    using Outcome = core::state::sequencer::SequencerPreparedPatternEditSealOutcome;
    return operations_->sealPreparedPatternEdit != nullptr
               ? operations_->sealPreparedPatternEdit(context_, owner, key, mutationChanged,
                                                      descriptor)
               : Outcome::Failed;
}

FLASHMEM core::state::sequencer::SequencerPreparedPatternEditCommitOutcome
SequencerHistoryDomainServices::commitPreparedPatternEdit(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner) const {
    using Outcome = core::state::sequencer::SequencerPreparedPatternEditCommitOutcome;
    return operations_->commitPreparedPatternEdit != nullptr
               ? operations_->commitPreparedPatternEdit(context_, owner)
               : Outcome::Failed;
}

FLASHMEM core::state::sequencer::SequencerPreparedPatternEditAbortOutcome
SequencerHistoryDomainServices::abortPreparedPatternEdit(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner,
    uint8_t key
) const {
    using Outcome = core::state::sequencer::SequencerPreparedPatternEditAbortOutcome;
    return operations_->abortPreparedPatternEdit != nullptr
               ? operations_->abortPreparedPatternEdit(context_, owner, key)
               : Outcome::Failed;
}

FLASHMEM core::state::sequencer::SequencerPreparedFullBankEditResult
SequencerHistoryDomainServices::applyPreparedProjectScaleChoice(
    core::state::sequencer::SequencerPreparedFullBankEditOwner owner,
    uint8_t row,
    int choiceIndex
) const {
    if (operations_->applyPreparedProjectScaleChoice == nullptr) return {};
    return operations_->applyPreparedProjectScaleChoice(
        context_, owner, row, choiceIndex);
}

}  // namespace core::handler
