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
#include "app/ExtmemAllocator.hpp"
#include "context/StandaloneContext.hpp"
#include "context/standalone/StandaloneSequencerRuntimeHook.hpp"
#include "diagnostics/StorageQualificationProbe.hpp"
#include "persistence/PersistenceStatus.hpp"
#include "persistence/ProductDirectoryCatalog.hpp"
#include "persistence/ProductFileService.hpp"
#include "persistence/ProductStorageRecoveryService.hpp"
#include "persistence/ProductStorageRecoveryPlan.hpp"
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
static core::app::ExtmemUniquePtr<core::persistence::ProductDirectoryCatalog>
    productDirectoryCatalog;
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
constexpr uint32_t STORAGE_RECOVERY_RETRY_BACKOFF_MS = 5000;
const char kPersistenceTurnRejected[] PROGMEM =
    "[Persistence] Foreground turn rejected: {}";
const char kNoRecoveryErrorContext[] PROGMEM = "none";
const char kRecoveryFailureLog[] PROGMEM =
    "[StorageRecovery] Reconciliation failed at {}ms status={}({}) "
    "error={} stage={} context={}";
const char kRecoveryRetrySuspendedLog[] PROGMEM =
    "[StorageRecovery] Automatic retry suspended for media generation {}; "
    "RAM remains authoritative until media removal/reinsert";
const char kRecoveryRevalidationBackoffLog[] PROGMEM =
    "[StorageRecovery] Revalidation failed; retrying after backoff";
const char kRecoveryWorkspaceUnavailable[] PROGMEM =
    "storage recovery PSRAM workspace unavailable";
const char kStorageInitializationDeferredLog[] PROGMEM =
    "Storage initialization deferred; application will start degraded "
    "and wait for runtime media recovery";
const char kBootRecoveryDegradedLog[] PROGMEM =
    "[StorageRecovery] Boot degraded status={}({}) error={} stage={} "
    "context={}; continuing with RAM-authoritative state";
const char kBootRecoveryCompleteLog[] PROGMEM =
    "[StorageRecovery] Boot reconciliation complete session-save={}B";

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
        return reconcileProductStorageSynchronously_(nowMs);
    }

    void update(uint32_t nowMs, bool playing) {
        if (runtimeRecoveryActive_()) {
            const bool mediaPresent = allStorageBackendsAvailable_();
            if (!mediaPresent) {
                if (productFileService) productFileService->markMediaUnavailable();
                latched_media_generation_ = 0;
                recovery_plan_->cancel(
                    *productFileService,
                    *projectSessionAutosaveService,
                    oc::type::ErrorCode::HARDWARE_NOT_FOUND
                );
                recovery_job_ = {};
                (void)machine_.completeRevalidation(false, nowMs);
                return;
            }
            if (!playing) advanceRuntimeRecovery_(nowMs);
            return;
        }

        if (last_sample_ms_ != 0 &&
            static_cast<uint32_t>(nowMs - last_sample_ms_) <
                STORAGE_RECOVERY_SAMPLE_MS) {
            return;
        }
        last_sample_ms_ = nowMs;

        const bool mediaPresent = allStorageBackendsAvailable_();
        if (!mediaPresent && productFileService) {
            productFileService->markMediaUnavailable();
            latched_media_generation_ = 0;
        }
        if (mediaPresent && sameMediaRetryLatched_()) return;

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
    bool servicesAvailable_() const {
        return productFileService && projectSessionRestoreService &&
               projectSessionAutosaveService && coreState;
    }

    bool ensureRecoveryPlan_() {
        if (recovery_plan_) return true;
        recovery_plan_ = core::app::makeExtmemUniqueCold<
            core::persistence::ProductStorageRecoveryPlan>();
        return static_cast<bool>(recovery_plan_);
    }

    bool runtimeRecoveryActive_() const {
        return recovery_plan_ && recovery_plan_->active();
    }

    bool allStorageBackendsAvailable_() const {
        for (const auto& item : storageBackends) {
            if (!item.backend->available()) return false;
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

    core::persistence::ProductStorageRecoveryResult unavailableWorkspace_() const {
        core::persistence::ProductStorageRecoveryResult result{};
        result.status =
            core::persistence::ProductStorageRecoveryStatus::RESOURCE_EXHAUSTED;
        result.error = oc::type::ErrorCode::RESOURCE_EXHAUSTED;
        result.errorContext = kRecoveryWorkspaceUnavailable;
        return result;
    }

    core::persistence::ProductStorageRecoveryResult
    reconcileProductStorageSynchronously_(uint32_t nowMs) {
        if (!servicesAvailable_()) {
            auto result = unavailableStorage_();
            result.status = core::persistence::ProductStorageRecoveryStatus::BUSY;
            result.error = oc::type::ErrorCode::INVALID_STATE;
            return result;
        }
        if (!ensureRecoveryPlan_()) {
            auto result = unavailableWorkspace_();
            recordRecoveryResult_(result, nowMs);
            return result;
        }
        if (recovery_plan_->begin(
                *productFileService,
                *projectSessionAutosaveService,
                *coreState,
                core::persistence::ProductStorageRecoveryMode::BOOT
            )) {
            while (recovery_plan_->active()) {
                (void)recovery_plan_->advance(
                    *productFileService,
                    *projectSessionRestoreService,
                    *projectSessionAutosaveService,
                    *coreState
                );
            }
        }
        const auto result = recovery_plan_->result();
        recordRecoveryResult_(result, nowMs);
        return result;
    }

    void startRuntimeRecovery_(uint32_t nowMs) {
        if (!servicesAvailable_() || !ensureRecoveryPlan_()) {
            const auto result = servicesAvailable_()
                ? unavailableWorkspace_()
                : unavailableStorage_();
            recordRecoveryResult_(result, nowMs);
            (void)machine_.completeRevalidation(false, nowMs);
            return;
        }
        if (!recovery_plan_->begin(
                *productFileService,
                *projectSessionAutosaveService,
                *coreState,
                core::persistence::ProductStorageRecoveryMode::HOT_SWAP
            )) {
            const auto result = recovery_plan_->result();
            recordRecoveryResult_(result, nowMs);
            (void)machine_.completeRevalidation(false, nowMs);
            return;
        }

        auto admitted = productFileService->persistenceJobs().admit({
            .owner = core::persistence::ProductPersistenceJobOwner::STORAGE_RECOVERY,
            .nowMs = nowMs,
            .deadlineAfterMs = 0U,
            .quota = recovery_plan_->nextWorkQuota(
                *projectSessionAutosaveService
            ),
        });
        if (!admitted) {
            recovery_plan_->cancel(
                *productFileService,
                *projectSessionAutosaveService,
                admitted.error().code
            );
            const auto result = recovery_plan_->result();
            recordRecoveryResult_(result, nowMs);
            (void)machine_.completeRevalidation(false, nowMs);
            return;
        }
        recovery_job_ = std::move(admitted.value());
        OC_LOG_INFO(
            "[StorageRecovery] Cooperative stopped-only reconciliation scheduled"
        );
    }

    void advanceRuntimeRecovery_(uint32_t nowMs) {
        auto& jobs = productFileService->persistenceJobs();
        if (!jobs.owns(recovery_job_)) {
            recovery_plan_->cancel(
                *productFileService,
                *projectSessionAutosaveService,
                oc::type::ErrorCode::INVALID_STATE
            );
            recovery_job_ = {};
            finishRuntimeRecovery_(nowMs);
            return;
        }
        if (!jobs.isActive(recovery_job_)) return;

        const auto quota = recovery_plan_->nextWorkQuota(
            *projectSessionAutosaveService
        );
        if (!jobs.prepareAdvance(recovery_job_, quota) ||
            !jobs.claimAdvance(recovery_job_, nowMs)) {
            return;
        }

        core::persistence::ProductPersistenceWorkUsage usage{};
        const uint32_t startedMicros = micros();
        bool terminal = false;
        auto measured = productFileService->measurePersistenceWork(usage);
        if (measured) {
            auto measurement = std::move(measured.value());
            terminal = recovery_plan_->advance(
                *productFileService,
                *projectSessionRestoreService,
                *projectSessionAutosaveService,
                *coreState,
                &measurement
            );
        }
        usage.bytes += recovery_plan_->lastWorkBytes();
        usage.wallMicros = static_cast<uint32_t>(micros() - startedMicros);
        const auto finished = jobs.finishAdvance(recovery_job_, usage, true);
        if (!finished) {
            recovery_plan_->cancel(
                *productFileService,
                *projectSessionAutosaveService,
                finished.error().code
            );
            (void)jobs.cancelAfterUnwind(recovery_job_);
            recovery_job_ = {};
            finishRuntimeRecovery_(nowMs);
            return;
        }
        if (!terminal) return;

        if (!jobs.complete(recovery_job_)) {
            (void)jobs.cancelAfterUnwind(recovery_job_);
        }
        recovery_job_ = {};
        finishRuntimeRecovery_(nowMs);
    }

    void finishRuntimeRecovery_(uint32_t nowMs) {
        const auto result = recovery_plan_->result();
        recordRecoveryResult_(result, nowMs);
        const auto next = machine_.completeRevalidation(result.recovered(), nowMs);
        if (result.recovered()) {
            handleAction_(next, nowMs);
        } else if (!sameMediaRetryLatched_()) {
            OC_LOG_WARN(kRecoveryRevalidationBackoffLog);
        }
    }

    void recordRecoveryResult_(
        const core::persistence::ProductStorageRecoveryResult& result,
        uint32_t nowMs
    ) {
        if (!result.recovered()) {
            const char* context = result.errorContext
                ? result.errorContext
                : kNoRecoveryErrorContext;
            OC_LOG_WARN(
                kRecoveryFailureLog,
                nowMs,
                static_cast<unsigned>(result.status),
                core::persistence::productStorageRecoveryStatusLabel(result.status),
                oc::type::errorCodeToString(result.error),
                core::persistence::ProjectSessionAutosaveService::failureStageLabel(
                    result.sessionSaveFailureStage
                ),
                context
            );
            if (productFileService &&
                core::persistence::productStorageRecoveryRequiresMediaChange(
                    result.status
                )) {
                latched_media_generation_ =
                    productFileService->storageIdentity().mediaGeneration;
                OC_LOG_WARN(kRecoveryRetrySuspendedLog, latched_media_generation_);
            }
        } else {
            latched_media_generation_ = 0;
        }
    }

    bool sameMediaRetryLatched_() const {
        return latched_media_generation_ != 0 && productFileService &&
               productFileService->storageIdentity().mediaGeneration ==
                   latched_media_generation_;
    }

    void handleAction_(core::persistence::StorageRecoveryAction action, uint32_t nowMs) {
        switch (action) {
            case core::persistence::StorageRecoveryAction::MARK_OFFLINE:
                OC_LOG_WARN(
                    "[StorageRecovery] SD unavailable; runtime RAM remains authoritative"
                );
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
            case core::persistence::StorageRecoveryAction::ATTEMPT_REVALIDATE:
                startRuntimeRecovery_(nowMs);
                return;
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
            .retryBackoffMs = STORAGE_RECOVERY_RETRY_BACKOFF_MS,
        }
    };
    core::app::ExtmemUniquePtr<core::persistence::ProductStorageRecoveryPlan>
        recovery_plan_;
    core::persistence::ProductPersistenceJobToken recovery_job_{};
    uint32_t last_sample_ms_ = 0;
    uint32_t latched_media_generation_ = 0;
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
    productDirectoryCatalog =
        core::app::makeExtmemUniqueCold<core::persistence::ProductDirectoryCatalog>(
            *productFileService,
            &millis,
            &micros
        );
    if (!productDirectoryCatalog) {
        OC_LOG_ERROR("Product directory catalog PSRAM allocation failed");
        storageBackendsReady = false;
    }
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
        OC_LOG_WARN(kStorageInitializationDeferredLog);
    }
    return initialized;
}

#if !defined(MS_PROJECT_STORE_SMOKE)
static FLASHMEM void initApp() {
    if (!productDirectoryCatalog) {
        OC_LOG_ERROR("Product directory catalog unavailable");
        while (true) {}
    }
    coreState.emplace(deviceSettingsStorage);
    projectSessionStore.emplace(*productFileService);
    projectSessionRestoreService.emplace(*projectSessionStore);
    projectSessionAutosaveService.emplace(*projectSessionStore);

    const auto bootRecovery = storageRecovery.reconcileBoot(millis());
    if (!bootRecovery.recovered()) {
        const char* context = bootRecovery.errorContext
            ? bootRecovery.errorContext
            : kNoRecoveryErrorContext;
        OC_LOG_WARN(
            kBootRecoveryDegradedLog,
            static_cast<unsigned>(bootRecovery.status),
            core::persistence::productStorageRecoveryStatusLabel(bootRecovery.status),
            oc::type::errorCodeToString(bootRecovery.error),
            core::persistence::ProjectSessionAutosaveService::failureStageLabel(
                bootRecovery.sessionSaveFailureStage
            ),
            context
        );
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
    if (bootRecovery.recovered()) {
        OC_LOG_INFO(
            kBootRecoveryCompleteLog,
            bootRecovery.sessionSaveBytes
        );
    }

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
                *productFileService,
                *productDirectoryCatalog
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
    core::diagnostics::storage_qualification::begin();

    OC_LOG_INFO("=== MIDI Studio Core Boot ===");
    OC_LOG_INFO(
        "App {}Hz, LVGL {}Hz",
        Config::Timing::INPUT_APP_ADMISSION_HZ,
        Config::Timing::LVGL_SERVICE_HZ
    );

#if defined(MS_PROJECT_STORE_SMOKE)
    const bool storageReady = initStorage();
    if (storageReady && productFileService && productDirectoryCatalog) {
        coreState.emplace(deviceSettingsStorage);
        projectStoreSmokeResult =
            core::validation::project::runProjectStoreSmoke(
                *productFileService,
                *productDirectoryCatalog,
                *coreState
            );
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
constexpr uint32_t APP_PERIOD_US =
    1'000'000U / Config::Timing::INPUT_APP_ADMISSION_HZ;
constexpr uint32_t LVGL_PERIOD_US =
    1'000'000U / Config::Timing::LVGL_SERVICE_HZ;
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
    core::diagnostics::storage_qualification::foregroundBegin();
    {
        OC_PERF_SCOPE(perfMainLoop, "main.loop");

        {
            OC_PERF_SCOPE(perfAppUpdate, "main.app-update");
            app->update();
        }

        // State maintenance is unconditional. Persistence jobs are admitted
        // only after coalescers, transient state and pending applies advance.
        {
            OC_PERF_SCOPE(perfCoreState, "main.core-state");
            coreState->update();
        }

        const uint32_t persistenceNowMs = millis();
        bool persistenceTurnReady = false;
        if (productFileService) {
            const auto turn =
                productFileService->persistenceJobs().beginTurn(persistenceNowMs);
            persistenceTurnReady = static_cast<bool>(turn);
            if (!turn) {
                OC_LOG_ERROR(
                    kPersistenceTurnRejected,
                    oc::type::errorCodeToString(turn.error().code)
                );
            }
        }

        // Sampling never pauses for an open stream: observed removal must
        // invalidate its lease and abort the backend handle immediately.
        const bool playbackActive = coreState->statusBar.playing.get();
        {
            OC_PERF_SCOPE(perfStorageRecovery, "main.storage-recovery");
            storageRecovery.update(
                persistenceNowMs,
                playbackActive
            );
        }

        // The transport callback only retained the request in PSRAM. Execute
        // at most one admitted RPC advance after state maintenance and the
        // shared turn boundary have both completed.
        if (persistenceTurnReady && app &&
            app->contexts().activeId() ==
                static_cast<uint8_t>(Config::ContextID::STANDALONE)) {
            auto* activeContext = app->contexts().active();
            if (activeContext) {
                static_cast<core::context::StandaloneContext*>(activeContext)
                    ->advancePersistence(persistenceNowMs, playbackActive);
            }
        }

        if (persistenceTurnReady && projectSessionAutosaveService && productFileService &&
            productFileService->available()) {
            OC_PERF_SCOPE(perfAutosave, "main.autosave");
            projectSessionAutosaveService->update(
                *coreState,
                persistenceNowMs,
                false,
                playbackActive
            );
        }

#if defined(MS_UX_RECORDER)
        if (semanticUxRecorder) {
            semanticUxRecorder->flush(millis(), *coreState);
        }
#endif

        // Service LVGL on its independently owned lower cadence.
        lvglAccumulator += APP_PERIOD_US;
        if (lvglAccumulator >= LVGL_PERIOD_US) {
            lvglAccumulator = 0;
            lvgl->refresh();
        }
    }
    core::diagnostics::storage_qualification::foregroundEnd();

#if OC_ENABLE_STATS
    core::diagnostics::performanceReporter().update(millis());
#endif
    core::diagnostics::storage_qualification::update();
#endif
}
