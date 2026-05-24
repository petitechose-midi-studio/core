#include "ContextSoftkeyBar.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;

ContextSoftkeyBar::ContextSoftkeyBar(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, LV_PCT(100), theme::layout::TRANSPORT_BAR_HEIGHT);
    style::apply(container_).bgColor(theme::color::BACKGROUND);

    static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(container_, col_dsc, row_dsc);
    lv_obj_set_layout(container_, LV_LAYOUT_GRID);

    left_label_ = lv_label_create(container_);
    lv_obj_set_grid_cell(left_label_, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_style_text_color(left_label_, lv_color_hex(theme::color::TEXT_SECONDARY), 0);
    lv_obj_set_style_pad_left(left_label_, theme::layout::PAD_MD, 0);

    center_label_ = lv_label_create(container_);
    lv_obj_set_grid_cell(center_label_, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_style_text_color(center_label_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);

    right_label_ = lv_label_create(container_);
    lv_obj_set_grid_cell(right_label_, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_style_text_color(right_label_, lv_color_hex(theme::color::TEXT_SECONDARY), 0);
    lv_obj_set_style_pad_right(right_label_, theme::layout::PAD_MD, 0);

    hide();
}

ContextSoftkeyBar::~ContextSoftkeyBar() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        left_label_ = nullptr;
        center_label_ = nullptr;
        right_label_ = nullptr;
    }
}

void ContextSoftkeyBar::setLabels(const char* left, const char* center, const char* right) {
    lv_obj_set_style_text_font(left_label_, fonts.inter_13_medium, 0);
    lv_obj_set_style_text_font(center_label_, fonts.inter_13_medium, 0);
    lv_obj_set_style_text_font(right_label_, fonts.inter_13_medium, 0);
    lv_label_set_text(left_label_, left ? left : "");
    lv_label_set_text(center_label_, center ? center : "");
    lv_label_set_text(right_label_, right ? right : "");
}

void ContextSoftkeyBar::setLeftIcon(const char* icon) {
    standalone::icons::set(left_label_, icon ? icon : "", standalone::icons::Size::M);
    lv_obj_set_style_text_font(center_label_, fonts.inter_13_medium, 0);
    lv_obj_set_style_text_font(right_label_, fonts.inter_13_medium, 0);
    lv_label_set_text(center_label_, "");
    lv_label_set_text(right_label_, "");
}

void ContextSoftkeyBar::show() {
    if (container_) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ContextSoftkeyBar::hide() {
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace core::ui
