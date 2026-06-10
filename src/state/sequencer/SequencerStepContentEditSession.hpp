#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerPatternState.hpp"

namespace core::state::sequencer {

enum class StepContentContextKind : uint8_t {
    ROOT_STEP = 0,
    MICRO_SEQUENCE,
    CYCLE_STATES,
};

enum class StepContentChildKind : uint8_t {
    MICRO_SEQUENCE = 0,
    CYCLE_STATES,
};

struct StepContentEditResult {
    bool ok = false;
    bool limitReached = false;
    bool openedExisting = false;
};

enum class StepContentCreationBlockReason : uint8_t {
    NONE = 0,
    INACTIVE_CONTEXT,
    MAX_DEPTH_REACHED,
    INVALID_FOCUSED_STEP,
    GRAPH_LIMIT_REACHED,
};

struct StepContentCreationAvailability {
    bool canCreateOrOpen = false;
    bool opensExisting = false;
    StepContentCreationBlockReason blockedReason =
        StepContentCreationBlockReason::INACTIVE_CONTEXT;
};

struct StepContentContextView {
    StepContentContextKind kind = StepContentContextKind::ROOT_STEP;
    uint8_t rootStep = 0;
    uint8_t localIndex = 0;
    uint8_t length = 0;
    uint8_t depth = 0;
    bool active = false;
};

class SequencerStepContentEditSession {
public:
    static constexpr uint8_t MAX_CONTEXTS =
        oc::note::sequencer::StepSequencerGraphLimits::MAX_DEPTH + 1U;
    static constexpr uint8_t DEFAULT_MICRO_SEQUENCE_LENGTH = 2;
    static constexpr uint8_t DEFAULT_CYCLE_STATE_COUNT = 4;

    void reset();

    bool active() const { return active_; }
    bool openRootStepContext(uint8_t rootStep);
    bool focusLocalStep(uint8_t index);
    bool leaveChildContext();
    bool maxDepthReached() const;

    StepContentContextView current() const;

    StepContentEditResult createOrOpenMicroSequence(SequencerPatternState& pattern);
    StepContentEditResult createOrOpenMicroSequence(SequencerPatternState& pattern,
                                                    uint8_t length);
    StepContentEditResult createOrOpenCycleStates(SequencerPatternState& pattern);
    StepContentEditResult createOrOpenCycleStates(SequencerPatternState& pattern,
                                                  uint8_t length);

    bool focusedStepHasMicroSequence(const SequencerPatternState& pattern) const;
    bool focusedStepHasCycleStates(const SequencerPatternState& pattern) const;
    StepContentCreationAvailability childCreationAvailability(
        const SequencerPatternState& pattern,
        StepContentChildKind childKind,
        uint8_t length
    ) const;

    bool setFocusedNoteOffset(SequencerPatternState& pattern, int8_t offset);
    bool setFocusedVelocityOffset(SequencerPatternState& pattern, int16_t offset);
    bool setFocusedGateOffset(SequencerPatternState& pattern, int16_t offset);
    bool setFocusedNudgeOffset(SequencerPatternState& pattern, int8_t offset);
    bool setFocusedProbabilityOffset(SequencerPatternState& pattern, int16_t offset);

    bool removeFocusedChild(SequencerPatternState& pattern, StepContentChildKind childKind);
    bool removeCurrentChildContext(SequencerPatternState& pattern);

private:
    struct Context {
        StepContentContextKind kind = StepContentContextKind::ROOT_STEP;
        uint8_t rootStep = 0;
        uint8_t localIndex = 0;
        uint8_t length = 0;
        uint8_t depth = 0;
        SequencerGraphNodeId nodeId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
        SequencerGraphNodeId parentNodeId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
        SequencerGraphSequenceId sequenceId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
        SequencerGraphCycleSetId cycleSetId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    };

    const Context* currentContext_() const;
    Context* currentContext_();
    SequencerGraphNodeId focusedNodeId_(const SequencerPatternState& pattern) const;

    bool active_ = false;
    uint8_t stackDepth_ = 0;
    std::array<Context, MAX_CONTEXTS> stack_{};
};

bool stepHasMicroSequence(const SequencerPatternState& pattern, uint8_t rootStep);
bool stepHasCycleStates(const SequencerPatternState& pattern, uint8_t rootStep);

}  // namespace core::state::sequencer
