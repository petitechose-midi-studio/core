/**
 * @file main-wasm.cpp
 * @brief WebAssembly entry point for MIDI Studio Core (Browser)
 *
 * Uses emscripten_set_main_loop for browser-compatible event loop.
 * Connects to oc-bridge via WebSocket for protocol communication.
 *
 * Storage: In-memory only (preview mode). Persistence via bridge REST API (future).
 */

#include "SdlEnvironment.hpp"
#include "MemoryStorage.hpp"

#include "entry/MidiDefaults.hpp"
#include "entry/SdlRunLoop.hpp"
#include "entry/BridgeArgs.hpp"
#include "entry/WasmArgs.hpp"

#include <cstdio>
#include <memory>
#include <optional>

#include <oc/hal/sdl/Sdl.hpp>
#include <oc/impl/HostFileSystem.hpp>
#include <oc/hal/midi/LibreMidiTransport.hpp>
#include <oc/hal/net/WebSocketTransport.hpp>

#include <config/App.hpp>
#include "app/AppLogic.hpp"
#include "context/standalone/StandaloneSequencerRuntimeHook.hpp"
#include "persistence/ProductFileService.hpp"
#include "sequencer/SequencerRuntimeService.hpp"
#include "state/CoreState.hpp"

static void tick_core_state(void* user) {
    static_cast<core::state::CoreState*>(user)->update();
}

int main(int argc, char** argv) {
    static sdl::SdlEnvironment env;
    static desktop::MemoryStorage deviceSettingsStorage;
    static std::optional<core::state::CoreState> coreState;
    static oc::impl::HostFileSystem productFilesystem("/midi-studio-wasm");
    static core::persistence::ProductFileService productFiles(productFilesystem);
    static std::unique_ptr<core::sequencer::SequencerRuntimeService> standaloneSequencerRuntime;

    if (!deviceSettingsStorage.init()) {
        return 1;
    }
    if (!coreState) {
        coreState.emplace(deviceSettingsStorage);
    }
    if (!productFilesystem.init() || !productFiles.init()) {
        return 1;
    }

    if (!env.init(argc, argv)) {
        return 1;
    }

    const auto midi = ms::wasm::parse_midi_args(argc, argv);
    const auto ws_url = ms::bridge::ws_url(argc, argv, "ws://localhost:8100");

    static oc::app::OpenControlApp app = oc::hal::sdl::AppBuilder()
        .midi(std::make_unique<oc::hal::midi::LibreMidiTransport>(
            ms::midi::make_wasm_config("MIDI Studio WASM", midi.in, midi.out)))
        .remote(std::make_unique<oc::hal::net::WebSocketTransport>(
            oc::hal::net::WebSocketConfig{
                .url = ws_url  // Controller: core wasm (configurable via --bridge-ws-url)
            }))
        .controllers(env.inputMapper())
        .inputConfig(Config::Input::CONFIG);

    if (!app.midiAPI()) {
        std::fprintf(stderr, "Sequencer runtime init failed: MIDI API unavailable\n");
        return 1;
    }

    standaloneSequencerRuntime =
        std::make_unique<core::sequencer::SequencerRuntimeService>(
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

    core::app::registerContexts(app, *coreState, productFiles);
    app.begin();

    return ms::entry::run_wasm(env, app, &(*coreState), tick_core_state);
}
