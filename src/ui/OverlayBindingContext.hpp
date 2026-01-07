#pragma once

/**
 * @file OverlayBindingContext.hpp
 * @brief Context struct for handlers that manage overlay-based selection UI
 *
 * Groups overlay-related dependencies that always travel together to reduce
 * handler constructor parameter count.
 */

#include <lvgl.h>

#include "OverlayController.hpp"

namespace core::ui {

/**
 * @brief Context for handlers that manage overlay-based selection UI
 *
 * Groups overlay-related dependencies that always travel together:
 * - OverlayController for show/hide management
 * - scopeElement for scoped input bindings (nullptr = global scope)
 * - overlayElement for positioning/rendering the overlay
 *
 * Usage in handlers:
 * @code
 * class MyOverlayHandler {
 * public:
 *     MyOverlayHandler(OverlayBindingContext<MyOverlayType>& ctx)
 *         : overlayCtx_(ctx) {
 *         ctx.controller.show(MyOverlayType::SELECTOR);
 *         // Use ctx.scopeElement for scoped bindings
 *         // Use ctx.overlayElement for UI positioning
 *     }
 * private:
 *     OverlayBindingContext<MyOverlayType>& overlayCtx_;
 * };
 * @endcode
 */
template <typename OverlayEnumT>
struct OverlayBindingContext {
    OverlayController<OverlayEnumT>& controller;
    lv_obj_t* scopeElement;    ///< Element for scoped input bindings (nullptr = global)
    lv_obj_t* overlayElement;  ///< Element for overlay UI positioning
};

}  // namespace core::ui
