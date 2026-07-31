#include <cassert>
#include <cstring>
#include <iostream>

#include "context/standalone/SequencerChordFieldPresentation.hpp"
#include "ui/sequencer/SequencerPresetLibraryPresentation.hpp"

namespace {

namespace presentation =
    core::context::standalone::sequencer_chord_field_presentation;
using Basis = oc::note::sequencer::StepSequencerChordIntervalBasis;
using Field = core::state::sequencer::SequencerChordEditField;
using Harmony = oc::note::sequencer::StepSequencerChordHarmony;
using Mode = oc::note::sequencer::StepSequencerChordMode;
using Spec = oc::note::sequencer::StepSequencerChordSpec;

oc::note::sequencer::StepSequencerScaleSettings fHarmonicMinor() {
    return {
        .root = 5,
        .type =
            oc::note::sequencer::StepSequencerScaleType::HarmonicMinor,
        .mode =
            oc::note::sequencer::StepSequencerScaleConstraintMode::ConstrainNearest,
    };
}

core::state::sequencer::SequencerStepChordUiState baseChord(
    Spec spec,
    bool scaleBased
) {
    core::state::sequencer::SequencerStepChordUiState chord{};
    chord.valid = true;
    chord.rootContext = true;
    chord.pitchFollowsScale = scaleBased;
    chord.scaleAvailable = true;
    chord.intervalsUseScaleDegrees = scaleBased;
    chord.mode = Mode::Local;
    chord.spec = spec;
    chord.effectiveVoiceCount = spec.voices();
    chord.preview.valid = true;
    chord.preview.rootNote = 65;
    chord.preview.voiceCount = spec.voices();
    chord.preview.requestedVoiceCount = spec.voices();
    chord.preview.intervalBasis =
        scaleBased ? Basis::ScaleDegrees : Basis::ChromaticSemitones;
    chord.preview.harmony = spec.harmony();
    chord.preview.scaleSettings = fHarmonicMinor();
    chord.preview.analysis.rootPitchClass = 5;
    chord.preview.analysis.bassPitchClass = 5;
    chord.preview.analysis.pitchClassCount = spec.voices();
    return chord;
}

void test_main_chord_surface_keeps_context_and_formula_visible() {
    auto chord = baseChord(
        Spec::semantic(
            Harmony::DiatonicTriad,
            3,
            oc::note::sequencer::StepSequencerChordVoicing::Close,
            0,
            Basis::ScaleDegrees
        ),
        true
    );
    chord.preview.voices[0].note = 65;
    chord.preview.voices[1].note = 68;
    chord.preview.voices[2].note = 72;

    char value[16] = {};
    presentation::formatValue(
        value,
        sizeof(value),
        Field::SHAPE,
        chord
    );
    assert(std::strcmp(value, "Triad") == 0);
    presentation::formatValue(
        value,
        sizeof(value),
        Field::FORMULA,
        chord
    );
    assert(std::strcmp(value, "1-3-5") == 0);
    presentation::formatValue(
        value,
        sizeof(value),
        Field::INVERSION,
        chord
    );
    assert(std::strcmp(value, "Root") == 0);
    presentation::formatContext(value, sizeof(value), chord);
    assert(std::strcmp(value, "Follow DEG") == 0);
    presentation::formatSource(value, sizeof(value), chord);
    assert(std::strcmp(value, "Local chord") == 0);

    std::cout
        << "[PASS] test_main_chord_surface_keeps_context_and_formula_visible\n";
}

void test_formula_surface_formats_the_complete_eight_voice_rail() {
    auto spec = Spec::semantic(
        Harmony::Custom,
        8,
        oc::note::sequencer::StepSequencerChordVoicing::Close,
        0,
        Basis::ChromaticSemitones
    );
    spec.setCustomIntervals({0, 3, 5, 7, 10, 12, 15, 17});
    auto chord = baseChord(spec, false);
    chord.preview.voices[0].note = 65;
    chord.preview.voices[1].note = 68;
    chord.preview.voices[2].note = 70;

    char value[16] = {};
    presentation::formatFormulaVoice(value, sizeof(value), 0, chord);
    assert(std::strcmp(value, "0  F4") == 0);
    presentation::formatFormulaVoice(value, sizeof(value), 1, chord);
    assert(std::strcmp(value, "+3  Ab4") == 0);
    presentation::formatFormulaVoice(value, sizeof(value), 2, chord);
    assert(std::strcmp(value, "+5  Bb4") == 0);
    presentation::formatFormulaVoice(value, sizeof(value), 7, chord);
    assert(std::strcmp(value, "+17  Bb5") == 0);
    presentation::formatFormulaVoiceInterval(
        value,
        sizeof(value),
        7,
        chord
    );
    assert(std::strcmp(value, "+17") == 0);
    assert(presentation::formulaVoiceActive(0, chord));
    assert(presentation::formulaVoiceActive(1, chord));
    assert(presentation::formulaVoiceActive(2, chord));
    assert(presentation::formulaVoiceActive(7, chord));
    presentation::formatContext(value, sizeof(value), chord);
    assert(std::strcmp(value, "Chromatic ST") == 0);
    presentation::formatFormula(value, sizeof(value), chord);
    assert(std::strcmp(value, "0-3-5-7+") == 0);

    std::cout
        << "[PASS] test_formula_surface_formats_the_complete_eight_voice_rail\n";
}

void test_chord_preset_surface_has_dedicated_copy_and_voice_preview() {
    namespace seq = core::state::sequencer;
    core::state::sequencer::SequencerState sequencer;
    auto& picker = sequencer.presetLibrary;
    picker.open(
        seq::SequencerPresetLibraryMode::LOAD,
        seq::SequencerPresetLibraryKind::CHORD
    );
    picker.setEntry(
        0U,
        "chord-preset-001",
        "Wide minor",
        true
    );
    picker.entryCount.set(1U);
    picker.chord().target.valid = true;
    picker.chord().target.canSave = true;
    picker.chord().target.targetUsesScaleDegrees = true;
    picker.chord().target.scale = fHarmonicMinor();

    auto formula = Spec::semantic(
        Harmony::Custom,
        6U,
        oc::note::sequencer::StepSequencerChordVoicing::Wide,
        2U,
        Basis::ScaleDegrees
    );
    formula.setCustomIntervals({0U, 2U, 4U, 6U, 8U, 10U, 0U, 0U});
    formula.strum = 12;
    picker.chord().descriptor.valid = true;
    std::strcpy(
        picker.chord().descriptor.semanticName,
        "Wide minor"
    );
    std::strcpy(
        picker.chord().descriptor.technicalId,
        "chord-preset-001"
    );
    picker.chord().descriptor.compatibility =
        seq::SequencerChordPresetCompatibility::WARNING_ADAPTED;
    picker.chord().descriptor.targetBasis = Basis::ScaleDegrees;
    picker.chord().descriptor.projectedFormula = formula;
    picker.chord().descriptor.resolution.count = 6U;
    for (uint8_t voice = 0U; voice < 6U; ++voice) {
        picker.chord().descriptor.resolution.voices[voice].note =
            static_cast<uint8_t>(65U + voice * 3U);
    }
    picker.bump();

    auto list = core::ui::sequencer::
        buildSequencerPresetLibraryPresentation(sequencer);
    assert(list.visible);
    assert(std::strcmp(list.title.data(), "Load Chord Preset") == 0);
    assert(std::strcmp(list.items[0], "Wide minor") == 0);
    assert(!list.chordVoiceRail.visible);

    picker.detailVisible.set(true);
    picker.bump();
    auto detail = core::ui::sequencer::
        buildSequencerPresetLibraryPresentation(sequencer);
    assert(detail.visible);
    assert(std::strcmp(detail.title.data(), "Wide minor") == 0);
    assert(std::strcmp(detail.meta.data(), "Adapted") == 0);
    assert(detail.chordVoiceRail.visible);
    assert(detail.chordVoiceRail.itemCount == 6U);
    assert(std::strcmp(detail.chordVoiceRail.items[0].label, "R") == 0);
    assert(std::strcmp(detail.chordVoiceRail.items[0].value, "1") == 0);
    assert(std::strcmp(detail.chordVoiceRail.items[5].label, "V6") == 0);
    assert(std::strcmp(detail.chordVoiceRail.items[5].value, "11") == 0);
    assert(std::strstr(detail.items[2], "DEG") != nullptr);
    assert(std::strstr(detail.items[3], "Wide") != nullptr);
    assert(detail.selectedIndex == 2);

    picker.detailFocus.set(1U);
    detail = core::ui::sequencer::
        buildSequencerPresetLibraryPresentation(sequencer);
    assert(detail.selectedIndex == 3);

    const auto action = core::ui::sequencer::
        buildSequencerPresetLibraryActionPresentation(picker);
    assert(
        action.visual ==
        core::ui::ContextActionStripVisualState::ACTIVE
    );
    assert(
        action.tone ==
        core::ui::ContextActionStripTone::WARNING
    );

    std::cout
        << "[PASS] test_chord_preset_surface_has_dedicated_copy_and_voice_preview\n";
}

void test_chord_preset_error_detail_remains_explicit_and_navigable() {
    namespace seq = core::state::sequencer;
    seq::SequencerState sequencer;
    auto& picker = sequencer.presetLibrary;
    picker.open(
        seq::SequencerPresetLibraryMode::LOAD,
        seq::SequencerPresetLibraryKind::CHORD
    );
    picker.setEntry(0U, "broken-chord", "", false);
    picker.entryCount.set(1U);
    picker.detailVisible.set(true);
    picker.detailFocus.set(2U);
    auto& descriptor = picker.chord().descriptor;
    descriptor.valid = true;
    std::strcpy(descriptor.technicalId, "broken-chord");
    descriptor.compatibility =
        seq::SequencerChordPresetCompatibility::CORRUPT;

    const auto detail = core::ui::sequencer::
        buildSequencerPresetLibraryPresentation(sequencer);
    assert(detail.visible);
    assert(std::strcmp(detail.title.data(), "broken-chord") == 0);
    assert(detail.itemCount == 3);
    assert(detail.selectedIndex == 2);
    assert(std::strstr(detail.items[1], "Corrupt") != nullptr);
    assert(std::strcmp(detail.meta.data(), "Corrupt") == 0);
    assert(!detail.chordVoiceRail.visible);

    std::cout
        << "[PASS] test_chord_preset_error_detail_remains_explicit_and_navigable\n";
}

}  // namespace

int main() {
    test_main_chord_surface_keeps_context_and_formula_visible();
    test_formula_surface_formats_the_complete_eight_voice_rail();
    test_chord_preset_surface_has_dedicated_copy_and_voice_preview();
    test_chord_preset_error_detail_remains_explicit_and_navigable();

    std::cout << "\nAll SequencerChordPresentation tests passed.\n";
    return 0;
}
