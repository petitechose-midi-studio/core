#pragma once

#include <lvgl.h>

#include <oc/ui/lvgl/IView.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

/**
 * @brief Empty view with solid background color
 *
 * Default view for contexts - just a solid background.
 * Use as placeholder or base layer.
 *
 * Note: Must be created after LVGL is initialized.
 */
class EmptyView : public oc::ui::lvgl::IView {
public:
    explicit EmptyView(lv_obj_t* parent, uint32_t color = oc::ui::lvgl::BaseTheme::Color::BACKGROUND)
        : color_(color) {
        container_ = lv_obj_create(parent);
        lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(container_, lv_color_hex(color_), 0);
        lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(container_, 0, 0);
        lv_obj_set_style_pad_all(container_, 0, 0);
    }

    ~EmptyView() override {
        if (container_) {
            lv_obj_delete(container_);
        }
    }

    void onActivate() override {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }

    void onDeactivate() override {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }

    const char* getViewId() const override { return "core.empty"; }
    lv_obj_t* getElement() const override { return container_; }

private:
    lv_obj_t* container_;
    uint32_t color_;
};
