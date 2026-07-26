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
                      state.overlays,
                      state.activeView,
                      state.projectNavigation,
                      state.macroEdit,
                      state.pages,
                      state.projectTracks,
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
        state.macroEdit.openModulation(
            services.modulationStoredFor(macroIndex) ? 1U : 0U
        );
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
        test_support::project_control::ModulationShape shape;
        shape.durationBeats = 2.0f;
        assert(test_support::project_control::appendModulationPoint(
            shape, 0.0f, 0.25f
        ));
        assert(test_support::project_control::appendModulationPoint(
            shape, 1.0f, -0.25f
        ));
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

void test_assignment_tap_opens_exact_source_workspace() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    h.configureModulation();
    h.openModulationEditor();

    const auto& graph = h.state.pages.control.authored.modulation;
    assert(graph.sourceCount == 1U);
    assert(graph.outputBindingCount == 1U);
    const auto sourceId = graph.sources[0].id;
    const auto bindingId = graph.outputBindings[0].id;

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(h.state.activeView.get() == core::ui::ViewType::MODULATORS);
    assert(!h.state.overlays.hasVisible());
    assert(h.state.projectNavigation.currentNode.get() ==
           core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL);
    assert(h.state.projectNavigation.selectedModulator == sourceId);
    assert(h.state.projectNavigation.modulatorReturn.active());
    assert(h.state.projectNavigation.modulatorReturn.sourceId == sourceId);
    assert(h.state.projectNavigation.modulatorReturn.bindingId == bindingId);
    assert(h.state.projectNavigation.modulatorReturn.focusedRow == 1U);
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATION);

    std::cout << "[PASS] Assignment tap opens its exact source workspace\n";
}

void test_macro_view_return_resynchronizes_focused_depth_encoder() {
    MacroAutomationHarness h;
    h.configureModulation();
    h.openModulationEditor();
    h.handler.update(0U);

    const auto opt = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
    assert(std::fabs(h.encoderHw.getPosition(opt) - 0.75f) < 0.0001f);
    h.state.activeView.set(core::ui::ViewType::PROJECT);
    h.handler.update(10U);
    h.encoderHw.setPosition(opt, 0.0f);

    h.state.activeView.set(core::ui::ViewType::MACRO);
    h.handler.update(20U);

    assert(h.encoderHw.getDiscreteSteps(opt) == 0U);
    assert(std::fabs(h.encoderHw.getPosition(opt) - 0.75f) < 0.0001f);
    std::cout << "[PASS] Macro return resynchronizes focused Depth before input\n";
}

void test_unstacked_modulation_back_materializes_macro_editor_parent() {
    MacroAutomationHarness h;
    h.configureModulation();
    h.openModulationEditor();
    assert(h.state.overlays.current() ==
           core::ui::OverlayType::MACRO_AUTOMATION);

    h.press(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_TOP);

    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::EDIT);
    assert(h.state.overlays.current() == core::ui::OverlayType::MACRO_EDIT);
    std::cout << "[PASS] Unstacked detail Back materializes Macro Edit parent\n";
}

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
    assert(preserved.automation.stored());
    assert(!preserved.automation.enabled);

    h.turn(Config::EncoderID::OPT, 1.0f);
    preserved = test_support::project_control::readSlot(
        h.state.pages.control,
        preserved.address
    );
    assert(preserved.automation.enabled);
    assert(preserved.automation.pointCount == 3);

    h.flushState();

    std::cout
        << "[PASS] test_playback_row_toggles_automation_without_clearing_curve\n";
}

void test_modulation_entry_synchronizes_opt_to_focused_depth() {
    MacroAutomationHarness h;
    h.configureAutomation();
    h.configureModulation();
    assert(h.services.automationPlaybackActiveFor(0));
    assert(h.services.modulationPlaybackActiveFor(0));
    h.openModulationEditor();

    h.handler.update(0);

    const auto opt = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
    assert(h.encoderHw.getDiscreteSteps(opt) == 0U);
    assert(std::fabs(h.encoderHw.getPosition(opt) - 0.75f) < 0.0001f);

    std::cout
        << "[PASS] "
        << "test_modulation_entry_synchronizes_opt_to_focused_depth\n";
}

void test_recorded_shape_assignment_depth_uses_typed_full_range() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
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
    bindingDraft.amountQ15 = 16384;
    const auto assigned = addProjectModulationBinding(graph, bindingDraft);
    assert(assigned.changed());
    control.markAuthoredMutation();
    assert(h.services.focusModulationBinding(0U, assigned.bindingId));

    h.openModulationEditor();
    h.handler.update(0U);
    const auto opt = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
    assert(h.encoderHw.getDiscreteSteps(opt) == 0U);
    assert(std::fabs(h.encoderHw.getPosition(opt) - 0.75f) < 0.001f);

    h.turn(Config::EncoderID::OPT, 0.625f);
    const auto* binding = findProjectModulationBinding(
        graph,
        assigned.bindingId
    );
    assert(binding != nullptr);
    assert(binding->amountQ15 == 8192);
    h.flushState();

    std::cout << "[PASS] Recorded Shape assignment Depth uses ±200% UI\n";
}

void test_all_row_edits_global_depth_without_rewriting_assignment() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    h.configureModulation();
    h.openModulationEditor();
    h.handler.update(0U);
    const auto destination = projectControlDestination({0U, 0U, 0U});
    const auto bindingBefore =
        h.state.pages.control.authored.modulation.outputBindings[0];

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.macroEdit.modulationFocusedRow.get() == 0U);
    const auto opt = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
    assert(h.encoderHw.getDiscreteSteps(opt) == 201U);
    assert(std::fabs(h.encoderHw.getPosition(opt) - 0.5f) < 0.0001f);

    h.turn(Config::EncoderID::OPT, 0.75f);
    h.turn(Config::EncoderID::OPT, 0.25f);
    assert(h.state.macroHistory.undoCount() == 1U);
    assert(projectModulationDestinationScaleQ15(
        h.state.pages.control.authored.modulation,
        destination
    ) == 16384U);
    const auto* binding = findProjectModulationBinding(
        h.state.pages.control.authored.modulation,
        bindingBefore.id
    );
    assert(binding != nullptr && binding->amountQ15 == bindingBefore.amountQ15);
    assert(binding->flags == bindingBefore.flags);

    assert(h.state.undoProjectHistory());
    assert(projectModulationDestinationScaleQ15(
        h.state.pages.control.authored.modulation,
        destination
    ) == PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    binding = findProjectModulationBinding(
        h.state.pages.control.authored.modulation,
        bindingBefore.id
    );
    assert(binding != nullptr &&
           (binding->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) == 0U);
    assert(projectModulationDestinationScaleQ15(
        h.state.pages.control.authored.modulation,
        destination
    ) == PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15);
    std::cout << "[PASS] All row separates Global Depth from aggregate bypass\n";
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
    const auto* slot = h.services.controlDestination(0);
    assert(slot != nullptr);
    assert(slot->automation.stored());
    assert(!slot->automation.enabled);
    assert(slot->primaryModulation.recordedShape.stored());
    assert(slot->primaryModulation.enabled);
    // The triangular 0 -> 1 -> 0 automation is centered on 0.5 and has a
    // symmetric amplitude of 0.5. Conversion preserves that audible range.
    assert(std::fabs(slot->primaryModulation.amount - 0.5f) < 0.0001f);

    // Conversion disables Automation by default but does not make that choice
    // irreversible: reopening its stored lane and turning Playback on must
    // combine the original absolute movement with the new relative source.
    h.state.macroEdit.openAutomation();
    h.handler.update(110U);
    h.turn(Config::EncoderID::OPT, 1.0f);
    slot = h.services.controlDestination(0);
    assert(slot != nullptr);
    assert(slot->automation.enabled);
    assert(slot->primaryModulation.enabled);

    std::cout
        << "[PASS] conversion defaults Automation off and remains explicitly reversible\n";
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
    assert(slot.automation.enabled);
    assert(slot.modulationCount > 0U);
    assert(!slot.primaryModulation.enabled);
    assert(std::fabs(slot.primaryModulation.amount - 0.5f) < 0.0001f);
    assert(h.state.pages.isMacroSlotActive(0));

    h.press(Config::ButtonID::BOTTOM_LEFT);
    const uint32_t clearAt =
        100U + Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    h.setNow(clearAt);
    h.handler.update(clearAt);
    assert(h.state.macroEdit.contextFeedback.get().action ==
           core::state::contextual::ContextActionId::REMOVE);
    h.setNow(clearAt + 10U);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    slot = test_support::project_control::readSlot(
        h.state.pages.control,
        slot.address
    );
    assert(slot.automation.enabled);
    assert(slot.modulationCount == 0U);
    assert(slot.primaryModulation.amount == 0.0f);
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
    auto& source = payload.entries[0].control;
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

    const auto validValueDomain = source.automation.spec.valueDomain;
    source.automation.spec.valueDomain =
        core::state::modulation::ProjectCurveValueDomain::BIPOLAR;
    assertRejectedWithoutMutation();

    source.automation.spec.valueDomain = validValueDomain;
    source.automation.pointOffset = payload.pointPool.used;
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
        target.automation.spec.durationTicks
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
        target.automation.spec.durationTicks
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
        target.automation.spec.durationTicks
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
        target.automation.spec.durationTicks
    ) == 2.0f);
    assert(target.modulationCount > 0U);
    assert(std::fabs(target.primaryModulation.amount - 0.75f) < 0.0001f);
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
    assert(h.services.controlDestination(0) == nullptr);
    assert(!test_support::project_control::readSlot(
        h.state.pages.control,
        {h.state.pages.currentActiveTrack(), h.state.pages.currentActivePage(), 0}
    ).automation.stored());

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
        target.automation.spec.durationTicks
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
    assert(slot.automation.enabled);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.automation.spec.durationTicks) == 3.0f);

    h.turn(Config::EncoderID::OPT, 1.0f);

    slot = test_support::project_control::readSlot(h.state.pages.control, slot.address);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.automation.spec.durationTicks) == 64.0f);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.automation.spec.sourceDurationTicks) == 2.0f);
    assert(slot.automation.pointCount == 3);

    const auto point = test_support::project_control::readCurvePoint(
        h.state.pages.control,
        slot.automation.id,
        1,
        false
    );
    assert(point.beat == 1.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 3);
    h.turn(Config::EncoderID::OPT, 1.0f);
    slot = test_support::project_control::readSlot(h.state.pages.control, slot.address);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.automation.spec.windowOffsetTicks) == 1.0f);

    assert(h.state.macroHistory.undoCount() == 2U);
    assert(h.state.undoProjectHistory());
    slot = test_support::project_control::readSlot(
        h.state.pages.control,
        slot.address
    );
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        slot.automation.spec.windowOffsetTicks
    ) == 0.0f);
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        slot.automation.spec.durationTicks
    ) == 64.0f);
    assert(h.state.undoProjectHistory());
    slot = test_support::project_control::readSlot(
        h.state.pages.control,
        slot.address
    );
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        slot.automation.spec.durationTicks
    ) == 2.0f);
    assert(h.state.redoProjectHistory());
    assert(h.state.redoProjectHistory());
    slot = test_support::project_control::readSlot(
        h.state.pages.control,
        slot.address
    );
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        slot.automation.spec.durationTicks
    ) == 64.0f);
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        slot.automation.spec.windowOffsetTicks
    ) == 1.0f);

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
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.automation.spec.durationTicks) == 8.0f);

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 64);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.automationFocusedRow.get() == 3);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 8);

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 2);
    h.turn(Config::EncoderID::OPT, 1.0f);
    slot = test_support::project_control::readSlot(h.state.pages.control, slot.address);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot.automation.spec.windowOffsetTicks) == 4.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.encoderHw.getDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)) == 8);

    h.flushState();

    std::cout << "[PASS] test_left_center_enables_coarse_length_and_offset_steps_temporarily\n";
}

void test_new_lfo_selection_opens_full_project_source_workspace() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    h.state.pages.setMacroSlotActive(0, true);
    h.openModulationEditor();
    h.handler.update(0);

    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATION);
    assert(h.state.pages.control.authored.modulation.sourceCount == 0);
    assert(h.encoderHw.getDiscreteSteps(
        static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)
    ) == 1);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_CREATE);
    assert(h.state.pages.control.audition.active());
    assert(h.state.pages.control.audition.sourceCreated());
    assert(h.state.pages.control.audition.mode ==
           ProjectModulatorSourceSessionMode::AUDITION_NEW);
    assert(h.state.pages.control.authored.modulation.sourceCount == 1);
    assert(h.state.pages.control.authored.modulation.outputBindingCount == 1);
    assert(h.state.macroHistory.undoCount() == 0);
    const auto& source = h.state.pages.control.authored.modulation.sources[0];
    const auto& binding =
        h.state.pages.control.authored.modulation.outputBindings[0];
    assert(std::strcmp(source.name.data(), "LFO 1") == 0);
    assert(source.parameters.lfo.shape == ModulatorLfoShape::SINE);
    assert(source.parameters.lfo.periodTicks == PROJECT_CONTROL_TICKS_PER_BEAT);
    assert(source.parameters.lfo.retrigger == ModulatorRetriggerPolicy::TRANSPORT);
    assert(binding.amountQ15 == 8192);
    assert(binding.application == ModulationApplication::NATURAL);
    assert(h.state.activeView.get() == core::ui::ViewType::MODULATORS);
    assert(!h.state.overlays.hasVisible());
    assert(h.state.projectNavigation.currentNode.get() ==
           core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL);
    assert(h.state.projectNavigation.selectedModulator == source.id);
    assert(h.state.projectNavigation.modulatorReturn.caller ==
           core::state::project::ModulatorNavigationCaller::MACRO_AUDITION);
    assert(h.state.projectNavigation.modulatorReturn.target ==
           core::state::project::ModulatorMacroReturnTarget::MODULATOR_CREATE);
    assert(h.state.projectNavigation.modulatorReturn.sourceId == source.id);
    assert(h.state.projectNavigation.modulatorReturn.bindingId == binding.id);
    assert(h.state.projectNavigation.modulatorReturn.macroAddress.track ==
           h.state.pages.currentActiveTrack());
    assert(h.state.projectNavigation.modulatorReturn.macroAddress.page ==
           h.state.pages.currentActivePage());
    assert(h.state.projectNavigation.modulatorReturn.macroAddress.macro == 0U);
    assert(h.state.projectNavigation.modulatorReturn.focusedRow == 0U);
    std::cout
        << "[PASS] New LFO opens the full Project source workspace\n";
}

void test_new_adsr_selection_keeps_create_origin_and_exact_return_ids() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    h.state.pages.setMacroSlotActive(0, true);
    h.openModulationEditor();
    h.handler.update(0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.modulationFocusedRow.get() == 1U);
    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    const auto& graph = h.state.pages.control.authored.modulation;
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_CREATE);
    assert(h.state.pages.control.audition.mode ==
           ProjectModulatorSourceSessionMode::AUDITION_NEW);
    assert(graph.sourceCount == 1U);
    assert(graph.outputBindingCount == 1U);
    const auto& source = graph.sources[0];
    const auto& binding = graph.outputBindings[0];
    assert(source.kind == ModulatorKind::ADSR);
    assert(h.state.pages.control.audition.sourceId == source.id);
    assert(h.state.pages.control.audition.bindingId == binding.id);
    assert(h.state.macroHistory.undoCount() == 0U);

    assert(h.state.activeView.get() == core::ui::ViewType::MODULATORS);
    assert(!h.state.overlays.hasVisible());
    assert(h.state.projectNavigation.currentNode.get() ==
           core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL);
    assert(h.state.projectNavigation.selectedModulator == source.id);
    const auto& returnContext = h.state.projectNavigation.modulatorReturn;
    assert(returnContext.caller ==
           core::state::project::ModulatorNavigationCaller::MACRO_AUDITION);
    assert(returnContext.target ==
           core::state::project::ModulatorMacroReturnTarget::MODULATOR_CREATE);
    assert(returnContext.sourceId == source.id);
    assert(returnContext.bindingId == binding.id);
    assert(returnContext.focusedRow == 1U);
    std::cout
        << "[PASS] New ADSR keeps Create origin and exact Project return IDs\n";
}

core::state::modulation::ModulatorId createReusableLfo(
    MacroAutomationHarness& h
) {
    using namespace core::state::modulation;
    ModulatorLfoDraft draft{};
    draft.name = "Shared LFO";
    draft.parameters.periodTicks = PROJECT_CONTROL_TICKS_PER_BEAT;
    draft.parameters.shape = ModulatorLfoShape::TRIANGLE;
    draft.parameters.retrigger = ModulatorRetriggerPolicy::TRANSPORT;
    draft.parameters.timing = ModulatorTimingMode::SYNC;
    const auto created = createLfoModulator(
        h.state.pages.control.authored.modulation,
        draft
    );
    assert(created.changed());
    h.state.pages.control.markAuthoredMutation();
    return created.sourceId;
}

core::state::modulation::ModulationBindingId bindReusableLfo(
    MacroAutomationHarness& h,
    core::state::modulation::ModulatorId sourceId,
    uint8_t macroIndex,
    int16_t amountQ15
) {
    using namespace core::state::modulation;
    h.state.pages.setMacroSlotActive(macroIndex, true);
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = h.state.pages.currentActiveTrack(),
        .page = h.state.pages.currentActivePage(),
        .macro = macroIndex,
    };
    ModulationBindingDraft draft{};
    draft.sourceId = sourceId;
    draft.destination = projectControlDestination(address);
    draft.amountQ15 = amountQ15;
    draft.application = ModulationApplication::AROUND_BASE;
    const auto result = addProjectModulationBinding(
        h.state.pages.control.authored.modulation,
        draft
    );
    assert(result.changed());
    h.state.pages.control.markAuthoredMutation();
    return result.bindingId;
}

void test_stale_assignment_row_cannot_remove_the_replacement_edge() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    const auto firstSource = createReusableLfo(h);
    const auto secondSource = createReusableLfo(h);
    const auto armedBinding = bindReusableLfo(h, firstSource, 0, 8192);
    const auto replacementBinding = bindReusableLfo(h, secondSource, 0, 4096);

    h.openModulationEditor(0);
    h.handler.update(0U);
    assert(h.state.macroEdit.modulationFocusedRow.get() == 1U);
    assert(h.services.focusedModulationBinding(0) == armedBinding);

    h.setNow(0U);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.handler.update(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);

    // Simulate the projected edge disappearing before release. Dense storage
    // moves the second edge into the same UI ordinal; the armed action must
    // retain the original BindingId and fail closed, never retarget by row.
    auto& graph = h.state.pages.control.authored.modulation;
    assert(removeProjectModulationBinding(graph, armedBinding).changed());
    h.state.pages.control.markAuthoredMutation();
    assert(graph.outputBindingCount == 1U);
    assert(graph.outputBindings[0].id == replacementBinding);

    h.setNow(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 10U);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(graph.outputBindingCount == 1U);
    assert(findProjectModulationBinding(graph, replacementBinding) != nullptr);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::FAILED);

    h.flushState();
    std::cout
        << "[PASS] stale assignment row cannot remove its replacement edge\n";
}

void test_stale_all_row_cannot_clear_replacement_assignments() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    const auto firstSource = createReusableLfo(h);
    const auto replacementSource = createReusableLfo(h);
    const auto armedBinding = bindReusableLfo(h, firstSource, 0, 8192);

    h.openModulationEditor(0);
    h.handler.update(0U);
    h.state.macroEdit.modulationFocusedRow.set(0U);
    h.setNow(0U);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.handler.update(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);
    assert(h.state.macroEdit.contextFeedback.get().action ==
           core::state::contextual::ContextActionId::CLEAR);

    auto& graph = h.state.pages.control.authored.modulation;
    assert(removeProjectModulationBinding(graph, armedBinding).changed());
    h.state.pages.control.markAuthoredMutation();
    const auto replacementBinding =
        bindReusableLfo(h, replacementSource, 0, 4096);
    assert(graph.outputBindingCount == 1U);

    h.setNow(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 10U);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(graph.outputBindingCount == 1U);
    assert(findProjectModulationBinding(graph, replacementBinding) != nullptr);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::FAILED);

    h.flushState();
    std::cout
        << "[PASS] stale All row cannot clear replacement assignments\n";
}

void test_stale_assignment_row_cannot_paste_over_replacement_edge() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    const auto sourceId = createReusableLfo(h);
    (void)bindReusableLfo(h, sourceId, 0, 16384);
    const auto armedTarget = bindReusableLfo(h, sourceId, 1, -8192);

    h.openModulationEditor(0);
    h.handler.update(0U);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.setNow(100U);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasMacroModulationAssignment());

    h.openModulationEditor(1);
    h.handler.update(0U);
    h.setNow(0U);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.macroEdit.contextGuard.get().phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);
    assert(h.state.macroEdit.contextFeedback.get().action ==
           core::state::contextual::ContextActionId::OVERWRITE);

    auto& graph = h.state.pages.control.authored.modulation;
    assert(removeProjectModulationBinding(graph, armedTarget).changed());
    h.state.pages.control.markAuthoredMutation();
    const auto replacementTarget = bindReusableLfo(h, sourceId, 1, -4096);

    h.setNow(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 10U);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto* replacement = findProjectModulationBinding(
        graph,
        replacementTarget
    );
    assert(replacement != nullptr);
    assert(replacement->amountQ15 == -4096);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::FAILED);

    h.flushState();
    std::cout
        << "[PASS] stale assignment row cannot Paste over replacement edge\n";
}

void test_add_source_create_focus_reaches_use_existing_without_picker_mutation() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    const auto firstSource = createReusableLfo(h);
    const auto secondSource = createReusableLfo(h);
    const auto existingBinding = bindReusableLfo(h, firstSource, 0, 8192);

    h.openModulationEditor();
    h.handler.update(0);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.modulationFocusedRow.get() == 2U);
    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_CREATE);
    assert(h.state.macroEdit.modulationFocusedRow.get() == 0U);
    assert(h.encoderHw.getDiscreteSteps(
        static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)
    ) == 1U);

    const int pickerSelection =
        h.state.macroEdit.modulatorPickerIndex.get();
    const uint32_t authoredRevision = h.state.pages.control.authoredRevision;
    const uint16_t sourceCount =
        h.state.pages.control.authored.modulation.sourceCount;
    const uint16_t bindingCount =
        h.state.pages.control.authored.modulation.outputBindingCount;

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_CREATE);
    assert(h.state.macroEdit.modulationFocusedRow.get() == 2U);
    assert(h.encoderHw.getDiscreteSteps(
        static_cast<oc::type::EncoderID>(Config::EncoderID::OPT)
    ) == 1U);
    assert(h.state.macroEdit.modulatorPickerIndex.get() ==
           pickerSelection);
    assert(h.state.pages.control.authoredRevision == authoredRevision);
    assert(h.state.pages.control.authored.modulation.sourceCount == sourceCount);
    assert(h.state.pages.control.authored.modulation.outputBindingCount ==
           bindingCount);
    assert(h.state.pages.control.authored.modulation.sources[0].id == firstSource);
    assert(h.state.pages.control.authored.modulation.sources[1].id == secondSource);
    assert(h.state.pages.control.authored.modulation.outputBindings[0].id ==
           existingBinding);
    assert(h.state.macroHistory.undoCount() == 0U);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_PICKER);
    assert(h.state.macroEdit.modulatorPickerIndex.get() ==
           pickerSelection);
    assert(h.state.pages.control.authoredRevision == authoredRevision);
    assert(h.state.macroHistory.undoCount() == 0U);
    std::cout
        << "[PASS] Add Source focus reaches Use Existing without mutation\n";
}

void test_use_existing_opens_common_project_source_with_typed_return() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    h.state.pages.setMacroSlotActive(0, true);
    const auto sourceId = createReusableLfo(h);
    const auto root = h.state.pages.control.authored.modulation.sources[0];
    const auto runtimeBefore = h.state.pages.control.runtime;
    h.openModulationEditor();
    h.handler.update(0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_PICKER);
    assert(h.state.pages.control.authored.modulation.outputBindingCount == 0U);
    assert(std::memcmp(
        &h.state.pages.control.runtime,
        &runtimeBefore,
        sizeof(runtimeBefore)
    ) == 0);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_PICKER);
    assert(h.state.pages.control.audition.active());
    assert(!h.state.pages.control.audition.sourceCreated());
    assert(h.state.pages.control.audition.existingSource());
    assert(h.state.pages.control.audition.mode ==
           ProjectModulatorSourceSessionMode::AUDITION_EXISTING);
    assert(h.state.pages.control.audition.sourceId == sourceId);
    assert(h.state.pages.control.authored.modulation.sourceCount == 1U);
    assert(h.state.pages.control.authored.modulation.outputBindingCount == 1U);
    assert(h.state.macroHistory.undoCount() == 0U);
    const auto& binding =
        h.state.pages.control.authored.modulation.outputBindings[0];
    assert(binding.id == h.state.pages.control.audition.bindingId);
    assert(binding.sourceId == sourceId);
    assert(std::memcmp(
        &h.state.pages.control.authored.modulation.sources[0],
        &root,
        sizeof(root)
    ) == 0);
    assert(h.state.activeView.get() == core::ui::ViewType::MODULATORS);
    assert(!h.state.overlays.hasVisible());
    assert(h.state.projectNavigation.currentNode.get() ==
           core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL);
    assert(h.state.projectNavigation.selectedModulator == sourceId);
    const auto& returnContext = h.state.projectNavigation.modulatorReturn;
    assert(returnContext.caller ==
           core::state::project::ModulatorNavigationCaller::MACRO_AUDITION);
    assert(returnContext.target ==
           core::state::project::ModulatorMacroReturnTarget::MODULATOR_PICKER);
    assert(h.state.projectNavigation.focusedRow.get() == 2U);
    assert(returnContext.sourceId == sourceId);
    assert(returnContext.bindingId == binding.id);
    assert(returnContext.macroAddress.track ==
           h.state.pages.currentActiveTrack());
    assert(returnContext.macroAddress.page ==
           h.state.pages.currentActivePage());
    assert(returnContext.macroAddress.macro == 0U);
    std::cout
        << "[PASS] Use Existing opens common Project Source with typed return\n";
}

void test_assignment_copy_pastes_shared_source_to_empty_macro_with_one_undo() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    const auto sourceId = createReusableLfo(h);
    const auto originalBinding = bindReusableLfo(h, sourceId, 0, -12288);
    h.state.pages.setMacroSlotActive(1, true);

    h.openModulationEditor(0);
    h.handler.update(0);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.setNow(100);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasMacroModulationAssignment());
    assert(h.state.structureClipboard.macroModulationAssignment->sourceId ==
           sourceId);
    assert(h.state.structureClipboard.macroModulationAssignment->binding.id ==
           originalBinding);
    assert(h.state.macroHistory.undoCount() == 0U);

    h.openModulationEditor(1);
    h.handler.update(0);
    h.setNow(0);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.setNow(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 10U);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto target = projectControlDestination({
        .track = h.state.pages.currentActiveTrack(),
        .page = h.state.pages.currentActivePage(),
        .macro = 1,
    });
    const auto& graph = h.state.pages.control.authored.modulation;
    assert(graph.sourceCount == 1U);
    assert(graph.outputBindingCount == 2U);
    const auto& pasted = graph.outputBindings[1];
    assert(pasted.id != originalBinding);
    assert(pasted.sourceId == sourceId);
    assert(pasted.destination == target);
    assert(pasted.amountQ15 == -12288);
    assert(h.services.focusedModulationBinding(1) == pasted.id);
    assert(h.state.macroHistory.undoCount() == 1U);
    assert(h.state.macroEdit.contextFeedback.get().status ==
           core::state::contextual::OperationFeedbackStatus::APPLIED);

    assert(h.state.macroHistory.undo(h.state.pages));
    assert(h.state.pages.control.authored.modulation.sourceCount == 1U);
    assert(h.state.pages.control.authored.modulation.outputBindingCount == 1U);
    assert(h.state.pages.control.authored.modulation.outputBindings[0].id ==
           originalBinding);
    std::cout
        << "[PASS] typed assignment Paste reuses source and creates one Undo\n";
}

void test_assignment_paste_overwrites_only_matching_edge_with_stable_id() {
    using namespace core::state::modulation;
    MacroAutomationHarness h;
    const auto sourceId = createReusableLfo(h);
    const auto sourceBinding = bindReusableLfo(h, sourceId, 0, 16384);
    const auto targetBinding = bindReusableLfo(h, sourceId, 1, -4096);

    h.openModulationEditor(0);
    h.handler.update(0);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.setNow(100);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasMacroModulationAssignment());
    assert(h.state.structureClipboard.macroModulationAssignment->binding.id ==
           sourceBinding);

    h.openModulationEditor(1);
    h.handler.update(0);
    const auto plan = h.services.preflightModulationPaste(1);
    assert(plan.actionable());
    assert(plan.requiresOverwrite());
    h.setNow(0);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.setNow(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS + 10U);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto* updated = findProjectModulationBinding(
        h.state.pages.control.authored.modulation,
        targetBinding
    );
    assert(updated != nullptr);
    assert(updated->id == targetBinding);
    assert(updated->amountQ15 == 16384);
    assert(h.state.pages.control.authored.modulation.outputBindingCount == 2U);
    assert(h.services.focusedModulationBinding(1) == targetBinding);
    assert(h.state.macroHistory.undoCount() == 1U);

    assert(h.state.macroHistory.undo(h.state.pages));
    const auto* restored = findProjectModulationBinding(
        h.state.pages.control.authored.modulation,
        targetBinding
    );
    assert(restored != nullptr);
    assert(restored->amountQ15 == -4096);
    std::cout
        << "[PASS] assignment overwrite keeps edge identity and is local\n";
}

}  // namespace

int main() {
    test_assignment_tap_opens_exact_source_workspace();
    test_macro_view_return_resynchronizes_focused_depth_encoder();
    test_unstacked_modulation_back_materializes_macro_editor_parent();
    test_playback_row_toggles_automation_without_clearing_curve();
    test_modulation_entry_synchronizes_opt_to_focused_depth();
    test_recorded_shape_assignment_depth_uses_typed_full_range();
    test_all_row_edits_global_depth_without_rewriting_assignment();
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
    test_new_lfo_selection_opens_full_project_source_workspace();
    test_new_adsr_selection_keeps_create_origin_and_exact_return_ids();
    test_stale_assignment_row_cannot_paste_over_replacement_edge();
    test_stale_all_row_cannot_clear_replacement_assignments();
    test_stale_assignment_row_cannot_remove_the_replacement_edge();
    test_add_source_create_focus_reaches_use_existing_without_picker_mutation();
    test_use_existing_opens_common_project_source_with_typed_return();
    test_assignment_copy_pastes_shared_source_to_empty_macro_with_one_undo();
    test_assignment_paste_overwrites_only_matching_edge_with_stable_id();
    std::cout << "\nAll MacroAutomationHandler tests passed.\n";
    return 0;
}
