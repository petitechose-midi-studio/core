#include <cassert>
#include <cmath>
#include <iostream>

#include <config/InputIDs.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/context/standalone/SequencerEncoderSyncCoordinator.hpp"
#include "../../src/handler/sequencer/SequencerInputUtils.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerQuickControls.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

uint32_t mockTimeMs() {
    return 0;
}

using StepProperty = core::state::sequencer::StepProperty;
namespace input_utils = core::handler::sequencer::input_utils;
constexpr auto OPT_ENCODER_ID = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);

bool almostEqual(float lhs, float rhs) {
    return std::fabs(lhs - rhs) < 0.0005f;
}

struct SequencerEncoderSyncHarness {
    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers> navigationFocus;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    test_support::TestEncoderHardware encoderHw;
    oc::api::EncoderAPI encoders;
    core::context::standalone::SequencerEncoderSyncCoordinator sync;

    SequencerEncoderSyncHarness()
        : state(storages.settings,
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , navigationFocus(core::state::StructureNavigationFocus::PAGE)
        , inputBinding(eventBus, mockTimeMs)
        , encoders(inputBinding, encoderHw)
        , sync(
              core::context::standalone::SequencerEncoderSyncCoordinator::StateRefs{
                  state.overlays,
                  state.activeView,
                  navigationFocus,
                  state.trackNavigation,
                  state.sequencer,
                  state.sequencerTracks,
              },
              encoders
          ) {
        state.activeView.set(core::ui::ViewType::SEQUENCER);
    }
};

void expects_pattern_focus_syncs_opt_to_pattern_dimension() {
    SequencerEncoderSyncHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.state.sequencer.patternQuickControls.focusedItem.set(
        core::state::sequencer::PatternQuickControlItem::SWING
    );
    h.state.sequencer.setPatternSwingOffsetPercent(17);

    h.sync.syncNow();

    const auto config = input_utils::encoderConfigForQuickControl(
        core::state::sequencer::PatternQuickControlItem::SWING
    );
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) == config.discreteSteps);
    assert(h.encoderHw.getDiscreteTicksPerStep(OPT_ENCODER_ID) ==
           config.discreteTicksPerStep);
    assert(almostEqual(
        h.encoderHw.getPosition(OPT_ENCODER_ID),
        input_utils::quickControlToNormalized(
            h.state.sequencer,
            core::state::sequencer::PatternQuickControlItem::SWING
        )
    ));

    std::cout << "[PASS] expects_pattern_focus_syncs_opt_to_pattern_dimension\n";
}

void expects_step_focus_syncs_opt_to_focused_step_property() {
    SequencerEncoderSyncHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.focusedStep.set(2);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.setStepVelocityAt(2, 96);

    h.sync.syncNow();

    const auto config = input_utils::encoderConfigForProperty(StepProperty::VELOCITY);
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) == config.discreteSteps);
    assert(h.encoderHw.getDiscreteTicksPerStep(OPT_ENCODER_ID) ==
           config.discreteTicksPerStep);
    assert(almostEqual(
        h.encoderHw.getPosition(OPT_ENCODER_ID),
        core::state::sequencer::activeContentStepPropertyToNormalized(
            h.state.sequencer,
            2,
            StepProperty::VELOCITY,
            h.state.sequencer.pattern.pitchEditMode,
            core::state::sequencer::resolveEffectiveScaleSettings(
                h.state.sequencerTracks.projectScaleSettings(),
                h.state.sequencer.pattern.scalePolicy,
                h.state.sequencer.pattern.scaleOverride
            )
        )
    ));

    std::cout << "[PASS] expects_step_focus_syncs_opt_to_focused_step_property\n";
}

void expects_track_selection_policy_blocks_opt_sync() {
    SequencerEncoderSyncHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.state.trackNavigation.selection.active.set(true);
    h.encoderHw.setPosition(OPT_ENCODER_ID, 0.321f);

    h.sync.syncNow();

    assert(almostEqual(h.encoderHw.getPosition(OPT_ENCODER_ID), 0.321f));

    std::cout << "[PASS] expects_track_selection_policy_blocks_opt_sync\n";
}

}  // namespace

int main() {
    expects_pattern_focus_syncs_opt_to_pattern_dimension();
    expects_step_focus_syncs_opt_to_focused_step_property();
    expects_track_selection_policy_blocks_opt_sync();

    std::cout << "SequencerEncoderSyncCoordinator tests passed\n";
    return 0;
}
