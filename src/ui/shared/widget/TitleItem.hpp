#pragma once

#include <lvgl.h>
#include <cstring>

namespace UI
{

struct TitleItemProps
{
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

    bool operator==(const TitleItemProps &other) const
    {
        return std::strcmp(text, other.text) == 0 &&
               icon == other.icon &&
               iconFont == other.iconFont &&
               textFont == other.textFont &&
               iconColor == other.iconColor &&
               textColor == other.textColor &&
               textOpacity == other.textOpacity &&
               iconOpacity == other.iconOpacity &&
               showIndicator == other.showIndicator &&
               indicator == other.indicator &&
               indicatorColor == other.indicatorColor &&
               visible == other.visible;
    }

    bool operator!=(const TitleItemProps &other) const
    {
        return !(*this == other);
    }
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
    void forceRender(const TitleItemProps &props);
    lv_coord_t getContentWidth() const;
    bool isCreated() const { return label_ != nullptr; }

private:
    void ensureCreated();
    void applyProps(const TitleItemProps &props);

    lv_obj_t *parent_ = nullptr;
    lv_obj_t *icon_ = nullptr;
    lv_obj_t *label_ = nullptr;
    lv_obj_t *indicator_ = nullptr;

    TitleItemProps lastProps_{};
    bool firstRender_ = true;
};

} // namespace UI
