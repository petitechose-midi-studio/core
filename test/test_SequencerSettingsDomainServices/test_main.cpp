#include <cassert>
#include <iostream>

#include "../../src/handler/settings/SequencerSettingsDomainServices.hpp"
#include "../../src/handler/sequencer/PatternPitchSettingsDomainServices.hpp"

namespace {

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleType;

void test_project_scale_choices_update_track_bank() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::handler::SequencerSettingsDomainServices services{
        core::handler::SequencerSettingsDomainServices::StateRefs{
            sequencer,
            trackBank,
        }
    };

    assert(services.choiceCount(0) == 12);
    assert(services.choiceCount(1) == 14);
    assert(services.choiceCount(2) == 4);
    assert(services.currentChoiceIndex(0) == 0);
    assert(services.currentChoiceIndex(1) == 0);
    assert(services.currentChoiceIndex(2) == 0);

    services.applyChoice(0, 9);
    services.applyChoice(1, 2);
    services.applyChoice(2, 1);

    const auto settings = trackBank.projectScaleSettings();
    assert(settings.root == 9);
    assert(settings.type == StepSequencerScaleType::NaturalMinor);
    assert(settings.mode == StepSequencerScaleConstraintMode::ConstrainNearest);
    assert(services.currentChoiceIndex(0) == 9);
    assert(services.currentChoiceIndex(1) == 2);
    assert(services.currentChoiceIndex(2) == 1);

    std::cout << "[PASS] test_project_scale_choices_update_track_bank\n";
}

void test_project_scale_choices_are_clamped() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::handler::SequencerSettingsDomainServices services{
        core::handler::SequencerSettingsDomainServices::StateRefs{
            sequencer,
            trackBank,
        }
    };

    services.applyChoice(0, 99);
    services.applyChoice(1, 99);
    services.applyChoice(2, 99);

    const auto settings = trackBank.projectScaleSettings();
    assert(settings.root == 11);
    assert(settings.type == StepSequencerScaleType::WholeTone);
    assert(settings.mode == StepSequencerScaleConstraintMode::ConstrainDown);

    std::cout << "[PASS] test_project_scale_choices_are_clamped\n";
}

void test_project_scale_invalidates_inherited_active_telemetry() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::handler::SequencerSettingsDomainServices services{
        core::handler::SequencerSettingsDomainServices::StateRefs{
            sequencer,
            trackBank,
        }
    };

    sequencer.cycleVariationTelemetry.validMask.setBit(0, true);
    const uint32_t before = sequencer.variationTelemetryRevision.get();

    services.applyChoice(1, 1);

    assert(!sequencer.cycleVariationTelemetry.validMask.test(0));
    assert(sequencer.variationTelemetryRevision.get() == before + 1U);

    std::cout << "[PASS] test_project_scale_invalidates_inherited_active_telemetry\n";
}

void test_project_scale_keeps_override_active_telemetry() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::handler::SequencerSettingsDomainServices services{
        core::handler::SequencerSettingsDomainServices::StateRefs{
            sequencer,
            trackBank,
        }
    };

    sequencer.setPatternScalePolicy(core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE);
    sequencer.cycleVariationTelemetry.validMask.setBit(0, true);
    const uint32_t before = sequencer.variationTelemetryRevision.get();

    services.applyChoice(1, 1);

    assert(sequencer.cycleVariationTelemetry.validMask.test(0));
    assert(sequencer.variationTelemetryRevision.get() == before);

    std::cout << "[PASS] test_project_scale_keeps_override_active_telemetry\n";
}

void test_pattern_pitch_settings_override_copies_project_before_local_edits() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::handler::SequencerSettingsDomainServices projectServices{
        core::handler::SequencerSettingsDomainServices::StateRefs{
            sequencer,
            trackBank,
        }
    };
    core::handler::PatternPitchSettingsDomainServices patternServices{
        core::handler::PatternPitchSettingsDomainServices::StateRefs{
            sequencer,
            trackBank,
        }
    };

    projectServices.applyChoice(0, 2);
    projectServices.applyChoice(1, 2);
    projectServices.applyChoice(2, 1);
    assert(patternServices.choiceCount(1) == 0);

    patternServices.applyChoice(0, 1);
    assert(sequencer.scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE);
    assert(patternServices.choiceCount(1) == 12);
    assert(patternServices.currentChoiceIndex(1) == 2);
    assert(patternServices.currentChoiceIndex(2) == 2);

    patternServices.applyChoice(1, 9);
    patternServices.applyChoice(2, 13);
    assert(sequencer.scaleOverride.root == 9);
    assert(sequencer.scaleOverride.type == StepSequencerScaleType::WholeTone);

    patternServices.applyChoice(3, 1);
    assert(sequencer.pitchEditMode ==
           core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES);

    patternServices.applyChoice(0, 0);
    assert(sequencer.scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT);
    assert(patternServices.choiceCount(1) == 0);

    std::cout << "[PASS] test_pattern_pitch_settings_override_copies_project_before_local_edits\n";
}

}  // namespace

int main() {
    test_project_scale_choices_update_track_bank();
    test_project_scale_choices_are_clamped();
    test_project_scale_invalidates_inherited_active_telemetry();
    test_project_scale_keeps_override_active_telemetry();
    test_pattern_pitch_settings_override_copies_project_before_local_edits();
    std::cout << "All SequencerSettingsDomainServices tests passed\n";
    return 0;
}
