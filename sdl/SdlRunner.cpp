/**
 * @file SdlRunner.cpp
 * @brief SDL runtime implementation
 */

#include "SdlRunner.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>

// LVGL SDL driver
extern "C" {
    void lv_sdl_mouse_handler(SDL_Event* event);
}

// Framework
#include <oc/hal/sdl/Sdl.hpp>
#include <oc/hal/midi/LibreMidiTransport.hpp>
#include <oc/ui/lvgl/SdlBridge.hpp>

// Application
#include "app/AppLogic.hpp"
#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include "state/CoreState.hpp"
#include "MemoryStorage.hpp"
#include "HwLayout.hpp"
#include "HwSimulator.hpp"

SdlRunner::SdlRunner() = default;

SdlRunner::~SdlRunner() {
    shutdown();
}

bool SdlRunner::init(int argc, char** argv) {
    using namespace Config;

    // Parse window size from args (used by HTML/JS for responsive sizing)
    int windowSize = 1053;
    if (argc > 1) {
        windowSize = std::atoi(argv[1]);
        if (windowSize < 400) windowSize = 1053;
    }

    // SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    // Layout
    constexpr int SCREEN_W = 320;
    constexpr float PANEL_TO_SCREEN_RATIO = 190.0f / 60.0f;
    constexpr int PANEL_SIZE = static_cast<int>(SCREEN_W * PANEL_TO_SCREEN_RATIO);
    constexpr int MARGIN = 40;
    const int LVGL_SIZE = PANEL_SIZE + MARGIN;
    auto layout = desktop::HwLayout::fit(LVGL_SIZE, MARGIN / 2);

    // LVGL + SDL Bridge
    bridge_ = std::make_unique<oc::ui::lvgl::SdlBridge>(
        LVGL_SIZE, LVGL_SIZE,
        oc::hal::sdl::defaultTimeProvider,
        oc::ui::lvgl::SdlBridgeConfig{.windowTitle = "MIDI Studio", .createInputDevices = true}
    );
    bridge_->init();
    renderer_ = bridge_->getRenderer();

    if (renderer_) {
        SDL_RenderSetLogicalSize(renderer_, LVGL_SIZE, LVGL_SIZE);
    }

    // Dark background
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // Hardware simulator
    hwSim_ = std::make_unique<desktop::HwSimulator>(screen);
    hwSim_->init(layout);

    // Input
    input_ = std::make_unique<oc::hal::sdl::InputMapper>();
    hwSim_->setInputMapper(input_.get());

    input_->setButtonFeedback([this](oc::hal::ButtonID id, bool pressed) {
        hwSim_->setButtonPressed(id, pressed);
    });
    input_->setEncoderFeedback([this](oc::hal::EncoderID id, float value) {
        hwSim_->setEncoderValue(id, value);
    });

    // Keyboard mappings
    input_->button(SDLK_ESCAPE, static_cast<oc::hal::ButtonID>(ButtonID::LEFT_TOP))
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

    // Application
    storage_ = std::make_unique<desktop::MemoryStorage>();
    coreState_ = std::make_unique<core::state::CoreState>(*storage_);

    app_ = std::make_unique<oc::app::OpenControlApp>(
        oc::hal::sdl::AppBuilder()
            .midi(std::make_unique<oc::hal::midi::LibreMidiTransport>(
                oc::hal::midi::LibreMidiConfig{
                    .appName = "MIDI Studio",
                    .inputPortPattern = "IN [core-desktop]",
                    .outputPortPattern = "OUT [core-desktop]"
                }))
            .controllers(*input_)
            .inputConfig(Config::Input::CONFIG)
    );

    core::app::registerContexts(*app_, *coreState_);
    app_->begin();

    // Reparent app UI into screen area
    lv_obj_t* screenArea = hwSim_->getScreenArea();
    lv_obj_t* appUI = lv_obj_get_child(screen, 1);
    if (appUI && appUI != hwSim_->getPanel()) {
        lv_obj_set_parent(appUI, screenArea);
        lv_obj_set_pos(appUI, 0, 0);
        lv_obj_set_size(appUI, layout.screenW, layout.screenH);
    }

    running_ = true;
    return true;
}

bool SdlRunner::tick() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        lv_sdl_mouse_handler(&event);

        if (event.type == SDL_QUIT) {
            running_ = false;
            return false;
        }
        
        if (event.type == SDL_WINDOWEVENT && 
            event.window.event == SDL_WINDOWEVENT_RESIZED) {
            lv_obj_invalidate(lv_screen_active());
        }
        
        if (event.type == SDL_MOUSEWHEEL) {
            int sdlX, sdlY;
            SDL_GetMouseState(&sdlX, &sdlY);
            float lvglX, lvglY;
            SDL_RenderWindowToLogical(renderer_, sdlX, sdlY, &lvglX, &lvglY);
            hwSim_->handleMouseWheel(static_cast<int>(lvglX), static_cast<int>(lvglY), event.wheel.y);
        }

        input_->handleEvent(event);
    }

    app_->update();
    coreState_->update();
    bridge_->refresh();

    return running_;
}

void SdlRunner::shutdown() {
    app_.reset();
    coreState_.reset();
    storage_.reset();
    input_.reset();
    hwSim_.reset();
    bridge_.reset();
    
    SDL_Quit();
    running_ = false;
}
