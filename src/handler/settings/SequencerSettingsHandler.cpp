#include "SequencerSettingsHandler.hpp"

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "state/ViewSelectorItems.hpp"

namespace core::handler {

using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

FLASHMEM SequencerSettingsHandler::SequencerSettingsHandler(
    StateRefs state,
    SequencerSettingsDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID settingsOverlayScope,
    oc::type::ScopeID selectorOverlayScope
)
    : sequencer_settings_(state.sequencerSettings)
    , view_selector_(state.viewSelector)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , settings_overlay_scope_(settingsOverlayScope)
    , selector_overlay_scope_(selectorOverlayScope) {
    setupBindings();
}

FLASHMEM void SequencerSettingsHandler::setupBindings() {
    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(settings_overlay_scope_)
        .then([this](float delta) { moveFocus(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(settings_overlay_scope_)
        .then([this]() { openValueSelector(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .press()
        .scope(settings_overlay_scope_)
        .then([this]() { armSettingsBack(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(settings_overlay_scope_)
        .when([this]() { return left_top_pressed_in_settings_; })
        .then([this]() { backToViewSelector(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(selector_overlay_scope_)
        .then([this](float delta) { navigateSelector(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(selector_overlay_scope_)
        .then([this]() { applySelectorAndClose(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(selector_overlay_scope_)
        .then([this]() { closeSelectorCancel(); });
}

FLASHMEM void SequencerSettingsHandler::closeSettings() {
    left_top_pressed_in_settings_ = false;
    overlays_.hide();
    sequencer_settings_.closeOverlay();
}

FLASHMEM void SequencerSettingsHandler::armSettingsBack() {
    left_top_pressed_in_settings_ = true;
}

FLASHMEM void SequencerSettingsHandler::backToViewSelector() {
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

FLASHMEM void SequencerSettingsHandler::openValueSelector() {
    auto& s = sequencer_settings_;
    if (s.flowPhase.get() != core::state::SequencerSettingsFlowPhase::OVERLAY) {
        return;
    }

    const uint8_t row = s.focusedRow.get();
    if (services_.choiceCount(row) <= 0) {
        return;
    }
    const int current = services_.currentChoiceIndex(row);
    s.openSelector(row, current);
    overlays_.show(core::ui::OverlayType::SEQUENCER_SETTINGS_SELECTOR, true);
}

FLASHMEM void SequencerSettingsHandler::navigateSelector(float delta) {
    if (sequencer_settings_.flowPhase.get() !=
        core::state::SequencerSettingsFlowPhase::VALUE_SELECTOR) {
        return;
    }

    const uint8_t row = sequencer_settings_.selector.editingRow.get();
    const int count = services_.choiceCount(row);
    if (count <= 0) return;

    int next = sequencer_settings_.selector.selectedIndex.get();
    if (!modal::advanceWrappedSelection(delta, sequencer_settings_.selector, count, next)) {
        return;
    }
    sequencer_settings_.selector.selectedIndex.set(next);
}

FLASHMEM void SequencerSettingsHandler::applySelectorAndClose() {
    auto& selector = sequencer_settings_.selector;
    const uint8_t row = selector.editingRow.get();
    const int choice = selector.selectedIndex.get();

    services_.applyChoice(row, choice);

    modal::hideIfCurrent(overlays_, core::ui::OverlayType::SEQUENCER_SETTINGS_SELECTOR);
    sequencer_settings_.closeSelector();
}

FLASHMEM void SequencerSettingsHandler::closeSelectorCancel() {
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::SEQUENCER_SETTINGS_SELECTOR);
    sequencer_settings_.closeSelector();
}

}  // namespace core::handler
