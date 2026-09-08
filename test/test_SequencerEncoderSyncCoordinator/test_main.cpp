#include <cassert>
#include <cmath>
#include <iostream>

#include <config/InputIDs.hpp>
#include <config/Timing.hpp>
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
#include "../support/SequencerHistoryTransactionAssertions.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"

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
                  state.sequencerClips,
              },
              encoders
          ) {
        assert(sync.bind());
        state.activeView.set(core::ui::ViewType::CLIPS);
        state.sequencer.clipWorkspace.enterPattern(0U, 0U);
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
    h.navigationFocus.set(core::state::StructureNavigationFocus::LANE);
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

void expects_drum_pattern_defaults_to_own_opt_only_while_open() {
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
    drumUi.openPatternDefaults();
    drumUi.patternDefaultField =
        core::state::sequencer::DrumPatternDefaultField::DIVISION;

    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) ==
           input_utils::STEPS_PER_BEAT_CHOICES.size());

    drumUi.cancelSelector();
    h.encoderHw.setDiscreteSteps(OPT_ENCODER_ID, 7U);
    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) == 7U);

    std::cout <<
        "[PASS] expects_drum_pattern_defaults_to_own_opt_only_while_open\n";
}

void expects_clip_quick_property_to_publish_its_exact_opt_contract() {
    SequencerEncoderSyncHarness h;
    auto& workspace = h.state.sequencer.clipWorkspace;
    workspace.reset(0U);
    workspace.focus(0U, 0U);
    workspace.showQuickSelector();
    workspace.moveQuickAction(1);
    workspace.armQuickProperty(0U);
    assert(workspace.quickAction ==
           core::state::sequencer::ClipWorkspaceQuickAction::LENGTH);
    assert(h.state.sequencerClips.setClipBehavior({0U, 0U}, {
        .length = 8U,
        .follow = core::state::sequencer::SequencerLauncherFollowChoice::NONE,
        .quantization =
            core::state::sequencer::SequencerLauncherFollowQuantization::GLOBAL,
    }));

    test_support::drainNotifications();
    h.sync.syncNow();

    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) == 17U);
    assert(almostEqual(
        h.encoderHw.getPosition(OPT_ENCODER_ID),
        input_utils::indexToNormalized(8, 17)
    ));

    std::cout <<
        "[PASS] expects_clip_quick_property_to_publish_its_exact_opt_contract\n";
}

void expects_clip_matrix_to_borrow_opt_only_for_quick_editing() {
    SequencerEncoderSyncHarness h;
    auto& workspace = h.state.sequencer.clipWorkspace;
    workspace.reset(0U);
    workspace.focus(0U, 0U);

    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getMode(OPT_ENCODER_ID) ==
           oc::interface::EncoderMode::RELATIVE);
    assert(almostEqual(h.encoderHw.getDelta(OPT_ENCODER_ID), 1.0f));

    workspace.showQuickSelector();
    workspace.moveQuickAction(1);
    workspace.armQuickProperty(0U);
    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getMode(OPT_ENCODER_ID) ==
           oc::interface::EncoderMode::NORMALIZED);

    workspace.updateQuickFeedback(
        Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS
    );
    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getMode(OPT_ENCODER_ID) ==
           oc::interface::EncoderMode::RELATIVE);
    assert(almostEqual(h.encoderHw.getDelta(OPT_ENCODER_ID), 1.0f));

    std::cout <<
        "[PASS] expects_clip_matrix_to_borrow_opt_only_for_quick_editing\n";
}

void expects_clip_behavior_editor_to_follow_the_focused_field() {
    SequencerEncoderSyncHarness h;
    auto& workspace = h.state.sequencer.clipWorkspace;
    workspace.reset(0U);
    workspace.focus(0U, 0U);
    workspace.openEditor(
        core::state::sequencer::ClipWorkspaceEditor::CLIP_BEHAVIOR,
        8U,
        static_cast<uint8_t>(
            core::state::sequencer::SequencerLauncherFollowChoice::NEXT),
        static_cast<uint8_t>(
            core::state::sequencer::SequencerLauncherFollowQuantization::BAR)
    );

    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) == 17U);
    assert(almostEqual(
        h.encoderHw.getPosition(OPT_ENCODER_ID),
        input_utils::indexToNormalized(8, 17)
    ));

    workspace.moveEditorField(1);
    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) ==
           core::state::sequencer::sequencerLauncherFollowChoiceCount());
    assert(almostEqual(
        h.encoderHw.getPosition(OPT_ENCODER_ID),
        input_utils::indexToNormalized(
            core::state::sequencer::sequencerLauncherFollowChoiceIndex(
                core::state::sequencer::SequencerLauncherFollowChoice::NEXT),
            core::state::sequencer::sequencerLauncherFollowChoiceCount()
        )
    ));

    workspace.moveEditorField(1);
    test_support::drainNotifications();
    h.sync.syncNow();
    assert(h.encoderHw.getDiscreteSteps(OPT_ENCODER_ID) == 3U);
    assert(almostEqual(
        h.encoderHw.getPosition(OPT_ENCODER_ID),
        input_utils::indexToNormalized(2, 3)
    ));

    std::cout <<
        "[PASS] expects_clip_behavior_editor_to_follow_the_focused_field\n";
}

// History restores the musical owner; deferred encoder bindings must see it
// without requiring a second editable copy in the active bank slot.
void expects_history_replay_to_publish_only_the_canonical_owner() {
    namespace seq = core::state::sequencer;
    namespace tx = test_support::sequencer_transaction;
    for (const auto storage : {seq::SequencerHistoryPatternStorage::FullGraph,
                               seq::SequencerHistoryPatternStorage::FlatOnly}) {
        SequencerEncoderSyncHarness h;
        auto& editor = h.state.sequencer;
        auto& tracks = h.state.sequencerTracks;
        tracks.syncSharedTrackState(3U, 0U);
        editor.patternQuickControls.focusedItem.set(seq::PatternQuickControlItem::SWING);
        assert(seq::ensureGraphRoot(editor.pattern));
        auto* lanes = seq::ensureSequencerCcLaneBank(editor.pattern);
        seq::SequencerCcLaneDraft draft{};
        draft.destination.controller = 74U;
        assert(lanes && seq::createSequencerCcLane(*lanes, 0U, draft).changed());
        assert(seq::setSequencerCcLaneEvent(*lanes, 0U, 0U, 99U).changed());
        editor.pattern.bumpCcLaneRevision();
        const auto capture = [&](seq::SequencerHistoryPatternSnapshot& snapshot) {
            if (storage == seq::SequencerHistoryPatternStorage::FullGraph)
                assert(seq::captureHistorySnapshot(editor, snapshot));
            else seq::captureFlatHistorySnapshot(editor, snapshot);
        };
        seq::SequencerHistoryPatternSnapshot before, after;
        capture(before);
        assert(editor.setPatternSwingOffsetPercent(17));
        capture(after);
        assert(tx::commitAdmittedPattern(h.state.sequencerHistory,
            std::move(before), std::move(after), {}, storage));
        test_support::drainNotifications();
        const auto scratchSwing = tracks.track(0U).swingOffsetPercent.get();
        const auto scratchRevision = tracks.track(0U).patternTimingRevision.get();
        const auto allocations = storage == seq::SequencerHistoryPatternStorage::FullGraph ? 2U : 0U;
        for (bool redo : {false, true}) {
            seq::SequencerHistoryPatternSnapshot liveBefore;
            assert(seq::captureHistorySnapshot(editor, liveBefore));
            const auto invariant = tx::captureStateInvariant(h.state);
            for (size_t ordinal = 1; ordinal <= allocations; ++ordinal) {
                {
                    core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
                    assert(!(redo ? h.state.redoSequencerHistory() : h.state.undoSequencerHistory()));
                    tx::assertFailureConsumed(ordinal);
                    tx::assertStateInvariant(h.state, invariant);
                }
                tx::assertMusicalSnapshot(h.state, liveBefore);
            }
            {
                core::app::testing::ScopedExtmemAllocationFailure failure(allocations + 1U);
                assert(redo ? h.state.redoSequencerHistory() : h.state.undoSequencerHistory());
                std::cout << "Replay allocations: " << core::app::testing::extmemAllocationAttempt
                          << " expected " << allocations << '\n';
                tx::assertMaxPlusOneStillArmed(allocations);
            }
            // No syncNow(): exercise the real deferred watcher.
            test_support::drainNotifications();
            assert(editor.pattern.swingOffsetPercent.get() == (redo ? 17 : 0));
            assert(almostEqual(h.encoderHw.getPosition(OPT_ENCODER_ID),
                input_utils::quickControlToNormalized(editor, seq::PatternQuickControlItem::SWING)));
            assert(editor.pattern.graph && editor.pattern.ccLanes->lanes[0].values[0] == 99U);
            assert(!tracks.track(0U).graph && !tracks.track(0U).ccLanes);
            assert(tracks.track(0U).swingOffsetPercent.get() == scratchSwing);
            assert(tracks.track(0U).patternTimingRevision.get() == scratchRevision);
        }
        // Switch away before replay: the same logical target is now in the bank.
        assert(seq::switchActiveTrack(tracks, editor, 1U));
        const auto otherSwing = editor.pattern.swingOffsetPercent.get();
        assert(h.state.undoSequencerHistory());
        assert(editor.pattern.swingOffsetPercent.get() == otherSwing);
        assert(tracks.track(0U).swingOffsetPercent.get() == 0);
        assert(h.state.redoSequencerHistory());
        assert(seq::switchActiveTrack(tracks, editor, 0U));
        test_support::drainNotifications();
        assert(editor.pattern.swingOffsetPercent.get() == 17);
        assert(editor.pattern.graph && editor.pattern.ccLanes->lanes[0].values[0] == 99U);
        // A new real coalesced edit must not require the old active mirror.
        const auto previousNote = editor.pattern.note[0U];
        const auto* graph = editor.pattern.graph.get();
        const auto* cc = editor.pattern.ccLanes.get();
        assert(seq::sequencerHistoryOpenAccepted(
            h.state.beginOrContinueSequencerPatternHistoryCoalescing(
                0U, StepProperty::NOTE, 100U, seq::SequencerCoalescedPatternPayloadPlan::FlatOnly)));
        assert(editor.setStepNoteAt(0U, 69U));
        assert(h.state.sealSequencerPatternHistoryCoalescing(true));
        assert(h.state.commitSequencerPatternHistoryCoalescing());
        assert(h.state.undoSequencerHistory());
        assert(editor.pattern.note[0U] == previousNote);
        assert(editor.pattern.graph.get() == graph && editor.pattern.ccLanes.get() == cc);
        assert(editor.pattern.ccLanes->lanes[0].values[0] == 99U);
    }
    std::cout << "[PASS] canonical history replay, OOM, deferred encoder and track navigation\n";
}

}  // namespace

int main() {
    std::cout << std::unitbuf;
    expects_history_replay_to_publish_only_the_canonical_owner();
    expects_pattern_focus_syncs_opt_to_pattern_dimension();
    expects_step_focus_syncs_opt_to_focused_step_property();
    expects_pattern_focus_to_replace_the_two_state_opt_contract();
    expects_overlay_release_to_reapply_the_main_opt_contract();
    expects_drum_lane_editor_to_keep_opt_authority_over_the_visible_grid();
    expects_drum_pattern_defaults_to_own_opt_only_while_open();
    expects_clip_quick_property_to_publish_its_exact_opt_contract();
    expects_clip_matrix_to_borrow_opt_only_for_quick_editing();
    expects_clip_behavior_editor_to_follow_the_focused_field();

    std::cout << "SequencerEncoderSyncCoordinator tests passed\n";
    return 0;
}
