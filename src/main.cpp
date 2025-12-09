/**
 * @file main.cpp
 * @brief MIDI Studio Core - Open Control Migration
 *
 * Phase 4: Display + Contexts + Inputs + MIDI
 */

#include <memory>
#include <optional>

#include <Arduino.h>
#include <oc/teensy/Ili9341.hpp>
#include <oc/teensy/Teensy.hpp>
#include <oc/ui/lvgl/Bridge.hpp>

#include "config/App.hpp"
#include "config/Buffer.hpp"
#include "config/Hardware.hpp"
#include "context/BootContext.hpp"
#include "context/StandaloneContext.hpp"

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
    // Phase 4: AppBuilder with inputs + MIDI
    app = oc::teensy::AppBuilder()
              .midi()
              .encoders(Hardware::Encoder::ENCODERS)
              .buttons(Hardware::Button::BUTTONS, *mux, Config::Timing::DEBOUNCE_MS)
              .inputConfig(Config::Input::CONFIG);

    // Register contexts
    app->registerContext<context::BootContext>(Config::ContextID::BOOT, "Boot");
    app->registerContext<context::StandaloneContext>(Config::ContextID::STANDALONE, "Standalone");

    return app->begin();
}

// =============================================================================
// Arduino Entry Points
// =============================================================================

void setup() {
#ifdef DEV_MODE
    while (!Serial && millis() < 5000) {}
#endif

    // Init log time provider early (before AppBuilder)
    oc::log::setTimeProvider(millis);

    OC_LOG_INFO("MIDI Studio - App {}Hz, LVGL {}Hz", Config::Timing::APP_HZ,
                Config::Timing::LVGL_HZ);

    if (!initDisplay()) {
        OC_LOG_ERROR("Display init failed");
        while (true);
    }
    if (!initLVGL()) {
        OC_LOG_ERROR("LVGL init failed");
        while (true);
    }
    if (!initMux()) {
        OC_LOG_ERROR("MUX init failed");
        while (true);
    }
    if (!initApp()) {
        OC_LOG_ERROR("App init failed");
        while (true);
    }

    OC_LOG_INFO("Ready");
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
