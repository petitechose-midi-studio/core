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
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleType;
using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
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
        : state(storages.settings,
                storages.macroWorkspace,
                storages.macroLibrary,
                storages.sequencerWorkspace,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
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

    patternServices.applyChoice(3, 1);
    assert(sequencer.pattern.pitchEditMode ==
           core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES);

    patternServices.applyChoice(0, 0);
    assert(sequencer.pattern.scalePolicy ==
           core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT);
    assert(patternServices.choiceCount(1) == 0);

    std::cout << "[PASS] test_pattern_pitch_settings_override_copies_project_before_local_edits\n";
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

}  // namespace

int main() {
    test_project_scale_choices_update_track_bank();
    test_project_scale_choices_are_clamped();
    test_project_scale_invalidates_inherited_active_telemetry();
    test_project_scale_keeps_override_active_telemetry();
    test_pattern_pitch_settings_override_copies_project_before_local_edits();
    test_project_scale_settings_are_undoable_through_handler();
    std::cout << "All SequencerSettingsDomainServices tests passed\n";
    return 0;
}
