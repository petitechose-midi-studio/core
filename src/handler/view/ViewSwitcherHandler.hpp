#pragma once

/**
 * @file ViewSwitcherHandler.hpp
 * @brief Handles top-level view switching via LEFT_TOP selector overlay
 */

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "state/project/ProjectHistoryAccess.hpp"

namespace core::state {
struct ViewSelectorState;
}

namespace core::state::project {
struct ProjectNavigationState;
struct ProjectHistoryCoordinator;
}

namespace core::state::sequencer {
struct ClipWorkspaceUiState;
struct SequencerStepContentDraftSession;
}

namespace core::handler {

class ViewSwitcherHandler {
public:
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<oc::type::ScopeID, VIEW_SCOPE_COUNT>;

    /**
     * @brief Narrow state surface used by the global view selector
     *
     * Project-history operations stay owned by CoreState; composition supplies
     * allocation-free thunks so this handler never sees the full aggregate.
     */
    struct Refs {
        struct HistoryOps {
            void* context = nullptr;
            core::state::project::ProjectHistoryBlockReason (*blockReason)(void*) = nullptr;
            bool (*prepareInteraction)(void*) = nullptr;
            bool (*undo)(void*) = nullptr;
            bool (*redo)(void*) = nullptr;
        };

        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::project::ProjectNavigationState& projectNavigation;
        core::state::project::ProjectHistoryCoordinator& projectHistory;
        core::state::sequencer::SequencerStepContentDraftSession& stepContentDraft;
        core::state::sequencer::ClipWorkspaceUiState& clipWorkspace;
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::ViewSelectorState& viewSelector;
        HistoryOps historyOps{};
    };

    ViewSwitcherHandler(
        Refs refs,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        ViewScopes viewScopes,
        oc::type::ScopeID viewSelectorScope
    );

    ~ViewSwitcherHandler() = default;

    ViewSwitcherHandler(const ViewSwitcherHandler&) = delete;
    ViewSwitcherHandler& operator=(const ViewSwitcherHandler&) = delete;

private:
    void setupBindings();
    bool canOpenSelector() const;

    bool beginSelectorPress();
    [[nodiscard]] bool openSelector();
    void navigate(float delta);
    void confirmSelection();
    void closeSelector();

    Refs refs_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    ViewScopes view_scopes_{};
    oc::type::ScopeID view_selector_scope_ = 0;
};

}  // namespace core::handler
