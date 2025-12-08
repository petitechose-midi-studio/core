/**
 * @file main.cpp
 * @brief MIDI Studio Core - Open Control Migration
 *
 * Phase 3: Display + Contexts + Inputs (MUX, Buttons, Encoders)
 */

#include "config/App.hpp"
#include "config/Hardware.hpp"
#include "config/Buffer.hpp"
#include "context/BootContext.hpp"
#include "context/StandaloneContext.hpp"

#include <memory>
#include <optional>

#include <Arduino.h>
#include <oc/teensy/Ili9341.hpp>
#include <oc/teensy/AppBuilder.hpp>
#include <oc/teensy/GenericMux.hpp>
#include <oc/teensy/TeensyGpio.hpp>
#include <oc/ui/lvgl/Bridge.hpp>

// =============================================================================
// Static Objects
// =============================================================================

static std::optional<oc::teensy::Ili9341> display;
static std::optional<oc::ui::lvgl::Bridge> lvgl;
static std::optional<oc::teensy::CD74HC4067> mux;
static std::optional<oc::app::OpenControlApp> app;

// =============================================================================
// Initialization
// =============================================================================

static bool initDisplay() {
    using oc::teensy::Ili9341;

    display = Ili9341(
        Hardware::Display::CONFIG,
        {.framebuffer = Buffer::framebuffer, .diff1 = Buffer::diff1, .diff2 = Buffer::diff2});

    return display->init();
}

static bool initLVGL() {
    using oc::ui::lvgl::Bridge;

    lvgl = Bridge(*display, Buffer::lvgl, millis, Hardware::LVGL::CONFIG);

    return lvgl->init();
}

static bool initMux() {
    using oc::teensy::CD74HC4067;
    using oc::teensy::gpio;

    mux = CD74HC4067(Hardware::Mux::CONFIG, gpio());

    return mux->init();
}

static bool initApp() {
    // Phase 3: AppBuilder with inputs
    app = oc::teensy::AppBuilder()
              .encoders(Hardware::Encoder::ENCODERS)
              .buttons(Hardware::Button::BUTTONS, *mux, Config::Timing::DEBOUNCE_MS);

    // Register contexts
    app->registerContext<context::BootContext>(Config::ContextID::BOOT, "Boot");
    app->registerContext<context::StandaloneContext>(Config::ContextID::STANDALONE, "Standalone");

    return app->begin();
}

// =============================================================================
// Arduino Entry Points
// =============================================================================

void setup() {
    while (!Serial && millis() < 3000) {}

    Serial.printf("\n[MIDI Studio] Phase 3 - App %luHz, LVGL %luHz\n\n",
                  Config::Timing::APP_HZ, Config::Timing::LVGL_HZ);

    if (!initDisplay()) {
        Serial.println("[ERROR] Display init failed");
        while (true);
    }
    if (!initLVGL()) {
        Serial.println("[ERROR] LVGL init failed");
        while (true);
    }
    if (!initMux()) {
        Serial.println("[ERROR] MUX init failed");
        while (true);
    }
    if (!initApp()) {
        Serial.println("[ERROR] App init failed");
        while (true);
    }

    Serial.println("[OK] Ready\n");
}

// Timing constants for main loop
constexpr uint32_t APP_PERIOD_US = 1'000'000 / Config::Timing::APP_HZ;
constexpr uint32_t LVGL_PERIOD_US = 1'000'000 / Config::Timing::LVGL_HZ;

void loop() {
    static uint32_t lastMicros = 0;
    static uint32_t lvglAccumulator = 0;

    const uint32_t now = micros();
    if (now - lastMicros < APP_PERIOD_US) return;
    lastMicros = now;

    // Poll hardware and update active context
    app->update();

    // Refresh LVGL at lower frequency to reduce CPU load
    lvglAccumulator += APP_PERIOD_US;
    if (lvglAccumulator >= LVGL_PERIOD_US) {
        lvglAccumulator = 0;
        lvgl->refresh();
    }
}
