#pragma once

#include <optional>

#include <ILI9341_T4.h>

/**
 * @brief ILI9341 display driver with lazy initialization
 *
 * All hardware objects are created in init() to avoid
 * constructor calls before Arduino framework is ready.
 */
class Ili9341Driver {
public:
    Ili9341Driver() = default;
    ~Ili9341Driver() = default;

    bool init();
    bool isInitialized() const { return initialized_; }
    void refresh(bool redraw_now, uint16_t* pixels);
    void waitAsyncComplete();
private:
    std::optional<ILI9341_T4::ILI9341Driver> tft_;
    std::optional<ILI9341_T4::DiffBuff> diff1_;
    std::optional<ILI9341_T4::DiffBuff> diff2_;
    uint16_t* framebuffer_ = nullptr;
    bool initialized_ = false;
};
