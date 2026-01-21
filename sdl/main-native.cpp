/**
 * @file main-native.cpp
 * @brief Native entry point for MIDI Studio Core (Windows/Linux/macOS)
 *
 * Demonstrates SdlEnvironment usage with explicit app setup.
 */

#define SDL_MAIN_HANDLED
#include "SdlEnvironment.hpp"

#include <oc/hal/sdl/Sdl.hpp>
#include <oc/impl/FileStorage.hpp>
#include <oc/hal/midi/LibreMidiTransport.hpp>

#include <config/App.hpp>
#include "app/AppLogic.hpp"
#include "state/CoreState.hpp"

int main(int argc, char** argv) {
    // 1. Initialize SDL environment (SDL, LVGL, HwSimulator, InputMapper)
    sdl::SdlEnvironment env;
    if (!env.init(argc, argv)) {
        return 1;
    }

    // 2. Create storage and state (specific to core)
    oc::impl::FileStorage storage("./macros.bin");
    if (!storage.init()) {
        fprintf(stderr, "Failed to open storage file\n");
        return 1;
    }
    core::state::CoreState coreState(storage);

    // 3. Build application with MIDI transport
    oc::app::OpenControlApp app = oc::hal::sdl::AppBuilder()
        .midi(std::make_unique<oc::hal::midi::LibreMidiTransport>(
            oc::hal::midi::LibreMidiConfig{
                .appName = "MIDI Studio",
                .inputPortPattern = "IN [core-desktop]",
                .outputPortPattern = "OUT [core-desktop]"
            }))
        .controllers(env.inputMapper())
        .inputConfig(Config::Input::CONFIG);

    // 4. Register contexts and start
    // Note: Contexts use Screen::root() which is configured to HwSimulator's screenArea
    core::app::registerContexts(app, coreState);
    app.begin();

    // 5. Main loop
    while (env.processEvents()) {
        app.update();
        coreState.update();
        env.refresh();
    }

    // 6. Cleanup: handled by destructors in correct order
    //    (app destroyed first, then env calls SDL_Quit)
    return 0;
}
