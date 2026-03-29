#include "ViewSwitcherHandler.hpp"

#include <oc/ui/lvgl/Scope.hpp>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

ViewSwitcherHandler::ViewSwitcherHandler(StateRefs state,
                                         OverlayCtx overlayCtx,
                                         oc::api::EncoderAPI& encoders,
                                         oc::api::ButtonAPI& buttons,
                                         ViewSwitcherHandler::ViewScopes viewScopes)
    : overlays_state_(state.overlays)
    , active_view_(state.activeView)
    , view_selector_(state.viewSelector)
    , range_selection_(state.rangeSelection)
    , track_selector_(state.trackSelector)
    , pattern_quick_controls_(state.patternQuickControls)
    , step_property_inline_selector_(state.stepPropertyInlineSelector)
    , overlay_ctx_(overlayCtx)
    , encoders_(encoders)
    , buttons_(buttons)
    , view_scopes_(viewScopes) {
    setupBindings();
}

FLASHMEM void ViewSwitcherHandler::setupBindings() {
    // Open selector from any active top-level view scope (latch behavior for toggle)
    oc::type::ScopeID lastBoundScope = 0;
    for (oc::type::ScopeID viewScope : view_scopes_) {
        if (!viewScope || viewScope == lastBoundScope) continue;

        buttons_.button(ButtonID::LEFT_TOP)
            .press()
            .latch()
            .scope(viewScope)
            .when([this]() { return canOpenSelector(); })
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

bool ViewSwitcherHandler::canOpenSelector() const {
    if (overlays_state_.hasVisible()) return false;

    if (active_view_.get() != core::ui::ViewType::SEQUENCER) {
        return true;
    }

    return !range_selection_.active() &&
           !track_selector_.selecting.get() &&
           !pattern_quick_controls_.selecting.get() &&
           !step_property_inline_selector_.selecting.get();
}

void ViewSwitcherHandler::openSelector() {
    if (!view_selector_.visible.get()) {
        overlay_ctx_.controller.show(core::ui::OverlayType::VIEW_SELECTOR, false);
    }

    encoders_.setMode(EncoderID::NAV, oc::interface::EncoderMode::RELATIVE);
    view_selector_.selectedIndex.set(static_cast<int>(active_view_.get()));
}

void ViewSwitcherHandler::navigate(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    int current = view_selector_.selectedIndex.get();
    int next = nav::nextWrappedIndex(delta, current, VIEW_COUNT);
    view_selector_.selectedIndex.set(next);
}

void ViewSwitcherHandler::confirmSelection() {
    int index = view_selector_.selectedIndex.get();
    if (index < 0 || index >= VIEW_COUNT) return;

    auto type = static_cast<core::ui::ViewType>(index);
    active_view_.set(type);
}

void ViewSwitcherHandler::closeSelector() {
    confirmSelection();
    overlay_ctx_.controller.hide();
}

}  // namespace core::handler
