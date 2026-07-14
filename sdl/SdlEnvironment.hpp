#pragma once

/**
 * @file SdlEnvironment.hpp
 * @brief SDL environment management (SRP: only SDL/LVGL/HwSimulator)
 *
 * Responsible for:
 * - SDL initialization and cleanup
 * - LVGL SdlBridge lifecycle
 * - HwSimulator visual representation
 * - InputMapper with keyboard bindings
 * - Event processing
 *
 * NOT responsible for (caller's job):
 * - Storage/State creation
 * - App creation via AppBuilder
 * - Context registration
 * - Main loop logic
 */

#include <memory>
#include <functional>
#include <lvgl.h>

struct SDL_Renderer;

namespace oc::ui::lvgl { class SdlBridge; }
namespace oc::hal::sdl { class InputMapper; }
namespace desktop { class HwSimulator; struct HwLayout; }

namespace sdl {

enum class ScreenshotScope {
    Controller,
    Screen,
};

/**
 * @brief SDL environment for MIDI Studio
 *
 * Usage:
 * @code
 * SdlEnvironment env;
 * if (!env.init(argc, argv)) return 1;
 *
 * auto app = oc::hal::sdl::AppBuilder()
 *     .midi(...)
 *     .controllers(env.inputMapper())
 *     .inputConfig(...);
 *
 * registerContexts(app);
 * app.begin();
 *
 * while (env.processEvents()) {
 *     app.update();
 *     // optional: state.update();
 *     env.refresh();
 * }
 *
 * env.shutdown();
 * @endcode
 */
class SdlEnvironment {
public:
    SdlEnvironment();
    ~SdlEnvironment();

    // Non-copyable, non-movable
    SdlEnvironment(const SdlEnvironment&) = delete;
    SdlEnvironment& operator=(const SdlEnvironment&) = delete;

    /**
     * @brief Initialize SDL, LVGL bridge, HwSimulator, InputMapper
     * @param argc Command line argument count
     * @param argv Command line arguments (argv[1] = window size)
     * @return true on success
     */
    bool init(int argc, char** argv);

    /**
     * @brief Process SDL events
     *
     * Handles:
     * - Window close (returns false)
     * - Window resize
     * - Mouse wheel (encoder simulation)
     * - Keyboard/mouse input via InputMapper
     *
     * @return true if running, false if quit requested
     */
    bool processEvents();

    /**
     * @brief Refresh LVGL display
     *
     * Call after app.update() in main loop.
     */
    void refresh();

    /**
     * @brief Save the current SDL renderer contents to a BMP file.
     */
    bool saveScreenshotBmp(const char* path, ScreenshotScope scope = ScreenshotScope::Controller);

    /**
     * @brief Cleanup all resources
     */
    void shutdown();

    /**
     * @brief Get InputMapper for AppBuilder
     * @return Reference to InputMapper (valid after init)
     */
    oc::hal::sdl::InputMapper& inputMapper() { return *input_; }

    [[nodiscard]] bool isRunning() const { return running_; }

private:
    static void displayFlushStartCb(lv_event_t* event);
    void setupKeyboardMappings();
    lv_obj_t* getScreenArea() const;

    std::unique_ptr<oc::ui::lvgl::SdlBridge> bridge_;
    std::unique_ptr<oc::hal::sdl::InputMapper> input_;
    std::unique_ptr<desktop::HwSimulator> hwSim_;
    std::unique_ptr<desktop::HwLayout> layout_;

    SDL_Renderer* renderer_ = nullptr;
    // SDL's renderer backbuffer is undefined after SDL_RenderPresent(). Keep
    // the full LVGL buffer that was actually handed to the display driver so
    // deterministic captures never read a recycled swapchain image.
    lv_draw_buf_t* lastFlushedBuffer_ = nullptr;
    bool running_ = false;
};

}  // namespace sdl
