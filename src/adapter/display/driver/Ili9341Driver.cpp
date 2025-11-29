#include "Ili9341Driver.hpp"

#include "config/System.hpp"
#include "log/Macros.hpp"

namespace
{
    // Boot timing delays (ms) - adjust if boot fails at specific step
    constexpr uint8_t DELAY_CACHE_FLUSH = 5;
    constexpr uint8_t DELAY_RESET_LOW = 30;
    constexpr uint16_t DELAY_RESET_HIGH = 150; // ILI9341 datasheet: 120ms after reset
    constexpr uint8_t DELAY_PRE_SPI = 15;
    constexpr uint8_t DELAY_POST_BUFFERS = 10;  // More time before timing config
    constexpr uint8_t DELAY_POST_REFRESH = 50;
    constexpr uint8_t DELAY_POST_VSYNC = 50;
}

DMAMEM static uint16_t main_framebuffer[System::Display::FRAMEBUFFER_SIZE];
DMAMEM static uint8_t diffbuffer1[System::Display::DIFFBUFFER_SIZE];
DMAMEM static uint8_t diffbuffer2[System::Display::DIFFBUFFER_SIZE];

bool Ili9341Driver::init() {
    if (initialized_) return true;

    // Flush DMAMEM cache + wait
    LOG("[Display] Cache flush +");
    LOG(DELAY_CACHE_FLUSH);
    LOGLN("ms");
    arm_dcache_flush_delete(main_framebuffer, sizeof(main_framebuffer));
    arm_dcache_flush_delete(diffbuffer1, sizeof(diffbuffer1));
    arm_dcache_flush_delete(diffbuffer2, sizeof(diffbuffer2));
    delay(DELAY_CACHE_FLUSH);

    // Hardware reset LOW + wait
    LOG("[Display] RST LOW +");
    LOG(DELAY_RESET_LOW);
    LOGLN("ms");
    pinMode(System::Hardware::DISPLAY_RST_PIN, OUTPUT);
    digitalWrite(System::Hardware::DISPLAY_RST_PIN, LOW);
    delay(DELAY_RESET_LOW);

    // Hardware reset HIGH + wait
    LOG("[Display] RST HIGH +");
    LOG(DELAY_RESET_HIGH);
    LOGLN("ms");
    digitalWrite(System::Hardware::DISPLAY_RST_PIN, HIGH);
    delay(DELAY_RESET_HIGH);

    // Create TFT object + wait
    LOG("[Display] TFT create +");
    LOG(DELAY_PRE_SPI);
    LOGLN("ms");
    framebuffer_ = main_framebuffer;
    diff1_.emplace(diffbuffer1, sizeof(diffbuffer1));
    diff2_.emplace(diffbuffer2, sizeof(diffbuffer2));
    tft_.emplace(
        System::Hardware::DISPLAY_CS_PIN,
        System::Hardware::DISPLAY_DC_PIN,
        System::Hardware::DISPLAY_SCK_PIN,
        System::Hardware::DISPLAY_MOSI_PIN,
        System::Hardware::DISPLAY_MISO_PIN,
        System::Hardware::DISPLAY_RST_PIN
    );
    delay(DELAY_PRE_SPI);

    // SPI begin
    LOGLN("[Display] SPI begin");
    if (!tft_->begin(System::Hardware::DISPLAY_SPI_SPEED)) {
        LOGLN("[Display] FAILED");
        return false;
    }

    // Configure buffers + wait
    LOG("[Display] Buffers +");
    LOG(DELAY_POST_BUFFERS);
    LOGLN("ms");
    tft_->setRotation(System::Display::ROTATION);
    tft_->invertDisplay(true);
    tft_->setFramebuffer(framebuffer_);
    tft_->setDiffBuffers(&(*diff1_), &(*diff2_));
    delay(DELAY_POST_BUFFERS);

    // Refresh rate + wait
    LOG("[Display] RefreshRate +");
    LOG(DELAY_POST_REFRESH);
    LOGLN("ms");
    tft_->setRefreshRate(System::Display::REFRESH_RATE_HZ);
    delay(DELAY_POST_REFRESH);

    // VSync + wait
    LOG("[Display] VSync +");
    LOG(DELAY_POST_VSYNC);
    LOGLN("ms");
    tft_->setVSyncSpacing(System::Display::VSYNC_SPACING);
    delay(DELAY_POST_VSYNC);

    // DMA params (no wait needed after)
    LOGLN("[Display] DMA params");
    tft_->setDiffGap(System::Display::DIFF_GAP);
    tft_->setIRQPriority(System::Display::IRQ_PRIORITY);
    tft_->setLateStartRatio(System::Display::LATE_START_RATIO);

    LOGLN("[Display] OK");
    initialized_ = true;
    return true;
}

void Ili9341Driver::refresh(bool redraw_now, uint16_t* pixels) {
    if (tft_) {
        tft_->update(pixels, redraw_now);
    }
}

void Ili9341Driver::waitAsyncComplete() {
    if (tft_) {
        tft_->waitUpdateAsyncComplete();
    }
}
