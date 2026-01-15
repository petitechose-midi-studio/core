/**
 * @file main.cpp
 * @brief Desktop/WASM entry point for MIDI Studio
 *
 * Uses SDL2 for display and input.
 * Browser builds use emscripten_set_main_loop_arg().
 */

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <emscripten.h>
#include <optional>
#include <cstdlib>

#include <lvgl.h>

// LVGL SDL function (from lv_sdl_*.c)
extern "C" {
    void lv_sdl_mouse_handler(SDL_Event* event);
}

#include <oc/hal/desktop/Sdl.hpp>
#include <oc/ui/lvgl/SdlBridge.hpp>

#include "app/AppLogic.hpp"
#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include "state/CoreState.hpp"
#include "MemoryStorage.hpp"
#include "HwLayout.hpp"
#include "HwSimulator.hpp"

// =============================================================================
// Application Context (passed to main loop callback)
// =============================================================================
struct AppContext {
    oc::ui::lvgl::SdlBridge* bridge;
    oc::hal::desktop::InputMapper* input;
    oc::app::OpenControlApp* app;
    core::state::CoreState* coreState;
    desktop::HwSimulator* hwSim;
    SDL_Renderer* renderer;
    bool running;
};

static AppContext g_ctx;

// =============================================================================
// Main Loop Iteration (called by Emscripten)
// =============================================================================
void main_loop_iteration(void* arg) {
    AppContext* ctx = static_cast<AppContext*>(arg);

    // --- Events ---
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Forward to LVGL for widget interaction
        lv_sdl_mouse_handler(&event);

        if (event.type == SDL_QUIT) {
            ctx->running = false;
            // In browser, we don't actually quit, but we can stop the loop
            emscripten_cancel_main_loop();
            return;
        }
        else if (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_RESIZED) {
            lv_obj_invalidate(lv_screen_active());
        }
        else if (event.type == SDL_MOUSEWHEEL) {
            int sdlX, sdlY;
            SDL_GetMouseState(&sdlX, &sdlY);
            float lvglX, lvglY;
            SDL_RenderWindowToLogical(ctx->renderer, sdlX, sdlY, &lvglX, &lvglY);
            ctx->hwSim->handleMouseWheel(static_cast<int>(lvglX), static_cast<int>(lvglY), event.wheel.y);
        }

        ctx->input->handleEvent(event);
    }

    // --- Update ---
    ctx->app->update();
    ctx->coreState->update();

    // --- Render ---
    ctx->bridge->refresh();
}

// =============================================================================
// Main Entry Point
// =============================================================================
int main(int argc, char* argv[]) {
    using namespace Config;

    // ══════════════════════════════════════════════════════════════
    // Parse resolution from command line (set by HTML/JS)
    // Default: 1053x1053 (same as native desktop)
    // ══════════════════════════════════════════════════════════════
    int windowSize = 1053;
    if (argc > 1) {
        windowSize = atoi(argv[1]);
        if (windowSize < 400) windowSize = 1053;
    }

    printf("MIDI Studio WASM starting with window size: %d\n", windowSize);

    // ══════════════════════════════════════════════════════════════
    // SDL Initialization
    // ══════════════════════════════════════════════════════════════
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    // ══════════════════════════════════════════════════════════════
    // Layout configuration
    // ══════════════════════════════════════════════════════════════
    constexpr int SCREEN_W = 320;
    constexpr float PANEL_TO_SCREEN_RATIO = 190.0f / 60.0f;
    constexpr int PANEL_SIZE = static_cast<int>(SCREEN_W * PANEL_TO_SCREEN_RATIO);
    constexpr int MARGIN = 40;
    const int LVGL_SIZE = PANEL_SIZE + MARGIN;

    auto layout = desktop::HwLayout::fit(LVGL_SIZE, MARGIN / 2);

    // ══════════════════════════════════════════════════════════════
    // LVGL + SDL Bridge
    // ══════════════════════════════════════════════════════════════
    static oc::ui::lvgl::SdlBridge bridge(
        LVGL_SIZE, LVGL_SIZE,
        oc::hal::desktop::defaultTimeProvider,
        {.windowTitle = "MIDI Studio", .createInputDevices = true}
    );
    bridge.init();

    SDL_Window* window = bridge.getWindow();
    SDL_Renderer* renderer = bridge.getRenderer();

    if (renderer) {
        SDL_RenderSetLogicalSize(renderer, LVGL_SIZE, LVGL_SIZE);
    }

    // Dark background
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // ══════════════════════════════════════════════════════════════
    // HwSimulator
    // ══════════════════════════════════════════════════════════════
    static desktop::HwSimulator hwSim(screen);
    hwSim.init(layout);

    // ══════════════════════════════════════════════════════════════
    // InputMapper
    // ══════════════════════════════════════════════════════════════
    static oc::hal::desktop::InputMapper input;
    hwSim.setInputMapper(&input);

    input.setButtonFeedback([](oc::hal::ButtonID id, bool pressed) {
        hwSim.setButtonPressed(id, pressed);
    });
    input.setEncoderFeedback([](oc::hal::EncoderID id, float value) {
        hwSim.setEncoderValue(id, value);
    });

    // Keyboard shortcuts
    input
        .button(SDLK_ESCAPE, static_cast<oc::hal::ButtonID>(ButtonID::LEFT_TOP))
        .button(SDLK_q, static_cast<oc::hal::ButtonID>(ButtonID::LEFT_CENTER))
        .button(SDLK_a, static_cast<oc::hal::ButtonID>(ButtonID::LEFT_BOTTOM))
        .button(SDLK_COMMA, static_cast<oc::hal::ButtonID>(ButtonID::BOTTOM_LEFT))
        .button(SDLK_PERIOD, static_cast<oc::hal::ButtonID>(ButtonID::BOTTOM_CENTER))
        .button(SDLK_SLASH, static_cast<oc::hal::ButtonID>(ButtonID::BOTTOM_RIGHT))
        .button(SDLK_1, static_cast<oc::hal::ButtonID>(ButtonID::MACRO_1))
        .button(SDLK_2, static_cast<oc::hal::ButtonID>(ButtonID::MACRO_2))
        .button(SDLK_3, static_cast<oc::hal::ButtonID>(ButtonID::MACRO_3))
        .button(SDLK_4, static_cast<oc::hal::ButtonID>(ButtonID::MACRO_4))
        .button(SDLK_5, static_cast<oc::hal::ButtonID>(ButtonID::MACRO_5))
        .button(SDLK_6, static_cast<oc::hal::ButtonID>(ButtonID::MACRO_6))
        .button(SDLK_7, static_cast<oc::hal::ButtonID>(ButtonID::MACRO_7))
        .button(SDLK_8, static_cast<oc::hal::ButtonID>(ButtonID::MACRO_8))
        .button(SDLK_SPACE, static_cast<oc::hal::ButtonID>(ButtonID::NAV))
        .encoder(SDLK_UP, SDLK_DOWN, static_cast<oc::hal::EncoderID>(EncoderID::NAV), 0.05f)
        .encoder(SDLK_LEFT, SDLK_RIGHT, static_cast<oc::hal::EncoderID>(EncoderID::OPT), 0.02f);

    // ══════════════════════════════════════════════════════════════
    // Application
    // ══════════════════════════════════════════════════════════════
    static desktop::MemoryStorage storage;
    static core::state::CoreState coreState(storage);

    // Build app with real MIDI (WebMIDI in browser, native on desktop)
    // Port naming convention: "MIDI Studio IN" = DAW receives, "MIDI Studio OUT" = DAW sends
    // So our app: reads from "OUT" (DAW sends to us), writes to "IN" (we send to DAW)
    static oc::app::OpenControlApp app = oc::hal::desktop::AppBuilder()
                                             .midi({
                                                 .appName = "MIDI Studio",
                                                 .inputPortPattern = "IN [core-desktop]",   // Read from DAW's output
                                                 .outputPortPattern = "OUT [core-desktop]"  // Write to DAW's input
                                             })
                                             .controllers(input)
                                             .inputConfig(Config::Input::CONFIG);

    core::app::registerContexts(app, coreState);
    app.begin();

    // Reparent app UI into the screen area
    lv_obj_t* screenArea = hwSim.getScreenArea();
    lv_obj_t* appUI = lv_obj_get_child(screen, 1);
    if (appUI && appUI != hwSim.getPanel()) {
        lv_obj_set_parent(appUI, screenArea);
        lv_obj_set_pos(appUI, 0, 0);
        lv_obj_set_size(appUI, layout.screenW, layout.screenH);
    }

    // ══════════════════════════════════════════════════════════════
    // Setup context and start Emscripten main loop
    // ══════════════════════════════════════════════════════════════
    g_ctx.bridge = &bridge;
    g_ctx.input = &input;
    g_ctx.app = &app;
    g_ctx.coreState = &coreState;
    g_ctx.hwSim = &hwSim;
    g_ctx.renderer = renderer;
    g_ctx.running = true;

    printf("Starting Emscripten main loop...\n");

    // -1 = use requestAnimationFrame (synced to display refresh)
    // true = simulate infinite loop (don't return from main)
    emscripten_set_main_loop_arg(main_loop_iteration, &g_ctx, -1, true);

    // This line is never reached (emscripten_set_main_loop doesn't return)
    return 0;
}
