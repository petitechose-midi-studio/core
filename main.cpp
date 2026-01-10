/**
 * @file main.cpp
 * @brief MIDI Studio Core - Open Control Migration
 *
 * Uses oc::teensy::AppBuilder for simplified Teensy 4.1 setup.
 * Pattern follows open-control/example-teensy41-lvgl.
 */

#include <optional>

#include <oc/teensy/EEPROMBackend.hpp>
#include <oc/teensy/Teensy.hpp>

#include "App.hpp"
#include "Buffer.hpp"
#include "Hardware.hpp"
#include "context/BootContext.hpp"
#include "context/StandaloneContext.hpp"
#include "state/CoreState.hpp"

// =============================================================================
// Static Objects
// =============================================================================

static std::optional<oc::teensy::Ili9341> display;
static std::optional<oc::ui::lvgl::Bridge> lvgl;
static std::optional<oc::teensy::CD74HC4067> mux;
static oc::teensy::EEPROMBackend storage;  // EEPROM backend for persistence
static std::optional<core::state::CoreState> coreState;
static std::optional<oc::app::OpenControlApp> app;

// =============================================================================
// Initialization Helpers
// =============================================================================

/// Check result and halt on error (embedded systems have no recovery)
static void checkOrHalt(const oc::core::Result<void>& result, const char* component) {
    if (!result) {
        OC_LOG_ERROR("{} init failed: {}", component,
                     oc::core::errorCodeToString(result.error().code));
        while (true) {}
    }
}

static void initDisplay() {
    display = oc::teensy::Ili9341(
        Hardware::Display::CONFIG,
        {.framebuffer = Buffer::framebuffer, .diff1 = Buffer::diff1, .diff2 = Buffer::diff2});
    checkOrHalt(display->init(), "Display");
}

static void initLVGL() {
    lvgl = oc::ui::lvgl::Bridge(*display, Buffer::lvgl, oc::teensy::defaultTimeProvider,
                                 Hardware::LVGL::CONFIG);
    checkOrHalt(lvgl->init(), "LVGL");
}

static void initMux() {
    mux = oc::teensy::CD74HC4067(Hardware::Mux::CONFIG, oc::teensy::gpio());
    checkOrHalt(mux->init(), "MUX");
}

static void initApp() {
    // Create global state with EEPROM storage (survives context switches)
    coreState.emplace(storage);

    app = oc::teensy::AppBuilder()
              .midi()
              .serial()
              .encoders(Hardware::Encoder::ENCODERS)
              .buttons(Hardware::Button::BUTTONS, *mux, Config::Timing::DEBOUNCE_MS)
              .inputConfig(Config::Input::CONFIG);

    // Skip BootContext for now - debug crash
    // app->registerContext<core::context::BootContext>(Config::ContextID::BOOT, "Boot");

    // Register context with factory that captures CoreState reference
    app->registerContextWithFactory(
        Config::ContextID::STANDALONE,
        "Standalone",
        [&]() { return std::make_unique<core::context::StandaloneContext>(*coreState); });

    app->begin();
}

// =============================================================================
// Arduino Entry Points
// =============================================================================

void setup() {
    oc::teensy::initLogging();

    OC_LOG_INFO("=== MIDI Studio Core Boot ===");
    OC_LOG_INFO("App {}Hz, LVGL {}Hz", Config::Timing::APP_HZ, Config::Timing::LVGL_HZ);

    initDisplay();
    initLVGL();
    initMux();
    initApp();

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

    // Update persistence (handles delayed value saves)
    coreState->update();

    // Refresh LVGL at lower frequency to reduce CPU load
    lvglAccumulator += APP_PERIOD_US;
    if (lvglAccumulator >= LVGL_PERIOD_US) {
        lvglAccumulator = 0;
        lvgl->refresh();
    }
}
