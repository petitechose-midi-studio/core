#include "ViewSwitcherHandler.hpp"

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

ViewSwitcherHandler::ViewSwitcherHandler(core::state::CoreState& state,
                                         OverlayCtx overlayCtx,
                                         oc::api::EncoderAPI& encoders,
                                         oc::api::ButtonAPI& buttons,
                                         ViewSwitcherHandler::ViewScopes viewScopes)
    : state_(state)
    , overlay_ctx_(overlayCtx)
    , encoders_(encoders)
    , buttons_(buttons)
    , view_scopes_(viewScopes) {
    setupBindings();
}

void ViewSwitcherHandler::setupBindings() {
    // Open selector from any active top-level view scope (latch behavior for toggle)
    lv_obj_t* lastBoundScope = nullptr;
    for (auto* viewScope : view_scopes_) {
        if (!viewScope || viewScope == lastBoundScope) continue;

        buttons_.button(ButtonID::LEFT_TOP)
            .press()
            .latch()
            .scope(scope(viewScope))
            .then([this]() { openSelector(); });

        lastBoundScope = viewScope;
    }

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
    if (!nav::hasTurnDelta(delta)) return;

    int current = state_.viewSelector.selectedIndex.get();
    int next = nav::nextWrappedIndex(delta, current, VIEW_COUNT);
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
