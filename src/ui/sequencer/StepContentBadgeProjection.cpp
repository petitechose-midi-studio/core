#include "ui/sequencer/StepContentBadgeProjection.hpp"

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"

namespace core::ui::sequencer::grid {
namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;

}  // namespace

FLASHMEM StepContentBadgeProjection buildStepContentBadgeProjection(
    const core::state::sequencer::SequencerPatternState& pattern,
    uint8_t absoluteStep
) {
    const auto rootNodeId = core::state::sequencer::rootStepNodeId(absoluteStep);
    return buildStepContentBadgeProjectionForNode(pattern, rootNodeId);
}

FLASHMEM StepContentBadgeProjection buildStepContentBadgeProjectionForNode(
    const core::state::sequencer::SequencerPatternState& pattern,
    core::state::sequencer::SequencerGraphNodeId nodeId
) {
    StepContentBadgeProjection badges;
    const auto* graph = core::state::sequencer::graphView(pattern);
    if (graph == nullptr) return badges;

    const auto* node = graph->stepNode(nodeId);
    if (node == nullptr) return badges;

    badges.microSequence =
        node->has(STEP_NODE_CHILD_SEQUENCE) &&
        graph->sequence(node->childSequenceId) != nullptr;
    badges.cycleStates =
        node->has(STEP_NODE_CYCLE_SET) &&
        graph->cycleSet(node->cycleSetId) != nullptr;
    return badges;
}

}  // namespace core::ui::sequencer::grid
