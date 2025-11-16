#pragma once

#include <ILI9341_T4.h>

#include "config/System.hpp"

class Ili9341Driver {
public:
    Ili9341Driver();
    ~Ili9341Driver() = default;

    void refresh(bool redraw_now, uint16_t* pixels);

private:
    ILI9341_T4::ILI9341Driver tft_;
    uint16_t* framebuffer_;
    ILI9341_T4::DiffBuff diff1_;
    ILI9341_T4::DiffBuff diff2_;
};