#include "ui/view/PausableLvglTimer.hpp"

namespace core::ui {

PausableLvglTimer::PausableLvglTimer(uint32_t periodMs, lv_timer_cb_t callback, void* userData) {
    timer_ = lv_timer_create(callback, periodMs, userData);
    if (timer_) {
        lv_timer_pause(timer_);
    }
}

PausableLvglTimer::~PausableLvglTimer() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void PausableLvglTimer::pause() {
    if (timer_) {
        lv_timer_pause(timer_);
    }
}

void PausableLvglTimer::resume(bool ready) {
    if (!timer_) return;

    lv_timer_resume(timer_);
    if (ready) {
        lv_timer_ready(timer_);
    }
}

}  // namespace core::ui
