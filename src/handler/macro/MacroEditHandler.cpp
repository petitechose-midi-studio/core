#include "MacroEditHandler.hpp"

#include <algorithm>

#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include <oc/util/Index.hpp>

#include <config/App.hpp>

using oc::util::wrapIndex;

namespace core::handler {

namespace {

constexpr uint32_t QUICK_RELEASE_WINDOW_MS = 450;
constexpr uint8_t ROW_COUNT = 2;

float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

}  // namespace

MacroEditHandler::MacroEditHandler(
    core::state::CoreState& state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* macroViewScope,
    lv_obj_t* overlayScope,
    lv_obj_t* selectorScope,
    lv_obj_t* pageSelectorScope,
    lv_obj_t* macroSelectorScope
)
    : state_(state)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , macro_view_scope_(macroViewScope)
    , overlay_scope_(overlayScope)
    , selector_scope_(selectorScope)
    , page_selector_scope_(pageSelectorScope)
    , macro_selector_scope_(macroSelectorScope)
{
    setupBindings();
}

void MacroEditHandler::setupBindings() {
    const auto navButton = static_cast<oc::type::ButtonID>(Config::ButtonID::NAV);
    const auto leftTopButton = static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP);
    const auto leftCenterButton = static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_CENTER);
    const auto leftBottomButton = static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_BOTTOM);

    const oc::type::ScopeID mainScope = oc::ui::lvgl::scopeID(overlay_scope_);
    const oc::type::ScopeID valueScope = oc::ui::lvgl::scopeID(selector_scope_);
    const oc::type::ScopeID pageScope = oc::ui::lvgl::scopeID(page_selector_scope_);
    const oc::type::ScopeID macroScope = oc::ui::lvgl::scopeID(macro_selector_scope_);

    const oc::type::ScopeID openingReleaseScopes[] = {mainScope, valueScope, pageScope, macroScope};

    // Long press opens MacroEdit for targeted macro; paired release decides latch/close.
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto macroButton = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);

        buttons_.button(macroButton)
            .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            .scope(oc::ui::lvgl::scopeID(macro_view_scope_))
            .then([this, i]() { openEdit(i); });

        for (oc::type::ScopeID scopeId : openingReleaseScopes) {
            buttons_.button(macroButton)
                .release()
                .scope(scopeId)
                .then([this, i]() { handleOpeningMacroRelease(i); });
        }
    }

    // ===== MAIN MACRO EDIT OVERLAY SCOPE =====
    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(mainScope)
        .then([this](float delta) { moveFocus(delta); });

    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT))
        .turn()
        .scope(mainScope)
        .then([this](float normalized) { setFocusedValue(normalized); });

    buttons_.button(navButton)
        .press()
        .scope(mainScope)
        .then([this]() { openValueSelector(); });

    buttons_.button(leftTopButton)
        .release()
        .scope(mainScope)
        .then([this]() { closeOverlay(); });

    buttons_.button(leftCenterButton)
        .press()
        .scope(mainScope)
        .then([this]() { openPageSelector(); });

    buttons_.button(leftBottomButton)
        .press()
        .scope(mainScope)
        .then([this]() { openMacroTargetSelector(); });

    // ===== VALUE SELECTOR OVERLAY SCOPE =====
    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(valueScope)
        .then([this](float delta) { navigateValueSelector(delta); });

    buttons_.button(navButton)
        .release()
        .scope(valueScope)
        .then([this]() { applyValueSelectorAndClose(); });

    buttons_.button(leftTopButton)
        .release()
        .scope(valueScope)
        .then([this]() { closeOverlay(); });

    // ===== PAGE SELECTOR OVERLAY SCOPE =====
    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(pageScope)
        .then([this](float delta) { navigatePageSelector(delta); });

    buttons_.button(leftCenterButton)
        .release()
        .scope(pageScope)
        .then([this]() { applyPageSelectorAndClose(); });

    // ===== MACRO TARGET SELECTOR OVERLAY SCOPE =====
    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(macroScope)
        .then([this](float delta) { navigateMacroTargetSelector(delta); });

    buttons_.button(leftBottomButton)
        .release()
        .scope(macroScope)
        .then([this]() { applyMacroTargetSelectorAndClose(); });

    const oc::type::ScopeID selectorCloseScopes[] = {pageScope, macroScope};
    for (oc::type::ScopeID scopeId : selectorCloseScopes) {
        buttons_.button(leftTopButton)
            .release()
            .scope(scopeId)
            .then([this]() { closeOverlay(); });
    }

}

void MacroEditHandler::openEdit(uint8_t macroIndex) {
    const auto& config = state_.getMacroConfig(macroIndex);
    has_staged_config_changes_ = false;

    auto& edit = state_.macroEdit;
    edit.startEditing(macroIndex, config.channel, config.cc);
    edit.openedByMacroIndex = macroIndex;
    edit.openedAtMs = 0;
    edit.pendingOpenReleaseDecision = true;

    overlays_.show(core::ui::OverlayType::MACRO_EDIT);

    configureOptForFocusedRow();

}

void MacroEditHandler::handleOpeningMacroRelease(uint8_t macroIndex) {
    auto& edit = state_.macroEdit;
    if (!edit.visible.get()) return;
    if (!edit.pendingOpenReleaseDecision) return;
    if (macroIndex != edit.openedByMacroIndex) return;

    edit.pendingOpenReleaseDecision = false;

    // If the UI has not stamped the visible-open time yet, treat this release
    // as quick-release and keep the overlay open.
    if (edit.openedAtMs == 0) {
        return;
    }

    const uint32_t elapsedMs = oc::time::millis() - edit.openedAtMs;
    if (elapsedMs >= QUICK_RELEASE_WINDOW_MS) {
        closeOverlay();
    }
}

void MacroEditHandler::closeOverlay() {
    // Close any stacked macro-edit related selector first, then the main overlay.
    while (true) {
        const auto current = overlays_.current();
        if (current == core::ui::OverlayType::MACRO_EDIT_SELECTOR ||
            current == core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR ||
            current == core::ui::OverlayType::PAGE_SELECTOR) {
            overlays_.hide();
            continue;
        }
        break;
    }

    if (overlays_.current() == core::ui::OverlayType::MACRO_EDIT) {
        overlays_.hide();
    }

    // Persist staged CH/CC updates once per edit session.
    if (has_staged_config_changes_) {
        state_.settings.commit();
        state_.configRevision.set(state_.configRevision.get() + 1);
        has_staged_config_changes_ = false;
    }

    state_.macroEdit.reset();
    state_.pages.selector.visible.set(false);
    state_.pages.selector.selectedIndex.set(state_.pages.activePage);
}

void MacroEditHandler::moveFocus(float delta) {
    if (delta == 0.0f) return;

    const int step = (delta > 0.0f) ? 1 : -1;
    const int current = static_cast<int>(state_.macroEdit.focusedRow.get());
    const int next = wrapIndex(current + step, ROW_COUNT);
    state_.macroEdit.focusedRow.set(static_cast<uint8_t>(next));

    configureOptForFocusedRow();
}

void MacroEditHandler::setFocusedValue(float normalized) {
    const uint8_t row = state_.macroEdit.focusedRow.get();
    const int count = valueCountForRow(row);

    const float clamped = clampNormalized(normalized);
    const int index = static_cast<int>(clamped * static_cast<float>(count - 1) + 0.5f);
    setValueForRow(row, index);
}

void MacroEditHandler::openValueSelector() {
    if (!state_.macroEdit.visible.get()) return;
    if (state_.macroEdit.selector.visible.get()) return;

    auto& selector = state_.macroEdit.selector;
    const uint8_t row = state_.macroEdit.focusedRow.get();

    selector.reset();
    selector.editingRow.set(row);

    selector.selectedIndex.set(valueForRow(row));

    overlays_.show(core::ui::OverlayType::MACRO_EDIT_SELECTOR, true);

}

void MacroEditHandler::navigateValueSelector(float delta) {
    if (delta == 0.0f) return;

    auto& selector = state_.macroEdit.selector;
    if (!selector.visible.get()) return;

    const int count = valueCountForRow(selector.editingRow.get());
    const int step = (delta > 0.0f) ? 1 : -1;
    const int current = selector.selectedIndex.get();
    const int next = wrapIndex(current + step, count);
    selector.selectedIndex.set(next);
}

void MacroEditHandler::applyValueSelectorAndClose() {
    auto& selector = state_.macroEdit.selector;
    if (!selector.visible.get()) return;

    setValueForRow(selector.editingRow.get(), selector.selectedIndex.get());

    overlays_.hide();
    selector.reset();
    configureOptForFocusedRow();
}

void MacroEditHandler::openPageSelector() {
    if (!state_.macroEdit.visible.get()) return;
    if (state_.macroEdit.selector.visible.get()) return;
    if (state_.macroEdit.macroSelector.visible.get()) return;

    state_.pages.selector.selectedIndex.set(state_.pages.activePage);
    overlays_.show(core::ui::OverlayType::PAGE_SELECTOR, true);

}

void MacroEditHandler::navigatePageSelector(float delta) {
    if (delta == 0.0f) return;
    if (!state_.pages.selector.visible.get()) return;

    const int step = (delta > 0.0f) ? 1 : -1;
    const int current = static_cast<int>(state_.pages.selector.selectedIndex.get());
    const int next = wrapIndex(current + step, core::state::macro::PAGE_COUNT);
    state_.pages.selector.selectedIndex.set(static_cast<uint8_t>(next));
}

void MacroEditHandler::applyPageSelectorAndClose() {
    if (!state_.pages.selector.visible.get()) return;

    const uint8_t targetPage = std::clamp(
        state_.pages.selector.selectedIndex.get(),
        static_cast<uint8_t>(0),
        static_cast<uint8_t>(core::state::macro::PAGE_COUNT - 1)
    );

    if (targetPage != state_.pages.activePage) {
        state_.switchToPage(targetPage);
        has_staged_config_changes_ = false;

        const uint8_t macroIndex = state_.macroEdit.editingIndex.get();
        const auto& config = state_.getMacroConfig(macroIndex);
        state_.macroEdit.tempChannel.set(config.channel);
        state_.macroEdit.tempCC.set(config.cc);
    }

    overlays_.hide();
    state_.pages.selector.visible.set(false);
    configureOptForFocusedRow();
}

void MacroEditHandler::openMacroTargetSelector() {
    if (!state_.macroEdit.visible.get()) return;
    if (state_.macroEdit.selector.visible.get()) return;
    if (state_.pages.selector.visible.get()) return;
    if (state_.macroEdit.macroSelector.visible.get()) return;

    auto& selector = state_.macroEdit.macroSelector;
    selector.reset();
    selector.selectedIndex.set(state_.macroEdit.editingIndex.get());

    overlays_.show(core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR, true);

}

void MacroEditHandler::navigateMacroTargetSelector(float delta) {
    if (delta == 0.0f) return;

    auto& selector = state_.macroEdit.macroSelector;
    if (!selector.visible.get()) return;

    const int step = (delta > 0.0f) ? 1 : -1;
    const int current = selector.selectedIndex.get();
    const int next = wrapIndex(current + step, core::state::MACRO_COUNT);
    selector.selectedIndex.set(next);
}

void MacroEditHandler::applyMacroTargetSelectorAndClose() {
    auto& selector = state_.macroEdit.macroSelector;
    if (!selector.visible.get()) return;

    const uint8_t targetMacro = static_cast<uint8_t>(std::clamp(
        selector.selectedIndex.get(),
        0,
        static_cast<int>(core::state::MACRO_COUNT) - 1
    ));

    if (targetMacro != state_.macroEdit.editingIndex.get()) {
        const auto& config = state_.getMacroConfig(targetMacro);
        state_.macroEdit.editingIndex.set(targetMacro);
        state_.macroEdit.tempChannel.set(config.channel);
        state_.macroEdit.tempCC.set(config.cc);
    }

    overlays_.hide();
    selector.reset();
    configureOptForFocusedRow();
}

void MacroEditHandler::setValueForRow(uint8_t row, int value) {
    if (row == 0) {
        const int clamped = std::clamp(value, 0, 15);
        state_.macroEdit.tempChannel.set(static_cast<uint8_t>(clamped));
    } else {
        const int clamped = std::clamp(value, 0, 127);
        state_.macroEdit.tempCC.set(static_cast<uint8_t>(clamped));
    }

    applyTempConfig();
}

int MacroEditHandler::valueForRow(uint8_t row) const {
    if (row == 0) {
        return static_cast<int>(state_.macroEdit.tempChannel.get());
    }
    return static_cast<int>(state_.macroEdit.tempCC.get());
}

int MacroEditHandler::valueCountForRow(uint8_t row) const {
    if (row == 0) return 16;
    return 128;
}

void MacroEditHandler::applyTempConfig() {
    const uint8_t macroIndex = state_.macroEdit.editingIndex.get();
    const uint8_t channel = state_.macroEdit.tempChannel.get();
    const uint8_t cc = state_.macroEdit.tempCC.get();

    has_staged_config_changes_ |= state_.setMacroConfig(macroIndex, channel, cc);
}

void MacroEditHandler::configureOptForFocusedRow() {
    const uint8_t row = state_.macroEdit.focusedRow.get();
    const int count = valueCountForRow(row);
    const int current = valueForRow(row);

    encoders_.setDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT),
                               static_cast<uint8_t>(count));

    const float position = (count > 1)
                               ? static_cast<float>(current) / static_cast<float>(count - 1)
                               : 0.0f;
    encoders_.setPosition(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT), position);
}

}  // namespace core::handler
