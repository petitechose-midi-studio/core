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
#include "integration/CaptureScenarios.hpp"
#include "integration/InputBindingTraceWriter.hpp"
#include "integration/UxScenarioRunner.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include <SDL2/SDL.h>
#include <oc/hal/sdl/Sdl.hpp>
#include <oc/impl/FileStorage.hpp>
#include <oc/hal/midi/LibreMidiTransport.hpp>
#include <oc/hal/net/UdpTransport.hpp>

#include <config/App.hpp>
#include "app/AppLogic.hpp"
#include "context/standalone/StandaloneSequencerRuntimeHook.hpp"
#include "sequencer/SequencerRuntimeService.hpp"
#include "state/CoreState.hpp"

int main(int argc, char** argv) {
    // 1. Initialize SDL environment (SDL, LVGL, HwSimulator, InputMapper)
    sdl::SdlEnvironment env;
    if (!env.init(argc, argv)) {
        return 1;
    }

    // 2. Create storages and state (specific to core)
    oc::impl::FileStorage settingsStorage("./macros.bin");
    oc::impl::FileStorage macroWorkspaceStorage("./macro-workspace.bin");
    oc::impl::FileStorage macroLibraryStorage("./macro-library.bin");
    oc::impl::FileStorage sequencerWorkspaceStorage("./sequencer-workspace.bin");
    oc::impl::FileStorage sequencerPatternLibraryStorage("./sequencer-pattern-library.bin");
    oc::impl::FileStorage sequencerSetLibraryStorage("./sequencer-set-library.bin");
    if (!settingsStorage.init() ||
        !macroWorkspaceStorage.init() ||
        !macroLibraryStorage.init() ||
        !sequencerWorkspaceStorage.init() ||
        !sequencerPatternLibraryStorage.init() ||
        !sequencerSetLibraryStorage.init()) {
        fprintf(stderr, "Failed to open storage files\n");
        return 1;
    }
    core::state::CoreState coreState(settingsStorage,
                                     macroWorkspaceStorage,
                                     macroLibraryStorage,
                                     sequencerWorkspaceStorage,
                                     sequencerPatternLibraryStorage,
                                     sequencerSetLibraryStorage);

    const int bridge_udp_port = ms::bridge::udp_port(argc, argv, 8000);
    const char* uxScript = ms::args::value(argc, argv, "--ux-script");
    const char* uxOutputArg = ms::args::value(argc, argv, "--ux-output");
    const char* uxOutput = uxOutputArg ? uxOutputArg : ".captures/ux-run";
    std::string bindingTracePath;
    sdl::integration::InputBindingTraceWriter bindingTrace;

    if (uxScript) {
        std::filesystem::create_directories(uxOutput);
        bindingTracePath = (std::filesystem::path(uxOutput) / "binding-trace.ndjson").string();
        if (!bindingTrace.open(bindingTracePath.c_str())) {
            std::fprintf(stderr, "Failed to open binding trace: %s\n", bindingTrace.error().c_str());
            return 1;
        }
    }

    // 3. Build application with MIDI transport
    oc::app::OpenControlApp app = oc::hal::sdl::AppBuilder()
        .midi(std::make_unique<oc::hal::midi::LibreMidiTransport>(
            ms::midi::make_native_config("MIDI Studio")))
        .remote(std::make_unique<oc::hal::net::UdpTransport>(
            oc::hal::net::UdpConfig{
                .host = "127.0.0.1",
                .port = static_cast<uint16_t>(bridge_udp_port)  // --bridge-udp-port
            }))
        .controllers(env.inputMapper())
        .inputConfig(Config::Input::CONFIG)
        .inputTrace([&bindingTrace](const oc::core::input::InputBindingTraceEvent& event) {
            bindingTrace.write(event);
        });

    if (!app.midiAPI()) {
        std::fprintf(stderr, "Sequencer runtime init failed: MIDI API unavailable\n");
        return 1;
    }

    auto standaloneSequencerRuntime =
        std::make_unique<core::sequencer::SequencerRuntimeService>(
            core::sequencer::SequencerRuntimeService::StateRefs{
                coreState.sequencer,
                coreState.sequencerTracks,
                coreState.statusBar,
                coreState.midiSync,
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
    core::app::registerContexts(app, coreState);
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
            }
        );
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
        &coreState,
        [](void* user) { static_cast<core::state::CoreState*>(user)->update(); }
    );

    // 6. Cleanup: handled by destructors in correct order
    //    (app destroyed first, then env calls SDL_Quit)
}
