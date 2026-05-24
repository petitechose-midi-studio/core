#include "handler/sequencer/PatternPitchSettingsHandler.hpp"

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {

using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

namespace {

bool canOpenPatternPitchSettings(const core::state::sequencer::SequencerState& sequencer) {
    return sequencer.stepPropertyInlineSelector.selecting.get() &&
           sequencer.activeStepProperty.get() == core::state::sequencer::StepProperty::NOTE;
}

}  // namespace

FLASHMEM PatternPitchSettingsHandler::PatternPitchSettingsHandler(
    StateRefs state,
    PatternPitchSettingsDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID sequencerViewScope,
    oc::type::ScopeID settingsOverlayScope,
    oc::type::ScopeID selectorOverlayScope
)
    : settings_(state.settings)
    , sequencer_(state.sequencer)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , sequencer_view_scope_(sequencerViewScope)
    , settings_overlay_scope_(settingsOverlayScope)
    , selector_overlay_scope_(selectorOverlayScope) {
    setupBindings();
}

FLASHMEM void PatternPitchSettingsHandler::setupBindings() {
    buttons_.button(ButtonID::BOTTOM_LEFT)
        .release()
        .scope(sequencer_view_scope_)
        .when([this]() { return canOpenPatternPitchSettings(sequencer_); })
        .then([this]() { openSettings(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(settings_overlay_scope_)
        .then([this](float delta) { moveFocus(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(settings_overlay_scope_)
        .then([this]() { openValueSelector(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(settings_overlay_scope_)
        .then([this]() { closeSettings(); });

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

FLASHMEM void PatternPitchSettingsHandler::openSettings() {
    sequencer_.stepPropertyInlineSelector.reset();
    settings_.openOverlay();
    overlays_.show(core::ui::OverlayType::PATTERN_PITCH_SETTINGS, false);
}

FLASHMEM void PatternPitchSettingsHandler::closeSettings() {
    overlays_.hide();
    settings_.closeOverlay();
}

FLASHMEM void PatternPitchSettingsHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta) || ROW_COUNT <= 1) return;

    const int current = static_cast<int>(settings_.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);
    settings_.focusedRow.set(static_cast<uint8_t>(next));
}

FLASHMEM void PatternPitchSettingsHandler::openValueSelector() {
    if (settings_.flowPhase.get() != core::state::PatternPitchSettingsFlowPhase::OVERLAY) {
        return;
    }

    const uint8_t row = settings_.focusedRow.get();
    if (services_.choiceCount(row) <= 0) return;

    settings_.openSelector(row, services_.currentChoiceIndex(row));
    overlays_.show(core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR, true);
}

FLASHMEM void PatternPitchSettingsHandler::navigateSelector(float delta) {
    if (settings_.flowPhase.get() != core::state::PatternPitchSettingsFlowPhase::VALUE_SELECTOR) {
        return;
    }

    const uint8_t row = settings_.selector.editingRow.get();
    const int count = services_.choiceCount(row);
    if (count <= 0) return;

    int next = settings_.selector.selectedIndex.get();
    if (!modal::advanceWrappedSelection(delta, settings_.selector, count, next)) return;
    settings_.selector.selectedIndex.set(next);
}

FLASHMEM void PatternPitchSettingsHandler::applySelectorAndClose() {
    services_.applyChoice(settings_.selector.editingRow.get(), settings_.selector.selectedIndex.get());
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR);
    settings_.closeSelector();
}

FLASHMEM void PatternPitchSettingsHandler::closeSelectorCancel() {
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR);
    settings_.closeSelector();
}

}  // namespace core::handler
