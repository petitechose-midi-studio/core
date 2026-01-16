/**
 * @file SdlRunner.hpp
 * @brief SDL runtime for MIDI Studio (native and WASM)
 */

#pragma once

#include <memory>

// Forward declarations
struct SDL_Renderer;
namespace oc::ui::lvgl { class SdlBridge; }
namespace oc::hal::sdl { class InputMapper; }
namespace oc::app { class OpenControlApp; }
namespace core::state { class CoreState; }
namespace desktop { class HwSimulator; class MemoryStorage; }

/**
 * @brief SDL-based application runner
 * 
 * Encapsulates SDL initialization, main loop iteration, and cleanup.
 * Used by both native (while loop) and WASM (emscripten_set_main_loop) builds.
 */
class SdlRunner {
public:
    SdlRunner();
    ~SdlRunner();

    // Non-copyable, non-movable (owns resources)
    SdlRunner(const SdlRunner&) = delete;
    SdlRunner& operator=(const SdlRunner&) = delete;

    /**
     * @brief Initialize SDL, LVGL, and application
     * @return true on success
     */
    bool init(int argc, char** argv);

    /**
     * @brief Execute one frame (events, update, render)
     * @return true if running, false if quit requested
     */
    bool tick();

    /**
     * @brief Cleanup all resources
     */
    void shutdown();

    [[nodiscard]] bool isRunning() const { return running_; }

private:
    // Pimpl-style storage (avoids exposing implementation details)
    std::unique_ptr<oc::ui::lvgl::SdlBridge> bridge_;
    std::unique_ptr<oc::hal::sdl::InputMapper> input_;
    std::unique_ptr<desktop::MemoryStorage> storage_;
    std::unique_ptr<core::state::CoreState> coreState_;
    std::unique_ptr<desktop::HwSimulator> hwSim_;
    std::unique_ptr<oc::app::OpenControlApp> app_;
    
    SDL_Renderer* renderer_ = nullptr;
    bool running_ = false;
};
