#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>
#include <oc/impl/HostFileSystem.hpp>

#include <config/Timing.hpp>

#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerInputUtils.hpp"
#include "../../src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "../../src/handler/sequencer/SequencerStepEditHandler.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerChordUiOps.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerStepEditRows.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;
namespace input_utils = core::handler::sequencer::input_utils;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;

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

bool stepHasMicroSequence(
    const core::state::sequencer::SequencerPatternState& pattern,
    uint8_t rootStep
) {
    const auto* graph = core::state::sequencer::graphView(pattern);
    const auto* node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(rootStep)) : nullptr;
    return node != nullptr &&
           node->has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE) &&
           graph->sequence(node->childSequenceId) != nullptr;
}

bool stepHasCycleStates(
    const core::state::sequencer::SequencerPatternState& pattern,
    uint8_t rootStep
) {
    const auto* graph = core::state::sequencer::graphView(pattern);
    const auto* node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(rootStep)) : nullptr;
    return node != nullptr &&
           node->has(oc::note::sequencer::STEP_NODE_CYCLE_SET) &&
           graph->cycleSet(node->cycleSetId) != nullptr;
}

struct SequencerStepEditHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 901;
    static constexpr oc::type::ScopeID OVERLAY_SCOPE = 902;
    static constexpr oc::type::ScopeID STEP_PRESET_SCOPE = 903;

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
        : state(storages.settings,
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , filesystem(testRoot().string().c_str())
        , productFiles(filesystem)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::SequencerStepEditHandler::StateRefs{
                      state.overlays,
                      state.sequencer,
                      state.sequencerTracks,
                      state.structureClipboard,
                      state.trackNavigation,
                      state.structureNavigationFocus,
                      core::handler::SequencerHistoryDomainServices::fromCoreState(state),
                      core::handler::SequencerStepPresetDomainServices::fromCoreState(
                          state,
                          productFiles
                      ),
                  },
                  overlays,
                  encoders,
                  buttons,
                  SEQUENCER_SCOPE,
                  OVERLAY_SCOPE,
                  STEP_PRESET_SCOPE)
        , quickControlsHandler(
              core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
                  state.structureNavigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE
          ) {
        resetTestRoot();
        assert(filesystem.init());
        assert(productFiles.init());
        overlays.setActiveViewProvider([]() { return SEQUENCER_SCOPE; });
        overlays.registerCleanup(core::ui::OverlayType::SEQ_STEP_EDIT, OVERLAY_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::SEQ_STEP_PRESET, STEP_PRESET_SCOPE);
        state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        g_now_ms = 0;
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

float normalizedForScaleNote(
    uint8_t note,
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    return input_utils::indexToNormalized(
        input_utils::scaleDegreeIndexForNote(note, settings),
        input_utils::countScaleNotes(settings)
    );
}

void holdPatternQuickControls(SequencerStepEditHarness& h) {
    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.patternQuickControls.physicalHoldActive.get());
}

void test_long_press_opens_step_edit_and_ignores_open_release() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(16);
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

void test_nav_and_opt_edit_then_nav_apply() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.velocity[3] = 64;

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);

    focusStepEditRow(h, VELOCITY_ROW);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == VELOCITY_ROW);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.velocity[3] == 127);

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.pattern.velocity[3] == 127);
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_nav_and_opt_edit_then_nav_apply\n";
}

void test_left_bottom_hold_edits_local_variation_for_focused_property() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[2] = 60;

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);

    focusStepEditRow(h, NOTE_ROW);
    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.stepEdit.localVariationEditActive.get());

    h.turn(Config::EncoderID::OPT, 1.0f);
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(
        core::state::sequencer::nodeLocalVariationRange(
            *node,
            core::state::sequencer::StepProperty::NOTE
        ) ==
        input_utils::variationRangeMaxForProperty(core::state::sequencer::StepProperty::NOTE)
    );
    assert(h.state.sequencer.pattern.note[2] == 60);

    h.release(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.sequencer.stepEdit.localVariationEditActive.get());

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.note[2] == 127);

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_left_bottom_hold_edits_local_variation_for_focused_property\n";
}

void test_chance_row_does_not_enter_local_variation_mode() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);

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
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.velocity[0] = 64;

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

    assert(
        core::state::sequencer::nodeLocalVariationRange(
            *root,
            core::state::sequencer::StepProperty::VELOCITY
        ) == 0
    );
    assert(
        core::state::sequencer::nodeLocalVariationRange(
            *child,
            core::state::sequencer::StepProperty::VELOCITY
        ) ==
        input_utils::variationRangeMaxForProperty(core::state::sequencer::StepProperty::VELOCITY)
    );
    assert(child->velocityOffset == 0);

    h.release(Config::ButtonID::LEFT_BOTTOM);
    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_local_variation_edit_targets_active_child_step_node\n";
}

void test_context_rows_are_focusable_and_root_action_rows_do_not_edit_properties() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
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

    std::cout << "[PASS] test_context_rows_are_focusable_and_root_action_rows_do_not_edit_properties\n";
}

void test_activated_row_edits_root_step_enabled_state() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
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
    h.state.sequencer.pattern.length.set(8);
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
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 3));

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    assert(root != nullptr);
    const auto* sequence = graph->sequence(root->childSequenceId);
    assert(sequence != nullptr);
    const auto* firstMicroStep = graph->stepNode(sequence->firstStepNode);
    assert(firstMicroStep != nullptr);
    assert(firstMicroStep->noteOffset == 0);

    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer,
        0,
        core::state::sequencer::StepProperty::NOTE,
        62.0f / 127.0f,
        h.state.sequencer.pattern.pitchEditMode,
        {}
    ));

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    sequence = graph->sequence(root->childSequenceId);
    firstMicroStep = graph->stepNode(sequence->firstStepNode);
    assert(firstMicroStep != nullptr);
    assert(firstMicroStep->noteOffset == 2);

    holdPatternQuickControls(h);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::LENGTH
    );
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.contentView.length.get() == 16);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());

    h.state.sequencer.focusedStep.set(15);
    h.state.sequencer.page.set(1);
    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer,
        15,
        core::state::sequencer::StepProperty::VELOCITY,
        127.0f / 127.0f,
        h.state.sequencer.pattern.pitchEditMode,
        {}
    ));

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    sequence = graph->sequence(root->childSequenceId);
    const auto* lastMicroStep = graph->stepNode(static_cast<uint16_t>(sequence->firstStepNode + 15U));
    assert(lastMicroStep != nullptr);
    assert(lastMicroStep->velocityOffset == 63);

    assert(core::state::sequencer::leaveContentView(h.state.sequencer));
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 2);

    std::cout << "[PASS] test_create_edit_and_commit_micro_sequence_context\n";
}

void test_step_edit_opens_nested_content_from_child_contexts() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
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
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[2] = 60;
    h.state.sequencer.pattern.velocity[2] = 64;

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);
    focusStepEditRow(h, CYCLE_STATES_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isCycleStatesContentView(h.state.sequencer));
    assert(core::state::sequencer::activeContentLength(h.state.sequencer) == 4);

    holdPatternQuickControls(h);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::LENGTH
    );
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(core::state::sequencer::activeContentLength(h.state.sequencer) == 16);
    h.release(Config::ButtonID::LEFT_CENTER);

    h.state.sequencer.focusedStep.set(15);
    h.state.sequencer.page.set(1);
    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer,
        15,
        core::state::sequencer::StepProperty::VELOCITY,
        127.0f / 127.0f,
        h.state.sequencer.pattern.pitchEditMode,
        {}
    ));

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
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
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[0] = 60;
    h.state.sequencer.pattern.velocity[0] = 64;

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        4
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));

    auto* graph = h.state.sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto wrappedSourceNode = static_cast<uint16_t>(sequence->firstStepNode + 3U);
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        wrappedSourceNode,
        7
    ));

    auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        h.state.sequencer,
        0,
        {}
    );
    assert(projection.valid);
    assert(projection.note == 60);

    holdPatternQuickControls(h);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::LENGTH
    );
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        core::state::sequencer::PatternQuickControlItem::OFFSET
    );
    h.turn(Config::EncoderID::OPT, 4.0f / 6.0f);
    graph = h.state.sequencer.pattern.graph.get();
    assert(graph != nullptr);
    sequence = graph->sequence(h.state.sequencer.contentView.sequenceId.get());
    assert(sequence != nullptr);
    assert(sequence->offset == 0);
    h.release(Config::ButtonID::LEFT_CENTER);

    projection = core::state::sequencer::resolveActiveContentStepProjection(
        h.state.sequencer,
        0,
        {}
    );
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
        core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES
    );
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[3] = 60;

    openStepEdit(h, 3);
    h.release(Config::MACRO_BUTTONS[3]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::NAV);
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));

    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer,
        0,
        core::state::sequencer::StepProperty::NOTE,
        normalizedForScaleNote(62, cMajor),
        h.state.sequencer.pattern.pitchEditMode,
        cMajor
    ));

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* root = graph->stepNode(core::state::sequencer::rootStepNodeId(3));
    assert(root != nullptr);
    const auto* sequence = graph->sequence(root->childSequenceId);
    assert(sequence != nullptr);
    const auto* firstMicroStep = graph->stepNode(sequence->firstStepNode);
    assert(firstMicroStep != nullptr);
    assert(firstMicroStep->noteOffset == 1);

    auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        h.state.sequencer,
        0,
        cMajor
    );
    assert(projection.valid);
    assert(projection.note == 62);
    assert(projection.noteOffset == 1);

    h.state.sequencer.setStepNoteAt(3, 62);
    projection = core::state::sequencer::resolveActiveContentStepProjection(
        h.state.sequencer,
        0,
        cMajor
    );
    assert(projection.valid);
    assert(projection.parentNote == 62);
    assert(projection.note == 64);
    assert(projection.noteOffset == 1);

    std::cout << "[PASS] test_micro_sequence_note_offsets_follow_parent_scale_degrees\n";
}

void test_back_from_child_step_edit_returns_to_parent_and_records_history() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[0] = 60;

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

void test_step_edit_context_rows_clear_selected_child_context() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    ).ok);
    assert(core::state::sequencer::createCycleStateSet(
        h.state.sequencer.pattern,
        rootNode,
        4
    ).ok);
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(stepHasCycleStates(h.state.sequencer.pattern, 0));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);

    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(stepHasCycleStates(h.state.sequencer.pattern, 0));
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(stepHasCycleStates(h.state.sequencer.pattern, 0));
    assert(h.state.sequencerHistory.undoCount() == 1);

    focusStepEditRow(h, CYCLE_STATES_ROW);
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(stepHasCycleStates(h.state.sequencer.pattern, 0));
    assert(h.state.sequencerHistory.undoCount() == 1);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(!stepHasCycleStates(h.state.sequencer.pattern, 0));
    assert(h.state.sequencerHistory.undoCount() == 2);

    std::cout << "[PASS] test_step_edit_context_rows_clear_selected_child_context\n";
}

void test_graph_compaction_remaps_or_closes_active_child_content_view() {
    {
        core::state::sequencer::SequencerState state;
        const auto firstRoot = core::state::sequencer::rootStepNodeId(0);
        const auto secondRoot = core::state::sequencer::rootStepNodeId(1);
        const auto first = core::state::sequencer::createMicroSequence(
            state.pattern,
            firstRoot,
            2
        );
        assert(first.ok);
        const auto second = core::state::sequencer::createMicroSequence(
            state.pattern,
            secondRoot,
            2
        );
        assert(second.ok);
        assert(core::state::sequencer::enterMicroSequenceContentView(
            state,
            secondRoot,
            second.id
        ));

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
        const auto micro = core::state::sequencer::createMicroSequence(
            state.pattern,
            root,
            2
        );
        assert(micro.ok);
        assert(core::state::sequencer::enterMicroSequenceContentView(
            state,
            root,
            micro.id
        ));

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
    h.state.sequencer.pattern.length.set(8);

    const auto sourceNode = core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        sourceNode,
        2
    ).ok);
    assert(stepHasMicroSequence(h.state.sequencer.pattern, 0));
    assert(!stepHasMicroSequence(h.state.sequencer.pattern, 1));

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerStepContent());
    h.tap(Config::ButtonID::LEFT_TOP);

    openStepEdit(h, 1);
    h.release(Config::MACRO_BUTTONS[1]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(stepHasMicroSequence(h.state.sequencer.pattern, 1));
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_step_edit_context_rows_copy_and_paste_step_content\n";
}

void test_step_edit_musical_row_bottom_left_resets_row_to_default() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[2] = 74;
    h.state.sequencer.pattern.velocity[2] = 105;
    h.state.sequencer.pattern.setEnabled(2, true);
    assert(core::state::sequencer::setNodeLocalVariationRange(
        h.state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(2),
        core::state::sequencer::StepProperty::NOTE,
        3
    ));

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
    assert(
        core::state::sequencer::nodeLocalVariationRange(
            *node,
            core::state::sequencer::StepProperty::NOTE
        ) == 0
    );
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_step_edit_musical_row_bottom_left_resets_row_to_default\n";
}

void test_step_edit_chord_row_edits_root_chord_live_and_undoes_as_session() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);
    focusStepEditRow(h, CHORD_ROW);

    h.turn(Config::EncoderID::OPT, 1.0f);
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(node->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(node->chordSpec.voiceCount == oc::note::sequencer::StepSequencerChordSpec::MAX_VOICES);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_CENTER);

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(2)) : nullptr;
    assert(node == nullptr || !node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));

    std::cout << "[PASS] test_step_edit_chord_row_edits_root_chord_live_and_undoes_as_session\n";
}

void test_step_edit_chord_detail_edits_all_chord_fields() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);

    openStepEdit(h, 1);
    h.release(Config::MACRO_BUTTONS[1]);
    focusStepEditRow(h, CHORD_ROW);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(
        h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
        core::state::sequencer::SequencerChordEditField::MODE
    );

    h.turn(Config::EncoderID::OPT, 1.0f);
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(1));
    assert(node != nullptr);
    assert(node->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
        core::state::sequencer::SequencerChordEditField::VOICES
    );
    h.turn(Config::EncoderID::OPT, 1.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
        core::state::sequencer::SequencerChordEditField::COLOR
    );
    h.turn(Config::EncoderID::OPT, 1.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
        core::state::sequencer::SequencerChordEditField::VARIANT
    );
    h.turn(Config::EncoderID::OPT, 1.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
        core::state::sequencer::SequencerChordEditField::SPREAD
    );
    h.turn(Config::EncoderID::OPT, 1.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
        core::state::sequencer::SequencerChordEditField::STRUM
    );
    h.turn(Config::EncoderID::OPT, 0.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
        core::state::sequencer::SequencerChordEditField::VELOCITY_CURVE
    );
    h.turn(Config::EncoderID::OPT, 1.0f);

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(1)) : nullptr;
    assert(node != nullptr);
    assert(node->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL));
    assert(node->chordSpec.voiceCount == oc::note::sequencer::StepSequencerChordSpec::MAX_VOICES);
    assert(node->chordSpec.color == oc::note::sequencer::StepSequencerChordSpec::MAX_COLOR);
    assert(node->chordSpec.variant == oc::note::sequencer::StepSequencerChordSpec::MAX_VARIANT);
    assert(node->chordSpec.spread == oc::note::sequencer::StepSequencerChordSpec::MAX_SPREAD);
    assert(node->chordSpec.strum == oc::note::sequencer::StepSequencerChordSpec::MIN_STRUM);
    assert(
        node->chordSpec.velocityCurve ==
        oc::note::sequencer::StepSequencerChordSpec::MAX_VELOCITY_CURVE
    );

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(1)) : nullptr;
    assert(node != nullptr);
    assert(node->chordSpec.velocityCurve == 0);
    assert(node->chordSpec.strum == oc::note::sequencer::StepSequencerChordSpec::MIN_STRUM);

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepEdit.chordEditor.active.get());
    assert(h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencer.stepEdit.focusedRow.get() == CHORD_ROW);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_CENTER);

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    node = graph ? graph->stepNode(core::state::sequencer::rootStepNodeId(1)) : nullptr;
    assert(node == nullptr || !node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));

    std::cout << "[PASS] test_step_edit_chord_detail_edits_all_chord_fields\n";
}

void test_child_chord_preview_resolves_inherited_parent_chord() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    oc::note::sequencer::StepSequencerChordSpec parentChord{};
    parentChord.voiceCount = 4;
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern,
        rootNode,
        parentChord
    ));

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

    const oc::note::sequencer::StepSequencerScaleSettings scaleSettings{};
    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        h.state.sequencer,
        0,
        scaleSettings
    );
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
    h.state.sequencer.pattern.length.set(8);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    oc::note::sequencer::StepSequencerChordSpec parentChord{};
    parentChord.voiceCount = 4;
    parentChord.color = 2;
    parentChord.variant = 3;
    parentChord.spread = 4;
    parentChord.strum = 25;
    parentChord.velocityCurve = -12;
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern,
        rootNode,
        parentChord
    ));

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

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, CHORD_ROW);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.stepEdit.chordEditor.active.get());
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(
        h.state.sequencer.stepEdit.chordEditor.focusedField.get() ==
        core::state::sequencer::SequencerChordEditField::COLOR
    );
    h.turn(Config::EncoderID::OPT, 1.0f);

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto* child = graph->stepNode(sequence->firstStepNode);
    assert(child != nullptr);
    assert(child->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(child->chordSpec.voiceCount == parentChord.voiceCount);
    assert(child->chordSpec.color == oc::note::sequencer::StepSequencerChordSpec::MAX_COLOR);
    assert(child->chordSpec.variant == parentChord.variant);
    assert(child->chordSpec.spread == parentChord.spread);
    assert(child->chordSpec.strum == parentChord.strum);
    assert(child->chordSpec.velocityCurve == parentChord.velocityCurve);

    std::cout << "[PASS] test_step_edit_child_chord_detail_localizes_from_inherited_spec\n";
}

void test_step_edit_child_chord_row_resets_to_inherit_default() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);

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

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, CHORD_ROW);

    auto chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0);
    assert(chord.valid);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Inherit);

    h.turn(
        Config::EncoderID::OPT,
        input_utils::indexToNormalized(
            1,
            oc::note::sequencer::StepSequencerChordSpec::MAX_VOICES + 1
        )
    );

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto* child = graph->stepNode(sequence->firstStepNode);
    assert(child != nullptr);
    assert(child->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(child->chordMode == oc::note::sequencer::StepSequencerChordMode::Single);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    sequence = graph->sequence(micro.id);
    child = graph->stepNode(sequence->firstStepNode);
    assert(child != nullptr);
    assert(!child->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));

    chord = core::state::sequencer::resolveStepChordUiState(h.state.sequencer, 0);
    assert(chord.valid);
    assert(chord.mode == oc::note::sequencer::StepSequencerChordMode::Inherit);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_step_edit_child_chord_row_resets_to_inherit_default\n";
}

void test_step_edit_context_clipboard_requires_matching_child_kind() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);

    const auto microSource = core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        microSource,
        2
    ).ok);
    const auto cycleSource = core::state::sequencer::rootStepNodeId(2);
    assert(core::state::sequencer::createCycleStateSet(
        h.state.sequencer.pattern,
        cycleSource,
        4
    ).ok);

    openStepEdit(h, 0);
    h.release(Config::MACRO_BUTTONS[0]);
    focusStepEditRow(h, MICRO_SEQUENCE_ROW);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerStepContent(
        core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE
    ));
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
        core::state::SequencerStepContentClipboardKind::CYCLE_STATES
    ));
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
    h.state.sequencer.pattern.length.set(8);
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
    assert(
        h.state.sequencer.pattern.gate[2] ==
        core::state::sequencer::SequencerState::MAX_GATE_PERCENT
    );

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencer.pattern.note[2] == 127);
    assert(
        h.state.sequencer.pattern.gate[2] ==
        core::state::sequencer::SequencerState::MAX_GATE_PERCENT
    );

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.pattern.note[2] == 61);
    assert(h.state.sequencer.pattern.gate[2] == 55);
    assert(h.state.sequencer.focusedStep.get() == 6);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.sequencer.pattern.note[2] == 127);
    assert(
        h.state.sequencer.pattern.gate[2] ==
        core::state::sequencer::SequencerState::MAX_GATE_PERCENT
    );
    assert(h.state.sequencer.focusedStep.get() == 2);
    h.release(Config::ButtonID::LEFT_CENTER);

    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.redoCount() == 0);

    std::cout << "[PASS] test_step_edit_session_undo_redo_workflow\n";
}

void test_left_top_close_keeps_live_edit_and_records_history() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
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

    holdPatternQuickControls(h);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.pattern.note[4] == 62);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);
    h.release(Config::ButtonID::LEFT_CENTER);

    std::cout << "[PASS] test_left_top_close_keeps_live_edit_and_records_history\n";
}

void test_step_edit_does_not_open_when_blocked() {
    {
        SequencerStepEditHarness h;
        h.state.overlays.show(core::ui::OverlayType::DATA_MANAGER);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    {
        SequencerStepEditHarness h;
        h.state.trackNavigation.selection.active.set(true);
        longPressMacro(h, 0);
        assert(!h.state.sequencer.stepEdit.visible.get());
    }

    {
        SequencerStepEditHarness h;
        h.state.sequencer.structureUi.pageSelection.active.set(true);
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

void test_step_preset_picker_saves_and_loads_focused_step() {
    SequencerStepEditHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.setEnabled(2, true);
    assert(h.state.sequencer.setStepDataAt(2, 67, 96, 155, -3, 84));

    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(2),
        2
    );
    assert(micro.ok);
    const auto* sourceGraph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(sourceGraph != nullptr);
    const auto* sourceSequence = sourceGraph->sequence(micro.id);
    assert(sourceSequence != nullptr);
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        static_cast<uint16_t>(sourceSequence->firstStepNode + 1U),
        6
    ));

    openStepEdit(h, 2);
    h.release(Config::MACRO_BUTTONS[2]);

    h.tap(Config::ButtonID::BOTTOM_CENTER);
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_PRESET);
    assert(h.state.sequencer.stepPresetPicker.visible.get());
    assert(
        h.state.sequencer.stepPresetPicker.mode.get() ==
        core::state::sequencer::SequencerStepPresetPickerMode::LOAD
    );
    assert(h.state.sequencer.stepPresetPicker.entryCount.get() == 0);

    h.tap(Config::ButtonID::BOTTOM_CENTER);
    assert(
        h.state.sequencer.stepPresetPicker.mode.get() ==
        core::state::sequencer::SequencerStepPresetPickerMode::SAVE
    );
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(
        h.state.sequencer.stepPresetPicker.feedback.get() ==
        core::state::sequencer::SequencerStepPresetFeedback::SAVED
    );
    assert(h.state.sequencer.stepPresetPicker.entryCount.get() == 1);
    assert(std::strcmp(h.state.sequencer.stepPresetPicker.entryId(0), "step-preset-001") == 0);
    assert(std::filesystem::exists(
        testRoot() / "midi-studio" / "library" / "step-presets" / "step-preset-001.mssp"
    ));

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
    h.tap(Config::ButtonID::BOTTOM_CENTER);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);
    assert(!h.state.sequencer.stepPresetPicker.visible.get());
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
    const auto* targetChild = targetGraph->stepNode(
        static_cast<uint16_t>(targetSequence->firstStepNode + 1U)
    );
    assert(targetChild != nullptr);
    assert(targetChild->noteOffset == 6);
    assert(h.state.sequencerHistory.undoCount() == 2);

    resetTestRoot();
    std::cout << "[PASS] test_step_preset_picker_saves_and_loads_focused_step\n";
}

}  // namespace

int main() {
    test_long_press_opens_step_edit_and_ignores_open_release();
    test_nav_and_opt_edit_then_nav_apply();
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
    test_step_edit_context_rows_clear_selected_child_context();
    test_graph_compaction_remaps_or_closes_active_child_content_view();
    test_step_edit_context_rows_copy_and_paste_step_content();
    test_step_edit_musical_row_bottom_left_resets_row_to_default();
    test_step_edit_chord_row_edits_root_chord_live_and_undoes_as_session();
    test_step_edit_chord_detail_edits_all_chord_fields();
    test_child_chord_preview_resolves_inherited_parent_chord();
    test_step_edit_child_chord_detail_localizes_from_inherited_spec();
    test_step_edit_child_chord_row_resets_to_inherit_default();
    test_step_edit_context_clipboard_requires_matching_child_kind();
    test_step_edit_session_undo_redo_workflow();
    test_left_top_close_keeps_live_edit_and_records_history();
    test_step_edit_does_not_open_when_blocked();
    test_step_preset_picker_saves_and_loads_focused_step();

    std::cout << "\nAll SequencerStepEditHandler tests passed.\n";
    return 0;
}
