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

    void reset();

    bool active() const { return active_; }
    bool openRootStepContext(uint8_t rootStep);
    bool focusLocalStep(uint8_t index);
    bool leaveChildContext();
    bool maxDepthReached() const;

    StepContentContextView current() const;

    StepContentEditResult createOrOpenMicroSequence(SequencerPatternState& pattern,
                                                    uint8_t length);
    StepContentEditResult createOrOpenCycleStates(SequencerPatternState& pattern,
                                                  uint8_t length);

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
