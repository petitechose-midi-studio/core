/**
 * @file main.cpp
 * @brief MIDI Studio Core firmware entry point
 *
 * Uses oc::hal::teensy::AppBuilder for simplified Teensy 4.1 setup.
 * Pattern follows open-control/example-teensy41-lvgl.
 */

#include <optional>

#include <imxrt.h>

#include <oc/hal/teensy/SDCardBackend.hpp>
#include <oc/hal/teensy/SDFileSystemBackend.hpp>
#include <oc/hal/teensy/Teensy.hpp>
#include <oc/diagnostics/Performance.hpp>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#if !defined(MS_PROJECT_STORE_SMOKE)
#include <config/platform-teensy/Buffer.hpp>
#include <config/platform-teensy/Hardware.hpp>
#endif
#include "context/StandaloneContext.hpp"
#include "context/standalone/StandaloneSequencerRuntimeHook.hpp"
#include "persistence/PersistenceStatus.hpp"
#include "persistence/ProductFileService.hpp"
#include "persistence/ProductStorageRecoveryService.hpp"
#include "persistence/ProjectSessionAutosaveService.hpp"
#include "persistence/ProjectSessionRestoreService.hpp"
#include "persistence/ProjectSessionStore.hpp"
#include "persistence/StorageRecoveryMachine.hpp"
#include "sequencer/SequencerRuntimeService.hpp"
#include "state/CoreState.hpp"
#include "validation/project/ProjectStoreSmoke.hpp"
#if OC_ENABLE_STATS
#include "diagnostics/MemoryFootprintReporter.hpp"
#include "diagnostics/PerformanceReporter.hpp"
#endif
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
static oc::hal::teensy::SDCardBackend deviceSettingsStorage("/core-settings.bin");
static oc::hal::teensy::SDFileSystemBackend productFileSystemBackend;
static std::optional<core::persistence::ProductFileService> productFileService;
static std::optional<core::persistence::ProjectSessionStore> projectSessionStore;
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
    {"Settings", &deviceSettingsStorage},
};

FLASHMEM bool initializeStorageBackend(const StorageBackendRef& item) {
    const auto initialized = item.backend->init();
    if (!initialized) {
        OC_LOG_WARN("[StorageRecovery] {} init pending: {}",
                    item.label,
                    oc::type::errorCodeToString(initialized.error().code));
        return false;
    }
    if (!item.backend->available()) {
        OC_LOG_WARN("[StorageRecovery] {} initialized but unavailable", item.label);
        return false;
    }
    return true;
}

class StorageRecoveryRuntimeManager {
public:
    core::persistence::ProductStorageRecoveryResult reconcileBoot(uint32_t nowMs) {
        if (!allStorageBackendsAvailable_() &&
            (!reopenStorageBackends_() || !allStorageBackendsAvailable_())) {
            return unavailableStorage_();
        }
        return reconcileProductStorage_(
            core::persistence::ProductStorageRecoveryMode::BOOT,
            nowMs
        );
    }

    void update(uint32_t nowMs, bool playing) {
        if (last_sample_ms_ != 0 &&
            static_cast<uint32_t>(nowMs - last_sample_ms_) < STORAGE_RECOVERY_SAMPLE_MS) {
            return;
        }
        last_sample_ms_ = nowMs;

        const bool mediaPresent = allStorageBackendsAvailable_();
        if (!mediaPresent && productFileService) {
            // Invalidate the exact lease and abort a live stream as soon as an
            // absence is observed. Debounce controls recovery scheduling only.
            productFileService->markMediaUnavailable();
        }
        const bool reconciliationRequired =
            mediaPresent && productFileService &&
            productFileService->storageState() !=
                core::persistence::ProductStorageState::READY &&
            productFileService->storageState() !=
                core::persistence::ProductStorageState::EXHAUSTED;

        const auto action = machine_.update({
            .mediaPresent = mediaPresent,
            .playing = playing,
            .reconciliationRequired = reconciliationRequired,
            .nowMs = nowMs,
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
        return !productFileService || productFileService->mediaPresent();
    }

    bool reopenStorageBackends_() const {
        bool ok = true;
        for (const auto& item : storageBackends) {
            const bool backendReady = item.backend->available()
                ? item.backend->reopen()
                : initializeStorageBackend(item);
            if (!backendReady) {
                OC_LOG_WARN("[StorageRecovery] Open/init failed for {}", item.label);
                ok = false;
            }
        }

        if (productFileService && !productFileService->mediaPresent()) {
            const auto initialized = productFileService->initForRecovery();
            if (!initialized) {
                OC_LOG_WARN(
                    "[StorageRecovery] Product filesystem init pending: {}",
                    oc::type::errorCodeToString(initialized.error().code)
                );
                ok = false;
            }
        }
        return ok && allStorageBackendsAvailable_();
    }

    static core::persistence::ProductStorageRecoveryResult unavailableStorage_() {
        core::persistence::ProductStorageRecoveryResult result{};
        result.status =
            core::persistence::ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE;
        result.error = oc::type::ErrorCode::HARDWARE_NOT_FOUND;
        return result;
    }

    core::persistence::ProductStorageRecoveryResult reconcileProductStorage_(
        core::persistence::ProductStorageRecoveryMode mode,
        uint32_t nowMs
    ) {
        if (!productFileService || !projectSessionRestoreService ||
            !projectSessionAutosaveService || !coreState) {
            core::persistence::ProductStorageRecoveryResult unavailable{};
            unavailable.status =
                core::persistence::ProductStorageRecoveryStatus::BUSY;
            unavailable.error = oc::type::ErrorCode::INVALID_STATE;
            OC_LOG_WARN("[StorageRecovery] Runtime services unavailable at {}ms", nowMs);
            return unavailable;
        }

        auto result = core::persistence::ProductStorageRecoveryService::reconcile(
            *productFileService,
            *projectSessionRestoreService,
            *projectSessionAutosaveService,
            *coreState,
            mode
        );
        if (!result.recovered()) {
            OC_LOG_WARN(
                "[StorageRecovery] Reconciliation failed at {}ms status={} error={}",
                nowMs,
                static_cast<unsigned>(result.status),
                oc::type::errorCodeToString(result.error)
            );
        }
        return result;
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
                OC_LOG_INFO("[StorageRecovery] Reconciling journal, settings and live session");
                const auto recovery = reconcileProductStorage_(
                    core::persistence::ProductStorageRecoveryMode::HOT_SWAP,
                    nowMs
                );
                const bool revalidateOk = recovery.recovered();
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

    core::persistence::StorageRecoveryMachine machine_{
        core::persistence::StorageRecoveryConfig{
            .removalDebounceMs = 1000,
            .insertionDebounceMs = 1000,
            .retryBackoffMs = 500,
        }
    };
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
core::app::ExtmemUniquePtr<core::validation::ux::SemanticUxRecorder>
    semanticUxRecorder;

}  // namespace
#endif

// =============================================================================
// Initialization Helpers
// =============================================================================

/// Halt only for non-storage peripherals which cannot run in a degraded mode.
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

static FLASHMEM bool initStorage() {
    bool storageBackendsReady = true;
    for (const auto& item : storageBackends) {
        if (!initializeStorageBackend(item)) {
            storageBackendsReady = false;
        }
    }

    productFileService.emplace(productFileSystemBackend);
#if defined(MS_PROJECT_STORE_SMOKE)
    const auto productFilesResult = productFileService->init();
#else
    const auto productFilesResult = productFileService->initForRecovery();
#endif
    if (!productFilesResult) {
        OC_LOG_WARN("[StorageRecovery] Product file service init pending: {}",
                    oc::type::errorCodeToString(productFilesResult.error().code));
    }

    const bool initialized = storageBackendsReady && productFilesResult;
    if (initialized) {
        OC_LOG_INFO(
            "Storage backends initialized settings={}B; reconciliation follows",
            deviceSettingsStorage.capacity()
        );
    } else {
        OC_LOG_WARN(
            "Storage initialization deferred; boot recovery will retry every {}ms",
            STORAGE_RECOVERY_SAMPLE_MS
        );
    }
    return initialized;
}

#if !defined(MS_PROJECT_STORE_SMOKE)
static FLASHMEM void initApp() {
    coreState.emplace(deviceSettingsStorage);
    projectSessionStore.emplace(*productFileService);
    projectSessionRestoreService.emplace(*projectSessionStore);
    projectSessionAutosaveService.emplace(*projectSessionStore);

    auto bootRecovery = storageRecovery.reconcileBoot(millis());
    while (!bootRecovery.recovered()) {
        OC_LOG_WARN(
            "[StorageRecovery] Boot reconciliation pending status={} error={}",
            static_cast<unsigned>(bootRecovery.status),
            oc::type::errorCodeToString(bootRecovery.error)
        );
        delay(STORAGE_RECOVERY_SAMPLE_MS);
        bootRecovery = storageRecovery.reconcileBoot(millis());
    }

    switch (bootRecovery.sessionRestoreStatus) {
        case core::persistence::ProjectSessionRestoreService::Status::RESTORED:
            OC_LOG_INFO("[ProjectSession] restored current.mspj bytes={}",
                        bootRecovery.sessionRestoreBytes);
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
    OC_LOG_INFO(
        "[StorageRecovery] Boot reconciliation complete session-save={}B",
        bootRecovery.sessionSaveBytes
    );

#if defined(MS_UX_RECORDER)
    semanticUxRecorder =
        core::app::makeExtmemUnique<core::validation::ux::SemanticUxRecorder>(
            core::validation::ux::SemanticUxRecorderOptions{
                .sink = &semanticUxSink,
                .enabled = true,
            }
        );
    if (!semanticUxRecorder) {
        OC_LOG_ERROR("Semantic UX recorder init failed: EXTMEM allocation failed");
        while (true) {}
    }
#endif

    oc::hal::teensy::AppBuilder appBuilder;
    appBuilder.midi()
        .frames()
        .encoders(Hardware::Encoder::ENCODERS)
        .buttons(
            Hardware::Button::BUTTONS,
            *mux,
            Config::Timing::DEBOUNCE_MS,
            Hardware::Mux::BUTTON_READS_PER_APP_TICK
        )
        .inputConfig(Config::Input::CONFIG);

#if defined(MS_UX_RECORDER)
    appBuilder.inputTrace([](const oc::core::input::InputBindingTraceEvent& event) {
        if (!semanticUxRecorder) return;
        if (!coreState) {
            semanticUxRecorder->onBindingTrace(event);
            return;
        }
        semanticUxRecorder->onBindingTrace(
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
                coreState->projectTracks,
                coreState->projectNavigation,
            coreState->statusBar,
            coreState->midiSync,
            coreState->sequencerTrackActivations,
            &coreState->midiCcCoordinator,
            &coreState->sequencerRuntimeProjectRevision,
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
#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("ui-ready");
#endif
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
    const bool storageReady = initStorage();
    if (storageReady && productFileService) {
        coreState.emplace(deviceSettingsStorage);
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
#if OC_ENABLE_STATS
    core::diagnostics::beginMemoryFootprintTracking();
    core::diagnostics::performanceReporter().begin();
#endif
    initDisplay();
    initLVGL();
    initMux();
    (void)initStorage();
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
    {
        OC_PERF_SCOPE(perfMainLoop, "main.loop");

        {
            OC_PERF_SCOPE(perfAppUpdate, "main.app-update");
            app->update();
        }

        // Sampling never pauses for an open stream: observed removal must
        // invalidate its lease and abort the backend handle immediately.
        {
            OC_PERF_SCOPE(perfStorageRecovery, "main.storage-recovery");
            storageRecovery.update(millis(), coreState->statusBar.playing.get());
        }

        const bool productFileWriteActive =
            productFileService && productFileService->writeSessionActive();
        const bool autosaveWriteActive =
            projectSessionAutosaveService &&
            projectSessionAutosaveService->writeSessionActive();
        const bool externalProductFileWriteActive =
            productFileWriteActive && !autosaveWriteActive;
        // Update state-side coalescing before evaluating the project autosave.
        if (!externalProductFileWriteActive) {
            {
                OC_PERF_SCOPE(perfCoreState, "main.core-state");
                coreState->update();
            }

            if (projectSessionAutosaveService && productFileService &&
                productFileService->available()) {
                OC_PERF_SCOPE(perfAutosave, "main.autosave");
                projectSessionAutosaveService->update(
                    *coreState,
                    millis()
                );
            }
        }

#if defined(MS_UX_RECORDER)
        if (semanticUxRecorder) {
            semanticUxRecorder->flush(millis(), *coreState);
        }
#endif

        // Refresh LVGL at lower frequency to reduce CPU load.
        lvglAccumulator += APP_PERIOD_US;
        if (lvglAccumulator >= LVGL_PERIOD_US) {
            lvglAccumulator = 0;
            lvgl->refresh();
        }
    }

#if OC_ENABLE_STATS
    core::diagnostics::performanceReporter().update(millis());
#endif
#endif
}
