#pragma once

#include <cstdint>

#include <lvgl.h>

namespace oc::ui::lvgl {

// Native projection tests do not link LVGL. Keep this shim aligned with the
// integration package API so UI prop headers remain testable in isolation.
class PausableTimer {
public:
    using Callback = void (*)(lv_timer_t*);

    PausableTimer(uint32_t, Callback, void*) {}
    ~PausableTimer() = default;

    PausableTimer(const PausableTimer&) = delete;
    PausableTimer& operator=(const PausableTimer&) = delete;
    PausableTimer(PausableTimer&&) = delete;
    PausableTimer& operator=(PausableTimer&&) = delete;

    void pause() {}
    void resume(bool = false) {}
    bool valid() const { return true; }
};

}  // namespace oc::ui::lvgl
