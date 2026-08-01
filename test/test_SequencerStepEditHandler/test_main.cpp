#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdio>
#include <cstring>

#include <array>

#include <config/App.hpp>
#include <config/Timing.hpp>
#include <filesystem>
#include <iostream>
#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>
#include <oc/impl/HostFileSystem.hpp>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerInputUtils.hpp"
#include "../../src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "../../src/handler/sequencer/SequencerStepContentDraftWorkflow.hpp"
#include "../../src/handler/sequencer/SequencerStepEditHandler.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/sequencer/ProjectTrackRuntimeSnapshotBank.hpp"
#include "../../src/sequencer/RealtimeMidiQueue.hpp"
#include "../../src/sequencer/SequencerPlaybackService.hpp"
#include "../../src/sequencer/SequencerRuntimeGraphBank.hpp"
#include "../../src/sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerChordUiOps.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"
#include "../../src/state/sequencer/SequencerStepEditRows.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/SequencerHistoryTransactionAssertions.hpp"

namespace {

uint32_t g_now_ms = 0;
uint32_t g_step_preset_time_lag_ms = 0;

uint32_t mockTimeMs() { return g_now_ms; }

uint32_t mockStepPresetTimeMs() { return g_now_ms - g_step_preset_time_lag_ms; }

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;
namespace input_utils = core::handler::sequencer::input_utils;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;
namespace tx = test_support::sequencer_transaction;

constexpr uint8_t ACTIVATED_ROW = step_edit_rows::ACTIVATED;
constexpr uint8_t NOTE_ROW = step_edit_rows::PROPERTY_OFFSET;
constexpr uint8_t VELOCITY_ROW = step_edit_rows::PROPERTY_OFFSET + 1U;
constexpr uint8_t GATE_ROW = step_edit_rows::PROPERTY_OFFSET + 2U;
constexpr uint8_t NUDGE_ROW = step_edit_rows::PROPERTY_OFFSET + 3U;
constexpr uint8_t CHANCE_ROW = step_edit_rows::PROPERTY_OFFSET + 4U;
constexpr uint8_t CHORD_ROW = step_edit_rows::CHORD;
constexpr uint8_t MICRO_SEQUENCE_ROW = step_edit_rows::MICRO_SEQUENCE;
constexpr uint8_t CYCLE_STATES_ROW = step_edit_rows::CYCLE_STATES;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() / "midi-studio-core-step-edit-handler-test";
}

void resetTestRoot() {
    std::error_code ec;
    std::filesystem::remove_all(testRoot(), ec);
}

bool stepHasMicroSequence(const core::state::sequencer::SequencerPatternState& pattern,
                          uint8_t rootStep) {
    const auto* graph = core::state::sequencer::graphView(pattern);
    const auto* node =
        graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(rootStep)) : nullptr;
    return node != nullptr && node->has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE) &&
           graph->sequence(node->childSequenceId) != nullptr;
}

bool stepHasCycleStates(const core::state::sequencer::SequencerPatternState& pattern,
                        uint8_t rootStep) {
    const auto* graph = core::state::sequencer::graphView(pattern);
    const auto* node =
        graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(rootStep)) : nullptr;
    return node != nullptr && node->has(oc::note::sequencer::STEP_NODE_CYCLE_SET) &&
           graph->cycleSet(node->cycleSetId) != nullptr;
}

struct GraphReachability {
    using Limits = oc::note::sequencer::StepSequencerGraphLimits;

    std::array<bool, Limits::MAX_STEP_NODES> stepNodes{};
    std::array<bool, Limits::MAX_SEQUENCES> sequences{};
    std::array<bool, Limits::MAX_CYCLE_SETS> cycleSets{};
};

void visitReachableGraphNode(const oc::note::sequencer::StepSequencerGraph& graph, uint16_t nodeId,
                             GraphReachability& reachable);

void markReservedGraphNode(const oc::note::sequencer::StepSequencerGraph& graph, uint16_t nodeId,
                           GraphReachability& reachable) {
    using namespace oc::note::sequencer;

    assert(nodeId < graph.stepNodeCount);
    assert(nodeId < reachable.stepNodes.size());
    assert(!reachable.stepNodes[nodeId]);
    reachable.stepNodes[nodeId] = true;
    const auto& node = graph.stepNodes[nodeId];
    assert(!node.has(STEP_NODE_CHILD_SEQUENCE));
    assert(!node.has(STEP_NODE_CYCLE_SET));
    assert(node.childSequenceId == StepSequencerGraphLimits::INVALID_ID);
    assert(node.cycleSetId == StepSequencerGraphLimits::INVALID_ID);
}

void visitReachableGraphSequence(const oc::note::sequencer::StepSequencerGraph& graph,
                                 uint16_t sequenceId, GraphReachability& reachable) {
    assert(sequenceId < graph.sequenceCount);
    assert(sequenceId < reachable.sequences.size());
    if (reachable.sequences[sequenceId]) return;
    reachable.sequences[sequenceId] = true;

    const auto* sequence = graph.sequence(sequenceId);
    assert(sequence != nullptr);
    for (uint8_t index = 0; index < sequence->length; ++index) {
        visitReachableGraphNode(graph, static_cast<uint16_t>(sequence->firstStepNode + index),
                                reachable);
    }
    const uint8_t reservedCapacity =
        sequence->kind == oc::note::sequencer::StepSequencerSequenceKind::RootPattern
            ? sequence->length
            : oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP;
    assert(static_cast<uint32_t>(sequence->firstStepNode) + reservedCapacity <=
           graph.stepNodeCount);
    for (uint8_t index = sequence->length; index < reservedCapacity; ++index) {
        markReservedGraphNode(graph, static_cast<uint16_t>(sequence->firstStepNode + index),
                              reachable);
    }
}

void visitReachableGraphCycleSet(const oc::note::sequencer::StepSequencerGraph& graph,
                                 uint16_t cycleSetId, GraphReachability& reachable) {
    assert(cycleSetId < graph.cycleSetCount);
    assert(cycleSetId < reachable.cycleSets.size());
    if (reachable.cycleSets[cycleSetId]) return;
    reachable.cycleSets[cycleSetId] = true;

    const auto* cycleSet = graph.cycleSet(cycleSetId);
    assert(cycleSet != nullptr);
    for (uint8_t index = 0; index < cycleSet->length; ++index) {
        visitReachableGraphNode(graph, static_cast<uint16_t>(cycleSet->firstStateNode + index),
                                reachable);
    }
    constexpr uint8_t reservedCapacity =
        oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET;
    assert(static_cast<uint32_t>(cycleSet->firstStateNode) + reservedCapacity <=
           graph.stepNodeCount);
    for (uint8_t index = cycleSet->length; index < reservedCapacity; ++index) {
        markReservedGraphNode(graph, static_cast<uint16_t>(cycleSet->firstStateNode + index),
                              reachable);
    }
}

void visitReachableGraphNode(const oc::note::sequencer::StepSequencerGraph& graph, uint16_t nodeId,
                             GraphReachability& reachable) {
    using namespace oc::note::sequencer;

    assert(nodeId < graph.stepNodeCount);
    assert(nodeId < reachable.stepNodes.size());
    if (reachable.stepNodes[nodeId]) return;
    reachable.stepNodes[nodeId] = true;

    const auto& node = graph.stepNodes[nodeId];
    if (node.has(STEP_NODE_CHILD_SEQUENCE)) {
        visitReachableGraphSequence(graph, node.childSequenceId, reachable);
    } else {
        assert(node.childSequenceId == StepSequencerGraphLimits::INVALID_ID);
    }
    if (node.has(STEP_NODE_CYCLE_SET)) {
        visitReachableGraphCycleSet(graph, node.cycleSetId, reachable);
    } else {
        assert(node.cycleSetId == StepSequencerGraphLimits::INVALID_ID);
    }
}

void assertGraphHasNoOrphans(const oc::note::sequencer::StepSequencerGraph& graph) {
    assert(graph.enabled);
    assert(graph.stepNodeCount <= graph.stepNodes.size());
    assert(graph.sequenceCount <= graph.sequences.size());
    assert(graph.cycleSetCount <= graph.cycleSets.size());

    GraphReachability reachable;
    visitReachableGraphSequence(graph, graph.rootSequenceId, reachable);
    for (uint16_t index = 0; index < graph.stepNodeCount; ++index) {
        assert(reachable.stepNodes[index]);
    }
    for (uint16_t index = 0; index < graph.sequenceCount; ++index) {
        assert(reachable.sequences[index]);
    }
    for (uint16_t index = 0; index < graph.cycleSetCount; ++index) {
        assert(reachable.cycleSets[index]);
    }
}

struct SequencerStepEditHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 901;
    static constexpr oc::type::ScopeID OVERLAY_SCOPE = 902;
    static constexpr oc::type::ScopeID PRESET_LIBRARY_SCOPE = 903;

    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::impl::HostFileSystem filesystem;
    core::persistence::ProductFileService productFiles;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::SequencerStepEditHandler handler;
    core::handler::SequencerPatternQuickControlsHandler quickControlsHandler;

    SequencerStepEditHarness()
        : state(storages.settings), filesystem(testRoot().string().c_str()),
          productFiles(filesystem), inputBinding(eventBus, mockTimeMs, Config::Input::CONFIG),
          buttons(inputBinding, buttonHw), encoders(inputBinding, encoderHw),
          overlays(state.overlays, buttons),
          handler(
              core::handler::SequencerStepEditHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.sequencerTracks,
                  state.structureClipboard,
                  state.trackNavigation,
                  state.patternPitchSettings,
                  state.structureNavigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
                  core::handler::SequencerStepPresetDomainServices::fromCoreState(state,
                                                                                  productFiles),
                  core::handler::SequencerChordPresetDomainServices::fromCoreState(state,
                                                                                   productFiles),
              },
              overlays, encoders, buttons, SEQUENCER_SCOPE, OVERLAY_SCOPE, PRESET_LIBRARY_SCOPE,
              mockStepPresetTimeMs),
          quickControlsHandler(
              core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
                  state.structureNavigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders, buttons, SEQUENCER_SCOPE) {
        resetTestRoot();
        assert(filesystem.init());
        assert(productFiles.init());
        overlays.setActiveViewProvider([]() { return SEQUENCER_SCOPE; });
        overlays.registerCleanup(core::ui::OverlayType::SEQ_STEP_EDIT, OVERLAY_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::PRESET_LIBRARY, PRESET_LIBRARY_SCOPE);
        state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        g_now_ms = 0;
        g_step_preset_time_lag_ms = 0;
    }

    void tick(uint32_t nowMs) {
        g_now_ms = nowMs;
        inputBinding.processTick();
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
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }
};

void longPressMacro(SequencerStepEditHarness& h, uint8_t indexInPage) {
    h.tick(0);
    h.press(Config::MACRO_BUTTONS[indexInPage]);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS - 1U);
    assert(!h.state.sequencer.stepEdit.visible.get());
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
}

void openStepEdit(SequencerStepEditHarness& h, uint8_t indexInPage) {
    longPressMacro(h, indexInPage);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);
}

void openStepPresetLibrary(SequencerStepEditHarness& h) {
    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.presetLibrary.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::PRESET_LIBRARY);

    // The authority transition quarantines the physical opener. Its release
    // must not immediately toggle the newly opened preset detail.
    h.release(Config::ButtonID::NAV);
    assert(h.state.sequencer.presetLibrary.visible.get());
    assert(!h.state.sequencer.presetLibrary.detailVisible.get());
}

void openChordPresetLibrary(SequencerStepEditHarness& h) {
    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    auto& picker = h.state.sequencer.presetLibrary;
    assert(picker.visible.get());
    assert(picker.libraryKind.get() == core::state::sequencer::SequencerPresetLibraryKind::CHORD);
    assert(h.overlays.current() == core::ui::OverlayType::PRESET_LIBRARY);

    h.release(Config::ButtonID::NAV);
    assert(picker.visible.get());
    assert(!picker.detailVisible.get());
}

void focusStepEditRow(SequencerStepEditHarness& h, uint8_t row) {
    for (uint8_t guard = 0;
         h.state.sequencer.stepEdit.focusedRow.get() != row && guard < step_edit_rows::COUNT * 2U;
         ++guard) {
        h.turn(Config::EncoderID::NAV, 1.0f);
    }
    if (h.state.sequencer.stepEdit.focusedRow.get() != row) {
        std::cerr << "Unable to focus row " << static_cast<int>(row)
                  << ", current=" << static_cast<int>(h.state.sequencer.stepEdit.focusedRow.get())
                  << "\n";
    }
    assert(h.state.sequencer.stepEdit.focusedRow.get() == row);
}

core::state::sequencer::SequencerStepChordUiState resolveChordPreview(SequencerStepEditHarness& h,
                                                                      uint8_t step) {
    const auto scale = core::state::sequencer::resolveEffectiveScaleSettings(
        h.state.sequencerTracks.projectScaleSettings(), h.state.sequencer.pattern.scalePolicy,
        h.state.sequencer.pattern.scaleOverride);
    auto chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, step);
    const auto projection =
        core::state::sequencer::resolveActiveContentStepProjection(h.state.sequencer, step, scale);
    core::state::sequencer::resolveStepChordPreview(chord, projection, scale);
    return chord;
}

float normalizedForScaleNote(uint8_t note,
                             oc::note::sequencer::StepSequencerScaleSettings settings) {
    return input_utils::indexToNormalized(input_utils::scaleDegreeIndexForNote(note, settings),
                                          input_utils::countScaleNotes(settings));
}

void holdPatternQuickControls(SequencerStepEditHarness& h) {
    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
}

void test_long_press_opens_step_edit_and_ignores_open_release() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(16);
    h.state.sequencer.page.set(1);

    openStepEdit(h, 2);
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 10);
    assert(h.state.sequencer.focusedStep.get() == 10);

    h.release(Config::MACRO_BUTTONS[2]);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);

    h.tap(Config::MACRO_BUTTONS[2]);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_long_press_opens_step_edit_and_ignores_open_release\n";
}

void test_left_center_nav_retargets_root_steps_across_pages() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(12);
    h.state.sequencer.page.set(0);
    oc::note::sequencer::StepBitMask128 enabled{};
    enabled.setBit(7);
    h.state.sequencer.pattern.enabledMask.set(enabled);

    openStepEdit(h, 7);
    h.release(Config::MACRO_BUTTONS[7]);
    focusStepEditRow(h, NOTE_ROW);

    h.press(Config::ButtonID::LEFT_CENTER);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 8);
    assert(h.state.sequencer.focusedStep.get() == 8);
    assert(h.state.sequencer.page.get() == 1);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == NOTE_ROW);
    assert(!h.state.sequencer.pattern.enabledMask.get().test(8));
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.stepEdit.visible.get());

    h.press(Config::ButtonID::LEFT_CENTER);
    h.turn(Config::EncoderID::NAV, -1.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 7);
    assert(h.state.sequencer.page.get() == 0);

    std::cout << "[PASS] test_left_center_nav_retargets_root_steps_across_pages\n";
}

void test_focused_step_entry_keeps_next_nav_tap_available() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(2);
    const bool enabledBefore = h.state.sequencer.pattern.isEnabled(2);

    assert(h.handler.openFocusedStepAtRow(ACTIVATED_ROW));
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 2);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == ACTIVATED_ROW);
    assert(h.state.sequencer.pattern.isEnabled(2) == enabledBefore);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.pattern.isEnabled(2) != enabledBefore);

    std::cout << "[PASS] test_focused_step_entry_keeps_next_nav_tap_available\n";
}

void test_direct_step_content_entry_opens_detail_or_child_without_intermediate_editor() {
    {
        SequencerStepEditHarness h;
        h.state.sequencer.pattern.setContentLength(8U);
        h.state.sequencer.focusedStep.set(2U);

        assert(h.handler.openFocusedStepContentAtRow(CHORD_ROW));
        assert(h.state.sequencer.stepEdit.visible.get());
        assert(h.state.sequencer.stepEdit.stepIndex.get() == 2U);
        assert(h.state.sequencer.stepEdit.focusedRow.get() == CHORD_ROW);
        assert(h.state.sequencer.stepEdit.chordEditor.active.get());
        assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);
    }

    {
        SequencerStepEditHarness h;
        h.state.sequencer.pattern.setContentLength(8U);
        h.state.sequencer.focusedStep.set(3U);

        assert(h.handler.openFocusedStepContentAtRow(MICRO_SEQUENCE_ROW));
        assert(!h.state.sequencer.stepEdit.visible.get());
        assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
        assert(h.state.sequencer.stepContentDraft.active.get());
        assert(h.overlays.current() == core::ui::OverlayType::NONE);
    }

    {
        SequencerStepEditHarness h;
        h.state.sequencer.pattern.setContentLength(8U);
        h.state.sequencer.focusedStep.set(4U);

        assert(h.handler.openFocusedStepContentAtRow(CYCLE_STATES_ROW));
        assert(!h.state.sequencer.stepEdit.visible.get());
        assert(core::state::sequencer::isCycleStatesContentView(h.state.sequencer));
        assert(h.state.sequencer.stepContentDraft.active.get());
        assert(h.overlays.current() == core::ui::OverlayType::NONE);
    }

    std::cout
        << "[PASS] "
           "test_direct_step_content_entry_opens_detail_or_child_without_intermediate_editor\n";
}

void test_nav_and_opt_edit_then_nav_confirms_without_closing() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.velocity[3] = 64;

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);

    focusStepEditRow(h, VELOCITY_ROW);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == VELOCITY_ROW);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.velocity[3] == 127);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.pattern.velocity[3] == 127);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_nav_and_opt_edit_then_nav_confirms_without_closing\n";
}

void test_left_bottom_hold_edits_local_variation_for_focused_property() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[2] = 60;

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);

    focusStepEditRow(h, NOTE_ROW);
    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepEdit.localVariationEditActive.get());

    h.turn(Config::EncoderID::OPT, 1.0f);
    const auto* graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(h.state.sequencer));
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node, core::state::sequencer::StepProperty::NOTE) ==
           input_utils::variationRangeMaxForProperty(core::state::sequencer::StepProperty::NOTE));
    assert(h.state.sequencer.pattern.note[2] == 60);

    h.release(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepEdit.localVariationEditActive.get());

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.note[2] == 127);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.visible.get());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    // Creating the local-variation Graph and the later Flat note edit use
    // different prepared storage plans, so they are adjacent chronological
    // segments rather than one over-captured aggregate entry.
    assert(h.state.sequencerHistory.undoCount() == 2);

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[2] == 60);
    const auto* graphAfterNoteUndo = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterNoteUndo != nullptr);
    const auto* nodeAfterNoteUndo =
        graphAfterNoteUndo->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(nodeAfterNoteUndo != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *nodeAfterNoteUndo, core::state::sequencer::StepProperty::NOTE) ==
           input_utils::variationRangeMaxForProperty(core::state::sequencer::StepProperty::NOTE));

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[2] == 60);
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);

    assert(h.state.redoProjectHistory());
    const auto* graphAfterVariationRedo =
        core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterVariationRedo != nullptr);
    assert(h.state.sequencer.pattern.note[2] == 60);

    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.note[2] == 127);
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) != nullptr);

    std::cout << "[PASS] test_left_bottom_hold_edits_local_variation_for_focused_property\n";
}

void test_chance_row_does_not_enter_local_variation_mode() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    openStepEdit(h, 1);
    h.release(Config::MACRO_BUTTONS[1]);

    focusStepEditRow(h, CHANCE_ROW);
    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepEdit.localVariationEditActive.get());
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);
    h.release(Config::ButtonID::LEFT_BOTTOM);

    std::cout << "[PASS] test_chance_row_does_not_enter_local_variation_mode\n";
}

void test_local_variation_edit_targets_active_child_step_node() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.velocity[0] = 64;

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));

    openStepEdit(h, 1);
    h.release(Config::MACRO_BUTTONS[1]);

    focusStepEditRow(h, VELOCITY_ROW);
    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepEdit.localVariationEditActive.get());
    h.turn(Config::EncoderID::OPT, 1.0f);

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* root = graph->stepNode(rootNode);
    assert(root != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto* child = graph->stepNode(static_cast<uint16_t>(sequence->firstStepNode + 1U));
    assert(child != nullptr);

    assert(core::state::sequencer::nodeLocalVariationRange(
               *root, core::state::sequencer::StepProperty::VELOCITY) == 0);
    assert(
        core::state::sequencer::nodeLocalVariationRange(
            *child, core::state::sequencer::StepProperty::VELOCITY) ==
        input_utils::variationRangeMaxForProperty(core::state::sequencer::StepProperty::VELOCITY));
    assert(child->velocityOffset == 0);

    h.release(Config::ButtonID::LEFT_BOTTOM);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.visible.get());
    h.tap(Config::MACRO_BUTTONS[1]);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_local_variation_edit_targets_active_child_step_node\n";
}

void test_context_rows_are_focusable_and_root_action_rows_do_not_edit_properties() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[3] = 60;
    h.state.sequencer.pattern.velocity[3] = 64;
    h.state.sequencer.pattern.gate[3] = 70;
    h.state.sequencer.pattern.nudge[3] = 0;
    h.state.sequencer.pattern.probability[3] = 80;

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);

    assert(h.state.sequencer.stepEdit.focusedRow.get() == ACTIVATED_ROW);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == CHANCE_ROW);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == NOTE_ROW);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == VELOCITY_ROW);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == GATE_ROW);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == NUDGE_ROW);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == CHORD_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.note[3] == 60);
    assert(h.state.sequencer.pattern.velocity[3] == 64);
    assert(h.state.sequencer.pattern.gate[3] == 70);
    assert(h.state.sequencer.pattern.nudge[3] == 0);
    assert(h.state.sequencer.pattern.probability[3] == 80);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == MICRO_SEQUENCE_ROW);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.note[3] == 60);
    assert(h.state.sequencer.pattern.velocity[3] == 64);
    assert(h.state.sequencer.pattern.gate[3] == 70);
    assert(h.state.sequencer.pattern.nudge[3] == 0);
    assert(h.state.sequencer.pattern.probability[3] == 80);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == CYCLE_STATES_ROW);
    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.sequencer.pattern.note[3] == 60);
    assert(h.state.sequencer.pattern.velocity[3] == 64);
    assert(h.state.sequencer.pattern.gate[3] == 70);
    assert(h.state.sequencer.pattern.nudge[3] == 0);
    assert(h.state.sequencer.pattern.probability[3] == 80);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == ACTIVATED_ROW);

    std::cout
        << "[PASS] test_context_rows_are_focusable_and_root_action_rows_do_not_edit_properties\n";
}

void test_activated_row_edits_root_step_enabled_state() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.setEnabled(3, false);

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);
    focusStepEditRow(h, ACTIVATED_ROW);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == ACTIVATED_ROW);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.isEnabled(3));

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(!h.state.sequencer.pattern.isEnabled(3));

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.pattern.isEnabled(3));
    assert(h.state.sequencer.stepEdit.visible.get());

    h.tap(Config::MACRO_BUTTONS[3]);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_activated_row_edits_root_step_enabled_state\n";
}

void test_create_edit_and_commit_micro_sequence_context() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[3] = 60;
    h.state.sequencer.pattern.velocity[3] = 64;

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);

    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == MICRO_SEQUENCE_ROW);

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(h.state.sequencer.contentView.parentStep.get() == 3);
    assert(h.state.sequencer.contentView.length.get() == 2);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 3));
    assert(stepHasMicroSequence(core::state::sequencer::authoringPattern(h.state.sequencer), 3));

    const auto* graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(h.state.sequencer));
    assert(graph != nullptr);
    const auto* root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    assert(root != nullptr);
    const auto* sequence = graph->sequence(root->childSequenceId);
    assert(sequence != nullptr);
    const auto* firstMicroStep = graph->stepNode(sequence->firstStepNode);
    assert(firstMicroStep != nullptr);
    assert(firstMicroStep->noteOffset == 0);

    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer, 0, core::state::sequencer::StepProperty::NOTE, 62.0f / 127.0f,
        h.state.sequencer.pattern.pitchEditMode, {}));

    graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(h.state.sequencer));
    root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    sequence = graph->sequence(root->childSequenceId);
    firstMicroStep = graph->stepNode(sequence->firstStepNode);
    assert(firstMicroStep != nullptr);
    assert(firstMicroStep->noteOffset == 2);

    holdPatternQuickControls(h);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::LENGTH);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.contentView.length.get() == 16);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    h.state.sequencer.focusedStep.set(15);
    h.state.sequencer.page.set(1);
    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer, 15, core::state::sequencer::StepProperty::VELOCITY, 127.0f / 127.0f,
        h.state.sequencer.pattern.pitchEditMode, {}));

    graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(h.state.sequencer));
    root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    sequence = graph->sequence(root->childSequenceId);
    const auto* lastMicroStep =
        graph->stepNode(static_cast<uint16_t>(sequence->firstStepNode + 15U));
    assert(lastMicroStep != nullptr);
    assert(lastMicroStep->velocityOffset == 63);

    assert(core::handler::sequencer::step_content_draft_workflow::apply(
        h.state.sequencer, h.state.sequencerTracks,
        core::handler::SequencerHistoryDomainServices::fromCoreState(h.state)));
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 3));
    assert(core::state::sequencer::leaveContentView(h.state.sequencer));
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_create_edit_and_commit_micro_sequence_context\n";
}

void test_step_edit_opens_nested_content_from_child_contexts() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[3] = 60;
    h.state.sequencer.pattern.velocity[3] = 64;

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(core::state::sequencer::activeContentDepth(h.state.sequencer) == 1);

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(core::state::sequencer::activeContentDepth(h.state.sequencer) == 2);
    assert(h.state.sequencer.contentView.length.get() == 2);

    assert(core::state::sequencer::leaveContentView(h.state.sequencer));
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(core::state::sequencer::activeContentDepth(h.state.sequencer) == 1);

    openStepEdit(h, 1);
    h.release(Config::MACRO_BUTTONS[1]);
    focusStepEditRow(h, CYCLE_STATES_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isCycleStatesContentView(h.state.sequencer));
    assert(core::state::sequencer::activeContentDepth(h.state.sequencer) == 2);
    assert(h.state.sequencer.contentView.length.get() == 4);

    assert(core::state::sequencer::leaveContentView(h.state.sequencer));
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(core::state::sequencer::leaveContentView(h.state.sequencer));
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));

    std::cout << "[PASS] test_step_edit_opens_nested_content_from_child_contexts\n";
}

void test_cycle_state_context_length_is_editable_to_sixteen() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[2] = 60;
    h.state.sequencer.pattern.velocity[2] = 64;

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);
    focusStepEditRow(h, CYCLE_STATES_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isCycleStatesContentView(h.state.sequencer));
    assert(core::state::sequencer::activeContentLength(h.state.sequencer) == 4);

    holdPatternQuickControls(h);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::LENGTH);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(core::state::sequencer::activeContentLength(h.state.sequencer) == 16);
    h.release(Config::ButtonID::LEFT_CENTER);

    h.state.sequencer.focusedStep.set(15);
    h.state.sequencer.page.set(1);
    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer, 15, core::state::sequencer::StepProperty::VELOCITY, 127.0f / 127.0f,
        h.state.sequencer.pattern.pitchEditMode, {}));

    const auto* graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(h.state.sequencer));
    assert(graph != nullptr);
    const auto* root = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(root != nullptr);
    const auto* cycleSet = graph->cycleSet(root->cycleSetId);
    assert(cycleSet != nullptr);
    assert(cycleSet->length == 16);
    const auto* lastState = graph->stepNode(static_cast<uint16_t>(cycleSet->firstStateNode + 15U));
    assert(lastState != nullptr);
    assert(lastState->velocityOffset == 63);

    std::cout << "[PASS] test_cycle_state_context_length_is_editable_to_sixteen\n";
}

void test_child_context_offset_wraps_steps_from_quick_controls() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[0] = 60;
    h.state.sequencer.pattern.velocity[0] = 64;

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 4);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));

    auto* graph = h.state.sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto wrappedSourceNode = static_cast<uint16_t>(sequence->firstStepNode + 3U);
    assert(
        core::state::sequencer::setNodeNoteOffset(h.state.sequencer.pattern, wrappedSourceNode, 7));

    auto projection =
        core::state::sequencer::resolveActiveContentStepProjection(h.state.sequencer, 0, {});
    assert(projection.valid);
    assert(projection.note == 60);

    holdPatternQuickControls(h);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::LENGTH);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternQuickControls.focusedItem.get() ==
           core::state::sequencer::PatternQuickControlItem::OFFSET);
    h.turn(Config::EncoderID::OPT, 4.0f / 6.0f);
    graph = h.state.sequencer.pattern.graph.get();
    assert(graph != nullptr);
    sequence = graph->sequence(h.state.sequencer.contentView.sequenceId.get());
    assert(sequence != nullptr);
    assert(sequence->offset == 0);
    h.release(Config::ButtonID::LEFT_CENTER);

    projection =
        core::state::sequencer::resolveActiveContentStepProjection(h.state.sequencer, 0, {});
    assert(projection.valid);
    assert(projection.note == 67);

    std::cout << "[PASS] test_child_context_offset_wraps_steps_from_quick_controls\n";
}

void test_micro_sequence_note_offsets_follow_parent_scale_degrees() {
    SequencerStepEditHarness h;
    const oc::note::sequencer::StepSequencerScaleSettings cMajor{
        .root = 0,
        .type = oc::note::sequencer::StepSequencerScaleType::Major,
        .mode = oc::note::sequencer::StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    h.state.sequencerTracks.setProjectScaleSettings(cMajor);
    h.state.sequencer.setPitchEditMode(
        core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE);
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[3] = 60;

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));

    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer, 0, core::state::sequencer::StepProperty::NOTE,
        normalizedForScaleNote(62, cMajor), h.state.sequencer.pattern.pitchEditMode, cMajor));

    const auto* graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(h.state.sequencer));
    assert(graph != nullptr);
    const auto* root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    assert(root != nullptr);
    const auto* sequence = graph->sequence(root->childSequenceId);
    assert(sequence != nullptr);
    const auto* firstMicroStep = graph->stepNode(sequence->firstStepNode);
    assert(firstMicroStep != nullptr);
    assert(firstMicroStep->noteOffset == 1);

    auto projection =
        core::state::sequencer::resolveActiveContentStepProjection(h.state.sequencer, 0, cMajor);
    assert(projection.valid);
    assert(projection.note == 62);
    assert(projection.noteOffset == 1);

    core::state::sequencer::authoringPattern(h.state.sequencer).setStepNoteAt(3, 62);
    projection =
        core::state::sequencer::resolveActiveContentStepProjection(h.state.sequencer, 0, cMajor);
    assert(projection.valid);
    assert(projection.parentNote == 62);
    assert(projection.note == 64);
    assert(projection.noteOffset == 1);

    std::cout << "[PASS] test_micro_sequence_note_offsets_follow_parent_scale_degrees\n";
}

void test_back_from_child_step_edit_returns_to_parent_and_records_history() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[0] = 60;

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, NOTE_ROW);
    h.turn(Config::EncoderID::OPT, 62.0f / 127.0f);

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto* firstMicroStep = graph->stepNode(sequence->firstStepNode);
    assert(firstMicroStep != nullptr);
    assert(firstMicroStep->noteOffset != 0);

    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 0);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == MICRO_SEQUENCE_ROW);
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    firstMicroStep = graph->stepNode(sequence->firstStepNode);
    assert(firstMicroStep != nullptr);
    assert(firstMicroStep->noteOffset != 0);
    assert(h.state.sequencerHistory.undoCount() == 1);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_back_from_child_step_edit_returns_to_parent_and_records_history\n";
}

void test_pristine_child_draft_back_abandons_before_returning_to_parent() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    openStepEdit(h, 4);
    h.release(Config::MACRO_BUTTONS[4]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(!h.state.sequencer.stepContentDraft.modified());
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 4));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(!h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 4));

    h.tap(Config::ButtonID::LEFT_TOP);

    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 4));
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 4);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == MICRO_SEQUENCE_ROW);
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_pristine_child_draft_back_abandons_before_returning_to_parent\n";
}

void test_modified_child_draft_back_prompts_and_default_save_returns_to_parent() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    openStepEdit(h, 5);
    h.release(Config::MACRO_BUTTONS[5]);
    focusStepEditRow(h, CYCLE_STATES_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isCycleStatesContentView(h.state.sequencer));
    assert(h.state.sequencer.stepContentDraft.active.get());

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, VELOCITY_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.stepContentDraft.modified());

    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(core::state::sequencer::isCycleStatesContentView(h.state.sequencer));
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(!h.state.sequencer.stepContentDraft.exitPromptVisible.get());

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(core::state::sequencer::isCycleStatesContentView(h.state.sequencer));
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(h.state.sequencer.stepContentDraft.exitChoice.get() ==
           core::state::sequencer::SequencerStepContentDraftExitChoice::SAVE);
    assert(!stepHasCycleStates(h.state.sequencer.pattern, 5));

    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(stepHasCycleStates(h.state.sequencer.pattern, 5));
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 5);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == CYCLE_STATES_ROW);
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout
        << "[PASS] test_modified_child_draft_back_prompts_and_default_save_returns_to_parent\n";
}

void test_step_draft_exit_prompt_is_modal_for_short_and_long_actions() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, NOTE_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.stepContentDraft.modified());

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 3));

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.structureClipboard.kind.get() == core::state::StructureClipboardKind::NONE);

    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(!h.state.sequencer.presetLibrary.visible.get());
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    h.release(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 3));
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_step_draft_exit_prompt_is_modal_for_short_and_long_actions\n";
}

void test_child_draft_step_editor_exposes_reset_but_blocks_hidden_preset() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(h.state.sequencer.stepContentDraft.active.get());

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, NOTE_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.stepContentDraft.modified());
    const auto draftNode = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
    const auto* draftGraph =
        core::state::sequencer::authoringPattern(h.state.sequencer).graph.get();
    assert(draftGraph != nullptr);
    const int8_t authoredOffset = draftGraph->stepNodes[draftNode].noteOffset;
    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    const uint32_t clipboardRevision = h.state.structureClipboard.revision.get();

    // Reset is a visible draft-local edit, while the hidden preset binding may
    // not open a second editor over unpublished child content.
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);

    assert(h.state.sequencer.stepContentDraft.active.get());
    draftGraph = core::state::sequencer::authoringPattern(h.state.sequencer).graph.get();
    assert(draftGraph != nullptr);
    assert(authoredOffset != 0);
    assert(draftGraph->stepNodes[draftNode].noteOffset == 0);
    assert(!h.state.sequencer.presetLibrary.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 3));
    assert(h.state.sequencerHistory.undoCount() == undoBefore);
    assert(h.state.structureClipboard.revision.get() == clipboardRevision);
    h.inputBinding.consumePress(static_cast<oc::type::ButtonID>(Config::ButtonID::NAV));
    h.release(Config::ButtonID::NAV);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepContentDraft.active.get());

    std::cout << "[PASS] test_child_draft_step_editor_exposes_reset_but_blocks_hidden_preset\n";
}

void test_child_step_editor_bottom_right_applies_draft_not_copy() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto clipboardSourceNode = core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::createMicroSequence(h.state.sequencer.pattern,
                                                       clipboardSourceNode, 2)
               .ok);
    const auto* sourceGraph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(sourceGraph != nullptr);
    assert(h.state.structureClipboard.storeSequencerStepContent(
        *sourceGraph, clipboardSourceNode,
        core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE));
    const uint32_t clipboardRevision = h.state.structureClipboard.revision.get();
    const auto clipboardNodeId = h.state.structureClipboard.sequencerStepContentNodeId;

    openStepEdit(h, 6);
    h.release(Config::MACRO_BUTTONS[6]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, NOTE_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.stepContentDraft.modified());
    const auto localNodeId = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
    assert(core::state::sequencer::createCycleStateSet(
               core::state::sequencer::authoringPattern(h.state.sequencer), localNodeId,
               core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT)
               .ok);
    core::state::sequencer::notifyStepContentDraftMutation(h.state.sequencer);
    focusStepEditRow(h, CYCLE_STATES_ROW);
    assert(h.state.structureClipboard.kind.get() ==
           core::state::StructureClipboardKind::SEQUENCER_STEP_CONTENT);

    // Remove is unavailable while the child editor is authoring an unpublished
    // outer draft, even when the focused row contains removable content.
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(core::state::sequencer::activeContentStepHasChildContent(
        h.state.sequencer, 0, core::state::sequencer::StepContentChildKind::CYCLE_STATES));

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 6));
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.structureClipboard.kind.get() ==
           core::state::StructureClipboardKind::SEQUENCER_STEP_CONTENT);
    assert(h.state.structureClipboard.revision.get() == clipboardRevision);
    assert(h.state.structureClipboard.sequencerStepContentKind ==
           core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE);
    assert(h.state.structureClipboard.sequencerStepContentNodeId == clipboardNodeId);
    assert(core::state::sequencer::activeContentStepHasChildContent(
        h.state.sequencer, 0, core::state::sequencer::StepContentChildKind::CYCLE_STATES));

    // Apply consumed only its own release. The next real tap must expose the
    // ordinary contextual Copy action immediately after publication.
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.structureClipboard.revision.get() == clipboardRevision + 1U);
    assert(h.state.structureClipboard.sequencerStepContentKind ==
           core::state::SequencerStepContentClipboardKind::CYCLE_STATES);

    std::cout << "[PASS] test_child_step_editor_bottom_right_applies_draft_not_copy\n";
}

void test_child_step_editor_bottom_right_hold_applies_without_paste() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto clipboardSourceNode = core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::createMicroSequence(h.state.sequencer.pattern,
                                                       clipboardSourceNode, 2)
               .ok);
    const auto* sourceGraph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(sourceGraph != nullptr);
    assert(h.state.structureClipboard.storeSequencerStepContent(
        *sourceGraph, clipboardSourceNode,
        core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE));
    const uint32_t clipboardRevision = h.state.structureClipboard.revision.get();
    const auto clipboardNodeId = h.state.structureClipboard.sequencerStepContentNodeId;

    openStepEdit(h, 7);
    h.release(Config::MACRO_BUTTONS[7]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, NOTE_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.stepContentDraft.modified());
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    assert(!core::state::sequencer::activeContentStepHasChildContent(
        h.state.sequencer, 0, core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE));

    // A held Apply must neither invoke the compatible Paste long action nor
    // fall through to Copy when release publishes the draft.
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 7));
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(!core::state::sequencer::activeContentStepHasChildContent(
        h.state.sequencer, 0, core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE));
    assert(h.state.structureClipboard.revision.get() == clipboardRevision);
    assert(h.state.structureClipboard.kind.get() ==
           core::state::StructureClipboardKind::SEQUENCER_STEP_CONTENT);
    assert(h.state.structureClipboard.sequencerStepContentKind ==
           core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE);
    assert(h.state.structureClipboard.sequencerStepContentNodeId == clipboardNodeId);

    std::cout << "[PASS] test_child_step_editor_bottom_right_hold_applies_without_paste\n";
}

void test_nested_chord_editor_keeps_its_owning_micro_draft() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    openStepEdit(h, 7);
    h.release(Config::MACRO_BUTTONS[7]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(h.state.sequencer.stepContentDraft.kind.get() ==
           core::state::sequencer::SequencerStepContentDraftKind::MICRO_SEQUENCE);

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, CHORD_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(!h.state.sequencer.stepContentDraft.modified());

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(!h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));

    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 7));
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_nested_chord_editor_keeps_its_owning_micro_draft\n";
}

void test_step_edit_context_rows_clear_selected_child_context() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2).ok);
    assert(core::state::sequencer::createCycleStateSet(h.state.sequencer.pattern, rootNode, 4).ok);
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(stepHasCycleStates(h.state.sequencer.pattern, 0));

    const auto* graphBeforeClear = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphBeforeClear != nullptr);
    assertGraphHasNoOrphans(*graphBeforeClear);
    const auto* graphOwner = graphBeforeClear;
    const uint16_t nodesBeforeMicroClear = graphBeforeClear->stepNodeCount;
    const uint8_t sequencesBeforeMicroClear = graphBeforeClear->sequenceCount;
    const uint8_t cyclesBeforeMicroClear = graphBeforeClear->cycleSetCount;

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);

    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(stepHasCycleStates(h.state.sequencer.pattern, 0));
    assert(h.state.sequencerHistory.undoCount() == 0);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        h.press(Config::ButtonID::BOTTOM_LEFT);
        h.advance(0);
        h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        h.release(Config::ButtonID::BOTTOM_LEFT);
        tx::assertMaxPlusOneStillArmed(4U);
    }
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(stepHasCycleStates(h.state.sequencer.pattern, 0));
    assert(h.state.sequencerHistory.undoCount() == 1);
    const auto* graphAfterMicroClear = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterMicroClear == graphOwner);
    assert(graphAfterMicroClear->stepNodeCount ==
           nodesBeforeMicroClear -
               oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);
    assert(graphAfterMicroClear->sequenceCount == sequencesBeforeMicroClear - 1U);
    assert(graphAfterMicroClear->cycleSetCount == cyclesBeforeMicroClear);
    assertGraphHasNoOrphans(*graphAfterMicroClear);

    const uint16_t nodesBeforeCycleClear = graphAfterMicroClear->stepNodeCount;
    const uint8_t sequencesBeforeCycleClear = graphAfterMicroClear->sequenceCount;
    const uint8_t cyclesBeforeCycleClear = graphAfterMicroClear->cycleSetCount;

    focusStepEditRow(h, CYCLE_STATES_ROW);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(stepHasCycleStates(h.state.sequencer.pattern, 0));
    assert(h.state.sequencerHistory.undoCount() == 1);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        h.press(Config::ButtonID::BOTTOM_LEFT);
        h.advance(0);
        h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        h.release(Config::ButtonID::BOTTOM_LEFT);
        tx::assertMaxPlusOneStillArmed(4U);
    }
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(!stepHasCycleStates(h.state.sequencer.pattern, 0));
    assert(h.state.sequencerHistory.undoCount() == 2);
    const auto* graphAfterCycleClear = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterCycleClear == graphOwner);
    assert(graphAfterCycleClear->stepNodeCount ==
           nodesBeforeCycleClear -
               oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET);
    assert(graphAfterCycleClear->sequenceCount == sequencesBeforeCycleClear);
    assert(graphAfterCycleClear->cycleSetCount == cyclesBeforeCycleClear - 1U);
    assertGraphHasNoOrphans(*graphAfterCycleClear);

    std::cout << "[PASS] test_step_edit_context_rows_clear_selected_child_context\n";
}

void test_graph_compaction_remaps_or_closes_active_child_content_view() {
    {
        core::state::sequencer::SequencerState state;
        const auto firstRoot = core::state::sequencer::rootStepNodeId(0);
        const auto secondRoot = core::state::sequencer::rootStepNodeId(1);
        const auto first = core::state::sequencer::createMicroSequence(state.pattern, firstRoot, 2);
        assert(first.ok);
        const auto second =
            core::state::sequencer::createMicroSequence(state.pattern, secondRoot, 2);
        assert(second.ok);
        assert(core::state::sequencer::enterMicroSequenceContentView(state, secondRoot, second.id));

        assert(core::state::sequencer::clearNodeChildSequence(state.pattern, firstRoot));
        assert(core::state::sequencer::compactSequencerGraph(state));

        assert(core::state::sequencer::isMicroSequenceContentView(state));
        assert(state.contentView.ownerNodeId.get() == secondRoot);
        assert(state.contentView.sequenceId.get() == 1);
        assert(state.contentView.length.get() == 2);
    }

    {
        core::state::sequencer::SequencerState state;
        const auto root = core::state::sequencer::rootStepNodeId(0);
        const auto micro = core::state::sequencer::createMicroSequence(state.pattern, root, 2);
        assert(micro.ok);
        assert(core::state::sequencer::enterMicroSequenceContentView(state, root, micro.id));

        assert(core::state::sequencer::clearNodeChildSequence(state.pattern, root));
        assert(core::state::sequencer::compactSequencerGraph(state));

        assert(core::state::sequencer::isRootContentView(state));
        assert(state.contentView.ownerNodeId.get() ==
               oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID);
    }

    std::cout << "[PASS] test_graph_compaction_remaps_or_closes_active_child_content_view\n";
}

void test_step_edit_context_rows_copy_and_paste_step_content() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto sourceNode = core::state::sequencer::rootStepNodeId(0);
    const auto sourceMicro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, sourceNode, 2);
    assert(sourceMicro.ok);
    const auto* sourceSequence = h.state.sequencer.pattern.graph->sequence(sourceMicro.id);
    assert(sourceSequence != nullptr);
    assert(core::state::sequencer::setNodeNoteOffset(h.state.sequencer.pattern,
                                                     sourceSequence->firstStepNode, 7));

    const auto destinationNode = core::state::sequencer::rootStepNodeId(1);
    const auto replacedMicro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, destinationNode, 4);
    assert(replacedMicro.ok);
    const auto* replacedSequence = h.state.sequencer.pattern.graph->sequence(replacedMicro.id);
    assert(replacedSequence != nullptr);
    assert(core::state::sequencer::setNodeNoteOffset(h.state.sequencer.pattern,
                                                     replacedSequence->firstStepNode, 11));
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 1));

    const auto* graphBeforePaste = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphBeforePaste != nullptr);
    assertGraphHasNoOrphans(*graphBeforePaste);
    const auto* graphOwner = graphBeforePaste;
    const uint16_t nodesBeforePaste = graphBeforePaste->stepNodeCount;
    const uint8_t sequencesBeforePaste = graphBeforePaste->sequenceCount;
    const uint8_t cyclesBeforePaste = graphBeforePaste->cycleSetCount;

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerStepContent());
    h.tap(Config::ButtonID::LEFT_TOP);

    openStepEdit(h, 1);
    h.release(Config::MACRO_BUTTONS[1]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        h.press(Config::ButtonID::BOTTOM_RIGHT);
        h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        h.release(Config::ButtonID::BOTTOM_RIGHT);
        tx::assertMaxPlusOneStillArmed(4U);
    }

    assert(stepHasMicroSequence(h.state.sequencer.pattern, 1));
    assert(h.state.sequencerHistory.undoCount() == 1);
    const auto* graphAfterPaste = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterPaste == graphOwner);
    assert(graphAfterPaste->stepNodeCount == nodesBeforePaste);
    assert(graphAfterPaste->sequenceCount == sequencesBeforePaste);
    assert(graphAfterPaste->cycleSetCount == cyclesBeforePaste);
    const auto* destinationRoot = graphAfterPaste->stepNode(destinationNode);
    assert(destinationRoot != nullptr);
    const auto* pastedSequence = graphAfterPaste->sequence(destinationRoot->childSequenceId);
    assert(pastedSequence != nullptr);
    assert(pastedSequence->length == 2U);
    assert(graphAfterPaste->stepNodes[pastedSequence->firstStepNode].noteOffset == 7);
    assertGraphHasNoOrphans(*graphAfterPaste);

    assert(h.state.undoProjectHistory());
    const auto* graphAfterUndo = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterUndo != nullptr);
    const auto* restoredRoot = graphAfterUndo->stepNode(destinationNode);
    assert(restoredRoot != nullptr);
    const auto* restoredSequence = graphAfterUndo->sequence(restoredRoot->childSequenceId);
    assert(restoredSequence != nullptr);
    assert(restoredSequence->length == 4U);
    assert(graphAfterUndo->stepNodes[restoredSequence->firstStepNode].noteOffset == 11);
    assertGraphHasNoOrphans(*graphAfterUndo);

    std::cout << "[PASS] test_step_edit_context_rows_copy_and_paste_step_content\n";
}

void test_step_edit_context_preflight_failures_are_atomic() {
    for (std::size_t ordinal = 1U; ordinal <= 4U; ++ordinal) {
        SequencerStepEditHarness h;
        h.state.sequencer.pattern.setContentLength(8);
        const auto rootNode = core::state::sequencer::rootStepNodeId(0);
        assert(
            core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2).ok);
        assert(
            core::state::sequencer::createCycleStateSet(h.state.sequencer.pattern, rootNode, 4).ok);

        openStepEdit(h, 0);
        h.release(Config::MACRO_BUTTONS[0]);
        focusStepEditRow(h, MICRO_SEQUENCE_ROW);

        const auto* graphOwner = core::state::sequencer::graphView(h.state.sequencer.pattern);
        assert(graphOwner != nullptr);
        assertGraphHasNoOrphans(*graphOwner);
        core::state::sequencer::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);
        const auto invariantBefore = tx::captureStateInvariant(h.state);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            h.press(Config::ButtonID::BOTTOM_LEFT);
            h.advance(0);
            h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
            h.release(Config::ButtonID::BOTTOM_LEFT);
            tx::assertFailureConsumed(ordinal);
        }

        const auto* graphAfter = core::state::sequencer::graphView(h.state.sequencer.pattern);
        assert(graphAfter == graphOwner);
        assertGraphHasNoOrphans(*graphAfter);
        assert(stepHasMicroSequence(h.state.sequencer.pattern, 0));
        assert(stepHasCycleStates(h.state.sequencer.pattern, 0));
        tx::assertMusicalSnapshot(h.state, musicalBefore);
        tx::assertStateInvariant(h.state, invariantBefore);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    for (std::size_t ordinal = 1U; ordinal <= 4U; ++ordinal) {
        SequencerStepEditHarness h;
        h.state.sequencer.pattern.setContentLength(8);
        const auto sourceNode = core::state::sequencer::rootStepNodeId(0);
        const auto destinationNode = core::state::sequencer::rootStepNodeId(1);
        assert(core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, sourceNode, 2)
                   .ok);
        assert(core::state::sequencer::createMicroSequence(h.state.sequencer.pattern,
                                                           destinationNode, 4)
                   .ok);

        openStepEdit(h, 0);
        h.release(Config::MACRO_BUTTONS[0]);
        focusStepEditRow(h, MICRO_SEQUENCE_ROW);
        h.tap(Config::ButtonID::BOTTOM_RIGHT);
        assert(h.state.structureClipboard.hasSequencerStepContent());
        h.tap(Config::ButtonID::LEFT_TOP);
        openStepEdit(h, 1);
        h.release(Config::MACRO_BUTTONS[1]);
        focusStepEditRow(h, MICRO_SEQUENCE_ROW);

        const auto* graphOwner = core::state::sequencer::graphView(h.state.sequencer.pattern);
        assert(graphOwner != nullptr);
        assertGraphHasNoOrphans(*graphOwner);
        core::state::sequencer::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);
        const auto invariantBefore = tx::captureStateInvariant(h.state);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            h.press(Config::ButtonID::BOTTOM_RIGHT);
            h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
            h.release(Config::ButtonID::BOTTOM_RIGHT);
            tx::assertFailureConsumed(ordinal);
        }

        const auto* graphAfter = core::state::sequencer::graphView(h.state.sequencer.pattern);
        assert(graphAfter == graphOwner);
        assertGraphHasNoOrphans(*graphAfter);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
        tx::assertStateInvariant(h.state, invariantBefore);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    std::cout << "[PASS] all context clear/paste preflight failures are atomic\n";
}

void test_step_edit_context_paste_prepares_graphless_destination() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    core::state::sequencer::SequencerPatternState source;
    source.setContentLength(8);
    const auto sourceRoot = core::state::sequencer::rootStepNodeId(0);
    const auto sourceMicro = core::state::sequencer::createMicroSequence(source, sourceRoot, 2);
    assert(sourceMicro.ok);
    const auto* sourceGraph = core::state::sequencer::graphView(source);
    assert(sourceGraph != nullptr);
    assert(h.state.structureClipboard.storeSequencerStepContent(
        *sourceGraph, sourceRoot, core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE));
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);

    openStepEdit(h, 4);
    h.release(Config::MACRO_BUTTONS[4]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(stepHasMicroSequence(h.state.sequencer.pattern, 4));
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    assert(h.state.undoProjectHistory());
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 4));
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);

    assert(h.state.redoProjectHistory());
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 4));

    std::cout << "[PASS] graphless context paste uses prospective prepared Graph\n";
}

void test_step_edit_musical_row_bottom_left_resets_row_to_default() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[2] = 74;
    h.state.sequencer.pattern.velocity[2] = 105;
    h.state.sequencer.pattern.setEnabled(2, true);
    assert(core::state::sequencer::setNodeLocalVariationRange(
        h.state.sequencer.pattern, core::state::sequencer::rootStepNodeId(2),
        core::state::sequencer::StepProperty::NOTE, 3));

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);
    focusStepEditRow(h, NOTE_ROW);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.pattern.note[2] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node, core::state::sequencer::StepProperty::NOTE) == 0);
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_step_edit_musical_row_bottom_left_resets_row_to_default\n";
}

void test_step_edit_chord_row_opens_transactional_editor_and_applies_once() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);
    focusStepEditRow(h, CHORD_ROW);

    // The row itself is an explicit Create/Edit action. Turning OPT must not
    // revive the former live-publication shortcut.
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);
    assert(!h.state.sequencer.stepContentDraft.active.get());

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(!h.state.sequencer.stepContentDraft.modified());
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);
    assert(!h.state.sequencer.stepContentDraft.scratch);
    auto chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 2);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Local);

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(node->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(h.state.sequencerHistory.undoCount() == 1);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepEdit.visible.get());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoProjectHistory());

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(2)) : nullptr;
    assert(node == nullptr || !node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));

    assert(h.state.redoProjectHistory());
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(2)) : nullptr;
    assert(node != nullptr);
    assert(node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));

    std::cout << "[PASS] test_step_edit_chord_row_opens_transactional_editor_and_applies_once\n";
}

void test_step_draft_apply_coexists_with_adjacent_global_history() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[0] = 60;

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, NOTE_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.visible.get());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.pattern.note[0] == 127);
    assert(h.state.sequencerHistory.undoCount() == 1);

    openStepEdit(h, 1);
    h.release(Config::MACRO_BUTTONS[1]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, VELOCITY_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 1));
    assert(h.state.sequencerHistory.undoCount() == 2);

    h.tap(Config::ButtonID::LEFT_TOP);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.undoProjectHistory());
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 1));
    assert(h.state.sequencer.pattern.note[0] == 127);
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 60);

    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 127);
    assert(h.state.redoProjectHistory());
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 1));

    std::cout << "[PASS] test_step_draft_apply_coexists_with_adjacent_global_history\n";
}

void test_step_edit_chord_detail_edits_all_chord_fields() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::CHROMATIC);

    openStepEdit(h, 1);
    h.release(Config::MACRO_BUTTONS[1]);
    focusStepEditRow(h, CHORD_ROW);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
           core::state::sequencer::SequencerChordEditField::SHAPE);

    auto chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 1);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Local);

    h.turn(Config::EncoderID::OPT, input_utils::indexToNormalized(8, 9));

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
           core::state::sequencer::SequencerChordEditField::FORMULA);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
           core::state::sequencer::SequencerChordEditField::INVERSION);
    h.turn(Config::EncoderID::OPT, 1.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
           core::state::sequencer::SequencerChordEditField::VOICING);
    h.turn(Config::EncoderID::OPT, 1.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
           core::state::sequencer::SequencerChordEditField::STRUM);
    h.turn(Config::EncoderID::OPT, 0.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
           core::state::sequencer::SequencerChordEditField::VELOCITY_CONTOUR);
    h.turn(Config::EncoderID::OPT, 1.0f);

    chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 1);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(chord.spec.voices() == 4);
    assert(chord.spec.intervalBasis() ==
           oc::note::sequencer::StepSequencerChordIntervalBasis::ChromaticSemitones);
    assert(chord.spec.harmony() == oc::note::sequencer::StepSequencerChordHarmony::Minor7);
    assert(chord.spec.voicing() == oc::note::sequencer::StepSequencerChordVoicing::Wide);
    assert(chord.spec.inversion() == 3);
    assert(chord.spec.strum == oc::note::sequencer::StepSequencerChordSpec::MIN_STRUM);
    assert(chord.spec.velocityCurve ==
           oc::note::sequencer::StepSequencerChordSpec::MAX_VELOCITY_CURVE);

    // Reset remains a valid draft-local action for the focused Chord field.
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 1);
    assert(chord.spec.velocityCurve == 0);
    assert(chord.spec.strum == oc::note::sequencer::StepSequencerChordSpec::MIN_STRUM);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(!h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.stepEdit.focusedRow.get() == CHORD_ROW);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoProjectHistory());

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    const auto* node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(1)) : nullptr;
    assert(node == nullptr || !node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));

    std::cout << "[PASS] test_step_edit_chord_detail_edits_all_chord_fields\n";
}

void test_scale_context_drives_shape_formula_without_local_basis_toggle() {
    using Basis = oc::note::sequencer::StepSequencerChordIntervalBasis;
    using Field = core::state::sequencer::SequencerChordEditField;
    using Harmony = oc::note::sequencer::StepSequencerChordHarmony;

    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[0] = 65;  // F4
    h.state.sequencer.pattern.pitchEditMode =
        core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE;

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, CHORD_ROW);
    h.tap(Config::ButtonID::NAV);

    auto chord = resolveChordPreview(h, 0);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(chord.spec.intervalBasis() == Basis::ScaleDegrees);
    assert(chord.spec.harmony() == Harmony::DiatonicTriad);
    assert(chord.preview.voiceCount == 3);
    assert(chord.preview.voices[0].note == 65);
    assert(chord.preview.voices[1].note == 68);
    assert(chord.preview.voices[2].note == 72);

    h.turn(Config::EncoderID::OPT, input_utils::indexToNormalized(1, 4));

    chord = resolveChordPreview(h, 0);
    assert(chord.spec.intervalBasis() == Basis::ScaleDegrees);
    assert(chord.spec.harmony() == Harmony::DiatonicSeventh);
    assert(chord.spec.voices() == 4);
    assert(chord.preview.voices[0].note == 65);
    assert(chord.preview.voices[1].note == 68);
    assert(chord.preview.voices[2].note == 72);
    assert(chord.preview.voices[3].note == 76);
    for (uint8_t voice = 0; voice < chord.preview.voiceCount; ++voice) {
        assert(chord.preview.voices[voice].inSelectedScale);
    }
    assert(h.state.sequencer.pattern.pitchEditMode ==
           core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE);

    std::cout << "[PASS] test_scale_context_drives_shape_formula_without_local_basis_toggle\n";
}

void test_chord_context_opens_pattern_pitch_context_directly() {
    using Field = core::state::sequencer::SequencerChordEditField;

    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, CHORD_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());

    for (uint8_t i = 0; i < 6U; ++i) { h.turn(Config::EncoderID::NAV, 1.0f); }
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() == Field::PITCH_CONTEXT);

    h.tap(Config::ButtonID::NAV);

    assert(h.state.patternPitchSettings.flowPhase.get() ==
           core::state::PatternPitchSettingsFlowPhase::OVERLAY);
    assert(h.state.patternPitchSettings.focusedRow.get() == 3U);
    assert(h.state.overlays.current() == core::ui::OverlayType::PATTERN_PITCH_SETTINGS);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());

    h.state.patternPitchSettings.closeOverlay();
    h.handler.update(g_now_ms);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() == Field::PITCH_CONTEXT);

    std::cout << "[PASS] Chord Context opens Pattern Pitch Context directly\n";
}

void test_formula_editor_is_explicit_cancellable_and_edits_zero_three_five() {
    using Basis = oc::note::sequencer::StepSequencerChordIntervalBasis;
    using Field = core::state::sequencer::SequencerChordEditField;
    using Harmony = oc::note::sequencer::StepSequencerChordHarmony;

    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[0] = 65;  // F4
    h.state.sequencer.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::CHROMATIC);

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, CHORD_ROW);
    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() == Field::FORMULA);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().formulaEditorActive);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem == 1);

    // Opening Formula is non-mutating. The first real edit localizes and
    // materializes the effective named formula.
    auto chord = resolveChordPreview(h, 0);
    assert(chord.spec.harmony() == Harmony::Major);
    assert(chord.spec.intervalBasis() == Basis::ChromaticSemitones);
    assert(chord.spec.voices() == 3);
    const auto namedFormula = oc::note::sequencer::resolveChordFormula(chord.spec, false);
    assert(namedFormula.intervals[1] == 4);
    assert(namedFormula.intervals[2] == 7);
    assert(chord.preview.voices[0].note == 65);
    assert(chord.preview.voices[1].note == 69);
    assert(chord.preview.voices[2].note == 72);

    // Voice 2 is always present. Set it to +3 semitones.
    h.turn(Config::EncoderID::OPT, input_utils::indexToNormalized(2, 6));
    chord = resolveChordPreview(h, 0);
    assert(chord.spec.harmony() == Harmony::Minor);

    // LEFT_TOP cancels the Formula sub-session and restores the exact source.
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.chordEditor.subEditor.get().formulaEditorActive);
    chord = resolveChordPreview(h, 0);
    assert(chord.spec.harmony() == Harmony::Major);
    assert(chord.preview.voices[1].note == 69);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().formulaEditorActive);
    h.turn(Config::EncoderID::OPT, input_utils::indexToNormalized(2, 6));
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem == 2);

    // Voice 3 only contains musical intervals. Choice 1 is +5 ST here;
    // add/remove are explicit rail actions rather than OPT sentinel values.
    h.turn(Config::EncoderID::OPT, input_utils::indexToNormalized(1, 28));
    chord = resolveChordPreview(h, 0);
    assert(chord.spec.voices() == 3);
    assert(chord.spec.customInterval(1) == 3);
    assert(chord.spec.customInterval(2) == 5);
    assert(chord.preview.voices[0].note == 65);
    assert(chord.preview.voices[1].note == 68);
    assert(chord.preview.voices[2].note == 70);

    // The trailing item is Add. OPT is inert there; NAV creates the next
    // voice at the smallest valid interval and keeps focus on that voice.
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem == 3);
    h.turn(Config::EncoderID::OPT, 0.5f);
    chord = resolveChordPreview(h, 0);
    assert(chord.spec.voices() == 3);
    h.tap(Config::ButtonID::NAV);
    chord = resolveChordPreview(h, 0);
    assert(chord.spec.voices() == 4);
    assert(chord.spec.customInterval(3) == 6);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem == 3);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    chord = resolveChordPreview(h, 0);
    assert(chord.spec.voices() == 3);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem == 2);

    // Repeated Add reaches all eight voices without exposing a ninth item.
    for (uint8_t expectedCount = 4U; expectedCount <= 8U; ++expectedCount) {
        h.turn(Config::EncoderID::NAV, 1.0f);
        assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem ==
               expectedCount - 1U);
        h.tap(Config::ButtonID::NAV);
        chord = resolveChordPreview(h, 0);
        assert(chord.spec.voices() == expectedCount);
        assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem ==
               expectedCount - 1U);
    }
    assert(chord.spec.customInterval(7) == 10U);

    // With eight voices there is no Add item: focus wraps V8 -> V2.
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem == 1U);

    // Trash removes only the focused middle voice and compacts the suffix.
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem == 3U);
    assert(chord.spec.customInterval(3) == 6U);
    assert(chord.spec.customInterval(4) == 7U);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    chord = resolveChordPreview(h, 0);
    assert(chord.spec.voices() == 7U);
    assert(chord.spec.customInterval(3) == 7U);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem == 3U);

    // A formula ending at the storage bound keeps Add visible but disabled.
    for (uint8_t turn = 0; turn < 3U; ++turn) { h.turn(Config::EncoderID::NAV, 1.0f); }
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem == 6U);
    h.turn(Config::EncoderID::OPT, 1.0f);
    chord = resolveChordPreview(h, 0);
    assert(chord.spec.customInterval(6) == 31U);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedFormulaItem == 7U);
    h.tap(Config::ButtonID::NAV);
    chord = resolveChordPreview(h, 0);
    assert(chord.spec.voices() == 7U);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().formulaEditorActive);
    h.turn(Config::EncoderID::NAV, -1.0f);

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepEdit.chordEditor.subEditor.get().formulaEditorActive);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() == Field::FORMULA);

    std::cout << "[PASS] test_formula_editor_is_explicit_cancellable_and_edits_zero_three_five\n";
}

void test_child_chord_preview_resolves_inherited_parent_chord() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    oc::note::sequencer::StepSequencerChordSpec parentChord{};
    parentChord.voiceCount = 4;
    assert(
        core::state::sequencer::setNodeChordSpec(h.state.sequencer.pattern, rootNode, parentChord));

    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));

    const oc::note::sequencer::StepSequencerScaleSettings scaleSettings{};
    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        h.state.sequencer, 0, scaleSettings);
    assert(projection.valid);
    assert(projection.inheritedChord.valid);
    assert(projection.inheritedChord.spec.voiceCount == 4);

    auto chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0);
    assert(chord.valid);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Inherit);

    core::state::sequencer::resolveStepChordPreview(chord, projection, scaleSettings);
    assert(chord.preview.valid);
    assert(chord.preview.source == oc::note::sequencer::StepSequencerChordSource::Inherited);
    assert(chord.preview.voiceCount == 4);
    assert(chord.effectiveVoiceCount == 4);
    assert(chord.spec.voiceCount == 4);

    std::cout << "[PASS] test_child_chord_preview_resolves_inherited_parent_chord\n";
}

void test_step_edit_child_chord_detail_localizes_from_inherited_spec() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::CHROMATIC);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    auto parentChord = oc::note::sequencer::StepSequencerChordSpec::semantic(
        oc::note::sequencer::StepSequencerChordHarmony::Suspended, 4,
        oc::note::sequencer::StepSequencerChordVoicing::Wide, 3);
    parentChord.strum = 25;
    parentChord.velocityCurve = -12;
    assert(
        core::state::sequencer::setNodeChordSpec(h.state.sequencer.pattern, rootNode, parentChord));

    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, CHORD_ROW);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
           core::state::sequencer::SequencerChordEditField::SHAPE);
    h.turn(Config::EncoderID::OPT, input_utils::indexToNormalized(8, 9));

    const auto authoredChord =
        core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0);
    assert(authoredChord.mode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(authoredChord.spec.voiceCount == parentChord.voiceCount);
    assert(authoredChord.spec.harmony() == oc::note::sequencer::StepSequencerChordHarmony::Minor7);
    assert(authoredChord.spec.voicing() == oc::note::sequencer::StepSequencerChordVoicing::Wide);
    assert(authoredChord.spec.inversion() == 3);
    assert(authoredChord.spec.strum == parentChord.strum);
    assert(authoredChord.spec.velocityCurve == parentChord.velocityCurve);
    assert(!h.state.sequencer.stepContentDraft.scratch);

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencerHistory.undoCount() == 1);
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto* child = graph->stepNode(sequence->firstStepNode);
    assert(child != nullptr);
    assert(child->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);

    std::cout << "[PASS] test_step_edit_child_chord_detail_localizes_from_inherited_spec\n";
}

void test_step_edit_child_chord_row_resets_to_inherit_default() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, CHORD_ROW);

    auto chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0);
    assert(chord.valid);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Inherit);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepContentDraft.active.get());
    chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Inherit);

    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().sourceSelectorActive);
    assert(h.state.sequencer.stepEdit.chordEditor.subEditor.get().focusedSourceChoice ==
           core::state::sequencer::SequencerChordSourceChoice::PARENT_CHORD);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Single);

    h.tap(Config::ButtonID::LEFT_CENTER);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0);
    assert(chord.valid);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Inherit);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.chordEditor.subEditor.get().sourceSelectorActive);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(!h.state.sequencer.stepEdit.chordEditor.active.get());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_step_edit_child_chord_row_resets_to_inherit_default\n";
}

void test_step_edit_context_clipboard_requires_matching_child_kind() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto microSource = core::state::sequencer::rootStepNodeId(0);
    assert(
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, microSource, 2).ok);
    const auto cycleSource = core::state::sequencer::rootStepNodeId(2);
    assert(
        core::state::sequencer::createCycleStateSet(h.state.sequencer.pattern, cycleSource, 4).ok);

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerStepContent(
        core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE));
    h.tap(Config::ButtonID::LEFT_TOP);

    openStepEdit(h, 1);
    h.release(Config::MACRO_BUTTONS[1]);
    focusStepEditRow(h, CYCLE_STATES_ROW);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(!stepHasCycleStates(h.state.sequencer.pattern, 1));

    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 1));
    h.tap(Config::ButtonID::LEFT_TOP);

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);
    focusStepEditRow(h, CYCLE_STATES_ROW);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerStepContent(
        core::state::SequencerStepContentClipboardKind::CYCLE_STATES));
    h.tap(Config::ButtonID::LEFT_TOP);

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 3));

    focusStepEditRow(h, CYCLE_STATES_ROW);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(stepHasCycleStates(h.state.sequencer.pattern, 3));

    std::cout << "[PASS] test_step_edit_context_clipboard_requires_matching_child_kind\n";
}

void test_step_edit_session_undo_redo_workflow() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(6);
    h.state.sequencer.pattern.note[2] = 61;
    h.state.sequencer.pattern.gate[2] = 55;

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);

    focusStepEditRow(h, NOTE_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.note[2] == 127);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == GATE_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.gate[2] ==
           core::state::sequencer::SequencerState::MAX_GATE_PERCENT);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.visible.get());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencer.pattern.note[2] == 127);
    assert(h.state.sequencer.pattern.gate[2] ==
           core::state::sequencer::SequencerState::MAX_GATE_PERCENT);

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[2] == 61);
    assert(h.state.sequencer.pattern.gate[2] == 55);
    // The retained transaction prepares lazily at the first musical write,
    // after Step Edit has focused its owner step.
    assert(h.state.sequencer.focusedStep.get() == 2);

    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.note[2] == 127);
    assert(h.state.sequencer.pattern.gate[2] ==
           core::state::sequencer::SequencerState::MAX_GATE_PERCENT);
    assert(h.state.sequencer.focusedStep.get() == 2);

    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.redoCount() == 0);

    std::cout << "[PASS] test_step_edit_session_undo_redo_workflow\n";
}

void test_left_top_close_keeps_live_edit_and_records_history() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[4] = 62;
    h.state.sequencer.pattern.velocity[4] = 80;
    h.state.sequencer.pattern.gate[4] = 70;
    h.state.sequencer.pattern.nudge[4] = -5;
    h.state.sequencer.pattern.probability[4] = 90;

    openStepEdit(h, 4);
    h.release(Config::MACRO_BUTTONS[4]);

    focusStepEditRow(h, NOTE_ROW);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.note[4] == 127);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.pattern.note[4] == 127);
    assert(h.state.sequencer.pattern.velocity[4] == 80);
    assert(h.state.sequencer.pattern.gate[4] == 70);
    assert(h.state.sequencer.pattern.nudge[4] == -5);
    assert(h.state.sequencer.pattern.probability[4] == 90);
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[4] == 62);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    std::cout << "[PASS] test_left_top_close_keeps_live_edit_and_records_history\n";
}

void test_step_edit_does_not_open_when_blocked() {
    {
        SequencerStepEditHarness h;
        h.state.overlays.show(core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    {
        SequencerStepEditHarness h;
        h.state.sequencer.structureUi.stepSelection.active.set(true);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    {
        SequencerStepEditHarness h;
        h.state.sequencer.patternQuickControls.selecting.set(true);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    {
        SequencerStepEditHarness h;
        h.state.sequencer.stepPropertyInlineSelector.selecting.set(true);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    std::cout << "[PASS] test_step_edit_does_not_open_when_blocked\n";
}

void test_step_preset_library_saves_and_loads_focused_step() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.setEnabled(2, true);
    assert(h.state.sequencer.setStepDataAt(2, 67, 96, 155, -3, 84));

    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern, core::state::sequencer::rootStepNodeId(2), 2);
    assert(micro.ok);
    const auto* sourceGraph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(sourceGraph != nullptr);
    const auto* sourceSequence = sourceGraph->sequence(micro.id);
    assert(sourceSequence != nullptr);
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern, static_cast<uint16_t>(sourceSequence->firstStepNode + 1U), 6));
    assert(core::state::sequencer::storeActiveTrack(h.state.sequencerTracks, h.state.sequencer));

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);

    openStepPresetLibrary(h);
    assert(h.overlays.current() == core::ui::OverlayType::PRESET_LIBRARY);
    assert(h.state.sequencer.presetLibrary.visible.get());
    assert(h.state.sequencer.presetLibrary.mode.get() ==
           core::state::sequencer::SequencerPresetLibraryMode::LOAD);
    assert(h.state.sequencer.presetLibrary.entryCount.get() == 0);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencer.presetLibrary.mode.get() ==
           core::state::sequencer::SequencerPresetLibraryMode::SAVE);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.presetLibrary.feedback.get() ==
           core::state::sequencer::SequencerPresetLibraryFeedback::SAVED);
    assert(h.state.sequencer.presetLibrary.operationFeedback.get().action ==
           core::state::contextual::ContextActionId::SAVE);
    assert(h.state.sequencer.presetLibrary.entryCount.get() == 1);
    assert(std::strcmp(h.state.sequencer.presetLibrary.entryId(0), "step-preset-001") == 0);
    assert(std::filesystem::exists(testRoot() / "midi-studio" / "library" / "step-presets" /
                                   "step-preset-001.mssp"));

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);
    h.tap(Config::MACRO_BUTTONS[2]);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    h.state.sequencer.pattern.setEnabled(5, false);
    assert(h.state.sequencer.setStepDataAt(5, 41, 12, 40, 4, 100));
    openStepEdit(h, 5);
    h.release(Config::MACRO_BUTTONS[5]);
    focusStepEditRow(h, NOTE_ROW);
    h.turn(Config::EncoderID::OPT, 72.0f / 127.0f);
    openStepPresetLibrary(h);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);

    assert(h.overlays.current() == core::ui::OverlayType::PRESET_LIBRARY);
    assert(h.state.sequencer.presetLibrary.visible.get());
    assert(h.state.sequencer.presetLibrary.feedback.get() ==
           core::state::sequencer::SequencerPresetLibraryFeedback::LOADED);
    assert(h.state.sequencer.presetLibrary.operationFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::APPLIED);
    assert(h.state.sequencer.pattern.isEnabled(5));
    assert(h.state.sequencer.pattern.note[5] == 67);
    assert(h.state.sequencer.pattern.velocity[5] == 96);
    assert(h.state.sequencer.pattern.gate[5] == 155);
    assert(h.state.sequencer.pattern.nudge[5] == -3);
    assert(h.state.sequencer.pattern.probability[5] == 84);

    const auto* targetGraph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(targetGraph != nullptr);
    const auto* targetRoot = targetGraph->stepNode(core::state::sequencer::rootStepNodeId(5));
    assert(targetRoot != nullptr);
    assert(targetRoot->has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE));
    const auto* targetSequence = targetGraph->sequence(targetRoot->childSequenceId);
    assert(targetSequence != nullptr);
    const auto* targetChild =
        targetGraph->stepNode(static_cast<uint16_t>(targetSequence->firstStepNode + 1U));
    assert(targetChild != nullptr);
    assert(targetChild->noteOffset == 6);
    assert(h.state.sequencerHistory.undoCount() == 2);

    // A completed hold may outlive the temporary feedback deadline. Keep the
    // picker authoritative until the physical release is consumed; otherwise
    // the release can fall through to the parent Step Editor and overwrite its
    // typed clipboard action.
    h.advance(Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS);
    h.handler.update(g_now_ms);
    assert(h.state.sequencer.presetLibrary.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::PRESET_LIBRARY);

    h.release(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(g_now_ms);
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);
    assert(!h.state.sequencer.presetLibrary.visible.get());

    resetTestRoot();
    std::cout << "[PASS] test_step_preset_library_saves_and_loads_focused_step\n";
}

void test_step_preset_overwrite_accepts_authoritative_long_press_with_clock_lag() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setEnabled(0, true);
    assert(h.state.sequencer.setStepDataAt(0, 60, 80, 100, 0, 100));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    auto presets =
        core::handler::SequencerStepPresetDomainServices::fromCoreState(h.state, h.productFiles);
    const auto initialTarget = presets.captureTarget();
    assert(initialTarget.valid);
    assert(presets.savePreset("clock-lag-overwrite", initialTarget, false).ok());

    // The overwrite stores the current step, which is deliberately different
    // from the initial asset content.
    assert(h.state.sequencer.setStepDataAt(0, 67, 96, 140, -2, 84));
    openStepPresetLibrary(h);
    auto& picker = h.state.sequencer.presetLibrary;
    assert(picker.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::LOAD);
    assert(picker.entryCount.get() == 1);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(picker.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::SAVE);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(!picker.selectedItemIsNewAsset());
    assert(std::strcmp(picker.entryId(picker.existingEntryIndexForSelectedItem()),
                       "clock-lag-overwrite") == 0);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    const uint32_t pressedAt = g_now_ms;

    // InputBinding owns the physical LONG_PRESS threshold. Reproduce the SDL
    // trace in which its callback fires at 1000 ms while the workflow's clock
    // sample is still 12 ms behind. A completed physical hold must remain
    // authoritative and must not be converted to a cancellation on release.
    g_step_preset_time_lag_ms = 12;
    h.tick(pressedAt + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    g_step_preset_time_lag_ms = 0;

    assert(picker.feedback.get() == core::state::sequencer::SequencerPresetLibraryFeedback::SAVED);
    const auto feedback = picker.operationFeedback.get();
    assert(feedback.active);
    assert(feedback.action == core::state::contextual::ContextActionId::SAVE);
    assert(feedback.status == core::state::contextual::OperationFeedbackStatus::APPLIED);
    assert(picker.actionGuard.get().phase == core::state::contextual::GuardedActionPhase::IDLE);

    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(picker.operationFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::APPLIED);
    assert(picker.feedback.get() == core::state::sequencer::SequencerPresetLibraryFeedback::SAVED);

    resetTestRoot();
    std::cout
        << "[PASS] test_step_preset_overwrite_accepts_authoritative_long_press_with_clock_lag\n";
}

void test_step_preset_library_pages_without_mutating_and_restores_load_focus() {
    SequencerStepEditHarness h;
    auto presets =
        core::handler::SequencerStepPresetDomainServices::fromCoreState(h.state, h.productFiles);
    const auto target = presets.captureTarget();
    constexpr uint8_t pageCapacity =
        core::state::sequencer::SequencerPresetLibrarySessionState::ENTRY_CAPACITY;
    constexpr uint8_t presetCount = static_cast<uint8_t>(pageCapacity + 2U);

    core::sequencer::RealtimeMidiQueue midiQueue;
    core::sequencer::SequencerRuntimeGraphBank runtimeGraphBank;
    core::sequencer::SequencerRuntimeSnapshotBank runtimeSnapshotBank{
        h.state.sequencer,
        h.state.sequencerTracks,
        h.state.projectNavigation,
    };
    core::sequencer::SequencerPlaybackService playback{
        h.state.sequencer,
        h.state.statusBar,
        midiQueue,
        runtimeGraphBank,
    };
    uint32_t runtimeTick = 0;
    const auto assertSilentRuntime = [&]() {
        assert(runtimeGraphBank.prepare(h.state.sequencer, h.state.sequencerTracks));
        const uint8_t snapshotIndex = runtimeSnapshotBank.refresh();
        assert(runtimeSnapshotBank.lastRefreshSucceeded());
        runtimeGraphBank.publishPrepared([&]() { runtimeSnapshotBank.commit(snapshotIndex); });
        const auto& runtimeSnapshot = runtimeSnapshotBank.activeSnapshot();
        core::sequencer::ProjectTrackRuntimeSnapshot projectTracks{};
        core::sequencer::captureProjectTrackRuntimeSnapshot(
            h.state.projectTracks, runtimeSnapshot.enabledMask, projectTracks);
        playback.update(runtimeSnapshot, runtimeTick, true, runtimeTick * 1000U, 1000U,
                        projectTracks, false,
                        runtimeSnapshotBank.laneSnapshot(runtimeSnapshotBank.activeIndex()));
        assert(midiQueue.size() == 0);
        ++runtimeTick;
    };

    for (uint8_t i = 0; i < presetCount; ++i) {
        char id[24]{};
        std::snprintf(id, sizeof(id), "page-preset-%02u", static_cast<unsigned>(i));
        const auto saved = presets.savePreset(id, target, false);
        assert(saved.ok());
    }

    const uint8_t noteBefore = h.state.sequencer.pattern.note[0];
    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    openStepPresetLibrary(h);
    assertSilentRuntime();

    auto& picker = h.state.sequencer.presetLibrary;
    assert(picker.entryCount.get() == pageCapacity);
    assert(!picker.hasPreviousPage.get());
    assert(picker.hasNextPage.get());
    assert(picker.totalEntryCount.get() == presetCount);
    assert(std::strcmp(picker.entryId(0), "page-preset-00") == 0);

    // Detail and example browsing are visual inspection only. Exercise the
    // real playback boundary after each gesture so a future audition path
    // cannot silently enqueue MIDI.
    h.tap(Config::ButtonID::NAV);
    assert(picker.detailVisible.get());
    assertSilentRuntime();
    h.turn(Config::EncoderID::OPT, 1.0f);
    assertSilentRuntime();
    // Preset libraries follow the same hierarchy as every editor: NAV enters
    // the focused asset and LEFT_TOP backs out one level.
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!picker.detailVisible.get());
    assert(picker.visible.get());
    assertSilentRuntime();

    // Walking past the last visible row loads the next alphabetical page; no
    // asset becomes unreachable because of the bounded embedded page size.
    for (uint8_t i = 0; i < pageCapacity; ++i) { h.turn(Config::EncoderID::NAV, 1.0f); }
    assertSilentRuntime();
    assert(picker.entryCount.get() == 2);
    assert(picker.hasPreviousPage.get());
    assert(!picker.hasNextPage.get());
    assert(picker.selectedIndex.get() == 0);
    char secondPageFirst[24]{};
    std::snprintf(secondPageFirst, sizeof(secondPageFirst), "page-preset-%02u",
                  static_cast<unsigned>(pageCapacity));
    assert(std::strcmp(picker.entryId(0), secondPageFirst) == 0);

    // Load/Save is a mode change, not a browse reset. Returning to Load keeps
    // the page, focus, and frozen target selected by the user.
    const auto frozenTarget = picker.step().target;
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(picker.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::SAVE);
    assert(picker.selectedIndex.get() == 1U);
    assert(!picker.selectedItemIsNewAsset());
    assert(std::strcmp(picker.entryId(picker.existingEntryIndexForSelectedItem()),
                       secondPageFirst) == 0);
    assertSilentRuntime();
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(picker.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::LOAD);
    assertSilentRuntime();
    assert(std::strcmp(picker.entryId(0), secondPageFirst) == 0);
    assert(picker.selectedIndex.get() == 0);
    assert(core::state::sequencer::sequencerStepPresetTargetHash(picker.step().target) ==
           core::state::sequencer::sequencerStepPresetTargetHash(frozenTarget));

    // Browsing and changing modes are strictly non-mutating.
    assert(h.state.sequencer.pattern.note[0] == noteBefore);
    assert(h.state.sequencerHistory.undoCount() == undoBefore);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assertSilentRuntime();
    assert(picker.entryCount.get() == pageCapacity);
    assert(picker.selectedIndex.get() == static_cast<uint8_t>(pageCapacity - 1U));
    char firstPageLast[24]{};
    std::snprintf(firstPageLast, sizeof(firstPageLast), "page-preset-%02u",
                  static_cast<unsigned>(pageCapacity - 1U));
    assert(std::strcmp(picker.entryId(static_cast<uint8_t>(pageCapacity - 1U)), firstPageLast) ==
           0);

    resetTestRoot();
    std::cout << "[PASS] test_step_preset_library_pages_without_mutating_and_restores_load_focus\n";
}

void test_preset_nav_defers_inspection_until_focus_settles() {
    SequencerStepEditHarness h;
    auto presets =
        core::handler::SequencerStepPresetDomainServices::fromCoreState(h.state, h.productFiles);
    const auto target = presets.captureTarget();
    assert(target.valid);
    assert(presets.savePreset("browse-a", target, false).ok());
    assert(presets.savePreset("browse-b", target, false).ok());
    assert(presets.savePreset("browse-c", target, false).ok());

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    openStepPresetLibrary(h);
    auto& picker = h.state.sequencer.presetLibrary;
    assert(picker.step().descriptor.valid);
    assert(std::strcmp(picker.step().descriptor.technicalId, "browse-a") == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(picker.selectedIndex.get() == 2U);
    assert(picker.inspecting.get());
    assert(!picker.step().descriptor.valid);

    h.handler.update(g_now_ms + Config::Timing::PRESET_LIBRARY_INSPECTION_SETTLE_MS - 1U);
    assert(picker.inspecting.get());
    assert(!picker.step().descriptor.valid);

    h.handler.update(g_now_ms + Config::Timing::PRESET_LIBRARY_INSPECTION_SETTLE_MS);
    assert(!picker.inspecting.get());
    assert(picker.step().descriptor.valid);
    assert(std::strcmp(picker.step().descriptor.technicalId, "browse-c") == 0);

    // An explicit command never waits for the trailing-edge deadline.
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(picker.inspecting.get());
    h.tap(Config::ButtonID::NAV);
    assert(!picker.inspecting.get());
    assert(picker.detailVisible.get());
    assert(std::strcmp(picker.step().descriptor.technicalId, "browse-b") == 0);

    resetTestRoot();
    std::cout << "[PASS] test_preset_nav_defers_inspection_until_focus_settles\n";
}

void test_blocked_preset_load_keeps_pending_history_coalescing() {
    SequencerStepEditHarness h;
    auto presets =
        core::handler::SequencerStepPresetDomainServices::fromCoreState(h.state, h.productFiles);
    const auto target = presets.captureTarget();
    assert(target.valid);
    assert(presets.savePreset("corrupt-load", target, false).ok());

    constexpr uint8_t corruptPayload[] = {0x42U, 0x41U, 0x44U};
    assert(h.productFiles.beginWrite("library/step-presets/corrupt-load.mssp",
                                     sizeof(corruptPayload)));
    assert(h.productFiles.appendWrite(corruptPayload, sizeof(corruptPayload)));
    assert(h.productFiles.finishWrite());

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    openStepPresetLibrary(h);
    auto& picker = h.state.sequencer.presetLibrary;
    assert(picker.selectedItemIsExistingAsset());
    assert(picker.step().descriptor.compatibility ==
           core::state::sequencer::SequencerStepPresetCompatibility::CORRUPT);

    assert(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        0U, core::state::sequencer::StepProperty::NOTE, g_now_ms,
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly));
    assert(h.state.sequencer.setStepNoteAt(0U, 72U));
    assert(h.state.sealSequencerPatternHistoryCoalescing(true));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(picker.operationFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::BLOCKED);

    resetTestRoot();
    std::cout << "[PASS] blocked preset load keeps pending history coalescing\n";
}

void test_step_preset_save_selects_new_asset_beyond_first_page() {
    SequencerStepEditHarness h;
    auto presets =
        core::handler::SequencerStepPresetDomainServices::fromCoreState(h.state, h.productFiles);
    const auto target = presets.captureTarget();
    constexpr uint8_t pageCapacity =
        core::state::sequencer::SequencerPresetLibrarySessionState::ENTRY_CAPACITY;
    constexpr uint8_t existingPresetCount = static_cast<uint8_t>(pageCapacity + 2U);

    // These names sort before the auto-generated "Step Preset 001", forcing
    // the new asset beyond the first bounded picker page.
    for (uint8_t i = 0; i < existingPresetCount; ++i) {
        char id[24]{};
        std::snprintf(id, sizeof(id), "page-preset-%02u", static_cast<unsigned>(i));
        assert(presets.savePreset(id, target, false).ok());
    }

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    openStepPresetLibrary(h);

    auto& picker = h.state.sequencer.presetLibrary;
    assert(picker.entryCount.get() == pageCapacity);
    assert(picker.hasNextPage.get());
    for (uint8_t i = 0; i < picker.entryCount.get(); ++i) {
        assert(std::strcmp(picker.entryId(i), "step-preset-001") != 0);
    }

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(picker.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::SAVE);
    assert(!picker.selectedItemIsNewAsset());
    assert(picker.selectedIndex.get() == 1U);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(picker.selectedItemIsNewAsset());
    h.tap(Config::ButtonID::BOTTOM_RIGHT);

    assert(picker.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::LOAD);
    assert(picker.feedback.get() == core::state::sequencer::SequencerPresetLibraryFeedback::SAVED);
    assert(picker.totalEntryCount.get() == static_cast<uint16_t>(existingPresetCount + 1U));
    assert(picker.hasPreviousPage.get());
    assert(picker.selectedIndex.get() < picker.entryCount.get());
    assert(std::strcmp(picker.entryId(picker.selectedIndex.get()), "step-preset-001") == 0);
    assert(picker.step().descriptor.valid);
    assert(std::strcmp(picker.step().descriptor.technicalId, "step-preset-001") == 0);
    assert(std::filesystem::exists(testRoot() / "midi-studio" / "library" / "step-presets" /
                                   "step-preset-001.mssp"));

    resetTestRoot();
    std::cout << "[PASS] test_step_preset_save_selects_new_asset_beyond_first_page\n";
}

void test_step_preset_queued_feedback_resolves_to_applied_and_auto_closes() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.setEnabled(0, true);
    assert(h.state.sequencer.setStepDataAt(0, 70, 96, 120, 0, 100));

    auto presets =
        core::handler::SequencerStepPresetDomainServices::fromCoreState(h.state, h.productFiles);
    const auto target = presets.captureTarget();
    assert(target.valid && target.trackIndex == 0 && target.stepIndex == 0);
    assert(presets.savePreset("queued-ui", target, false).ok());
    assert(presets.savePreset("queued-ui-b", target, false).ok());

    assert(h.state.sequencer.setStepDataAt(0, 41, 12, 40, 4, 84));
    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    openStepPresetLibrary(h);
    assert(h.state.sequencer.presetLibrary.visible.get());
    assert(std::strcmp(h.state.sequencer.presetLibrary.entryId(0), "queued-ui") == 0);

    h.state.statusBar.playing.set(true);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);

    auto& picker = h.state.sequencer.presetLibrary;
    assert(picker.feedback.get() == core::state::sequencer::SequencerPresetLibraryFeedback::QUEUED);
    assert(picker.operationFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::QUEUED);
    assert(picker.step().activationGeneration != 0);
    const uint32_t activationGeneration = picker.step().activationGeneration;
    const auto queuedTelemetry = h.state.sequencerTrackActivations.telemetry(0);
    assert(queuedTelemetry.generation == activationGeneration);
    assert(queuedTelemetry.origin ==
           core::state::sequencer::SequencerTrackActivationOrigin::STEP_PRESET);
    assert(h.state.sequencer.pattern.note[0] == 70);
    assert(h.state.sequencerTrackActivations.pendingTrackMask() == 0x0001);

    // Consume the physical release before the queued runtime generation
    // resolves, so it cannot fall through to the parent Step Editor.
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    const uint8_t selectedWhileQueued = picker.selectedIndex.get();
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(picker.visible.get());
    assert(picker.selectedIndex.get() == selectedWhileQueued);
    assert(picker.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::LOAD);
    assert(picker.feedback.get() == core::state::sequencer::SequencerPresetLibraryFeedback::QUEUED);

    const auto publication = h.state.sequencerTrackActivations.captureRuntimePublication();
    h.state.sequencerTrackActivations.applyRuntimePublication(publication);
    const auto realtime = h.state.sequencerTrackActivations.realtimeView(0);
    assert(realtime.disposition ==
           core::state::sequencer::SequencerTrackActivationRealtimeView::Disposition::STAGED);
    assert(h.state.sequencerTrackActivations.markAppliedFromRealtime(0, realtime.generation));
    assert(h.state.sequencerTrackActivations.publishRealtimeTelemetry());

    ++g_now_ms;
    h.handler.update(g_now_ms);
    assert(picker.feedback.get() == core::state::sequencer::SequencerPresetLibraryFeedback::LOADED);
    assert(picker.operationFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::APPLIED);
    assert(picker.step().activationGeneration == activationGeneration);
    assert(picker.visible.get());

    // Terminal feedback owns the library until its deterministic close.
    // Incidental navigation or a mode-button tap cannot cancel that close or
    // start another operation against the already-mutated target.
    const uint8_t selectedBeforeClose = picker.selectedIndex.get();
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(picker.selectedIndex.get() == selectedBeforeClose);
    assert(picker.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::LOAD);

    g_now_ms += Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS;
    h.handler.update(g_now_ms);
    assert(!picker.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);

    resetTestRoot();
    std::cout << "[PASS] test_step_preset_queued_feedback_resolves_to_applied_and_auto_closes\n";
}

void test_step_preset_queued_feedback_resolves_to_cancelled_on_undo() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.setEnabled(0, true);
    assert(h.state.sequencer.setStepDataAt(0, 72, 96, 120, 0, 100));

    auto presets =
        core::handler::SequencerStepPresetDomainServices::fromCoreState(h.state, h.productFiles);
    const auto target = presets.captureTarget();
    assert(presets.savePreset("queued-cancel", target, false).ok());
    assert(h.state.sequencer.setStepDataAt(0, 43, 12, 40, 4, 84));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    openStepPresetLibrary(h);
    h.state.statusBar.playing.set(true);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    auto& picker = h.state.sequencer.presetLibrary;
    assert(picker.feedback.get() == core::state::sequencer::SequencerPresetLibraryFeedback::QUEUED);
    const uint32_t activationGeneration = picker.step().activationGeneration;
    assert(activationGeneration != 0);
    assert(h.state.sequencer.pattern.note[0] == 72);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[0] == 43);

    ++g_now_ms;
    h.handler.update(g_now_ms);
    assert(picker.feedback.get() ==
           core::state::sequencer::SequencerPresetLibraryFeedback::CANCELLED);
    assert(picker.operationFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::CANCELLED);
    assert(picker.step().activationGeneration == activationGeneration);
    const auto cancelledTelemetry = h.state.sequencerTrackActivations.telemetry(0);
    assert(cancelledTelemetry.status ==
           core::state::sequencer::SequencerTrackActivationStatus::CANCELLED);
    assert(cancelledTelemetry.generation == activationGeneration);
    assert(cancelledTelemetry.origin ==
           core::state::sequencer::SequencerTrackActivationOrigin::STEP_PRESET);
    assert(picker.visible.get());

    g_now_ms += Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS;
    h.handler.update(g_now_ms);
    assert(!picker.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);

    resetTestRoot();
    std::cout << "[PASS] test_step_preset_queued_feedback_resolves_to_cancelled_on_undo\n";
}

void test_chord_preset_library_saves_and_loads_only_the_active_draft() {
    using Spec = oc::note::sequencer::StepSequencerChordSpec;

    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[0] = 60;

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, CHORD_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepContentDraft.active.get());

    const auto savedSpec =
        core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0).spec;
    openChordPresetLibrary(h);
    auto& picker = h.state.sequencer.presetLibrary;
    assert(picker.entryCount.get() == 0U);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(picker.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::SAVE);
    assert(picker.selectedItemIsNewAsset());
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(picker.feedback.get() == core::state::sequencer::SequencerPresetLibraryFeedback::SAVED);
    assert(picker.entryCount.get() == 1U);
    assert(picker.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::LOAD);
    assert(h.state.sequencerHistory.undoCount() == 0U);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!picker.visible.get());
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());

    Spec changed = savedSpec;
    changed.strum = 73;
    const uint16_t nodeId = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
    assert(core::state::sequencer::setAuthoringNodeChordSpec(h.state.sequencer, nodeId, changed));
    core::state::sequencer::notifyStepContentDraftMutation(h.state.sequencer);
    assert(core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0).spec.strum == 73);

    openChordPresetLibrary(h);
    assert(picker.entryCount.get() == 1U);
    assert(picker.chord().descriptor.valid);
    assert(picker.chord().descriptor.compatibility ==
           core::state::sequencer::SequencerChordPresetCompatibility::READY);
    const Spec expectedLoaded = picker.chord().descriptor.projectedFormula;

    // Chord-library OPT and LEFT_CENTER are explicit no-ops.
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0).spec.strum == 73);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(picker.visible.get());

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    const auto loadedSpec =
        core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0).spec;
    assert(oc::note::sequencer::chordSpecsEqualCanonical(loadedSpec, expectedLoaded));
    assert(h.state.sequencer.stepContentDraft.modified());
    assert(h.state.sequencerHistory.undoCount() == 0U);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!picker.visible.get());
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencerHistory.undoCount() == 1U);

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    const auto* node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(0)) : nullptr;
    assert(node != nullptr);
    assert(oc::note::sequencer::chordSpecsEqualCanonical(node->chordSpec, expectedLoaded));

    resetTestRoot();
    std::cout << "[PASS] test_chord_preset_library_saves_and_loads_only_the_active_draft\n";
}

void test_step_and_chord_preset_libraries_share_the_navigation_contract() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.setEnabled(0, true);

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);

    auto stepPresets =
        core::handler::SequencerStepPresetDomainServices::fromCoreState(h.state, h.productFiles);
    const auto stepTarget = stepPresets.captureTarget();
    assert(stepTarget.valid);
    assert(stepPresets.savePreset("shared-step-contract", stepTarget, false).ok());

    openStepPresetLibrary(h);
    auto& library = h.state.sequencer.presetLibrary;
    assert(library.libraryKind.get() == core::state::sequencer::SequencerPresetLibraryKind::STEP);
    assert(library.entryCount.get() == 1U);
    const auto frozenStepTarget = library.step().target;
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(library.selectedIndex.get() == 1U);
    assert(!library.selectedItemIsNewAsset());
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(library.selectedIndex.get() == 0U);
    assert(core::state::sequencer::sequencerStepPresetTargetHash(library.step().target) ==
           core::state::sequencer::sequencerStepPresetTargetHash(frozenStepTarget));

    h.tap(Config::ButtonID::NAV);
    assert(library.detailVisible.get());
    assert(library.detailFocus.get() == 0U);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(library.detailFocus.get() == 1U);
    h.tap(Config::ButtonID::NAV);
    assert(library.detailVisible.get());
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(library.visible.get());
    assert(library.detailVisible.get());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(library.visible.get());
    assert(!library.detailVisible.get());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!library.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);

    focusStepEditRow(h, CHORD_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepContentDraft.active.get());

    openChordPresetLibrary(h);
    assert(library.libraryKind.get() == core::state::sequencer::SequencerPresetLibraryKind::CHORD);
    const auto frozenChordTargetHash =
        core::state::sequencer::sequencerChordPresetTargetHash(library.chord().target);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(library.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::SAVE);
    assert(core::state::sequencer::sequencerChordPresetTargetHash(library.chord().target) ==
           frozenChordTargetHash);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(library.entryCount.get() == 1U);
    assert(library.mode.get() == core::state::sequencer::SequencerPresetLibraryMode::LOAD);

    h.tap(Config::ButtonID::NAV);
    assert(library.detailVisible.get());
    assert(library.detailFocus.get() == 0U);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(library.detailFocus.get() == 1U);
    h.tap(Config::ButtonID::NAV);
    assert(library.detailVisible.get());
    const auto chordBeforeOpt =
        core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0).spec;
    h.turn(Config::EncoderID::OPT, 1.0f);
    const auto chordAfterOpt =
        core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0).spec;
    assert(oc::note::sequencer::chordSpecsEqualCanonical(chordBeforeOpt, chordAfterOpt));
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(library.visible.get());
    assert(library.detailVisible.get());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(library.visible.get());
    assert(!library.detailVisible.get());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!library.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);

    resetTestRoot();
    std::cout << "[PASS] Step and Chord preset libraries share one navigation contract\n";
}

}  // namespace

int main() {
    test_focused_step_entry_keeps_next_nav_tap_available();
    test_direct_step_content_entry_opens_detail_or_child_without_intermediate_editor();
    test_left_center_nav_retargets_root_steps_across_pages();
    test_long_press_opens_step_edit_and_ignores_open_release();
    test_nav_and_opt_edit_then_nav_confirms_without_closing();
    test_left_bottom_hold_edits_local_variation_for_focused_property();
    test_chance_row_does_not_enter_local_variation_mode();
    test_local_variation_edit_targets_active_child_step_node();
    test_context_rows_are_focusable_and_root_action_rows_do_not_edit_properties();
    test_activated_row_edits_root_step_enabled_state();
    test_create_edit_and_commit_micro_sequence_context();
    test_step_edit_opens_nested_content_from_child_contexts();
    test_cycle_state_context_length_is_editable_to_sixteen();
    test_child_context_offset_wraps_steps_from_quick_controls();
    test_micro_sequence_note_offsets_follow_parent_scale_degrees();
    test_back_from_child_step_edit_returns_to_parent_and_records_history();
    test_pristine_child_draft_back_abandons_before_returning_to_parent();
    test_modified_child_draft_back_prompts_and_default_save_returns_to_parent();
    test_step_draft_exit_prompt_is_modal_for_short_and_long_actions();
    test_child_draft_step_editor_exposes_reset_but_blocks_hidden_preset();
    test_child_step_editor_bottom_right_applies_draft_not_copy();
    test_child_step_editor_bottom_right_hold_applies_without_paste();
    test_nested_chord_editor_keeps_its_owning_micro_draft();
    test_step_edit_context_rows_clear_selected_child_context();
    test_graph_compaction_remaps_or_closes_active_child_content_view();
    test_step_edit_context_rows_copy_and_paste_step_content();
    test_step_edit_context_preflight_failures_are_atomic();
    test_step_edit_context_paste_prepares_graphless_destination();
    test_step_edit_musical_row_bottom_left_resets_row_to_default();
    test_step_edit_chord_row_opens_transactional_editor_and_applies_once();
    test_step_draft_apply_coexists_with_adjacent_global_history();
    test_step_edit_chord_detail_edits_all_chord_fields();
    test_scale_context_drives_shape_formula_without_local_basis_toggle();
    test_chord_context_opens_pattern_pitch_context_directly();
    test_formula_editor_is_explicit_cancellable_and_edits_zero_three_five();
    test_child_chord_preview_resolves_inherited_parent_chord();
    test_step_edit_child_chord_detail_localizes_from_inherited_spec();
    test_step_edit_child_chord_row_resets_to_inherit_default();
    test_step_edit_context_clipboard_requires_matching_child_kind();
    test_step_edit_session_undo_redo_workflow();
    test_left_top_close_keeps_live_edit_and_records_history();
    test_step_edit_does_not_open_when_blocked();
    test_step_preset_library_saves_and_loads_focused_step();
    test_step_preset_overwrite_accepts_authoritative_long_press_with_clock_lag();
    test_step_preset_library_pages_without_mutating_and_restores_load_focus();
    test_preset_nav_defers_inspection_until_focus_settles();
    test_blocked_preset_load_keeps_pending_history_coalescing();
    test_step_preset_save_selects_new_asset_beyond_first_page();
    test_step_preset_queued_feedback_resolves_to_applied_and_auto_closes();
    test_step_preset_queued_feedback_resolves_to_cancelled_on_undo();
    test_chord_preset_library_saves_and_loads_only_the_active_draft();
    test_step_and_chord_preset_libraries_share_the_navigation_contract();

    std::cout << "\nAll SequencerStepEditHandler tests passed.\n";
    return 0;
}
