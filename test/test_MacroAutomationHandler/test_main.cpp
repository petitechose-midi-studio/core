#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/config/Timing.hpp"
#include "../../src/handler/macro/MacroAutomationHandler.hpp"
#include "../../src/handler/macro/MacroEditDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
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

struct MacroAutomationHarness {
    static constexpr oc::type::ScopeID AUTOMATION_SCOPE = 1401;

    CoreStorages storage;
    core::state::CoreState state;
    core::handler::MacroEditDomainServices services;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::MacroAutomationHandler handler;

    MacroAutomationHarness()
        : state(storage.settings,
                storage.macroLibrary,
                storage.sequencerPatternLibrary,
                storage.sequencerSetLibrary)
        , services(core::handler::MacroEditDomainServices::fromCoreState(state))
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::MacroAutomationHandler::StateRefs{
                      state.macroEdit,
                      state.pages,
                  },
                  services,
                  overlays,
                  encoders,
                  buttons,
                  AUTOMATION_SCOPE,
                  mockTimeMs) {
        overlays.registerCleanup(core::ui::OverlayType::MACRO_AUTOMATION, AUTOMATION_SCOPE);
        overlays.setActiveViewProvider([]() { return AUTOMATION_SCOPE; });
        g_now_ms = 0;
    }

    void openAutomationEditor(uint8_t macroIndex = 0) {
        state.macroEdit.openEditor(macroIndex, 0, 0, 0);
        state.macroEdit.openAutomation();
        overlays.show(core::ui::OverlayType::MACRO_AUTOMATION, true);
    }

    void openModulationEditor(uint8_t macroIndex = 0) {
        state.macroEdit.openEditor(macroIndex, 0, 0, 0);
        state.macroEdit.openModulation();
        overlays.show(core::ui::OverlayType::MACRO_AUTOMATION, true);
    }

    void configureAutomation(uint8_t macroIndex = 0, float durationBeats = 2.0f) {
        state.pages.setMacroSlotActive(macroIndex, true);
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = macroIndex,
        };
        core::state::macro::MacroAutomationLane lane;
        lane.durationBeats = durationBeats;
        assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.0f));
        assert(core::state::macro::macroAutomationAppendPoint(
            lane,
            durationBeats * 0.5f,
            1.0f
        ));
        assert(core::state::macro::macroAutomationAppendPoint(lane, durationBeats, 0.0f));
        assert(test_support::project_control::assignAutomation(
            state.pages.control,
            address,
            lane
        ));
    }

    void configureModulation(uint8_t macroIndex = 0, float depth = 0.5f) {
        state.pages.setMacroSlotActive(macroIndex, true);
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = macroIndex,
        };
        core::state::macro::MacroModulationShape shape;
        shape.durationBeats = 2.0f;
        assert(core::state::macro::macroModulationAppendPoint(shape, 0.0f, 0.25f));
        assert(core::state::macro::macroModulationAppendPoint(shape, 1.0f, -0.25f));
        assert(test_support::project_control::assignModulation(
            state.pages.control,
            address,
            shape,
            depth
        ));
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
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

    void setNow(uint32_t nowMs) {
        g_now_ms = nowMs;
    }

    void flushState() {
        drainNotifications();
        state.flush();
    }
};

void test_playback_row_toggles_automation_without_clearing_curve() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.openAutomationEditor();

    h.turn(Config::EncoderID::OPT, 0.0f);
    auto preserved = test_support::project_control::readSlot(
        h.state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(preserved.automationStored);
    assert(!preserved.automationEnabled);

    h.turn(Config::EncoderID::OPT, 1.0f);
    preserved = test_support::project_control::readSlot(
        h.state.pages.control,
        preserved.address
    );
    assert(preserved.automationEnabled);
    assert(preserved.legacy.automation.pointCount == 3);

    h.flushState();

    std::cout
        << "[PASS] test_playback_row_toggles_automation_without_clearing_curve\n";
}

void test_modulation_entry_synchronizes_opt_to_playback_state() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.configureModulation();
    assert(h.services.sourceModeFor(0) == core::handler::MacroSourceMode::AUTO_MOD);
    h.openModulationEditor();

    h.handler.update(0);

    const auto opt = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
    assert(h.encoderHw.getDiscreteSteps(opt) == 2);
    assert(h.encoderHw.getPosition(opt) == 1.0f);

    std::cout
        << "[PASS] "
        << "test_modulation_entry_synchronizes_opt_to_playback_state\n";
}

void test_contextual_resume_row_restores_sources_and_disappears() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.openAutomationEditor();
    h.services.setManualOverride(0, true);
    assert(h.services.manualOverrideActiveFor(0));

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 1);
    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(!h.services.manualOverrideActiveFor(0));
    assert(h.state.macroEdit.automationFocusedRow.get() == 0);
    std::cout
        << "[PASS] test_contextual_resume_row_restores_sources_and_disappears\n";
}

void test_conversion_is_one_turn_away_and_switches_playback_truth() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.openAutomationEditor();

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 1);
    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::CONVERT_PREVIEW);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.setNow(100);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATION);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::APPLIED);
    const auto* slot = h.services.automationSlot(0);
    assert(slot != nullptr);
    assert(core::state::macro::macroCurveStored(slot->automation));
    assert(!core::state::macro::macroCurvePlaybackActive(slot->automation));
    assert(core::state::macro::macroCurveStored(slot->modulation));
    assert(core::state::macro::macroCurvePlaybackActive(slot->modulation));
    // The triangular 0 -> 1 -> 0 automation is centered on 0.5 and has a
    // symmetric amplitude of 0.5. Conversion preserves that audible range.
    assert(std::fabs(slot->modulationDepth - 0.5f) < 0.0001f);

    std::cout
        << "[PASS] test_conversion_is_one_turn_away_and_switches_playback_truth\n";
}

void test_modulation_tap_toggles_and_hold_clears_only_modulation() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.configureModulation();
    h.openModulationEditor();

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.setNow(100);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    auto slot = test_support::project_control::readSlot(
        h.state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot.automationEnabled);
    assert(slot.modulationStored);
    assert(!slot.modulationEnabled);
    assert(std::fabs(slot.legacy.modulationDepth - 0.5f) < 0.0001f);
    assert(h.state.pages.isMacroSlotActive(0));

    h.press(Config::ButtonID::BOTTOM_LEFT);
    const uint32_t clearAt =
        100U + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    h.setNow(clearAt);
    h.handler.update(clearAt);
    assert(h.state.macroEdit.contextFeedback.get().action ==
           core::state::contextual::ContextActionId::CLEAR);
    h.setNow(clearAt + 10U);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    slot = test_support::project_control::readSlot(
        h.state.pages.control,
        slot.address
    );
    assert(slot.automationEnabled);
    assert(!slot.modulationStored);
    assert(slot.legacy.modulationDepth == 0.0f);
    assert(h.state.pages.isMacroSlotActive(0));
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATION);

    h.flushState();

    std::cout
        << "[PASS] modulation tap toggles and hold clears only Modulation\n";
}

void seedTypedAutomationPaste(MacroAutomationHarness& h) {
    h.configureAutomation(0, 2.0f);
    assert(h.services.copyAutomation(0));
    h.configureAutomation(1, 4.0f);
    h.configureModulation(1, 0.75f);
    h.openAutomationEditor(1);
}

void test_typed_paste_preflight_rejects_invalid_payload_without_mutation() {
    namespace clipboard_ops = core::handler::macro::automation_clipboard_ops;
    MacroAutomationHarness h;
    seedTypedAutomationPaste(h);
    auto& payload = *h.state.structureClipboard.macroAutomationSet;
    auto& source = payload.entries[0].state;
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = h.state.pages.currentActiveTrack(),
        .page = h.state.pages.currentActivePage(),
        .macro = 1,
    };

    auto assertRejectedWithoutMutation = [&]() {
        std::array<unsigned char, sizeof(h.state.pages.control.authored)> before{};
        std::memcpy(
            before.data(),
            &h.state.pages.control.authored,
            sizeof(h.state.pages.control.authored)
        );
        const auto plan = clipboard_ops::preflightAutomationPaste(
            h.state.pages,
            address,
            h.state.structureClipboard
        );
        assert(plan.status == clipboard_ops::MacroTypedPasteStatus::INVALID_PAYLOAD);
        assert(!clipboard_ops::pasteAutomationFromClipboard(
            h.state.pages,
            address,
            h.state.structureClipboard,
            true
        ));
        assert(std::memcmp(
            before.data(),
            &h.state.pages.control.authored,
            sizeof(h.state.pages.control.authored)
        ) == 0);
    };

    const auto validPlaybackState = source.automation.playbackState;
    source.automation.playbackState =
        static_cast<core::state::macro::MacroCurvePlaybackState>(0xFFU);
    assertRejectedWithoutMutation();

    source.automation.playbackState = validPlaybackState;
    source.modulationDepth = std::numeric_limits<float>::quiet_NaN();
    assertRejectedWithoutMutation();

    std::cout
        << "[PASS] "
        << "test_typed_paste_preflight_rejects_invalid_payload_without_mutation\n";
}

void test_typed_paste_quick_release_keeps_copy_semantics() {
    MacroAutomationHarness h;
    seedTypedAutomationPaste(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.setNow(100);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.structureClipboard.hasMacroAutomation());
    assert(h.state.structureClipboard.macroAutomationSet->sourceMacro == 1);
    const auto target = test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 1}
    );
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        target.legacy.automation.durationTicks
    ) == 4.0f);
    std::cout << "[PASS] test_typed_paste_quick_release_keeps_copy_semantics\n";
}

void test_typed_paste_early_armed_release_cancels_without_copy_or_mutation() {
    MacroAutomationHarness h;
    seedTypedAutomationPaste(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(250);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::ARMED);
    h.setNow(300);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.structureClipboard.macroAutomationSet->sourceMacro == 0);
    const auto target = test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 1}
    );
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        target.legacy.automation.durationTicks
    ) == 4.0f);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::CANCELLED);
    std::cout << "[PASS] test_typed_paste_early_armed_release_cancels_without_copy_or_mutation\n";
}

void test_typed_paste_commits_once_after_full_guard() {
    MacroAutomationHarness h;
    seedTypedAutomationPaste(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);

    auto target = test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 1}
    );
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        target.legacy.automation.durationTicks
    ) == 4.0f);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);

    h.setNow(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 10U);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    target = test_support::project_control::readSlot(
        h.state.pages.control,
        target.address
    );
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        target.legacy.automation.durationTicks
    ) == 2.0f);
    assert(target.modulationStored);
    assert(std::fabs(target.legacy.modulationDepth - 0.75f) < 0.0001f);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::APPLIED);
    std::cout << "[PASS] test_typed_paste_commits_once_after_full_guard\n";
}

void test_hold_clear_automation_keeps_slot_and_detail_surface() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.overlays.show(core::ui::OverlayType::MACRO_EDIT);
    h.openAutomationEditor();
    assert(h.overlays.hasVisible());
    assert(h.overlays.isCurrent(core::ui::OverlayType::MACRO_AUTOMATION));
    assert(h.overlays.currentScope() == MacroAutomationHarness::AUTOMATION_SCOPE);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.handler.update(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.macroEdit.contextButton.get() ==
           core::state::MacroContextButton::BOTTOM_LEFT);
    assert(h.state.macroEdit.contextFeedback.get().action ==
           core::state::contextual::ContextActionId::CLEAR);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);
    h.setNow(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 10U);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.pages.isMacroSlotActive(0));
    assert(h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::AUTOMATION);
    assert(h.overlays.hasVisible());
    // Clearing the last Project Control assignment removes the cold control
    // slot itself. The musical Macro slot and its editor remain present.
    assert(h.services.automationSlot(0) == nullptr);
    assert(!test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 0}
    ).automationStored);

    std::cout
        << "[PASS] Automation hold-clear preserves Slot and detail surface\n";
}

void test_navigation_cancels_completed_guard_before_release_without_mutation() {
    MacroAutomationHarness h;
    seedTypedAutomationPaste(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);

    h.setNow(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 10U);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::CANCELLED);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::CANCELLED);

    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.macroAutomationSet->sourceMacro == 0);
    const auto target = test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 1}
    );
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        target.legacy.automation.durationTicks
    ) == 4.0f);
    std::cout
        << "[PASS] test_navigation_cancels_completed_guard_before_release_without_mutation\n";
}

void test_length_row_resizes_automation_duration_without_scaling_points() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.openAutomationEditor();

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 2);
    h.turn(Config::EncoderID::OPT, 2.0f / 63.0f);

    auto slot = test_support::project_control::readSlot(
        h.state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot.automationEnabled);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.legacy.automation.durationTicks) == 3.0f);

    h.turn(Config::EncoderID::OPT, 1.0f);

    slot = test_support::project_control::readSlot(h.state.pages.control, slot.address);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.legacy.automation.durationTicks) == 64.0f);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.legacy.automation.sourceDurationTicks) == 2.0f);
    assert(slot.legacy.automation.pointCount == 3);

    const auto point = test_support::project_control::readCurvePoint(
        h.state.pages.control,
        slot.automationCurveId,
        1,
        false
    );
    assert(point.beat == 1.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 3);
    h.turn(Config::EncoderID::OPT, 1.0f);
    slot = test_support::project_control::readSlot(h.state.pages.control, slot.address);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.legacy.automation.windowOffsetTicks) == 1.0f);

    h.flushState();

    std::cout << "[PASS] test_length_row_resizes_automation_duration_without_scaling_points\n";
}

void test_left_center_enables_coarse_length_and_offset_steps_temporarily() {
    MacroAutomationHarness h;
    h.configureAutomation(0, 8.0f);
    h.openAutomationEditor();

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 2);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 64);

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 16);
    h.turn(Config::EncoderID::OPT, 1.0f / 15.0f);

    auto slot = test_support::project_control::readSlot(
        h.state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.legacy.automation.durationTicks) == 8.0f);

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 64);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 3);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 8);

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 2);
    h.turn(Config::EncoderID::OPT, 1.0f);
    slot = test_support::project_control::readSlot(h.state.pages.control, slot.address);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.legacy.automation.windowOffsetTicks) == 4.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 8);

    h.flushState();

    std::cout << "[PASS] test_left_center_enables_coarse_length_and_offset_steps_temporarily\n";
}

}  // namespace

int main() {
    test_playback_row_toggles_automation_without_clearing_curve();
    test_modulation_entry_synchronizes_opt_to_playback_state();
    test_contextual_resume_row_restores_sources_and_disappears();
    test_conversion_is_one_turn_away_and_switches_playback_truth();
    test_modulation_tap_toggles_and_hold_clears_only_modulation();
    test_typed_paste_preflight_rejects_invalid_payload_without_mutation();
    test_typed_paste_quick_release_keeps_copy_semantics();
    test_typed_paste_early_armed_release_cancels_without_copy_or_mutation();
    test_typed_paste_commits_once_after_full_guard();
    test_hold_clear_automation_keeps_slot_and_detail_surface();
    test_navigation_cancels_completed_guard_before_release_without_mutation();
    test_length_row_resizes_automation_duration_without_scaling_points();
    test_left_center_enables_coarse_length_and_offset_steps_temporarily();
    std::cout << "\nAll MacroAutomationHandler tests passed.\n";
    return 0;
}
