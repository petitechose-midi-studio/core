/**
 * @file main-native.cpp
 * @brief Native entry point for MIDI Studio Core (Windows/Linux/macOS)
 *
 * Demonstrates SdlEnvironment usage with explicit app setup.
 */

#define SDL_MAIN_HANDLED
#include "SdlEnvironment.hpp"

#include "entry/MidiDefaults.hpp"
#include "entry/SdlRunLoop.hpp"
#include "entry/BridgeArgs.hpp"

#include <oc/hal/sdl/Sdl.hpp>
#include <oc/impl/FileStorage.hpp>
#include <oc/hal/midi/LibreMidiTransport.hpp>
#include <oc/hal/net/UdpTransport.hpp>

#include <config/App.hpp>
#include "app/AppLogic.hpp"
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
        .inputConfig(Config::Input::CONFIG);

    // 4. Register contexts and start
    // Note: Contexts use Screen::root() which is configured to HwSimulator's screenArea
    core::app::registerContexts(app, coreState);
    app.begin();

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
