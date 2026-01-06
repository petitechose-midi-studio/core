#include "LayoutOverlay.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

namespace core::ui {

using namespace oc::ui::lvgl;
namespace style = oc::ui::lvgl::style;

namespace {
constexpr uint8_t OVERLAY_BG_OPACITY = 230;  // ~90% opacity
}

LayoutOverlay::LayoutOverlay(lv_obj_t* parent) : parent_(parent) {
    // Fullscreen overlay with semi-transparent background
    overlay_ = lv_obj_create(parent_);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_FLOATING);
    style::apply(overlay_)
        .fullSize()
        .bgColor(BaseTheme::Color::BACKGROUND, OVERLAY_BG_OPACITY)
        .noScroll()
        .noBorder();
    lv_obj_align(overlay_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    // Container with flex column layout
    container_ = lv_obj_create(overlay_);
    style::apply(container_).fullSize().transparent().noScroll();
    lv_obj_align(container_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container_, BaseTheme::Layout::ROW_GAP_MD, LV_STATE_DEFAULT);

    // Header slot (auto-height, collapses when empty)
    header_ = lv_obj_create(container_);
    lv_obj_set_size(header_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(header_).transparent().noScroll().pad(0);

    // Content slot (flex-grow to fill available space)
    content_ = lv_obj_create(container_);
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_flex_grow(content_, 1);
    style::apply(content_).transparent().noScroll().pad(0);

    // Footer slot (auto-height, collapses when empty)
    footer_ = lv_obj_create(container_);
    lv_obj_set_size(footer_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(footer_).transparent().noScroll().pad(0);
}

LayoutOverlay::~LayoutOverlay() {
    if (overlay_) {
        lv_obj_delete(overlay_);
        overlay_ = nullptr;
    }
}

void LayoutOverlay::showHeader(bool show) {
    if (header_) {
        if (show) {
            lv_obj_clear_flag(header_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(header_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void LayoutOverlay::showFooter(bool show) {
    if (footer_) {
        if (show) {
            lv_obj_clear_flag(footer_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(footer_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void LayoutOverlay::show() {
    if (overlay_) {
        lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
        visible_ = true;
    }
}

void LayoutOverlay::hide() {
    if (overlay_) {
        lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
        visible_ = false;
    }
}

}  // namespace core::ui
