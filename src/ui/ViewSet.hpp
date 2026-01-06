#pragma once

#include <memory>
#include <tuple>

#include <lvgl.h>

#include <oc/ui/lvgl/IView.hpp>

namespace core::ui {

/**
 * @brief Type-safe container for mutually exclusive views
 *
 * Only one view can be active at a time. Switching views automatically
 * handles deactivate/hide/show/activate transitions.
 *
 * Usage:
 * @code
 * ViewSet<MainView, SettingsView, DeviceView> views_;
 *
 * void initialize() {
 *     views_.create<MainView>(screen);
 *     views_.create<SettingsView>(screen);
 *     views_.show<MainView>();
 * }
 *
 * void openSettings() {
 *     views_.show<SettingsView>();
 * }
 * @endcode
 */
template <typename... Views>
class ViewSet {
public:
    ViewSet() = default;
    ~ViewSet() = default;

    // Non-copyable, movable
    ViewSet(const ViewSet&) = delete;
    ViewSet& operator=(const ViewSet&) = delete;
    ViewSet(ViewSet&&) = default;
    ViewSet& operator=(ViewSet&&) = default;

    /**
     * @brief Create a view of type V
     * @return Reference to the created view
     */
    template <typename V, typename... Args>
    V& create(lv_obj_t* screen, Args&&... args) {
        auto& ptr = std::get<std::unique_ptr<V>>(views_);
        ptr = std::make_unique<V>(screen, std::forward<Args>(args)...);
        return *ptr;
    }

    /**
     * @brief Get a view by type
     * @return Reference to the view
     */
    template <typename V>
    V& get() {
        return *std::get<std::unique_ptr<V>>(views_);
    }

    /**
     * @brief Get a view by type (const)
     */
    template <typename V>
    const V& get() const {
        return *std::get<std::unique_ptr<V>>(views_);
    }

    /**
     * @brief Check if a view exists
     */
    template <typename V>
    bool has() const {
        return std::get<std::unique_ptr<V>>(views_) != nullptr;
    }

    /**
     * @brief Show a view (hide current, show new, activate)
     *
     * Handles full transition:
     * 1. Deactivate current view
     * 2. Hide current view
     * 3. Show new view
     * 4. Activate new view
     */
    template <typename V>
    void show() {
        auto* newView = std::get<std::unique_ptr<V>>(views_).get();
        if (!newView || newView == current_) return;

        // Deactivate + hide current
        if (current_) {
            current_->onDeactivate();
            lv_obj_add_flag(current_->getElement(), LV_OBJ_FLAG_HIDDEN);
        }

        // Show + activate new
        current_ = newView;
        lv_obj_clear_flag(current_->getElement(), LV_OBJ_FLAG_HIDDEN);
        current_->onActivate();
    }

    /**
     * @brief Hide all views
     */
    void hideAll() {
        if (current_) {
            current_->onDeactivate();
            lv_obj_add_flag(current_->getElement(), LV_OBJ_FLAG_HIDDEN);
            current_ = nullptr;
        }
    }

    /**
     * @brief Get currently active view (may be nullptr)
     */
    oc::ui::lvgl::IView* current() const { return current_; }

private:
    std::tuple<std::unique_ptr<Views>...> views_;
    oc::ui::lvgl::IView* current_ = nullptr;
};

}  // namespace core::ui
