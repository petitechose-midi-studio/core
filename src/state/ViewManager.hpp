#pragma once

/**
 * @file ViewManager.hpp
 * @brief Template view manager with reactive signals
 *
 * Manages view switching for mutually exclusive views.
 * Lives in state/ because it manages reactive state (Signal<EnumT>).
 *
 * @tparam EnumT View enum type (must have COUNT member)
 * @tparam DefaultValue Default enum value for initialization
 */

#include <array>
#include <cstdint>

#include <oc/log/Log.hpp>
#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/IView.hpp>

namespace core::state {

using oc::state::Signal;
using oc::ui::lvgl::IView;

/**
 * @brief Template view manager for mutually exclusive views
 *
 * Usage:
 * @code
 * // In BitwigState.hpp
 * core::state::ViewManager<ViewType, ViewType::REMOTE_CONTROLS> views;
 *
 * // Register views
 * state_.views.registerView(ViewType::REMOTE_CONTROLS, view);
 * state_.views.initialize();
 *
 * // Subscribe to changes
 * state_.views.currentView().subscribe([](ViewType type) { ... });
 * @endcode
 */
template <typename EnumT, EnumT DefaultValue = static_cast<EnumT>(0)>
class ViewManager {
    static constexpr size_t COUNT = static_cast<size_t>(EnumT::COUNT);

public:
    ViewManager() = default;

    // Non-copyable, non-movable (holds view references)
    ViewManager(const ViewManager&) = delete;
    ViewManager& operator=(const ViewManager&) = delete;
    ViewManager(ViewManager&&) = delete;
    ViewManager& operator=(ViewManager&&) = delete;

    // =========================================================================
    // Registration
    // =========================================================================

    /**
     * @brief Register a view for a given type
     * @param type The view type
     * @param view Pointer to the view (must outlive ViewManager)
     */
    void registerView(EnumT type, IView* view) {
        auto idx = static_cast<size_t>(type);
        if (idx < COUNT) {
            views_[idx] = view;
            OC_LOG_DEBUG("[ViewManager] Registered view '{}' for type {}",
                         view ? view->getViewId() : "null", static_cast<int>(type));
        }
    }

    // =========================================================================
    // Navigation
    // =========================================================================

    /**
     * @brief Switch to a specific view
     * @param type The view to switch to
     */
    void switchTo(EnumT type) {
        if (static_cast<size_t>(type) >= COUNT) return;
        if (type == current_.get()) return;

        auto oldIdx = static_cast<size_t>(current_.get());
        auto newIdx = static_cast<size_t>(type);

        // Deactivate current view
        if (views_[oldIdx] != nullptr) {
            views_[oldIdx]->onDeactivate();
            OC_LOG_DEBUG("[ViewManager] Deactivated '{}'", views_[oldIdx]->getViewId());
        }

        // Activate new view
        if (views_[newIdx] != nullptr) {
            views_[newIdx]->onActivate();
            OC_LOG_DEBUG("[ViewManager] Activated '{}'", views_[newIdx]->getViewId());
        }

        current_.set(type);
        OC_LOG_INFO("[ViewManager] Switched to view type={}", static_cast<int>(type));
    }

    /**
     * @brief Cycle to next view
     */
    void next() {
        auto idx = static_cast<size_t>(current_.get());
        auto nextIdx = (idx + 1) % COUNT;
        switchTo(static_cast<EnumT>(nextIdx));
    }

    /**
     * @brief Cycle to previous view
     */
    void previous() {
        auto idx = static_cast<size_t>(current_.get());
        auto prevIdx = (idx + COUNT - 1) % COUNT;
        switchTo(static_cast<EnumT>(prevIdx));
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * @brief Get current view type (reactive signal)
     */
    Signal<EnumT>& currentView() { return current_; }
    const Signal<EnumT>& currentView() const { return current_; }

    /**
     * @brief Get current view type value
     */
    EnumT current() const { return current_.get(); }

    /**
     * @brief Get view pointer for a type
     */
    IView* getView(EnumT type) const {
        auto idx = static_cast<size_t>(type);
        return (idx < COUNT) ? views_[idx] : nullptr;
    }

    /**
     * @brief Get current view pointer
     */
    IView* currentViewPtr() const {
        return getView(current_.get());
    }

    /**
     * @brief Initialize with first registered view active
     * Call after all views are registered
     */
    void initialize() {
        // Find first registered view and activate it
        for (size_t i = 0; i < COUNT; ++i) {
            if (views_[i] != nullptr) {
                auto type = static_cast<EnumT>(i);
                views_[i]->onActivate();
                current_.set(type);
                OC_LOG_INFO("[ViewManager] Initialized with view type={}", static_cast<int>(type));
                return;
            }
        }
        OC_LOG_WARN("[ViewManager] No views registered during initialization");
    }

    /**
     * @brief Reset all views (deactivate and clear)
     */
    void reset() {
        auto idx = static_cast<size_t>(current_.get());
        if (views_[idx] != nullptr) {
            views_[idx]->onDeactivate();
        }
        for (auto& view : views_) {
            view = nullptr;
        }
        current_.set(DefaultValue);
    }

private:
    std::array<IView*, COUNT> views_{};
    Signal<EnumT> current_{DefaultValue};
};

}  // namespace core::state
