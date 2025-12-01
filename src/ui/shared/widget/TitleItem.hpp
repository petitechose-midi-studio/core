#pragma once

#include <lvgl.h>

struct TitleItemProps {
    const char *text = "";
    const char *icon = nullptr;
    const lv_font_t *iconFont = nullptr;
    const lv_font_t *textFont = nullptr;
    uint32_t iconColor = 0xFFFFFF;
    uint32_t textColor = 0xFFFFFF;
    lv_opa_t textOpacity = LV_OPA_COVER;
    lv_opa_t iconOpacity = LV_OPA_COVER;
    bool showIndicator = false;
    const char *indicator = nullptr;
    uint32_t indicatorColor = 0x888888;
    bool visible = true;
};

/**
 * Pure UI: [icon?] [label] [indicator?]
 * Creates children directly in parent. Uses render(Props) pattern.
 */
class TitleItem
{
public:
    explicit TitleItem(lv_obj_t *parent);
    ~TitleItem();

    void render(const TitleItemProps &props);
    lv_coord_t getContentWidth() const;
    bool isCreated() const { return label_ != nullptr; }

private:
    void applyProps(const TitleItemProps &props);

    lv_obj_t *parent_ = nullptr;
    lv_obj_t *icon_ = nullptr;
    lv_obj_t *label_ = nullptr;
    lv_obj_t *indicator_ = nullptr;
};
