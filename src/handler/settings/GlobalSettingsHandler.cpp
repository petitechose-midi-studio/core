#include "GlobalSettingsHandler.hpp"

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "state/ViewSelectorItems.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

FLASHMEM GlobalSettingsHandler::GlobalSettingsHandler(StateRefs state,
                                                      GlobalSettingsDomainServices services,
                                                      oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                                      oc::api::EncoderAPI& encoders,
                                                      oc::api::ButtonAPI& buttons,
                                                      oc::type::ScopeID settingsOverlayScope,
                                                      oc::type::ScopeID selectorOverlayScope)
    : global_settings_(state.globalSettings)
    , view_selector_(state.viewSelector)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , settings_overlay_scope_(settingsOverlayScope)
    , selector_overlay_scope_(selectorOverlayScope) {
    setupBindings();
}

FLASHMEM void GlobalSettingsHandler::setupBindings() {
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

FLASHMEM void GlobalSettingsHandler::closeSettings() {
    overlays_.hide();
    global_settings_.closeOverlay();
}

FLASHMEM void GlobalSettingsHandler::armSettingsBack() {
    left_top_pressed_in_settings_ = true;
}

FLASHMEM void GlobalSettingsHandler::backToViewSelector() {
    left_top_pressed_in_settings_ = false;
    closeSettings();
    view_selector_.selectedIndex.set(
        static_cast<int>(core::state::ViewSelectorItem::GLOBAL_SETTINGS)
    );
    overlays_.show(core::ui::OverlayType::VIEW_SELECTOR, false);
}

FLASHMEM void GlobalSettingsHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = static_cast<int>(global_settings_.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);

    global_settings_.focusedRow.set(static_cast<uint8_t>(next));
}

FLASHMEM void GlobalSettingsHandler::openValueSelector() {
    auto& s = global_settings_;
    if (s.flowPhase.get() != core::state::GlobalSettingsFlowPhase::OVERLAY) {
        return;
    }

    const uint8_t row = s.focusedRow.get();
    const int current = services_.currentChoiceIndex(row);
    s.openSelector(row, current);
    overlays_.show(core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR, true);
}

FLASHMEM void GlobalSettingsHandler::navigateSelector(float delta) {
    if (global_settings_.flowPhase.get() != core::state::GlobalSettingsFlowPhase::VALUE_SELECTOR) {
        return;
    }

    const uint8_t row = global_settings_.selector.editingRow.get();
    const int count = services_.choiceCount(row);
    if (count <= 0) return;

    int next = global_settings_.selector.selectedIndex.get();
    if (!modal::advanceWrappedSelection(delta, global_settings_.selector, count, next)) {
        return;
    }
    global_settings_.selector.selectedIndex.set(next);
}

FLASHMEM void GlobalSettingsHandler::applySelectorAndClose() {
    auto& selector = global_settings_.selector;
    const uint8_t row = selector.editingRow.get();
    const int choice = selector.selectedIndex.get();

    services_.applyChoice(row, choice);

    modal::hideIfCurrent(overlays_, core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR);
    global_settings_.closeSelector();
}

FLASHMEM void GlobalSettingsHandler::closeSelectorCancel() {
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR);
    global_settings_.closeSelector();
}

}  // namespace core::handler
