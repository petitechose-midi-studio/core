#include "ViewContainer.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

namespace ui {

namespace Theme = oc::ui::lvgl::BaseTheme;
namespace style = oc::ui::lvgl::style;

ViewContainer::ViewContainer(lv_obj_t* parent) {
    // Root container (full screen, flex column)
    container_ = lv_obj_create(parent);
    style::apply(container_).fullSize().pad(0).bgColor(Theme::Color::BACKGROUND);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(container_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container_, 0, LV_STATE_DEFAULT);

    // Top zone (fixed height, for TopBar)
    topZone_ = lv_obj_create(container_);
    lv_obj_set_size(topZone_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(topZone_).transparent();
    lv_obj_set_style_pad_all(topZone_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(topZone_, 0, LV_STATE_DEFAULT);

    // Main zone (takes remaining space)
    mainZone_ = lv_obj_create(container_);
    lv_obj_set_size(mainZone_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(mainZone_, 1);
    style::apply(mainZone_).transparent();
    lv_obj_set_style_pad_all(mainZone_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(mainZone_, 0, LV_STATE_DEFAULT);

    // Bottom zone (content height, for StatusBar)
    bottomZone_ = lv_obj_create(container_);
    lv_obj_set_size(bottomZone_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(bottomZone_).transparent();
    lv_obj_set_style_pad_all(bottomZone_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bottomZone_, 0, LV_STATE_DEFAULT);
}

ViewContainer::~ViewContainer() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        topZone_ = nullptr;
        mainZone_ = nullptr;
        bottomZone_ = nullptr;
    }
}

}  // namespace ui
