#include "Ili9341Driver.hpp"

#include "config/System.hpp"
#include "log/Macros.hpp"

DMAMEM static uint16_t main_framebuffer[System::Display::FRAMEBUFFER_SIZE];
DMAMEM static uint8_t diffbuffer1[System::Display::DIFFBUFFER_SIZE];
DMAMEM static uint8_t diffbuffer2[System::Display::DIFFBUFFER_SIZE];

bool Ili9341Driver::init() {
    if (initialized_) return true;

    LOGLN("[Display] Init...");

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

    if (!tft_->begin(System::Hardware::DISPLAY_SPI_SPEED)) {
        LOGLN("[Display] FAILED");
        return false;
    }

    tft_->setRotation(System::Display::ROTATION);
    tft_->invertDisplay(true);
    tft_->setFramebuffer(framebuffer_);
    tft_->setDiffBuffers(&(*diff1_), &(*diff2_));
    tft_->setVSyncSpacing(System::Display::VSYNC_SPACING);
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
