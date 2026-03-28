/**
 * @file main.cpp
 * @brief MIDI Studio Core - Open Control Migration
 *
 * Uses oc::hal::teensy::AppBuilder for simplified Teensy 4.1 setup.
 * Pattern follows open-control/example-teensy41-lvgl.
 */

#include <optional>

#include <oc/hal/teensy/SDCardBackend.hpp>
#include <oc/hal/teensy/Teensy.hpp>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <config/platform-teensy/Buffer.hpp>
#include <config/platform-teensy/Hardware.hpp>
#include "context/StandaloneContext.hpp"
#include "sequencer/SequencerRuntimeService.hpp"
#include "state/CoreState.hpp"

// =============================================================================
// Static Objects
// =============================================================================

static std::optional<oc::hal::teensy::Ili9341> display;
static std::optional<oc::ui::lvgl::Bridge> lvgl;
static std::optional<oc::hal::teensy::CD74HC4067> mux;
static oc::hal::teensy::SDCardBackend settingsStorage("/macros.bin");
static oc::hal::teensy::SDCardBackend macroWorkspaceStorage("/macro-workspace.bin");
static oc::hal::teensy::SDCardBackend macroLibraryStorage("/macro-library.bin");
static oc::hal::teensy::SDCardBackend sequencerWorkspaceStorage("/sequencer-workspace.bin");
static oc::hal::teensy::SDCardBackend sequencerPatternLibraryStorage("/sequencer-pattern-library.bin");
static oc::hal::teensy::SDCardBackend sequencerSetLibraryStorage("/sequencer-set-library.bin");
static std::optional<core::state::CoreState> coreState;
static std::optional<oc::app::OpenControlApp> app;
static std::optional<core::sequencer::SequencerRuntimeService> sequencerRuntime;

// =============================================================================
// Initialization Helpers
// =============================================================================

/// Check result and halt on error (embedded systems have no recovery)
static FLASHMEM void checkOrHalt(const oc::type::Result<void>& result, const char* component) {
    if (!result) {
        OC_LOG_ERROR("{} init failed: {}", component,
                     oc::type::errorCodeToString(result.error().code));
        while (true) {}
    }
}

static FLASHMEM void initDisplay() {
    display = oc::hal::teensy::Ili9341(
        Hardware::Display::CONFIG,
        {.framebuffer = Buffer::framebuffer, .diff1 = Buffer::diff1, .diff2 = Buffer::diff2});
    checkOrHalt(display->init(), "Display");
}

static FLASHMEM void initLVGL() {
    lvgl = oc::ui::lvgl::Bridge(*display, Buffer::lvgl, oc::hal::teensy::defaultTimeProvider,
                                 Hardware::LVGL::CONFIG);
    checkOrHalt(lvgl->init(), "LVGL");
}

static FLASHMEM void initMux() {
    mux = oc::hal::teensy::CD74HC4067(Hardware::Mux::CONFIG, oc::hal::teensy::gpio());
    checkOrHalt(mux->init(), "MUX");
}

static FLASHMEM void initStorageBackend(oc::hal::teensy::SDCardBackend& backend,
                                        const char* label) {
    const auto result = backend.init();
    if (!result) {
        OC_LOG_ERROR("{} storage init failed: {}",
                     label,
                     oc::type::errorCodeToString(result.error().code));
        while (true) {}
    }
}

static FLASHMEM void initStorage() {
    struct StorageInitItem {
        const char* label;
        oc::hal::teensy::SDCardBackend* backend;
    };

    const StorageInitItem items[] = {
        {"Settings", &settingsStorage},
        {"Macro workspace", &macroWorkspaceStorage},
        {"Macro library", &macroLibraryStorage},
        {"Sequencer workspace", &sequencerWorkspaceStorage},
        {"Sequencer pattern library", &sequencerPatternLibraryStorage},
        {"Sequencer set library", &sequencerSetLibraryStorage},
    };

    for (const auto& item : items) {
        initStorageBackend(*item.backend, item.label);
    }

    OC_LOG_INFO("Storages ready settings={}B macroWs={}B macroLib={}B seqWs={}B seqPatternLib={}B seqSetLib={}B",
                settingsStorage.capacity(),
                macroWorkspaceStorage.capacity(),
                macroLibraryStorage.capacity(),
                sequencerWorkspaceStorage.capacity(),
                sequencerPatternLibraryStorage.capacity(),
                sequencerSetLibraryStorage.capacity());
}

static FLASHMEM void initApp() {
    // Create global state with dedicated storage domains (survives context switches)
    coreState.emplace(settingsStorage,
                      macroWorkspaceStorage,
                      macroLibraryStorage,
                      sequencerWorkspaceStorage,
                      sequencerPatternLibraryStorage,
                      sequencerSetLibraryStorage);

    app = oc::hal::teensy::AppBuilder()
              .midi()
              .frames()
              .encoders(Hardware::Encoder::ENCODERS)
              .buttons(Hardware::Button::BUTTONS, *mux, Config::Timing::DEBOUNCE_MS)
              .inputConfig(Config::Input::CONFIG);

    if (!app->midiAPI()) {
        OC_LOG_ERROR("Sequencer runtime init failed: MIDI API unavailable");
        while (true) {}
    }

    sequencerRuntime.emplace(*coreState, *app->midiAPI(), app->eventBus());

    const bool runtimeHookRegistered = app->registerPreContextUpdateHook([]() {
        if (!app || !sequencerRuntime) return;

        static bool wasStandaloneActive = false;

        const bool isStandaloneActive =
            app->contexts().activeId() == static_cast<uint8_t>(Config::ContextID::STANDALONE);

        if (!isStandaloneActive) {
            if (wasStandaloneActive) {
                sequencerRuntime->stop();
            }
            wasStandaloneActive = false;
            return;
        }

        sequencerRuntime->update();
        wasStandaloneActive = true;
    });

    if (!runtimeHookRegistered) {
        OC_LOG_ERROR("Sequencer runtime init failed: app pre-context hook registry full");
        while (true) {}
    }

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

FLASHMEM void setup() {
    oc::hal::teensy::initLogging();

    OC_LOG_INFO("=== MIDI Studio Core Boot ===");
    OC_LOG_INFO("App {}Hz, LVGL {}Hz", Config::Timing::APP_HZ, Config::Timing::LVGL_HZ);

    initDisplay();
    initLVGL();
    initMux();
    initStorage();
    initApp();

    OC_LOG_INFO("Ready");
}

// Timing constants for main loop
constexpr uint32_t APP_PERIOD_US = 1'000'000 / Config::Timing::APP_HZ;
constexpr uint32_t LVGL_PERIOD_US = 1'000'000 / Config::Timing::LVGL_HZ;
constexpr uint32_t DISPLAY_PERF_LOG_WINDOW_MS = 1000;

static void logDisplayPerfWindow() {
    if (!display) return;

    const auto perf = display->perfSnapshot();
    if (!perf.valid || perf.frames == 0) {
        display->resetPerfStats();
        return;
    }

    const float pixelsPct = perf.pixelsRatio * 100.0f;
    const float tearPct = perf.tearRatio * 100.0f;
    const float diff1OverflowPct = perf.diff1.overflowRatio * 100.0f;
    const float diff2OverflowPct = perf.diff2.overflowRatio * 100.0f;

    OC_LOG_INFO(
        "[Perf][Display] fps={} avgFps={} uploadRate={} uploadAvg={}us cpuAvg={}us pixels={} tx={} tear={}%",
        perf.currentFps,
        perf.averageFps,
        perf.uploadRateFps,
        perf.uploadTimeUs.avg,
        perf.cpuTimeUs.avg,
        pixelsPct,
        perf.transactionsPerFrame.avg,
        tearPct);

    OC_LOG_INFO(
        "[Perf][DisplayDiff] diff1Ov={}/{} ({}%) diff1Size={}B diff1Cpu={}us diff2Ov={}/{} ({}%) diff2Size={}B diff2Cpu={}us vsync={} margin={}",
        perf.diff1.overflow,
        perf.diff1.computed,
        diff1OverflowPct,
        perf.diff1.sizeBytes.avg,
        perf.diff1.computeTimeUs.avg,
        perf.diff2.overflow,
        perf.diff2.computed,
        diff2OverflowPct,
        perf.diff2.sizeBytes.avg,
        perf.diff2.computeTimeUs.avg,
        perf.realVSyncSpacing.avg,
        perf.marginPerFrame.avg);

    display->resetPerfStats();
}

void loop() {
    static uint32_t lastMicros = 0;
    static uint32_t lvglAccumulator = 0;
    static uint32_t displayPerfWindowStartMs = 0;

    const uint32_t now = micros();
    if (now - lastMicros < APP_PERIOD_US) return;
    lastMicros = now;

    const uint32_t nowMs = millis();
    if (displayPerfWindowStartMs == 0) {
        displayPerfWindowStartMs = nowMs;
        display->resetPerfStats();
    }

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

    if ((nowMs - displayPerfWindowStartMs) >= DISPLAY_PERF_LOG_WINDOW_MS) {
        logDisplayPerfWindow();
        displayPerfWindowStartMs = nowMs;
    }
}
