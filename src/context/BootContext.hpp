#pragma once

/**
 * @file BootContext.hpp
 * @brief Boot animation context - displays splash then transitions to Standalone
 */

#include <memory>

#include <lvgl.h>
#include <oc/time/Time.hpp>

#include <oc/context/ContextBase.hpp>
#include <oc/context/Requirements.hpp>

#include <oc/ui/lvgl/FontLoader.hpp>
#include <oc/ui/lvgl/Screen.hpp>

#include <config/App.hpp>
#include "ui/font/CoreFonts.hpp"
#include "ui/view/SplashScreenView.hpp"

namespace core::context {

class BootContext : public oc::context::ContextBase {
public:
    static constexpr oc::context::Requirements REQUIRES{};

    oc::type::Result<void> init() override {
        oc::ui::lvgl::font::loadEssential(CORE_FONT_ENTRIES, CORE_FONT_COUNT);

        splash_ = std::make_unique<core::ui::SplashScreenView>(oc::ui::lvgl::Screen::root());
        splash_->onActivate();

        start_ms_ = oc::time::millis();
        return oc::type::Result<void>::ok();
    }

    void update() override {
        uint32_t elapsed = oc::time::millis() - start_ms_;

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

    const char* getName() const override { return "Boot"; }

protected:
    void onCleanup() override {
        splash_.reset();
    }

private:
    static constexpr uint32_t DURATION_MS = 1000;
    static constexpr uint32_t FADE_MS = DURATION_MS / 10;  // 100ms fade out
    static constexpr uint32_t FADE_START_MS = DURATION_MS - FADE_MS;  // Start fade at 900ms

    uint32_t start_ms_ = 0;
    bool fading_ = false;
    std::unique_ptr<core::ui::SplashScreenView> splash_;
};

}  // namespace core::context
