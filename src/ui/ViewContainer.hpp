#pragma once

/**
 * @file ViewContainer.hpp
 * @brief Main UI container with zones for views and persistent elements
 *
 * ViewContainer divides the screen into:
 * - mainZone: Primary content area (views, flex grow)
 * - bottomZone: Persistent footer (StatusBar)
 */

#include <lvgl.h>

namespace ui {

/**
 * @brief Container managing main view and bottom zones
 *
 * Mirrors the layout pattern from plugin-bitwig's ViewContainer.
 * Uses flex column layout with mainZone taking remaining space.
 */
class ViewContainer {
public:
    /**
     * @brief Create a ViewContainer as child of parent
     * @param parent Parent LVGL object (typically screen)
     */
    explicit ViewContainer(lv_obj_t* parent);
    ~ViewContainer();

    // Non-copyable, non-movable
    ViewContainer(const ViewContainer&) = delete;
    ViewContainer& operator=(const ViewContainer&) = delete;
    ViewContainer(ViewContainer&&) = delete;
    ViewContainer& operator=(ViewContainer&&) = delete;

    /// Get the main content zone (for views)
    lv_obj_t* getMainZone() const { return mainZone_; }

    /// Get the bottom zone (for StatusBar)
    lv_obj_t* getBottomZone() const { return bottomZone_; }

    /// Get the root container
    lv_obj_t* getContainer() const { return container_; }

    /// Show the container
    void show() { lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN); }

    /// Hide the container
    void hide() { lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN); }

private:
    lv_obj_t* container_{nullptr};
    lv_obj_t* mainZone_{nullptr};
    lv_obj_t* bottomZone_{nullptr};
};

}  // namespace ui
