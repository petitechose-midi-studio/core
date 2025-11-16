#pragma once

#include <lvgl.h>

class Ili9341Driver;

/**
 * @brief Bridge between LVGL graphics library and ILI9341 hardware driver
 *
 * Manages LVGL initialization and provides the flush callback to render
 * graphics to the physical display through the hardware driver.
 */
class LVGLBridge {
public:
    explicit LVGLBridge(Ili9341Driver& driver);
    ~LVGLBridge();

    void refresh();

private:
    Ili9341Driver& driver_;
    lv_display_t* display_;

    static void flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
};