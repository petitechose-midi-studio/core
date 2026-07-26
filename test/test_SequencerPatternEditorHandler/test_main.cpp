#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include <config/InputIDs.hpp>

#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerPatternEditorHandler.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "support/CoreStorages.hpp"
#include "support/InputTestHardware.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

struct Harness {
    static constexpr oc::type::ScopeID VIEW_SCOPE = 701;
    static constexpr oc::type::ScopeID OVERLAY_SCOPE = 702;

    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    test_support::TestButtonHardware buttonHw;
    test_support::TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::state::sequencer::SequencerPatternRandomizeSession randomize;
    core::handler::SequencerPatternEditorHandler handler;

    Harness()
        : state(storages.settings,
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(
              core::handler::SequencerPatternEditorHandler::StateRefs{
                  state.sequencer,
                  state.sequencerTracks,
                  randomize,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              overlays,
              encoders,
              buttons,
              VIEW_SCOPE,
              OVERLAY_SCOPE
          ) {
        g_now_ms = 0;
        overlays.setActiveViewProvider([]() { return VIEW_SCOPE; });
        overlays.registerCleanup(
            core::ui::OverlayType::SEQ_PATTERN_EDIT,
            OVERLAY_SCOPE
        );
    }

    void press(Config::ButtonID id) {
        const auto button = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(button, true);
        eventBus.emit(oc::core::event::ButtonPressEvent(button, true));
    }

    void release(Config::ButtonID id) {
        const auto button = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(button, false);
        eventBus.emit(oc::core::event::ButtonReleaseEvent(button));
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoder = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoder, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoder, value));
    }
};

void enableFirstSteps(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t count
) {
    oc::note::sequencer::StepBitMask128 mask{};
    for (uint8_t step = 0; step < count; ++step) mask.setBit(step);
    sequencer.pattern.enabledMask.set(mask);
}

void focusRandomize(Harness& h) {
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.randomize.active);
}

void test_retained_navigation_uses_modifier_grammar_without_history() {
    Harness h;
    assert(h.state.sequencer.pattern.setContentLength(24));
    assert(h.handler.openFromCurrentPage());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_PATTERN_EDIT);
    assert(h.overlays.currentScope() == Harness::OVERLAY_SCOPE);

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencer.patternEditor.navigationMode ==
           core::state::sequencer::SequencerPatternEditorNavigationMode::WINDOWS);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternEditor.windowStart == 16);
    h.release(Config::ButtonID::LEFT_CENTER);

    h.press(Config::ButtonID::LEFT_BOTTOM);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.sequencer.patternEditor.focusedLayer ==
           core::state::sequencer::SequencerPatternEditorLayer::REGION);
    h.release(Config::ButtonID::LEFT_BOTTOM);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.patternEditor.focusedField ==
           core::state::sequencer::SequencerPatternEditorField::LOOP_END);
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.press(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.patternEditor.active.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
}

void test_opt_edits_coalesce_per_field_and_restore_exact_region() {
    Harness h;
    assert(h.state.sequencer.pattern.setContentLength(16));
    assert(h.handler.openFromCurrentPage());
    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.pattern.loopEnd == 16);

    h.turn(Config::EncoderID::OPT, 11.0f / 127.0f);
    h.turn(Config::EncoderID::OPT, 7.0f / 127.0f);
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.pattern.loopEnd == 8);
    assert(h.state.sequencerHistory.undoCount() == 0);

    // Changing field closes exactly one contiguous OPT gesture.
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerHistory.undoCount() == 1);
    h.handler.close();

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.pattern.playStart == 0);
    assert(h.state.sequencer.pattern.loopStart == 0);
    assert(h.state.sequencer.pattern.loopEnd == 16);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.pattern.loopEnd == 8);
}

void test_external_track_switch_commits_old_owner_then_closes() {
    Harness h;
    assert(h.state.sequencer.pattern.setContentLength(16));
    assert(h.handler.openFromCurrentPage());
    h.turn(Config::EncoderID::OPT, 7.0f / 127.0f);
    assert(h.state.sequencer.pattern.length.get() == 8);
    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0);

    assert(core::state::sequencer::switchActiveTrack(
        h.state.sequencerTracks,
        h.state.sequencer,
        1
    ));
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencerTracks.track(0).length.get() == 8);
    h.handler.update(++g_now_ms);

    assert(!h.state.sequencer.patternEditor.active.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.track(0).length.get() == 16);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
}

void test_randomize_preview_reroll_and_cancel_never_publish() {
    Harness h;
    assert(h.state.sequencer.pattern.setContentLength(16));
    enableFirstSteps(h.state.sequencer, 16);
    const auto before = h.state.sequencer.pattern.note;
    assert(h.handler.openFromCurrentPage());
    focusRandomize(h);
    assert(h.randomize.summary.changedCount > 0U);
    assert(h.state.sequencer.pattern.note == before);

    const uint32_t seed = h.randomize.draft.seed;
    const auto firstPreview = h.randomize.preview.note;
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.randomize.draft.seed != seed);
    assert(h.randomize.preview.note != firstPreview);
    assert(h.state.sequencer.pattern.note == before);

    h.press(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(!h.randomize.active);
    assert(h.state.sequencer.patternEditor.active.get());
    assert(h.state.sequencer.pattern.note == before);
    assert(h.state.sequencerHistory.undoCount() == 0U);
}

void test_randomize_apply_is_one_exact_flat_history_entry() {
    Harness h;
    assert(h.state.sequencer.pattern.setContentLength(16));
    enableFirstSteps(h.state.sequencer, 16);
    const auto before = h.state.sequencer.pattern.note;
    assert(h.handler.openFromCurrentPage());
    focusRandomize(h);
    const auto expected = h.randomize.preview.note;
    assert(expected != before);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(!h.randomize.active);
    assert(h.state.sequencer.patternEditor.active.get());
    assert(h.state.sequencer.pattern.note == expected);
    assert(h.state.sequencerTracks.track(0).note == expected);
    assert(h.state.sequencerHistory.undoCount() == 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note == before);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.note == expected);
}

void test_add_page_extends_to_next_window_and_is_one_undoable_action() {
    Harness h;
    assert(h.state.sequencer.pattern.setContentLength(16));
    assert(h.handler.openFromCurrentPage());

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.pattern.length.get() == 24U);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 16U);
}

}  // namespace

int main() {
    test_retained_navigation_uses_modifier_grammar_without_history();
    test_opt_edits_coalesce_per_field_and_restore_exact_region();
    test_external_track_switch_commits_old_owner_then_closes();
    test_randomize_preview_reroll_and_cancel_never_publish();
    test_randomize_apply_is_one_exact_flat_history_entry();
    test_add_page_extends_to_next_window_and_is_one_undoable_action();
    std::cout << "All SequencerPatternEditorHandler tests passed.\n";
    return 0;
}
