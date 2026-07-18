#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/state/StaticSignalWatcher.hpp>

#include "state/MacroEditState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::context::standalone::macro_overlay_invalidation {

inline constexpr uint32_t RENDER_EDIT = 1U << 0;
inline constexpr uint32_t RENDER_AUTOMATION = 1U << 1;
inline constexpr uint32_t RENDER_EDIT_SELECTOR = 1U << 2;
inline constexpr uint32_t RENDER_PAGE_SELECTOR = 1U << 3;
inline constexpr uint32_t RENDER_TARGET_SELECTOR = 1U << 4;
inline constexpr uint32_t PHASE_RENDER_MASK =
    RENDER_EDIT | RENDER_AUTOMATION | RENDER_EDIT_SELECTOR |
    RENDER_PAGE_SELECTOR | RENDER_TARGET_SELECTOR;

struct StateRefs {
    core::state::MacroEditState& macroEdit;
    core::state::macro::MacroPagesState& pages;
    core::state::macro::MacroUiState& macroUi;
    oc::state::Signal<uint32_t>& configRevision;
    core::state::StructureClipboardState* clipboard = nullptr;
};

/**
 * Owns the reactive invalidation graph for MacroOverlayPresenter.
 *
 * Flow phase is deliberately observed once and fans out to every projection
 * whose visibility depends on it. The specialized groups therefore only
 * observe their projection-specific data and cannot exhaust flowPhase's fixed
 * subscriber budget as more overlay modes are added.
 */
class Bindings {
public:
    using InvalidateCallback = void (*)(void* context, uint32_t renderFlags);

    [[nodiscard]] bool bind(StateRefs stateRefs,
                            void* callbackContext,
                            InvalidateCallback callback);
    void clear();

    [[nodiscard]] size_t phaseSubscriptionCount() const {
        return phase_watcher_.subscriptionCount();
    }

    [[nodiscard]] size_t subscriptionCount() const {
        return phase_watcher_.subscriptionCount() +
               clipboard_watcher_.subscriptionCount() +
               edit_watcher_.subscriptionCount() +
               automation_watcher_.subscriptionCount() +
               edit_selector_watcher_.subscriptionCount() +
               page_selector_watcher_.subscriptionCount() +
               target_selector_watcher_.subscriptionCount();
    }

private:
    void requestPhaseRenders();
    void requestClipboardRenders();
    void requestEditRender();
    void requestAutomationRender();
    void requestEditSelectorRender();
    void requestPageSelectorRender();
    void requestTargetSelectorRender();
    void invalidate(uint32_t renderFlags);

    void* callback_context_ = nullptr;
    InvalidateCallback callback_ = nullptr;
    oc::state::StaticWatchGroup<1> phase_watcher_;
    oc::state::StaticWatchGroup<1> clipboard_watcher_;
    oc::state::StaticWatchGroup<12> edit_watcher_;
    oc::state::StaticWatchGroup<12> automation_watcher_;
    oc::state::StaticWatchGroup<2> edit_selector_watcher_;
    oc::state::StaticWatchGroup<1> page_selector_watcher_;
    oc::state::StaticWatchGroup<1> target_selector_watcher_;
};

}  // namespace core::context::standalone::macro_overlay_invalidation
