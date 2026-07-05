#include "handler/macro/MacroAutomationHandler.hpp"

#include <algorithm>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {

namespace {

constexpr uint8_t ROW_COUNT = 4;

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
}

FLASHMEM void MacroAutomationHandler::editFocusedValue(float normalized) {
    if (!active()) return;
    if (macro_edit_.automationFocusedRow.get() != 0) return;
    const uint8_t index = macroIndex();
    if (!services_.automationActiveFor(index)) return;
    services_.setAutomationManualOverride(index, normalized < 0.5f);
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
