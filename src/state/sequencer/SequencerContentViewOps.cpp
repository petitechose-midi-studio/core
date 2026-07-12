#include "state/sequencer/SequencerContentViewOps.hpp"

#include <algorithm>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerContentViewInternal.hpp"

namespace core::state::sequencer {
using namespace content_view_internal;

namespace {

FLASHMEM core::state::SequencerStepContentClipboardKind clipboardKindForChild(
    StepContentChildKind childKind
) {
    return childKind == StepContentChildKind::MICRO_SEQUENCE
        ? core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE
        : core::state::SequencerStepContentClipboardKind::CYCLE_STATES;
}

}  // namespace

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

FLASHMEM StepContentOpenResult openOrCreateActiveContentChild(
    SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind,
    uint8_t length
) {
    StepContentOpenResult result{
        .childKind = childKind,
    };
    const auto availability = activeContentChildCreationAvailability(
        sequencer,
        step,
        childKind,
        length
    );
    result.blockedReason = availability.blockedReason;
    if (!availability.canCreateOrOpen) {
        return result;
    }

    const auto ownerNodeId = activeContentStepNodeId(sequencer, step);
    if (ownerNodeId == kInvalidId) {
        result.blockedReason = StepContentCreationBlockReason::INVALID_FOCUSED_STEP;
        return result;
    }

    const uint32_t graphRevisionBefore = sequencer.pattern.graphRevision.get();
    const auto created = childKind == StepContentChildKind::MICRO_SEQUENCE
        ? createMicroSequence(sequencer.pattern, ownerNodeId, length)
        : createCycleStateSet(sequencer.pattern, ownerNodeId, length);
    if (!created.ok) {
        result.blockedReason = created.limitReached
            ? StepContentCreationBlockReason::GRAPH_LIMIT_REACHED
            : StepContentCreationBlockReason::INACTIVE_CONTEXT;
        return result;
    }

    const bool opened = childKind == StepContentChildKind::MICRO_SEQUENCE
        ? enterMicroSequenceContentView(sequencer, ownerNodeId, created.id)
        : enterCycleStatesContentView(sequencer, ownerNodeId, created.id);
    if (!opened) {
        result.blockedReason = StepContentCreationBlockReason::INVALID_FOCUSED_STEP;
        return result;
    }

    result.opened = true;
    result.created = sequencer.pattern.graphRevision.get() != graphRevisionBefore;
    result.blockedReason = StepContentCreationBlockReason::NONE;
    result.ownerNodeId = ownerNodeId;
    result.contentId = created.id;
    return result;
}

FLASHMEM bool activeContentStepCanReceiveChildContent(
    const SequencerState& sequencer,
    uint8_t step
) {
    return activeContentDepth(sequencer) < GraphLimits::MAX_DEPTH - 1U &&
           activeContentStepNodeId(sequencer, step) != kInvalidId;
}

FLASHMEM bool activeContentStepHasChildContent(
    const SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind
) {
    if (step >= activeContentLength(sequencer)) return false;

    const auto nodeId = activeContentStepNodeId(sequencer, step);
    if (nodeId == kInvalidId) return false;

    return childKind == StepContentChildKind::MICRO_SEQUENCE
        ? stepNodeHasMicroSequence(sequencer.pattern, nodeId)
        : stepNodeHasCycleStateSet(sequencer.pattern, nodeId);
}

FLASHMEM bool clipboardCanPasteActiveContentChild(
    const core::state::StructureClipboardState& clipboard,
    StepContentChildKind childKind
) {
    return clipboard.hasSequencerStepContent(clipboardKindForChild(childKind));
}

FLASHMEM bool copyActiveContentChildToClipboard(
    const SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind,
    core::state::StructureClipboardState& clipboard
) {
    if (!activeContentStepHasChildContent(sequencer, step, childKind)) return false;

    const auto* graph = graphView(sequencer.pattern);
    if (graph == nullptr) return false;

    const auto nodeId = activeContentStepNodeId(sequencer, step);
    if (nodeId == kInvalidId) return false;

    return clipboard.storeSequencerStepContent(
        *graph,
        nodeId,
        clipboardKindForChild(childKind)
    );
}

namespace {

FLASHMEM void settleGraphMutation(SequencerState& sequencer) {
    if (compactSequencerGraph(sequencer)) return;
    refreshContentView(sequencer);
    sequencer.contentView.bump();
}

}  // namespace

FLASHMEM bool clearActiveContentChild(
    SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind
) {
    if (!activeContentStepHasChildContent(sequencer, step, childKind)) return false;

    const auto nodeId = activeContentStepNodeId(sequencer, step);
    if (nodeId == kInvalidId) return false;

    const bool changed = childKind == StepContentChildKind::MICRO_SEQUENCE
        ? clearNodeChildSequence(sequencer.pattern, nodeId)
        : clearNodeCycleStateSet(sequencer.pattern, nodeId);
    if (!changed) return false;

    settleGraphMutation(sequencer);
    return true;
}

FLASHMEM bool pasteActiveContentChildFromClipboard(
    SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind,
    const core::state::StructureClipboardState& clipboard
) {
    if (!activeContentStepCanReceiveChildContent(sequencer, step)) return false;
    if (!clipboardCanPasteActiveContentChild(clipboard, childKind)) return false;
    if (!clipboard.sequencerGraph) return false;

    const auto nodeId = activeContentStepNodeId(sequencer, step);
    if (nodeId == kInvalidId) return false;

    const bool changed = childKind == StepContentChildKind::MICRO_SEQUENCE
        ? copyNodeChildSequenceFromGraph(
              sequencer.pattern,
              nodeId,
              *clipboard.sequencerGraph,
              clipboard.sequencerStepContentNodeId
          )
        : copyNodeCycleStateSetFromGraph(
              sequencer.pattern,
              nodeId,
              *clipboard.sequencerGraph,
              clipboard.sequencerStepContentNodeId
          );
    if (!changed) return false;

    settleGraphMutation(sequencer);
    return true;
}

FLASHMEM bool copyActiveContentChildrenToClipboard(
    const SequencerState& sequencer,
    uint8_t step,
    core::state::StructureClipboardState& clipboard
) {
    const auto* graph = graphView(sequencer.pattern);
    const auto nodeId = activeContentStepNodeId(sequencer, step);
    if (graph == nullptr ||
        nodeId == kInvalidId ||
        !stepNodeHasAnyChildContent(sequencer.pattern, nodeId)) {
        return false;
    }
    return clipboard.storeSequencerStepContent(
        *graph,
        nodeId,
        core::state::SequencerStepContentClipboardKind::ALL
    );
}

FLASHMEM bool clearActiveContentChildren(SequencerState& sequencer, uint8_t step) {
    const auto nodeId = activeContentStepNodeId(sequencer, step);
    if (nodeId == kInvalidId ||
        !stepNodeHasAnyChildContent(sequencer.pattern, nodeId) ||
        !clearNodeChildren(sequencer.pattern, nodeId)) {
        return false;
    }

    settleGraphMutation(sequencer);
    return true;
}

FLASHMEM bool pasteActiveContentChildrenFromClipboard(
    SequencerState& sequencer,
    uint8_t step,
    const core::state::StructureClipboardState& clipboard
) {
    if (!activeContentStepCanReceiveChildContent(sequencer, step) ||
        !clipboard.hasSequencerStepContent(
            core::state::SequencerStepContentClipboardKind::ALL
        ) ||
        !clipboard.sequencerGraph) {
        return false;
    }

    const auto nodeId = activeContentStepNodeId(sequencer, step);
    if (nodeId == kInvalidId ||
        !copyNodeChildrenFromGraph(
            sequencer.pattern,
            nodeId,
            *clipboard.sequencerGraph,
            clipboard.sequencerStepContentNodeId
        )) {
        return false;
    }

    settleGraphMutation(sequencer);
    return true;
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

FLASHMEM bool compactSequencerGraph(SequencerState& sequencer) {
    SequencerGraphCompactionRemap remap;
    const auto result = compactGraph(sequencer.pattern, &remap);
    if (!result.ok || !result.compacted) {
        return false;
    }

    auto& view = sequencer.contentView;
    if (view.stackDepth > view.frames.size()) {
        view.reset();
        return true;
    }

    uint8_t validDepth = view.stackDepth;
    for (uint8_t i = 0; i < validDepth; ++i) {
        auto& frame = view.frames[i];
        const uint16_t ownerNodeId = remap.stepNode(frame.ownerNodeId);
        if (ownerNodeId == kInvalidId) {
            validDepth = i;
            break;
        }
        frame.ownerNodeId = ownerNodeId;

        if (frame.kind == SequencerContentViewKind::MICRO_SEQUENCE) {
            const uint16_t sequenceId = remap.sequence(frame.sequenceId);
            if (sequenceId == kInvalidId) {
                validDepth = i;
                break;
            }
            frame.sequenceId = sequenceId;
        } else if (frame.kind == SequencerContentViewKind::CYCLE_STATES) {
            const uint16_t cycleSetId = remap.cycleSet(frame.cycleSetId);
            if (cycleSetId == kInvalidId) {
                validDepth = i;
                break;
            }
            frame.cycleSetId = cycleSetId;
        } else {
            validDepth = i;
            break;
        }
    }

    for (uint8_t i = validDepth; i < view.stackDepth && i < view.frames.size(); ++i) {
        view.frames[i] = {};
    }
    view.stackDepth = validDepth;
    refreshContentView(sequencer);
    view.bump();
    return true;
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
    if (page >= SequencerState::PAGE_COUNT) return false;

    const uint16_t step =
        static_cast<uint16_t>(page) * SequencerState::STEPS_PER_PAGE +
        indexInPage;
    if (step >= activeContentLength(sequencer)) return false;
    outStep = static_cast<uint8_t>(step);
    return true;
}

FLASHMEM bool activeContentStepInPattern(const SequencerState& sequencer, uint8_t step) {
    return step < activeContentLength(sequencer);
}

}  // namespace core::state::sequencer
