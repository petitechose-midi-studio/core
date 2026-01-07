#include "ViewContainer.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

namespace core::ui {

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

ViewContainer::ViewContainer(lv_obj_t* parent) {
    // Root container (full screen, flex column)
    container_ = lv_obj_create(parent);
    style::apply(container_).fullSize().pad(0).bgColor(theme::color::BACKGROUND);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(container_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container_, 0, LV_STATE_DEFAULT);

    // Main zone (takes remaining space, with flex column for multi-view pattern)
    main_zone_ = lv_obj_create(container_);
    lv_obj_set_size(main_zone_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(main_zone_, 1);
    lv_obj_set_layout(main_zone_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(main_zone_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(main_zone_, 0, LV_STATE_DEFAULT);
    style::apply(main_zone_).transparent();
    lv_obj_set_style_pad_all(main_zone_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(main_zone_, 0, LV_STATE_DEFAULT);

    // Bottom zone (content height, for TransportBar)
    bottom_zone_ = lv_obj_create(container_);
    lv_obj_set_size(bottom_zone_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(bottom_zone_).transparent();
    lv_obj_set_style_pad_all(bottom_zone_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bottom_zone_, 0, LV_STATE_DEFAULT);
}

ViewContainer::~ViewContainer() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        main_zone_ = nullptr;
        bottom_zone_ = nullptr;
    }
}

}  // namespace core::ui
