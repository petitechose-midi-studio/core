#include "state/sequencer/SequencerStepContentEditSession.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {
namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::StepSequencerGraphLimits;

constexpr uint16_t kInvalidId = StepSequencerGraphLimits::INVALID_ID;

bool rootStepInRange(uint8_t rootStep) {
    return rootStep < SequencerPatternState::MAX_STEPS;
}

const oc::note::sequencer::StepSequencerStepNode* rootStepNode(
    const SequencerPatternState& pattern,
    uint8_t rootStep
) {
    const auto* graph = graphView(pattern);
    if (graph == nullptr || !rootStepInRange(rootStep)) return nullptr;
    return graph->stepNode(rootStepNodeId(rootStep));
}

StepContentChildKind childKindForContext(StepContentContextKind kind) {
    return kind == StepContentContextKind::CYCLE_STATES
        ? StepContentChildKind::CYCLE_STATES
        : StepContentChildKind::MICRO_SEQUENCE;
}

}  // namespace

FLASHMEM void SequencerStepContentEditSession::reset() {
    active_ = false;
    stackDepth_ = 0;
    stack_ = {};
}

FLASHMEM bool SequencerStepContentEditSession::openRootStepContext(uint8_t rootStep) {
    if (!rootStepInRange(rootStep)) return false;

    reset();
    active_ = true;
    stackDepth_ = 0;
    stack_[0] = Context{
        .kind = StepContentContextKind::ROOT_STEP,
        .rootStep = rootStep,
        .localIndex = 0,
        .length = 1,
        .depth = 0,
        .nodeId = rootStepNodeId(rootStep),
        .parentNodeId = kInvalidId,
        .sequenceId = kInvalidId,
        .cycleSetId = kInvalidId,
    };
    return true;
}

FLASHMEM bool SequencerStepContentEditSession::focusLocalStep(uint8_t index) {
    auto* context = currentContext_();
    if (context == nullptr) return false;
    if (context->length == 0 || index >= context->length) return false;

    context->localIndex = index;
    return true;
}

FLASHMEM bool SequencerStepContentEditSession::leaveChildContext() {
    if (!active_ || stackDepth_ == 0) return false;
    stack_[stackDepth_] = {};
    --stackDepth_;
    return true;
}

FLASHMEM bool SequencerStepContentEditSession::maxDepthReached() const {
    if (!active_) return false;
    return stackDepth_ >= StepSequencerGraphLimits::MAX_DEPTH;
}

FLASHMEM StepContentContextView SequencerStepContentEditSession::current() const {
    const auto* context = currentContext_();
    if (context == nullptr) return {};

    return StepContentContextView{
        .kind = context->kind,
        .rootStep = context->rootStep,
        .localIndex = context->localIndex,
        .length = context->length,
        .depth = context->depth,
        .active = true,
    };
}

FLASHMEM StepContentEditResult SequencerStepContentEditSession::createOrOpenMicroSequence(
    SequencerPatternState& pattern,
    uint8_t length
) {
    if (!active_ || maxDepthReached()) {
        return {.ok = false, .limitReached = true};
    }

    const auto parentNodeId = focusedNodeId_(pattern);
    if (parentNodeId == kInvalidId) {
        return {.ok = false, .limitReached = true};
    }

    const bool openedExisting = [&]() {
        const auto* graph = graphView(pattern);
        const auto* parent = graph ? graph->stepNode(parentNodeId) : nullptr;
        return parent != nullptr && parent->has(STEP_NODE_CHILD_SEQUENCE);
    }();

    const auto result = createMicroSequence(pattern, parentNodeId, length);
    if (!result.ok) {
        return {.ok = false, .limitReached = result.limitReached};
    }

    const auto* graph = graphView(pattern);
    const auto* sequence = graph ? graph->sequence(result.id) : nullptr;
    if (sequence == nullptr) {
        return {.ok = false, .limitReached = true};
    }

    ++stackDepth_;
    stack_[stackDepth_] = Context{
        .kind = StepContentContextKind::MICRO_SEQUENCE,
        .rootStep = stack_[0].rootStep,
        .localIndex = 0,
        .length = sequence->length,
        .depth = stackDepth_,
        .nodeId = sequence->firstStepNode,
        .parentNodeId = parentNodeId,
        .sequenceId = result.id,
        .cycleSetId = kInvalidId,
    };
    return {.ok = true, .limitReached = false, .openedExisting = openedExisting};
}

FLASHMEM StepContentEditResult SequencerStepContentEditSession::createOrOpenCycleStates(
    SequencerPatternState& pattern,
    uint8_t length
) {
    if (!active_ || maxDepthReached()) {
        return {.ok = false, .limitReached = true};
    }

    const auto parentNodeId = focusedNodeId_(pattern);
    if (parentNodeId == kInvalidId) {
        return {.ok = false, .limitReached = true};
    }

    const bool openedExisting = [&]() {
        const auto* graph = graphView(pattern);
        const auto* parent = graph ? graph->stepNode(parentNodeId) : nullptr;
        return parent != nullptr && parent->has(STEP_NODE_CYCLE_SET);
    }();

    const auto result = createCycleStateSet(pattern, parentNodeId, length);
    if (!result.ok) {
        return {.ok = false, .limitReached = result.limitReached};
    }

    const auto* graph = graphView(pattern);
    const auto* cycleSet = graph ? graph->cycleSet(result.id) : nullptr;
    if (cycleSet == nullptr) {
        return {.ok = false, .limitReached = true};
    }

    ++stackDepth_;
    stack_[stackDepth_] = Context{
        .kind = StepContentContextKind::CYCLE_STATES,
        .rootStep = stack_[0].rootStep,
        .localIndex = 0,
        .length = cycleSet->length,
        .depth = stackDepth_,
        .nodeId = cycleSet->firstStateNode,
        .parentNodeId = parentNodeId,
        .sequenceId = kInvalidId,
        .cycleSetId = result.id,
    };
    return {.ok = true, .limitReached = false, .openedExisting = openedExisting};
}

FLASHMEM bool SequencerStepContentEditSession::removeFocusedChild(
    SequencerPatternState& pattern,
    StepContentChildKind childKind
) {
    if (!active_) return false;
    const auto nodeId = focusedNodeId_(pattern);
    if (nodeId == kInvalidId) return false;

    return childKind == StepContentChildKind::MICRO_SEQUENCE
        ? clearNodeChildSequence(pattern, nodeId)
        : clearNodeCycleStateSet(pattern, nodeId);
}

FLASHMEM bool SequencerStepContentEditSession::removeCurrentChildContext(
    SequencerPatternState& pattern
) {
    auto* context = currentContext_();
    if (context == nullptr || context->kind == StepContentContextKind::ROOT_STEP) {
        return false;
    }

    const auto parentNodeId = context->parentNodeId;
    const auto childKind = childKindForContext(context->kind);
    const bool removed = childKind == StepContentChildKind::MICRO_SEQUENCE
        ? clearNodeChildSequence(pattern, parentNodeId)
        : clearNodeCycleStateSet(pattern, parentNodeId);
    if (removed) {
        leaveChildContext();
    }
    return removed;
}

FLASHMEM const SequencerStepContentEditSession::Context*
SequencerStepContentEditSession::currentContext_() const {
    if (!active_ || stackDepth_ >= stack_.size()) return nullptr;
    return &stack_[stackDepth_];
}

FLASHMEM SequencerStepContentEditSession::Context*
SequencerStepContentEditSession::currentContext_() {
    if (!active_ || stackDepth_ >= stack_.size()) return nullptr;
    return &stack_[stackDepth_];
}

FLASHMEM SequencerGraphNodeId SequencerStepContentEditSession::focusedNodeId_(
    const SequencerPatternState& pattern
) const {
    const auto* context = currentContext_();
    if (context == nullptr) return kInvalidId;

    if (context->kind == StepContentContextKind::ROOT_STEP) {
        return context->nodeId;
    }

    const auto* graph = graphView(pattern);
    if (graph == nullptr || context->localIndex >= context->length) return kInvalidId;

    if (context->kind == StepContentContextKind::MICRO_SEQUENCE) {
        const auto* sequence = graph->sequence(context->sequenceId);
        if (sequence == nullptr || context->localIndex >= sequence->length) return kInvalidId;
        return static_cast<uint16_t>(sequence->firstStepNode + context->localIndex);
    }

    const auto* cycleSet = graph->cycleSet(context->cycleSetId);
    if (cycleSet == nullptr || context->localIndex >= cycleSet->length) return kInvalidId;
    return static_cast<uint16_t>(cycleSet->firstStateNode + context->localIndex);
}

FLASHMEM bool stepHasMicroSequence(const SequencerPatternState& pattern, uint8_t rootStep) {
    const auto* node = rootStepNode(pattern, rootStep);
    return node != nullptr && node->has(STEP_NODE_CHILD_SEQUENCE);
}

FLASHMEM bool stepHasCycleStates(const SequencerPatternState& pattern, uint8_t rootStep) {
    const auto* node = rootStepNode(pattern, rootStep);
    return node != nullptr && node->has(STEP_NODE_CYCLE_SET);
}

}  // namespace core::state::sequencer
