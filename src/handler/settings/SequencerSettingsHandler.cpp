#include "SequencerSettingsHandler.hpp"

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "state/ViewSelectorItems.hpp"

namespace core::handler {

using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

FLASHMEM SequencerSettingsHandler::SequencerSettingsHandler(
    StateRefs state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID settingsOverlayScope
)
    : sequencer_settings_(state.sequencerSettings)
    , view_selector_(state.viewSelector)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , settings_overlay_scope_(settingsOverlayScope) {
    setupBindings();
}

FLASHMEM void SequencerSettingsHandler::setupBindings() {
    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(settings_overlay_scope_)
        .then([this](float delta) { moveFocus(delta); });

    buttons_.button(ButtonID::LEFT_TOP)
        .press()
        .scope(settings_overlay_scope_)
        .then([this]() { armSettingsBack(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(settings_overlay_scope_)
        .when([this]() { return left_top_pressed_in_settings_; })
        .then([this]() { backToViewSelector(); });
}

FLASHMEM void SequencerSettingsHandler::closeSettings() {
    overlays_.hide();
    sequencer_settings_.closeOverlay();
}

FLASHMEM void SequencerSettingsHandler::armSettingsBack() {
    left_top_pressed_in_settings_ = true;
}

FLASHMEM void SequencerSettingsHandler::backToViewSelector() {
    left_top_pressed_in_settings_ = false;
    closeSettings();
    view_selector_.selectedIndex.set(
        static_cast<int>(core::state::ViewSelectorItem::SEQUENCER)
    );
    overlays_.show(core::ui::OverlayType::VIEW_SELECTOR, false);
}

FLASHMEM void SequencerSettingsHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta) || ROW_COUNT <= 1) return;

    const int current = static_cast<int>(sequencer_settings_.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);
    sequencer_settings_.focusedRow.set(static_cast<uint8_t>(next));
}

}  // namespace core::handler
