#include "handler/sequencer/SequencerStepContentHandler.hpp"

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::handler {
namespace interaction_policy = core::handler::sequencer::interaction_policy;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;
using Action = core::state::sequencer::SequencerStepContentAction;

namespace {

inline oc::type::IsActiveFn canOpen(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus
) {
    return [&overlays, &sequencer, &trackUi, &navigationFocus]() {
        const auto policy = interaction_policy::build(
            sequencer,
            trackUi,
            navigationFocus.get(),
            overlays.hasVisible()
        );
        return policy.leftBottomPress ==
            core::state::sequencer::SequencerInteractionAction::OPEN_STEP_CONTENT_SELECTOR;
    };
}

inline oc::type::IsActiveFn selecting(
    core::state::sequencer::SequencerState& sequencer
) {
    return [&sequencer]() { return sequencer.stepContentSelector.selecting.get(); };
}

FLASHMEM uint8_t rowForAction(
    Action action,
    core::state::sequencer::StepProperty activeProperty
) {
    switch (action) {
        case Action::CHORD:
            return step_edit_rows::CHORD;
        case Action::MICRO_SEQUENCE:
            return step_edit_rows::MICRO_SEQUENCE;
        case Action::CYCLE_STATES:
            return step_edit_rows::CYCLE_STATES;
        case Action::VARIATION:
        default:
            return static_cast<uint8_t>(
                step_edit_rows::PROPERTY_OFFSET + static_cast<uint8_t>(activeProperty)
            );
    }
}

}  // namespace

FLASHMEM SequencerStepContentHandler::SequencerStepContentHandler(
    StateRefs state,
    SequencerStepHandler& stepHandler,
    SequencerStepEditHandler& stepEditHandler,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId
)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , track_ui_(state.trackNavigation)
    , navigation_focus_(state.navigationFocus)
    , step_handler_(stepHandler)
    , step_edit_handler_(stepEditHandler)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void SequencerStepContentHandler::setupBindings() {
    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope_id_)
        .when(canOpen(overlays_, sequencer_, track_ui_, navigation_focus_))
        .then([this]() { open(); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(selecting(sequencer_))
        .then([this](float delta) { navigate(delta); });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .when(selecting(sequencer_))
        .then([this]() { apply(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(selecting(sequencer_))
        .then([this]() { cancel(); });
}

FLASHMEM void SequencerStepContentHandler::open() {
    sequencer_.stepContentSelector.selecting.set(true);
}

FLASHMEM void SequencerStepContentHandler::cancel() {
    sequencer_.stepContentSelector.selecting.set(false);
}

FLASHMEM void SequencerStepContentHandler::navigate(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const int current = static_cast<int>(
        sequencer_.stepContentSelector.focusedAction.get()
    );
    const int next = nav::nextWrappedIndex(
        delta,
        current,
        static_cast<int>(Action::COUNT)
    );
    sequencer_.stepContentSelector.focusedAction.set(static_cast<Action>(next));
}

FLASHMEM void SequencerStepContentHandler::apply() {
    const auto action = sequencer_.stepContentSelector.focusedAction.get();
    sequencer_.stepContentSelector.selecting.set(false);
    if (action == Action::SELECT_STEPS) {
        step_handler_.enterSelectionModeForCurrentFocus();
        return;
    }
    (void)step_edit_handler_.openFocusedStepAtRow(
        rowForAction(action, sequencer_.activeStepProperty.get())
    );
}

}  // namespace core::handler
