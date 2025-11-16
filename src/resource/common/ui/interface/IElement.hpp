#pragma once

#include "lvgl.h"

namespace UI {

/**
 * @brief Base interface for all UI elements backed by LVGL objects
 *
 * Provides access to the underlying LVGL object for:
 * - Scoped input bindings (ControllerAPI)
 * - Direct LVGL manipulation when needed
 * - Parent-child relationships
 *
 * All UI elements (widgets, components, views) must implement this interface.
 */
class IElement {
public:
    virtual ~IElement() = default;

    /**
     * @brief Get the underlying LVGL object
     * @return LVGL object pointer, or nullptr if not created/destroyed
     *
     * This object can be used as a scope for ControllerAPI bindings,
     * ensuring inputs are only active when this element is visible.
     *
     * For composite elements, this should return the top-level container.
     */
    virtual lv_obj_t* getElement() const = 0;
};

}  // namespace UI
