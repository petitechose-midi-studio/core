#pragma once

#include <cstdint>

#include <array>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/sequencer/SequencerPatternState.hpp"

namespace core::state::sequencer {

using SequencerGraphNodeId = uint16_t;
using SequencerGraphSequenceId = uint16_t;
using SequencerGraphCycleSetId = uint16_t;

enum class SequencerGraphPayloadInspectionStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    MalformedGraph,
    DepthExceeded,
    CycleDetected,
    ArithmeticOverflow,
};

/** Additional fixed-capacity storage consumed by one or more payload copies. */
struct SequencerGraphCopyBudget {
    uint32_t stepNodes = 0;
    uint32_t sequences = 0;
    uint32_t cycleSets = 0;
};

struct SequencerGraphPayloadInspection {
    SequencerGraphCopyBudget budget{};
    SequencerGraphPayloadInspectionStatus status =
        SequencerGraphPayloadInspectionStatus::InvalidArgument;
    bool payloadPresent = false;

    [[nodiscard]] bool ok() const noexcept {
        return status == SequencerGraphPayloadInspectionStatus::Ok;
    }
};

struct SequencerGraphPayloadComparison {
    SequencerGraphPayloadInspectionStatus status =
        SequencerGraphPayloadInspectionStatus::InvalidArgument;
    bool same = false;

    [[nodiscard]] bool ok() const noexcept {
        return status == SequencerGraphPayloadInspectionStatus::Ok;
    }
};

static_assert(sizeof(SequencerGraphCopyBudget) == 12U);
static_assert(sizeof(SequencerGraphPayloadInspection) <= 16U);
static_assert(sizeof(SequencerGraphPayloadComparison) <= 2U);

struct SequencerGraphCreateResult {
    bool ok = false;
    bool limitReached = false;
    uint16_t id = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
};

struct SequencerGraphCompactionRemap {
    using Limits = oc::note::sequencer::StepSequencerGraphLimits;

    std::array<uint16_t, Limits::MAX_STEP_NODES> stepNodes{};
    std::array<uint16_t, Limits::MAX_SEQUENCES> sequences{};
    std::array<uint16_t, Limits::MAX_CYCLE_SETS> cycleSets{};

    void reset();
    uint16_t stepNode(uint16_t id) const;
    uint16_t sequence(uint16_t id) const;
    uint16_t cycleSet(uint16_t id) const;
};

struct SequencerGraphCompactionResult {
    bool ok = false;
    bool compacted = false;
};

bool ensureGraphRoot(SequencerPatternState& pattern);
void clearGraph(SequencerPatternState& pattern);
[[nodiscard]] bool copyGraph(SequencerPatternState& target, const SequencerPatternState& source);
[[nodiscard]] bool copyGraph(SequencerPatternState& target,
                             const oc::note::sequencer::StepSequencerGraph* source,
                             uint32_t revision);
SequencerGraphCompactionResult compactGraph(SequencerPatternState& pattern,
                                            SequencerGraphCompactionRemap& remap);
SequencerGraphCompactionResult compactGraph(SequencerPatternState& pattern);
// Uses an owner reserved before a surrounding transaction's first live write.
// The live Graph owner is preserved; only its bytes are replaced when the
// canonical graph differs. No allocation occurs.
SequencerGraphCompactionResult compactGraphUsingReservedStorage(
    SequencerPatternState& pattern, oc::note::sequencer::StepSequencerGraph& reservedGraph,
    SequencerGraphCompactionRemap& remap);

const oc::note::sequencer::StepSequencerGraph* graphView(const SequencerPatternState& pattern);

/**
 * Complete allocation-free proof for one enabled Pattern Graph.
 *
 * The root shape, every reachable node/container, maximum depth and canonical
 * payload are validated. A sequence/cycle-set may have exactly one reachable
 * owner and active node intervals may not overlap. Detached containers are
 * intentionally accepted because compaction is their reclamation boundary.
 */
[[nodiscard]] bool validInitializedSequencerGraph(
    const oc::note::sequencer::StepSequencerGraph& graph
) noexcept;

/**
 * Validates the recursively copied payload rooted at sourceNodeId.
 *
 * The root node already exists in the destination and is therefore excluded
 * from budget.stepNodes. Each child container is charged at its fixed reserved
 * capacity, exactly as the copy primitives allocate it. targetDepth is the
 * depth of the destination root node and bounds all recursive descent.
 */
[[nodiscard]] SequencerGraphPayloadInspection inspectSequencerGraphPayload(
    const oc::note::sequencer::StepSequencerGraph& source,
    SequencerGraphNodeId sourceNodeId,
    uint8_t targetDepth
) noexcept;

/** Strong-guarantee checked addition: aggregate is unchanged on overflow. */
[[nodiscard]] bool appendSequencerGraphCopyBudget(
    SequencerGraphCopyBudget& aggregate,
    const SequencerGraphCopyBudget& addition
) noexcept;

/** Requires an enabled, initialized Pattern Graph (normally post-compaction). */
[[nodiscard]] bool sequencerGraphHasCopyCapacity(
    const oc::note::sequencer::StepSequencerGraph& target,
    const SequencerGraphCopyBudget& budget
) noexcept;

/** Exact semantic proof of StepSequencerGraph::reset(), including unused slots. */
[[nodiscard]] bool isCanonicalDisabledSequencerGraph(
    const oc::note::sequencer::StepSequencerGraph& graph
) noexcept;

/**
 * Initializes a canonical disabled owner as an empty enabled Pattern Graph.
 * The operation is allocation-free, idempotent and never touches a revision
 * signal. Non-canonical disabled owners and malformed enabled owners fail
 * without mutation.
 */
[[nodiscard]] bool initializeSequencerGraphRootUnversioned(
    oc::note::sequencer::StepSequencerGraph& graph
) noexcept;

/** Copied-node semantics: child presence is compared, physical IDs are not. */
[[nodiscard]] bool sameSequencerGraphNodePayload(
    const oc::note::sequencer::StepSequencerStepNode& lhs,
    const oc::note::sequencer::StepSequencerStepNode& rhs
) noexcept;

/** True when copying this node would produce the canonical default payload. */
[[nodiscard]] bool isDefaultSequencerGraphNodePayload(
    const oc::note::sequencer::StepSequencerStepNode& node
) noexcept;

/**
 * Compares two recursive payloads independently of their physical Graph IDs.
 * Both inputs receive the same bounded malformed/depth/cycle validation as
 * inspectSequencerGraphPayload().
 */
[[nodiscard]] SequencerGraphPayloadComparison compareSequencerGraphPayloads(
    const oc::note::sequencer::StepSequencerGraph& lhs,
    SequencerGraphNodeId lhsNodeId,
    const oc::note::sequencer::StepSequencerGraph& rhs,
    SequencerGraphNodeId rhsNodeId,
    uint8_t targetDepth
) noexcept;

SequencerGraphNodeId rootStepNodeId(uint8_t step);
bool stepNodeHasMicroSequence(const SequencerPatternState& pattern, SequencerGraphNodeId nodeId);
bool stepNodeHasCycleStateSet(const SequencerPatternState& pattern, SequencerGraphNodeId nodeId);
bool stepNodeHasAnyChildContent(const SequencerPatternState& pattern, SequencerGraphNodeId nodeId);

SequencerGraphCreateResult createMicroSequence(SequencerPatternState& pattern,
                                               SequencerGraphNodeId parentNodeId, uint8_t length);
bool resizeMicroSequence(SequencerPatternState& pattern, SequencerGraphSequenceId sequenceId,
                         uint8_t length);
bool resizeCycleStateSet(SequencerPatternState& pattern, SequencerGraphCycleSetId cycleSetId,
                         uint8_t length);
[[nodiscard]] bool resizeMicroSequenceUnversioned(
    oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphSequenceId sequenceId,
    uint8_t length
) noexcept;
[[nodiscard]] bool resizeCycleStateSetUnversioned(
    oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphCycleSetId cycleSetId,
    uint8_t length
) noexcept;
/**
 * Extends child content without changing the logical identity of old Steps.
 *
 * A child offset is interpreted modulo its current length. Growing the
 * container therefore requires a bounded in-place remap: every old logical
 * Step (including its child references) is preserved and only newly exposed
 * logical Steps become canonical disabled overrides. These primitives are
 * allocation-free and do not publish a Graph revision.
 */
[[nodiscard]] bool extendMicroSequencePreservingLogicalContentUnversioned(
    oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphSequenceId sequenceId,
    uint8_t length
) noexcept;
[[nodiscard]] bool extendCycleStateSetPreservingLogicalContentUnversioned(
    oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphCycleSetId cycleSetId,
    uint8_t length
) noexcept;
[[nodiscard]] uint8_t sequencerMicroSequenceReservedCapacity(
    const oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphSequenceId sequenceId
) noexcept;
[[nodiscard]] uint8_t sequencerCycleStateSetReservedCapacity(
    const oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphCycleSetId cycleSetId
) noexcept;
bool setMicroSequenceOffset(SequencerPatternState& pattern, SequencerGraphSequenceId sequenceId,
                            int8_t offset);
bool setCycleStateSetOffset(SequencerPatternState& pattern, SequencerGraphCycleSetId cycleSetId,
                            int8_t offset);
bool rotateRootStepNodes(SequencerPatternState& pattern, int offsetSteps);
bool rotateMicroSequenceSteps(SequencerPatternState& pattern, SequencerGraphSequenceId sequenceId,
                              int offsetSteps);
bool rotateCycleStateSetSteps(SequencerPatternState& pattern, SequencerGraphCycleSetId cycleSetId,
                              int offsetSteps);

SequencerGraphCreateResult createCycleStateSet(SequencerPatternState& pattern,
                                               SequencerGraphNodeId parentNodeId, uint8_t length);

bool clearNodeChildren(SequencerPatternState& pattern, SequencerGraphNodeId nodeId);
bool clearNodeChildSequence(SequencerPatternState& pattern, SequencerGraphNodeId nodeId);
bool clearNodeCycleStateSet(SequencerPatternState& pattern, SequencerGraphNodeId nodeId);

enum class SequencerGraphNodeResetMode : uint8_t {
    DEFAULT = 0,
    DISABLED_OVERRIDE,
};

bool resetStepNodePayload(SequencerPatternState& pattern, SequencerGraphNodeId nodeId,
                          SequencerGraphNodeResetMode mode = SequencerGraphNodeResetMode::DEFAULT);

// Allocation-free building blocks for prepared/batched mutations. They never
// touch SequencerPatternState revision signals; their versioned wrappers below
// preserve the existing public behavior.
[[nodiscard]] bool resetStepNodePayloadUnversioned(
    oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphNodeId nodeId,
    SequencerGraphNodeResetMode mode = SequencerGraphNodeResetMode::DEFAULT
) noexcept;
[[nodiscard]] bool resetStepNodePayloadPreservingChildrenUnversioned(
    oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphNodeId nodeId,
    SequencerGraphNodeResetMode mode = SequencerGraphNodeResetMode::DEFAULT
) noexcept;
[[nodiscard]] bool copyStepNodePayloadFromGraphUnversioned(
    oc::note::sequencer::StepSequencerGraph& targetGraph,
    SequencerGraphNodeId targetNodeId,
    const oc::note::sequencer::StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId,
    uint8_t targetDepth
) noexcept;
bool copyStepNodePayloadFromGraph(SequencerPatternState& targetPattern,
                                  SequencerGraphNodeId targetNodeId,
                                  const oc::note::sequencer::StepSequencerGraph& sourceGraph,
                                  SequencerGraphNodeId sourceNodeId);
bool copyNodeChildrenFromGraph(SequencerPatternState& targetPattern,
                               SequencerGraphNodeId targetNodeId,
                               const oc::note::sequencer::StepSequencerGraph& sourceGraph,
                               SequencerGraphNodeId sourceNodeId);
bool copyNodeChildSequenceFromGraph(SequencerPatternState& targetPattern,
                                    SequencerGraphNodeId targetNodeId,
                                    const oc::note::sequencer::StepSequencerGraph& sourceGraph,
                                    SequencerGraphNodeId sourceNodeId);
bool copyNodeCycleStateSetFromGraph(SequencerPatternState& targetPattern,
                                    SequencerGraphNodeId targetNodeId,
                                    const oc::note::sequencer::StepSequencerGraph& sourceGraph,
                                    SequencerGraphNodeId sourceNodeId);

bool setNodeEnabledOverride(SequencerPatternState& pattern, SequencerGraphNodeId nodeId,
                            bool enabled);
bool clearNodeEnabledOverride(SequencerPatternState& pattern, SequencerGraphNodeId nodeId);
bool setNodeNoteOffset(SequencerPatternState& pattern, SequencerGraphNodeId nodeId, int8_t offset);
bool setNodeVelocityOffset(SequencerPatternState& pattern, SequencerGraphNodeId nodeId,
                           int16_t offset);
bool setNodeGateOffset(SequencerPatternState& pattern, SequencerGraphNodeId nodeId, int16_t offset);
bool setNodeNudgeOffset(SequencerPatternState& pattern, SequencerGraphNodeId nodeId, int8_t offset);
bool setNodeProbabilityOffset(SequencerPatternState& pattern, SequencerGraphNodeId nodeId,
                              int16_t offset);
bool setNodeChordMode(SequencerPatternState& pattern, SequencerGraphNodeId nodeId,
                      oc::note::sequencer::StepSequencerChordMode mode);
bool setNodeChordSpec(SequencerPatternState& pattern, SequencerGraphNodeId nodeId,
                      oc::note::sequencer::StepSequencerChordSpec spec);
bool clearNodeChordState(SequencerPatternState& pattern, SequencerGraphNodeId nodeId);
uint8_t nodeLocalVariationRange(const oc::note::sequencer::StepSequencerStepNode& node,
                                StepProperty property);
bool setNodeLocalVariationRange(SequencerPatternState& pattern, SequencerGraphNodeId nodeId,
                                StepProperty property, uint8_t range);

}  // namespace core::state::sequencer
