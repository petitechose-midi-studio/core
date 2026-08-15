#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>
#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include <config/Timing.hpp>
#include "../../src/handler/macro/MacroEditDomainServices.hpp"
#include "../../src/handler/macro/MacroEditHandler.hpp"
#include "../../src/handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "../../src/state/project/ProjectTrackDomainServices.hpp"
#include "../../src/sequencer/MidiCcGlobalFrameCoordinator.hpp"
#include "../../src/handler/common/ProjectRecordedShapeCaptureWorkflow.hpp"
#include "../../src/sequencer/RealtimeMidiQueue.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/modulation/ProjectModulationDomainOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/NotificationTestUtils.hpp"
#include "../support/ProjectControlTestUtils.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::CoreStorages;
using test_support::drainNotifications;
using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

struct MacroEditHarness {
    static constexpr oc::type::ScopeID MACRO_VIEW_SCOPE = 401;
    static constexpr oc::type::ScopeID EDIT_SCOPE = 402;
    static constexpr oc::type::ScopeID VALUE_SCOPE = 403;

    CoreStorages storage;
    core::state::CoreState state;
    core::handler::MacroEditDomainServices services;
    core::handler::MacroPerformanceDomainServices performanceServices;
    core::sequencer::RealtimeMidiQueue realtimeQueue;
    core::sequencer::MidiCcGlobalFrameCoordinator midiCoordinator;
    core::handler::MacroMidiCcRuntimeAdapter midiRuntime;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::MacroEditHandler handler;

    MacroEditHarness()
        : state(storage.settings)
        , services(core::handler::MacroEditDomainServices::fromCoreState(state))
        , performanceServices(
              core::handler::MacroPerformanceDomainServices::fromCoreState(state)
          )
        , midiCoordinator(realtimeQueue)
        , midiRuntime(
              core::handler::MacroMidiCcRuntimeAdapter::StateRefs{
                  state.pages,
                  state.projectTracks,
              },
              performanceServices,
              midiCoordinator
          )
        , inputBinding(eventBus, mockTimeMs, Config::Input::CONFIG)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::MacroEditHandler::StateRefs{
                       state.macroEdit,
                       state.pages,
                       state.macroUi,
                       state.statusBar,
                       state.macroHistory,
                   },
                  services,
                  performanceServices,
                  midiRuntime,
                  overlays,
                  encoders,
                  buttons,
                  MACRO_VIEW_SCOPE,
                  EDIT_SCOPE,
                  VALUE_SCOPE,
                  mockTimeMs) {
        overlays.registerCleanup(core::ui::OverlayType::MACRO_EDIT, EDIT_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::MACRO_EDIT_SELECTOR, VALUE_SCOPE);
        overlays.setActiveViewProvider([]() { return MACRO_VIEW_SCOPE; });
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

    void pressMacro(uint8_t index) {
        press(Config::MACRO_BUTTONS[index]);
    }

    void releaseMacro(uint8_t index) {
        release(Config::MACRO_BUTTONS[index]);
    }

    void tapMacro(uint8_t index) {
        tap(Config::MACRO_BUTTONS[index]);
    }

    void turn(Config::EncoderID id, float delta) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, delta);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, delta));
    }

    void flushState() {
        drainNotifications();
        state.flush();
    }
};

void openMacroEdit(MacroEditHarness& h, uint8_t macroIndex, uint32_t releaseAtMs) {
    h.tick(0);
    h.state.pages.setMacroSlotActive(macroIndex, true);
    h.state.macroUi.focusedMacroSlot.set(macroIndex);
    h.state.macroUi.performanceOverlayMode.set(
        core::state::macro::MacroPerformanceOverlayMode::EDIT
    );
    h.press(Config::ButtonID::NAV);
    h.tick(releaseAtMs);
    h.release(Config::ButtonID::NAV);
    assert(h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.editingIndex.get() == macroIndex);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);
}

void test_quick_release_keeps_macro_edit_open_and_left_top_closes() {
    MacroEditHarness h;

    openMacroEdit(
        h,
        0,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
    );

    assert(h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    h.flushState();

    std::cout << "[PASS] test_quick_release_keeps_macro_edit_open_and_left_top_closes\n";
}

void test_macro_press_alone_is_inert_and_edit_release_never_closes() {
    MacroEditHarness h;

    h.pressMacro(0U);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS * 2U);
    h.releaseMacro(0U);
    assert(!h.state.macroEdit.visible.get());

    openMacroEdit(
        h,
        0,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 600U
    );

    assert(h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);

    h.flushState();

    std::cout << "[PASS] Macro press is inert and Edit release is stable\n";
}

void test_edit_intent_nav_opens_the_focused_macro() {
    MacroEditHarness h;
    h.state.pages.setMacroSlotActive(2U, true);
    h.state.macroUi.focusedMacroSlot.set(2U);
    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.state.macroUi.performanceOverlayMode.set(
        core::state::macro::MacroPerformanceOverlayMode::EDIT
    );

    h.tap(Config::ButtonID::NAV);

    assert(h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.editingIndex.get() == 2U);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);
    h.release(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.macroEdit.visible.get());
    assert(h.state.macroUi.performanceOverlayMode.get() ==
           core::state::macro::MacroPerformanceOverlayMode::NONE);

    h.flushState();
    std::cout << "[PASS] test_edit_intent_nav_opens_the_focused_macro\n";
}

void test_macro_edit_cycles_active_macros_and_contextual_destination_props() {
    MacroEditHarness h;

    assert(core::state::project::ProjectTrackDomainServices::fromCoreState(
               h.state
           ).setMidiChannel(0U, 5U));
    h.state.pages.activePageData().setMacroActive(3U, true);
    h.state.pages.activePageData().cc[3] = 99U;
    h.state.pages.updateActiveConfigs();

    openMacroEdit(
        h,
        0,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
    );
    assert(h.state.macroEdit.tempChannel.get() == 5);
    assert(h.state.macroEdit.tempCC.get() == 0);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.macroEdit.tempCC.get() == 0);
    assert(h.services.activeConfig(0).cc == 0);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::VALUE_SELECTOR);
    assert(h.state.macroEdit.selector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT_SELECTOR);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.selector.selectedIndex.get() == 1);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);
    assert(h.state.macroEdit.tempCC.get() == 1);
    assert(h.services.activeConfig(0).cc == 0);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.state.macroEdit.macroCycleActive.get());
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.editingIndex.get() == 3U);
    assert(h.state.macroEdit.tempCC.get() == 99U);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.macroEdit.macroCycleActive.get());
    assert(h.state.pages.currentActivePage() == 0U);
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);
    assert(h.state.pages.tracks[h.state.pages.currentActiveTrack()].pages[0].cc[0] == 1);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);

    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.macroEdit.contextSelectorActive.get());
    assert(h.state.macroEdit.contextPropertyIndex.get() == 0U);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.contextPropertyIndex.get() == 1U);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.pages.currentActiveTrack()
    ] == 15U);
    h.release(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.state.macroEdit.contextSelectorActive.get());
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    h.flushState();

    std::cout << "[PASS] active-Macro cycle and contextual Destination props\n";
}

void test_macro_edit_automation_row_exposes_direct_playback_and_detail() {
    MacroEditHarness h;

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = h.state.pages.currentActiveTrack(),
        .page = h.state.pages.currentActivePage(),
        .macro = 0,
    };
    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = 2.0f;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.0f));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, 1.0f));
    assert(test_support::project_control::assignAutomation(
        h.state.pages.control,
        address,
        lane
    ));

    openMacroEdit(
        h,
        0,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
    );

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.focusedRow.get() == 1);

    // OPT is the direct, semantic control for the focused summary row. It
    // changes playback state without deleting or rewriting the recorded lane.
    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(!h.services.automationPlaybackActiveFor(0));
    const auto* playbackHistory = h.state.projectHistory.peekUndo();
    assert(playbackHistory != nullptr);
    assert(playbackHistory->domain ==
           core::state::project::ProjectHistoryDomain::Macro);
    assert(playbackHistory->actionKind == static_cast<uint8_t>(
        core::state::macro::MacroHistoryActionKind::AUTOMATION_STATE
    ));

    const auto disabled = test_support::project_control::readSlot(
        h.state.pages.control,
        address
    );
    assert(!disabled.automation.enabled);
    assert(disabled.automation.pointCount == 2);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.services.automationPlaybackActiveFor(0));

    // A running Automation can be in Manual takeover. In that state the
    // contextual Playback shortcut projects as the resumable Off position;
    // turning OPT On releases Manual without touching the stored curve.
    h.services.setManualOverride(0, true);
    assert(h.services.manualOverrideActiveFor(0));
    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.macroEdit.contextSelectorActive.get());
    assert(h.state.macroEdit.contextPropertyIndex.get() == 0U);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.contextPropertyIndex.get() == 1U);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(!h.services.manualOverrideActiveFor(0));
    assert(h.services.automationPlaybackActiveFor(0));
    h.release(Config::ButtonID::LEFT_BOTTOM);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::AUTOMATION);
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_AUTOMATION);

    const auto preserved = test_support::project_control::readSlot(
        h.state.pages.control,
        address
    );
    assert(preserved.automation.enabled);
    assert(preserved.automation.pointCount == 2);

    h.flushState();

    std::cout
        << "[PASS] test_macro_edit_automation_row_exposes_direct_playback_and_detail\n";
}

void test_macro_edit_contextual_record_uses_shared_take_and_lane_length() {
    MacroEditHarness h;
    // Contextual Record targets only the focused Macro even when other slots
    // are active on the same Page.
    h.state.pages.activePageData().setMacroActive(1U, true);
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = h.state.pages.currentActiveTrack(),
        .page = h.state.pages.currentActivePage(),
        .macro = 0,
    };
    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = 2.0f;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.2f));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 2.0f, 0.8f));
    assert(test_support::project_control::assignAutomation(
        h.state.pages.control,
        address,
        lane
    ));
    const uint8_t undoBefore = h.state.macroHistory.undoCount();

    openMacroEdit(
        h,
        0,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
    );
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.focusedRow.get() == 1U);
    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.macroEdit.contextPropertyIndex.get() == 0U);

    g_now_ms = 1000U;
    h.turn(Config::EncoderID::OPT, 0.75f);
    assert(h.performanceServices.automationTakeRecording());
    assert(h.state.macroUi.automationTake.candidateMask == 0x0001U);
    assert(h.state.macroUi.automationTake.durationTicks == 384U);

    g_now_ms = 1250U;
    h.release(Config::ButtonID::LEFT_BOTTOM);
    assert(!h.performanceServices.automationTakeRecording());
    assert(h.state.macroHistory.undoCount() == undoBefore + 1U);
    const auto result = test_support::project_control::readSlot(
        h.state.pages.control,
        address
    );
    assert(result.automation.enabled);
    assert(result.automation.spec.durationTicks == 384U);
    std::cout << "[PASS] Macro Edit Record Macro reuses shared take\n";
}

void test_remove_waits_for_owner_scope_release_without_fallback_dispatch() {
    for (const auto focus : {
             core::state::StructureNavigationFocus::TRACK,
             core::state::StructureNavigationFocus::PAGE,
         }) {
        MacroEditHarness h;
        uint8_t fallbackReleaseCount = 0;
        h.buttons.button(Config::ButtonID::BOTTOM_LEFT)
            .release()
            .scope(MacroEditHarness::MACRO_VIEW_SCOPE)
            .then([&fallbackReleaseCount]() { ++fallbackReleaseCount; });

        h.state.structureNavigationFocus.set(focus);
        openMacroEdit(
            h,
            0,
            Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
        );
        assert(h.state.pages.isMacroSlotActive(0));

        h.tick(1400);
        h.press(Config::ButtonID::BOTTOM_LEFT);
        h.handler.update(2400);

        // Visible progress may be complete, but no mutation or scope
        // transition occurs before the owning overlay receives the release.
        assert(h.state.macroEdit.contextGuard.get().phase ==
               core::state::contextual::GuardedActionPhase::COMMITTED);
        assert(h.state.pages.isMacroSlotActive(0));
        assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);

        h.tick(2450);
        h.release(Config::ButtonID::BOTTOM_LEFT);
        assert(!h.state.pages.isMacroSlotActive(0));
        assert(h.overlays.current() == core::ui::OverlayType::NONE);
        assert(fallbackReleaseCount == 0);

        h.flushState();
    }
    std::cout
        << "[PASS] test_remove_waits_for_owner_scope_release_without_fallback_dispatch\n";
}

void test_corrupted_focused_row_cannot_commit_an_armed_remove() {
    MacroEditHarness h;
    openMacroEdit(
        h,
        0,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
    );
    assert(h.state.pages.isMacroSlotActive(0U));

    h.tick(1400U);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.handler.update(2400U);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);

    // Simulate stale/corrupted UI focus between arming and release. The
    // semantic target can no longer be resolved, so the destructive action
    // must fail closed rather than falling back to Destination.
    h.state.macroEdit.focusedRow.set(0xFFU);
    h.tick(2450U);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.pages.isMacroSlotActive(0U));
    assert(h.state.macroEdit.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::MACRO_EDIT);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::FAILED);

    h.flushState();
    std::cout
        << "[PASS] corrupted Macro focus cannot commit an armed remove\n";
}

void test_contextual_recorded_shape_depth_uses_continuous_full_range() {
    using namespace core::state::modulation;
    MacroEditHarness h;
    h.state.pages.setMacroSlotActive(0U, true);
    auto& control = h.state.pages.control;
    auto& graph = control.authored.modulation;

    const ProjectPackedCurvePoint points[]{
        {0U, -32767},
        {192U, 32767},
    };
    RecordedShapeDraft sourceDraft{};
    sourceDraft.name = "Gesture";
    sourceDraft.curve = {
        .sourceDurationTicks = 192U,
        .durationTicks = 192U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    sourceDraft.points = points;
    sourceDraft.pointCount = 2U;
    const auto source = createRecordedShapeModulator(
        graph,
        control.authored.curves,
        sourceDraft
    );
    assert(source.changed());

    ModulationBindingDraft bindingDraft{};
    bindingDraft.sourceId = source.sourceId;
    bindingDraft.destination = projectControlDestination({0U, 0U, 0U});
    bindingDraft.amountQ15 = 0;
    const auto assigned = addProjectModulationBinding(graph, bindingDraft);
    assert(assigned.changed());
    control.markAuthoredMutation();
    assert(h.services.focusModulationBinding(0U, assigned.bindingId));

    openMacroEdit(
        h,
        0U,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
    );
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.focusedRow.get() == 2U);

    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.macroEdit.contextSelectorActive.get());
    assert(h.state.macroEdit.contextPropertyIndex.get() == 0U);
    constexpr auto OPT_ID =
        static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
    assert(h.encoderHw.getDiscreteSteps(OPT_ID) == 0U);
    assert(std::fabs(h.encoderHw.getPosition(OPT_ID) - 0.5f) < 0.001f);

    h.turn(Config::EncoderID::OPT, 0.75f);
    const auto* binding = findProjectModulationBinding(
        graph,
        assigned.bindingId
    );
    assert(binding != nullptr);
    assert(binding->amountQ15 == 16384);
    h.release(Config::ButtonID::LEFT_BOTTOM);

    std::cout << "[PASS] Macro contextual Recorded Shape Depth is +100% at Q15/2\n";
}

void test_contextual_recorded_shape_capture_commits_once_and_restores_opt() {
    namespace mod = core::state::modulation;
    MacroEditHarness h;
    h.state.pages.setMacroSlotActive(0U, true);
    auto& control = h.state.pages.control;
    const uint16_t sourceBefore = control.authored.modulation.sourceCount;
    const uint16_t bindingBefore =
        control.authored.modulation.outputBindingCount;
    const uint8_t undoBefore = h.state.macroHistory.undoCount();

    openMacroEdit(
        h,
        0U,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
    );
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.focusedRow.get() == 2U);
    h.press(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.macroEdit.contextPropertyIndex.get() == 0U);
    constexpr auto OPT_ID =
        static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
    assert(h.encoderHw.getMode(OPT_ID) == oc::interface::EncoderMode::RAW);

    g_now_ms = 1000U;
    h.turn(Config::EncoderID::OPT, 60.0f);
    assert(h.state.macroUi.recordedShapeCapture.active());
    assert(control.authored.modulation.sourceCount == sourceBefore);
    assert(control.authored.modulation.outputBindingCount == bindingBefore);
    h.handler.update(1250U);
    g_now_ms = 1250U;
    h.release(Config::ButtonID::LEFT_BOTTOM);

    assert(!h.state.macroUi.recordedShapeCapture.active());
    assert(h.state.macroUi.recordedShapeCapture.status ==
           mod::ProjectRecordedShapeCaptureStatus::COMMITTED);
    assert(control.authored.modulation.sourceCount == sourceBefore + 1U);
    assert(control.authored.modulation.outputBindingCount == bindingBefore + 1U);
    assert(h.state.macroHistory.undoCount() == undoBefore + 1U);
    assert(h.encoderHw.getMode(OPT_ID) ==
           oc::interface::EncoderMode::NORMALIZED);

    const auto& source = control.authored.modulation.sources[sourceBefore];
    const auto& binding =
        control.authored.modulation.outputBindings[bindingBefore];
    assert(source.kind == mod::ModulatorKind::RECORDED_SHAPE);
    assert(binding.sourceId == source.id);
    assert(binding.destination == mod::projectControlDestination({0U, 0U, 0U}));
    assert(binding.amountQ15 ==
           core::handler::ProjectRecordedShapeCaptureWorkflow::
               DEPTH_100_PERCENT_Q15);
    const auto* curve = mod::findProjectCurve(
        control.authored.curves,
        source.parameters.recordedCurveId
    );
    assert(curve != nullptr);
    assert(curve->durationTicks ==
           4U * core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT);
    assert(h.services.focusedModulationBinding(0U) == binding.id);

    std::cout << "[PASS] Macro Recorded Shape RAW capture commits once\n";
}

void test_contextual_recorded_shape_capture_no_move_and_close_are_atomic() {
    {
        MacroEditHarness h;
        h.state.pages.setMacroSlotActive(0U, true);
        openMacroEdit(
            h,
            0U,
            Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
        );
        h.turn(Config::EncoderID::NAV, 1.0f);
        h.turn(Config::EncoderID::NAV, 1.0f);
        h.press(Config::ButtonID::LEFT_BOTTOM);
        h.release(Config::ButtonID::LEFT_BOTTOM);
        assert(h.state.pages.control.authored.modulation.sourceCount == 0U);
        assert(h.state.macroHistory.undoCount() == 0U);
    }
    {
        MacroEditHarness h;
        h.state.pages.setMacroSlotActive(0U, true);
        openMacroEdit(
            h,
            0U,
            Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
        );
        h.turn(Config::EncoderID::NAV, 1.0f);
        h.turn(Config::EncoderID::NAV, 1.0f);
        h.press(Config::ButtonID::LEFT_BOTTOM);
        g_now_ms = 1000U;
        h.turn(Config::EncoderID::OPT, 60.0f);
        assert(h.state.macroUi.recordedShapeCapture.active());
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.macroUi.recordedShapeCapture.active());
        assert(h.state.macroUi.recordedShapeCapture.status ==
               core::state::modulation::
                   ProjectRecordedShapeCaptureStatus::CANCELLED);
        assert(h.state.pages.control.authored.modulation.sourceCount == 0U);
        assert(h.state.macroHistory.undoCount() == 0U);
    }
    std::cout << "[PASS] Macro Recorded Shape no-op/cancel stay atomic\n";
}

void test_macro_handler_does_not_cancel_project_owned_recorded_shape_capture() {
    MacroEditHarness h;
    auto projectCapture =
        core::handler::ProjectRecordedShapeCaptureWorkflow::fromCoreState(
            h.state
        );
    assert(projectCapture.armCreateUnassigned(
        0U,
        4U * core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT,
        "Project Motion"
    ));

    // MacroFeature keeps updating while Project owns the visible view. A
    // shared capture must therefore be cancelled only by its owning handler.
    h.handler.update(10U);
    assert(projectCapture.active());

    // Closing a Macro editor is likewise scoped to a capture started by that
    // editor; it must not consume a concurrently owned Project transaction.
    h.state.pages.setMacroSlotActive(0U, true);
    openMacroEdit(
        h,
        0U,
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 200U
    );
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(projectCapture.active());

    assert(projectCapture.cancel());
    h.flushState();
    std::cout << "[PASS] Macro handler preserves Project-owned Recorded Shape capture\n";
}

}  // namespace

int main() {
    test_quick_release_keeps_macro_edit_open_and_left_top_closes();
    test_macro_press_alone_is_inert_and_edit_release_never_closes();
    test_edit_intent_nav_opens_the_focused_macro();
    test_macro_edit_cycles_active_macros_and_contextual_destination_props();
    test_macro_edit_automation_row_exposes_direct_playback_and_detail();
    test_macro_edit_contextual_record_uses_shared_take_and_lane_length();
    test_remove_waits_for_owner_scope_release_without_fallback_dispatch();
    test_corrupted_focused_row_cannot_commit_an_armed_remove();
    test_contextual_recorded_shape_depth_uses_continuous_full_range();
    test_contextual_recorded_shape_capture_commits_once_and_restores_opt();
    test_contextual_recorded_shape_capture_no_move_and_close_are_atomic();
    test_macro_handler_does_not_cancel_project_owned_recorded_shape_capture();
    std::cout << "\nAll MacroEditHandler tests passed.\n";
    return 0;
}
