#pragma once

/**
 * @file StandaloneContext.hpp
 * @brief Main context for standalone operation mode
 *
 * StandaloneContext manages the lifecycle and top-level assembly order.
 *
 * ## Architecture
 *
 * ```
 * StandaloneContext
 *     ├── CoreState (reactive state - single source of truth, external)
 *     ├── StandaloneUiAssembly (container, views, bottom bars)
 *     ├── StandaloneOverlayAssembly (overlay manager, view selector)
 *     ├── StandaloneFeatureAssembly (macro, sequencer, settings modules)
 *     └── StandaloneGlobalHandlerAssembly (transport, view switching)
 * ```
 *
 * The context itself stays focused on lifecycle and assembly order.
 * CoreState is received from main.cpp (survives context switches).
 */

#include <memory>

#include <oc/context/ContextBase.hpp>
#include <oc/context/Requirements.hpp>
#include <oc/state/SignalWatcher.hpp>

namespace core::state {
struct CoreState;
}

namespace core::context::standalone {
class StandaloneFeatureAssembly;
class StandaloneGlobalHandlerAssembly;
class StandaloneOverlayAssembly;
class StandaloneUiAssembly;
}

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
    void createUiAssembly();
    void createOverlayAssembly();
    void createFeatureAssembly();
    void createGlobalHandlerAssembly();
    void registerMidiRouting();
    void cleanupGlobalHandlerAssembly();
    void cleanupFeatureAssembly();
    void cleanupOverlayAssembly();
    void cleanupUiAssembly();

    void syncEncodersFromState();
    void setupViewSelectorRendering();
    void setupActiveViewSwitching();
    void applyActiveView();
    oc::type::ScopeID activeViewScopeId() const;

    core::state::CoreState& core_state_;  // External reference (survives context switches)

    std::unique_ptr<core::context::standalone::StandaloneUiAssembly> ui_assembly_;
    std::unique_ptr<core::context::standalone::StandaloneOverlayAssembly> overlay_assembly_;
    std::unique_ptr<core::context::standalone::StandaloneFeatureAssembly> feature_assembly_;
    std::unique_ptr<core::context::standalone::StandaloneGlobalHandlerAssembly>
        global_handler_assembly_;
    oc::state::SignalWatcher view_selector_watcher_;
    oc::state::SignalWatcher active_view_watcher_;
};

}  // namespace core::context
