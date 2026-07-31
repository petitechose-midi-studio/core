#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstring>
#include <iostream>

#include "../../src/state/sequencer/SequencerNoteSpelling.hpp"

namespace {

namespace spelling = core::state::sequencer::note_spelling;

oc::note::sequencer::StepSequencerScaleSettings harmonicMinor(uint8_t root) {
    return {
        .root = root,
        .type = oc::note::sequencer::StepSequencerScaleType::HarmonicMinor,
        .mode =
            oc::note::sequencer::StepSequencerScaleConstraintMode::ConstrainNearest,
    };
}

void test_f_harmonic_minor_uses_expected_flat_spelling() {
    const auto scale = harmonicMinor(5);
    assert(std::strcmp(spelling::pitchClassLabel(5, scale), "F") == 0);
    assert(std::strcmp(spelling::pitchClassLabel(8, scale), "Ab") == 0);
    assert(std::strcmp(spelling::pitchClassLabel(10, scale), "Bb") == 0);
    assert(std::strcmp(spelling::pitchClassLabel(0, scale), "C") == 0);
    assert(std::strcmp(spelling::pitchClassLabel(1, scale), "Db") == 0);
    assert(std::strcmp(spelling::pitchClassLabel(4, scale), "E") == 0);

    char note[8] = {};
    spelling::formatNoteName(note, sizeof(note), 68, scale);
    assert(std::strcmp(note, "Ab4") == 0);

    std::cout
        << "[PASS] test_f_harmonic_minor_uses_expected_flat_spelling\n";
}

void test_harmonic_minor_preserves_required_sharp_leading_tone() {
    const auto scale = harmonicMinor(7);
    assert(std::strcmp(spelling::pitchClassLabel(6, scale), "F#") == 0);

    std::cout
        << "[PASS] test_harmonic_minor_preserves_required_sharp_leading_tone\n";
}

}  // namespace

int main() {
    test_f_harmonic_minor_uses_expected_flat_spelling();
    test_harmonic_minor_preserves_required_sharp_leading_tone();
    std::cout << "\nAll SequencerNoteSpelling tests passed.\n";
    return 0;
}
