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

#include <oc/hal/sdl/Sdl.hpp>
#include <oc/hal/midi/LibreMidiTransport.hpp>
#include <oc/hal/net/WebSocketTransport.hpp>

#include <config/App.hpp>
#include "app/AppLogic.hpp"
#include "state/CoreState.hpp"

#include <emscripten.h>

// Global state for emscripten main loop callback
static sdl::SdlEnvironment* g_env = nullptr;
static oc::app::OpenControlApp* g_app = nullptr;
static core::state::CoreState* g_coreState = nullptr;

static void tick(void*) {
    if (!g_env->processEvents()) {
        emscripten_cancel_main_loop();
        return;
    }
    g_app->update();
    g_coreState->update();
    g_env->refresh();
}

int main(int argc, char** argv) {
    static sdl::SdlEnvironment env;
    static desktop::MemoryStorage storage;
    static core::state::CoreState coreState(storage);

    if (!storage.init()) {
        return 1;
    }

    if (!env.init(argc, argv)) {
        return 1;
    }

    static oc::app::OpenControlApp app = oc::hal::sdl::AppBuilder()
        .midi(std::make_unique<oc::hal::midi::LibreMidiTransport>(
            oc::hal::midi::LibreMidiConfig{
                .appName = "MIDI Studio WASM"
            }))
        .remote(std::make_unique<oc::hal::net::WebSocketTransport>(
            oc::hal::net::WebSocketConfig{
                .url = "ws://localhost:8001"
            }))
        .controllers(env.inputMapper())
        .inputConfig(Config::Input::CONFIG);

    core::app::registerContexts(app, coreState);
    app.begin();

    g_env = &env;
    g_app = &app;
    g_coreState = &coreState;

    emscripten_set_main_loop_arg(tick, nullptr, -1, true);
    return 0;
}
