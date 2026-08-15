#include <cassert>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include <config/InputIDs.hpp>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/ProjectTrackEditorHandler.hpp"
#include "state/CoreState.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackEditorOps.hpp"
#include "support/CoreStorages.hpp"
#include "support/InputTestHardware.hpp"

namespace {

uint32_t mockTimeMs() { return 0U; }

struct Harness {
    static constexpr oc::type::ScopeID VIEW_SCOPE = 811U;
    static constexpr oc::type::ScopeID OVERLAY_SCOPE = 812U;

    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    test_support::TestButtonHardware buttonHw;
    test_support::TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::ProjectTrackEditorHandler handler;

    Harness()
        : state(storages.settings)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(
              core::handler::ProjectTrackEditorHandler::StateRefs{
                  state.projectTrackEditor,
                  state.projectTracks,
                  state.sequencerTracks,
                  core::handler::SharedTrackDomainServices::fromCoreState(state),
                  core::state::project::ProjectTrackDomainServices::fromCoreState(state),
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              overlays,
              encoders,
              buttons,
              OVERLAY_SCOPE
          ) {
        overlays.setActiveViewProvider([]() { return VIEW_SCOPE; });
        overlays.registerCleanup(core::ui::OverlayType::SEQ_TRACK_EDIT, OVERLAY_SCOPE);
        assert(state.setSharedTrackState(0x0003U, 0U));
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

void test_open_and_modifier_track_switch_wrap_enabled_tracks() {
    Harness h;
    assert(h.handler.openActiveTrack());
    assert(h.state.projectTrackEditor.active);
    assert(h.state.projectTrackEditor.trackIndex == 0U);
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_TRACK_EDIT);

    h.press(Config::ButtonID::LEFT_CENTER);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sharedTrackActive.get() == 1U);
    assert(h.state.projectTrackEditor.trackIndex == 1U);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sharedTrackActive.get() == 0U);
    assert(h.state.projectTrackEditor.trackIndex == 0U);
    h.release(Config::ButtonID::LEFT_CENTER);

    h.press(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_TOP);
    assert(!h.state.projectTrackEditor.active);
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
}

void test_property_selection_opt_edit_and_history_coalescing() {
    Harness h;
    assert(h.handler.openActiveTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectTrackEditor.selectedProperty ==
           core::state::project::ProjectTrackEditorProperty::DELAY);

    h.turn(Config::EncoderID::OPT, 1.0f);
    h.turn(Config::EncoderID::OPT, 0.75f);
    assert(core::state::project::projectTrackDelayMs(h.state.projectTracks, 0U) == 50);
    assert(h.state.projectTrackHistory.undoCount() == 0U);

    // Moving to another property closes one contiguous OPT gesture.
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectTrackHistory.undoCount() == 1U);
    assert(h.state.undoProjectHistory());
    assert(core::state::project::projectTrackDelayMs(h.state.projectTracks, 0U) == 0);
    assert(h.state.redoProjectHistory());
    assert(core::state::project::projectTrackDelayMs(h.state.projectTracks, 0U) == 50);
}

void test_bottom_mute_and_solo_are_global_history_commands() {
    Harness h;
    assert(h.handler.openActiveTrack());

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(core::state::project::projectTrackMuted(h.state.projectTracks, 0U));
    assert(h.state.projectTrackHistory.undoCount() == 1U);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(core::state::project::projectTrackSoloed(h.state.projectTracks, 0U));
    assert(h.state.projectTrackHistory.undoCount() == 2U);

    assert(h.state.undoProjectHistory());
    assert(!core::state::project::projectTrackSoloed(h.state.projectTracks, 0U));
    assert(core::state::project::projectTrackMuted(h.state.projectTracks, 0U));
}

void test_track_type_conversion_is_destructive_but_fully_undoable() {
    Harness h;
    assert(h.handler.openActiveTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectTrackEditor.selectedProperty ==
           core::state::project::ProjectTrackEditorProperty::TYPE);

    h.turn(Config::EncoderID::OPT, 1.0f);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencerTracks.isDrumTrack(0U));
    assert(h.state.sequencerHistory.undoCount() == 1U);

    assert(h.state.undoProjectHistory());
    assert(!h.state.sequencerTracks.isDrumTrack(0U));
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencerTracks.isDrumTrack(0U));
}

void test_dirty_type_draft_blocks_track_retarget_until_resolved() {
    Harness h;
    assert(h.handler.openActiveTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectTrackEditor.selectedProperty ==
           core::state::project::ProjectTrackEditorProperty::TYPE);
    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(core::state::project::projectTrackEditorKindDraftDirty(
        h.state.projectTrackEditor
    ));

    h.press(Config::ButtonID::LEFT_CENTER);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sharedTrackActive.get() == 0U);
    assert(h.state.projectTrackEditor.trackIndex == 0U);
    assert(h.state.projectTrackEditor.draftKind ==
           core::state::project::ProjectTrackEditorKind::DRUM);
    assert(!h.state.sequencerTracks.isDrumTrack(0U));

    // The left contextual action is Cancel while Type is dirty; it must not
    // toggle Mute or publish the destructive conversion.
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(!core::state::project::projectTrackEditorKindDraftDirty(
        h.state.projectTrackEditor
    ));
    assert(!core::state::project::projectTrackMuted(h.state.projectTracks, 0U));
    assert(!h.state.sequencerTracks.isDrumTrack(0U));

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(core::state::project::projectTrackEditorKindDraftDirty(
        h.state.projectTrackEditor
    ));
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencerTracks.isDrumTrack(0U));
    assert(!core::state::project::projectTrackEditorKindDraftDirty(
        h.state.projectTrackEditor
    ));

    h.press(Config::ButtonID::LEFT_CENTER);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sharedTrackActive.get() == 1U);
    assert(h.state.projectTrackEditor.trackIndex == 1U);
}

}  // namespace

int main() {
    test_open_and_modifier_track_switch_wrap_enabled_tracks();
    test_property_selection_opt_edit_and_history_coalescing();
    test_bottom_mute_and_solo_are_global_history_commands();
    test_track_type_conversion_is_destructive_but_fully_undoable();
    test_dirty_type_draft_blocks_track_retarget_until_resolved();
    std::cout << "All ProjectTrackEditorHandler tests passed.\n";
    return 0;
}
