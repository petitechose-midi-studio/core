#pragma once

#include <lvgl.h>

namespace core::ui {

/**
 * Shared scheduling contract for retained top-level views.
 *
 * A retained root is attached to the active host only while its view is
 * selected. Inactive roots are hidden and moved to dedicated off-screen mounts
 * that mirror their active viewport, so an active-screen layout pass cannot
 * traverse them and reactivation can preserve child geometry. IGNORE_LAYOUT
 * keeps host reparenting independent from the active host layout. Hidden views
 * keep their dirty state but never wake their render timer.
 */
class RetainedViewRenderPolicy {
public:
    static void initializeHidden(lv_obj_t* root);
    static bool visible(lv_obj_t* root);
    static bool renderable(lv_obj_t* root, bool blocked = false);
    static void attach(lv_obj_t* root, lv_obj_t* parent);
    static void show(lv_obj_t* root);
    static void hide(lv_obj_t* root);
};

}  // namespace core::ui
