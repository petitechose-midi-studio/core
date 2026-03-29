#include "SequencerPropertySelectorHandler.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

namespace {
constexpr int PROPERTY_COUNT =
    static_cast<int>(core::state::sequencer::StepProperty::PROBABILITY) + 1;

inline oc::type::IsActiveFn selectingPredicate(core::state::sequencer::SequencerState& sequencer) {
    return [&sequencer]() { return sequencer.stepPropertyInlineSelector.selecting.get(); };
}

inline oc::type::IsActiveFn canOpenPropertySelector(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks
) {
    return [&overlays, &sequencer, &tracks]() {
        return !overlays.hasVisible() &&
               !tracks.selector.selecting.get() &&
               !sequencer.patternQuickControls.selecting.get() &&
               !sequencer.rangeSelection.active();
    };
}

}  // namespace

SequencerPropertySelectorHandler::SequencerPropertySelectorHandler(
    StateRefs state,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId
)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void SequencerPropertySelectorHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope_id_)
        .when(canOpenPropertySelector(overlays_, sequencer_, tracks_))
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .when(selectingPredicate(sequencer_))
        .then([this]() { closeApply(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(selectingPredicate(sequencer_))
        .then([this](float delta) { navigate(delta); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(selectingPredicate(sequencer_))
        .then([this]() { closeCancel(); });
}

void SequencerPropertySelectorHandler::open() {
    auto& o = sequencer_.stepPropertyInlineSelector;
    o.reset();
    o.selecting.set(true);

    const int active = static_cast<int>(sequencer_.activeStepProperty.get());
    o.snapshotIndex = active;
    o.snapshotValid = true;
    o.selectedIndex.set(active);
}

void SequencerPropertySelectorHandler::navigate(float delta) {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int current = sequencer_.stepPropertyInlineSelector.selectedIndex.get();
    const int next = nav::nextWrappedIndex(delta, current, PROPERTY_COUNT);
    sequencer_.stepPropertyInlineSelector.selectedIndex.set(next);
    sequencer_.activeStepProperty.set(
        static_cast<core::state::sequencer::StepProperty>(next)
    );
}

void SequencerPropertySelectorHandler::closeApply() {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
    sequencer_.stepPropertyInlineSelector.reset();
}

void SequencerPropertySelectorHandler::closeCancel() {
    auto& o = sequencer_.stepPropertyInlineSelector;
    if (!o.selecting.get()) return;
    if (o.snapshotValid) {
        const int restored = std::clamp(o.snapshotIndex, 0, PROPERTY_COUNT - 1);
        sequencer_.activeStepProperty.set(static_cast<core::state::sequencer::StepProperty>(restored));
    }
    o.reset();
}

}  // namespace core::handler
