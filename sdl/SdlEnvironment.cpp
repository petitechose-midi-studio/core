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

    if (bridge_->getDisplay()) {
        lv_display_add_event_cb(
            bridge_->getDisplay(),
            displayFlushStartCb,
            LV_EVENT_FLUSH_START,
            this
        );
    }

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

void SdlEnvironment::displayFlushStartCb(lv_event_t* event) {
    auto* self = static_cast<SdlEnvironment*>(lv_event_get_user_data(event));
    if (!self || !self->bridge_ || !self->bridge_->getDisplay()) return;

    // FLUSH_START is sent synchronously before LVGL rotates a double buffer,
    // so buf_active is the complete frame being presented, not the next draw
    // buffer. Drawing and capture both run on this thread.
    self->lastFlushedBuffer_ =
        lv_display_get_buf_active(self->bridge_->getDisplay());
}

bool SdlEnvironment::saveScreenshotBmp(const char* path, ScreenshotScope scope) {
    if (!path || path[0] == '\0' || !lastFlushedBuffer_ ||
        !lastFlushedBuffer_->data) {
        return false;
    }

    const lv_image_header_t& header = lastFlushedBuffer_->header;
    const auto colorFormat = static_cast<lv_color_format_t>(header.cf);
    Uint32 sdlPixelFormat = SDL_PIXELFORMAT_UNKNOWN;
    switch (colorFormat) {
        case LV_COLOR_FORMAT_XRGB8888:
            sdlPixelFormat = SDL_PIXELFORMAT_XRGB8888;
            break;
        case LV_COLOR_FORMAT_ARGB8888:
            sdlPixelFormat = SDL_PIXELFORMAT_ARGB8888;
            break;
        default:
            // The desktop LVGL configuration is 32-bit. Failing explicitly is
            // safer than falling back to SDL_RenderReadPixels after Present,
            // whose result is backend-dependent and can be a stale backbuffer.
            return false;
    }

    const int width = static_cast<int>(header.w);
    const int height = static_cast<int>(header.h);
    const int pitch = static_cast<int>(header.stride);
    if (width <= 0 || height <= 0 || pitch <= 0) return false;

    SDL_Rect sourceRect{0, 0, width, height};
    if (scope == ScreenshotScope::Screen && layout_) {
        const int panelOffsetX = (width - layout_->panelSize) / 2;
        const int panelOffsetY = (height - layout_->panelSize) / 2;
        sourceRect.x = panelOffsetX + layout_->screenX;
        sourceRect.y = panelOffsetY + layout_->screenY;
        sourceRect.w = layout_->screenW;
        sourceRect.h = layout_->screenH;
    }

    if (sourceRect.x < 0 || sourceRect.y < 0 || sourceRect.w <= 0 ||
        sourceRect.h <= 0 || sourceRect.x + sourceRect.w > width ||
        sourceRect.y + sourceRect.h > height) {
        return false;
    }

    SDL_Surface* source = SDL_CreateRGBSurfaceWithFormatFrom(
        lastFlushedBuffer_->data,
        width,
        height,
        32,
        pitch,
        sdlPixelFormat
    );
    if (!source) return false;
    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);

    SDL_Surface* capture = SDL_CreateRGBSurfaceWithFormat(
        0,
        sourceRect.w,
        sourceRect.h,
        32,
        SDL_PIXELFORMAT_ARGB8888
    );
    if (!capture) {
        SDL_FreeSurface(source);
        return false;
    }

    const int blitResult = SDL_BlitSurface(source, &sourceRect, capture, nullptr);
    const int saveResult = blitResult == 0 ? SDL_SaveBMP(capture, path) : -1;
    SDL_FreeSurface(capture);
    SDL_FreeSurface(source);
    return blitResult == 0 && saveResult == 0;
}

void SdlEnvironment::shutdown() {
    input_.reset();
    hwSim_.reset();
    layout_.reset();
    lastFlushedBuffer_ = nullptr;
    bridge_.reset();

    SDL_Quit();
    running_ = false;
}

lv_obj_t* SdlEnvironment::getScreenArea() const {
    return hwSim_ ? hwSim_->getScreenArea() : nullptr;
}

}  // namespace sdl
