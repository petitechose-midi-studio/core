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
        auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
            state.pages.automation,
            core::state::macro::MacroAutomationSlotAddress{
                .track = state.pages.currentActiveTrack(),
                .page = state.pages.currentActivePage(),
                .macro = macroIndex,
            }
        );
        assert(slot != nullptr);
        core::state::macro::MacroAutomationLane lane;
        lane.durationBeats = durationBeats;
        assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.0f));
        assert(core::state::macro::macroAutomationAppendPoint(
            lane,
            durationBeats * 0.5f,
            1.0f
        ));
        assert(core::state::macro::macroAutomationAppendPoint(lane, durationBeats, 0.0f));
        assert(core::state::macro::macroAutomationAssignAutomation(
            state.pages.automation,
            *slot,
            lane
        ));
    }

    void configureModulation(uint8_t macroIndex = 0, float depth = 0.5f) {
        state.pages.setMacroSlotActive(macroIndex, true);
        auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
            state.pages.automation,
            core::state::macro::MacroAutomationSlotAddress{
                .track = state.pages.currentActiveTrack(),
                .page = state.pages.currentActivePage(),
                .macro = macroIndex,
            }
        );
        assert(slot != nullptr);
        core::state::macro::MacroModulationShape shape;
        shape.durationBeats = 2.0f;
        assert(core::state::macro::macroModulationAppendPoint(shape, 0.0f, 0.25f));
        assert(core::state::macro::macroModulationAppendPoint(shape, 1.0f, -0.25f));
        assert(core::state::macro::macroAutomationAssignModulation(
            state.pages.automation,
            *slot,
            shape
        ));
        slot->modulationDepth = depth;
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

void test_state_row_restores_auto_without_clearing_lane() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.openAutomationEditor();
    h.services.setManualOverride(0, true);

    h.turn(Config::EncoderID::OPT, 1.0f);

    assert((h.state.macroUi.automationManualOverrideMask.get() & 0x0001) == 0);
    const auto* preserved = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(preserved != nullptr);
    assert(preserved->automation.active);
    assert(preserved->automation.pointCount == 3);

    h.flushState();

    std::cout << "[PASS] test_state_row_restores_auto_without_clearing_lane\n";
}

void test_modulation_entry_synchronizes_opt_to_visible_source_mode() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.configureModulation();
    assert(h.services.sourceModeFor(0) == core::handler::MacroSourceMode::AUTO_MOD);
    h.openModulationEditor();

    h.handler.update(0);

    const auto opt = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
    assert(h.encoderHw.getDiscreteSteps(opt) == 3);
    assert(h.encoderHw.getPosition(opt) == 1.0f);

    std::cout
        << "[PASS] "
        << "test_modulation_entry_synchronizes_opt_to_visible_source_mode\n";
}

void test_modulation_tap_clear_preserves_automation_and_destination() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.configureModulation();
    h.openModulationEditor();

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.setNow(100);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    const auto* slot = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot != nullptr);
    assert(slot->automation.active);
    assert(!slot->modulation.active);
    assert(slot->modulationDepth == 0.0f);
    assert(h.state.pages.isMacroSlotActive(0));

    h.flushState();

    std::cout << "[PASS] test_modulation_tap_clear_preserves_automation_and_destination\n";
}

void seedTypedSlotPaste(MacroAutomationHarness& h) {
    h.configureAutomation(0, 2.0f);
    const auto source = core::state::macro::MacroAutomationSlotAddress{
        .track = h.state.pages.currentActiveTrack(),
        .page = h.state.pages.currentActivePage(),
        .macro = 0,
    };
    assert(h.state.structureClipboard.storeMacroSlot(h.state.pages, source));
    h.configureAutomation(1, 4.0f);
    h.openAutomationEditor(1);
}

void test_typed_paste_preflight_rejects_invalid_payload_without_mutation() {
    namespace clipboard_ops = core::handler::macro::automation_clipboard_ops;
    MacroAutomationHarness h;
    seedTypedSlotPaste(h);
    auto& payload = *h.state.structureClipboard.macroAutomationSet;
    auto& source = payload.entries[0].state;
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = h.state.pages.currentActiveTrack(),
        .page = h.state.pages.currentActivePage(),
        .macro = 1,
    };

    auto assertRejectedWithoutMutation = [&]() {
        std::array<unsigned char, sizeof(h.state.pages.automation)> before{};
        std::memcpy(
            before.data(),
            &h.state.pages.automation,
            sizeof(h.state.pages.automation)
        );
        const auto plan = clipboard_ops::preflightSlotPaste(
            h.state.pages,
            address,
            h.state.structureClipboard
        );
        assert(plan.status == clipboard_ops::MacroTypedPasteStatus::INVALID_PAYLOAD);
        assert(!clipboard_ops::pasteSlotFromClipboard(
            h.state.pages,
            address,
            h.state.structureClipboard,
            true
        ));
        assert(std::memcmp(
            before.data(),
            &h.state.pages.automation,
            sizeof(h.state.pages.automation)
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
    seedTypedSlotPaste(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.setNow(100);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.structureClipboard.hasMacroSlot());
    assert(h.state.structureClipboard.macroAutomationSet->sourceMacro == 1);
    const auto* target = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 1}
    );
    assert(target != nullptr);
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        target->automation.durationTicks
    ) == 4.0f);
    std::cout << "[PASS] test_typed_paste_quick_release_keeps_copy_semantics\n";
}

void test_typed_paste_early_armed_release_cancels_without_copy_or_mutation() {
    MacroAutomationHarness h;
    seedTypedSlotPaste(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(250);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::ARMED);
    h.setNow(300);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.structureClipboard.macroAutomationSet->sourceMacro == 0);
    const auto* target = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 1}
    );
    assert(target != nullptr);
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        target->automation.durationTicks
    ) == 4.0f);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::CANCELLED);
    std::cout << "[PASS] test_typed_paste_early_armed_release_cancels_without_copy_or_mutation\n";
}

void test_typed_paste_commits_once_after_full_guard() {
    MacroAutomationHarness h;
    seedTypedSlotPaste(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);

    const auto* target = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 1}
    );
    assert(target != nullptr);
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        target->automation.durationTicks
    ) == 4.0f);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);

    h.setNow(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 10U);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        target->automation.durationTicks
    ) == 2.0f);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::APPLIED);
    std::cout << "[PASS] test_typed_paste_commits_once_after_full_guard\n";
}

void test_remove_slot_closes_detail_and_parent_editor_surfaces() {
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
           core::state::contextual::ContextActionId::REMOVE);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);
    h.setNow(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 10U);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(!h.state.pages.isMacroSlotActive(0));
    assert(!h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::CLOSED);
    assert(!h.overlays.hasVisible());

    std::cout
        << "[PASS] test_remove_slot_closes_detail_and_parent_editor_surfaces\n";
}

void test_navigation_cancels_completed_guard_before_release_without_mutation() {
    MacroAutomationHarness h;
    seedTypedSlotPaste(h);

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
    const auto* target = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 1}
    );
    assert(target != nullptr);
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        target->automation.durationTicks
    ) == 4.0f);
    std::cout
        << "[PASS] test_navigation_cancels_completed_guard_before_release_without_mutation\n";
}

void test_length_row_resizes_automation_duration_without_scaling_points() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.openAutomationEditor();

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 1);
    h.turn(Config::EncoderID::OPT, 2.0f / 63.0f);

    const auto* slot = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot != nullptr);
    assert(slot->automation.active);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.durationTicks) == 3.0f);

    h.turn(Config::EncoderID::OPT, 1.0f);

    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.durationTicks) == 64.0f);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.sourceDurationTicks) == 2.0f);
    assert(slot->automation.pointCount == 3);

    core::state::macro::MacroCurvePoint point{};
    assert(core::state::macro::macroAutomationReadPoint(
        slot->automation,
        h.state.pages.automation.pointPool,
        1,
        false,
        point
    ));
    assert(point.beat == 1.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 2);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.windowOffsetTicks) == 1.0f);

    h.flushState();

    std::cout << "[PASS] test_length_row_resizes_automation_duration_without_scaling_points\n";
}

void test_left_center_enables_coarse_length_and_offset_steps_temporarily() {
    MacroAutomationHarness h;
    h.configureAutomation(0, 8.0f);
    h.openAutomationEditor();

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 1);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 64);

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 16);
    h.turn(Config::EncoderID::OPT, 1.0f / 15.0f);

    const auto* slot = core::state::macro::macroAutomationFindSlot(
        h.state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.state.pages.currentActiveTrack(),
            .page = h.state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot != nullptr);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.durationTicks) == 8.0f);

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 64);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 2);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 8);

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 2);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.windowOffsetTicks) == 4.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 8);

    h.flushState();

    std::cout << "[PASS] test_left_center_enables_coarse_length_and_offset_steps_temporarily\n";
}

}  // namespace

int main() {
    test_state_row_restores_auto_without_clearing_lane();
    test_modulation_entry_synchronizes_opt_to_visible_source_mode();
    test_modulation_tap_clear_preserves_automation_and_destination();
    test_typed_paste_preflight_rejects_invalid_payload_without_mutation();
    test_typed_paste_quick_release_keeps_copy_semantics();
    test_typed_paste_early_armed_release_cancels_without_copy_or_mutation();
    test_typed_paste_commits_once_after_full_guard();
    test_remove_slot_closes_detail_and_parent_editor_surfaces();
    test_navigation_cancels_completed_guard_before_release_without_mutation();
    test_length_row_resizes_automation_duration_without_scaling_points();
    test_left_center_enables_coarse_length_and_offset_steps_temporarily();
    std::cout << "\nAll MacroAutomationHandler tests passed.\n";
    return 0;
}
