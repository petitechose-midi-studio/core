#pragma once

#include <array>

#include "handler/common/NavigationUtils.hpp"

namespace core::handler::modal {

/**
 * Small modal-navigation helpers shared by retained settings and picker handlers.
 *
 * These helpers only advance wrapped selections and close overlays; callers own
 * validation, command execution, and state-specific side effects.
 */
inline bool advanceWrappedSelection(float delta,
                                    bool visible,
                                    int current,
                                    int count,
                                    int& next) {
    if (!visible || !nav::hasTurnDelta(delta) || count <= 0) {
        return false;
    }

    next = nav::nextWrappedIndex(delta, current, count);
    return true;
}

template <typename SelectorState>
inline bool advanceWrappedSelection(float delta,
                                    const SelectorState& selector,
                                    int count,
                                    int& next) {
    return advanceWrappedSelection(
        delta,
        selector.visible.get(),
        selector.selectedIndex.get(),
        count,
        next
    );
}

template <typename OverlayManager, typename ResetFn>
inline void hideOverlayAndReset(OverlayManager& overlays, ResetFn&& reset) {
    overlays.hide();
    reset();
}

template <typename OverlayManager, typename SelectorState>
inline void hideOverlayAndResetSelector(OverlayManager& overlays, SelectorState& selector) {
    hideOverlayAndReset(overlays, [&selector]() { selector.reset(); });
}

template <typename OverlayManager, typename OverlayEnum>
inline bool hideIfCurrent(OverlayManager& overlays, OverlayEnum overlay) {
    if (overlays.current() != overlay) {
        return false;
    }
    overlays.hide();
    return true;
}

template <typename OverlayManager, typename OverlayEnum, std::size_t N>
inline void hideWhileCurrentIn(OverlayManager& overlays,
                               const std::array<OverlayEnum, N>& overlaysToClose) {
    while (true) {
        const auto current = overlays.current();
        bool shouldHide = false;
        for (OverlayEnum overlay : overlaysToClose) {
            if (current == overlay) {
                shouldHide = true;
                break;
            }
        }

        if (!shouldHide) {
            return;
        }

        overlays.hide();
    }
}

template <typename OverlayManager, typename OverlayEnum, typename SelectorState, typename InitFn>
inline void openSelectorOverlay(OverlayManager& overlays,
                                OverlayEnum overlay,
                                SelectorState& selector,
                                int selectedIndex,
                                InitFn&& init) {
    selector.reset();
    init(selector);
    selector.selectedIndex.set(selectedIndex);
    overlays.show(overlay, true);
}

template <typename OverlayManager, typename OverlayEnum, typename SelectorState>
inline void openSelectorOverlay(OverlayManager& overlays,
                                OverlayEnum overlay,
                                SelectorState& selector,
                                int selectedIndex) {
    openSelectorOverlay(
        overlays,
        overlay,
        selector,
        selectedIndex,
        [](SelectorState&) {}
    );
}

}  // namespace core::handler::modal
