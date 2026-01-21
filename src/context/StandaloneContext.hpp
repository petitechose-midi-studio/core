#pragma once

/**
 * @file StandaloneContext.hpp
 * @brief Main context for standalone operation mode
 *
 * StandaloneContext manages the lifecycle and wires together:
 * - CoreState: reactive state for all application data
 * - Handlers: input bindings for encoders/buttons
 * - Views: UI components that subscribe to state
 *
 * ## Architecture
 *
 * ```
 * StandaloneContext
 *     ├── CoreState (reactive state - single source of truth, external)
 *     ├── ViewContainer (2-zone layout: main + bottom)
 *     ├── Handlers
 *     │   ├── MacroValueHandler (encoder → MIDI CC out)
 *     │   ├── MacroMidiHandler (MIDI CC in → state)
 *     │   ├── MacroEditHandler (overlay editing)
 *     │   └── TransportHandler (transport controls)
 *     ├── Views
 *     │   ├── MacroView (main zone, owns TopBar)
 *     │   └── TransportBar (bottom zone)
 *     └── Overlays (managed by OverlayManager)
 *         └── MacroEditOverlay (edit CH/CC for a macro)
 * ```
 *
 * The context itself is thin - handlers and views do the work.
 * CoreState is received from main.cpp (survives context switches).
 */

#include <memory>

#include <oc/context/ContextBase.hpp>
#include <oc/context/Requirements.hpp>
#include <oc/state/SignalWatcher.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"

// Forward declarations
namespace core::handler {
class MacroValueHandler;
class MacroMidiHandler;
class MacroEditHandler;
class TransportHandler;
}  // namespace core::handler

namespace core::ui {
class ViewContainer;
class MacroView;
class TransportBar;
class MacroEditOverlay;
}  // namespace core::ui

namespace core::state {
template<typename T> class OverlayManager;
}  // namespace core::state

namespace core::context {

/**
 * @brief Standalone mode context
 *
 * 8 macro knobs with:
 * - Encoder → MIDI CC out
 * - MIDI CC in → state + encoder sync
 *
 * Receives CoreState reference (state survives context switches).
 */
class StandaloneContext : public oc::context::ContextBase {
public:
    static constexpr oc::context::Requirements REQUIRES{
        .button = true,
        .encoder = true,
        .midi = true
    };

    /**
     * @brief Construct with external CoreState reference
     * @param state Reference to global CoreState (owned by main.cpp)
     */
    explicit StandaloneContext(core::state::CoreState& state);

    ~StandaloneContext() override;

    // Non-copyable, non-movable
    StandaloneContext(const StandaloneContext&) = delete;
    StandaloneContext& operator=(const StandaloneContext&) = delete;
    StandaloneContext(StandaloneContext&&) = delete;
    StandaloneContext& operator=(StandaloneContext&&) = delete;

    // IContext interface
    oc::type::Result<void> init() override;
    void update() override;
    void cleanup() override;
    const char* getName() const override { return "Standalone"; }

private:
    void syncEncodersFromState();
    void setupMacroEditRendering();
    void renderMacroEdit();

    core::state::CoreState& core_state_;  // External reference (survives context switches)

    // UI containers
    std::unique_ptr<core::ui::ViewContainer> view_container_;
    std::unique_ptr<core::ui::MacroView> view_;
    std::unique_ptr<core::ui::TransportBar> transport_bar_;

    // Overlay system
    std::unique_ptr<core::state::OverlayManager<core::ui::OverlayType>> overlay_controller_;
    std::unique_ptr<core::ui::MacroEditOverlay> macro_edit_overlay_;
    oc::state::SignalWatcher macro_edit_watcher_;

    // Handlers
    std::unique_ptr<core::handler::MacroValueHandler> input_handler_;
    std::unique_ptr<core::handler::MacroMidiHandler> midi_handler_;
    std::unique_ptr<core::handler::TransportHandler> transport_handler_;
    std::unique_ptr<core::handler::MacroEditHandler> macro_edit_handler_;
};

}  // namespace core::context
