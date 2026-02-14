#include "ViewSwitcherHandler.hpp"

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>

namespace core::handler {

using oc::ui::lvgl::scope;
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

static int wrapIndex(int idx, int count) {
    if (count <= 0) return 0;
    idx %= count;
    if (idx < 0) idx += count;
    return idx;
}

ViewSwitcherHandler::ViewSwitcherHandler(core::state::CoreState& state,
                                         OverlayCtx overlayCtx,
                                         oc::api::EncoderAPI& encoders,
                                         oc::api::ButtonAPI& buttons)
    : state_(state)
    , overlay_ctx_(overlayCtx)
    , encoders_(encoders)
    , buttons_(buttons) {
    setupBindings();
}

void ViewSwitcherHandler::setupBindings() {
    // Open selector (latch behavior for toggle)
    buttons_.button(ButtonID::LEFT_TOP)
        .press()
        .latch()
        .scope(scope(overlay_ctx_.scopeElement))
        .then([this]() { openSelector(); });

    // Close and confirm on release
    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope(overlay_ctx_.overlayElement))
        .then([this]() { closeSelector(); });

    // Navigate views (active while overlay visible)
    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(scope(overlay_ctx_.overlayElement))
        .then([this](float delta) { navigate(delta); });

    // Confirm selection on NAV button (without closing)
    buttons_.button(ButtonID::NAV)
        .release()
        .scope(scope(overlay_ctx_.overlayElement))
        .then([this]() { confirmSelection(); });
}

void ViewSwitcherHandler::openSelector() {
    OC_LOG_DEBUG("[Core ViewSwitcher] openSelector");

    if (!state_.viewSelector.visible.get()) {
        overlay_ctx_.controller.show(core::ui::OverlayType::VIEW_SELECTOR, false);
    }

    encoders_.setMode(EncoderID::NAV, oc::interface::EncoderMode::RELATIVE);
    state_.viewSelector.selectedIndex.set(static_cast<int>(state_.activeView.get()));
}

void ViewSwitcherHandler::navigate(float delta) {
    if (delta == 0.0f) return;
    int step = (delta > 0.0f) ? 1 : -1;

    int current = state_.viewSelector.selectedIndex.get();
    int next = wrapIndex(current + step, VIEW_COUNT);
    state_.viewSelector.selectedIndex.set(next);
}

void ViewSwitcherHandler::confirmSelection() {
    int index = state_.viewSelector.selectedIndex.get();
    if (index < 0 || index >= VIEW_COUNT) return;

    auto type = static_cast<core::ui::ViewType>(index);
    state_.activeView.set(type);
    OC_LOG_INFO("[Core ViewSwitcher] Switched to view index={}", index);
}

void ViewSwitcherHandler::closeSelector() {
    confirmSelection();
    overlay_ctx_.controller.hide();
}

}  // namespace core::handler
