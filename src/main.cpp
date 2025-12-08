/**
 * @file main.cpp
 * @brief MIDI Studio Core - Open Control Migration
 *
 * Phase 1: Display boot only
 */

#include "config/App.hpp"
#include "config/Hardware.hpp"
#include "config/Buffer.hpp"

#include <optional>

#include <Arduino.h>
#include <oc/teensy/Ili9341.hpp>
#include <oc/ui/lvgl/Bridge.hpp>

// ═══════════════════════════════════════════════════════════════════════════
// Static Objects
// ═══════════════════════════════════════════════════════════════════════════

static std::optional<oc::teensy::Ili9341> display;
static std::optional<oc::ui::lvgl::Bridge> lvgl;

// ═══════════════════════════════════════════════════════════════════════════
// Initialization
// ═══════════════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════════════
// Arduino Entry Points
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    while (!Serial && millis() < 3000) {}

    Serial.printf("\n[MIDI Studio] Phase 1 - LVGL %luHz\n\n", Config::Timing::LVGL_HZ);

    if (!initDisplay()) {
        Serial.println("[ERROR] Display init failed");
        while (true);
    }
    if (!initLVGL()) {
        Serial.println("[ERROR] LVGL init failed");
        while (true);
    }

    Serial.println("[OK] Display ready\n");
}

constexpr uint32_t LVGL_PERIOD_US = 1'000'000 / Config::Timing::LVGL_HZ;

void loop() {
    static uint32_t lastMicros = 0;

    const uint32_t now = micros();
    if (now - lastMicros < LVGL_PERIOD_US) return;
    lastMicros = now;

    lvgl->refresh();
}
