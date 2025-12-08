#pragma once

/**
 * @file BootContext.hpp
 * @brief Boot animation context - transitions to Standalone after animation
 */

#include "config/App.hpp"

#include <oc/context/IContext.hpp>
#include <oc/context/Requirements.hpp>

namespace context {

/**
 * @brief Boot context - displays startup animation then switches to Standalone
 *
 * Phase 2: Minimal version without actual UI animation.
 * Just waits ~1 second (60 frames at 60Hz) then transitions.
 */
class BootContext : public oc::context::IContext {
public:
    /// No API requirements - boot is self-contained
    static constexpr oc::context::Requirements REQUIRES{};

    bool initialize() override {
        Serial.println("[Boot] Starting...");
        return true;
    }

    void update() override {
        if (++frame_ >= BOOT_FRAMES) {
            Serial.println("[Boot] Complete, switching to Standalone");
            switchTo(Config::ContextID::STANDALONE);
        }
    }

    void cleanup() override {
        Serial.println("[Boot] Cleanup");
    }

    const char* getName() const override { return "Boot"; }

private:
    static constexpr uint16_t BOOT_FRAMES = 60;  // ~1 second at 60Hz
    uint16_t frame_ = 0;
};

}  // namespace context
