#pragma once

#include <lvgl.h>

namespace core::ui {

/**
 * Temporary bottom bar used by modal contexts for softkey labels.
 *
 * It owns only three labels and visibility. Modal presenters decide when it
 * replaces the transport bar and what text to display.
 */
class ContextSoftkeyBar {
public:
    explicit ContextSoftkeyBar(lv_obj_t* parent);
    ~ContextSoftkeyBar();

    ContextSoftkeyBar(const ContextSoftkeyBar&) = delete;
    ContextSoftkeyBar& operator=(const ContextSoftkeyBar&) = delete;

    void setLabels(const char* left, const char* center, const char* right);
    void setLeftIcon(const char* icon);

    void show();
    void hide();

private:
    lv_obj_t* container_ = nullptr;
    lv_obj_t* left_label_ = nullptr;
    lv_obj_t* center_label_ = nullptr;
    lv_obj_t* right_label_ = nullptr;
};

}  // namespace core::ui
