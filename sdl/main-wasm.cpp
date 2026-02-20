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

#include <oc/hal/sdl/Sdl.hpp>
#include <oc/hal/midi/LibreMidiTransport.hpp>
#include <oc/hal/net/WebSocketTransport.hpp>

#include <config/App.hpp>
#include "app/AppLogic.hpp"
#include "state/CoreState.hpp"

static void tick_core_state(void* user) {
    static_cast<core::state::CoreState*>(user)->update();
}

int main(int argc, char** argv) {
    static sdl::SdlEnvironment env;
    static desktop::MemoryStorage settingsStorage;
    static desktop::MemoryStorage macroWorkspaceStorage;
    static desktop::MemoryStorage macroLibraryStorage;
    static desktop::MemoryStorage sequencerWorkspaceStorage;
    static desktop::MemoryStorage sequencerPatternLibraryStorage;
    static desktop::MemoryStorage sequencerSetLibraryStorage;
    static core::state::CoreState coreState(settingsStorage,
                                            macroWorkspaceStorage,
                                            macroLibraryStorage,
                                            sequencerWorkspaceStorage,
                                            sequencerPatternLibraryStorage,
                                            sequencerSetLibraryStorage);

    if (!settingsStorage.init() ||
        !macroWorkspaceStorage.init() ||
        !macroLibraryStorage.init() ||
        !sequencerWorkspaceStorage.init() ||
        !sequencerPatternLibraryStorage.init() ||
        !sequencerSetLibraryStorage.init()) {
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

    core::app::registerContexts(app, coreState);
    app.begin();

    return ms::entry::run_wasm(env, app, &coreState, tick_core_state);
}
