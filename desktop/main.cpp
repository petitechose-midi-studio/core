#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <optional>

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

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    using namespace Config;

    // ══════════════════════════════════════════════════════════════
    // Get display DPI for real-size rendering
    // ══════════════════════════════════════════════════════════════

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

    // Enable linear filtering for better scaling quality (vs pixelated "nearest")
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    // Enable VSync to avoid tearing and unnecessary frames
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    // Real DPI for 27" UHD (3840x2160) = 163 DPI
    // Note: SDL_GetDisplayDPI returns 96 on Windows (logical DPI, not physical)
    constexpr float hdpi = 163.0f;

    // ══════════════════════════════════════════════════════════════
    // Layout configuration - Screen is ALWAYS 320x240 pixels
    // ══════════════════════════════════════════════════════════════

    // Calculate panel size from fixed screen size (320x240)
    // Screen = 60mm wide, Panel = 190mm → ratio = 190/60 ≈ 3.167
    constexpr int SCREEN_W = 320;
    constexpr float PANEL_TO_SCREEN_RATIO = 190.0f / 60.0f;  // ~3.167
    constexpr int PANEL_SIZE = static_cast<int>(SCREEN_W * PANEL_TO_SCREEN_RATIO);  // ~1013
    constexpr int MARGIN = 40;
    constexpr int LVGL_SIZE = PANEL_SIZE + MARGIN;  // ~1053

    auto layout = desktop::HwLayout::fit(LVGL_SIZE, MARGIN / 2);

    // Initial window = LVGL size (1:1), SDL can scale up
    int initialWindowSize = LVGL_SIZE;

    // ══════════════════════════════════════════════════════════════
    // SDL + LVGL (fixed render size, zoom for window scaling)
    // ══════════════════════════════════════════════════════════════

    oc::ui::lvgl::SdlBridge bridge(
        LVGL_SIZE, LVGL_SIZE,  // Fixed LVGL size (~1053)
        oc::hal::desktop::defaultTimeProvider,
        {.windowTitle = "MIDI Studio", .createInputDevices = true}
    );
    bridge.init();

    // Make window resizable with minimum size = LVGL size (1:1)
    // This ensures mouse coordinates are always valid (>= LVGL resolution)
    SDL_Window* window = bridge.getWindow();
    SDL_Renderer* renderer = bridge.getRenderer();
    if (window) {
        SDL_SetWindowResizable(window, SDL_TRUE);
        SDL_SetWindowMinimumSize(window, LVGL_SIZE, LVGL_SIZE);
    }

    // Set logical size for SDL scaling (GPU handles upscaling, LVGL stays at fixed size)
    if (renderer) {
        SDL_RenderSetLogicalSize(renderer, LVGL_SIZE, LVGL_SIZE);
    }

    // Dark background for the window
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // ══════════════════════════════════════════════════════════════
    // HwSimulator (LVGL-based)
    // ══════════════════════════════════════════════════════════════

    desktop::HwSimulator hwSim(screen);
    hwSim.init(layout);

    // ══════════════════════════════════════════════════════════════
    // InputMapper - Configuration
    // ══════════════════════════════════════════════════════════════

    oc::hal::desktop::InputMapper input;

    // Connect HwSimulator to InputMapper (LVGL widgets → App)
    hwSim.setInputMapper(&input);

    // Visual feedback (App → HwSimulator)
    input.setButtonFeedback([&hwSim](oc::hal::ButtonID id, bool pressed) {
        hwSim.setButtonPressed(id, pressed);
    });
    input.setEncoderFeedback([&hwSim](oc::hal::EncoderID id, float value) {
        hwSim.setEncoderValue(id, value);
    });

    // Keyboard shortcuts only (mouse is handled by LVGL widgets)
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

    desktop::MemoryStorage storage;
    core::state::CoreState coreState(storage);

    // Build the app
    oc::app::OpenControlApp app = oc::hal::desktop::AppBuilder(input)
                                      .controllers()
                                      .midi()
                                      .inputConfig(Config::Input::CONFIG);

    core::app::registerContexts(app, coreState);
    app.begin();

    // Reparent app UI into the screen area
    lv_obj_t* screenArea = hwSim.getScreenArea();
    lv_obj_t* appUI = lv_obj_get_child(screen, 1);  // First child after panel
    if (appUI && appUI != hwSim.getPanel()) {
        lv_obj_set_parent(appUI, screenArea);
        lv_obj_set_pos(appUI, 0, 0);
        lv_obj_set_size(appUI, layout.screenW, layout.screenH);
    }

    // ══════════════════════════════════════════════════════════════
    // Main loop
    // ══════════════════════════════════════════════════════════════

    bool running = true;

    while (running) {
        // --- Events ---
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Forward to LVGL for widget interaction
            lv_sdl_mouse_handler(&event);

            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (event.type == SDL_WINDOWEVENT &&
                     event.window.event == SDL_WINDOWEVENT_RESIZED) {
                // Force LVGL to redraw on window resize
                // SDL_RenderSetLogicalSize handles the scaling, but we need to trigger a refresh
                lv_obj_invalidate(lv_screen_active());
            }
            else if (event.type == SDL_MOUSEWHEEL) {
                // Mouse wheel over encoders
                // Convert SDL window coords to LVGL logical coords
                int sdlX, sdlY;
                SDL_GetMouseState(&sdlX, &sdlY);
                float lvglX, lvglY;
                SDL_RenderWindowToLogical(renderer, sdlX, sdlY, &lvglX, &lvglY);
                hwSim.handleMouseWheel(static_cast<int>(lvglX), static_cast<int>(lvglY), event.wheel.y);
            }

            input.handleEvent(event);
        }

        // --- Update ---
        app.update();
        coreState.update();

        // --- Render ---
        bridge.refresh();
        SDL_Delay(1);
    }

    // --- Cleanup ---
    SDL_Quit();
    return 0;
}
