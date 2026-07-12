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
 * The standalone sequencer runtime is owned outside this context from the
 * app pre-context hook, so UI/context updates never become its execution path.
 * CoreState is received from main.cpp (survives context switches).
 */

#include <memory>

#include "app/ExtmemAllocator.hpp"
#include <oc/context/ContextBase.hpp>
#include <oc/context/Requirements.hpp>
#include <oc/state/StaticSignalWatcher.hpp>

#if defined(MS_UX_RECORDER)
#include "validation/ux/SemanticUxSurface.hpp"
#endif

namespace core::state {
struct CoreState;
}

namespace core::persistence {
class ProductFileService;
}

namespace core::ui {
class CoalescedLvglRenderScheduler;
}

namespace core::protocol::filesystem {
class FileSystemRpcEndpoint;
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
        .midi = true,
        .frames = true
    };

    /**
     * @brief Construct with external CoreState reference
     * @param state Reference to global CoreState (owned by main.cpp)
     */
    StandaloneContext(core::state::CoreState& state,
                      core::persistence::ProductFileService& productFiles);

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
    bool createUiAssembly();
    bool createOverlayAssembly();
    bool createFeatureAssembly();
    bool createGlobalHandlerAssembly();
    bool createFileSystemRpcEndpoint();
    void registerMidiRouting();
    void cleanupFileSystemRpcEndpoint();
    void cleanupGlobalHandlerAssembly();
    void cleanupFeatureAssembly();
    void cleanupOverlayAssembly();
    void cleanupUiAssembly();

    void syncEncodersFromState();
    bool setupViewSelectorRendering();
    void requestViewSelectorRender();
    void renderViewSelectorProjection();
    void syncViewSelectorChrome();
    static void drainViewSelectorRender(void* context, uint32_t flags);
    bool setupActiveViewSwitching();
    void applyActiveView();
    oc::type::ScopeID activeViewScopeId() const;

    core::state::CoreState& core_state_;  // External reference (survives context switches)
    core::persistence::ProductFileService& product_files_;

#if defined(MS_UX_RECORDER)
    core::validation::ux::SemanticUxSurfaceRegistry ux_surface_registry_;
#endif

    core::app::ExtmemUniquePtr<core::context::standalone::StandaloneUiAssembly> ui_assembly_;
    core::app::ExtmemUniquePtr<core::context::standalone::StandaloneOverlayAssembly> overlay_assembly_;
    core::app::ExtmemUniquePtr<core::ui::CoalescedLvglRenderScheduler>
        view_selector_render_scheduler_;
    core::app::ExtmemUniquePtr<core::context::standalone::StandaloneFeatureAssembly> feature_assembly_;
    core::app::ExtmemUniquePtr<core::context::standalone::StandaloneGlobalHandlerAssembly>
        global_handler_assembly_;
    core::app::ExtmemUniquePtr<core::protocol::filesystem::FileSystemRpcEndpoint>
        filesystem_rpc_endpoint_;
    oc::state::StaticWatchGroup<2> view_selector_watcher_;
    oc::state::StaticWatchGroup<1> active_view_watcher_;
};

}  // namespace core::context
