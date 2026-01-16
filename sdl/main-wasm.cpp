/**
 * @file main-wasm.cpp
 * @brief WebAssembly entry point (Browser)
 */

#include "SdlRunner.hpp"
#include <emscripten.h>

static SdlRunner g_runner;

static void tick(void*) {
    if (!g_runner.tick()) {
        emscripten_cancel_main_loop();
    }
}

int main(int argc, char** argv) {
    if (!g_runner.init(argc, argv)) {
        return 1;
    }
    emscripten_set_main_loop_arg(tick, nullptr, -1, true);
    return 0;
}
