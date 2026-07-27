#include "ViewSwitcherHandler.hpp"

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/CoreState.hpp"
#include "state/ViewSelectorItems.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler {

using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

FLASHMEM ViewSwitcherHandler::ViewSwitcherHandler(StateRefs state,
                                                  oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                                  oc::api::EncoderAPI& encoders,
                                                  oc::api::ButtonAPI& buttons,
                                                  ViewSwitcherHandler::ViewScopes viewScopes,
                                                  oc::type::ScopeID viewSelectorScope)
    : core_state_(state.coreState)
    , overlays_state_(state.overlays)
    , active_view_(state.activeView)
    , view_selector_(state.viewSelector)
    , pattern_quick_controls_(state.patternQuickControls)
    , step_property_inline_selector_(state.stepPropertyInlineSelector)
    , cc_lane_ui_(state.ccLaneUi)
    , sequencer_step_selection_(state.sequencerStepSelection)
    , project_navigation_(state.projectNavigation)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , view_scopes_(viewScopes)
    , view_selector_scope_(viewSelectorScope) {
    setupBindings();
}

FLASHMEM void ViewSwitcherHandler::setupBindings() {
    const auto projectScope =
        view_scopes_[static_cast<std::size_t>(core::ui::ViewType::PROJECT)];
    std::array<oc::type::ScopeID, VIEW_SCOPE_COUNT> boundScopes{};
    std::size_t boundScopeCount = 0;
    for (oc::type::ScopeID viewScope : view_scopes_) {
        if (!viewScope) continue;
        bool alreadyBound = false;
        for (std::size_t i = 0; i < boundScopeCount; ++i) {
            if (boundScopes[i] == viewScope) {
                alreadyBound = true;
                break;
            }
        }
        if (alreadyBound) continue;

        if (viewScope == projectScope) {
            // Project children own LEFT_TOP exclusively for Back/Cancel. Once
            // back at a root, the same short gesture opens the global selector.
            buttons_.button(ButtonID::LEFT_TOP)
                .press()
                .latch()
                .scope(viewScope)
                .when([this]() {
                    return canOpenSelector() &&
                           core::state::project::projectNavigationAtRoot(
                               project_navigation_
                           );
                })
                .then([this]() { (void)beginSelectorPress(); });
        } else {
            buttons_.button(ButtonID::LEFT_TOP)
                .press()
                .latch()
                .scope(viewScope)
                .when([this]() { return canOpenSelector(); })
                .then([this]() { (void)beginSelectorPress(); });
        }

        boundScopes[boundScopeCount++] = viewScope;
    }

    // The opening press transfers ownership to this scope. A short release
    // activates this latch and keeps the selector visible; the next tap
    // releases it. A physical hold exceeds the configured latch threshold, so
    // its first release is dispatched immediately to close/apply below.
    buttons_.button(ButtonID::LEFT_TOP)
        .press()
        .latch()
        .scope(view_selector_scope_)
        .then([]() {});

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

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(view_selector_scope_)
        .when([this]() { return core_state_.projectHistory.canUndo(); })
        .then([this]() { undoProjectHistory(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(view_selector_scope_)
        .when([this]() { return core_state_.projectHistory.canRedo(); })
        .then([this]() { redoProjectHistory(); });
}

FLASHMEM bool ViewSwitcherHandler::canOpenSelector() const {
    if (overlays_state_.hasVisible()) return false;
    // Local structure-selection state owns LEFT_TOP before the global view
    // selector, independently of which performance view is visible.
    if (core_state_.trackNavigation.selection.active.get() ||
        core_state_.macroUi.pageSelection.active.get() ||
        core_state_.macroUi.slotSelection.active.get() ||
        core_state_.sequencer.structureUi.pageSelection.active.get() ||
        sequencer_step_selection_.active.get()) {
        return false;
    }
    if (active_view_.get() != core::ui::ViewType::SEQUENCER) {
        if (core::ui::isProjectWorkspaceView(active_view_.get())) {
            const auto node = project_navigation_.currentNode.get();
            return !project_navigation_.physicalHoldActive.get() &&
                   !core_state_.pages.control.audition.active() &&
                   !project_navigation_.creatingModulatorSource &&
                   !core::state::project::projectNavigationInProjectConfirmation(
                       project_navigation_
                   ) &&
                   node != core::state::project::ProjectNodeId::MODULATOR_SOURCE_RENAME;
        }
        return true;
    }

    return core::state::sequencer::isRootContentView(core_state_.sequencer) &&
           !pattern_quick_controls_.selecting.get() &&
           !step_property_inline_selector_.selecting.get() &&
           !core_state_.sequencer.stepContentSelector.selecting.get() &&
           !cc_lane_ui_.visible();
}

FLASHMEM bool ViewSwitcherHandler::beginSelectorPress() {
    if (!openSelector()) return false;
    buttons_.handoffPress(ButtonID::LEFT_TOP, view_selector_scope_);
    return true;
}

FLASHMEM bool ViewSwitcherHandler::openSelector() {
    if (core_state_.sequencer.stepContentDraft.active.get()) {
        core_state_.sequencer.stepContentDraft.noteBlockedTransition(
            core::state::sequencer::
                SequencerStepContentDraftBlockedTransition::VIEW
        );
        return false;
    }
    if (!core_state_.prepareProjectHistoryInteraction()) return false;
    const auto selected = core::state::viewSelectorItemForView(active_view_.get());
    view_selector_.selectedIndex.set(static_cast<int>(selected));

    if (!view_selector_.visible.get()) {
        overlays_.show(core::ui::OverlayType::VIEW_SELECTOR, false);
    }
    encoders_.setMode(EncoderID::NAV, oc::interface::EncoderMode::RELATIVE);
    return true;
}

FLASHMEM void ViewSwitcherHandler::navigate(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    int current = view_selector_.selectedIndex.get();
    int next = nav::nextWrappedIndex(delta, current, core::state::VIEW_SELECTOR_ITEM_COUNT);
    view_selector_.selectedIndex.set(next);
}

FLASHMEM void ViewSwitcherHandler::confirmSelection() {
    if (core_state_.sequencer.stepContentDraft.active.get()) {
        core_state_.sequencer.stepContentDraft.noteBlockedTransition(
            core::state::sequencer::
                SequencerStepContentDraftBlockedTransition::VIEW
        );
        return;
    }
    int index = view_selector_.selectedIndex.get();
    if (index < 0 || index >= core::state::VIEW_SELECTOR_ITEM_COUNT) return;

    const auto item = core::state::viewSelectorItemAt(index);
    if (!core::state::viewSelectorItemHasView(item)) return;

    auto type = core::state::viewForSelectorItem(item);
    if (item == core::state::ViewSelectorItem::MODULATORS) {
        if (active_view_.get() != core::ui::ViewType::MODULATORS ||
            project_navigation_.activeTab.get() !=
                core::state::project::ProjectTab::MODULATORS) {
            core::state::project::openProjectRootTab(
                project_navigation_,
                core::state::project::ProjectTab::MODULATORS
            );
            encoders_.setMode(
                EncoderID::OPT,
                oc::interface::EncoderMode::RELATIVE
            );
        }
    } else if (item == core::state::ViewSelectorItem::PROJECT_SETTINGS) {
        if (active_view_.get() != core::ui::ViewType::PROJECT ||
            project_navigation_.activeTab.get() ==
            core::state::project::ProjectTab::MODULATORS) {
            core::state::project::openProjectRootTab(
                project_navigation_,
                core::state::project::ProjectTab::OVERVIEW
            );
            encoders_.setMode(
                EncoderID::OPT,
                oc::interface::EncoderMode::RELATIVE
            );
        }
    }
    if (active_view_.get() == type) return;
    active_view_.set(type);
}

FLASHMEM void ViewSwitcherHandler::closeSelector() {
    overlays_.hide();
    confirmSelection();
}

FLASHMEM void ViewSwitcherHandler::undoProjectHistory() {
    (void)core_state_.undoProjectHistory();
}

FLASHMEM void ViewSwitcherHandler::redoProjectHistory() {
    (void)core_state_.redoProjectHistory();
}

}  // namespace core::handler
