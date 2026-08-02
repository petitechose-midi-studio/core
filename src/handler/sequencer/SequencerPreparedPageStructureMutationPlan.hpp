#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>

#include "handler/sequencer/SequencerPreparedPageStructureTransaction.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler {

enum class StepResetDepth : uint8_t;

enum class SequencerPreparedPageStructurePreflightOutcome : uint8_t {
    Rejected = 0,
    NoChange,
    Ready,
};

enum class SequencerPreparedPageStructureContentContext : uint8_t {
    Root = 0,
    MicroSequence,
    CycleStates,
    Invalid,
};

/**
 * The exact post-reclaim capacity charge, packed to the physical Graph limits.
 * Public conversion keeps the wider overflow-safe Graph budget at API edges.
 */
struct SequencerPreparedPageStructureGraphBudget {
    uint32_t stepNodes = 0U;
    uint16_t sequences = 0U;
    uint16_t cycleSets = 0U;

    [[nodiscard]] core::state::sequencer::SequencerGraphCopyBudget expanded()
        const noexcept {
        return {
            .stepNodes = stepNodes,
            .sequences = sequences,
            .cycleSets = cycleSets,
        };
    }
};

static_assert(sizeof(SequencerPreparedPageStructureGraphBudget) == 8U);

/**
 * Allocation-free, non-owning preflight for one Page/Step structure command.
 *
 * `targetToSource` is indexed by the destination logical Step. Values 0..127
 * address the frozen clipboard payload, 254 means canonical reset, and 255
 * means untouched. This replaces the former 128-entry preview object with one
 * byte per possible target while preserving sparse placement exactly.
 */
struct SequencerPreparedPageStructureMutationPlan {
    static constexpr uint8_t TARGET_DEFAULT = 0xFEU;
    static constexpr uint8_t TARGET_UNTOUCHED = 0xFFU;

    std::array<uint8_t, core::state::sequencer::SequencerState::MAX_STEPS>
        targetToSource{};
    core::state::sequencer::SequencerPreparedGraphContentPath contentPath{};

    const core::state::StructureClipboardState* clipboard = nullptr;
    const oc::note::sequencer::StepSequencerGraph* sourceGraphIdentity = nullptr;

    uint32_t stepDataRevision = 0U;
    uint32_t graphRevision = 0U;
    uint32_t ccLaneRevision = 0U;
    uint32_t timingRevision = 0U;
    uint32_t clipboardRevision = 0U;

    SequencerPreparedPageStructureGraphBudget graphBudget{};
    uint16_t pageMask = 0U;
    SequencerPreparedPageStructureAction action =
        SequencerPreparedPageStructureAction::Invalid;
    SequencerPreparedPageStructurePreflightOutcome outcome =
        SequencerPreparedPageStructurePreflightOutcome::Rejected;
    core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan =
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly;
    uint8_t expectedTrack =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t patternLength = 0U;
    uint8_t contentLength = 0U;
    uint8_t resultingContentLength = 0U;
    uint8_t initialPage = 0U;
    uint8_t initialFocus = 0U;
    uint8_t finalFocus = 0U;
    uint8_t targetCount = 0U;
    StepResetDepth resetDepth;
    uint8_t flags = 0U;

    [[nodiscard]] bool ready() const noexcept {
        return outcome ==
               SequencerPreparedPageStructurePreflightOutcome::Ready;
    }
    [[nodiscard]] SequencerPreparedPageStructureContentContext context()
        const noexcept;
    [[nodiscard]] uint8_t depth() const noexcept {
        return contentPath.stackDepth;
    }
    [[nodiscard]] bool compactGraphOnSeal() const noexcept;
};

static_assert(
    sizeof(SequencerPreparedPageStructureMutationPlan) <= 256U,
    "Page Structure preflight must remain within its 256-byte stack contract"
);
static_assert(
    sizeof(void*) != 4U ||
        sizeof(SequencerPreparedPageStructureMutationPlan) <= 232U,
    "ARM Page Structure plan growth would break the 256-byte helper frame"
);

enum class SequencerPreparedPageStructureTarget : uint16_t {};

static_assert(
    sizeof(SequencerPreparedPageStructureTarget) == 2U,
    "Page Structure target must remain register-sized on ARM"
);

[[nodiscard]] constexpr SequencerPreparedPageStructureTarget
makeSequencerPreparedPageStructureTarget(
    uint8_t expectedTrack,
    uint8_t page
) noexcept {
    return static_cast<SequencerPreparedPageStructureTarget>(
        static_cast<uint16_t>(expectedTrack) |
        (static_cast<uint16_t>(page) << 8U));
}

[[nodiscard]] constexpr uint8_t sequencerPreparedPageStructureTargetTrack(
    SequencerPreparedPageStructureTarget target
) noexcept {
    return static_cast<uint8_t>(static_cast<uint16_t>(target));
}

[[nodiscard]] constexpr uint8_t sequencerPreparedPageStructureTargetPage(
    SequencerPreparedPageStructureTarget target
) noexcept {
    return static_cast<uint8_t>(static_cast<uint16_t>(target) >> 8U);
}

enum class SequencerPreparedStepPasteTarget : uint32_t {};

static_assert(
    sizeof(SequencerPreparedStepPasteTarget) == 4U,
    "Step paste target must remain register-sized on ARM"
);

[[nodiscard]] constexpr SequencerPreparedStepPasteTarget
makeSequencerPreparedStepPasteTarget(
    uint8_t expectedTrack,
    core::state::project::ProjectStepPasteMode mode,
    uint8_t cursorStep
) noexcept {
    return static_cast<SequencerPreparedStepPasteTarget>(
        static_cast<uint32_t>(expectedTrack) |
        (static_cast<uint32_t>(mode) << 8U) |
        (static_cast<uint32_t>(cursorStep) << 16U));
}

[[nodiscard]] constexpr uint8_t sequencerPreparedStepPasteTargetTrack(
    SequencerPreparedStepPasteTarget target
) noexcept {
    return static_cast<uint8_t>(static_cast<uint32_t>(target));
}

[[nodiscard]] constexpr core::state::project::ProjectStepPasteMode
sequencerPreparedStepPasteTargetMode(
    SequencerPreparedStepPasteTarget target
) noexcept {
    return static_cast<core::state::project::ProjectStepPasteMode>(
        static_cast<uint8_t>(static_cast<uint32_t>(target) >> 8U));
}

[[nodiscard]] constexpr uint8_t sequencerPreparedStepPasteTargetCursor(
    SequencerPreparedStepPasteTarget target
) noexcept {
    return static_cast<uint8_t>(static_cast<uint32_t>(target) >> 16U);
}

enum class SequencerPreparedFocusedStepResetTarget : uint32_t {};

static_assert(
    sizeof(SequencerPreparedFocusedStepResetTarget) == 4U,
    "focused Step reset target must remain register-sized on ARM"
);

[[nodiscard]] constexpr SequencerPreparedFocusedStepResetTarget
makeSequencerPreparedFocusedStepResetTarget(
    uint8_t expectedTrack,
    uint8_t step,
    StepResetDepth depth
) noexcept {
    return static_cast<SequencerPreparedFocusedStepResetTarget>(
        static_cast<uint32_t>(expectedTrack) |
        (static_cast<uint32_t>(step) << 8U) |
        (static_cast<uint32_t>(depth) << 16U));
}

[[nodiscard]] constexpr uint8_t sequencerPreparedFocusedStepResetTargetTrack(
    SequencerPreparedFocusedStepResetTarget target
) noexcept {
    return static_cast<uint8_t>(static_cast<uint32_t>(target));
}

[[nodiscard]] constexpr uint8_t sequencerPreparedFocusedStepResetTargetStep(
    SequencerPreparedFocusedStepResetTarget target
) noexcept {
    return static_cast<uint8_t>(static_cast<uint32_t>(target) >> 8U);
}

[[nodiscard]] constexpr StepResetDepth
sequencerPreparedFocusedStepResetTargetDepth(
    SequencerPreparedFocusedStepResetTarget target
) noexcept {
    return static_cast<StepResetDepth>(
        static_cast<uint8_t>(static_cast<uint32_t>(target) >> 16U));
}

enum class SequencerPreparedStepSelectionResetTarget : uint16_t {};

static_assert(
    sizeof(SequencerPreparedStepSelectionResetTarget) == 2U,
    "Step-selection reset target must remain register-sized on ARM"
);

[[nodiscard]] constexpr SequencerPreparedStepSelectionResetTarget
makeSequencerPreparedStepSelectionResetTarget(
    uint8_t expectedTrack,
    StepResetDepth depth
) noexcept {
    return static_cast<SequencerPreparedStepSelectionResetTarget>(
        static_cast<uint16_t>(expectedTrack) |
        (static_cast<uint16_t>(depth) << 8U));
}

[[nodiscard]] constexpr uint8_t sequencerPreparedStepSelectionResetTargetTrack(
    SequencerPreparedStepSelectionResetTarget target
) noexcept {
    return static_cast<uint8_t>(static_cast<uint16_t>(target));
}

[[nodiscard]] constexpr StepResetDepth
sequencerPreparedStepSelectionResetTargetDepth(
    SequencerPreparedStepSelectionResetTarget target
) noexcept {
    return static_cast<StepResetDepth>(
        static_cast<uint8_t>(static_cast<uint16_t>(target) >> 8U));
}

[[nodiscard]] SequencerPreparedPageStructurePreflightOutcome
buildSequencerPageSelectionPasteMutationPlan(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    SequencerPreparedPageStructureTarget target,
    SequencerPreparedPageStructureMutationPlan& out
) noexcept;

[[nodiscard]] SequencerPreparedPageStructurePreflightOutcome
buildSequencerPageClearMutationPlan(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t expectedTrack,
    uint8_t page,
    SequencerPreparedPageStructureMutationPlan& out
) noexcept;

[[nodiscard]] SequencerPreparedPageStructurePreflightOutcome
buildSequencerPageDeleteMutationPlan(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t expectedTrack,
    uint8_t page,
    SequencerPreparedPageStructureMutationPlan& out
) noexcept;

[[nodiscard]] SequencerPreparedPageStructurePreflightOutcome
buildSequencerPagePasteMutationPlan(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    SequencerPreparedPageStructureTarget target,
    SequencerPreparedPageStructureMutationPlan& out
) noexcept;

[[nodiscard]] SequencerPreparedPageStructurePreflightOutcome
buildSequencerStepPasteMutationPlan(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    SequencerPreparedStepPasteTarget target,
    SequencerPreparedPageStructureMutationPlan& out
) noexcept;

[[nodiscard]] SequencerPreparedPageStructurePreflightOutcome
buildSequencerFocusedStepResetMutationPlan(
    const core::state::sequencer::SequencerState& sequencer,
    SequencerPreparedFocusedStepResetTarget target,
    SequencerPreparedPageStructureMutationPlan& out
) noexcept;

[[nodiscard]] SequencerPreparedPageStructurePreflightOutcome
buildSequencerStepSelectionResetMutationPlan(
    const core::state::sequencer::SequencerState& sequencer,
    const oc::note::sequencer::StepBitMask128& selectedMask,
    SequencerPreparedStepSelectionResetTarget target,
    SequencerPreparedPageStructureMutationPlan& out
) noexcept;

[[nodiscard]] SequencerPreparedPageStructurePreflightOutcome
buildSequencerPageSelectionResetMutationPlan(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t expectedTrack,
    uint16_t selectedPageMask,
    SequencerPreparedPageStructureMutationPlan& out
) noexcept;

[[nodiscard]] SequencerPreparedPageStructurePreflightOutcome
buildSequencerPageSelectionDeleteOrDeepResetMutationPlan(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t expectedTrack,
    uint16_t selectedPageMask,
    SequencerPreparedPageStructureMutationPlan& out
) noexcept;

/** Builds the scalar transaction adapter; `plan` must outlive execute(). */
[[nodiscard]] SequencerPreparedPageStructureExecution
makeSequencerPreparedPageStructureExecution(
    SequencerPreparedPageStructureMutationPlan& plan
) noexcept;

// Isolates the scalar Execution object from callers that already own the
// 232-byte ARM plan frame. Keep this out-of-line so no Page orchestration frame
// simultaneously materializes Plan + Transaction + Execution.
[[nodiscard]] SequencerPreparedPageStructureResult
executeSequencerPreparedPageStructureMutationPlan(
    SequencerPreparedPageStructureTransaction& transaction,
    SequencerPreparedPageStructureMutationPlan& plan
);

}  // namespace core::handler
