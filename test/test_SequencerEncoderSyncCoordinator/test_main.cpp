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
#include "../support/NotificationTestUtils.hpp"

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
        : state(storages.settings)
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
        assert(sync.bind());
        state.activeView.set(core::ui::ViewType::SEQUENCER);
        test_support::drainNotifications();
    }
};

void expects_pattern_focus_syncs_opt_to_pattern_dimension() {
    SequencerEncoderSyncHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.state.sequencer.patternQuickControls.focusedItem.set(
        core::state::sequencer::PatternQuickControlItem::SWING
    );
    h.state.sequencer.setPatternSwingOffsetPercent(17);

    test_support::drainNotifications();
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
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(2);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.setStepVelocityAt(2, 96);

    test_support::drainNotifications();
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

void expects_pattern_focus_to_replace_the_two_state_opt_contract() {
    SequencerEncoderSyncHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.stepStatePropertyActive.set(true);
    test_support::drainNotifications();
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) == 2U);

    h.state.sequencer.patternQuickControls.focusedItem.set(
        core::state::sequencer::PatternQuickControlItem::SWING
    );
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    test_support::drainNotifications();

    const auto config = input_utils::encoderConfigForQuickControl(
        core::state::sequencer::PatternQuickControlItem::SWING
    );
    assert(config.discreteSteps > 2U);
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) == config.discreteSteps);
    assert(h.encoderHw.getDiscreteTicksPerStep(OPT_ENCODER_ID) ==
           config.discreteTicksPerStep);

    std::cout << "[PASS] expects_pattern_focus_to_replace_the_two_state_opt_contract\n";
}

void expects_overlay_release_to_reapply_the_main_opt_contract() {
    SequencerEncoderSyncHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.state.sequencer.patternQuickControls.focusedItem.set(
        core::state::sequencer::PatternQuickControlItem::SWING
    );
    test_support::drainNotifications();
    h.sync.syncNow();

    const auto config = input_utils::encoderConfigForQuickControl(
        core::state::sequencer::PatternQuickControlItem::SWING
    );
    h.state.overlays.show(core::ui::OverlayType::SEQ_TRACK_EDIT);
    test_support::drainNotifications();
    h.encoderHw.setMode(OPT_ENCODER_ID, oc::interface::EncoderMode::RAW);
    h.encoderHw.setBounds(OPT_ENCODER_ID, -1.0f, 1.0f);
    h.encoderHw.setDiscreteSteps(OPT_ENCODER_ID, 2U);
    h.state.overlays.hide();
    test_support::drainNotifications();

    assert(h.encoderHw.getMode(OPT_ENCODER_ID) ==
           oc::interface::EncoderMode::NORMALIZED);
    assert(almostEqual(h.encoderHw.getBoundsMin(OPT_ENCODER_ID), 0.0f));
    assert(almostEqual(h.encoderHw.getBoundsMax(OPT_ENCODER_ID), 1.0f));
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) == config.discreteSteps);

    std::cout << "[PASS] expects_overlay_release_to_reapply_the_main_opt_contract\n";
}

void expects_drum_lane_editor_to_keep_opt_authority_over_the_visible_grid() {
    SequencerEncoderSyncHarness h;
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    assert(h.state.sequencerTracks.setTrackKind(
        0U,
        core::state::sequencer::SequencerTrackKind::DRUM,
        true,
        core::state::sequencer::DrumKitPreset::GENERAL_MIDI
    ));
    auto& drumUi = h.state.sequencer.drumSequencer;
    drumUi.bindTrack(
        0U,
        h.state.sequencerTracks.drumTrack(0U),
        h.state.sequencerTracks
    );
    drumUi.enterGrid();
    drumUi.dimension = core::state::sequencer::DrumSequencerDimension::LENGTH;
    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) ==
           core::state::sequencer::DRUM_MAX_STEPS);

    assert(drumUi.openLaneEditor(false));
    h.state.overlays.show(core::ui::OverlayType::SEQ_DRUM_LANE_EDIT);

    // Simulate the Lane Editor's ICON field contract. A synchronizer pass while
    // the overlay is visible must leave the overlay-owned configuration intact.
    h.encoderHw.setMode(OPT_ENCODER_ID, oc::interface::EncoderMode::NORMALIZED);
    h.encoderHw.setBounds(OPT_ENCODER_ID, 0.0f, 1.0f);
    h.encoderHw.setDiscreteSteps(
        OPT_ENCODER_ID,
        static_cast<uint8_t>(core::state::sequencer::DrumLaneIcon::COUNT)
    );
    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) ==
           static_cast<uint8_t>(core::state::sequencer::DrumLaneIcon::COUNT));

    drumUi.cancelLaneEditor();
    h.state.overlays.hide();
    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) ==
           core::state::sequencer::DRUM_MAX_STEPS);

    std::cout <<
        "[PASS] expects_drum_lane_editor_to_keep_opt_authority_over_the_visible_grid\n";
}

}  // namespace

int main() {
    expects_pattern_focus_syncs_opt_to_pattern_dimension();
    expects_step_focus_syncs_opt_to_focused_step_property();
    expects_pattern_focus_to_replace_the_two_state_opt_contract();
    expects_overlay_release_to_reapply_the_main_opt_contract();
    expects_drum_lane_editor_to_keep_opt_authority_over_the_visible_grid();

    std::cout << "SequencerEncoderSyncCoordinator tests passed\n";
    return 0;
}
