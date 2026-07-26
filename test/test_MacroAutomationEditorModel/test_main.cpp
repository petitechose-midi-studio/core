#include <cassert>
#include <cmath>
#include <iostream>

#include "handler/macro/MacroAutomationEditorModel.hpp"

namespace {

bool near(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

core::state::modulation::ProjectControlCurveView
makeAutomationWithSourceDuration(float beats) {
    core::state::modulation::ProjectControlCurveView automation;
    automation.id = {1U};
    automation.pointCount = 1U;
    automation.spec.sourceDurationTicks =
        core::state::macro::macroAutomationTicksFromBeats(beats);
    return automation;
}

void test_length_range_edits_one_beat_steps_by_default() {
    using namespace core::handler;

    const auto range = macroAutomationLengthEditRange(false);
    assert(range.minBeat == 1);
    assert(range.beatStep == 1);
    assert(range.stepCount == 64);
    assert(near(macroAutomationEncoderPositionToBeat(0.0f, range), 1.0f));
    assert(near(macroAutomationEncoderPositionToBeat(2.0f / 63.0f, range), 3.0f));
    assert(near(macroAutomationEncoderPositionToBeat(1.0f, range), 64.0f));
    assert(near(
        macroAutomationTicksToEncoderPosition(
            core::state::macro::macroAutomationTicksFromBeats(3.0f),
            range
        ),
        2.0f / 63.0f
    ));

    std::cout << "[PASS] test_length_range_edits_one_beat_steps_by_default\n";
}

void test_length_range_uses_four_beat_steps_when_coarse() {
    using namespace core::handler;

    const auto range = macroAutomationLengthEditRange(true);
    assert(range.minBeat == 4);
    assert(range.beatStep == 4);
    assert(range.stepCount == 16);
    assert(near(macroAutomationEncoderPositionToBeat(0.0f, range), 4.0f));
    assert(near(macroAutomationEncoderPositionToBeat(1.0f / 15.0f, range), 8.0f));
    assert(near(macroAutomationEncoderPositionToBeat(1.0f, range), 64.0f));

    std::cout << "[PASS] test_length_range_uses_four_beat_steps_when_coarse\n";
}

void test_offset_range_is_bounded_by_source_duration_and_wrap_semantics() {
    using namespace core::handler;

    const auto shortAutomation = makeAutomationWithSourceDuration(2.0f);
    const auto shortRange =
        macroAutomationOffsetEditRange(&shortAutomation, false);
    assert(shortRange.minBeat == 0);
    assert(shortRange.beatStep == 1);
    assert(shortRange.stepCount == 2);
    assert(near(macroAutomationEncoderPositionToBeat(1.0f, shortRange), 1.0f));

    const auto longAutomation = makeAutomationWithSourceDuration(8.0f);
    const auto coarseRange =
        macroAutomationOffsetEditRange(&longAutomation, true);
    assert(coarseRange.minBeat == 0);
    assert(coarseRange.beatStep == 4);
    assert(coarseRange.stepCount == 2);
    assert(near(macroAutomationEncoderPositionToBeat(1.0f, coarseRange), 4.0f));

    const auto emptyRange = macroAutomationOffsetEditRange(nullptr, false);
    assert(emptyRange.stepCount == 1);
    assert(near(macroAutomationEncoderPositionToBeat(1.0f, emptyRange), 0.0f));
    assert(near(macroAutomationTicksToEncoderPosition(0, emptyRange), 0.0f));

    std::cout << "[PASS] test_offset_range_is_bounded_by_source_duration_and_wrap_semantics\n";
}

}  // namespace

int main() {
    test_length_range_edits_one_beat_steps_by_default();
    test_length_range_uses_four_beat_steps_when_coarse();
    test_offset_range_is_bounded_by_source_duration_and_wrap_semantics();

    std::cout << "\nAll MacroAutomationEditorModel tests passed.\n";
    return 0;
}
