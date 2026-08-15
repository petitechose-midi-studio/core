#include <cassert>
#include <iostream>

#include "../../src/handler/sequencer/PatternPitchSettingsDomainServices.hpp"
#include "../../src/handler/project/ProjectScaleSettingsDomainServices.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerProjectScaleOps.hpp"

namespace {

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleType;
using ChordBasis =
    oc::note::sequencer::StepSequencerChordIntervalBasis;
using ChordHarmony =
    oc::note::sequencer::StepSequencerChordHarmony;
using ChordSpec =
    oc::note::sequencer::StepSequencerChordSpec;
ChordSpec customChord(uint8_t second, uint8_t third) {
    auto spec = ChordSpec::semantic(
        ChordHarmony::Custom,
        3,
        oc::note::sequencer::StepSequencerChordVoicing::Close,
        0,
        ChordBasis::ChromaticSemitones
    );
    spec.setCustomInterval(1, second);
    spec.setCustomInterval(2, third);
    return spec;
}

const ChordSpec& rootChord(
    const core::state::sequencer::SequencerPatternState& pattern
) {
    const auto* graph =
        core::state::sequencer::graphView(pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(
        core::state::sequencer::rootStepNodeId(0)
    );
    assert(node != nullptr);
    assert(node->has(
        oc::note::sequencer::STEP_NODE_CHORD_LOCAL
    ));
    return node->chordSpec;
}

void authorScalePolicyChord(
    core::state::sequencer::SequencerPatternState& pattern,
    ChordSpec spec
) {
    pattern.setPitchEditMode(
        core::state::sequencer::
            SequencerPitchEditMode::FOLLOW_SCALE
    );
    pattern.note[0] = 65;
    assert(core::state::sequencer::setNodeChordSpec(
        pattern,
        core::state::sequencer::rootStepNodeId(0),
        spec
    ));
}

core::state::sequencer::SequencerProjectScaleMutationResult
applyProjectScaleChoice(
    core::state::sequencer::SequencerTrackBankState& trackBank,
    core::state::sequencer::SequencerState& sequencer,
    uint8_t row,
    int choiceIndex
) {
    const auto choice = core::state::sequencer::resolveProjectScaleChoice(
        trackBank.projectScaleSettings(),
        row,
        choiceIndex
    );
    assert(choice.valid);
    if (!choice.changes) return {};
    return core::state::sequencer::applyProjectScaleTransition(
        trackBank,
        sequencer,
        choice.target
    );
}

void test_project_scale_choices_update_track_bank() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::handler::ProjectScaleSettingsDomainServices services{
        core::handler::ProjectScaleSettingsDomainServices::StateRefs{
            trackBank,
        }
    };

    assert(services.choiceCount(0) == 12);
    assert(services.choiceCount(1) == 14);
    assert(services.choiceCount(2) == 4);
    assert(services.currentChoiceIndex(0) == 5);
    assert(services.currentChoiceIndex(1) == 3);
    assert(services.currentChoiceIndex(2) == 1);

    const uint32_t projectRevisionBefore =
        trackBank.projectScaleRevisionSignal().get();
    const uint32_t activeScaleRevisionBefore =
        sequencer.pattern.patternScaleRevision.get();
    assert(applyProjectScaleChoice(trackBank, sequencer, 0, 9).changed);
    assert(applyProjectScaleChoice(trackBank, sequencer, 1, 2).changed);
    assert(!applyProjectScaleChoice(trackBank, sequencer, 2, 1).changed);

    const auto settings = trackBank.projectScaleSettings();
    assert(settings.root == 9);
    assert(settings.type == StepSequencerScaleType::NaturalMinor);
    assert(settings.mode == StepSequencerScaleConstraintMode::ConstrainNearest);
    assert(services.currentChoiceIndex(0) == 9);
    assert(services.currentChoiceIndex(1) == 2);
    assert(services.currentChoiceIndex(2) == 1);
    assert(trackBank.projectScaleRevisionSignal().get() ==
           projectRevisionBefore + 2U);
    assert(sequencer.pattern.patternScaleRevision.get() ==
           activeScaleRevisionBefore + 2U);

    std::cout << "[PASS] test_project_scale_choices_update_track_bank\n";
}

void test_project_scale_choices_are_clamped() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;

    const uint32_t projectRevisionBefore =
        trackBank.projectScaleRevisionSignal().get();
    const uint32_t activeScaleRevisionBefore =
        sequencer.pattern.patternScaleRevision.get();
    assert(applyProjectScaleChoice(trackBank, sequencer, 0, 99).changed);
    assert(applyProjectScaleChoice(trackBank, sequencer, 1, 99).changed);
    assert(applyProjectScaleChoice(trackBank, sequencer, 2, 99).changed);

    const auto settings = trackBank.projectScaleSettings();
    assert(settings.root == 11);
    assert(settings.type == StepSequencerScaleType::WholeTone);
    assert(settings.mode == StepSequencerScaleConstraintMode::ConstrainDown);
    assert(trackBank.projectScaleRevisionSignal().get() ==
           projectRevisionBefore + 3U);
    assert(sequencer.pattern.patternScaleRevision.get() ==
           activeScaleRevisionBefore + 3U);

    std::cout << "[PASS] test_project_scale_choices_are_clamped\n";
}

void test_project_scale_resolver_invalid_noop_and_clamp_contract() {
    core::state::sequencer::SequencerTrackBankState trackBank;
    const auto current = trackBank.projectScaleSettings();

    const auto invalid = core::state::sequencer::resolveProjectScaleChoice(
        current,
        3,
        0
    );
    assert(!invalid.valid);
    assert(!invalid.changes);
    assert(invalid.target.root == current.root);
    assert(invalid.target.type == current.type);
    assert(invalid.target.mode == current.mode);

    const auto noop = core::state::sequencer::resolveProjectScaleChoice(
        current,
        0,
        current.root
    );
    assert(noop.valid);
    assert(!noop.changes);

    const auto low = core::state::sequencer::resolveProjectScaleChoice(
        current,
        0,
        -99
    );
    const auto high = core::state::sequencer::resolveProjectScaleChoice(
        current,
        0,
        99
    );
    assert(low.valid && low.changes && low.target.root == 0);
    assert(high.valid && high.changes && high.target.root == 11);

    std::cout
        << "[PASS] Project scale resolver invalid/no-op/clamp contract\n";
}

void test_project_scale_invalidates_inherited_active_telemetry() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;

    sequencer.cycleVariationTelemetry.validMask.setBit(0, true);
    const uint32_t before = sequencer.variationTelemetryRevision.get();

    const uint32_t projectRevisionBefore =
        trackBank.projectScaleRevisionSignal().get();
    const uint32_t activeScaleRevisionBefore =
        sequencer.pattern.patternScaleRevision.get();
    assert(applyProjectScaleChoice(trackBank, sequencer, 1, 1).changed);

    assert(!sequencer.cycleVariationTelemetry.validMask.test(0));
    assert(sequencer.variationTelemetryRevision.get() == before + 1U);
    assert(trackBank.projectScaleRevisionSignal().get() ==
           projectRevisionBefore + 1U);
    assert(sequencer.pattern.patternScaleRevision.get() ==
           activeScaleRevisionBefore + 1U);

    std::cout << "[PASS] test_project_scale_invalidates_inherited_active_telemetry\n";
}

void test_project_scale_keeps_override_active_telemetry() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;

    sequencer.setPatternScalePolicy(core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE);
    sequencer.cycleVariationTelemetry.validMask.setBit(0, true);
    const uint32_t before = sequencer.variationTelemetryRevision.get();

    const uint32_t projectRevisionBefore =
        trackBank.projectScaleRevisionSignal().get();
    const uint32_t activeScaleRevisionBefore =
        sequencer.pattern.patternScaleRevision.get();
    assert(applyProjectScaleChoice(trackBank, sequencer, 1, 1).changed);

    assert(sequencer.cycleVariationTelemetry.validMask.test(0));
    assert(sequencer.variationTelemetryRevision.get() == before);
    assert(trackBank.projectScaleRevisionSignal().get() ==
           projectRevisionBefore + 1U);
    assert(sequencer.pattern.patternScaleRevision.get() ==
           activeScaleRevisionBefore);

    std::cout << "[PASS] test_project_scale_keeps_override_active_telemetry\n";
}

void test_project_scale_boundary_projects_inherited_chords() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;

    assert(applyProjectScaleChoice(trackBank, sequencer, 2, 0).changed);
    authorScalePolicyChord(
        sequencer.pattern,
        customChord(3, 5)
    );

    const uint32_t projectRevisionBefore =
        trackBank.projectScaleRevisionSignal().get();
    const uint32_t activeScaleRevisionBefore =
        sequencer.pattern.patternScaleRevision.get();
    const auto mutation = applyProjectScaleChoice(trackBank, sequencer, 2, 1);
    const auto projection = mutation.projection;

    assert(mutation.changed);
    const auto& spec = rootChord(sequencer.pattern);
    assert(spec.intervalBasis() == ChordBasis::ScaleDegrees);
    assert(spec.customInterval(1) == 2);
    assert(spec.customInterval(2) == 3);
    assert(projection.changed == 1);
    assert(projection.exact == 1);
    assert(!projection.hasAdaptations());
    assert(trackBank.projectScaleRevisionSignal().get() ==
           projectRevisionBefore + 1U);
    assert(sequencer.pattern.patternScaleRevision.get() ==
           activeScaleRevisionBefore + 1U);

    std::cout
        << "[PASS] Project scale boundary projects inherited chords\n";
}

void test_pattern_pitch_settings_override_copies_project_before_local_edits() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::handler::PatternPitchSettingsDomainServices patternServices{
        core::handler::PatternPitchSettingsDomainServices::StateRefs{
            sequencer,
            trackBank,
        }
    };

    assert(applyProjectScaleChoice(trackBank, sequencer, 0, 2).changed);
    assert(applyProjectScaleChoice(trackBank, sequencer, 1, 2).changed);
    assert(!applyProjectScaleChoice(trackBank, sequencer, 2, 1).changed);
    assert(patternServices.choiceCount(1) == 0);

    patternServices.applyChoice(0, 1);
    assert(sequencer.pattern.scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE);
    assert(patternServices.choiceCount(1) == 12);
    assert(patternServices.currentChoiceIndex(1) == 2);
    assert(patternServices.currentChoiceIndex(2) == 2);

    patternServices.applyChoice(1, 9);
    patternServices.applyChoice(2, 13);
    assert(sequencer.pattern.scaleOverride.root == 9);
    assert(sequencer.pattern.scaleOverride.type == StepSequencerScaleType::WholeTone);

    patternServices.applyChoice(3, 0);
    assert(sequencer.pattern.pitchEditMode ==
           core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE);

    patternServices.applyChoice(0, 0);
    assert(sequencer.pattern.scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT);
    assert(patternServices.choiceCount(1) == 0);

    std::cout << "[PASS] test_pattern_pitch_settings_override_copies_project_before_local_edits\n";
}

void test_pattern_return_to_project_projects_local_chords() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::handler::PatternPitchSettingsDomainServices services{
        core::handler::PatternPitchSettingsDomainServices::StateRefs{
            sequencer,
            trackBank,
        }
    };

    auto overrideScale = trackBank.projectScaleSettings();
    overrideScale.type = StepSequencerScaleType::Chromatic;
    overrideScale.mode = StepSequencerScaleConstraintMode::Free;
    sequencer.setPatternScaleOverride(overrideScale);
    sequencer.setPatternScalePolicy(
        core::state::sequencer::
            SequencerPatternScalePolicy::OVERRIDE
    );
    authorScalePolicyChord(
        sequencer.pattern,
        customChord(3, 5)
    );

    const auto projection = services.applyChoice(0, 0);

    assert(
        sequencer.pattern.scalePolicy ==
        core::state::sequencer::
            SequencerPatternScalePolicy::INHERIT_PROJECT
    );
    const auto& spec = rootChord(sequencer.pattern);
    assert(spec.intervalBasis() == ChordBasis::ScaleDegrees);
    assert(spec.customInterval(1) == 2);
    assert(spec.customInterval(2) == 3);
    assert(projection.changed == 1);
    assert(projection.exact == 1);

    std::cout
        << "[PASS] Pattern return to Project projects local chords\n";
}

void test_pattern_pitch_context_projects_formula_at_the_mode_boundary() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::handler::PatternPitchSettingsDomainServices services{
        core::handler::PatternPitchSettingsDomainServices::StateRefs{
            sequencer,
            trackBank,
        }
    };

    auto relativeSource = customChord(2, 3);
    relativeSource.setIntervalBasis(ChordBasis::ScaleDegrees);
    authorScalePolicyChord(sequencer.pattern, relativeSource);
    assert(
        rootChord(sequencer.pattern).intervalBasis() ==
        ChordBasis::ScaleDegrees
    );

    const auto toChromatic = services.applyChoice(3, 1);
    assert(
        sequencer.pattern.pitchEditMode ==
        core::state::sequencer::SequencerPitchEditMode::CHROMATIC
    );
    const auto& chromatic = rootChord(sequencer.pattern);
    assert(chromatic.intervalBasis() == ChordBasis::ChromaticSemitones);
    assert(chromatic.customInterval(1) == 3U);
    assert(chromatic.customInterval(2) == 5U);
    assert(toChromatic.changed == 1U);
    assert(toChromatic.exact == 1U);

    const auto toFollow = services.applyChoice(3, 0);
    assert(
        sequencer.pattern.pitchEditMode ==
        core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE
    );
    const auto& relative = rootChord(sequencer.pattern);
    assert(relative.intervalBasis() == ChordBasis::ScaleDegrees);
    assert(relative.customInterval(1) == 2U);
    assert(relative.customInterval(2) == 3U);
    assert(toFollow.changed == 1U);
    assert(toFollow.exact == 1U);

    std::cout
        << "[PASS] Pattern Pitch Context projects formulas at its boundary\n";
}

}  // namespace

int main() {
    test_project_scale_choices_update_track_bank();
    test_project_scale_choices_are_clamped();
    test_project_scale_resolver_invalid_noop_and_clamp_contract();
    test_project_scale_invalidates_inherited_active_telemetry();
    test_project_scale_keeps_override_active_telemetry();
    test_project_scale_boundary_projects_inherited_chords();
    test_pattern_pitch_settings_override_copies_project_before_local_edits();
    test_pattern_return_to_project_projects_local_chords();
    test_pattern_pitch_context_projects_formula_at_the_mode_boundary();
    std::cout << "All ProjectScaleSettingsDomainServices tests passed\n";
    return 0;
}
