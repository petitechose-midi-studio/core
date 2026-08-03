#include <cassert>
#include <cmath>
#include <cstring>
#include <config/App.hpp>
#include <config/Timing.hpp>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include <utility>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/handler/macro/MacroPerformanceDomainServices.hpp"
#include "../../src/handler/view/ViewSwitcherHandler.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerCcLanePatternOps.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

struct ViewSwitcherHarness {
    static constexpr oc::type::ScopeID MACRO_VIEW_SCOPE = 801;
    static constexpr oc::type::ScopeID SEQUENCER_VIEW_SCOPE = 802;
    static constexpr oc::type::ScopeID PROJECT_VIEW_SCOPE = 803;
    static constexpr oc::type::ScopeID DEVICE_SETTINGS_VIEW_SCOPE = 804;
    static constexpr oc::type::ScopeID VIEW_SELECTOR_SCOPE = 805;

    test_support::CoreStorages storages;
    core::state::CoreState state;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::ViewSwitcherHandler handler;

    ViewSwitcherHarness()
        : state(storages.settings)
        , inputBinding(eventBus, mockTimeMs, Config::Input::CONFIG)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::ViewSwitcherHandler::StateRefs{
                      state,
                      state.overlays,
                      state.activeView,
                      state.viewSelector,
                      state.sequencer.patternQuickControls,
                      state.sequencer.stepPropertyInlineSelector,
                      state.sequencer.ccLaneUi,
                      state.sequencer.structureUi.stepSelection,
                      state.projectNavigation,
                  },
                  overlays,
                  encoders,
                  buttons,
                   core::handler::ViewSwitcherHandler::ViewScopes{
                       MACRO_VIEW_SCOPE,
                       SEQUENCER_VIEW_SCOPE,
                       PROJECT_VIEW_SCOPE,
                       DEVICE_SETTINGS_VIEW_SCOPE,
                       PROJECT_VIEW_SCOPE,
                   },
                  VIEW_SELECTOR_SCOPE) {
        overlays.registerCleanup(core::ui::OverlayType::VIEW_SELECTOR, VIEW_SELECTOR_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::SEQUENCER_SETTINGS, VIEW_SELECTOR_SCOPE);
        overlays.setActiveViewProvider([this]() {
            switch (state.activeView.get()) {
                case core::ui::ViewType::SEQUENCER:
                    return SEQUENCER_VIEW_SCOPE;
                case core::ui::ViewType::PROJECT:
                case core::ui::ViewType::MODULATORS:
                    return PROJECT_VIEW_SCOPE;
                case core::ui::ViewType::DEVICE_SETTINGS:
                    return DEVICE_SETTINGS_VIEW_SCOPE;
                case core::ui::ViewType::MACRO:
                default:
                    return MACRO_VIEW_SCOPE;
            }
        });
        g_now_ms = 0;
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

void openSelector(ViewSwitcherHarness& h) {
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::VIEW_SELECTOR);
}

void recordMacroDestination(ViewSwitcherHarness& h, uint8_t cc) {
    const core::state::macro::MacroAutomationSlotAddress address{
        .track = 0,
        .page = 0,
        .macro = 1,
    };
    auto change = h.state.macroHistory.prepare(
        h.state.pages,
        address,
        core::state::macro::MacroHistoryActionKind::PASTE_DESTINATION
    );
    assert(change);
    auto& page = h.state.pages.pageData(0, 0);
    page.setMacroActive(address.macro, true);
    page.cc[address.macro] = cc;
    h.state.pages.updateActiveConfigs();
    assert(h.state.macroHistory.commitPrepared(
        h.state.pages,
        std::move(change)
    ));
}

void authorPendingCcLaneEvent(ViewSwitcherHarness& h) {
    namespace seq = core::state::sequencer;
    auto bank = core::app::makeExtmemUnique<seq::SequencerCcLaneBank>();
    assert(bank);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = 1U;
    draft.destination.minimum = 0U;
    draft.destination.maximum = 127U;
    draft.destination.routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    draft.initialValue = 64U;
    assert(seq::createSequencerCcLane(*bank, 0U, draft).changed());
    seq::installSequencerCcLaneBank(h.state.sequencer.pattern, std::move(bank));
    assert(h.state.clearProjectHistory());

    assert(seq::cloneSequencerCcLaneBank(
        bank,
        seq::sequencerCcLaneView(h.state.sequencer.pattern)
    ));
    assert(seq::setSequencerCcLaneEvent(*bank, 0U, 0U, 64U).changed());
    assert(seq::sequencerHistoryOpenAccepted(
        h.state.beginOrContinueSequencerCcLaneEventHistoryCoalescing(
        0U,
        0U,
        -1,
        64,
        bank.get(),
        100U)));
    seq::installSequencerCcLaneBank(h.state.sequencer.pattern, std::move(bank));
}

void test_view_selector_opens_navigates_and_confirms_on_close() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);

    openSelector(h);
    assert(h.state.viewSelector.selectedIndex.get() == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 1);
    assert(h.state.activeView.get() == core::ui::ViewType::MACRO);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.activeView.get() == core::ui::ViewType::SEQUENCER);

    std::cout << "[PASS] test_view_selector_opens_navigates_and_confirms_on_close\n";
}

void test_view_selector_refuses_to_hide_an_active_step_draft() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::SEQUENCER);
    assert(core::state::sequencer::beginStepContentDraft(
        h.state.sequencer,
        core::state::sequencer::SequencerStepContentDraftKind::MICRO_SEQUENCE,
        0
    ));

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.activeView.get() == core::ui::ViewType::SEQUENCER);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepContentDraft.failure ==
           core::state::sequencer::
               SequencerStepContentDraftFailure::TRANSITION_BLOCKED);
    assert(h.state.sequencer.stepContentDraft.blockedTransition ==
           core::state::sequencer::
               SequencerStepContentDraftBlockedTransition::VIEW);
    std::cout
        << "[PASS] test_view_selector_refuses_to_hide_an_active_step_draft\n";
}

void test_open_selector_cannot_commit_a_view_change_after_draft_begins() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::SEQUENCER);
    openSelector(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(core::state::sequencer::beginStepContentDraft(
        h.state.sequencer,
        core::state::sequencer::SequencerStepContentDraftKind::MICRO_SEQUENCE,
        0
    ));

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.activeView.get() == core::ui::ViewType::SEQUENCER);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepContentDraft.blockedTransition ==
           core::state::sequencer::
               SequencerStepContentDraftBlockedTransition::VIEW);

    std::cout
        << "[PASS] an already-open selector cannot commit across a Step draft\n";
}

void test_nav_release_confirms_and_closes_selector() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);

    openSelector(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);

    assert(h.state.activeView.get() == core::ui::ViewType::SEQUENCER);
    assert(!h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    std::cout << "[PASS] test_nav_release_confirms_and_closes_selector\n";
}

void test_modulators_item_routes_to_project_modulators() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);

    openSelector(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 2);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.activeView.get() == core::ui::ViewType::MODULATORS);
    assert(h.state.projectNavigation.activeTab.get() ==
           core::state::project::ProjectTab::MODULATORS);
    assert(h.state.projectNavigation.currentNode.get() ==
           core::state::project::ProjectNodeId::MODULATORS_ROOT);

    std::cout << "[PASS] Modulators is a first-rank view reusing its workspace\n";
}

void test_selector_uses_active_view_scope() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::SEQUENCER);

    openSelector(h);
    assert(h.state.viewSelector.selectedIndex.get() == 1);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 0);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.activeView.get() == core::ui::ViewType::MACRO);

    std::cout << "[PASS] test_selector_uses_active_view_scope\n";
}

void test_sequencer_short_left_top_opens_selector_at_root() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::SEQUENCER);

    h.tap(Config::ButtonID::LEFT_TOP);

    assert(h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::VIEW_SELECTOR);
    assert(h.state.activeView.get() == core::ui::ViewType::SEQUENCER);

    std::cout << "[PASS] Sequencer root short LEFT_TOP opens View Selector\n";
}

void test_sequencer_left_top_hold_navigates_and_applies_on_release() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::SEQUENCER);

    h.press(Config::ButtonID::LEFT_TOP);
    assert(h.state.viewSelector.visible.get());
    assert(h.state.viewSelector.selectedIndex.get() == 1);

    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 0);
    h.release(Config::ButtonID::LEFT_TOP);

    assert(!h.state.viewSelector.visible.get());
    assert(h.state.activeView.get() == core::ui::ViewType::MACRO);

    std::cout
        << "[PASS] Sequencer LEFT_TOP hold applies View Selector on release\n";
}

void test_selector_uses_project_active_view_scope() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::PROJECT);

    openSelector(h);
    assert(h.state.viewSelector.selectedIndex.get() == 3);

    h.turn(Config::EncoderID::NAV, -1.0f);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 1);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.activeView.get() == core::ui::ViewType::SEQUENCER);

    std::cout << "[PASS] test_selector_uses_project_active_view_scope\n";
}

void test_modulators_route_applies_while_project_view_is_already_active() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::PROJECT);
    h.state.projectNavigation.activeTab.set(
        core::state::project::ProjectTab::MUSIC
    );
    h.state.projectNavigation.currentNode.set(
        core::state::project::ProjectNodeId::MUSIC_ROOT
    );
    h.state.projectNavigation.depth.set(0U);

    openSelector(h);
    assert(h.state.viewSelector.selectedIndex.get() == 3);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 2);
    h.tap(Config::ButtonID::LEFT_TOP);

    assert(h.state.activeView.get() == core::ui::ViewType::MODULATORS);
    assert(h.state.projectNavigation.activeTab.get() ==
           core::state::project::ProjectTab::MODULATORS);
    assert(h.state.projectNavigation.currentNode.get() ==
           core::state::project::ProjectNodeId::MODULATORS_ROOT);
    assert(h.state.projectNavigation.depth.get() == 0U);

    std::cout << "[PASS] Project Settings can switch to first-rank Modulators\n";
}

void test_device_settings_item_switches_to_device_settings_view() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);

    openSelector(h);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.viewSelector.selectedIndex.get() == 4);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(!h.state.deviceSettings.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.activeView.get() == core::ui::ViewType::DEVICE_SETTINGS);

    std::cout << "[PASS] test_device_settings_item_switches_to_device_settings_view\n";
}

void test_selector_exposes_global_undo_redo_without_changing_view() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);
    recordMacroDestination(h, 74);

    char label[40]{};
    h.state.projectHistory.formatUndoLabel(label, sizeof(label));
    assert(std::strcmp(label, "Undo Paste Destination") == 0);

    openSelector(h);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::VIEW_SELECTOR);
    assert(h.state.activeView.get() == core::ui::ViewType::MACRO);
    assert(!h.state.pages.pageData(0, 0).isMacroActive(1));
    assert(h.state.projectHistory.canRedo());

    h.state.projectHistory.formatRedoLabel(label, sizeof(label));
    assert(std::strcmp(label, "Redo Paste Destination") == 0);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.viewSelector.visible.get());
    assert(h.state.pages.pageData(0, 0).isMacroActive(1));
    assert(h.state.pages.pageData(0, 0).cc[1] == 74U);
    assert(!h.state.projectHistory.canRedo());

    std::cout << "[PASS] selector exposes global Undo/Redo without changing view\n";
}

void test_selector_undo_redo_uses_real_macro_gestures_with_value_coalescing() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::MACRO);
    const auto services =
        core::handler::MacroPerformanceDomainServices::fromCoreState(h.state);
    const float initial = h.state.pages.activePageData().values[0];

    services.setManualValue(0U, 0.20f);
    services.setManualValue(0U, 0.55f);
    services.setManualValue(0U, 0.80f);
    assert(h.state.macroHistory.undoCount() == 1U);
    assert(h.state.projectHistory.canUndo());

    openSelector(h);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(std::fabs(h.state.pages.activePageData().values[0] - initial) < 0.0001f);
    assert(h.state.projectHistory.canRedo());

    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(std::fabs(h.state.pages.activePageData().values[0] - 0.80f) < 0.0001f);
    assert(!h.state.projectHistory.canRedo());

    assert(services.activateMacroSlot(5U));
    assert(h.state.pages.isMacroSlotActive(5U));
    assert(h.state.projectHistory.canUndo());
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.pages.isMacroSlotActive(5U));
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.pages.isMacroSlotActive(5U));

    std::cout << "[PASS] physical Undo/Redo restores real Macro value/create gestures\n";
}

void test_selector_physically_restores_project_settings_history() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::PROJECT);
    const auto before =
        core::state::project::captureProjectSettingsHistorySnapshot(
            h.state.statusBar,
            h.state.projectNavigation,
            h.state.midiSync
        );
    h.state.statusBar.tempo.set(167.0f);
    h.state.statusBar.tempoDisplay.set(167.0f);
    const auto after =
        core::state::project::captureProjectSettingsHistorySnapshot(
            h.state.statusBar,
            h.state.projectNavigation,
            h.state.midiSync
        );
    assert(h.state.projectSettingsHistory.record(
        before,
        after,
        core::state::project::ProjectSettingsHistoryActionKind::Tempo,
        0U,
        true
    ));

    openSelector(h);
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.statusBar.tempo.get() == 120.0f);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.statusBar.tempo.get() == 167.0f);

    std::cout << "[PASS] physical Undo/Redo restores Project settings\n";
}

void test_selector_commits_pending_step_edit_before_global_undo() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::SEQUENCER);
    const uint8_t initial = h.state.sequencer.pattern.note[0];
    assert(core::state::sequencer::sequencerHistoryOpenAccepted(
        h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        0,
        core::state::sequencer::StepProperty::NOTE,
        100U,
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly)));
    assert(h.state.sequencer.setStepNoteAt(0, 72));
    assert(h.state.sealSequencerPatternHistoryCoalescing(true));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(!h.state.projectHistory.canUndo());

    openSelector(h);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.projectHistory.canUndo());
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.viewSelector.visible.get());
    assert(h.state.sequencer.pattern.note[0] == initial);

    std::cout << "[PASS] selector commits a pending Step edit before global Undo\n";
}

void test_selector_commits_pending_cc_edit_before_global_undo() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::SEQUENCER);
    authorPendingCcLaneEvent(h);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(!h.state.projectHistory.canUndo());

    openSelector(h);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.projectHistory.canUndo());
    h.tap(Config::ButtonID::LEFT_CENTER);
    assert(h.state.viewSelector.visible.get());
    const auto* bank = core::state::sequencer::sequencerCcLaneView(
        h.state.sequencer.pattern
    );
    assert(bank != nullptr && !bank->lanes[0].activeMask.test(0U));

    h.tap(Config::ButtonID::LEFT_BOTTOM);
    bank = core::state::sequencer::sequencerCcLaneView(h.state.sequencer.pattern);
    assert(bank != nullptr && bank->lanes[0].activeMask.test(0U));
    assert(bank->lanes[0].values[0] == 64U);

    std::cout << "[PASS] selector commits a pending CC edit before global Undo\n";
}

void test_empty_global_history_keys_are_no_ops() {
    ViewSwitcherHarness h;
    openSelector(h);

    h.tap(Config::ButtonID::LEFT_CENTER);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.viewSelector.visible.get());
    assert(!h.state.projectHistory.canUndo());
    assert(!h.state.projectHistory.canRedo());

    std::cout << "[PASS] empty global history keys are no-ops\n";
}

void test_selector_does_not_open_when_overlay_or_structure_selection_is_active() {
    {
        ViewSwitcherHarness h;
        h.state.overlays.show(core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.state.sequencer.structureUi.stepSelection.active.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.state.sequencer.structureUi.pageSelection.active.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.state.trackNavigation.selection.active.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::MACRO);
        h.state.macroUi.slotSelection.active.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::MACRO);
        h.state.macroUi.pageSelection.active.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::MACRO);
        h.state.trackNavigation.selection.active.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    std::cout << "[PASS] selector stays closed during every structure selection\n";
}

void test_selector_does_not_open_while_sequencer_inline_modes_are_active() {
    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.state.sequencer.patternQuickControls.selecting.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.state.sequencer.stepPropertyInlineSelector.selecting.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.state.sequencer.stepContentSelector.selecting.set(true);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::SEQUENCER);
        h.state.sequencer.ccLaneUi.mode =
            core::state::sequencer::SequencerCcLaneUiMode::LANE_SETTINGS;
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
    }

    std::cout << "[PASS] test_selector_does_not_open_while_sequencer_inline_modes_are_active\n";
}

void test_selector_waits_until_sequencer_child_has_backed_to_root() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::SEQUENCER);
    h.state.sequencer.contentView.kind.set(
        core::state::sequencer::SequencerContentViewKind::MICRO_SEQUENCE
    );
    h.state.sequencer.contentView.stackDepth = 1;
    h.state.sequencer.contentView.sequenceId.set(1);
    h.state.sequencer.contentView.depth.set(1);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);

    h.press(Config::ButtonID::LEFT_TOP);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(h.state.activeView.get() == core::ui::ViewType::SEQUENCER);
    assert(h.state.sequencer.contentView.kind.get() ==
           core::state::sequencer::SequencerContentViewKind::MICRO_SEQUENCE);
    assert(h.state.sequencer.contentView.depth.get() == 1U);

    std::cout << "[PASS] Sequencer child owns LEFT_TOP until Back reaches root\n";
}

void test_selector_waits_until_project_folder_has_backed_to_root() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::PROJECT);
    h.state.projectNavigation.activeTab.set(core::state::project::ProjectTab::MUSIC);
    h.state.projectNavigation.currentNode.set(core::state::project::ProjectNodeId::MUSIC_SCALE);
    h.state.projectNavigation.depth.set(1);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());

    h.press(Config::ButtonID::LEFT_TOP);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(h.state.activeView.get() == core::ui::ViewType::PROJECT);
    assert(h.state.projectNavigation.currentNode.get() ==
           core::state::project::ProjectNodeId::MUSIC_SCALE);
    assert(h.state.projectNavigation.depth.get() == 1U);

    std::cout << "[PASS] Project child owns LEFT_TOP until Back reaches root\n";
}

void test_selector_does_not_open_during_project_modulator_audition() {
    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::MODULATORS);
        h.state.pages.control.audition.mode = core::state::modulation::
            ProjectModulatorSourceSessionMode::AUDITION_NEW;

        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
        assert(h.overlays.current() == core::ui::OverlayType::NONE);
    }

    {
        ViewSwitcherHarness h;
        h.state.activeView.set(core::ui::ViewType::PROJECT);
        h.state.projectNavigation.activeTab.set(core::state::project::ProjectTab::MODULATORS);
        h.state.projectNavigation.currentNode.set(
            core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL
        );
        h.state.projectNavigation.depth.set(1);
        h.state.pages.control.audition.mode = core::state::modulation::
            ProjectModulatorSourceSessionMode::AUDITION_NEW;

        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.viewSelector.visible.get());
        assert(h.overlays.current() == core::ui::OverlayType::NONE);
    }

    std::cout << "[PASS] selector cannot abandon a Project modulator audition\n";
}

void test_selector_waits_until_cc_lane_has_backed_to_root() {
    ViewSwitcherHarness h;
    h.state.activeView.set(core::ui::ViewType::SEQUENCER);
    h.state.sequencer.ccLaneUi.mode =
        core::state::sequencer::SequencerCcLaneUiMode::LANE_GRID;

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    h.press(Config::ButtonID::LEFT_TOP);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(!h.state.viewSelector.visible.get());
    assert(h.state.sequencer.ccLaneUi.mode ==
           core::state::sequencer::SequencerCcLaneUiMode::LANE_GRID);

    std::cout << "[PASS] CC Lane owns LEFT_TOP until Back reaches root\n";
}

}  // namespace

int main() {
    test_view_selector_opens_navigates_and_confirms_on_close();
    test_view_selector_refuses_to_hide_an_active_step_draft();
    test_open_selector_cannot_commit_a_view_change_after_draft_begins();
    test_nav_release_confirms_and_closes_selector();
    test_modulators_item_routes_to_project_modulators();
    test_selector_uses_active_view_scope();
    test_sequencer_short_left_top_opens_selector_at_root();
    test_sequencer_left_top_hold_navigates_and_applies_on_release();
    test_selector_uses_project_active_view_scope();
    test_modulators_route_applies_while_project_view_is_already_active();
    test_device_settings_item_switches_to_device_settings_view();
    test_selector_exposes_global_undo_redo_without_changing_view();
    test_selector_undo_redo_uses_real_macro_gestures_with_value_coalescing();
    test_selector_physically_restores_project_settings_history();
    test_selector_commits_pending_step_edit_before_global_undo();
    test_selector_commits_pending_cc_edit_before_global_undo();
    test_empty_global_history_keys_are_no_ops();
    test_selector_does_not_open_when_overlay_or_structure_selection_is_active();
    test_selector_does_not_open_while_sequencer_inline_modes_are_active();
    test_selector_waits_until_sequencer_child_has_backed_to_root();
    test_selector_waits_until_project_folder_has_backed_to_root();
    test_selector_does_not_open_during_project_modulator_audition();
    test_selector_waits_until_cc_lane_has_backed_to_root();

    std::cout << "\nAll ViewSwitcherHandler tests passed.\n";
    return 0;
}
