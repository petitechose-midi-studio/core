#pragma once

#include "SdlEnvironment.hpp"

#include <oc/app/OpenControlApp.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace ms::entry {

using ExtraTickFn = void (*)(void*);

inline int run_native(
    sdl::SdlEnvironment& env,
    oc::app::OpenControlApp& app,
    void* extra_user = nullptr,
    ExtraTickFn extra = nullptr) {
    while (env.processEvents()) {
        app.update();
        if (extra) {
            extra(extra_user);
        }
        env.refresh();
    }
    return 0;
}

#ifdef __EMSCRIPTEN__
struct WasmLoopState {
    sdl::SdlEnvironment* env{};
    oc::app::OpenControlApp* app{};
    void* extra_user{};
    ExtraTickFn extra{};
};

inline void wasm_tick(void* user) {
    auto* st = static_cast<WasmLoopState*>(user);

    if (!st->env->processEvents()) {
        emscripten_cancel_main_loop();
        return;
    }

    st->app->update();
    if (st->extra) {
        st->extra(st->extra_user);
    }
    st->env->refresh();
}

inline int run_wasm(
    sdl::SdlEnvironment& env,
    oc::app::OpenControlApp& app,
    void* extra_user = nullptr,
    ExtraTickFn extra = nullptr) {
    static WasmLoopState st;
    st.env = &env;
    st.app = &app;
    st.extra_user = extra_user;
    st.extra = extra;

    emscripten_set_main_loop_arg(wasm_tick, &st, -1, true);
    return 0;
}
#endif

} // namespace ms::entry
