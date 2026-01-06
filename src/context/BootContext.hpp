#pragma once

/**
 * @file BootContext.hpp
 * @brief Boot animation context - displays splash then transitions to Standalone
 */

#include <memory>

#include <Arduino.h>
#include <lvgl.h>

#include <oc/context/IContext.hpp>
#include <oc/context/Requirements.hpp>

#include <oc/ui/lvgl/FontLoader.hpp>

#include "config/App.hpp"
#include "ui/font/CoreFonts.hpp"
#include "ui/view/SplashScreenView.hpp"

namespace core::context {

class BootContext : public oc::context::IContext {
public:
    static constexpr oc::context::Requirements REQUIRES{};

    bool initialize() override {
        oc::ui::lvgl::font::loadEssential(CORE_FONT_ENTRIES, CORE_FONT_COUNT);

        splash_ = std::make_unique<core::ui::SplashScreenView>(lv_screen_active());
        splash_->onActivate();

        startMs_ = millis();
        return true;
    }

    void update() override {
        uint32_t elapsed = millis() - startMs_;

        // Update progress
        uint8_t progress = (elapsed * 100) / DURATION_MS;
        splash_->setProgress(progress);

        // Start fade before end
        if (!fading_ && elapsed >= FADE_START_MS) {
            fading_ = true;
            splash_->fadeOut(FADE_MS);
        }

        // Switch when done
        if (elapsed >= DURATION_MS) {
            switchTo(Config::ContextID::STANDALONE);
        }
    }

    void cleanup() override {
        splash_.reset();
    }

    const char* getName() const override { return "Boot"; }

private:
    static constexpr uint32_t DURATION_MS = 1000;
    static constexpr uint32_t FADE_MS = DURATION_MS / 10;  // 100ms fade out
    static constexpr uint32_t FADE_START_MS = DURATION_MS - FADE_MS;  // Start fade at 900ms

    uint32_t startMs_ = 0;
    bool fading_ = false;
    std::unique_ptr<core::ui::SplashScreenView> splash_;
};

}  // namespace core::context
