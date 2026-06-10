#include <cassert>
#include <iostream>

#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"
#include "../../src/ui/sequencer/StepContentBadgeProjection.hpp"

namespace {

using core::state::sequencer::SequencerState;
using core::state::sequencer::createCycleStateSet;
using core::state::sequencer::createMicroSequence;
using core::state::sequencer::rootStepNodeId;
using core::ui::sequencer::grid::buildStepContentBadgeProjection;

void test_projects_root_step_content_badges() {
    core::state::sequencer::SequencerPatternState pattern;

    assert(createMicroSequence(pattern, rootStepNodeId(1), 2).ok);
    assert(createCycleStateSet(pattern, rootStepNodeId(2), 4).ok);
    assert(createMicroSequence(pattern, rootStepNodeId(3), 2).ok);
    assert(createCycleStateSet(pattern, rootStepNodeId(3), 4).ok);

    auto badges = buildStepContentBadgeProjection(pattern, 0);
    assert(!badges.microSequence);
    assert(!badges.cycleStates);

    badges = buildStepContentBadgeProjection(pattern, 1);
    assert(badges.microSequence);
    assert(!badges.cycleStates);

    badges = buildStepContentBadgeProjection(pattern, 2);
    assert(!badges.microSequence);
    assert(badges.cycleStates);

    badges = buildStepContentBadgeProjection(pattern, 3);
    assert(badges.microSequence);
    assert(badges.cycleStates);

    std::cout << "[PASS] test_projects_root_step_content_badges\n";
}

void test_invalid_or_missing_graph_has_no_badges() {
    core::state::sequencer::SequencerPatternState pattern;

    auto badges = buildStepContentBadgeProjection(pattern, 1);
    assert(!badges.microSequence);
    assert(!badges.cycleStates);

    assert(createMicroSequence(pattern, rootStepNodeId(1), 2).ok);
    badges = buildStepContentBadgeProjection(pattern, SequencerState::MAX_STEPS);
    assert(!badges.microSequence);
    assert(!badges.cycleStates);

    std::cout << "[PASS] test_invalid_or_missing_graph_has_no_badges\n";
}

}  // namespace

int main() {
    test_projects_root_step_content_badges();
    test_invalid_or_missing_graph_has_no_badges();

    std::cout << "\nAll SequencerStepGridFrame tests passed.\n";
    return 0;
}
