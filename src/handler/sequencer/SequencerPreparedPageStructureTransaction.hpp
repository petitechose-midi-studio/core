#pragma once

#include <cstdint>

#include "handler/sequencer/SequencerHistoryDomainServices.hpp"

namespace core::state::sequencer {
struct SequencerState;
}

namespace core::handler {

// Stable one-shot keys for the complete Page/Step structure command surface.
// Keep the declaration order synchronized with the frozen Page inventory.
enum class SequencerPreparedPageStructureAction : uint8_t {
    PageCreate = 0,
    PageSelectionPaste,
    PageClear,
    PageDelete,
    PagePaste,
    StepPaste,
    FocusedStepReset,
    StepSelectionReset,
    PageSelectionReset,
    PageSelectionDeleteOrDeepReset,
};

enum class SequencerPreparedPageStructureResult : uint8_t {
    Failed = 0,
    NoChange,
    Committed,
};

enum class SequencerPreparedPageStructureMutationOutcome : uint8_t {
    Failed = 0,
    NoChange,
    Changed,
};

using SequencerPreparedPageStructureMutationFn =
    SequencerPreparedPageStructureMutationOutcome (*)(
        void* context,
        core::state::sequencer::SequencerState& sequencer,
        const SequencerHistoryDomainServices& history) noexcept;
using SequencerPreparedPageStructureRevalidateFn = bool (*)(
    const void* context,
    const core::state::sequencer::SequencerState& sequencer) noexcept;
using SequencerPreparedPageStructureFinalizeFn = void (*)(
    void* context,
    core::state::sequencer::SequencerState& sequencer) noexcept;

struct SequencerPreparedPageStructureExecution {
    core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan =
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly;
    SequencerPreparedPageStructureAction action =
        SequencerPreparedPageStructureAction::PageCreate;
    uint8_t expectedTrack =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    int32_t beforePageCount = 0;
    int32_t afterPageCount = 0;
    bool compactGraphOnSeal = false;
    void* mutationContext = nullptr;
    SequencerPreparedPageStructureRevalidateFn revalidate = nullptr;
    SequencerPreparedPageStructureMutationFn mutate = nullptr;
    // Optional allocation-free publication hook. It runs only after the
    // prepared owner has returned CommitOutcome::Committed and is closed.
    SequencerPreparedPageStructureFinalizeFn finalizeCommitted = nullptr;
};

/**
 * Cold, one-shot owner for one prepared Page Structure Pattern transaction.
 *
 * openBoundary() rejects an active Step Content Draft before closing the
 * previous Pattern boundary. execute() then owns the fresh Started owner,
 * revalidation, mutation, seal and commit in one synchronous frame. Once
 * armed, every exit closes the owner; the destructor is the release-safe last
 * line of defence for an internal early return.
 *
 * The object owns no History, Graph, CC or SequencerState payload. Its only
 * state is one SequencerState reference, a copied two-pointer facade and
 * bounded transaction metadata.
 */
class SequencerPreparedPageStructureTransaction final {
public:
    SequencerPreparedPageStructureTransaction(
        core::state::sequencer::SequencerState& sequencer,
        SequencerHistoryDomainServices history,
        SequencerPreparedPageStructureAction action
    );
    ~SequencerPreparedPageStructureTransaction();

    SequencerPreparedPageStructureTransaction(
        const SequencerPreparedPageStructureTransaction&) = delete;
    SequencerPreparedPageStructureTransaction& operator=(
        const SequencerPreparedPageStructureTransaction&) = delete;
    SequencerPreparedPageStructureTransaction(
        SequencerPreparedPageStructureTransaction&&) = delete;
    SequencerPreparedPageStructureTransaction& operator=(
        SequencerPreparedPageStructureTransaction&&) = delete;

    // One effective Page Pattern boundary: Draft rejection first, then the
    // generic/coalesced Pattern commit. This method is intentionally one-shot.
    [[nodiscard]] bool openBoundary();

    // Owns begin -> Track/owner revalidation -> allocation-free mutation ->
    // seal -> commit -> optional allocation-free committed finalization in one
    // frame. Page counts describe the Pattern root.
    [[nodiscard]] SequencerPreparedPageStructureResult execute(
        const SequencerPreparedPageStructureExecution& execution);

private:
    enum class Phase : uint8_t {
        Initial = 0,
        BoundaryOpen,
        Armed,
        Sealed,
        Closed,
    };

    [[nodiscard]] bool ownsPending() const;
    [[nodiscard]] bool begin(
        const SequencerPreparedPageStructureExecution& execution);
    [[nodiscard]] SequencerPreparedPageStructureResult abort();
    [[nodiscard]] SequencerPreparedPageStructureResult sealAndCommit(
        bool mutationChanged);
    void abortOwned();
    void closeProtocolMisuse();

    core::state::sequencer::SequencerState& sequencer_;
    SequencerHistoryDomainServices history_;
    core::state::sequencer::SequencerHistoryDescriptor descriptor_{};
    SequencerPreparedPageStructureAction action_;
    Phase phase_ = Phase::Initial;
};

static_assert(
    sizeof(void*) != 4U ||
        sizeof(SequencerPreparedPageStructureTransaction) <= 32U,
    "Page Structure transaction must remain a bounded scalar ARM owner"
);
static_assert(
    sizeof(SequencerPreparedPageStructureTransaction) <= 48U,
    "Page Structure transaction must remain scalar/non-owning on native hosts"
);
static_assert(
    sizeof(void*) != 4U ||
        sizeof(SequencerPreparedPageStructureExecution) <= 32U,
    "Page Structure execution must remain bounded on ARM"
);
static_assert(
    sizeof(SequencerPreparedPageStructureExecution) <= 48U,
    "Page Structure execution must remain a bounded scalar plan"
);

}  // namespace core::handler
