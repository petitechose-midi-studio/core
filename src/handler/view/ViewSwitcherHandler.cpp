#include "ViewSwitcherHandler.hpp"

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"
#include "state/CoreState.hpp"
#include "state/ViewSelectorItems.hpp"
#include "state/project/ProjectMenuModel.hpp"

namespace core::handler {

using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

FLASHMEM ViewSwitcherHandler::ViewSwitcherHandler(
    core::state::CoreState& state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    ViewSwitcherHandler::ViewScopes viewScopes,
    oc::type::ScopeID viewSelectorScope
)
    : core_state_(state)
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
                               core_state_.projectNavigation
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
        .when([this]() {
            return core_state_.projectHistory.canUndo() &&
                core_state_.projectHistoryBlockReason() ==
                    core::state::project::ProjectHistoryBlockReason::NONE;
        })
        .then([this]() { (void)core_state_.undoProjectHistory(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(view_selector_scope_)
        .when([this]() {
            return core_state_.projectHistory.canRedo() &&
                core_state_.projectHistoryBlockReason() ==
                    core::state::project::ProjectHistoryBlockReason::NONE;
        })
        .then([this]() { (void)core_state_.redoProjectHistory(); });
}

FLASHMEM bool ViewSwitcherHandler::canOpenSelector() const {
    if (core_state_.overlays.hasVisible()) return false;

    // The global selector is a standalone gesture, never a chord. Opening an
    // overlay while another press-owned action is active would quarantine its
    // release and strand the local workflow.
    constexpr auto leftTop = static_cast<oc::type::ButtonID>(ButtonID::LEFT_TOP);
    for (oc::type::ButtonID button = 0; button < oc::MAX_BUTTONS; ++button) {
        if (button != leftTop && buttons_.isPressed(button)) return false;
    }

    if (core_state_.projectHistoryBlockReason() !=
        core::state::project::ProjectHistoryBlockReason::NONE) return false;

    // Back in a Pattern still belongs to its local route, even without a draft.
    return core_state_.activeView.get() != core::ui::ViewType::CLIPS ||
           core_state_.sequencer.clipWorkspace.matrixVisible();
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
    const auto selected = core::state::viewSelectorItemForView(
        core_state_.activeView.get()
    );
    core_state_.viewSelector.selectedIndex.set(static_cast<int>(selected));

    if (!core_state_.viewSelector.visible.get()) {
        overlays_.show(core::ui::OverlayType::VIEW_SELECTOR, false);
    }
    encoders_.setMode(EncoderID::NAV, oc::interface::EncoderMode::RELATIVE);
    return true;
}

FLASHMEM void ViewSwitcherHandler::navigate(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = core_state_.viewSelector.selectedIndex.get();
    const int next = nav::nextWrappedIndex(
        delta,
        current,
        core::state::VIEW_SELECTOR_ITEM_COUNT
    );
    core_state_.viewSelector.selectedIndex.set(next);
}

FLASHMEM void ViewSwitcherHandler::confirmSelection() {
    if (core_state_.sequencer.stepContentDraft.active.get()) {
        core_state_.sequencer.stepContentDraft.noteBlockedTransition(
            core::state::sequencer::
                SequencerStepContentDraftBlockedTransition::VIEW
        );
        return;
    }
    const int index = core_state_.viewSelector.selectedIndex.get();
    if (index < 0 || index >= core::state::VIEW_SELECTOR_ITEM_COUNT) return;

    const auto item = core::state::viewSelectorItemAt(index);
    if (!core::state::viewSelectorItemHasView(item)) return;

    const auto type = core::state::viewForSelectorItem(item);
    if (item == core::state::ViewSelectorItem::MODULATORS) {
        if (core_state_.activeView.get() != core::ui::ViewType::MODULATORS ||
            core_state_.projectNavigation.activeTab.get() !=
                core::state::project::ProjectTab::MODULATORS) {
            core::state::project::openProjectRootTab(
                core_state_.projectNavigation,
                core::state::project::ProjectTab::MODULATORS
            );
            encoders_.setMode(
                EncoderID::OPT,
                oc::interface::EncoderMode::RELATIVE
            );
        }
    } else if (item == core::state::ViewSelectorItem::PROJECT_SETTINGS) {
        if (core_state_.activeView.get() != core::ui::ViewType::PROJECT ||
            core_state_.projectNavigation.activeTab.get() ==
            core::state::project::ProjectTab::MODULATORS) {
            core::state::project::openProjectRootTab(
                core_state_.projectNavigation,
                core::state::project::ProjectTab::OVERVIEW
            );
            encoders_.setMode(
                EncoderID::OPT,
                oc::interface::EncoderMode::RELATIVE
            );
        }
    }
    if (core_state_.activeView.get() == type) return;
    core_state_.activeView.set(type);
}

FLASHMEM void ViewSwitcherHandler::closeSelector() {
    overlays_.hide();
    confirmSelection();
}

}  // namespace core::handler
