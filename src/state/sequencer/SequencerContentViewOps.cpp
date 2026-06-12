#include "state/sequencer/SequencerContentViewOps.hpp"

#include <algorithm>

#include "state/sequencer/SequencerContentViewInternal.hpp"

namespace core::state::sequencer {
using namespace content_view_internal;

FLASHMEM bool isRootContentView(const SequencerState& sequencer) {
    return !sequencer.contentView.isChildContent();
}

FLASHMEM bool isChildContentView(const SequencerState& sequencer) {
    return sequencer.contentView.isChildContent();
}

FLASHMEM bool isMicroSequenceContentView(const SequencerState& sequencer) {
    return sequencer.contentView.isMicroSequence();
}

FLASHMEM bool isCycleStatesContentView(const SequencerState& sequencer) {
    return sequencer.contentView.isCycleStates();
}

FLASHMEM uint8_t activeContentDepth(const SequencerState& sequencer) {
    return sequencer.contentView.depth.get();
}

FLASHMEM SequencerGraphNodeId activeContentStepNodeId(
    const SequencerState& sequencer,
    uint8_t step
) {
    if (isRootContentView(sequencer)) {
        return rootStepNodeId(step);
    }

    const auto* frame = sequencer.contentView.currentFrame();
    if (frame == nullptr) return kInvalidId;
    return stepNodeIdForFrame(sequencer, *frame, step);
}

FLASHMEM StepContentCreationAvailability activeContentChildCreationAvailability(
    const SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind,
    uint8_t length
) {
    if (step >= activeContentLength(sequencer)) {
        return {
            .canCreateOrOpen = false,
            .opensExisting = false,
            .blockedReason = StepContentCreationBlockReason::INVALID_FOCUSED_STEP,
        };
    }
    if (activeContentDepth(sequencer) >= GraphLimits::MAX_DEPTH - 1U) {
        return {
            .canCreateOrOpen = false,
            .opensExisting = false,
            .blockedReason = StepContentCreationBlockReason::MAX_DEPTH_REACHED,
        };
    }

    const auto nodeId = activeContentStepNodeId(sequencer, step);
    const auto* graph = graphView(sequencer.pattern);
    const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
    if (nodeId == kInvalidId) {
        return {
            .canCreateOrOpen = false,
            .opensExisting = false,
            .blockedReason = StepContentCreationBlockReason::INVALID_FOCUSED_STEP,
        };
    }
    if (graph != nullptr && node != nullptr) {
        const bool opensExisting = childKind == StepContentChildKind::MICRO_SEQUENCE
            ? nodeHasMicroSequence(*graph, *node)
            : nodeHasCycleStates(*graph, *node);
        if (opensExisting) {
            return {
                .canCreateOrOpen = true,
                .opensExisting = true,
                .blockedReason = StepContentCreationBlockReason::NONE,
            };
        }
    }

    const uint8_t reservedStepNodes = childKind == StepContentChildKind::MICRO_SEQUENCE
        ? MICRO_LENGTH_MAX
        : length;
    const uint16_t currentStepNodeCount =
        graph ? graph->stepNodeCount : SequencerPatternState::MAX_STEPS;
    if (reservedStepNodes == 0 ||
        static_cast<uint32_t>(currentStepNodeCount) + reservedStepNodes >
            GraphLimits::MAX_STEP_NODES) {
        return {
            .canCreateOrOpen = false,
            .opensExisting = false,
            .blockedReason = StepContentCreationBlockReason::GRAPH_LIMIT_REACHED,
        };
    }

    if (childKind == StepContentChildKind::MICRO_SEQUENCE) {
        const uint16_t sequenceCount = graph ? graph->sequenceCount : 1U;
        if (sequenceCount >= GraphLimits::MAX_SEQUENCES ||
            length < MICRO_LENGTH_MIN ||
            length > MICRO_LENGTH_MAX) {
            return {
                .canCreateOrOpen = false,
                .opensExisting = false,
                .blockedReason = StepContentCreationBlockReason::GRAPH_LIMIT_REACHED,
            };
        }
    } else {
        const uint16_t cycleSetCount = graph ? graph->cycleSetCount : 0U;
        if (cycleSetCount >= GraphLimits::MAX_CYCLE_SETS ||
            length < CYCLE_STATE_LENGTH_MIN ||
            length > CYCLE_STATE_LENGTH_MAX) {
            return {
                .canCreateOrOpen = false,
                .opensExisting = false,
                .blockedReason = StepContentCreationBlockReason::GRAPH_LIMIT_REACHED,
            };
        }
    }

    return {
        .canCreateOrOpen = true,
        .opensExisting = false,
        .blockedReason = StepContentCreationBlockReason::NONE,
    };
}

FLASHMEM bool activeContentStepCanReceiveChildContent(
    const SequencerState& sequencer,
    uint8_t step
) {
    return activeContentDepth(sequencer) < GraphLimits::MAX_DEPTH - 1U &&
           activeContentStepNodeId(sequencer, step) != kInvalidId;
}

FLASHMEM bool enterMicroSequenceContentView(
    SequencerState& sequencer,
    uint8_t parentStep,
    SequencerGraphSequenceId sequenceId
) {
    return enterMicroSequenceContentView(sequencer, rootStepNodeId(parentStep), sequenceId);
}

FLASHMEM bool enterMicroSequenceContentView(
    SequencerState& sequencer,
    SequencerGraphNodeId ownerNodeId,
    SequencerGraphSequenceId sequenceId
) {
    const auto* graph = graphView(sequencer.pattern);
    const auto* sequence = graph ? graph->sequence(sequenceId) : nullptr;
    if (graph == nullptr ||
        sequence == nullptr ||
        sequence->kind != oc::note::sequencer::StepSequencerSequenceKind::MicroSequence ||
        !ownsSequence(*graph, ownerNodeId, sequenceId)) {
        return false;
    }

    return pushFrame(
        sequencer,
        SequencerContentViewKind::MICRO_SEQUENCE,
        ownerNodeId,
        sequenceId,
        kInvalidId,
        sequence->length
    );
}

FLASHMEM bool enterCycleStatesContentView(
    SequencerState& sequencer,
    SequencerGraphNodeId ownerNodeId,
    SequencerGraphCycleSetId cycleSetId
) {
    const auto* graph = graphView(sequencer.pattern);
    const auto* cycleSet = graph ? graph->cycleSet(cycleSetId) : nullptr;
    if (graph == nullptr ||
        cycleSet == nullptr ||
        !ownsCycleSet(*graph, ownerNodeId, cycleSetId)) {
        return false;
    }

    return pushFrame(
        sequencer,
        SequencerContentViewKind::CYCLE_STATES,
        ownerNodeId,
        kInvalidId,
        cycleSetId,
        cycleSet->length
    );
}

FLASHMEM bool leaveContentView(SequencerState& sequencer) {
    auto& view = sequencer.contentView;
    if (view.stackDepth == 0) return false;

    const auto frame = view.frames[view.stackDepth - 1U];
    view.frames[view.stackDepth - 1U] = {};
    --view.stackDepth;
    syncPublicViewFields(view);

    sequencer.page.set(frame.pageSnapshot);
    const uint8_t length = activeContentLength(sequencer);
    const uint8_t focused = length > 0
        ? std::min<uint8_t>(frame.focusSnapshot, static_cast<uint8_t>(length - 1U))
        : 0;
    sequencer.focusedStep.set(focused);
    sequencer.page.set(normalizeActiveContentPage(sequencer, sequencer.page.get()));
    view.bump();
    return true;
}

FLASHMEM void refreshContentView(SequencerState& sequencer) {
    auto& view = sequencer.contentView;
    if (view.stackDepth == 0) {
        syncPublicViewFields(view);
        return;
    }

    while (view.stackDepth > 0) {
        auto& frame = view.frames[view.stackDepth - 1U];
        if (validateFrame(sequencer, frame)) break;
        view.frames[view.stackDepth - 1U] = {};
        --view.stackDepth;
    }
    syncPublicViewFields(view);

    const uint8_t length = activeContentLength(sequencer);
    if (length == 0) {
        sequencer.page.set(0);
        sequencer.focusedStep.set(0);
        return;
    }
    sequencer.page.set(normalizeActiveContentPage(sequencer, sequencer.page.get()));
    if (sequencer.focusedStep.get() >= length) {
        sequencer.focusedStep.set(static_cast<uint8_t>(length - 1U));
    }
}

FLASHMEM uint8_t activeContentLength(const SequencerState& sequencer) {
    if (isRootContentView(sequencer)) {
        return sequencer.pattern.length.get();
    }
    return sequencer.contentView.length.get();
}

FLASHMEM uint8_t activeContentPageCount(const SequencerState& sequencer) {
    const uint8_t len = activeContentLength(sequencer);
    if (len == 0) return 0;
    return static_cast<uint8_t>(
        std::min<uint16_t>(
            SequencerState::PAGE_COUNT,
            static_cast<uint16_t>((len + SequencerState::STEPS_PER_PAGE - 1U) /
                                  SequencerState::STEPS_PER_PAGE)
        )
    );
}

FLASHMEM uint8_t normalizeActiveContentPage(const SequencerState& sequencer, uint8_t page) {
    const uint8_t pages = activeContentPageCount(sequencer);
    if (pages == 0) return 0;
    return static_cast<uint8_t>(page % pages);
}

FLASHMEM uint8_t activeContentPageStartStep(const SequencerState& sequencer, uint8_t page) {
    return static_cast<uint8_t>(
        normalizeActiveContentPage(sequencer, page) * SequencerState::STEPS_PER_PAGE
    );
}

FLASHMEM uint8_t activeContentPageForStep(uint8_t step) {
    return static_cast<uint8_t>(step / SequencerState::STEPS_PER_PAGE);
}

FLASHMEM bool resolveActiveContentStepInPage(
    const SequencerState& sequencer,
    uint8_t page,
    uint8_t indexInPage,
    uint8_t& outStep
) {
    if (indexInPage >= SequencerState::STEPS_PER_PAGE) return false;
    const uint8_t pages = activeContentPageCount(sequencer);
    if (pages == 0) return false;

    const uint16_t step =
        static_cast<uint16_t>(normalizeActiveContentPage(sequencer, page)) *
            SequencerState::STEPS_PER_PAGE +
        indexInPage;
    if (step >= activeContentLength(sequencer)) return false;
    outStep = static_cast<uint8_t>(step);
    return true;
}

FLASHMEM bool activeContentStepInPattern(const SequencerState& sequencer, uint8_t step) {
    return step < activeContentLength(sequencer);
}

}  // namespace core::state::sequencer
