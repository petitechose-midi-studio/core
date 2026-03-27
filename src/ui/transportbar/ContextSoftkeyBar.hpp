#pragma once

#include <lvgl.h>

namespace core::ui {

class ContextSoftkeyBar {
public:
    explicit ContextSoftkeyBar(lv_obj_t* parent);
    ~ContextSoftkeyBar();

    ContextSoftkeyBar(const ContextSoftkeyBar&) = delete;
    ContextSoftkeyBar& operator=(const ContextSoftkeyBar&) = delete;

    void setLabels(const char* left, const char* center, const char* right);

    void show();
    void hide();

private:
    lv_obj_t* container_ = nullptr;
    lv_obj_t* left_label_ = nullptr;
    lv_obj_t* center_label_ = nullptr;
    lv_obj_t* right_label_ = nullptr;
};

}  // namespace core::ui
