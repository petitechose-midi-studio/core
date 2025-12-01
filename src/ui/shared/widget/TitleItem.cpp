#include "TitleItem.hpp"

TitleItem::TitleItem(lv_obj_t *parent) {
    if (!parent) return;

    // Create container with flex row layout
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);

    // Flex row with gap
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container_, 6, 0);  // Gap between elements

    // Create children inside container
    icon_ = lv_label_create(container_);
    lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(icon_, "");

    label_ = lv_label_create(container_);
    lv_label_set_text(label_, "");

    indicator_ = lv_label_create(container_);
    lv_obj_add_flag(indicator_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(indicator_, "");
}

TitleItem::~TitleItem() {
    // LVGL auto-deletes children when parent is deleted
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        icon_ = nullptr;
        label_ = nullptr;
        indicator_ = nullptr;
    }
}

void TitleItem::render(const TitleItemProps &props) {
    applyProps(props);
}

void TitleItem::applyProps(const TitleItemProps &props) {
    if (!container_) return;

    if (!props.visible) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);

    // Icon
    if (icon_) {
        if (props.icon && props.icon[0] != '\0') {
            lv_label_set_text(icon_, props.icon);
            lv_obj_set_style_text_color(icon_, lv_color_hex(props.iconColor), 0);
            lv_obj_set_style_text_opa(icon_, props.iconOpacity, 0);
            if (props.iconFont)
                lv_obj_set_style_text_font(icon_, props.iconFont, 0);
            lv_obj_clear_flag(icon_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Label
    if (label_) {
        lv_label_set_text(label_, props.text ? props.text : "");
        lv_obj_set_style_text_color(label_, lv_color_hex(props.textColor), 0);
        lv_obj_set_style_text_opa(label_, props.textOpacity, 0);
        if (props.textFont)
            lv_obj_set_style_text_font(label_, props.textFont, 0);
        lv_obj_clear_flag(label_, LV_OBJ_FLAG_HIDDEN);
    }

    // Indicator
    if (indicator_) {
        if (props.showIndicator && props.indicator && props.indicator[0] != '\0') {
            lv_label_set_text(indicator_, props.indicator);
            lv_obj_set_style_text_color(indicator_, lv_color_hex(props.indicatorColor), 0);
            if (props.iconFont)
                lv_obj_set_style_text_font(indicator_, props.iconFont, 0);
            lv_obj_clear_flag(indicator_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(indicator_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

lv_coord_t TitleItem::getContentWidth() const {
    if (!container_) return 0;
    lv_obj_update_layout(container_);
    return lv_obj_get_width(container_);
}
