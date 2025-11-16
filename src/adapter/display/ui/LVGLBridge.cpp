#include "LVGLBridge.hpp"

#include "../driver/Ili9341Driver.hpp"
#include "config/System.hpp"
#include "log/Macros.hpp"

DMAMEM static lv_color_t lvgl_buffer[System::Display::LVGL_BUFFER_SIZE];

LVGLBridge::LVGLBridge(Ili9341Driver& driver) : driver_(driver), display_(nullptr) {
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    display_ = lv_display_create(System::Display::SCREEN_WIDTH, System::Display::SCREEN_HEIGHT);
    if (!display_) {
        LOGLN("ERROR: LVGLBridge - Failed to create LVGL display");
        return;
    }

    lv_display_set_buffers(display_,
                           lvgl_buffer,
                           nullptr,
                           System::Display::LVGL_BUFFER_SIZE * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_FULL);

    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display_, flush);
    lv_display_set_user_data(display_, &driver_);
}

LVGLBridge::~LVGLBridge() {
    if (display_) {
        lv_display_delete(display_);
    }
}

void LVGLBridge::refresh() {
    lv_timer_handler();
}

void LVGLBridge::flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    auto* driver = static_cast<Ili9341Driver*>(lv_display_get_user_data(disp));

    if (!driver) {
        lv_display_flush_ready(disp);
        return;
    }

    auto* pixels = reinterpret_cast<uint16_t*>(px_map);
    driver->refresh(false, pixels);

    lv_display_flush_ready(disp);
}