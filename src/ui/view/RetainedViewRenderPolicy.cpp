#include "ui/view/RetainedViewRenderPolicy.hpp"

#include <oc/ui/lvgl/RetainedSurfaceParkingLot.hpp>

namespace core::ui {

void RetainedViewRenderPolicy::initializeHidden(lv_obj_t* root) {
    if (!root) return;

    // These roots are full-size overlapping surfaces, not layout siblings.
    // Set IGNORE_LAYOUT first so toggling HIDDEN never dirties their parent.
    lv_obj_add_flag(root, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
}

bool RetainedViewRenderPolicy::visible(lv_obj_t* root) {
    return root != nullptr && !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN);
}

bool RetainedViewRenderPolicy::renderable(lv_obj_t* root, bool blocked) {
    return visible(root) && !blocked;
}

void RetainedViewRenderPolicy::attach(lv_obj_t* root, lv_obj_t* parent) {
    oc::ui::lvgl::RetainedSurfaceParkingLot::attach(root, parent);
}

void RetainedViewRenderPolicy::show(lv_obj_t* root) {
    if (root) lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
}

void RetainedViewRenderPolicy::hide(lv_obj_t* root) {
    if (root) lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace core::ui
