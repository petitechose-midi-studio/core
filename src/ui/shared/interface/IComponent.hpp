#pragma once

#include "IWidget.hpp"

namespace UI {

/**
 * @brief Interface for UI components with explicit visibility control
 *
 * Represents UI elements that can be shown/hidden imperatively by code
 * (overlays, popups, modals, selectors, etc.).
 *
 * Components manage their own visibility state and can be toggled
 * independently from their parent view.
 *
 * Examples:
 * - DeviceSelector (overlay shown on button press)
 * - PageSelector (modal for page selection)
 * - ListOverlay (generic list popup)
 * - Dialog boxes, tooltips, context menus
 *
 * Note: Views (IView) do NOT inherit from IComponent, as their
 * lifecycle is managed by the ViewManager (onActivate/onDeactivate),
 * not by direct show/hide calls.
 */
class IComponent : public IWidget {
public:
    /**
     * @brief Show the component
     *
     * Makes the component visible. Implementation typically:
     * - Sets LVGL object visible
     * - Triggers animations
     * - Updates internal state
     */
    virtual void show() = 0;

    /**
     * @brief Hide the component
     *
     * Makes the component invisible. Implementation typically:
     * - Sets LVGL object hidden
     * - Stops animations
     * - Updates internal state
     */
    virtual void hide() = 0;

    /**
     * @brief Check if component is currently visible
     * @return true if visible, false otherwise
     */
    virtual bool isVisible() const = 0;
};

}  // namespace UI
