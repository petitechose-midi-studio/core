#include "MacroEditInputHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "config/InputIDs.hpp"

using oc::ui::lvgl::scope;

namespace handler {

MacroEditInputHandler::MacroEditInputHandler(
    state::CoreState& state,
    state::OverlayController& overlays,
    ui::MacroEditOverlay& overlay,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* macroViewScope,
    lv_obj_t* overlayScope
)
    : state_(state)
    , overlays_(overlays)
    , overlay_(overlay)
    , encoders_(encoders)
    , buttons_(buttons)
    , macroViewScope_(macroViewScope)
    , overlayScope_(overlayScope)
{
    setupBindings();
}

void MacroEditInputHandler::setupBindings() {
    // ===== MACRO VIEW SCOPE =====
    // Press macro button to open edit overlay
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btnId = static_cast<oc::hal::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btnId)
            .press()
            .scope(scope(macroViewScope_))
            .then([this, i]() { openEdit(i); });
    }

    // ===== OVERLAY SCOPE =====
    // NAV encoder: adjust focused value
    encoders_.encoder(static_cast<oc::hal::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(scope(overlayScope_))
        .then([this](float delta) { adjustValue(delta); });

    // NAV button: toggle focus between CH and CC
    buttons_.button(static_cast<oc::hal::ButtonID>(Config::ButtonID::NAV))
        .press()
        .scope(scope(overlayScope_))
        .then([this]() { toggleFocus(); });

    // Confirm: save and close (BOTTOM_CENTER)
    buttons_.button(static_cast<oc::hal::ButtonID>(Config::ButtonID::BOTTOM_CENTER))
        .release()
        .scope(scope(overlayScope_))
        .then([this]() { saveAndClose(); });

    // Cancel: close without saving (LEFT_TOP)
    buttons_.button(static_cast<oc::hal::ButtonID>(Config::ButtonID::LEFT_TOP))
        .release()
        .scope(scope(overlayScope_))
        .then([this]() { closeWithoutSave(); });

    OC_LOG_DEBUG("[MacroEditInputHandler] Bindings setup complete");
}

void MacroEditInputHandler::openEdit(uint8_t macroIndex) {
    const auto& config = state_.getMacroConfig(macroIndex);
    state_.macroEdit.startEditing(macroIndex, config.channel, config.cc);
    overlays_.show(state::CoreOverlayType::MACRO_EDIT);
    focusedRow_ = 0;
    overlay_.setFocusedRow(0);
    OC_LOG_DEBUG("[MacroEditInputHandler] Opening edit for macro {}", macroIndex);
}

void MacroEditInputHandler::closeWithoutSave() {
    overlays_.hide();
    OC_LOG_DEBUG("[MacroEditInputHandler] Cancelled edit");
}

void MacroEditInputHandler::saveAndClose() {
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
    OC_LOG_INFO("[MacroEditInputHandler] Saved macro {} config: CH={}, CC={}", idx, ch, cc);
}

void MacroEditInputHandler::adjustValue(float delta) {
    int step = (delta > 0) ? 1 : -1;

    if (focusedRow_ == 0) {
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

void MacroEditInputHandler::toggleFocus() {
    focusedRow_ = (focusedRow_ + 1) % 2;
    overlay_.setFocusedRow(focusedRow_);
    OC_LOG_DEBUG("[MacroEditInputHandler] Focus row: {}", focusedRow_ == 0 ? "CH" : "CC");
}

}  // namespace handler
