#include <cassert>
#include <cmath>
#include <limits>

#include "state/sequencer/SequencerPitchEditAuthority.hpp"
#include "state/sequencer/SequencerValueMapping.hpp"

namespace value = core::state::sequencer::value_mapping;
namespace pitch = core::state::sequencer::pitch_edit;
namespace normalized = core::state::normalized;

int main() {
    // Physical controls clamp at the ends and choose the upper item on a tie.
    assert(normalized::normalizedToIndex(-1.0f, 128) == 0);
    assert(normalized::normalizedToIndex(2.0f, 128) == 127);
    assert(normalized::normalizedToIndex(0.5f, 2) == 1);
    assert(normalized::normalizedToIndex(std::nextafter(0.5f, 0.0f), 2) == 1);
    assert(normalized::normalizedToIndex(0.0f, 0) == 0);
    assert(normalized::indexToNormalized(1, 1) == 0.0f);

    // Gate's physical midpoint represents a full step, not half the maximum.
    assert(value::normalizedToGatePercent(0.5f) == 100);
    assert(value::gatePercentToNormalized(100) == 0.5f);
    constexpr int maxGate = core::state::sequencer::SequencerPatternState::MAX_GATE_PERCENT;
    for (int gate = 0; gate <= maxGate; ++gate) {
        assert(value::normalizedToGatePercent(value::gatePercentToNormalized(gate)) == gate);
    }
    // Check clamping before narrowing: the content editor supplies signed ints.
    assert(value::gatePercentToNormalized(std::numeric_limits<int>::min()) == 0.0f);
    assert(value::gatePercentToNormalized(std::numeric_limits<int>::max()) == 1.0f);
    assert(value::nudgeToNormalized(std::numeric_limits<int>::min()) == 0.0f);
    assert(value::probabilityToNormalized(std::numeric_limits<int>::max()) == 1.0f);
    for (int nudge = -50; nudge <= 50; ++nudge) {
        assert(value::normalizedToNudge(value::nudgeToNormalized(nudge)) == nudge);
    }
    for (int probability = 0; probability <= 100; ++probability) {
        assert(value::normalizedToProbability(value::probabilityToNormalized(probability)) == probability);
    }

    using namespace oc::note::sequencer;
    StepSequencerScaleSettings scale{0, StepSequencerScaleType::Major,
                                    StepSequencerScaleConstraintMode::ConstrainNearest};
    assert(pitch::countScaleNotes(scale) == 75);
    assert(pitch::scaleNoteForDegreeIndex(-1, scale) == 0);
    assert(pitch::scaleNoteForDegreeIndex(1000, scale) == 127);
    for (int degree = 0; degree < pitch::countScaleNotes(scale); ++degree) {
        const auto note = pitch::scaleNoteForDegreeIndex(degree, scale);
        assert(scaleContainsNote(scale, note));
        assert(pitch::scaleDegreeIndexForNote(note, scale) == degree);
    }
}
