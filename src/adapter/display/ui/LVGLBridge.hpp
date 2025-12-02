#pragma once

#include <lvgl.h>

class Ili9341Driver;

class LVGLBridge {
public:
    explicit LVGLBridge(Ili9341Driver& driver);
    ~LVGLBridge();

    bool init();
    bool isInitialized() const { return initialized_; }
    void refresh();
private:
    Ili9341Driver& driver_;
    lv_display_t* display_;
    bool initialized_ = false;

    static void flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
};
