#pragma once

#include <cstdint>

#include <lvgl.h>

namespace core::ui {

class PausableLvglTimer {
public:
    PausableLvglTimer(uint32_t periodMs, lv_timer_cb_t callback, void* userData);
    ~PausableLvglTimer();

    PausableLvglTimer(const PausableLvglTimer&) = delete;
    PausableLvglTimer& operator=(const PausableLvglTimer&) = delete;

    void pause();
    void resume(bool ready = false);
    bool valid() const { return timer_ != nullptr; }

private:
    lv_timer_t* timer_ = nullptr;
};

}  // namespace core::ui
