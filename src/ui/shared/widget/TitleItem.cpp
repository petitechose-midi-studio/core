#include "TitleItem.hpp"

namespace UI
{

TitleItem::TitleItem(lv_obj_t *parent) : parent_(parent) {}

TitleItem::~TitleItem() {}

void TitleItem::ensureCreated()
{
    if (label_ || !parent_)
        return;

    icon_ = lv_label_create(parent_);
    lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(icon_, "");

    label_ = lv_label_create(parent_);
    lv_label_set_text(label_, "");

    indicator_ = lv_label_create(parent_);
    lv_obj_add_flag(indicator_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(indicator_, "");
}

void TitleItem::render(const TitleItemProps &props)
{
    ensureCreated();
    applyProps(props);
}

void TitleItem::applyProps(const TitleItemProps &props)
{
    if (!props.visible)
    {
        if (icon_) lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
        if (label_) lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
        if (indicator_) lv_obj_add_flag(indicator_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Icon
    if (icon_)
    {
        if (props.icon && props.icon[0] != '\0')
        {
            lv_label_set_text(icon_, props.icon);
            lv_obj_set_style_text_color(icon_, lv_color_hex(props.iconColor), 0);
            lv_obj_set_style_text_opa(icon_, props.iconOpacity, 0);
            if (props.iconFont)
                lv_obj_set_style_text_font(icon_, props.iconFont, 0);
            lv_obj_clear_flag(icon_, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Label
    if (label_)
    {
        lv_label_set_text(label_, props.text ? props.text : "");
        lv_obj_set_style_text_color(label_, lv_color_hex(props.textColor), 0);
        lv_obj_set_style_text_opa(label_, props.textOpacity, 0);
        if (props.textFont)
            lv_obj_set_style_text_font(label_, props.textFont, 0);
        lv_obj_clear_flag(label_, LV_OBJ_FLAG_HIDDEN);
    }

    // Indicator
    if (indicator_)
    {
        if (props.showIndicator && props.indicator && props.indicator[0] != '\0')
        {
            lv_label_set_text(indicator_, props.indicator);
            lv_obj_set_style_text_color(indicator_, lv_color_hex(props.indicatorColor), 0);
            if (props.iconFont)
                lv_obj_set_style_text_font(indicator_, props.iconFont, 0);
            lv_obj_clear_flag(indicator_, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(indicator_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

lv_coord_t TitleItem::getContentWidth() const
{
    lv_coord_t width = 0;
    constexpr lv_coord_t gap = 6;

    if (icon_ && !lv_obj_has_flag(icon_, LV_OBJ_FLAG_HIDDEN))
        width += lv_obj_get_width(icon_);

    if (label_ && !lv_obj_has_flag(label_, LV_OBJ_FLAG_HIDDEN))
    {
        if (width > 0) width += gap;
        width += lv_obj_get_width(label_);
    }

    if (indicator_ && !lv_obj_has_flag(indicator_, LV_OBJ_FLAG_HIDDEN))
    {
        if (width > 0) width += gap;
        width += lv_obj_get_width(indicator_);
    }

    return width;
}

} // namespace UI
