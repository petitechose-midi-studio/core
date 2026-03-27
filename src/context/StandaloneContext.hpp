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
 *         └── MacroEdit VirtualList overlays (property + selectors)
 * ```
 *
 * The context itself is thin - handlers and views do the work.
 * CoreState is received from main.cpp (survives context switches).
 */

#include <memory>
#include <array>

#include <oc/context/ContextBase.hpp>
#include <oc/context/Requirements.hpp>
#include <oc/state/SignalWatcher.hpp>

#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"

namespace ms::ui {
class ViewContainer;
class StringListSelector;
}  // namespace ms::ui

// Forward declarations
namespace core::handler {
class TransportHandler;
class ViewSwitcherHandler;
}  // namespace core::handler

namespace core::context::standalone {
class MacroFeatureModule;
class SequencerFeatureModule;
class SettingsFeatureModule;
}

namespace core::ui {
class MacroView;
class SequencerView;
class TransportBar;
class ContextSoftkeyBar;
}  // namespace core::ui

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace oc::context {
template <typename T>
class OverlayManager;
}  // namespace oc::context

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
    const char* getName() const override { return "Standalone"; }

protected:
    void onCleanup() override;

private:
    void configureEncoders();
    void createViewContainer();
    void createViews();
    void createBottomBar();
    void createOverlayController();
    void createViewSelectorOverlay();
    void createFeatureModules();
    void createGlobalHandlers();
    void resetTransientUiState();
    void cleanupGlobalHandlers();
    void cleanupFeatureModules();
    void cleanupOverlayController();
    void cleanupViews();

    void syncEncodersFromState();
    void setupViewSelectorRendering();
    void renderViewSelector();
    void setupActiveViewSwitching();
    void applyActiveView();

    core::state::CoreState& core_state_;  // External reference (survives context switches)

    // UI containers
    std::unique_ptr<ms::ui::ViewContainer> view_container_;
    std::unique_ptr<core::ui::MacroView> macro_view_;
    std::unique_ptr<core::ui::SequencerView> sequencer_view_;
    std::unique_ptr<core::ui::TransportBar> transport_bar_;
    std::unique_ptr<core::ui::ContextSoftkeyBar> context_softkey_bar_;

    // Overlay system
    std::unique_ptr<oc::context::OverlayManager<core::ui::OverlayType>> overlay_controller_;
    std::unique_ptr<ms::ui::StringListSelector> view_selector_;
    oc::state::SignalWatcher view_selector_watcher_;
    oc::state::SignalWatcher active_view_watcher_;
    std::unique_ptr<core::context::standalone::MacroFeatureModule> macro_feature_;
    std::unique_ptr<core::context::standalone::SequencerFeatureModule> sequencer_feature_;
    std::unique_ptr<core::context::standalone::SettingsFeatureModule> settings_feature_;

    // Global handlers
    std::unique_ptr<core::handler::TransportHandler> transport_handler_;
    std::unique_ptr<core::handler::ViewSwitcherHandler> view_switcher_handler_;
};

}  // namespace core::context
