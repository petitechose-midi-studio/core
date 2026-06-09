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
#include <oc/hal/teensy/SDFileSystemBackend.hpp>
#include <oc/hal/teensy/Teensy.hpp>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#if !defined(MS_PROJECT_STORE_SMOKE)
#include <config/platform-teensy/Buffer.hpp>
#include <config/platform-teensy/Hardware.hpp>
#endif
#include "context/StandaloneContext.hpp"
#include "context/standalone/StandaloneSequencerRuntimeHook.hpp"
#include "persistence/PersistenceSlotFileStore.hpp"
#include "persistence/ProductFileService.hpp"
#include "persistence/ProjectSessionAutosaveService.hpp"
#include "persistence/ProjectSessionRestoreService.hpp"
#include "persistence/StorageRecoveryMachine.hpp"
#include "sequencer/SequencerRuntimeService.hpp"
#include "state/CoreState.hpp"
#include "validation/project/ProjectStoreSmoke.hpp"
#if defined(MS_UX_RECORDER)
#include "validation/ux/SemanticUxRecorder.hpp"
#endif

// =============================================================================
// Static Objects
// =============================================================================

#if !defined(MS_PROJECT_STORE_SMOKE)
static std::optional<oc::hal::teensy::Ili9341> display;
static std::optional<oc::ui::lvgl::Bridge> lvgl;
static std::optional<oc::hal::teensy::CD74HC4067> mux;
#endif
static oc::hal::teensy::SDCardBackend settingsStorage("/macros.bin");
static oc::hal::teensy::SDCardBackend macroWorkspaceStorage("/macro-workspace.bin");
static oc::hal::teensy::SDCardBackend macroLibraryStorage("/macro-library.bin");
static oc::hal::teensy::SDCardBackend sequencerWorkspaceStorage("/sequencer-workspace.bin");
static oc::hal::teensy::SDCardBackend sequencerPatternLibraryStorage("/sequencer-pattern-library.bin");
static oc::hal::teensy::SDCardBackend sequencerSetLibraryStorage("/sequencer-set-library.bin");
static oc::hal::teensy::SDFileSystemBackend productFileSystemBackend;
static std::optional<core::persistence::ProductFileService> productFileService;
static std::optional<core::persistence::ProjectSessionAutosaveService> projectSessionAutosaveService;
static std::optional<core::persistence::ProjectSessionRestoreService> projectSessionRestoreService;
static std::optional<core::state::CoreState> coreState;
#if !defined(MS_PROJECT_STORE_SMOKE)
static std::optional<oc::app::OpenControlApp> app;
static core::app::ExtmemUniquePtr<core::sequencer::SequencerRuntimeService>
    standaloneSequencerRuntime;
#endif

namespace {

constexpr uint32_t STORAGE_RECOVERY_SAMPLE_MS = 500;

struct StorageBackendRef {
    const char* label;
    oc::hal::teensy::SDCardBackend* backend;
};

StorageBackendRef storageBackends[] = {
    {"Settings", &settingsStorage},
    {"Macro workspace", &macroWorkspaceStorage},
    {"Macro library", &macroLibraryStorage},
    {"Sequencer workspace", &sequencerWorkspaceStorage},
    {"Sequencer pattern library", &sequencerPatternLibraryStorage},
    {"Sequencer set library", &sequencerSetLibraryStorage},
};

class StorageRecoveryRuntimeManager {
public:
    void update(uint32_t nowMs, bool playing) {
        if (last_sample_ms_ != 0 &&
            static_cast<uint32_t>(nowMs - last_sample_ms_) < STORAGE_RECOVERY_SAMPLE_MS) {
            return;
        }
        last_sample_ms_ = nowMs;

        const auto action = machine_.update({
            allStorageBackendsAvailable_(),
            playing,
            nowMs,
        });
        handleAction_(action, nowMs);
    }

private:
    bool allStorageBackendsAvailable_() const {
        for (const auto& item : storageBackends) {
            if (!item.backend->available()) {
                return false;
            }
        }
        return true;
    }

    bool reopenStorageBackends_() const {
        bool ok = true;
        for (const auto& item : storageBackends) {
            if (!item.backend->reopen()) {
                OC_LOG_WARN("[StorageRecovery] Reopen failed for {}", item.label);
                ok = false;
            }
        }
        return ok;
    }

    bool revalidateFromRam_(uint32_t nowMs) {
        if (!coreState) {
            OC_LOG_WARN("[StorageRecovery] CoreState unavailable during revalidation");
            return false;
        }

        const auto status = coreState->recoverPersistenceFromRamAfterStorageReopen();
        if (status != core::persistence::PersistenceWriteStatus::OK) {
            OC_LOG_WARN("[StorageRecovery] RAM revalidation failed at {}ms: {}",
                        nowMs,
                        core::persistence::persistenceWriteStatusLabel(status));
            return false;
        }
        return true;
    }

    void handleAction_(core::persistence::StorageRecoveryAction action, uint32_t nowMs) {
        switch (action) {
            case core::persistence::StorageRecoveryAction::MARK_OFFLINE:
                OC_LOG_WARN("[StorageRecovery] SD unavailable; runtime RAM remains authoritative");
                return;

            case core::persistence::StorageRecoveryAction::ATTEMPT_REOPEN: {
                OC_LOG_INFO("[StorageRecovery] SD present; reopening storage backends");
                const bool reopenOk = reopenStorageBackends_();
                const auto next = machine_.completeReopen(reopenOk, nowMs);
                if (!reopenOk) {
                    OC_LOG_WARN("[StorageRecovery] Reopen failed; retrying after backoff");
                    return;
                }
                handleAction_(next, nowMs);
                return;
            }

            case core::persistence::StorageRecoveryAction::ATTEMPT_REVALIDATE: {
                OC_LOG_INFO("[StorageRecovery] Revalidating storage from live RAM");
                const bool revalidateOk = revalidateFromRam_(nowMs);
                const auto next = machine_.completeRevalidation(revalidateOk, nowMs);
                if (!revalidateOk) {
                    OC_LOG_WARN("[StorageRecovery] Revalidation failed; retrying after backoff");
                    return;
                }
                handleAction_(next, nowMs);
                return;
            }

            case core::persistence::StorageRecoveryAction::MARK_RECOVERED:
                OC_LOG_INFO("[StorageRecovery] SD recovered; persistence resumed");
                return;

            case core::persistence::StorageRecoveryAction::NONE:
            default:
                return;
        }
    }

    core::persistence::StorageRecoveryMachine machine_{};
    uint32_t last_sample_ms_ = 0;
};

StorageRecoveryRuntimeManager storageRecovery;

}  // namespace

#if defined(MS_PROJECT_STORE_SMOKE)
namespace {

bool projectStoreSmokeResult = false;
bool projectStoreSmokeCompleted = false;

}  // namespace
#endif

#if defined(MS_UX_RECORDER)
namespace {

class SerialSemanticUxSink : public core::validation::ux::SemanticUxLineSink {
public:
    void writeLine(const char* line) override {
        OC_LOG_INFO("{}", line);
    }
};

SerialSemanticUxSink semanticUxSink;
core::validation::ux::SemanticUxRecorder semanticUxRecorder{
    core::validation::ux::SemanticUxRecorderOptions{
        .sink = &semanticUxSink,
        .enabled = true,
    }
};

}  // namespace
#endif

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
#if !defined(MS_PROJECT_STORE_SMOKE)
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
#endif

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

    productFileService.emplace(productFileSystemBackend);
    const auto productFilesResult = productFileService->init();
    if (!productFilesResult) {
        OC_LOG_ERROR("Product file service init failed: {}",
                     oc::type::errorCodeToString(productFilesResult.error().code));
        while (true) {}
    }

    OC_LOG_INFO("Storages ready settings={}B macroWs={}B macroLib={}B seqWs={}B seqPatternLib={}B seqSetLib={}B",
                settingsStorage.capacity(),
                macroWorkspaceStorage.capacity(),
                macroLibraryStorage.capacity(),
                sequencerWorkspaceStorage.capacity(),
                sequencerPatternLibraryStorage.capacity(),
                sequencerSetLibraryStorage.capacity());
}

#if !defined(MS_PROJECT_STORE_SMOKE)
static FLASHMEM void initApp() {
    // Create global state with dedicated storage domains (survives context switches)
    coreState.emplace(settingsStorage,
                      macroWorkspaceStorage,
                      macroLibraryStorage,
                      sequencerWorkspaceStorage,
                      sequencerPatternLibraryStorage,
                      sequencerSetLibraryStorage);
    projectSessionRestoreService.emplace(*productFileService);
    const auto sessionRestore = projectSessionRestoreService->restore(*coreState);
    switch (sessionRestore.status) {
        case core::persistence::ProjectSessionRestoreService::Status::RESTORED:
            OC_LOG_INFO("[ProjectSession] restored current.mspj bytes={}",
                        sessionRestore.bytes);
            break;
        case core::persistence::ProjectSessionRestoreService::Status::MISSING:
            OC_LOG_INFO("[ProjectSession] no current.mspj; using default session");
            break;
        case core::persistence::ProjectSessionRestoreService::Status::APPLY_FAILED:
            OC_LOG_WARN("[ProjectSession] current.mspj apply failed; using default session");
            break;
        case core::persistence::ProjectSessionRestoreService::Status::DEGRADED:
        default:
            OC_LOG_WARN("[ProjectSession] current.mspj unavailable/corrupt; using default session");
            break;
    }
    projectSessionAutosaveService.emplace(*productFileService);

    oc::hal::teensy::AppBuilder appBuilder;
    appBuilder.midi()
        .frames()
        .encoders(Hardware::Encoder::ENCODERS)
        .buttons(Hardware::Button::BUTTONS, *mux, Config::Timing::DEBOUNCE_MS)
        .inputConfig(Config::Input::CONFIG);

#if defined(MS_UX_RECORDER)
    appBuilder.inputTrace([](const oc::core::input::InputBindingTraceEvent& event) {
        if (!coreState) {
            semanticUxRecorder.onBindingTrace(event);
            return;
        }
        semanticUxRecorder.onBindingTrace(
            event,
            core::validation::ux::makeSemanticUxSnapshot(*coreState)
        );
    });
#endif

    app = appBuilder;

    if (!app->midiAPI()) {
        OC_LOG_ERROR("Sequencer runtime init failed: MIDI API unavailable");
        while (true) {}
    }

    standaloneSequencerRuntime =
        core::app::makeExtmemUnique<core::sequencer::SequencerRuntimeService>(
        core::sequencer::SequencerRuntimeService::StateRefs{
            coreState->sequencer,
            coreState->sequencerTracks,
            coreState->statusBar,
            coreState->midiSync,
        },
        *app->midiAPI(),
        app->eventBus()
    );
    if (!standaloneSequencerRuntime) {
        OC_LOG_ERROR("Sequencer runtime init failed: EXTMEM allocation failed");
        while (true) {}
    }

    const bool runtimeHookRegistered =
        core::context::standalone::registerStandaloneSequencerRuntimeHook(
            *app,
            standaloneSequencerRuntime
        );

    if (!runtimeHookRegistered) {
        OC_LOG_ERROR("Sequencer runtime init failed: app pre-context hook registry full");
        while (true) {}
    }

    // Register context with factory that captures CoreState reference
    app->registerContextWithFactory(
        Config::ContextID::STANDALONE,
        "Standalone",
        [&]() {
            return std::make_unique<core::context::StandaloneContext>(
                *coreState,
                *productFileService
            );
        });
    app->begin();
}
#endif

// =============================================================================
// Arduino Entry Points
// =============================================================================

FLASHMEM void setup() {
    oc::hal::teensy::initLogging();

    OC_LOG_INFO("=== MIDI Studio Core Boot ===");
    OC_LOG_INFO("App {}Hz, LVGL {}Hz", Config::Timing::APP_HZ, Config::Timing::LVGL_HZ);

#if defined(MS_PROJECT_STORE_SMOKE)
    initStorage();
    if (productFileService) {
        coreState.emplace(settingsStorage,
                          macroWorkspaceStorage,
                          macroLibraryStorage,
                          sequencerWorkspaceStorage,
                          sequencerPatternLibraryStorage,
                          sequencerSetLibraryStorage);
        projectStoreSmokeResult =
            core::validation::project::runProjectStoreSmoke(*productFileService, *coreState);
    } else {
        OC_LOG_ERROR("[project-store-smoke] ProductFileService unavailable");
        projectStoreSmokeResult = false;
    }
    projectStoreSmokeCompleted = true;
    OC_LOG_INFO("[project-store-smoke] done result={}", projectStoreSmokeResult ? "OK" : "FAIL");
    Serial.println(projectStoreSmokeResult
                       ? "[project-store-smoke] done result=OK"
                       : "[project-store-smoke] done result=FAIL");
    return;
#else
    initDisplay();
    initLVGL();
    initMux();
    initStorage();
    initApp();

#if defined(MS_UX_RECORDER)
    semanticUxSink.writeLine("UXR {\"kind\":\"session\",\"event\":\"boot\",\"enabled\":1}");
#endif

    OC_LOG_INFO("Ready");
#endif
}

// Timing constants for main loop
constexpr uint32_t APP_PERIOD_US = 1'000'000 / Config::Timing::APP_HZ;
constexpr uint32_t LVGL_PERIOD_US = 1'000'000 / Config::Timing::LVGL_HZ;
void loop() {
#if defined(MS_PROJECT_STORE_SMOKE)
    static uint32_t lastHeartbeatMs = 0;
    const uint32_t nowMs = millis();
    if (lastHeartbeatMs == 0 || static_cast<uint32_t>(nowMs - lastHeartbeatMs) >= 2000U) {
        lastHeartbeatMs = nowMs;
        if (projectStoreSmokeCompleted) {
            OC_LOG_INFO("[project-store-smoke] heartbeat result={}",
                        projectStoreSmokeResult ? "OK" : "FAIL");
            Serial.println(projectStoreSmokeResult
                               ? "[project-store-smoke] heartbeat result=OK"
                               : "[project-store-smoke] heartbeat result=FAIL");
        } else {
            OC_LOG_INFO("[project-store-smoke] heartbeat result=PENDING");
            Serial.println("[project-store-smoke] heartbeat result=PENDING");
        }
    }
    delay(25);
#else
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

    const bool productFileWriteActive =
        productFileService && productFileService->writeSessionActive();
    // Product file sessions keep an SD handle open; avoid competing recovery
    // and persistence paths while a PC/controller transfer is in flight.
    if (!productFileWriteActive) {
        storageRecovery.update(millis(), coreState->statusBar.playing.get());
    }

    // Update persistence (handles delayed value saves)
#if defined(PERF_LOG)
    const uint32_t stateUpdateStartUs = micros();
#endif
    if (!productFileWriteActive) {
        coreState->update();
        if (projectSessionAutosaveService) {
            projectSessionAutosaveService->update(*coreState, millis());
        }
    }
#if defined(PERF_LOG)
    if (!productFileWriteActive) {
        recordPerfSample(
            g_loopPerfWindow.stateUpdateCount,
            g_loopPerfWindow.stateUpdateTotalUs,
            g_loopPerfWindow.stateUpdateMaxUs,
            micros() - stateUpdateStartUs
        );
    }
#endif

#if defined(MS_UX_RECORDER)
    semanticUxRecorder.flush(millis(), *coreState);
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
#endif
}
