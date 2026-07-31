#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/handler/settings/SequencerSettingsDomainServices.hpp"
#include "../../src/handler/settings/SequencerSettingsHandler.hpp"
#include "../../src/handler/sequencer/PatternPitchSettingsDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleType;
using ChordBasis =
    oc::note::sequencer::StepSequencerChordIntervalBasis;
using ChordHarmony =
    oc::note::sequencer::StepSequencerChordHarmony;
using ChordSpec =
    oc::note::sequencer::StepSequencerChordSpec;
using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

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

struct SequencerSettingsHandlerHarness {
    static constexpr oc::type::ScopeID SETTINGS_SCOPE = 811;
    static constexpr oc::type::ScopeID SELECTOR_SCOPE = 812;

    test_support::CoreStorages storages;
    core::state::CoreState state;
    core::handler::SequencerSettingsDomainServices services;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::SequencerSettingsHandler handler;

    SequencerSettingsHandlerHarness()
        : state(storages.settings)
        , services(core::handler::SequencerSettingsDomainServices::StateRefs{
              state.sequencer,
              state.sequencerTracks,
          })
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::SequencerSettingsHandler::StateRefs{
                      state.sequencerSettings,
                      state.viewSelector,
                      state.sequencer,
                      state.sequencerTracks,
                      core::handler::SequencerHistoryDomainServices::fromCoreState(state),
                  },
                  services,
                  overlays,
                  encoders,
                  buttons,
                  SETTINGS_SCOPE,
                  SELECTOR_SCOPE) {
        overlays.setActiveViewProvider([]() { return SETTINGS_SCOPE; });
        overlays.registerCleanup(core::ui::OverlayType::SEQUENCER_SETTINGS, SETTINGS_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::SEQUENCER_SETTINGS_SELECTOR, SELECTOR_SCOPE);
        g_now_ms = 0;
    }

    void openSettings() {
        state.sequencerSettings.openOverlay();
        overlays.show(core::ui::OverlayType::SEQUENCER_SETTINGS, false);
        assert(state.sequencerSettings.visible.get());
    }

    void press(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, true);
        eventBus.emit(oc::core::event::ButtonPressEvent(buttonId, true));
    }

    void release(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, false);
        eventBus.emit(oc::core::event::ButtonReleaseEvent(buttonId));
    }

    void tap(Config::ButtonID id) {
        press(id);
        release(id);
    }

    void turn(Config::EncoderID id, float delta) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, delta);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, delta));
    }
};

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
    assert(services.currentChoiceIndex(0) == 5);
    assert(services.currentChoiceIndex(1) == 3);
    assert(services.currentChoiceIndex(2) == 1);

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

void test_project_scale_boundary_projects_inherited_chords() {
    core::state::sequencer::SequencerState sequencer;
    core::state::sequencer::SequencerTrackBankState trackBank;
    core::handler::SequencerSettingsDomainServices services{
        core::handler::SequencerSettingsDomainServices::StateRefs{
            sequencer,
            trackBank,
        }
    };

    services.applyChoice(2, 0);
    authorScalePolicyChord(
        sequencer.pattern,
        customChord(3, 5)
    );

    const auto projection = services.applyChoice(2, 1);

    const auto& spec = rootChord(sequencer.pattern);
    assert(spec.intervalBasis() == ChordBasis::ScaleDegrees);
    assert(spec.customInterval(1) == 2);
    assert(spec.customInterval(2) == 3);
    assert(projection.changed == 1);
    assert(projection.exact == 1);
    assert(!projection.hasAdaptations());

    std::cout
        << "[PASS] Project scale boundary projects inherited chords\n";
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

void test_project_scale_settings_are_undoable_through_handler() {
    SequencerSettingsHandlerHarness h;
    h.openSettings();

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencerSettings.flowPhase.get() ==
           core::state::SequencerSettingsFlowPhase::VALUE_SELECTOR);

    for (uint8_t i = 0; i < 5; ++i) {
        h.turn(Config::EncoderID::NAV, 1.0f);
    }
    h.tap(Config::ButtonID::NAV);

    assert(h.state.sequencerTracks.projectScaleSettings().root == 10);
    assert(h.state.sequencerHistory.undoCount(core::state::sequencer::SequencerHistoryScope::FullBank) == 1);

    h.tap(Config::ButtonID::LEFT_TOP);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.projectScaleSettings().root == 5);
    assert(h.state.sequencerHistory.redoCount(core::state::sequencer::SequencerHistoryScope::FullBank) == 1);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencerTracks.projectScaleSettings().root == 10);

    std::cout << "[PASS] test_project_scale_settings_are_undoable_through_handler\n";
}

void test_project_chord_projection_is_one_undoable_transaction() {
    SequencerSettingsHandlerHarness h;
    h.services.applyChoice(2, 0);
    authorScalePolicyChord(
        h.state.sequencer.pattern,
        customChord(4, 7)
    );
    const auto before = rootChord(h.state.sequencer.pattern);
    assert(
        before.intervalBasis() ==
        ChordBasis::ChromaticSemitones
    );

    h.openSettings();
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);

    assert(
        h.state.sequencerTracks.projectScaleSettings().mode ==
        StepSequencerScaleConstraintMode::ConstrainNearest
    );
    assert(
        rootChord(h.state.sequencer.pattern).intervalBasis() ==
        ChordBasis::ScaleDegrees
    );
    assert(h.state.sequencer.historyFeedback.visible.get());
    assert(
        h.state.sequencerHistory.undoCount(
            core::state::sequencer::
                SequencerHistoryScope::FullBank
        ) == 1
    );

    assert(h.state.undoSequencerHistory());
    assert(
        h.state.sequencerTracks.projectScaleSettings().mode ==
        StepSequencerScaleConstraintMode::Free
    );
    assert(
        rootChord(h.state.sequencer.pattern).intervalBasis() ==
        ChordBasis::ChromaticSemitones
    );

    assert(h.state.redoSequencerHistory());
    assert(
        h.state.sequencerTracks.projectScaleSettings().mode ==
        StepSequencerScaleConstraintMode::ConstrainNearest
    );
    assert(
        rootChord(h.state.sequencer.pattern).intervalBasis() ==
        ChordBasis::ScaleDegrees
    );

    std::cout
        << "[PASS] Project chord projection is one undo transaction\n";
}

}  // namespace

int main() {
    test_project_scale_choices_update_track_bank();
    test_project_scale_choices_are_clamped();
    test_project_scale_invalidates_inherited_active_telemetry();
    test_project_scale_keeps_override_active_telemetry();
    test_project_scale_boundary_projects_inherited_chords();
    test_pattern_pitch_settings_override_copies_project_before_local_edits();
    test_pattern_return_to_project_projects_local_chords();
    test_pattern_pitch_context_projects_formula_at_the_mode_boundary();
    test_project_scale_settings_are_undoable_through_handler();
    test_project_chord_projection_is_one_undoable_transaction();
    std::cout << "All SequencerSettingsDomainServices tests passed\n";
    return 0;
}
