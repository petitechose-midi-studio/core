#include "ViewSwitcherHandler.hpp"

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/ViewSelectorItems.hpp"

namespace core::handler {

using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

FLASHMEM ViewSwitcherHandler::ViewSwitcherHandler(StateRefs state,
                                                  oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                                  oc::api::EncoderAPI& encoders,
                                                  oc::api::ButtonAPI& buttons,
                                                  ViewSwitcherHandler::ViewScopes viewScopes,
                                                  oc::type::ScopeID viewSelectorScope)
    : overlays_state_(state.overlays)
    , active_view_(state.activeView)
    , view_selector_(state.viewSelector)
    , sequencer_settings_(state.sequencerSettings)
    , sequencer_content_view_(state.sequencerContentView)
    , pattern_quick_controls_(state.patternQuickControls)
    , step_property_inline_selector_(state.stepPropertyInlineSelector)
    , track_structure_selection_(state.trackStructureSelection)
    , macro_page_selection_(state.macroPageSelection)
    , sequencer_page_selection_(state.sequencerPageSelection)
    , project_navigation_(state.projectNavigation)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , view_scopes_(viewScopes)
    , view_selector_scope_(viewSelectorScope) {
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
        .scope(view_selector_scope_)
        .then([this]() { closeSelector(); });

    // Navigate views (active while overlay visible)
    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(view_selector_scope_)
        .then([this](float delta) { navigate(delta); });

    // Confirm selection on NAV button.
    buttons_.button(ButtonID::NAV)
        .release()
        .scope(view_selector_scope_)
        .then([this]() { closeSelector(); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .release()
        .scope(view_selector_scope_)
        .when([this]() {
            return core::state::viewSelectorItemHasSettingsAction(
                core::state::viewSelectorItemAt(view_selector_.selectedIndex.get())
            );
        })
        .then([this]() { openSelectedItemSettings(); });
}

FLASHMEM bool ViewSwitcherHandler::canOpenSelector() const {
    if (overlays_state_.hasVisible()) return false;
    if (track_structure_selection_.active.get()) return false;

    if (active_view_.get() == core::ui::ViewType::MACRO &&
        macro_page_selection_.active.get()) {
        return false;
    }

    if (active_view_.get() == core::ui::ViewType::SEQUENCER &&
        sequencer_page_selection_.active.get()) {
        return false;
    }

    if (active_view_.get() != core::ui::ViewType::SEQUENCER) {
        if (active_view_.get() == core::ui::ViewType::PROJECT) {
            return core::state::project::projectNavigationAtRoot(project_navigation_) &&
                   !project_navigation_.physicalHoldActive.get();
        }
        return true;
    }

    if (sequencer_content_view_.isChildContent()) {
        return false;
    }

    return !pattern_quick_controls_.selecting.get() &&
           !step_property_inline_selector_.selecting.get();
}

FLASHMEM void ViewSwitcherHandler::openSelector() {
    view_selector_.selectedIndex.set(
        static_cast<int>(core::state::viewSelectorItemForView(active_view_.get()))
    );

    if (!view_selector_.visible.get()) {
        overlays_.show(core::ui::OverlayType::VIEW_SELECTOR, false);
    }

    encoders_.setMode(EncoderID::NAV, oc::interface::EncoderMode::RELATIVE);
}

FLASHMEM void ViewSwitcherHandler::navigate(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    int current = view_selector_.selectedIndex.get();
    int next = nav::nextWrappedIndex(delta, current, core::state::VIEW_SELECTOR_ITEM_COUNT);
    view_selector_.selectedIndex.set(next);
}

FLASHMEM void ViewSwitcherHandler::confirmSelection() {
    int index = view_selector_.selectedIndex.get();
    if (index < 0 || index >= core::state::VIEW_SELECTOR_ITEM_COUNT) return;

    const auto item = core::state::viewSelectorItemAt(index);
    if (!core::state::viewSelectorItemHasView(item)) return;

    auto type = core::state::viewForSelectorItem(item);
    if (active_view_.get() == type) return;
    active_view_.set(type);
}

FLASHMEM void ViewSwitcherHandler::closeSelector() {
    overlays_.hide();
    confirmSelection();
}

FLASHMEM void ViewSwitcherHandler::openSelectedItemSettings() {
    const auto item = core::state::viewSelectorItemAt(view_selector_.selectedIndex.get());
    if (item != core::state::ViewSelectorItem::SEQUENCER) return;

    overlays_.hide();
    view_selector_.visible.set(false);
    active_view_.set(core::ui::ViewType::SEQUENCER);
    sequencer_settings_.openOverlay();
    overlays_.show(core::ui::OverlayType::SEQUENCER_SETTINGS, false);
}

}  // namespace core::handler
