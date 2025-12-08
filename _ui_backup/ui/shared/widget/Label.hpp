#pragma once

#include <string>

#include <lvgl.h>

#include "interface/IWidget.hpp"

/**
 * @brief Smart label widget with overflow handling and auto-scroll
 *
 * Features:
 * - Auto-scroll animation when text overflows
 * - Flex-grow support for layout integration
 *
 * Usage:
 *   Label label(parent);
 *   label.setText("My long text that might overflow");
 */
class Label : public IWidget {
public:
    explicit Label(lv_obj_t *parent);
    ~Label();

    // Non-copyable
    Label(const Label &) = delete;
    Label &operator=(const Label &) = delete;

    // Movable
    Label(Label &&other) noexcept;
    Label &operator=(Label &&other) noexcept;

    void setText(const std::string &text);
    void setText(const char *text);
    void setColor(lv_color_t color);
    void setFont(const lv_font_t *font);

    lv_obj_t *getContainer() const { return container_; }
    lv_obj_t *getLabel() const { return label_; }
    lv_obj_t *getElement() const override { return container_; }

    void setAutoScroll(bool enabled) { auto_scroll_enabled_ = enabled; }
    void setFlexGrow(bool enabled);
    void setAlignment(lv_text_align_t align) { alignment_ = align; }
private:
    void createWidgets(lv_obj_t *parent);
    void destroyWidgets();
    void checkOverflowAndScroll();
    void startScrollAnimation();
    void stopScrollAnimation();

    static void scrollAnimCallback(void *var, int32_t value);
    static void pauseTimerCallback(lv_timer_t *timer);

    lv_obj_t *container_ = nullptr;
    lv_obj_t *label_ = nullptr;
    lv_anim_t scroll_anim_;

    bool auto_scroll_enabled_ = true;
    bool anim_running_ = false;
    lv_coord_t overflow_amount_ = 0;
    lv_text_align_t alignment_ = LV_TEXT_ALIGN_LEFT;

    uint32_t scroll_duration_ms_ = 2000;
    uint32_t pause_duration_ms_ = 1000;
};
