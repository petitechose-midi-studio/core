#pragma once

#include "IElement.hpp"

namespace UI {

/**
 * @brief Interface for full-screen views with system-managed lifecycle
 *
 * Views represent complete screens or pages managed by the ViewManager.
 * Unlike IComponent (which has imperative show/hide control), views
 * receive lifecycle notifications (onActivate/onDeactivate) from the system.
 *
 * Views are responsible for their content only.
 * ViewManager is responsible for screen management.
 *
 * Note: Views do NOT inherit from IComponent. They have a different
 * lifecycle model:
 * - IComponent: Imperative (code calls show/hide directly)
 * - IView: Declarative (system notifies onActivate/onDeactivate)
 *
 * Examples:
 * - DeviceView (Bitwig device control screen)
 * - MixerView (hypothetical mixer screen)
 * - SettingsView (hypothetical settings screen)
 */
class IView : public IElement {
public:
    /**
     * @brief Called when the view becomes active/visible
     *
     * The ViewManager calls this when transitioning to this view.
     * The view should:
     * - Show its content (via getElement())
     * - Start any animations or updates
     * - Subscribe to necessary events
     * - Activate input handlers
     */
    virtual void onActivate() = 0;

    /**
     * @brief Called when the view becomes inactive/hidden
     *
     * The ViewManager calls this when transitioning away from this view.
     * The view should:
     * - Hide its content
     * - Stop animations or updates
     * - Unsubscribe from events
     * - Deactivate input handlers
     * - Save state if necessary
     */
    virtual void onDeactivate() = 0;

    /**
     * @brief Get unique view identifier (for logging/debug)
     * @return String identifier (e.g., "bitwig.device", "ableton.mixer")
     */
    virtual const char* getViewId() const = 0;

    /**
     * @brief Get the underlying LVGL object (from IElement)
     *
     * For views, this typically returns the main container or zone.
     * This object can be used for scoped bindings that should only
     * be active when this view is displayed.
     */
    // Inherited from IElement: virtual lv_obj_t* getElement() const = 0;
};

}  // namespace UI
