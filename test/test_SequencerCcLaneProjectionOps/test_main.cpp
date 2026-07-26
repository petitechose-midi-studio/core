#include <cassert>
#include <cstdint>
#include <iostream>

#include "state/sequencer/SequencerCcLaneProjectionOps.hpp"

namespace {

namespace seq = core::state::sequencer;
using Region = oc::note::sequencer::StepSequencerPlaybackRegion;

seq::SequencerCcLane makeLane() {
    seq::SequencerCcLaneBank bank{};
    seq::SequencerCcLaneDraft draft{};
    assert(seq::createSequencerCcLane(bank, 0, draft).changed());
    return bank.lanes[0];
}

void author(
    seq::SequencerCcLane& lane,
    uint8_t step,
    uint8_t value,
    seq::SequencerCcLaneTransition transition = seq::SequencerCcLaneTransition::HOLD
) {
    seq::SequencerCcLaneBank bank{};
    bank.lanes[0] = lane;
    assert(seq::setSequencerCcLaneEvent(bank, 0, step, value).changed());
    if (transition != seq::SequencerCcLaneTransition::HOLD) {
        assert(seq::setSequencerCcLaneTransition(
            bank,
            0,
            step,
            transition
        ).changed());
    }
    lane = bank.lanes[0];
}

void test_prelude_flows_once_into_first_loop_event() {
    auto lane = makeLane();
    author(lane, 1, 10, seq::SequencerCcLaneTransition::LINEAR);
    author(lane, 4, 40);
    const Region region{8, 1, 3, 6};

    uint8_t value = 0;
    seq::SequencerCcLaneProjectionSpan span{};
    assert(seq::projectSequencerCcLaneValue(lane, region, 0, 0.0f, value, &span));
    assert(value == 10);
    assert(span.sourceStep == 1);
    assert(span.targetStep == 4);
    assert(span.distanceToTarget == 3);

    assert(seq::projectSequencerCcLaneValue(lane, region, 1, 0.0f, value));
    assert(value == 20);
    assert(seq::projectSequencerCcLaneValue(lane, region, 2, 0.0f, value));
    assert(value == 30);
    assert(seq::projectSequencerCcLaneValue(lane, region, 3, 0.0f, value));
    assert(value == 40);
}

void test_loop_wrap_never_targets_prelude_or_outside_events() {
    auto lane = makeLane();
    author(lane, 0, 1);
    author(lane, 1, 10);
    author(lane, 3, 30);
    author(lane, 5, 50, seq::SequencerCcLaneTransition::LINEAR);
    author(lane, 7, 127);
    const Region region{8, 1, 3, 6};

    // Second Loop traversal, step 5. Its target is Loop Start (step 3), never
    // Play Start (step 1) nor authored content outside Loop End.
    const uint32_t ordinal = region.preludeLength() + region.loopLength() + 2U;
    seq::SequencerCcLaneProjectionSpan span{};
    assert(seq::resolveSequencerCcLaneProjectionSpan(lane, region, ordinal, span));
    assert(span.sourceStep == 5);
    assert(span.targetStep == 3);
    assert(span.distanceToTarget == 1);

    uint8_t value = 0;
    assert(seq::projectSequencerCcLaneValue(
        lane,
        region,
        ordinal,
        0.5f,
        value
    ));
    assert(value == 40);
}

void test_before_first_event_is_silent_but_prelude_hold_persists() {
    auto loopLane = makeLane();
    author(loopLane, 4, 64);
    const Region loopOnly{8, 0, 3, 6};
    uint8_t value = 99;
    assert(!seq::projectSequencerCcLaneValue(loopLane, loopOnly, 0, 0.0f, value));
    assert(value == 99);

    auto preludeLane = makeLane();
    author(preludeLane, 1, 77);
    const Region withPrelude{8, 1, 3, 6};
    const uint32_t thirdLoop = withPrelude.preludeLength() +
        withPrelude.loopLength() * 2U;
    assert(seq::projectSequencerCcLaneValue(
        preludeLane,
        withPrelude,
        thirdLoop,
        0.0f,
        value
    ));
    assert(value == 77);
}

void test_ui_representative_ordinal_separates_prelude_and_steady_loop() {
    const Region region{16, 2, 6, 10};
    uint32_t ordinal = 0;
    assert(seq::representativeSequencerCcLaneOrdinalForStep(region, 4, ordinal));
    assert(ordinal == 2);
    assert(seq::representativeSequencerCcLaneOrdinalForStep(region, 7, ordinal));
    assert(ordinal == region.preludeLength() + region.loopLength() + 1U);
    assert(!seq::representativeSequencerCcLaneOrdinalForStep(region, 1, ordinal));
    assert(!seq::representativeSequencerCcLaneOrdinalForStep(region, 10, ordinal));
}

}  // namespace

int main() {
    test_prelude_flows_once_into_first_loop_event();
    test_loop_wrap_never_targets_prelude_or_outside_events();
    test_before_first_event_is_silent_but_prelude_hold_persists();
    test_ui_representative_ordinal_separates_prelude_and_steady_loop();
    std::cout << "SequencerCcLaneProjectionOps tests passed\n";
    return 0;
}
