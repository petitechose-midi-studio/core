#include "handler/macro/MacroAutomationHandler.hpp"

#include <algorithm>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/macro/MacroAutomationEditorModel.hpp"

namespace core::handler {

namespace {

constexpr uint8_t ROW_STATE = 0;
constexpr uint8_t ROW_LENGTH = 1;
constexpr uint8_t ROW_OFFSET = 2;
constexpr uint8_t ROW_COUNT = 3;

}  // namespace

FLASHMEM MacroAutomationHandler::MacroAutomationHandler(
    StateRefs state,
    MacroEditDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID automationScope
)
    : macro_edit_(state.macroEdit)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , automation_scope_(automationScope) {
    setupBindings();
}

FLASHMEM void MacroAutomationHandler::setupBindings() {
    using ButtonID = Config::ButtonID;
    using EncoderID = Config::EncoderID;

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(automation_scope_)
        .then([this](float delta) { moveFocus(delta); });

    encoders_.encoder(EncoderID::OPT)
        .turn()
        .scope(automation_scope_)
        .then([this](float normalized) { editFocusedValue(normalized); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(automation_scope_)
        .then([this]() { backToMacroEdit(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .scope(automation_scope_)
        .then([this]() { setCoarseEditActive(true); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(automation_scope_)
        .then([this]() { setCoarseEditActive(false); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .release()
        .scope(automation_scope_)
        .then([this]() { clearAutomation(); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(automation_scope_)
        .then([this]() { removeAutomation(); });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(automation_scope_)
        .then([this]() { copyAutomation(); });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(automation_scope_)
        .then([this]() { pasteAutomation(); });
}

bool MacroAutomationHandler::active() const {
    return macro_edit_.visible.get() &&
           macro_edit_.flowPhase.get() == core::state::MacroEditFlowPhase::AUTOMATION;
}

uint8_t MacroAutomationHandler::macroIndex() const {
    return macro_edit_.editingIndex.get();
}

FLASHMEM void MacroAutomationHandler::moveFocus(float delta) {
    if (!active() || !nav::hasTurnDelta(delta)) return;
    const int current = static_cast<int>(macro_edit_.automationFocusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);
    macro_edit_.automationFocusedRow.set(static_cast<uint8_t>(next));
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::editFocusedValue(float normalized) {
    if (!active()) return;
    const uint8_t index = macroIndex();
    if (!services_.automationActiveFor(index)) return;
    const float clamped = std::clamp(normalized, 0.0f, 1.0f);
    const uint8_t row = macro_edit_.automationFocusedRow.get();
    if (row == ROW_STATE) {
        services_.setManualOverride(index, clamped < 0.5f);
        return;
    }
    if (row == ROW_LENGTH) {
        const auto range = macroAutomationLengthEditRange(coarse_edit_active_);
        services_.setAutomationDurationBeats(
            index,
            macroAutomationEncoderPositionToBeat(clamped, range)
        );
        return;
    }
    if (row == ROW_OFFSET) {
        const auto* slot = services_.automationSlot(index);
        const auto range = macroAutomationOffsetEditRange(slot, coarse_edit_active_);
        services_.setAutomationWindowOffsetBeats(
            index,
            macroAutomationEncoderPositionToBeat(clamped, range)
        );
    }
}

FLASHMEM void MacroAutomationHandler::configureOptForFocusedRow() {
    uint8_t steps = 1;
    float position = 0.0f;
    const uint8_t row = macro_edit_.automationFocusedRow.get();
    if (row == ROW_STATE) {
        steps = 2;
        const uint8_t index = macroIndex();
        position = services_.manualOverrideActiveFor(index) ? 0.0f : 1.0f;
    } else if (row == ROW_LENGTH) {
        const auto range = macroAutomationLengthEditRange(coarse_edit_active_);
        steps = range.stepCount;
        const auto* slot = services_.automationSlot(macroIndex());
        if (slot != nullptr && slot->automation.active) {
            position = macroAutomationTicksToEncoderPosition(
                slot->automation.durationTicks,
                range
            );
        }
    } else if (row == ROW_OFFSET) {
        const auto* slot = services_.automationSlot(macroIndex());
        const auto range = macroAutomationOffsetEditRange(slot, coarse_edit_active_);
        steps = range.stepCount;
        if (slot != nullptr && slot->automation.active) {
            position = macroAutomationTicksToEncoderPosition(
                slot->automation.windowOffsetTicks,
                range
            );
        }
    }
    encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
    encoders_.setPosition(Config::EncoderID::OPT, position);
}

FLASHMEM void MacroAutomationHandler::setCoarseEditActive(bool active) {
    if (!this->active() || coarse_edit_active_ == active) return;
    coarse_edit_active_ = active;
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::backToMacroEdit() {
    if (!active()) return;
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_AUTOMATION);
    macro_edit_.closeAutomation();
}

FLASHMEM void MacroAutomationHandler::clearAutomation() {
    if (!active()) return;
    if (ignore_next_bottom_left_release_) {
        ignore_next_bottom_left_release_ = false;
        return;
    }
    services_.clearAutomation(macroIndex());
}

FLASHMEM void MacroAutomationHandler::removeAutomation() {
    if (!active()) return;
    ignore_next_bottom_left_release_ = true;
    services_.removeAutomation(macroIndex());
}

FLASHMEM void MacroAutomationHandler::copyAutomation() {
    if (!active()) return;
    if (ignore_next_bottom_right_release_) {
        ignore_next_bottom_right_release_ = false;
        return;
    }
    services_.copyAutomation(macroIndex());
}

FLASHMEM void MacroAutomationHandler::pasteAutomation() {
    if (!active()) return;
    ignore_next_bottom_right_release_ = true;
    services_.pasteAutomation(macroIndex());
}

}  // namespace core::handler
