#include "MacroEditHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>

using oc::ui::lvgl::scope;

namespace core::handler {

MacroEditHandler::MacroEditHandler(
    core::state::CoreState& state,
    core::state::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* macroViewScope,
    lv_obj_t* overlayScope
)
    : state_(state)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , macro_view_scope_(macroViewScope)
    , overlay_scope_(overlayScope)
{
    setupBindings();
}

void MacroEditHandler::setupBindings() {
    // ===== MACRO VIEW SCOPE =====
    // Press macro button to open edit overlay
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btnId = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btnId)
            .press()
            .scope(scope(macro_view_scope_))
            .then([this, i]() { openEdit(i); });
    }

    // ===== OVERLAY SCOPE =====
    // NAV encoder: adjust focused value
    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(scope(overlay_scope_))
        .then([this](float delta) { adjustValue(delta); });

    // NAV button: toggle focus between CH and CC
    buttons_.button(static_cast<oc::type::ButtonID>(Config::ButtonID::NAV))
        .press()
        .scope(scope(overlay_scope_))
        .then([this]() { toggleFocus(); });

    // Confirm: save and close (BOTTOM_CENTER)
    buttons_.button(static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_CENTER))
        .release()
        .scope(scope(overlay_scope_))
        .then([this]() { saveAndClose(); });

    // Cancel: close without saving (LEFT_TOP)
    buttons_.button(static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP))
        .release()
        .scope(scope(overlay_scope_))
        .then([this]() { closeWithoutSave(); });

    OC_LOG_DEBUG("[MacroEditHandler] Bindings setup complete");
}

void MacroEditHandler::openEdit(uint8_t macroIndex) {
    const auto& config = state_.getMacroConfig(macroIndex);
    state_.macroEdit.startEditing(macroIndex, config.channel, config.cc);
    overlays_.show(core::ui::OverlayType::MACRO_EDIT);
    OC_LOG_DEBUG("[MacroEditHandler] Opening edit for macro {}", macroIndex);
}

void MacroEditHandler::closeWithoutSave() {
    overlays_.hide();
    OC_LOG_DEBUG("[MacroEditHandler] Cancelled edit");
}

void MacroEditHandler::saveAndClose() {
    uint8_t idx = state_.macroEdit.editingIndex.get();
    uint8_t ch = state_.macroEdit.tempChannel.get();
    uint8_t cc = state_.macroEdit.tempCC.get();

    // Update active page config
    state_.pages.activeConfigs[idx].channel = ch;
    state_.pages.activeConfigs[idx].cc = cc;

    // Save to persistent storage
    state_.settings.saveChannel(state_.pages.activePage, idx, ch);
    state_.settings.saveCC(state_.pages.activePage, idx, cc);

    overlays_.hide();
    OC_LOG_INFO("[MacroEditHandler] Saved macro {} config: CH={}, CC={}", idx, ch, cc);
}

void MacroEditHandler::adjustValue(float delta) {
    int step = (delta > 0) ? 1 : -1;
    uint8_t focusedRow = state_.macroEdit.focusedRow.get();

    if (focusedRow == 0) {
        // Channel: 1-16
        int ch = state_.macroEdit.tempChannel.get() + step;
        ch = std::clamp(ch, 1, 16);
        state_.macroEdit.tempChannel.set(static_cast<uint8_t>(ch));
    } else {
        // CC: 0-127
        int cc = state_.macroEdit.tempCC.get() + step;
        cc = std::clamp(cc, 0, 127);
        state_.macroEdit.tempCC.set(static_cast<uint8_t>(cc));
    }
}

void MacroEditHandler::toggleFocus() {
    uint8_t current = state_.macroEdit.focusedRow.get();
    uint8_t next = (current + 1) % 2;
    state_.macroEdit.focusedRow.set(next);
    OC_LOG_DEBUG("[MacroEditHandler] Focus row: {}", next == 0 ? "CH" : "CC");
}

}  // namespace core::handler
