#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include <config/App.hpp>
#include <config/Timing.hpp>

#include "../../src/handler/common/SharedTrackDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "../../src/handler/sequencer/SequencerPatternEditorHandler.hpp"
#include "../../src/handler/sequencer/SequencerStepHandler.hpp"
#include "../../src/handler/sequencer/SequencerStructureNavigationWorkflow.hpp"
#include "../../src/handler/transport/TransportHandler.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/ProjectControlTestUtils.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

void configureProjectTrackFixture(
    core::state::CoreState& state,
    uint8_t track,
    uint8_t midiChannel,
    bool muted = false
) {
    assert(track < core::state::project::PROJECT_TRACK_COUNT);
    state.projectTracks.authored.midiChannels[track] = midiChannel;
    const uint16_t bit = static_cast<uint16_t>(1U << track);
    if (muted) {
        state.projectTracks.authored.mutedMask |= bit;
    } else {
        state.projectTracks.authored.mutedMask &= static_cast<uint16_t>(~bit);
    }
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

struct SequencerStepHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 501;
    static constexpr oc::type::ScopeID PATTERN_EDITOR_SCOPE = 502;

    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers> navigationFocus;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::state::sequencer::SequencerPatternRandomizeSession patternRandomize;
    core::handler::SequencerPatternEditorHandler patternEditorHandler;
    core::handler::SequencerStepHandler handler;
    core::handler::TransportHandler transportHandler;
    core::handler::SequencerPatternQuickControlsHandler quickControlsHandler;

    SequencerStepHarness()
        : state(storages.settings)
        , navigationFocus(core::state::StructureNavigationFocus::PAGE)
        , inputBinding(eventBus, mockTimeMs, Config::Input::CONFIG)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , patternEditorHandler(
              core::handler::SequencerPatternEditorHandler::StateRefs{
                  state.sequencer,
                  state.sequencerTracks,
                  patternRandomize,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              overlays,
              encoders,
              buttons,
              SEQUENCER_SCOPE,
              PATTERN_EDITOR_SCOPE
          )
        , handler(
              core::handler::SequencerStepHandler::StateRefs{
                  state.sequencer,
                  state.sequencerTracks,
                  navigationFocus,
                  state.trackNavigation,
                  state.projectNavigation,
                  state.projectTracks,
                  core::state::project::ProjectTrackDomainServices::fromCoreState(
                      state
                  ),
                  state.structureClipboard,
                  core::handler::SharedTrackDomainServices::fromCoreState(state),
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
                  state.pages,
                  &state.sequencerTrackActivations,
                  &state.statusBar,
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE
          )
        , transportHandler(
              core::handler::TransportHandler::StateRefs{state.statusBar},
              buttons
          )
        , quickControlsHandler(
              core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
                  navigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE
          ) {
        g_now_ms = 0;
        oc::time::setProvider(mockTimeMs);
        overlays.setActiveViewProvider([]() { return SEQUENCER_SCOPE; });
        overlays.registerCleanup(
            core::ui::OverlayType::SEQ_PATTERN_EDIT,
            PATTERN_EDITOR_SCOPE
        );
        handler.attachPatternEditorHandler(patternEditorHandler);
        handler.update(g_now_ms);
    }

    void tick(uint32_t nowMs) {
        g_now_ms = nowMs;
        inputBinding.processTick();
        handler.update(g_now_ms);
        patternEditorHandler.update(g_now_ms);
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

    void advance(uint32_t ms) {
        g_now_ms += ms;
        inputBinding.processTick();
        handler.update(g_now_ms);
        patternEditorHandler.update(g_now_ms);
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }
};

bool rootStepHasMicroSequence(const SequencerStepHarness& h, uint8_t step) {
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph == nullptr) return false;
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    if (nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE);
}

bool patternRootStepHasMicroSequence(
    const core::state::sequencer::SequencerPatternState& pattern,
    uint8_t step
) {
    const auto* graph = core::state::sequencer::graphView(pattern);
    if (graph == nullptr) return false;
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    if (nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(
        oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE
    );
}

const oc::note::sequencer::StepSequencerStepNode* rootStepNode(
    const SequencerStepHarness& h,
    uint8_t step
) {
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph == nullptr) return nullptr;
    return graph->stepNode(core::state::sequencer::rootStepNodeId(step));
}

void createRootMicroSequence(SequencerStepHarness& h, uint8_t step) {
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    const auto result = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        nodeId,
        2
    );
    assert(result.ok);
}

bool nodeHasCycleStates(const SequencerStepHarness& h, uint16_t nodeId) {
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph == nullptr || nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(oc::note::sequencer::STEP_NODE_CYCLE_SET);
}

void test_child_creation_draft_apply_and_back_decisions() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    auto opened = core::state::sequencer::openOrCreateActiveContentChild(
        h.state.sequencer,
        2,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    assert(opened.opened && opened.draft);
    assert(!rootStepHasMicroSequence(h, 2));
    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer,
        0,
        core::state::sequencer::StepProperty::NOTE,
        1.0f,
        h.state.sequencer.pattern.pitchEditMode,
        {}
    ));
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(rootStepHasMicroSequence(h, 2));
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(h.state.sequencerHistory.undoCount() == 1);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));

    opened = core::state::sequencer::openOrCreateActiveContentChild(
        h.state.sequencer,
        3,
        core::state::sequencer::StepContentChildKind::CYCLE_STATES,
        core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
    );
    assert(opened.opened && opened.draft);
    assert(!h.state.sequencer.stepContentDraft.modified());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(!nodeHasCycleStates(
        h,
        core::state::sequencer::rootStepNodeId(3)
    ));
    assert(h.state.sequencerHistory.undoCount() == 1);

    opened = core::state::sequencer::openOrCreateActiveContentChild(
        h.state.sequencer,
        3,
        core::state::sequencer::StepContentChildKind::CYCLE_STATES,
        core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
    );
    assert(opened.opened && opened.draft);
    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer,
        0,
        core::state::sequencer::StepProperty::VELOCITY,
        1.0f,
        h.state.sequencer.pattern.pitchEditMode,
        {}
    ));
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(
        h.state.sequencer.stepContentDraft.exitChoice.get() ==
        core::state::sequencer::SequencerStepContentDraftExitChoice::SAVE
    );
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(
        h.state.sequencer.stepContentDraft.exitChoice.get() ==
        core::state::sequencer::SequencerStepContentDraftExitChoice::DISCARD
    );
    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(!nodeHasCycleStates(
        h,
        core::state::sequencer::rootStepNodeId(3)
    ));
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_child_creation_draft_apply_and_back_decisions\n";
}

void holdPatternQuickControls(SequencerStepHarness& h) {
    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
}

void focusTrackNavigation(SequencerStepHarness& h) {
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.state.trackNavigation.syncPreviewTrack(
        h.state.sequencerTracks.activeTrackIndex()
    );
}

void openPatternEditor(SequencerStepHarness& h) {
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.patternEditor.active.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_PATTERN_EDIT);
}

void test_nav_context_selector_previews_and_applies_all_three_contexts() {
    SequencerStepHarness h;

    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    h.press(Config::ButtonID::NAV);
    assert(h.state.sequencer.contextSelector.visible);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(
        h.state.sequencer.contextSelector.previewFocus ==
        core::state::StructureNavigationFocus::TRACK
    );
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.contextSelector.visible);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.structureUi.stepSelection.selected(0));
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.structureUi.stepSelection.active.get());

    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.tap(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    assert(!h.state.sequencer.contextSelector.visible);
    assert(h.state.sequencer.patternEditor.active.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_PATTERN_EDIT);

    std::cout << "[PASS] test_nav_context_selector_previews_and_applies_all_three_contexts\n";
}

void test_child_context_selector_cycles_pattern_and_step_only() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    assert(!h.state.sequencer.structureUi.stepSelection.active.get());
    h.release(Config::ButtonID::NAV);
    assert(
        h.state.sequencer.structureUi.pageSelection.selectedMask.get() == 0U
    );
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.structureUi.pageSelection.active.get());

    h.press(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.contextSelector.previewFocus ==
        core::state::StructureNavigationFocus::STEP
    );
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);

    h.press(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(
        h.state.sequencer.contextSelector.previewFocus ==
        core::state::StructureNavigationFocus::PAGE
    );
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);

    std::cout << "[PASS] test_child_context_selector_cycles_pattern_and_step_only\n";
}

void test_track_selection_skips_gaps_and_mutes_atomically() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0005U, 0U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.trackNavigation.selection.active.get());
    assert(h.state.trackNavigation.selection.cursorIndex.get() == 0U);
    h.release(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.selectedMask.get() == 0U);

    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.selection.cursorIndex.get() == 2U);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.selectedMask.get() == 0x0005U);

    const uint8_t historyBefore = h.state.projectTrackHistory.undoCount();
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.projectTracks.authored.mutedMask == 0x0005U);
    assert(h.state.projectTrackHistory.undoCount() == historyBefore + 1U);
    assert(h.state.trackNavigation.selection.active.get());

    assert(h.state.undoProjectHistory());
    assert(h.state.projectTracks.authored.mutedMask == 0U);
    assert(h.state.redoProjectHistory());
    assert(h.state.projectTracks.authored.mutedMask == 0x0005U);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.projectTracks.authored.mutedMask == 0U);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.trackNavigation.selection.active.get());
    assert(
        h.state.trackNavigation.selection.selectedMask.get() == 0U
    );
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.trackNavigation.selection.active.get());

    std::cout << "[PASS] test_track_selection_skips_gaps_and_mutes_atomically\n";
}

void test_track_selection_delete_is_undoable_and_keeps_one_track() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0007U, 0U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.selectedMask.get() == 0x0002U);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0005U);
    assert(!h.state.trackNavigation.selection.active.get());
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0007U);
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0005U);

    std::cout << "[PASS] test_track_selection_delete_is_undoable_and_keeps_one_track\n";
}

void test_track_selection_copy_is_global_from_sequencer_view() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0005U, 0U);
    h.state.sequencer.pattern.setContentLength(8U);
    h.state.sequencer.pattern.note[0] = 79U;
    h.state.sequencer.pattern.velocity[0] = 103U;
    h.state.sequencer.pattern.setEnabled(0U, true);
    auto& sourcePatternTwo = h.state.sequencerTracks.track(2U);
    sourcePatternTwo.setContentLength(8U);
    sourcePatternTwo.note[0] = 67U;
    sourcePatternTwo.velocity[0] = 97U;
    sourcePatternTwo.setEnabled(0U, true);
    auto& untouchedPattern = h.state.sequencerTracks.track(5U);
    untouchedPattern.setContentLength(8U);
    untouchedPattern.note[0] = 55U;
    untouchedPattern.setEnabled(0U, true);

    auto& sourcePageZero = h.state.pages.pageData(0U, 0U);
    sourcePageZero.setMacroActive(2U, true);
    sourcePageZero.cc[2] = 22U;
    sourcePageZero.values[2] = 0.82f;
    auto& sourcePageTwo = h.state.pages.pageData(2U, 0U);
    sourcePageTwo.setMacroActive(5U, true);
    sourcePageTwo.cc[5] = 75U;
    sourcePageTwo.values[5] = 0.36f;
    h.state.pages.updateActiveConfigs();
    (void)test_support::project_control::addLocalLfo(
        h.state.pages.control,
        {.track = 0U, .page = 0U, .macro = 2U},
        "Sequencer Track 1 LFO"
    );
    (void)test_support::project_control::addLocalLfo(
        h.state.pages.control,
        {.track = 2U, .page = 0U, .macro = 5U},
        "Sequencer Track 3 LFO"
    );
    configureProjectTrackFixture(h.state, 4U, 9U);
    configureProjectTrackFixture(h.state, 6U, 13U, true);

    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.active.get());
    h.tap(Config::ButtonID::NAV);
    assert(
        h.state.trackNavigation.selection.selectedMask.get() ==
        0x0001U
    );
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.selection.cursorIndex.get() == 2U);
    h.tap(Config::ButtonID::NAV);
    assert(
        h.state.trackNavigation.selection.selectedMask.get() ==
        0x0005U
    );

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerTrackSelection());
    assert(
        h.state.structureClipboard.sequencerTrackSelection->count ==
        2U
    );
    assert(h.state.trackNavigation.selection.placementActive());

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.advance(0U);
    assert(h.state.trackNavigation.selection.cursorIndex.get() == 4U);
    assert(
        h.state.trackNavigation.selection.destinationMask.get() ==
        0x0050U
    );

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.currentSharedTrackEnabledMask() == 0x0055U);
    assert(h.state.currentSharedActiveTrack() == 4U);
    assert(h.state.sequencer.pattern.note[0] == 79U);
    assert(h.state.sequencer.pattern.velocity[0] == 103U);
    assert(h.state.sequencerTracks.track(6U).note[0] == 67U);
    assert(h.state.sequencerTracks.track(6U).velocity[0] == 97U);
    assert(h.state.sequencerTracks.track(5U).note[0] == 55U);
    assert(h.state.pages.pageData(4U, 0U).isMacroActive(2U));
    assert(h.state.pages.pageData(4U, 0U).cc[2] == 22U);
    assert(h.state.pages.pageData(6U, 0U).isMacroActive(5U));
    assert(h.state.pages.pageData(6U, 0U).cc[5] == 75U);
    assert(
        test_support::project_control::outputBindingCountAt(
            h.state.pages.control,
            {.track = 4U, .page = 0U, .macro = 2U}
        ) == 1U
    );
    assert(
        test_support::project_control::outputBindingCountAt(
            h.state.pages.control,
            {.track = 6U, .page = 0U, .macro = 5U}
        ) == 1U
    );
    assert(h.state.projectTracks.authored.midiChannels[4U] == 9U);
    assert(h.state.projectTracks.authored.midiChannels[6U] == 13U);
    assert(
        (h.state.projectTracks.authored.mutedMask &
         static_cast<uint16_t>(1U << 6U)) != 0U
    );
    assert(h.state.trackNavigation.selection.placementActive());

    assert(h.state.undoProjectHistory());
    assert(h.state.currentSharedTrackEnabledMask() == 0x0005U);
    assert(!h.state.pages.pageData(4U, 0U).isMacroActive(2U));
    assert(!h.state.pages.pageData(6U, 0U).isMacroActive(5U));
    assert(h.state.sequencerTracks.track(5U).note[0] == 55U);
    assert(
        test_support::project_control::outputBindingCountAt(
            h.state.pages.control,
            {.track = 4U, .page = 0U, .macro = 2U}
        ) == 0U
    );
    assert(
        test_support::project_control::outputBindingCountAt(
            h.state.pages.control,
            {.track = 6U, .page = 0U, .macro = 5U}
        ) == 0U
    );
    assert(h.state.redoProjectHistory());
    assert(h.state.currentSharedTrackEnabledMask() == 0x0055U);
    assert(h.state.pages.pageData(4U, 0U).isMacroActive(2U));
    assert(h.state.pages.pageData(6U, 0U).isMacroActive(5U));
    assert(
        test_support::project_control::outputBindingCountAt(
            h.state.pages.control,
            {.track = 4U, .page = 0U, .macro = 2U}
        ) == 1U
    );
    assert(
        test_support::project_control::outputBindingCountAt(
            h.state.pages.control,
            {.track = 6U, .page = 0U, .macro = 5U}
        ) == 1U
    );

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.trackNavigation.selection.active.get());
    assert(!h.state.trackNavigation.selection.placementActive());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.trackNavigation.selection.active.get());

    std::cout
        << "[PASS] sparse Track selection copies Sequencer, Macro and Modulators\n";
}

void test_page_selection_clear_and_delete_are_undoable() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(24U);
    h.state.sequencer.pattern.note[0] = 72U;
    h.state.sequencer.pattern.note[8] = 84U;
    h.state.sequencer.pattern.setEnabled(0U, true);
    h.state.sequencer.pattern.setEnabled(8U, true);
    h.state.sequencer.page.set(0U);
    h.state.sequencer.focusedStep.set(0U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    h.release(Config::ButtonID::NAV);
    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(
        h.state.sequencer.structureUi.pageSelection.selectedMask.get() ==
        0x0003U
    );

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(
        h.state.sequencer.pattern.note[0] ==
        core::state::sequencer::SequencerState::DEFAULT_NOTE
    );
    assert(
        h.state.sequencer.pattern.note[8] ==
        core::state::sequencer::SequencerState::DEFAULT_NOTE
    );
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 72U);
    assert(h.state.sequencer.pattern.note[8] == 84U);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencer.pattern.length.get() == 8U);
    assert(!h.state.sequencer.structureUi.pageSelection.active.get());
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.length.get() == 24U);
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.length.get() == 8U);

    std::cout << "[PASS] test_page_selection_clear_and_delete_are_undoable\n";
}

void test_pattern_selection_paste_previews_collisions_and_creates_intermediate_pages() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(16U);
    h.state.sequencer.pattern.note[0] = 72U;
    h.state.sequencer.pattern.velocity[0] = 91U;
    h.state.sequencer.pattern.setEnabled(0U, true);
    h.state.sequencer.pattern.note[8] = 84U;
    h.state.sequencer.pattern.velocity[8] = 111U;
    h.state.sequencer.pattern.setEnabled(8U, true);
    h.state.sequencer.page.set(0U);
    h.state.sequencer.focusedStep.set(0U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::NAV);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());

    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(
        h.state.sequencer.structureUi.pageSelection.selectedMask.get() ==
        0x0003U
    );

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    auto& selection = h.state.sequencer.structureUi.pageSelection;
    assert(selection.placementActive());
    assert(h.state.structureClipboard.hasSequencerPageSelection());
    assert(selection.cursorIndex.get() == 1U);
    assert(selection.destinationMask.get() == 0x0006U);
    assert(selection.overwriteMask.get() == 0x0002U);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.advance(0U);
    assert(selection.cursorIndex.get() == 4U);
    assert(selection.destinationMask.get() == 0x0030U);
    assert(selection.overwriteMask.get() == 0U);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.activePageCount() == 6U);
    assert(h.state.sequencer.pattern.note[32] == 72U);
    assert(h.state.sequencer.pattern.velocity[32] == 91U);
    assert(h.state.sequencer.pattern.isEnabled(32U));
    assert(h.state.sequencer.pattern.note[40] == 84U);
    assert(h.state.sequencer.pattern.velocity[40] == 111U);
    assert(h.state.sequencer.pattern.isEnabled(40U));
    assert(
        h.state.sequencer.pattern.note[16] ==
        core::state::sequencer::SequencerState::DEFAULT_NOTE
    );
    assert(!h.state.sequencer.pattern.isEnabled(16U));
    assert(
        h.state.sequencer.pattern.note[24] ==
        core::state::sequencer::SequencerState::DEFAULT_NOTE
    );
    assert(!h.state.sequencer.pattern.isEnabled(24U));
    assert(selection.active.get());
    assert(selection.placementActive());

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(selection.active.get());
    assert(!selection.placementActive());
    assert(selection.selectedMask.get() == 0U);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!selection.active.get());

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.activePageCount() == 2U);
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.activePageCount() == 6U);
    assert(h.state.sequencer.pattern.note[32] == 72U);
    assert(h.state.sequencer.pattern.note[40] == 84U);

    std::cout
        << "[PASS] Pattern selection previews collisions and fills page gaps\n";
}

void test_track_context_nav_crosses_sparse_slots_and_creates_without_structure() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0005U, 0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 0);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1U);
    assert(h.state.trackNavigation.previewAddSlot.get());
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 2);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 2U);
    assert(!h.state.trackNavigation.previewAddSlot.get());

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1U);
    assert(h.state.trackNavigation.previewAddSlot.get());
    h.tap(Config::ButtonID::NAV);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0007U);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1U);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1U);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.sequencerHistory.undoCount() == 1U);

    std::cout << "[PASS] test_track_context_nav_crosses_sparse_slots_and_creates_without_structure\n";
}

void test_step_toggle_undo_redo_workflow() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(3);
    createRootMicroSequence(h, 0);
    assert(core::state::sequencer::storeActiveTrack(
        h.state.sequencerTracks,
        h.state.sequencer
    ));

    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.tap(Config::MACRO_BUTTONS[0]);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(rootStepHasMicroSequence(h, 0));

    assert(h.state.undoProjectHistory());
    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 3);
    assert(rootStepHasMicroSequence(h, 0));

    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(rootStepHasMicroSequence(h, 0));

    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.redoCount() == 0);

    std::cout << "[PASS] test_step_toggle_undo_redo_workflow\n";
}

void test_pattern_editor_adds_only_the_next_page() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(24);
    h.state.sequencer.page.set(2);
    h.state.sequencer.focusedStep.set(16);

    openPatternEditor(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);

    assert(!h.state.sequencer.structureUi.previewAddPageSlot.get());
    assert(h.state.sequencer.patternEditor.active.get());
    assert(h.state.sequencer.pattern.length.get() == 32);
    assert(h.state.sequencer.page.get() == 3);
    assert(h.state.sequencer.focusedStep.get() == 24);

    for (uint8_t step = 24; step < 32; ++step) {
        assert(h.state.sequencer.pattern.note[step] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
        assert(h.state.sequencer.pattern.velocity[step] == core::state::sequencer::SequencerState::DEFAULT_VELOCITY);
        assert(h.state.sequencer.pattern.gate[step] == core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT);
        assert(h.state.sequencer.pattern.nudge[step] == 0);
        assert(h.state.sequencer.pattern.probability[step] == core::state::sequencer::SequencerState::DEFAULT_PROBABILITY);
        assert(!h.state.sequencer.pattern.isEnabled(step));
    }

    std::cout << "[PASS] test_pattern_editor_adds_only_the_next_page\n";
}

void test_track_focus_bottom_left_mutes_without_clearing_payload() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 1);
    h.state.sequencer.pattern.note[0] = 82;
    h.state.sequencer.pattern.velocity[0] = 108;
    h.state.sequencer.pattern.setEnabled(0, true);
    focusTrackNavigation(h);

    const uint8_t sequencerUndoBefore = h.state.sequencerHistory.undoCount();
    const uint8_t trackUndoBefore = h.state.projectTrackHistory.undoCount();
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.projectTracks.authored.mutedMask == 0x0002);
    assert(h.state.sequencer.pattern.note[0] == 82);
    assert(h.state.sequencer.pattern.velocity[0] == 108);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerHistory.undoCount() == sequencerUndoBefore);
    assert(h.state.projectTrackHistory.undoCount() == trackUndoBefore + 1U);

    assert(h.state.undoProjectHistory());
    assert(h.state.projectTracks.authored.mutedMask == 0);
    assert(h.state.sequencer.pattern.note[0] == 82);
    assert(h.state.sequencer.pattern.isEnabled(0));

    assert(h.state.redoProjectHistory());
    assert(h.state.projectTracks.authored.mutedMask == 0x0002);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.projectTracks.authored.mutedMask == 0);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.projectTracks.authored.mutedMask == 0x0002);

    std::cout << "[PASS] test_track_focus_bottom_left_mutes_without_clearing_payload\n";
}

void test_sequencer_page_copy_and_long_press_paste() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(16);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);
    h.state.sequencer.pattern.note[0] = 72;
    h.state.sequencer.pattern.velocity[0] = 99;
    h.state.sequencer.pattern.gate[0] = 80;
    h.state.sequencer.pattern.nudge[0] = 3;
    h.state.sequencer.pattern.probability[0] = 87;
    h.state.sequencer.pattern.setEnabled(0, true);
    createRootMicroSequence(h, 0);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerPage());
    assert(h.state.structureClipboard.sequencerPage.sourcePage == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.page.get() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::PASTE);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS / 2U);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS / 2U + 1U);
    assert(h.state.sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(h.state.sequencer.pattern.note[8] == 72);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.note[8] == 72);
    assert(h.state.sequencer.pattern.velocity[8] == 99);
    assert(h.state.sequencer.pattern.gate[8] == 80);
    assert(h.state.sequencer.pattern.nudge[8] == 3);
    assert(h.state.sequencer.pattern.probability[8] == 87);
    assert(h.state.sequencer.pattern.isEnabled(8));
    assert(rootStepHasMicroSequence(h, 8));

    std::cout << "[PASS] test_sequencer_page_copy_and_long_press_paste\n";
}

void test_child_content_clear_copy_and_paste_are_undoable() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));
    h.state.sequencer.focusedStep.set(0);

    const auto childNode0 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        0
    );
    const auto cycle = core::state::sequencer::createCycleStateSet(
        h.state.sequencer.pattern,
        childNode0,
        2
    );
    assert(cycle.ok);
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        h.state.sequencer.pattern.graph->cycleSets[cycle.id].firstStateNode,
        5
    ));
    h.state.sequencer.contentView.bump();

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerStepContent());

    const uint8_t undoBeforeClear = h.state.sequencerHistory.undoCount();
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    const auto* graphAfterClear = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterClear != nullptr);
    assert(!graphAfterClear->stepNodes[childNode0].has(
        oc::note::sequencer::STEP_NODE_CYCLE_SET
    ));
    assert(h.state.sequencerHistory.undoCount() == undoBeforeClear + 1U);

    h.press(Config::MACRO_BUTTONS[1]);
    h.release(Config::MACRO_BUTTONS[1]);
    assert(h.state.sequencer.focusedStep.get() == 1);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto childNode1 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        1
    );
    const auto* graphAfterPaste = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterPaste != nullptr);
    assert(graphAfterPaste->stepNodes[childNode1].has(
        oc::note::sequencer::STEP_NODE_CYCLE_SET
    ));
    const auto* pastedCycle =
        graphAfterPaste->cycleSet(graphAfterPaste->stepNodes[childNode1].cycleSetId);
    assert(pastedCycle != nullptr);
    assert(graphAfterPaste->stepNodes[pastedCycle->firstStateNode].noteOffset == 5);

    assert(h.state.undoProjectHistory());
    const auto* graphAfterUndo = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterUndo != nullptr);
    assert(!graphAfterUndo->stepNodes[childNode1].has(
        oc::note::sequencer::STEP_NODE_CYCLE_SET
    ));

    std::cout << "[PASS] test_child_content_clear_copy_and_paste_are_undoable\n";
}

void test_undo_removed_active_child_context_returns_to_root() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(0);

    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    assert(core::state::sequencer::captureHistorySnapshot(h.state.sequencer, before));

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);

    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    assert(core::state::sequencer::captureHistorySnapshot(h.state.sequencer, after));
    assert(h.state.recordSequencerPatternHistory(
        std::move(before),
        std::move(after),
        core::state::sequencer::SequencerHistoryDescriptor{
            .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
            .stepIndex = 0,
        }
    ));

    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));

    assert(h.state.undoSequencerHistory());
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(h.state.sequencer.contentView.depth.get() == 0);
    assert(h.state.sequencer.contentView.ownerNodeId.get() ==
           oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID);

    std::cout << "[PASS] test_undo_removed_active_child_context_returns_to_root\n";
}

void test_sequencer_track_copy_and_long_press_paste_to_add_slot() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0001, 0);
    configureProjectTrackFixture(h.state, 1, 8);
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[0] = 79;
    h.state.sequencer.pattern.velocity[0] = 96;
    h.state.sequencer.pattern.gate[0] = 72;
    h.state.sequencer.pattern.setEnabled(0, true);
    createRootMicroSequence(h, 0);
    focusTrackNavigation(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[1] == 8);
    assert(h.state.sequencer.pattern.note[0] == 79);
    assert(h.state.sequencer.pattern.velocity[0] == 96);
    assert(h.state.sequencer.pattern.gate[0] == 72);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(rootStepHasMicroSequence(h, 0));
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 1);
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0001);
    assert(h.state.projectTracks.authored.midiChannels[1] == 8);
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[1] == 8);
    assert(rootStepHasMicroSequence(h, 0));
    assert(h.state.structureClipboard.hasSequencerTrack());

    std::cout << "[PASS] test_sequencer_track_copy_and_long_press_paste_to_add_slot\n";
}

void test_sequencer_track_paste_preserves_occupied_destination_routing_and_mute() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    configureProjectTrackFixture(h.state, 1, 11, true);
    h.state.sequencer.pattern.note[0] = 76;
    h.state.sequencer.pattern.velocity[0] = 104;
    h.state.sequencer.pattern.setEnabled(0, true);
    focusTrackNavigation(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 11);
    // Project Track remains authoritative while the guarded paste is open.
    assert(core::state::project::ProjectTrackDomainServices::fromCoreState(
               h.state
           ).setMidiChannel(1, 13));
    assert(h.state.projectTracks.authored.midiChannels[1] == 13);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[1] == 13);
    assert(core::state::project::projectTrackMuted(
        h.state.projectTracks,
        1
    ));
    assert(h.state.sequencer.pattern.note[0] == 76);
    assert(h.state.sequencer.pattern.velocity[0] == 104);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 1);
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.undoSequencerHistory());
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 13);
    assert(core::state::project::projectTrackMuted(
        h.state.projectTracks,
        1
    ));
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.redoSequencerHistory());
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 13);
    assert(core::state::project::projectTrackMuted(
        h.state.projectTracks,
        1
    ));
    assert(h.state.sequencer.pattern.note[0] == 76);
    assert(h.state.structureClipboard.hasSequencerTrack());

    std::cout
        << "[PASS] test_sequencer_track_paste_preserves_occupied_destination_routing_and_mute\n";
}

void test_track_paste_global_undo_redo_restores_content_and_reports_outcome() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    focusTrackNavigation(h);

    configureProjectTrackFixture(h.state, 0, 2);
    h.state.sequencer.pattern.note[0] = 76;
    h.state.sequencer.pattern.velocity[0] = 104;
    h.state.sequencer.pattern.setEnabled(0, true);
    auto& destination = h.state.sequencerTracks.track(1);
    configureProjectTrackFixture(h.state, 1, 11, true);
    destination.note[0] = 42;
    destination.velocity[0] = 73;
    destination.setEnabled(0, true);

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencer.pattern.note[0] == 42);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.pattern.note[0] == 76);
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 11);
    assert(core::state::project::projectTrackMuted(
        h.state.projectTracks,
        1
    ));

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 42);
    assert(h.state.sequencer.pattern.velocity[0] == 73);
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 11);
    assert(core::state::project::projectTrackMuted(
        h.state.projectTracks,
        1
    ));
    assert(std::strcmp(
        h.state.sequencer.historyFeedback.line2.data(),
        "Track Paste"
    ) == 0);
    assert(std::strcmp(
        h.state.sequencer.historyFeedback.line3.data(),
        "Pending cancelled"
    ) == 0);

    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 76);
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 11);
    assert(std::strcmp(
        h.state.sequencer.historyFeedback.line2.data(),
        "Track Paste"
    ) == 0);

    std::cout
        << "[PASS] test_track_paste_global_undo_redo_restores_content_and_reports_outcome\n";
}

void test_track_paste_clamps_focus_to_short_source_before_history_commit() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    focusTrackNavigation(h);
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[0] = 68;
    h.state.sequencerTracks.track(1).setContentLength(128);
    configureProjectTrackFixture(h.state, 1, 7);

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    h.state.sequencer.focusedStep.set(100);
    h.state.sequencer.page.set(12);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.focusedStep.get() == 7);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.pattern.note[0] == 68);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 1);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 128);
    assert(h.state.sequencer.focusedStep.get() == 100);
    assert(h.state.sequencer.page.get() == 12);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.focusedStep.get() == 7);
    assert(h.state.sequencer.page.get() == 0);

    std::cout
        << "[PASS] test_track_paste_clamps_focus_to_short_source_before_history_commit\n";
}

void test_track_paste_mid_hold_release_cancels_without_mutation_or_history() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencer.pattern.note[0] = 81;
    h.state.sequencerTracks.track(1).note[0] = 44;
    configureProjectTrackFixture(h.state, 1, 6);
    focusTrackNavigation(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.pattern.note[0] == 44);

    const uint8_t undoBefore = h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    );
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::LATCH_THRESHOLD_MS);
    assert(h.state.sequencer.structureUi.trackPaste.guard.phase ==
           core::state::contextual::GuardedActionPhase::ARMED);
    h.advance(250);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.note[0] == 44);
    assert(h.state.sequencerTracks.track(1).note[0] == 44);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == undoBefore);
    assert(h.state.sequencer.structureUi.trackPaste.feedback.status ==
           core::state::contextual::OperationFeedbackStatus::CANCELLED);
    h.advance(1);
    assert(h.state.sequencer.structureUi.trackPaste.feedback.status ==
           core::state::contextual::OperationFeedbackStatus::CANCELLED);
}

void test_track_paste_commits_once_at_absolute_long_threshold() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencer.pattern.note[0] = 83;
    h.state.sequencerTracks.track(1).note[0] = 45;
    configureProjectTrackFixture(h.state, 1, 7);
    focusTrackNavigation(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS - 1U);
    assert(h.state.sequencer.pattern.note[0] == 45);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 0);
    h.advance(1);
    assert(h.state.sequencer.pattern.note[0] == 83);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 1);
    assert(h.state.sequencer.structureUi.trackPaste.guard.phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);
    h.advance(500);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 1);
}

void test_track_paste_left_top_cancels_and_consumes_later_release() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencer.pattern.note[0] = 84;
    h.state.sequencerTracks.track(1).note[0] = 46;
    focusTrackNavigation(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::LATCH_THRESHOLD_MS);
    h.tap(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.note[0] == 46);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 0);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK);
}

void test_track_paste_refreshes_route_during_hold_and_freezes_queued_plan() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencer.pattern.note[0] = 85;
    h.state.sequencerTracks.track(1).note[0] = 47;
    configureProjectTrackFixture(h.state, 1, 3);
    focusTrackNavigation(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::LATCH_THRESHOLD_MS);
    assert(core::state::project::ProjectTrackDomainServices::fromCoreState(
               h.state
           ).setMidiChannel(1, 8));
    h.advance(1);
    assert(h.state.sequencer.structureUi.trackPaste.plan.entries[0]
               .targetMidiChannel == 8);
    h.advance(
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS -
        Config::Timing::LATCH_THRESHOLD_MS - 1U
    );
    assert(h.state.sequencer.pattern.note[0] == 85);
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 8);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto frozenPlan = h.state.sequencer.structureUi.trackPaste.plan;
    const uint32_t frozenGeneration =
        h.state.sequencer.structureUi.trackPaste.activationGeneration;
    assert(frozenGeneration != 0);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);  // Copy the destination after commit.
    h.advance(0);
    assert(core::state::sameSequencerTrackClipboardTransferPlan(
        h.state.sequencer.structureUi.trackPaste.plan,
        frozenPlan
    ));
    assert(h.state.sequencer.structureUi.trackPaste.activationGeneration ==
           frozenGeneration);
}

void test_deleted_track_slot_can_be_recreated_at_any_gap() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0005, 0);
    configureProjectTrackFixture(h.state, 1, 8);
    configureProjectTrackFixture(h.state, 2, 2);
    h.state.sequencerTracks.track(2).note[0] = 83;
    h.state.sequencerTracks.track(2).setEnabled(0, true);
    focusTrackNavigation(h);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.sequencerTracks.activeTrackIndex() == 2);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.sequencerTracks.isTrackEnabled(1));
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[1] == 8);
    assert(h.state.sequencer.pattern.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerTracks.track(2).note[0] == 83);
    assert(h.state.sequencerTracks.track(2).isEnabled(0));
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencer.pattern.length.get() == 8);

    std::cout << "[PASS] test_deleted_track_slot_can_be_recreated_at_any_gap\n";
}

void test_created_page_is_undoable_and_redoable() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);

    openPatternEditor(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.page.get() == 1);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::PatternOnly
    ) == 1);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::FullBank
    ) == 0);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.page.get() == 0);
    assert(!h.state.sequencer.structureUi.previewAddPageSlot.get());
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "UNDO T01") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Page Structure") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "2 pages -> 1 page") == 0);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.page.get() == 1);
    assert(!h.state.sequencer.structureUi.previewAddPageSlot.get());
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "REDO T01") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "1 page -> 2 pages") == 0);

    std::cout << "[PASS] test_created_page_is_undoable_and_redoable\n";
}

void test_created_track_is_undoable_and_redoable() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0001, 0);
    focusTrackNavigation(h);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 1);
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure
    ) == 1);
    assert(h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::FullBank
    ) == 0);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0001);
    assert(h.state.sequencerTracks.activeTrackIndex() == 0);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 0);
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "UNDO T02") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Track Structure") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "2 tracks -> 1 track") == 0);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "REDO T02") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "1 track -> 2 tracks") == 0);

    std::cout << "[PASS] test_created_track_is_undoable_and_redoable\n";
}

void test_track_creation_history_failure_rolls_back_and_keeps_add_slot_open() {
    test_support::CoreStorages storages;
    core::state::CoreState state(
        storages.settings
    );
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers
    > navigationFocus{core::state::StructureNavigationFocus::TRACK};

    state.sequencerTracks.reset();
    state.setSharedTrackState(0x0001, 0);
    state.sequencerTracks.track(1).note[0] = 77;
    state.sequencerTracks.track(1).setEnabled(0, true);

    core::handler::SequencerStructureNavigationWorkflow workflow({
        state.sequencer,
        state.sequencerTracks,
        navigationFocus,
        state.trackNavigation,
        core::handler::SharedTrackDomainServices::fromCoreState(state),
        core::handler::SequencerHistoryDomainServices{},
    });
    workflow.moveByFocus(1.0f);
    assert(state.trackNavigation.previewAddSlot.get());
    assert(state.trackNavigation.previewTrackIndex.get() == 1);

    const auto result = workflow.createPreviewedStructure();

    assert(
        result == core::handler::SequencerStructureNavigationWorkflow::
            CreationResult::HISTORY_UNAVAILABLE
    );
    assert(state.sequencerTracks.currentEnabledMask() == 0x0001);
    assert(state.sequencerTracks.activeTrackIndex() == 0);
    assert(state.sharedTrackEnabledMask.get() == 0x0001);
    assert(state.sharedTrackActive.get() == 0);
    assert(state.sequencerTracks.track(1).note[0] == 77);
    assert(state.sequencerTracks.track(1).isEnabled(0));
    assert(state.trackNavigation.previewAddSlot.get());
    assert(state.sequencerHistory.undoCount() == 0);

    std::cout
        << "[PASS] test_track_creation_history_failure_rolls_back_and_keeps_add_slot_open\n";
}

void test_step_selection_copy_paste_extends_sparse_root_steps() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);

    h.state.sequencer.pattern.note[1] = 65;
    h.state.sequencer.pattern.velocity[1] = 91;
    h.state.sequencer.pattern.gate[1] = 130;
    h.state.sequencer.pattern.nudge[1] = -2;
    h.state.sequencer.pattern.probability[1] = 76;
    h.state.sequencer.pattern.setEnabled(1, true);

    h.state.sequencer.pattern.note[3] = 70;
    h.state.sequencer.pattern.velocity[3] = 112;
    h.state.sequencer.pattern.gate[3] = 180;
    h.state.sequencer.pattern.nudge[3] = 4;
    h.state.sequencer.pattern.probability[3] = 64;
    h.state.sequencer.pattern.setEnabled(3, true);
    createRootMicroSequence(h, 3);
    auto selectedChord = oc::note::sequencer::StepSequencerChordSpec::semantic(
        oc::note::sequencer::StepSequencerChordHarmony::Custom,
        8U,
        oc::note::sequencer::StepSequencerChordVoicing::Open,
        1U,
        oc::note::sequencer::StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    constexpr std::array<uint8_t, 8> intervals{
        0U, 3U, 5U, 8U, 12U, 17U, 24U, 31U,
    };
    for (uint8_t voice = 7U; voice > 0U; --voice) {
        selectedChord.setCustomInterval(voice, intervals[voice]);
    }
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(3),
        selectedChord
    ));

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);
    h.release(Config::ButtonID::NAV);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());

    h.tap(Config::MACRO_BUTTONS[1]);
    h.tap(Config::MACRO_BUTTONS[3]);
    assert(h.state.sequencer.structureUi.stepSelection.selected(1));
    assert(h.state.sequencer.structureUi.stepSelection.selected(3));

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(h.state.structureClipboard.sequencerSteps.rootContext);
    assert(h.state.structureClipboard.sequencerSteps.count == 2);
    assert(h.state.structureClipboard.sequencerSteps.span == 3);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.state.sequencer.structureUi.stepSelection.placementActive());

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.stepSelection.cursorStep.get() == 6);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    assert(
        h.state.sequencer.structureUi.pageHold.action.get() ==
        core::state::StructureHoldAction::PASTE
    );
    h.advance(0);
    assert(h.state.sequencer.structureUi.stepSelection.pastePreviewActive.get());
    assert(
        h.state.sequencer.structureUi.stepSelection.pastePreview.get() ==
        core::state::sequencer::SequencerStepPastePreview::GHOST
    );
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.state.sequencer.structureUi.stepSelection.placementActive());
    assert(
        h.state.sequencer.structureUi.pageHold.action.get() ==
        core::state::StructureHoldAction::NONE
    );
    assert(h.state.sequencer.pattern.length.get() == 9);
    assert(h.state.sequencer.focusedStep.get() == 6);
    assert(h.state.sequencer.pattern.note[6] == 65);
    assert(h.state.sequencer.pattern.velocity[6] == 91);
    assert(h.state.sequencer.pattern.gate[6] == 130);
    assert(h.state.sequencer.pattern.nudge[6] == -2);
    assert(h.state.sequencer.pattern.probability[6] == 76);
    assert(h.state.sequencer.pattern.isEnabled(6));
    assert(h.state.sequencer.pattern.note[8] == 70);
    assert(h.state.sequencer.pattern.velocity[8] == 112);
    assert(h.state.sequencer.pattern.gate[8] == 180);
    assert(h.state.sequencer.pattern.nudge[8] == 4);
    assert(h.state.sequencer.pattern.probability[8] == 64);
    assert(h.state.sequencer.pattern.isEnabled(8));
    assert(rootStepHasMicroSequence(h, 8));
    const auto* pastedChordNode = rootStepNode(h, 8);
    assert(pastedChordNode != nullptr);
    assert(pastedChordNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(pastedChordNode->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(oc::note::sequencer::chordSpecsEqual(
        pastedChordNode->chordSpec,
        selectedChord
    ));

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.structureUi.stepSelection.placementActive());
    assert(!h.state.sequencer.structureUi.stepSelection.anySelected());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.structureUi.stepSelection.active.get());

    std::cout << "[PASS] test_step_selection_copy_paste_extends_sparse_root_steps\n";
}

void test_step_selection_macro_long_press_consumes_release_without_toggling() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.state.sequencer.structureUi.stepSelection.cursorStep.set(2);
    h.state.sequencer.structureUi.stepSelection.setSelected(2, true);
    h.state.sequencer.structureUi.stepSelection.setSelected(4, true);

    h.press(Config::MACRO_BUTTONS[2]);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::MACRO_BUTTONS[2]);

    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.state.sequencer.structureUi.stepSelection.selected(2));
    assert(h.state.sequencer.structureUi.stepSelection.selected(4));

    h.tap(Config::MACRO_BUTTONS[2]);
    assert(!h.state.sequencer.structureUi.stepSelection.selected(2));
    assert(h.state.sequencer.structureUi.stepSelection.selected(4));

    std::cout << "[PASS] test_step_selection_macro_long_press_consumes_release_without_toggling\n";
}

void test_macro_press_on_future_page_does_not_wrap_to_existing_step() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.page.set(1);
    h.state.sequencer.focusedStep.set(0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);

    h.tap(Config::MACRO_BUTTONS[0]);

    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_macro_press_on_future_page_does_not_wrap_to_existing_step\n";
}

void test_step_focus_bottom_left_resets_focused_step_only() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(16);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(3);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);

    h.state.sequencer.pattern.note[3] = 74;
    h.state.sequencer.pattern.velocity[3] = 105;
    h.state.sequencer.pattern.setEnabled(3, true);
    createRootMicroSequence(h, 3);
    oc::note::sequencer::StepSequencerChordSpec chord{};
    chord.voiceCount = 6;
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(3),
        chord
    ));
    h.state.sequencer.pattern.note[8] = 81;
    h.state.sequencer.pattern.setEnabled(8, true);

    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.focusedStep.get() == 3);
    assert(h.state.sequencer.page.get() == 0);
    assert(!h.state.sequencer.pattern.isEnabled(3));
    assert(h.state.sequencer.pattern.note[3] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(rootStepHasMicroSequence(h, 3));
    const auto* shallowResetNode = rootStepNode(h, 3);
    assert(shallowResetNode != nullptr);
    assert(!shallowResetNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(h.state.sequencer.pattern.isEnabled(8));
    assert(h.state.sequencer.pattern.note[8] == 81);
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.focusedStep.get() == 3);
    assert(h.state.sequencer.page.get() == 0);
    assert(!rootStepHasMicroSequence(h, 3));

    std::cout << "[PASS] test_step_focus_bottom_left_resets_focused_step_only\n";
}

void test_step_focus_copy_paste_copies_complete_step_without_selection() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(1);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);

    h.state.sequencer.pattern.note[1] = 76;
    h.state.sequencer.pattern.velocity[1] = 112;
    h.state.sequencer.pattern.gate[1] = 180;
    h.state.sequencer.pattern.nudge[1] = 3;
    h.state.sequencer.pattern.setEnabled(1, true);
    createRootMicroSequence(h, 1);
    oc::note::sequencer::StepSequencerChordSpec chord{};
    chord.voiceCount = 7;
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(1),
        chord
    ));

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(h.state.structureClipboard.sequencerSteps.rootContext);
    assert(h.state.structureClipboard.sequencerSteps.count == 1);
    assert(!h.state.sequencer.structureUi.stepSelection.active.get());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.focusedStep.get() == 2);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(!h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.state.sequencer.focusedStep.get() == 2);
    assert(h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == 76);
    assert(h.state.sequencer.pattern.velocity[2] == 112);
    assert(h.state.sequencer.pattern.gate[2] == 180);
    assert(h.state.sequencer.pattern.nudge[2] == 3);
    assert(rootStepHasMicroSequence(h, 2));
    const auto* pastedNode = rootStepNode(h, 2);
    assert(pastedNode != nullptr);
    assert(pastedNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(pastedNode->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(pastedNode->chordSpec.voiceCount == 7);

    std::cout << "[PASS] test_step_focus_copy_paste_copies_complete_step_without_selection\n";
}

void test_step_selection_clear_is_undoable_and_keeps_selection_active() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.pattern.note[2] = 74;
    h.state.sequencer.pattern.velocity[2] = 105;
    h.state.sequencer.pattern.setEnabled(2, true);
    createRootMicroSequence(h, 2);

    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.state.sequencer.structureUi.stepSelection.cursorStep.set(2);
    h.state.sequencer.structureUi.stepSelection.setSelected(2, true);
    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(rootStepHasMicroSequence(h, 2));
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == 74);
    assert(h.state.sequencer.pattern.velocity[2] == 105);
    assert(rootStepHasMicroSequence(h, 2));

    const uint8_t undoBeforeDeepReset = h.state.sequencerHistory.undoCount();

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!rootStepHasMicroSequence(h, 2));
    assert(h.state.sequencerHistory.undoCount() == undoBeforeDeepReset + 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == 74);
    assert(h.state.sequencer.pattern.velocity[2] == 105);
    assert(rootStepHasMicroSequence(h, 2));

    std::cout << "[PASS] test_step_selection_clear_is_undoable_and_keeps_selection_active\n";
}

void test_step_selection_wrap_paste_overwrites_inside_pattern() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.projectNavigation.stepPasteMode =
        core::state::project::ProjectStepPasteMode::WRAP;

    h.state.sequencer.pattern.note[1] = 61;
    h.state.sequencer.pattern.note[3] = 63;
    h.state.sequencer.pattern.setEnabled(1, true);
    h.state.sequencer.pattern.setEnabled(3, true);
    h.state.sequencer.pattern.note[7] = 79;
    h.state.sequencer.pattern.setEnabled(7, true);

    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.state.sequencer.structureUi.stepSelection.cursorStep.set(1);
    h.state.sequencer.structureUi.stepSelection.setSelected(1, true);
    h.state.sequencer.structureUi.stepSelection.setSelected(3, true);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(h.state.sequencer.structureUi.stepSelection.placementActive());

    h.state.sequencer.structureUi.stepSelection.cursorStep.set(7);
    h.state.sequencer.focusedStep.set(7);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    assert(h.state.sequencer.structureUi.stepSelection.pastePreviewActive.get());
    assert(
        h.state.sequencer.structureUi.stepSelection.pastePreview.get() ==
        core::state::sequencer::SequencerStepPastePreview::OVERWRITE
    );
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.pattern.note[7] == 61);
    assert(h.state.sequencer.pattern.isEnabled(7));
    assert(h.state.sequencer.pattern.note[1] == 63);
    assert(h.state.sequencer.pattern.isEnabled(1));
    assert(h.state.sequencer.structureUi.stepSelection.placementActive());

    std::cout << "[PASS] test_step_selection_wrap_paste_overwrites_inside_pattern\n";
}

void test_child_content_nav_enters_step_selection_and_pastes_child_steps() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.focusedStep.set(0);

    const auto childNode0 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        0
    );
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        childNode0,
        4
    ));
    const auto cycle = core::state::sequencer::createCycleStateSet(
        h.state.sequencer.pattern,
        childNode0,
        2
    );
    assert(cycle.ok);

    h.handler.enterSelectionModeForCurrentFocus();
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);

    h.tap(Config::MACRO_BUTTONS[0]);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(!h.state.structureClipboard.sequencerSteps.rootContext);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.stepSelection.cursorStep.get() == 1);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto childNode1 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        1
    );
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[childNode1].noteOffset == 4);
    assert(graph->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_CYCLE_SET));

    std::cout << "[PASS] test_child_content_nav_enters_step_selection_and_pastes_child_steps\n";
}

void test_child_draft_owns_main_bottom_actions_until_single_apply() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto sourceRoot = core::state::sequencer::rootStepNodeId(0);
    const auto source = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        sourceRoot,
        2
    );
    assert(source.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        sourceRoot,
        source.id
    ));
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    const auto sourceNode = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        0
    );
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        sourceNode,
        9
    ));
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    const uint32_t sourceClipboardRevision = h.state.structureClipboard.revision.get();
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));

    const auto opened = core::state::sequencer::openOrCreateActiveContentChild(
        h.state.sequencer,
        3,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    assert(opened.opened && opened.draft);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.focusedStep.set(0);
    const auto draftNode = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        0
    );
    assert(core::state::sequencer::setNodeNoteOffset(
        core::state::sequencer::authoringPattern(h.state.sequencer),
        draftNode,
        3
    ));
    core::state::sequencer::notifyStepContentDraftMutation(h.state.sequencer);
    assert(h.state.sequencer.stepContentDraft.modified());
    assert(h.state.sequencerHistory.undoCount() == 0);

    // Hidden Reset/Remove actions must not touch the unpublished draft.
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    const auto* draftGraph = core::state::sequencer::authoringPattern(
        h.state.sequencer
    ).graph.get();
    assert(draftGraph != nullptr);
    assert(draftGraph->stepNodes[draftNode].noteOffset == 3);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(!rootStepHasMicroSequence(h, 3));
    assert(h.state.sequencerHistory.undoCount() == 0);

    // A held Apply may not begin or execute the compatible hidden Paste. The
    // release publishes the draft exactly once and keeps its local value.
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(core::state::sequencer::authoringPattern(h.state.sequencer)
               .graph->stepNodes[draftNode].noteOffset == 3);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(rootStepHasMicroSequence(h, 3));
    assert(h.state.sequencerHistory.undoCount() == 1);
    const auto publishedNode = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        0
    );
    const auto* publishedGraph = core::state::sequencer::graphView(
        h.state.sequencer.pattern
    );
    assert(publishedGraph != nullptr);
    assert(publishedGraph->stepNodes[publishedNode].noteOffset == 3);
    assert(h.state.structureClipboard.revision.get() == sourceClipboardRevision);

    // Apply consumes only its physical release; the next ordinary Copy works.
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.revision.get() == sourceClipboardRevision + 1U);
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout
        << "[PASS] test_child_draft_owns_main_bottom_actions_until_single_apply\n";
}

void test_child_step_focus_bottom_actions_use_local_step_payload() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.focusedStep.set(0);

    auto childNode0 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        0
    );
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        childNode0,
        4
    ));
    assert(core::state::sequencer::createCycleStateSet(
        h.state.sequencer.pattern,
        childNode0,
        2
    ).ok);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[childNode0].noteOffset == 0);
    assert(!graph->stepNodes[childNode0].has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(nodeHasCycleStates(h, childNode0));

    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        childNode0,
        5
    ));
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(!h.state.structureClipboard.sequencerSteps.rootContext);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.focusedStep.get() == 1);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto childNode1 = core::state::sequencer::activeContentStepNodeId(
        h.state.sequencer,
        1
    );
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[childNode1].noteOffset == 5);
    assert(graph->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(nodeHasCycleStates(h, childNode1));

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(!graph->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(!nodeHasCycleStates(h, childNode1));

    std::cout << "[PASS] test_child_step_focus_bottom_actions_use_local_step_payload\n";
}

}  // namespace

int main() {
    test_child_creation_draft_apply_and_back_decisions();
    test_nav_context_selector_previews_and_applies_all_three_contexts();
    test_child_context_selector_cycles_pattern_and_step_only();
    test_track_selection_skips_gaps_and_mutes_atomically();
    test_track_selection_delete_is_undoable_and_keeps_one_track();
    test_track_selection_copy_is_global_from_sequencer_view();
    test_page_selection_clear_and_delete_are_undoable();
    test_pattern_selection_paste_previews_collisions_and_creates_intermediate_pages();
    test_track_context_nav_crosses_sparse_slots_and_creates_without_structure();
    test_step_toggle_undo_redo_workflow();
    test_pattern_editor_adds_only_the_next_page();
    test_track_focus_bottom_left_mutes_without_clearing_payload();
    test_sequencer_page_copy_and_long_press_paste();
    test_child_content_clear_copy_and_paste_are_undoable();
    test_undo_removed_active_child_context_returns_to_root();
    test_sequencer_track_copy_and_long_press_paste_to_add_slot();
    test_sequencer_track_paste_preserves_occupied_destination_routing_and_mute();
    test_track_paste_global_undo_redo_restores_content_and_reports_outcome();
    test_track_paste_clamps_focus_to_short_source_before_history_commit();
    test_track_paste_mid_hold_release_cancels_without_mutation_or_history();
    test_track_paste_commits_once_at_absolute_long_threshold();
    test_track_paste_left_top_cancels_and_consumes_later_release();
    test_track_paste_refreshes_route_during_hold_and_freezes_queued_plan();
    test_deleted_track_slot_can_be_recreated_at_any_gap();
    test_created_page_is_undoable_and_redoable();
    test_created_track_is_undoable_and_redoable();
    test_track_creation_history_failure_rolls_back_and_keeps_add_slot_open();
    test_macro_press_on_future_page_does_not_wrap_to_existing_step();
    test_step_focus_bottom_left_resets_focused_step_only();
    test_step_focus_copy_paste_copies_complete_step_without_selection();
    test_step_selection_copy_paste_extends_sparse_root_steps();
    test_step_selection_macro_long_press_consumes_release_without_toggling();
    test_step_selection_clear_is_undoable_and_keeps_selection_active();
    test_step_selection_wrap_paste_overwrites_inside_pattern();
    test_child_content_nav_enters_step_selection_and_pastes_child_steps();
    test_child_draft_owns_main_bottom_actions_until_single_apply();
    test_child_step_focus_bottom_actions_use_local_step_payload();

    std::cout << "\nAll SequencerStepHandler tests passed.\n";
    return 0;
}
