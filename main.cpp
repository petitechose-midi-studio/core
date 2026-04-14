/**
 * @file main.cpp
 * @brief MIDI Studio Core - Open Control Migration
 *
 * Uses oc::hal::teensy::AppBuilder for simplified Teensy 4.1 setup.
 * Pattern follows open-control/example-teensy41-lvgl.
 */

#include <optional>

#include <imxrt.h>

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
static core::app::ExtmemUniquePtr<core::sequencer::SequencerRuntimeService> sequencerRuntime;

#if defined(PERF_LOG)
namespace {

struct LoopPerfWindow {
    uint32_t startedAtMs = 0;

    uint32_t appUpdateCount = 0;
    uint64_t appUpdateTotalUs = 0;
    uint32_t appUpdateMaxUs = 0;

    uint32_t stateUpdateCount = 0;
    uint64_t stateUpdateTotalUs = 0;
    uint32_t stateUpdateMaxUs = 0;

    uint32_t lvglRefreshCount = 0;
    uint64_t lvglRefreshTotalUs = 0;
    uint32_t lvglRefreshMaxUs = 0;
};

LoopPerfWindow g_loopPerfWindow;

void recordPerfSample(uint32_t& count, uint64_t& totalUs, uint32_t& maxUs, uint32_t sampleUs) {
    ++count;
    totalUs += sampleUs;
    if (sampleUs > maxUs) {
        maxUs = sampleUs;
    }
}

void maybeLogLoopPerfWindow(uint32_t nowMs) {
    auto& window = g_loopPerfWindow;
    if (window.startedAtMs == 0) {
        window.startedAtMs = nowMs;
        return;
    }

    if ((nowMs - window.startedAtMs) < 1000) {
        return;
    }

    const uint32_t appUpdateAvgUs =
        (window.appUpdateCount > 0)
            ? static_cast<uint32_t>(window.appUpdateTotalUs / window.appUpdateCount)
            : 0;
    const uint32_t stateUpdateAvgUs =
        (window.stateUpdateCount > 0)
            ? static_cast<uint32_t>(window.stateUpdateTotalUs / window.stateUpdateCount)
            : 0;
    const uint32_t lvglRefreshAvgUs =
        (window.lvglRefreshCount > 0)
            ? static_cast<uint32_t>(window.lvglRefreshTotalUs / window.lvglRefreshCount)
            : 0;

    OC_LOG_INFO(
        "[Perf][MainLoop] appUpdates={} avgApp={}us maxApp={}us stateUpdates={} avgState={}us "
        "maxState={}us lvglRefreshes={} avgLvgl={}us maxLvgl={}us",
        window.appUpdateCount,
        appUpdateAvgUs,
        window.appUpdateMaxUs,
        window.stateUpdateCount,
        stateUpdateAvgUs,
        window.stateUpdateMaxUs,
        window.lvglRefreshCount,
        lvglRefreshAvgUs,
        window.lvglRefreshMaxUs
    );

    window = {};
    window.startedAtMs = nowMs;
}

}  // namespace
#endif

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

    sequencerRuntime = core::app::makeExtmemUnique<core::sequencer::SequencerRuntimeService>(
        *coreState,
        *app->midiAPI(),
        app->eventBus()
    );
    if (!sequencerRuntime) {
        OC_LOG_ERROR("Sequencer runtime init failed: EXTMEM allocation failed");
        while (true) {}
    }

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
void loop() {
    static uint32_t lastMicros = 0;
    static uint32_t lvglAccumulator = 0;

    const uint32_t now = micros();
    if (now - lastMicros < APP_PERIOD_US) return;
    lastMicros = now;

    // Poll hardware and update active context
#if defined(PERF_LOG)
    const uint32_t appUpdateStartUs = micros();
#endif
    app->update();
#if defined(PERF_LOG)
    recordPerfSample(
        g_loopPerfWindow.appUpdateCount,
        g_loopPerfWindow.appUpdateTotalUs,
        g_loopPerfWindow.appUpdateMaxUs,
        micros() - appUpdateStartUs
    );
#endif

    // Update persistence (handles delayed value saves)
#if defined(PERF_LOG)
    const uint32_t stateUpdateStartUs = micros();
#endif
    coreState->update();
#if defined(PERF_LOG)
    recordPerfSample(
        g_loopPerfWindow.stateUpdateCount,
        g_loopPerfWindow.stateUpdateTotalUs,
        g_loopPerfWindow.stateUpdateMaxUs,
        micros() - stateUpdateStartUs
    );
#endif

    // Refresh LVGL at lower frequency to reduce CPU load
    lvglAccumulator += APP_PERIOD_US;
    if (lvglAccumulator >= LVGL_PERIOD_US) {
        lvglAccumulator = 0;
#if defined(PERF_LOG)
        const uint32_t lvglRefreshStartUs = micros();
#endif
        lvgl->refresh();
#if defined(PERF_LOG)
        recordPerfSample(
            g_loopPerfWindow.lvglRefreshCount,
            g_loopPerfWindow.lvglRefreshTotalUs,
            g_loopPerfWindow.lvglRefreshMaxUs,
            micros() - lvglRefreshStartUs
        );
#endif
    }

#if defined(PERF_LOG)
    maybeLogLoopPerfWindow(millis());
#endif
}
