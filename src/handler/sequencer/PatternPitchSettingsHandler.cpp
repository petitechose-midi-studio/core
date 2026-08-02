#include "handler/sequencer/PatternPitchSettingsHandler.hpp"

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerChordProjectionFeedback.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::handler {

using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

namespace {

using PreparedBeginOutcome = core::state::sequencer::SequencerPreparedPatternEditBeginOutcome;
using PreparedCommitOutcome = core::state::sequencer::SequencerPreparedPatternEditCommitOutcome;
using PreparedOwner = core::state::sequencer::SequencerPreparedPatternEditOwner;
using PreparedSealOutcome = core::state::sequencer::SequencerPreparedPatternEditSealOutcome;
using PayloadPlan = core::state::sequencer::SequencerCoalescedPatternPayloadPlan;

bool canOpenPatternPitchSettings(const core::state::sequencer::SequencerState& sequencer) {
    return sequencer.stepPropertyInlineSelector.selecting.get() &&
           sequencer.activeStepProperty.get() == core::state::sequencer::StepProperty::NOTE;
}

FLASHMEM uint8_t pitchSettingKey(uint8_t row, int choiceIndex) {
    constexpr uint8_t CHOICE_BITS = 4U;
    constexpr uint8_t CHOICE_MASK = (1U << CHOICE_BITS) - 1U;
    return static_cast<uint8_t>(static_cast<uint8_t>(row << CHOICE_BITS) |
                                (static_cast<uint8_t>(choiceIndex) & CHOICE_MASK));
}

}  // namespace

FLASHMEM PatternPitchSettingsHandler::PatternPitchSettingsHandler(
    StateRefs state, PatternPitchSettingsDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays, oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons, oc::type::ScopeID sequencerViewScope,
    oc::type::ScopeID settingsOverlayScope, oc::type::ScopeID selectorOverlayScope)
    : settings_(state.settings), sequencer_(state.sequencer), history_(state.history),
      services_(services), overlays_(overlays), encoders_(encoders), buttons_(buttons),
      sequencer_view_scope_(sequencerViewScope), settings_overlay_scope_(settingsOverlayScope),
      selector_overlay_scope_(selectorOverlayScope) {
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

    buttons_.button(ButtonID::NAV).release().scope(settings_overlay_scope_).then([this]() {
        openValueSelector();
    });

    buttons_.button(ButtonID::LEFT_TOP).release().scope(settings_overlay_scope_).then([this]() {
        closeSettings();
    });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(selector_overlay_scope_)
        .then([this](float delta) { navigateSelector(delta); });

    buttons_.button(ButtonID::NAV).release().scope(selector_overlay_scope_).then([this]() {
        applySelectorAndClose();
    });

    buttons_.button(ButtonID::LEFT_TOP).release().scope(selector_overlay_scope_).then([this]() {
        closeSelectorCancel();
    });
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
    if (settings_.flowPhase.get() != core::state::PatternPitchSettingsFlowPhase::VALUE_SELECTOR) {
        return;
    }

    const uint8_t row = settings_.selector.editingRow.get();
    const int selectedIndex = settings_.selector.selectedIndex.get();
    const uint8_t editKey = pitchSettingKey(row, selectedIndex);
    const bool choiceChanged = services_.currentChoiceIndex(row) != selectedIndex;
    const auto payloadPlan = core::state::sequencer::graphView(sequencer_.pattern) == nullptr
                                 ? PayloadPlan::FlatOnly
                                 : PayloadPlan::FullCurrentPayload;
    const auto descriptor = core::state::sequencer::SequencerHistoryDescriptor{
        .kind = core::state::sequencer::SequencerHistoryActionKind::PatternSettings,
    };
    const auto beginOutcome = history_.beginPreparedPatternEdit(PreparedOwner::PatternPitch,
                                                                editKey, payloadPlan, descriptor);
    if (beginOutcome == PreparedBeginOutcome::Failed) return;

    const auto projection = services_.applyChoice(row, selectedIndex);

    const auto sealOutcome = history_.sealPreparedPatternEdit(PreparedOwner::PatternPitch, editKey,
                                                              choiceChanged, descriptor);
    if (core::state::sequencer::sequencerPreparedPatternEditSealFailed(sealOutcome)) return;

    const auto commitOutcome = history_.commitPreparedPatternEdit(PreparedOwner::PatternPitch);
    if ((sealOutcome == PreparedSealOutcome::Sealed &&
         commitOutcome != PreparedCommitOutcome::Committed) ||
        (sealOutcome == PreparedSealOutcome::Cleared &&
         commitOutcome != PreparedCommitOutcome::NoPending &&
         commitOutcome != PreparedCommitOutcome::NoChange)) {
        return;
    }
    showChordProjectionFeedback(sequencer_.historyFeedback, projection, oc::time::millis());

    modal::hideIfCurrent(overlays_, core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR);
    settings_.closeSelector();
}

FLASHMEM void PatternPitchSettingsHandler::closeSelectorCancel() {
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR);
    settings_.closeSelector();
}

}  // namespace core::handler
