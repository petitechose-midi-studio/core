/**
 * @file main-native.cpp
 * @brief Native entry point for MIDI Studio Core (Windows/Linux/macOS)
 *
 * Demonstrates SdlEnvironment usage with explicit app setup.
 */

#define SDL_MAIN_HANDLED
#include "SdlEnvironment.hpp"

#include "entry/Args.hpp"
#include "entry/MidiDefaults.hpp"
#include "entry/SdlRunLoop.hpp"
#include "entry/BridgeArgs.hpp"
#include "entry/SdlProjectSessionRuntime.hpp"
#include "integration/CaptureScenarios.hpp"
#include "integration/InputBindingTraceWriter.hpp"
#include "integration/UxScenarioRunner.hpp"

#include <cstdio>
#include <cstdlib>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <SDL2/SDL.h>
#include <oc/hal/sdl/Sdl.hpp>
#include <oc/impl/FileStorage.hpp>
#include <oc/impl/HostFileSystem.hpp>
#include <oc/impl/NullMidi.hpp>
#include <oc/hal/midi/LibreMidiTransport.hpp>
#include <oc/hal/net/UdpTransport.hpp>

#include <config/App.hpp>
#include "app/AppLogic.hpp"
#include "app/ExtmemAllocator.hpp"
#include "context/standalone/StandaloneSequencerRuntimeHook.hpp"
#include "persistence/ProductDirectoryCatalog.hpp"
#include "persistence/ProductFileService.hpp"
#include "sequencer/SequencerRuntimeService.hpp"
#include "state/CoreState.hpp"
#include "validation/ux/SemanticUxRecorder.hpp"

namespace {

constexpr std::array<const char*, 1> kStorageFiles = {
    "./core-settings.bin",
};

bool removeStorageFilesForUxRun() {
    bool ok = true;
    for (const char* path : kStorageFiles) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) {
            std::fprintf(stderr, "Failed to reset UX storage file %s: %s\n", path, ec.message().c_str());
            ok = false;
        }
    }
    return ok;
}

class NdjsonSemanticUxSink final : public core::validation::ux::SemanticUxLineSink {
public:
    bool open(const std::filesystem::path& path) {
        stream_.open(path, std::ios::out | std::ios::trunc);
        failed_ = !stream_.is_open();
        return !failed_;
    }

    void writeLine(const char* line) override {
        if (failed_ || !line) return;
        constexpr char prefix[] = "UXR ";
        const char* json = std::strncmp(line, prefix, sizeof(prefix) - 1U) == 0
            ? line + sizeof(prefix) - 1U
            : line;
        stream_ << json << '\n';
        stream_.flush();
        failed_ = !stream_.good();
    }

    bool good() const { return !failed_; }

private:
    std::ofstream stream_;
    bool failed_ = false;
};

void reportProjectSessionRestore(
    const core::persistence::ProjectSessionRestoreService::Result& result
) {
    using Status = core::persistence::ProjectSessionRestoreService::Status;
    switch (result.status) {
        case Status::RESTORED:
            std::fprintf(stdout,
                         "[ProjectSession] restored current.mspj bytes=%u\n",
                         static_cast<unsigned>(result.bytes));
            return;
        case Status::MISSING:
            std::fprintf(stdout,
                         "[ProjectSession] no current.mspj; using default session\n");
            return;
        case Status::APPLY_FAILED:
            std::fprintf(stderr,
                         "[ProjectSession] current.mspj apply failed; using default session\n");
            return;
        case Status::DEGRADED:
        default:
            std::fprintf(stderr,
                         "[ProjectSession] current.mspj unavailable/corrupt; using default session\n");
            return;
    }
}

}  // namespace

int main(int argc, char** argv) {
    // 1. Initialize SDL environment (SDL, LVGL, HwSimulator, InputMapper)
    sdl::SdlEnvironment env;
    if (!env.init(argc, argv)) {
        return 1;
    }

    const char* uxScript = ms::args::value(argc, argv, "--ux-script");
    const char* uxOutputArg = ms::args::value(argc, argv, "--ux-output");
    const char* uxOutput = uxOutputArg ? uxOutputArg : ".captures/ux-run";
    const bool resetUxStorage = uxScript && !ms::args::has(argc, argv, "--ux-keep-storage");
    if (resetUxStorage) {
        if (!removeStorageFilesForUxRun()) {
            return 1;
        }
    }

    // 2. Create storages and state (specific to core)
    oc::impl::FileStorage deviceSettingsStorage(kStorageFiles[0]);
    if (!deviceSettingsStorage.init()) {
        fprintf(stderr, "Failed to open storage files\n");
        return 1;
    }
    core::state::CoreState coreState(deviceSettingsStorage);

    std::filesystem::path productFileRoot = uxScript
        ? std::filesystem::path(uxOutput) / "product-files"
        : std::filesystem::path(".runtime") / "core-product-files";
    if (resetUxStorage) {
        std::error_code ec;
        std::filesystem::remove_all(productFileRoot, ec);
        if (ec) {
            std::fprintf(stderr,
                         "Failed to reset UX product file root %s: %s\n",
                         productFileRoot.string().c_str(),
                         ec.message().c_str());
            return 1;
        }
    }
    oc::impl::HostFileSystem productFilesystem(productFileRoot.string().c_str());
    if (!productFilesystem.init()) {
        std::fprintf(stderr, "Failed to initialize product filesystem\n");
        return 1;
    }
    core::persistence::ProductFileService productFiles(productFilesystem);
    if (!productFiles.init()) {
        std::fprintf(stderr, "Failed to initialize product file service\n");
        return 1;
    }
    auto productDirectoryCatalog =
        core::app::makeExtmemUniqueCold<core::persistence::ProductDirectoryCatalog>(
            productFiles
        );
    if (!productDirectoryCatalog) {
        std::fprintf(stderr, "Failed to allocate product directory catalog\n");
        return 1;
    }
    const int bridge_udp_port = ms::bridge::udp_port(argc, argv, 8000);
    std::string bindingTracePath;
    sdl::integration::InputBindingTraceWriter bindingTrace;
    NdjsonSemanticUxSink semanticTrace;
    core::validation::ux::SemanticUxRecorder semanticRecorder;

    if (uxScript) {
        std::filesystem::create_directories(uxOutput);
        bindingTracePath = (std::filesystem::path(uxOutput) / "binding-trace.ndjson").string();
        if (!bindingTrace.open(bindingTracePath.c_str())) {
            std::fprintf(stderr, "Failed to open binding trace: %s\n", bindingTrace.error().c_str());
            return 1;
        }
        if (!semanticTrace.open(std::filesystem::path(uxOutput) /
                                "semantic-trace.ndjson")) {
            std::fprintf(stderr, "Failed to open semantic UX trace\n");
            return 1;
        }
        semanticRecorder.configure({.sink = &semanticTrace, .enabled = true});
    }
    core::validation::ux::setCurrentEncoderContractTraceRecorder(
        &semanticRecorder
    );

    // UX workflows must not inherit clock/transport traffic from the user's
    // live loopMIDI ports. They drive transport explicitly through scripted
    // controller input, so a no-op transport keeps captures deterministic.
    std::unique_ptr<oc::interface::IMidi> midiTransport;
    if (uxScript) {
        midiTransport = std::make_unique<oc::impl::NullMidi>();
    } else {
        midiTransport = std::make_unique<oc::hal::midi::LibreMidiTransport>(
            ms::midi::make_native_config("MIDI Studio")
        );
    }

    // 3. Build application with MIDI transport
    oc::app::OpenControlApp app = oc::hal::sdl::AppBuilder()
        .midi(std::move(midiTransport))
        .remote(std::make_unique<oc::hal::net::UdpTransport>(
            oc::hal::net::UdpConfig{
                .host = "127.0.0.1",
                .port = static_cast<uint16_t>(bridge_udp_port)  // --bridge-udp-port
            }))
        .controllers(env.inputMapper())
        .inputConfig(Config::Input::CONFIG)
        .inputTrace([&](const oc::core::input::InputBindingTraceEvent& event) {
            bindingTrace.write(event);
            semanticRecorder.onBindingTrace(
                event,
                core::validation::ux::makeSemanticUxSnapshot(coreState)
            );
        });

    ms::entry::SdlProjectSessionRuntime projectSessionRuntime(
        productFiles,
        coreState,
        0U,
        &app,
        [](void* context, uint32_t nowMs, bool playbackActive) {
            auto* app = static_cast<oc::app::OpenControlApp*>(context);
            if (!app || app->contexts().activeId() !=
                            static_cast<uint8_t>(Config::ContextID::STANDALONE)) {
                return;
            }
            auto* activeContext = app->contexts().active();
            if (activeContext) {
                static_cast<core::context::StandaloneContext*>(activeContext)
                    ->advancePersistence(nowMs, playbackActive);
            }
        }
    );
    reportProjectSessionRestore(projectSessionRuntime.restoreResult());

    if (!app.midiAPI()) {
        std::fprintf(stderr, "Sequencer runtime init failed: MIDI API unavailable\n");
        return 1;
    }

    auto standaloneSequencerRuntime =
        std::make_unique<core::sequencer::SequencerRuntimeService>(
            core::sequencer::SequencerRuntimeService::StateRefs{
                coreState.sequencer,
                coreState.sequencerTracks,
                coreState.projectTracks,
                coreState.projectNavigation,
                coreState.statusBar,
                coreState.midiSync,
                coreState.sequencerTrackActivations,
                &coreState.midiCcCoordinator,
                &coreState.sequencerRuntimeProjectRevision,
            },
            *app.midiAPI(),
            app.eventBus()
        );

    const bool runtimeHookRegistered =
        core::context::standalone::registerStandaloneSequencerRuntimeHook(
            app,
            standaloneSequencerRuntime
        );
    if (!runtimeHookRegistered) {
        std::fprintf(stderr, "Sequencer runtime init failed: app pre-context hook registry full\n");
        return 1;
    }

    // 4. Register contexts and start
    // Note: Contexts use Screen::root() which is configured to HwSimulator's screenArea
    core::app::registerContexts(
        app,
        coreState,
        productFiles,
        *productDirectoryCatalog
    );
    app.begin();

    if (uxScript) {
        sdl::integration::UxScenarioRunner runner;
        const bool ok = runner.run(
            {.scriptPath = uxScript, .outputDir = uxOutput},
            env,
            app,
            coreState,
            [&coreState](const char* scenario) {
                return sdl::integration::applyCaptureScenario(coreState, scenario);
            },
            [&projectSessionRuntime, &semanticRecorder, &coreState]() {
                projectSessionRuntime.update();
                semanticRecorder.flush(SDL_GetTicks(), coreState);
            },
            [&semanticRecorder, &coreState](const char* label) {
                semanticRecorder.capture(SDL_GetTicks(), label, coreState);
            },
            [&semanticRecorder]() {
                semanticRecorder.resetCaptureContext(true);
            }
        );
        semanticRecorder.flush(SDL_GetTicks(), coreState);
        if (!semanticTrace.good()) {
            std::fprintf(stderr, "Failed to write semantic UX trace\n");
            return 1;
        }
        if (!ok) {
            std::fprintf(stderr, "UX scenario failed: %s\n", runner.error().c_str());
            return 1;
        }
        return 0;
    }

    if (const char* capturePath = ms::args::value(argc, argv, "--capture-bmp")) {
        const char* scenario = ms::args::value(argc, argv, "--capture-scenario");
        const int frames = ms::args::int_value(argc, argv, "--capture-frames", 12);
        const sdl::ScreenshotScope captureScope =
            sdl::integration::captureScopeFromArg(ms::args::value(argc, argv, "--capture-scope"));
        if (!sdl::integration::applyCaptureScenario(coreState, scenario)) {
            return 1;
        }
        sdl::integration::tickFrames(env, app, coreState, frames);
        if (!env.saveScreenshotBmp(capturePath, captureScope)) {
            std::fprintf(stderr, "Failed to save capture: %s\n", capturePath);
            return 1;
        }
        return 0;
    }

    // 5. Main loop
    return ms::entry::run_native(
        env,
        app,
        &projectSessionRuntime,
        [](void* user) {
            static_cast<ms::entry::SdlProjectSessionRuntime*>(user)->update();
        }
    );

    // 6. Cleanup: handled by destructors in correct order
    //    (app destroyed first, then env calls SDL_Quit)
}
