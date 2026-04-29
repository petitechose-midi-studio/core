/**
 * @file SdlEnvironment.cpp
 * @brief SDL environment implementation
 */

#include "SdlEnvironment.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>

// LVGL SDL driver
extern "C" {
    void lv_sdl_mouse_handler(SDL_Event* event);
}

#include <oc/hal/sdl/Sdl.hpp>
#include <oc/ui/lvgl/SdlBridge.hpp>
#include <oc/ui/lvgl/Screen.hpp>

#include <config/InputIDs.hpp>
#include "HwLayout.hpp"
#include "HwSimulator.hpp"

namespace sdl {

SdlEnvironment::SdlEnvironment() = default;

SdlEnvironment::~SdlEnvironment() {
    shutdown();
}

bool SdlEnvironment::init(int argc, char** argv) {
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
    
    layout_ = std::make_unique<desktop::HwLayout>(desktop::HwLayout::fit(LVGL_SIZE, MARGIN / 2));

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
    hwSim_->init(*layout_);

    // Configure Screen root for app UI (contexts will use Screen::root())
    oc::ui::lvgl::Screen::setRoot(hwSim_->getScreenArea());

    // Input mapper
    input_ = std::make_unique<oc::hal::sdl::InputMapper>();
    hwSim_->setInputMapper(input_.get());

    input_->setButtonFeedback([this](oc::type::ButtonID id, bool pressed) {
        hwSim_->setButtonPressed(id, pressed);
    });
    input_->setEncoderFeedback([this](oc::type::EncoderID id, float value) {
        hwSim_->setEncoderValue(id, value);
    });

    setupKeyboardMappings();

    running_ = true;
    return true;
}

void SdlEnvironment::setupKeyboardMappings() {
    using namespace Config;

    input_->button(SDLK_ESCAPE, static_cast<oc::type::ButtonID>(ButtonID::LEFT_TOP))
        // Use physical scancodes for the left navigation column so the mapping
        // stays usable across keyboard layouts (AZERTY/QWERTY).
        .buttonScancode(SDL_SCANCODE_Q, static_cast<oc::type::ButtonID>(ButtonID::LEFT_CENTER))
        .buttonScancode(SDL_SCANCODE_A, static_cast<oc::type::ButtonID>(ButtonID::LEFT_BOTTOM))
        .button(SDLK_COMMA, static_cast<oc::type::ButtonID>(ButtonID::BOTTOM_LEFT))
        .button(SDLK_PERIOD, static_cast<oc::type::ButtonID>(ButtonID::BOTTOM_CENTER))
        .button(SDLK_SLASH, static_cast<oc::type::ButtonID>(ButtonID::BOTTOM_RIGHT))
        .button(SDLK_1, static_cast<oc::type::ButtonID>(ButtonID::MACRO_1))
        .button(SDLK_2, static_cast<oc::type::ButtonID>(ButtonID::MACRO_2))
        .button(SDLK_3, static_cast<oc::type::ButtonID>(ButtonID::MACRO_3))
        .button(SDLK_4, static_cast<oc::type::ButtonID>(ButtonID::MACRO_4))
        .button(SDLK_5, static_cast<oc::type::ButtonID>(ButtonID::MACRO_5))
        .button(SDLK_6, static_cast<oc::type::ButtonID>(ButtonID::MACRO_6))
        .button(SDLK_7, static_cast<oc::type::ButtonID>(ButtonID::MACRO_7))
        .button(SDLK_8, static_cast<oc::type::ButtonID>(ButtonID::MACRO_8))
        .button(SDLK_SPACE, static_cast<oc::type::ButtonID>(ButtonID::NAV))
        .encoder(SDLK_DOWN, SDLK_UP, static_cast<oc::type::EncoderID>(EncoderID::NAV), 0.05f)
        .encoder(SDLK_LEFT, SDLK_RIGHT, static_cast<oc::type::EncoderID>(EncoderID::OPT), 0.02f);
}

bool SdlEnvironment::processEvents() {
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
            hwSim_->handleMouseWheel(static_cast<int>(lvglX), static_cast<int>(lvglY), -event.wheel.y);
        }

        input_->handleEvent(event);
    }

    return running_;
}

void SdlEnvironment::refresh() {
    bridge_->refresh();
}

bool SdlEnvironment::saveScreenshotBmp(const char* path, ScreenshotScope scope) {
    if (!renderer_ || !path || path[0] == '\0') return false;

    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(renderer_, &width, &height) != 0 || width <= 0 || height <= 0) {
        return false;
    }

    SDL_Rect sourceRect{0, 0, width, height};
    if (scope == ScreenshotScope::Screen && layout_) {
        const int logicalSize = layout_->panelSize + 40;
        const int panelOffset = (logicalSize - layout_->panelSize) / 2;
        const float scaleX = static_cast<float>(width) / static_cast<float>(logicalSize);
        const float scaleY = static_cast<float>(height) / static_cast<float>(logicalSize);
        sourceRect.x = static_cast<int>((panelOffset + layout_->screenX) * scaleX);
        sourceRect.y = static_cast<int>((panelOffset + layout_->screenY) * scaleY);
        sourceRect.w = static_cast<int>(layout_->screenW * scaleX);
        sourceRect.h = static_cast<int>(layout_->screenH * scaleY);
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0,
        sourceRect.w,
        sourceRect.h,
        32,
        SDL_PIXELFORMAT_ARGB8888
    );
    if (!surface) return false;

    const int readResult = SDL_RenderReadPixels(
        renderer_,
        &sourceRect,
        SDL_PIXELFORMAT_ARGB8888,
        surface->pixels,
        surface->pitch
    );
    const int saveResult = (readResult == 0) ? SDL_SaveBMP(surface, path) : -1;
    SDL_FreeSurface(surface);
    return readResult == 0 && saveResult == 0;
}

void SdlEnvironment::shutdown() {
    input_.reset();
    hwSim_.reset();
    layout_.reset();
    bridge_.reset();

    SDL_Quit();
    running_ = false;
}

lv_obj_t* SdlEnvironment::getScreenArea() const {
    return hwSim_ ? hwSim_->getScreenArea() : nullptr;
}

}  // namespace sdl
